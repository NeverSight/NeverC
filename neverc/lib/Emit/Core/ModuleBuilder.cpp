#include "neverc/Emit/Core/ModuleBuilder.h"
#include "Core/ModuleEmitter.h"
#include "Debug/DebugEmitterInfo.h"
#include "neverc/Foundation/LangOpts/CodeGenOptions.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <memory>

using namespace neverc;
using namespace Emit;

// ===----------------------------------------------------------------------===
// CodeGeneratorImpl
// ===----------------------------------------------------------------------===

namespace {
class CodeGeneratorImpl : public IRGenerator {
  DiagnosticsEngine &Diags;
  TreeContext *Ctx;
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem>
      FS;                                  // Only used for debug info.
  const HeaderIndexOptions &HeaderIdxOpts; // Only used for debug info.
  const PrepOptions &PrepOpts;
  const CodeGenOptions &CodeGenOpts;

  unsigned HandlingTopLevelDecls;

  struct HandlingTopLevelDeclRAII {
    CodeGeneratorImpl &Self;
    bool genDeferred;
    HandlingTopLevelDeclRAII(CodeGeneratorImpl &Self, bool genDeferred = true)
        : Self(Self), genDeferred(genDeferred) {
      ++Self.HandlingTopLevelDecls;
    }
    ~HandlingTopLevelDeclRAII() {
      unsigned Level = --Self.HandlingTopLevelDecls;
      if (Level == 0 && genDeferred)
        Self.genDeferredDecls();
    }
  };

protected:
  std::unique_ptr<llvm::Module> M;
  std::unique_ptr<Emit::ModuleEmitter> Builder;

private:
  llvm::SmallVector<FunctionDecl *, 8> DeferredInlineFuncDefs;

  static llvm::StringRef ExpandModuleName(llvm::StringRef ModuleName,
                                          const CodeGenOptions &CGO) {
    if (ModuleName == "-" && !CGO.MainFileName.empty())
      return CGO.MainFileName;
    return ModuleName;
  }

public:
  CodeGeneratorImpl(DiagnosticsEngine &diags, llvm::StringRef ModuleName,
                    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS,
                    const HeaderIndexOptions &HSO, const PrepOptions &PPO,
                    const CodeGenOptions &CGO, llvm::LLVMContext &C)
      : Diags(diags), Ctx(nullptr), FS(std::move(FS)), HeaderIdxOpts(HSO),
        PrepOpts(PPO), CodeGenOpts(CGO), HandlingTopLevelDecls(0),
        M(new llvm::Module(ExpandModuleName(ModuleName, CGO), C)) {
    C.setDiscardValueNames(CGO.DiscardValueNames);
  }

  ~CodeGeneratorImpl() override {
    // There should normally not be any leftover inline function definitions.
    assert(DeferredInlineFuncDefs.empty() || Diags.hasErrorOccurred());
  }

  ModuleEmitter &ME() { return *Builder; }

  llvm::Module *getModule() { return M.get(); }

  DebugEmitter *getDebugEmitter() { return Builder->getModuleDebugInfo(); }

  llvm::Module *releaseModule() { return M.release(); }

  const Decl *getDeclForMangledName(llvm::StringRef MangledName) {
    GlobalDecl Result;
    if (!Builder->lookupRepresentativeDecl(MangledName, Result))
      return nullptr;
    const Decl *D = Result.getCanonicalDecl().getDecl();
    if (auto FD = dyn_cast<FunctionDecl>(D)) {
      if (FD->hasBody(FD))
        return FD;
    } else if (auto TD = dyn_cast<TagDecl>(D)) {
      if (auto Def = TD->getDefinition())
        return Def;
    }
    return D;
  }

  llvm::StringRef getMangledName(GlobalDecl GD) {
    return Builder->getMangledName(GD);
  }

  llvm::Constant *addrOfGlobal(GlobalDecl global, bool isForDefinition) {
    return Builder->addrOfGlobal(global, ForDefinition_t(isForDefinition));
  }

  llvm::Module *startModule(llvm::StringRef ModuleName, llvm::LLVMContext &C) {
    assert(!M && "Replacing existing Module?");
    M.reset(new llvm::Module(ExpandModuleName(ModuleName, CodeGenOpts), C));

    std::unique_ptr<ModuleEmitter> OldBuilder = std::move(Builder);

    Initialize(*Ctx);

    if (OldBuilder)
      OldBuilder->moveLazyEmissionStates(Builder.get());

    return M.get();
  }

  void Initialize(TreeContext &Context) override {
    Ctx = &Context;

    M->setTargetTriple(Ctx->getTargetInfo().getTriple().getTriple());
    M->setDataLayout(Ctx->getTargetInfo().getDataLayoutString());
    const auto &SDKVersion = Ctx->getTargetInfo().getSDKVersion();
    if (!SDKVersion.empty())
      M->setSDKVersion(SDKVersion);
    Builder.reset(new Emit::ModuleEmitter(Context, FS, HeaderIdxOpts, PrepOpts,
                                          CodeGenOpts, *M, Diags));

    for (auto &&Lib : CodeGenOpts.DependentLibraries)
      Builder->addDependentLib(Lib);
    for (auto &&Opt : CodeGenOpts.LinkerOptions)
      Builder->appendLinkerOptions(Opt);
  }

  bool ProcessTopLevelDecl(DeclGroupRef DG) override {
    if (Diags.hasErrorOccurred())
      return true;

    HandlingTopLevelDeclRAII HandlingDecl(*this);

    for (auto *I : DG)
      Builder->lowerTopLevel(I);

    return true;
  }

  void genDeferredDecls() {
    if (DeferredInlineFuncDefs.empty())
      return;

    // More defs may be added during this loop via TreeConsumer callbacks.
    HandlingTopLevelDeclRAII HandlingDecl(*this);
    for (unsigned I = 0; I != DeferredInlineFuncDefs.size(); ++I)
      Builder->lowerTopLevel(DeferredInlineFuncDefs[I]);
    DeferredInlineFuncDefs.clear();
  }

  void ProcessInlineFunctionDefinition(FunctionDecl *D) override {
    if (Diags.hasErrorOccurred())
      return;

    assert(D->doesThisDeclarationHaveABody());

    // Defer until linkage is finalized (enclosing struct may affect it).
    DeferredInlineFuncDefs.push_back(D);
  }

  void ProcessTagDeclDefinition(TagDecl *D) override {
    if (Diags.hasErrorOccurred())
      return;

    HandlingTopLevelDeclRAII HandlingDecl(*this, /*genDeferred=*/false);

    Builder->updateCompletedType(D);
  }

  void ProcessTagDeclRequiredDefinition(const TagDecl *D) override {
    if (Diags.hasErrorOccurred())
      return;

    HandlingTopLevelDeclRAII HandlingDecl(*this, /*genDeferred=*/false);

    if (Emit::DebugEmitter *DI = Builder->getModuleDebugInfo())
      if (const RecordDecl *RD = dyn_cast<RecordDecl>(D))
        DI->completeRequiredType(RD);
  }

  void ProcessTranslationUnit(TreeContext &Ctx) override {
    if (!Diags.hasErrorOccurred() && Builder)
      Builder->release();

    // On error, reset the module to prevent the backend from running.
    if (Diags.hasErrorOccurred()) {
      if (Builder)
        Builder->clear();
      M.reset();
      return;
    }
  }

  void FinalizeTentativeDefinition(VarDecl *D) override {
    if (Diags.hasErrorOccurred())
      return;

    Builder->genTentativeDefinition(D);
  }

  void FinalizeExternalDeclaration(VarDecl *D) override {
    Builder->genExternalDeclaration(D);
  }
};
} // namespace

// ===----------------------------------------------------------------------===
// IRGenerator forwarding
// ===----------------------------------------------------------------------===

void IRGenerator::anchor() {}

ModuleEmitter &IRGenerator::ME() {
  return static_cast<CodeGeneratorImpl *>(this)->ME();
}

llvm::Module *IRGenerator::getModule() {
  return static_cast<CodeGeneratorImpl *>(this)->getModule();
}

llvm::Module *IRGenerator::releaseModule() {
  return static_cast<CodeGeneratorImpl *>(this)->releaseModule();
}

DebugEmitter *IRGenerator::getDebugEmitter() {
  return static_cast<CodeGeneratorImpl *>(this)->getDebugEmitter();
}

const Decl *IRGenerator::getDeclForMangledName(llvm::StringRef name) {
  return static_cast<CodeGeneratorImpl *>(this)->getDeclForMangledName(name);
}

llvm::StringRef IRGenerator::getMangledName(GlobalDecl GD) {
  return static_cast<CodeGeneratorImpl *>(this)->getMangledName(GD);
}

llvm::Constant *IRGenerator::addrOfGlobal(GlobalDecl global,
                                          bool isForDefinition) {
  return static_cast<CodeGeneratorImpl *>(this)->addrOfGlobal(global,
                                                              isForDefinition);
}

llvm::Module *IRGenerator::startModule(llvm::StringRef ModuleName,
                                       llvm::LLVMContext &C) {
  return static_cast<CodeGeneratorImpl *>(this)->startModule(ModuleName, C);
}

// ===----------------------------------------------------------------------===
// Factory
// ===----------------------------------------------------------------------===

IRGenerator *
neverc::CreateIRGenerator(DiagnosticsEngine &Diags, llvm::StringRef ModuleName,
                          llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS,
                          const HeaderIndexOptions &HeaderIdxOpts,
                          const PrepOptions &PrepOpts,
                          const CodeGenOptions &CGO, llvm::LLVMContext &C) {
  return new CodeGeneratorImpl(Diags, ModuleName, std::move(FS), HeaderIdxOpts,
                               PrepOpts, CGO, C);
}
