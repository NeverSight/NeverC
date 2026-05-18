#include "Core/ModuleEmitter.h"
#include "ABI/ABIInfo.h"
#include "ABI/EmitterABI.h"
#include "ABI/TargetInfo.h"
#include "Core/ConstantEmitter.h"
#include "Core/FunctionEmitter.h"
#include "Debug/DebugEmitterInfo.h"
#include "Stmt/CallEmitterInfo.h"
#include "neverc/Compiler/FrontendDiag.h"
#include "neverc/Emit/Backend/BackendUtil.h"
#include "neverc/Emit/Decl/ConstantInitBuilder.h"
#include "neverc/Foundation/Builtin/BuiltinString.h"
#include "neverc/Foundation/Builtin/BuiltinStringNames.h"
#include "neverc/Foundation/Builtin/Builtins.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Core/Version.h"
#include "neverc/Tree/Core/CharUnits.h"
#include "neverc/Tree/Core/Mangle.h"
#include "neverc/Tree/Core/RecursiveTreeVisitor.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Stmt/StmtVisitor.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Config/config.h"
#include "llvm/IR/AttributeMask.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/ProfileData/SampleProf.h"
#include "llvm/Support/CRC.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/TargetParser/X86TargetParser.h"
#include <optional>

using namespace neverc;
using namespace Emit;

// ===----------------------------------------------------------------------===
// Internal helpers
// ===----------------------------------------------------------------------===

namespace {

const char AnnotationSection[] = "llvm.metadata";

std::unique_ptr<CGABI> constructABI(ModuleEmitter &ME) {
  return std::make_unique<CGABI>(ME);
}

std::unique_ptr<TargetCodeGenInfo> createTargetCodeGenInfo(ModuleEmitter &ME) {
  const TargetInfo &Target = ME.getTarget();
  const llvm::Triple &Triple = Target.getTriple();

  switch (Triple.getArch()) {
  default:
    return createDefaultTargetCodeGenInfo(ME);

  case llvm::Triple::aarch64: {
    AArch64ABIKind Kind = AArch64ABIKind::AAPCS;
    if (Target.getABI() == "darwinpcs")
      Kind = AArch64ABIKind::DarwinPCS;
    return createAArch64TargetCodeGenInfo(ME, Kind);
  }

  case llvm::Triple::x86_64: {
    llvm::StringRef ABI = Target.getABI();
    X86AVXABILevel AVXLevel = (ABI == "avx512" ? X86AVXABILevel::AVX512
                               : ABI == "avx"  ? X86AVXABILevel::AVX
                                               : X86AVXABILevel::None);

    switch (Triple.getOS()) {
    case llvm::Triple::Win32:
      return createWinX86_64TargetCodeGenInfo(ME, AVXLevel);
    default:
      return createX86_64TargetCodeGenInfo(ME, AVXLevel);
    }
  }
  }
}

} // namespace

// ===----------------------------------------------------------------------===
// Construction & lifecycle
// ===----------------------------------------------------------------------===

const TargetCodeGenInfo &ModuleEmitter::getTargetCodeGenInfo() {
  if (!TheTargetCodeGenInfo)
    TheTargetCodeGenInfo = createTargetCodeGenInfo(*this);
  return *TheTargetCodeGenInfo;
}

ModuleEmitter::ModuleEmitter(TreeContext &C,
                             llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS,
                             const HeaderIndexOptions &HSO,
                             const PrepOptions &PPO, const CodeGenOptions &CGO,
                             llvm::Module &M, DiagnosticsEngine &diags)
    : Context(C), LangOpts(C.getLangOpts()), FS(FS), HeaderIdxOpts(HSO),
      PrepOpts(PPO), CodeGenOpts(CGO), TheModule(M), Diags(diags),
      Target(C.getTargetInfo()), ABI(constructABI(*this)),
      VMContext(M.getContext()), Types(*this) {

  llvm::LLVMContext &LLVMContext = M.getContext();
  VoidTy = llvm::Type::getVoidTy(LLVMContext);
  Int8Ty = llvm::Type::getInt8Ty(LLVMContext);
  Int16Ty = llvm::Type::getInt16Ty(LLVMContext);
  Int32Ty = llvm::Type::getInt32Ty(LLVMContext);
  Int64Ty = llvm::Type::getInt64Ty(LLVMContext);
  HalfTy = llvm::Type::getHalfTy(LLVMContext);
  BFloatTy = llvm::Type::getBFloatTy(LLVMContext);
  FloatTy = llvm::Type::getFloatTy(LLVMContext);
  DoubleTy = llvm::Type::getDoubleTy(LLVMContext);
  PointerWidthInBits = C.getTargetInfo().getPointerWidth(LangAS::Default);
  PointerAlignInBytes =
      C.toCharUnitsFromBits(C.getTargetInfo().getPointerAlign(LangAS::Default))
          .getQuantity();
  SizeSizeInBytes =
      C.toCharUnitsFromBits(C.getTargetInfo().getMaxPointerWidth())
          .getQuantity();
  IntAlignInBytes =
      C.toCharUnitsFromBits(C.getTargetInfo().getIntAlign()).getQuantity();
  CharTy =
      llvm::IntegerType::get(LLVMContext, C.getTargetInfo().getCharWidth());
  IntTy = llvm::IntegerType::get(LLVMContext, C.getTargetInfo().getIntWidth());
  IntPtrTy = llvm::IntegerType::get(LLVMContext,
                                    C.getTargetInfo().getMaxPointerWidth());
  Int8PtrTy = llvm::PointerType::get(LLVMContext, 0);
  Int16PtrTy = Int16Ty->getPointerTo(0);
  Int32PtrTy = Int32Ty->getPointerTo(0);
  Int64PtrTy = Int64Ty->getPointerTo(0);
  Int8PtrPtrTy = Int8PtrTy->getPointerTo(0);
  const llvm::DataLayout &DL = M.getDataLayout();
  AllocaInt8PtrTy =
      llvm::PointerType::get(LLVMContext, DL.getAllocaAddrSpace());
  GlobalsInt8PtrTy =
      llvm::PointerType::get(LLVMContext, DL.getDefaultGlobalsAddressSpace());
  ConstGlobalsPtrTy = llvm::PointerType::get(
      LLVMContext, C.getTargetAddressSpace(getGlobalConstantAddressSpace()));
  ASTAllocaAddressSpace = getTargetCodeGenInfo().getASTAllocaAddressSpace();

  RuntimeCC = getTargetCodeGenInfo().getABIInfo().getRuntimeCC();

  // Enable TBAA unless it's suppressed.
  if (!CodeGenOpts.RelaxedAliasing && CodeGenOpts.OptimizationLevel > 0)
    TBAA.reset(new TBAAEmitter(Context, TheModule.getContext(), CodeGenOpts,
                               getLangOpts()));

  if (CodeGenOpts.getDebugInfo() != llvm::codegenoptions::NoDebugInfo)
    DebugInfo.reset(new DebugEmitter(*this));

  if (CodeGenOpts.UniqueInternalLinkageNames &&
      !getModule().getSourceFileName().empty()) {
    std::string Path = getModule().getSourceFileName();
    for (const auto &Entry : LangOpts.MacroPrefixMap)
      if (Path.rfind(Entry.first, 0) != std::string::npos) {
        Path = Entry.second + Path.substr(Entry.first.size());
        break;
      }
    ModuleNameHash = llvm::getUniqueInternalLinkagePostfix(Path);
  }
}

ModuleEmitter::~ModuleEmitter() {}

void ModuleEmitter::addReplacement(llvm::StringRef Name, llvm::Constant *C) {
  Replacements[Name] = C;
}

void ModuleEmitter::applyReplacements() {
  for (auto &I : Replacements) {
    llvm::StringRef MangledName = I.first;
    llvm::Constant *Replacement = I.second;
    llvm::GlobalValue *Entry = getGlobalValue(MangledName);
    if (!Entry)
      continue;
    auto *OldF = cast<llvm::Function>(Entry);
    auto *NewF = dyn_cast<llvm::Function>(Replacement);
    if (!NewF) {
      if (auto *Alias = dyn_cast<llvm::GlobalAlias>(Replacement)) {
        NewF = dyn_cast<llvm::Function>(Alias->getAliasee());
      } else {
        auto *CE = cast<llvm::ConstantExpr>(Replacement);
        assert(CE->getOpcode() == llvm::Instruction::BitCast ||
               CE->getOpcode() == llvm::Instruction::GetElementPtr);
        NewF = dyn_cast<llvm::Function>(CE->getOperand(0));
      }
    }

    // Replace old with new, but keep the old order.
    OldF->replaceAllUsesWith(Replacement);
    if (NewF) {
      NewF->removeFromParent();
      OldF->getParent()->getFunctionList().insertAfter(OldF->getIterator(),
                                                       NewF);
    }
    OldF->eraseFromParent();
  }
}

void ModuleEmitter::addGlobalValReplacement(llvm::GlobalValue *GV,
                                            llvm::Constant *C) {
  GlobalValReplacements.push_back(std::make_pair(GV, C));
}

void ModuleEmitter::applyGlobalValReplacements() {
  for (auto &I : GlobalValReplacements) {
    llvm::GlobalValue *GV = I.first;
    llvm::Constant *C = I.second;

    GV->replaceAllUsesWith(C);
    GV->eraseFromParent();
  }
}

namespace {

const llvm::GlobalValue *resolveAliasTarget(const llvm::GlobalValue *GV) {
  const llvm::Constant *C;
  if (auto *GA = dyn_cast<llvm::GlobalAlias>(GV))
    C = GA->getAliasee();
  else if (auto *GI = dyn_cast<llvm::GlobalIFunc>(GV))
    C = GI->getResolver();
  else
    return GV;

  const auto *AliaseeGV = dyn_cast<llvm::GlobalValue>(C->stripPointerCasts());
  if (!AliaseeGV)
    return nullptr;

  const llvm::GlobalValue *FinalGV = AliaseeGV->getAliaseeObject();
  if (FinalGV == GV)
    return nullptr;

  return FinalGV;
}

bool checkAliasedGlobal(
    const TreeContext &Context, DiagnosticsEngine &Diags,
    SourceLocation Location, bool IsIFunc, const llvm::GlobalValue *Alias,
    const llvm::GlobalValue *&GV,
    const llvm::MapVector<GlobalDecl, llvm::StringRef> &MangledDeclNames,
    SourceRange AliasRange) {
  GV = resolveAliasTarget(Alias);
  if (!GV) {
    Diags.Report(Location, diag::err_cyclic_alias) << IsIFunc;
    return false;
  }

  if (GV->isDeclaration()) {
    Diags.Report(Location, diag::err_alias_to_undefined) << IsIFunc << IsIFunc;
    Diags.Report(Location, diag::note_alias_requires_mangled_name)
        << IsIFunc << IsIFunc;
    // Provide a note if the given function is not found and exists as a
    // mangled name.
    for (const auto &[Decl, Name] : MangledDeclNames) {
      if (const auto *ND = dyn_cast<NamedDecl>(Decl.getDecl())) {
        if (ND->getName() == GV->getName()) {
          Diags.Report(Location, diag::note_alias_mangled_name_alternative)
              << Name
              << FixItHint::CreateReplacement(
                     AliasRange, (llvm::Twine(IsIFunc ? "ifunc" : "alias") +
                                  "(\"" + Name + "\")")
                                     .str());
        }
      }
    }
    return false;
  }

  if (IsIFunc) {
    const auto *F = dyn_cast<llvm::Function>(GV);
    if (!F) {
      Diags.Report(Location, diag::err_alias_to_undefined)
          << IsIFunc << IsIFunc;
      return false;
    }

    llvm::FunctionType *FTy = F->getFunctionType();
    if (!FTy->getReturnType()->isPointerTy()) {
      Diags.Report(Location, diag::err_ifunc_resolver_return);
      return false;
    }
  }

  return true;
}

} // namespace

// ===----------------------------------------------------------------------===
// Alias validation
// ===----------------------------------------------------------------------===

void ModuleEmitter::checkAliases() {
  bool Error = false;
  DiagnosticsEngine &Diags = getDiags();
  for (const GlobalDecl &GD : Aliases) {
    const auto *D = cast<ValueDecl>(GD.getDecl());
    SourceLocation Location;
    SourceRange Range;
    bool IsIFunc = D->hasAttr<IFuncAttr>();
    if (const Attr *A = D->getDefiningAttr()) {
      Location = A->getLocation();
      Range = A->getRange();
    } else
      llvm_unreachable("Not an alias or ifunc?");

    llvm::StringRef MangledName = getMangledName(GD);
    llvm::GlobalValue *Alias = getGlobalValue(MangledName);
    const llvm::GlobalValue *GV = nullptr;
    if (!checkAliasedGlobal(getContext(), Diags, Location, IsIFunc, Alias, GV,
                            MangledDeclNames, Range)) {
      Error = true;
      continue;
    }

    llvm::Constant *Aliasee =
        IsIFunc ? cast<llvm::GlobalIFunc>(Alias)->getResolver()
                : cast<llvm::GlobalAlias>(Alias)->getAliasee();

    llvm::GlobalValue *AliaseeGV;
    if (auto CE = dyn_cast<llvm::ConstantExpr>(Aliasee))
      AliaseeGV = cast<llvm::GlobalValue>(CE->getOperand(0));
    else
      AliaseeGV = cast<llvm::GlobalValue>(Aliasee);

    if (const SectionAttr *SA = D->getAttr<SectionAttr>()) {
      llvm::StringRef AliasSection = SA->getName();
      if (AliasSection != AliaseeGV->getSection())
        Diags.Report(SA->getLocation(), diag::warn_alias_with_section)
            << AliasSection << IsIFunc << IsIFunc;
    }

    // We have to handle alias to weak aliases in here. LLVM itself disallows
    // this since the object semantics would not match the IL one. For
    // compatibility with gcc we implement it by just pointing the alias
    // to its aliasee's aliasee. We also warn, since the user is probably
    // expecting the link to be weak.
    if (auto *GA = dyn_cast<llvm::GlobalAlias>(AliaseeGV)) {
      if (GA->isInterposable()) {
        Diags.Report(Location, diag::warn_alias_to_weak_alias)
            << GV->getName() << GA->getName() << IsIFunc;
        Aliasee = llvm::ConstantExpr::getPointerBitCastOrAddrSpaceCast(
            GA->getAliasee(), Alias->getType());

        if (IsIFunc)
          cast<llvm::GlobalIFunc>(Alias)->setResolver(Aliasee);
        else
          cast<llvm::GlobalAlias>(Alias)->setAliasee(Aliasee);
      }
    }
  }
  if (!Error)
    return;

  for (const GlobalDecl &GD : Aliases) {
    llvm::StringRef MangledName = getMangledName(GD);
    llvm::GlobalValue *Alias = getGlobalValue(MangledName);
    Alias->replaceAllUsesWith(llvm::UndefValue::get(Alias->getType()));
    Alias->eraseFromParent();
  }
}

void ModuleEmitter::clear() {
  DeferredDeclsToEmit.clear();
  DeferredAnnotations.clear();
}

namespace {

void setVisibilityFromDLLStorageClass(const neverc::LangOptions &LO,
                                      llvm::Module &M) {
  if (!LO.VisibilityFromDLLStorageClass)
    return;

  llvm::GlobalValue::VisibilityTypes DLLExportVisibility =
      ModuleEmitter::getLLVMVisibility(LO.getDLLExportVisibility());
  llvm::GlobalValue::VisibilityTypes NoDLLStorageClassVisibility =
      ModuleEmitter::getLLVMVisibility(LO.getNoDLLStorageClassVisibility());
  llvm::GlobalValue::VisibilityTypes ExternDeclDLLImportVisibility =
      ModuleEmitter::getLLVMVisibility(LO.getExternDeclDLLImportVisibility());
  llvm::GlobalValue::VisibilityTypes ExternDeclNoDLLStorageClassVisibility =
      ModuleEmitter::getLLVMVisibility(
          LO.getExternDeclNoDLLStorageClassVisibility());

  for (llvm::GlobalValue &GV : M.global_values()) {
    if (GV.hasAppendingLinkage() || GV.hasLocalLinkage())
      continue;

    // Reset DSO locality before setting the visibility. This removes
    // any effects that visibility options and annotations may have
    // had on the DSO locality. Setting the visibility will implicitly set
    // appropriate globals to DSO Local; however, this will be pessimistic
    // w.r.t. to the normal compiler IRGen.
    GV.setDSOLocal(false);

    if (GV.isDeclarationForLinker()) {
      GV.setVisibility(GV.getDLLStorageClass() ==
                               llvm::GlobalValue::DLLImportStorageClass
                           ? ExternDeclDLLImportVisibility
                           : ExternDeclNoDLLStorageClassVisibility);
    } else {
      GV.setVisibility(GV.getDLLStorageClass() ==
                               llvm::GlobalValue::DLLExportStorageClass
                           ? DLLExportVisibility
                           : NoDLLStorageClassVisibility);
    }

    GV.setDLLStorageClass(llvm::GlobalValue::DefaultStorageClass);
  }
}

bool isStackProtectorOn(const LangOptions &LangOpts, const llvm::Triple &Triple,
                        neverc::LangOptions::StackProtectorMode Mode) {
  return LangOpts.getStackProtector() == Mode;
}

} // namespace

// ===----------------------------------------------------------------------===
// Module finalization
// ===----------------------------------------------------------------------===

void ModuleEmitter::release() {
  genDeferred();
  applyGlobalValReplacements();
  applyReplacements();
  emitMultiVersionFunctions();

  GlobalInits.clear();
  registerGlobalDtorsWithAtExit();
  llvm::stable_sort(GlobalCtors, [](const Structor &L, const Structor &R) {
    return L.LexOrder < R.LexOrder;
  });
  genCtorList(GlobalCtors, "llvm.global_ctors");
  genCtorList(GlobalDtors, "llvm.global_dtors");
  genGlobalAnnotations();
  genStaticExternCAliases();
  checkAliases();
  emitOverrideSection();
  emitLLVMUsed();

  if (CodeGenOpts.Autolink && !LinkerOptionsMetadata.empty()) {
    genModuleLinkOptions();
  }

  // ELF passes dependent-library specifiers verbatim to the linker (unlike
  // COFF/MachO which map them to specific linker options).
  if (!ELFDependentLibraries.empty()) {
    auto *NMD =
        getModule().getOrInsertNamedMetadata("llvm.dependent-libraries");
    for (auto *MD : ELFDependentLibraries)
      NMD->addOperand(MD);
  }

  if (CodeGenOpts.DwarfVersion) {
    getModule().addModuleFlag(llvm::Module::Max, "Dwarf Version",
                              CodeGenOpts.DwarfVersion);
  }

  if (CodeGenOpts.Dwarf64)
    getModule().addModuleFlag(llvm::Module::Max, "DWARF64", 1);

  if (Context.getLangOpts().SemanticInterposition)
    getModule().setSemanticInterposition(true);

  if (CodeGenOpts.ControlFlowGuard)
    getModule().addModuleFlag(llvm::Module::Warning, "cfguard", 2);
  else if (CodeGenOpts.ControlFlowGuardNoChecks)
    getModule().addModuleFlag(llvm::Module::Warning, "cfguard", 1);
  if (CodeGenOpts.EHContGuard)
    getModule().addModuleFlag(llvm::Module::Warning, "ehcontguard", 1);
  if (Context.getLangOpts().Kernel)
    getModule().addModuleFlag(llvm::Module::Warning, "ms-kernel", 1);
  if (getModuleDebugInfo())
    getModule().addModuleFlag(llvm::Module::Warning, "Debug Info Version",
                              llvm::DEBUG_METADATA_VERSION);

  // We need to record the widths of enums and wchar_t, so that we can generate
  // the correct build attributes in the AArch64 backend. wchar_size is also
  // used by TargetLibraryInfo.
  uint64_t WCharWidth =
      Context.getTypeSizeInChars(Context.getWideCharType()).getQuantity();
  getModule().addModuleFlag(llvm::Module::Error, "wchar_size", WCharWidth);

  llvm::Triple::ArchType Arch = Context.getTargetInfo().getTriple().getArch();

  if (CodeGenOpts.DisableCFICheck)
    getModule().addModuleFlag(llvm::Module::Override, "Disable CFI Check", 1);
  if (CodeGenOpts.DisableInlineOpt)
    getModule().addModuleFlag(llvm::Module::Override, "DisableInlineOpt", 1);
  if (CodeGenOpts.DisableTryStmt)
    getModule().addModuleFlag(llvm::Module::Override, "DisableTryStmt", 1);
  if (CodeGenOpts.Jumptablerdata)
    getModule().addModuleFlag(llvm::Module::Override, "Jumptablerdata", 1);

  if (CodeGenOpts.CFProtectionReturn &&
      Target.checkCFProtectionReturnSupported(getDiags()))
    getModule().addModuleFlag(llvm::Module::Min, "cf-protection-return", 1);
  if (CodeGenOpts.CFProtectionBranch &&
      Target.checkCFProtectionBranchSupported(getDiags()))
    getModule().addModuleFlag(llvm::Module::Min, "cf-protection-branch", 1);

  if (CodeGenOpts.FunctionReturnThunks)
    getModule().addModuleFlag(llvm::Module::Override,
                              "function_return_thunk_extern", 1);

  if (CodeGenOpts.IndirectBranchCSPrefix)
    getModule().addModuleFlag(llvm::Module::Override,
                              "indirect_branch_cs_prefix", 1);

  // Module-level metadata for return address signing / BTI / stack tagging.
  // Function attributes carry the real config, but LTO serialises them too
  // late for build-attribute emission, so we duplicate here with "Min" merge
  // semantics (feature is off if any object lacks it).
  if (Arch == llvm::Triple::aarch64) {
    if (LangOpts.BranchTargetEnforcement)
      getModule().addModuleFlag(llvm::Module::Min, "branch-target-enforcement",
                                1);
    if (LangOpts.BranchProtectionPAuthLR)
      getModule().addModuleFlag(llvm::Module::Min, "branch-protection-pauth-lr",
                                1);
    if (LangOpts.hasSignReturnAddress())
      getModule().addModuleFlag(llvm::Module::Min, "sign-return-address", 1);
    if (LangOpts.isSignReturnAddressScopeAll())
      getModule().addModuleFlag(llvm::Module::Min, "sign-return-address-all",
                                1);
    if (!LangOpts.isSignReturnAddressWithAKey())
      getModule().addModuleFlag(llvm::Module::Min,
                                "sign-return-address-with-bkey", 1);
  }

  if (CodeGenOpts.StackClashProtector)
    getModule().addModuleFlag(
        llvm::Module::Override, "probe-stack",
        llvm::MDString::get(TheModule.getContext(), "inline-asm"));

  if (CodeGenOpts.StackProbeSize && CodeGenOpts.StackProbeSize != 4096)
    getModule().addModuleFlag(llvm::Module::Min, "stack-probe-size",
                              CodeGenOpts.StackProbeSize);

  if (LangOpts.EHAsynch)
    getModule().addModuleFlag(llvm::Module::Warning, "eh-asynch", 1);

  if (Context.getLangOpts().PIE)
    getModule().setPIELevel(llvm::PIELevel::Large);

  if (getCodeGenOpts().CodeModel.size() > 0) {
    unsigned CM = llvm::StringSwitch<unsigned>(getCodeGenOpts().CodeModel)
                      .Case("tiny", llvm::CodeModel::Tiny)
                      .Case("small", llvm::CodeModel::Small)
                      .Case("kernel", llvm::CodeModel::Kernel)
                      .Case("medium", llvm::CodeModel::Medium)
                      .Case("large", llvm::CodeModel::Large)
                      .Default(~0u);
    if (CM != ~0u) {
      llvm::CodeModel::Model codeModel =
          static_cast<llvm::CodeModel::Model>(CM);
      getModule().setCodeModel(codeModel);

      if (CM == llvm::CodeModel::Medium &&
          Context.getTargetInfo().getTriple().getArch() ==
              llvm::Triple::x86_64) {
        getModule().setLargeDataThreshold(getCodeGenOpts().LargeDataThreshold);
      }
    }
  }

  if (CodeGenOpts.NoPLT)
    getModule().setRtLibUseGOT();
  if (getTriple().isOSBinFormatELF() &&
      CodeGenOpts.DirectAccessExternalData !=
          getModule().getDirectAccessExternalData()) {
    getModule().setDirectAccessExternalData(
        CodeGenOpts.DirectAccessExternalData);
  }
  if (CodeGenOpts.UnwindTables)
    getModule().setUwtable(llvm::UWTableKind(CodeGenOpts.UnwindTables));

  switch (CodeGenOpts.getFramePointer()) {
  case CodeGenOptions::FramePointerKind::None:
    // 0 ("none") is the default.
    break;
  case CodeGenOptions::FramePointerKind::NonLeaf:
    getModule().setFramePointer(llvm::FramePointerKind::NonLeaf);
    break;
  case CodeGenOptions::FramePointerKind::All:
    getModule().setFramePointer(llvm::FramePointerKind::All);
    break;
  }

  if (DebugEmitter *DI = getModuleDebugInfo())
    DI->finalize();

  if (getCodeGenOpts().EmitVersionIdentMetadata)
    genVersionIdentMetadata();

  if (!getCodeGenOpts().RecordCommandLine.empty())
    genCommandLineMetadata();

  if (!getCodeGenOpts().StackProtectorGuard.empty())
    getModule().setStackProtectorGuard(getCodeGenOpts().StackProtectorGuard);
  if (!getCodeGenOpts().StackProtectorGuardReg.empty())
    getModule().setStackProtectorGuardReg(
        getCodeGenOpts().StackProtectorGuardReg);
  if (!getCodeGenOpts().StackProtectorGuardSymbol.empty())
    getModule().setStackProtectorGuardSymbol(
        getCodeGenOpts().StackProtectorGuardSymbol);
  if (getCodeGenOpts().StackProtectorGuardOffset != INT_MAX)
    getModule().setStackProtectorGuardOffset(
        getCodeGenOpts().StackProtectorGuardOffset);
  if (getCodeGenOpts().StackAlignment)
    getModule().setOverrideStackAlignment(getCodeGenOpts().StackAlignment);
  if (getCodeGenOpts().SkipRaxSetup)
    getModule().addModuleFlag(llvm::Module::Override, "SkipRaxSetup", 1);

  if (getContext().getTargetInfo().getMaxTLSAlign())
    getModule().addModuleFlag(llvm::Module::Error, "MaxTLSAlign",
                              getContext().getTargetInfo().getMaxTLSAlign());

  getTargetCodeGenInfo().emitTargetGlobals(*this);

  getTargetCodeGenInfo().emitTargetMetadata(*this, MangledDeclNames);

  setVisibilityFromDLLStorageClass(LangOpts, getModule());
}

// ===----------------------------------------------------------------------===
// Type info, diagnostics & visibility
// ===----------------------------------------------------------------------===

// ===----------------------------------------------------------------------===
// TBAA & type queries
// ===----------------------------------------------------------------------===

void ModuleEmitter::updateCompletedType(const TagDecl *TD) {
  Types.updateCompletedType(TD);
}

llvm::MDNode *ModuleEmitter::getTBAATypeInfo(QualType QTy) {
  if (!TBAA)
    return nullptr;
  return TBAA->getTypeInfo(QTy);
}

TBAAAccessInfo ModuleEmitter::getTBAAAccessInfo(QualType AccessType) {
  if (!TBAA)
    return TBAAAccessInfo();
  return TBAA->getAccessInfo(AccessType);
}

llvm::MDNode *ModuleEmitter::getTBAAStructInfo(QualType QTy) {
  if (!TBAA)
    return nullptr;
  return TBAA->getTBAAStructInfo(QTy);
}

llvm::MDNode *ModuleEmitter::getTBAABaseTypeInfo(QualType QTy) {
  if (!TBAA)
    return nullptr;
  return TBAA->getBaseTypeInfo(QTy);
}

llvm::MDNode *ModuleEmitter::getTBAAAccessTagInfo(TBAAAccessInfo Info) {
  if (!TBAA)
    return nullptr;
  return TBAA->getAccessTagInfo(Info);
}

TBAAAccessInfo ModuleEmitter::mergeTBAAInfoForCast(TBAAAccessInfo SourceInfo,
                                                   TBAAAccessInfo TargetInfo) {
  if (!TBAA)
    return TBAAAccessInfo();
  return TBAA->mergeTBAAInfoForCast(SourceInfo, TargetInfo);
}

TBAAAccessInfo
ModuleEmitter::mergeTBAAInfoForConditionalOperator(TBAAAccessInfo InfoA,
                                                   TBAAAccessInfo InfoB) {
  if (!TBAA)
    return TBAAAccessInfo();
  return TBAA->mergeTBAAInfoForConditionalOperator(InfoA, InfoB);
}

TBAAAccessInfo
ModuleEmitter::mergeTBAAInfoForMemoryTransfer(TBAAAccessInfo DestInfo,
                                              TBAAAccessInfo SrcInfo) {
  if (!TBAA)
    return TBAAAccessInfo();
  return TBAA->mergeTBAAInfoForConditionalOperator(DestInfo, SrcInfo);
}

void ModuleEmitter::decorateInstructionWithTBAA(llvm::Instruction *Inst,
                                                TBAAAccessInfo TBAAInfo) {
  if (llvm::MDNode *Tag = getTBAAAccessTagInfo(TBAAInfo))
    Inst->setMetadata(llvm::LLVMContext::MD_tbaa, Tag);
}

void ModuleEmitter::error(SourceLocation loc, llvm::StringRef message) {
  unsigned diagID = getDiags().getCustomDiagID(DiagnosticsEngine::Error, "%0");
  getDiags().Report(Context.getFullLoc(loc), diagID) << message;
}

void ModuleEmitter::errorUnsupported(const Stmt *S, const char *Type) {
  unsigned DiagID = getDiags().getCustomDiagID(DiagnosticsEngine::Error,
                                               "cannot compile this %0 yet");
  std::string Msg = Type;
  getDiags().Report(Context.getFullLoc(S->getBeginLoc()), DiagID)
      << Msg << S->getSourceRange();
}

void ModuleEmitter::errorUnsupported(const Decl *D, const char *Type) {
  unsigned DiagID = getDiags().getCustomDiagID(DiagnosticsEngine::Error,
                                               "cannot compile this %0 yet");
  std::string Msg = Type;
  getDiags().Report(Context.getFullLoc(D->getLocation()), DiagID) << Msg;
}

llvm::ConstantInt *ModuleEmitter::getSize(CharUnits size) {
  return llvm::ConstantInt::get(SizeTy, size.getQuantity());
}

// ===----------------------------------------------------------------------===
// Visibility & linkage properties
// ===----------------------------------------------------------------------===

void ModuleEmitter::setGlobalVisibility(llvm::GlobalValue *GV,
                                        const NamedDecl *D) const {
  // Internal definitions always have default visibility.
  if (GV->hasLocalLinkage()) {
    GV->setVisibility(llvm::GlobalValue::DefaultVisibility);
    return;
  }
  if (!D)
    return;

  // Visibility applies to definitions always, and to declarations only when
  // requested globally or set explicitly.
  LinkageInfo LV = D->getLinkageAndVisibility();

  if (GV->hasDLLExportStorageClass() || GV->hasDLLImportStorageClass()) {
    // Reject incompatible dlllstorage and visibility annotations.
    if (!LV.isVisibilityExplicit())
      return;
    if (GV->hasDLLExportStorageClass()) {
      if (LV.getVisibility() == HiddenVisibility)
        getDiags().Report(D->getLocation(),
                          diag::err_hidden_visibility_dllexport);
    } else if (LV.getVisibility() != DefaultVisibility) {
      getDiags().Report(D->getLocation(),
                        diag::err_non_default_visibility_dllimport);
    }
    return;
  }

  if (LV.isVisibilityExplicit() || getLangOpts().SetVisibilityForExternDecls ||
      !GV->isDeclarationForLinker())
    GV->setVisibility(getLLVMVisibility(LV.getVisibility()));
}

namespace {

bool shouldAssumeDSOLocal(const ModuleEmitter &ME, llvm::GlobalValue *GV) {
  if (GV->hasLocalLinkage())
    return true;

  if (!GV->hasDefaultVisibility() && !GV->hasExternalWeakLinkage())
    return true;

  // DLLImport explicitly marks the GV as external.
  if (GV->hasDLLImportStorageClass())
    return false;

  const llvm::Triple &TT = ME.getTriple();
  const auto &CGOpts = ME.getCodeGenOpts();

  // On COFF, don't mark 'extern_weak' symbols as DSO local. If these symbols
  // remain unresolved in the link, they can be resolved to zero, which is
  // outside the current DSO.
  if (TT.isOSBinFormatCOFF() && GV->hasExternalWeakLinkage())
    return false;

  // Every other GV is local on COFF.
  // Make an exception for windows OS in the triple: Some firmware builds use
  // *-win32-macho triples. This (accidentally?) produced windows relocations
  // without GOT tables in older versions; Keep this behaviour.
  if (TT.isOSBinFormatCOFF() || (TT.isOSWindows() && TT.isOSBinFormatMachO()))
    return true;

  // Only handle COFF and ELF for now.
  if (!TT.isOSBinFormatELF())
    return false;

  const auto &LOpts = ME.getLangOpts();
  if (!LOpts.PIE) {
    if (!(isa<llvm::Function>(GV) && GV->canBenefitFromLocalAlias()))
      return false;
    return !(ME.getLangOpts().SemanticInterposition ||
             ME.getLangOpts().HalfNoSemanticInterposition);
  }

  if (!GV->isDeclarationForLinker())
    return true;

  if (GV->hasExternalWeakLinkage())
    return false;

  if (CGOpts.DirectAccessExternalData) {
    if (auto *Var = dyn_cast<llvm::GlobalVariable>(GV))
      if (!Var->isThreadLocal())
        return true;
  }

  return false;
}

} // namespace

void ModuleEmitter::setDSOLocal(llvm::GlobalValue *GV) const {
  GV->setDSOLocal(shouldAssumeDSOLocal(*this, GV));
}

void ModuleEmitter::setDLLImportDLLExport(llvm::GlobalValue *GV,
                                          GlobalDecl GD) const {
  const auto *D = dyn_cast<NamedDecl>(GD.getDecl());
  setDLLImportDLLExport(GV, D);
}

void ModuleEmitter::setDLLImportDLLExport(llvm::GlobalValue *GV,
                                          const NamedDecl *D) const {
  if (D && D->isExternallyVisible()) {
    if (D->hasAttr<DLLImportAttr>())
      GV->setDLLStorageClass(llvm::GlobalVariable::DLLImportStorageClass);
    else if ((D->hasAttr<DLLExportAttr>() ||
              shouldMapVisibilityToDLLExport(D)) &&
             !GV->isDeclarationForLinker())
      GV->setDLLStorageClass(llvm::GlobalVariable::DLLExportStorageClass);
  }
}

void ModuleEmitter::setGVProperties(llvm::GlobalValue *GV,
                                    GlobalDecl GD) const {
  setDLLImportDLLExport(GV, GD);
  setGVPropertiesAux(GV, dyn_cast<NamedDecl>(GD.getDecl()));
}

void ModuleEmitter::setGVProperties(llvm::GlobalValue *GV,
                                    const NamedDecl *D) const {
  setDLLImportDLLExport(GV, D);
  setGVPropertiesAux(GV, D);
}

void ModuleEmitter::setGVPropertiesAux(llvm::GlobalValue *GV,
                                       const NamedDecl *D) const {
  setGlobalVisibility(GV, D);
  setDSOLocal(GV);
  GV->setPartition(CodeGenOpts.SymbolPartition);
}

// ===----------------------------------------------------------------------===
// Name mangling & function attributes
// ===----------------------------------------------------------------------===

// ===----------------------------------------------------------------------===
// TLS & name mangling
// ===----------------------------------------------------------------------===

namespace {

llvm::GlobalVariable::ThreadLocalMode getLLVMTLSModel(llvm::StringRef S) {
  return llvm::StringSwitch<llvm::GlobalVariable::ThreadLocalMode>(S)
      .Case("global-dynamic", llvm::GlobalVariable::GeneralDynamicTLSModel)
      .Case("local-dynamic", llvm::GlobalVariable::LocalDynamicTLSModel)
      .Case("initial-exec", llvm::GlobalVariable::InitialExecTLSModel)
      .Case("local-exec", llvm::GlobalVariable::LocalExecTLSModel);
}
} // namespace

llvm::GlobalVariable::ThreadLocalMode
ModuleEmitter::getDefaultLLVMTLSModel() const {
  switch (CodeGenOpts.getDefaultTLSModel()) {
  case CodeGenOptions::GeneralDynamicTLSModel:
    return llvm::GlobalVariable::GeneralDynamicTLSModel;
  case CodeGenOptions::LocalDynamicTLSModel:
    return llvm::GlobalVariable::LocalDynamicTLSModel;
  case CodeGenOptions::InitialExecTLSModel:
    return llvm::GlobalVariable::InitialExecTLSModel;
  case CodeGenOptions::LocalExecTLSModel:
    return llvm::GlobalVariable::LocalExecTLSModel;
  }
  llvm_unreachable("Invalid TLS model!");
}

void ModuleEmitter::setTLSMode(llvm::GlobalValue *GV, const VarDecl &D) const {
  assert(D.getTLSKind() && "setting TLS mode on non-TLS var!");

  llvm::GlobalValue::ThreadLocalMode TLM;
  TLM = getDefaultLLVMTLSModel();

  // Override the TLS model if it is explicitly specified.
  if (const TLSModelAttr *Attr = D.getAttr<TLSModelAttr>()) {
    TLM = getLLVMTLSModel(Attr->getModel());
  }

  GV->setThreadLocalMode(TLM);
}

namespace {

std::string getCPUSpecificMangling(const ModuleEmitter &ME,
                                   llvm::StringRef Name) {
  const TargetInfo &Target = ME.getTarget();
  return (llvm::Twine('.') +
          llvm::Twine(Target.CPUSpecificManglingCharacter(Name)))
      .str();
}

void appendCPUSpecificDispatchMangling(const ModuleEmitter &ME,
                                       const CPUSpecificAttr *Attr,
                                       unsigned CPUIndex,
                                       llvm::raw_ostream &Out) {
  // cpu_specific gets the current name, dispatch gets the resolver if IFunc is
  // supported.
  if (Attr)
    Out << getCPUSpecificMangling(ME, Attr->getCPUName(CPUIndex)->getName());
  else if (ME.getTarget().supportsIFunc())
    Out << ".resolver";
}

void appendTargetVersionMangling(const ModuleEmitter &ME,
                                 const TargetVersionAttr *Attr,
                                 llvm::raw_ostream &Out) {
  if (Attr->isDefaultVersion())
    return;
  Out << "._";
  const TargetInfo &TI = ME.getTarget();
  llvm::SmallVector<llvm::StringRef, 8> Feats;
  Attr->getFeatures(Feats);
  llvm::stable_sort(
      Feats, [&TI](const llvm::StringRef FeatL, const llvm::StringRef FeatR) {
        return TI.multiVersionSortPriority(FeatL) <
               TI.multiVersionSortPriority(FeatR);
      });
  for (const auto &Feat : Feats) {
    Out << 'M';
    Out << Feat;
  }
}

void appendTargetMangling(const ModuleEmitter &ME, const TargetAttr *Attr,
                          llvm::raw_ostream &Out) {
  if (Attr->isDefaultVersion())
    return;

  Out << '.';
  const TargetInfo &Target = ME.getTarget();
  ParsedTargetAttr Info = Target.parseTargetAttr(Attr->getFeaturesStr());
  llvm::sort(Info.Features,
             [&Target](llvm::StringRef LHS, llvm::StringRef RHS) {
               // Multiversioning doesn't allow "no-${feature}", so we can
               // only have "+" prefixes here.
               assert(LHS.starts_with("+") && RHS.starts_with("+") &&
                      "Features should always have a prefix.");
               return Target.multiVersionSortPriority(LHS.substr(1)) >
                      Target.multiVersionSortPriority(RHS.substr(1));
             });

  bool IsFirst = true;

  if (!Info.CPU.empty()) {
    IsFirst = false;
    Out << "arch_" << Info.CPU;
  }

  for (llvm::StringRef Feat : Info.Features) {
    if (!IsFirst)
      Out << '_';
    IsFirst = false;
    Out << Feat.substr(1);
  }
}

bool isUniqueInternalLinkageDecl(GlobalDecl GD, ModuleEmitter &ME) {
  const Decl *D = GD.getDecl();
  return !ME.getModuleNameHash().empty() && isa<FunctionDecl>(D) &&
         (ME.getFunctionLinkage(GD) == llvm::GlobalValue::InternalLinkage);
}

void appendTargetClonesMangling(const ModuleEmitter &ME,
                                const TargetClonesAttr *Attr,
                                unsigned VersionIndex, llvm::raw_ostream &Out) {
  const TargetInfo &TI = ME.getTarget();
  if (TI.getTriple().isAArch64()) {
    llvm::StringRef FeatureStr = Attr->getFeatureStr(VersionIndex);
    if (FeatureStr == "default")
      return;
    Out << "._";
    llvm::SmallVector<llvm::StringRef, 8> Features;
    FeatureStr.split(Features, "+");
    llvm::stable_sort(Features, [&TI](const llvm::StringRef FeatL,
                                      const llvm::StringRef FeatR) {
      return TI.multiVersionSortPriority(FeatL) <
             TI.multiVersionSortPriority(FeatR);
    });
    for (auto &Feat : Features) {
      Out << 'M';
      Out << Feat;
    }
  } else {
    Out << '.';
    llvm::StringRef FeatureStr = Attr->getFeatureStr(VersionIndex);
    if (FeatureStr.starts_with("arch="))
      Out << "arch_" << FeatureStr.substr(sizeof("arch=") - 1);
    else
      Out << FeatureStr;

    Out << '.' << Attr->getMangledIndex(VersionIndex);
  }
}

std::string getMangledNameImpl(ModuleEmitter &ME, GlobalDecl GD,
                               const NamedDecl *ND,
                               bool OmitMultiVersionMangling = false) {
  llvm::SmallString<256> Buffer;
  llvm::raw_svector_ostream Out(Buffer);
  MangleContext &MC = ME.getCGABI().getMangleContext();
  if (!ME.getModuleNameHash().empty())
    MC.needsUniqueInternalLinkageNames();
  bool ShouldMangle = MC.shouldMangleDeclName(ND);
  if (ShouldMangle)
    MC.mangleName(GD.getWithDecl(ND), Out);
  else {
    IdentifierInfo *II = ND->getIdentifier();
    assert(II && "Attempt to mangle unnamed decl.");
    const auto *FD = dyn_cast<FunctionDecl>(ND);

    if (FD &&
        FD->getType()->castAs<FunctionType>()->getCallConv() == CC_X86RegCall) {
      Out << "__regcall3__" << II->getName();
    } else {
      Out << II->getName();
    }
  }

  // Module name hash for internal linkage must precede multi-version target
  // suffixes to keep the name and module hash suffix of the
  // internal linkage function together.  The unique suffix should only be
  // added when name mangling is done to make sure that the final name can
  // be properly demangled.  For example, for C functions without prototypes,
  // name mangling is not done and the unique suffix should not be appeneded
  // then.
  if (ShouldMangle && isUniqueInternalLinkageDecl(GD, ME)) {
    assert(ME.getCodeGenOpts().UniqueInternalLinkageNames &&
           "Hash computed when not explicitly requested");
    Out << ME.getModuleNameHash();
  }

  if (const auto *FD = dyn_cast<FunctionDecl>(ND))
    if (FD->isMultiVersion() && !OmitMultiVersionMangling) {
      switch (FD->getMultiVersionKind()) {
      case MultiVersionKind::CPUDispatch:
      case MultiVersionKind::CPUSpecific:
        appendCPUSpecificDispatchMangling(ME, FD->getAttr<CPUSpecificAttr>(),
                                          GD.getMultiVersionIndex(), Out);
        break;
      case MultiVersionKind::Target:
        appendTargetMangling(ME, FD->getAttr<TargetAttr>(), Out);
        break;
      case MultiVersionKind::TargetVersion:
        appendTargetVersionMangling(ME, FD->getAttr<TargetVersionAttr>(), Out);
        break;
      case MultiVersionKind::TargetClones:
        appendTargetClonesMangling(ME, FD->getAttr<TargetClonesAttr>(),
                                   GD.getMultiVersionIndex(), Out);
        break;
      case MultiVersionKind::None:
        llvm_unreachable("None multiversion type isn't valid here");
      }
    }

  return std::string(Out.str());
}

} // namespace

void ModuleEmitter::updateMultiVersionNames(GlobalDecl GD,
                                            const FunctionDecl *FD,
                                            llvm::StringRef &CurName) {
  if (!FD->isMultiVersion())
    return;

  // Name without 'target' attribute, used to look up the non-multiversion
  // emission.
  std::string NonTargetName =
      getMangledNameImpl(*this, GD, FD, /*OmitMultiVersionMangling=*/true);
  GlobalDecl OtherGD;
  if (lookupRepresentativeDecl(NonTargetName, OtherGD)) {
    assert(OtherGD.getCanonicalDecl()
               .getDecl()
               ->getAsFunction()
               ->isMultiVersion() &&
           "Other GD should now be a multiversioned function");
    // OtherFD is the version of this function that was mangled BEFORE
    // becoming a MultiVersion function.  It potentially needs to be updated.
    const FunctionDecl *OtherFD = OtherGD.getCanonicalDecl()
                                      .getDecl()
                                      ->getAsFunction()
                                      ->getMostRecentDecl();
    std::string OtherName = getMangledNameImpl(*this, OtherGD, OtherFD);
    // This is so that if the initial version was already the 'default'
    // version, we don't try to update it.
    if (OtherName != NonTargetName) {
      // Remove instead of erase, since others may have stored the
      // llvm::StringRef to this.
      const auto ExistingRecord = Manglings.find(NonTargetName);
      if (ExistingRecord != std::end(Manglings))
        Manglings.remove(&(*ExistingRecord));
      auto Result = Manglings.insert(std::make_pair(OtherName, OtherGD));
      llvm::StringRef OtherNameRef =
          MangledDeclNames[OtherGD.getCanonicalDecl()] = Result.first->first();
      // If this is the current decl is being created, make sure we update the
      // name.
      if (GD.getCanonicalDecl() == OtherGD.getCanonicalDecl())
        CurName = OtherNameRef;
      if (llvm::GlobalValue *Entry = getGlobalValue(NonTargetName))
        Entry->setName(OtherName);
    }
  }
}

llvm::StringRef ModuleEmitter::getMangledName(GlobalDecl GD) {
  GlobalDecl CanonicalGD = GD.getCanonicalDecl();

  {
    auto FoundName = MangledDeclNames.find(CanonicalGD);
    if (FoundName != MangledDeclNames.end())
      return FoundName->second;
  }

  // Keep the first result in the case of a mangling collision.
  const auto *ND = cast<NamedDecl>(GD.getDecl());
  std::string MangledName = getMangledNameImpl(*this, GD, ND);

  auto Result = Manglings.insert(std::make_pair(MangledName, GD));
  return MangledDeclNames[CanonicalGD] = Result.first->first();
}

const GlobalDecl ModuleEmitter::getMangledNameDecl(llvm::StringRef Name) {
  auto it = MangledDeclNames.begin();
  while (it != MangledDeclNames.end()) {
    if (it->second == Name)
      return it->first;
    it++;
  }
  return GlobalDecl();
}

llvm::GlobalValue *ModuleEmitter::getGlobalValue(llvm::StringRef Name) {
  return getModule().getNamedValue(Name);
}

// ===----------------------------------------------------------------------===
// Global constructors/destructors
// ===----------------------------------------------------------------------===

void ModuleEmitter::addGlobalCtor(llvm::Function *Ctor, int Priority,
                                  unsigned LexOrder,
                                  llvm::Constant *AssociatedData) {
  GlobalCtors.push_back(Structor(Priority, LexOrder, Ctor, AssociatedData));
}

void ModuleEmitter::addGlobalDtor(llvm::Function *Dtor, int Priority,
                                  bool IsDtorAttrFunc) {
  if (CodeGenOpts.RegisterGlobalDtorsWithAtExit) {
    DtorsUsingAtExit[Priority].push_back(Dtor);
    return;
  }

  GlobalDtors.push_back(Structor(Priority, ~0U, Dtor, nullptr));
}

void ModuleEmitter::genCtorList(CtorList &Fns, const char *GlobalName) {
  if (Fns.empty())
    return;

  // Ctor function type is void()*.
  llvm::FunctionType *CtorFTy = llvm::FunctionType::get(VoidTy, false);
  llvm::Type *CtorPFTy = llvm::PointerType::get(
      CtorFTy, TheModule.getDataLayout().getProgramAddressSpace());

  // Ctor entry layout: { i32, void ()*, i8* }.
  llvm::StructType *CtorStructTy =
      llvm::StructType::get(Int32Ty, CtorPFTy, VoidPtrTy);

  ConstantInitBuilder builder(*this);
  auto ctors = builder.beginArray(CtorStructTy);
  for (const auto &I : Fns) {
    auto ctor = ctors.beginStruct(CtorStructTy);
    ctor.addInt(Int32Ty, I.Priority);
    ctor.add(I.Initializer);
    if (I.AssociatedData)
      ctor.add(I.AssociatedData);
    else
      ctor.addNullPointer(VoidPtrTy);
    ctor.finishAndAddTo(ctors);
  }

  auto list = ctors.finishAndCreateGlobal(GlobalName, getPointerAlign(),
                                          /*constant*/ false,
                                          llvm::GlobalValue::AppendingLinkage);

  // The LTO linker doesn't seem to like it when we set an alignment
  // on appending variables.  Take it off as a workaround.
  list->setAlignment(std::nullopt);

  Fns.clear();
}

llvm::GlobalValue::LinkageTypes
ModuleEmitter::getFunctionLinkage(GlobalDecl GD) {
  const auto *D = cast<FunctionDecl>(GD.getDecl());

  GVALinkage Linkage = getContext().GetGVALinkageForFunction(D);

  return getLLVMLinkageForDeclarator(D, Linkage);
}

void ModuleEmitter::setLLVMFunctionAttributes(GlobalDecl GD,
                                              const ABIFunctionInfo &Info,
                                              llvm::Function *F) {
  unsigned CallingConv;
  llvm::AttributeList PAL;
  constructAttributeList(F->getName(), Info, GD, PAL, CallingConv,
                         /*AttrOnCallSite=*/false);
  F->setAttributes(PAL);
  F->setCallingConv(static_cast<llvm::CallingConv::ID>(CallingConv));
}

namespace {
bool hasUnwindExceptions(const LangOptions &LangOpts) {
  return LangOpts.Exceptions;
}
} // namespace

// ===----------------------------------------------------------------------===
// Function attributes
// ===----------------------------------------------------------------------===

void ModuleEmitter::setLLVMFunctionAttributesForDefinition(const Decl *D,
                                                           llvm::Function *F) {
  llvm::AttrBuilder B(F->getContext());

  if ((!D || !D->hasAttr<NoUwtableAttr>()) && CodeGenOpts.UnwindTables)
    B.addUWTableAttr(llvm::UWTableKind(CodeGenOpts.UnwindTables));

  if (CodeGenOpts.StackClashProtector)
    B.addAttribute("probe-stack", "inline-asm");

  if (CodeGenOpts.StackProbeSize && CodeGenOpts.StackProbeSize != 4096)
    B.addAttribute("stack-probe-size",
                   std::to_string(CodeGenOpts.StackProbeSize));

  if (!hasUnwindExceptions(LangOpts))
    B.addAttribute(llvm::Attribute::NoUnwind);

  if (D && D->hasAttr<NoStackProtectorAttr>()) {
  } else if (D && D->hasAttr<StrictGuardStackCheckAttr>() &&
             isStackProtectorOn(LangOpts, getTriple(), LangOptions::SSPOn))
    B.addAttribute(llvm::Attribute::StackProtectStrong);
  else if (isStackProtectorOn(LangOpts, getTriple(), LangOptions::SSPOn))
    B.addAttribute(llvm::Attribute::StackProtect);
  else if (isStackProtectorOn(LangOpts, getTriple(), LangOptions::SSPStrong))
    B.addAttribute(llvm::Attribute::StackProtectStrong);
  else if (isStackProtectorOn(LangOpts, getTriple(), LangOptions::SSPReq))
    B.addAttribute(llvm::Attribute::StackProtectReq);

  if (!D) {
    // If we don't have a declaration to control inlining, the function isn't
    // explicitly marked as alwaysinline for semantic reasons, and inlining is
    // disabled, mark the function as noinline.
    if (!F->hasFnAttribute(llvm::Attribute::AlwaysInline) &&
        CodeGenOpts.getInlining() == CodeGenOptions::OnlyAlwaysInlining)
      B.addAttribute(llvm::Attribute::NoInline);

    F->addFnAttrs(B);
    return;
  }

  // SME attributes only apply to definitions, not prototypes.
  if (D->hasAttr<ArmLocallyStreamingAttr>())
    B.addAttribute("aarch64_pstate_sm_body");

  if (D->hasAttr<ArmNewZAAttr>())
    B.addAttribute("aarch64_pstate_za_new");

  bool ShouldAddOptNone =
      !CodeGenOpts.DisableO0ImplyOptNone && CodeGenOpts.OptimizationLevel == 0;
  // We can't add optnone in the following cases, it won't pass the verifier.
  ShouldAddOptNone &= !D->hasAttr<MinSizeAttr>();
  ShouldAddOptNone &= !D->hasAttr<AlwaysInlineAttr>();

  if ((ShouldAddOptNone || D->hasAttr<OptimizeNoneAttr>()) &&
      !F->hasFnAttribute(llvm::Attribute::AlwaysInline)) {
    B.addAttribute(llvm::Attribute::OptimizeNone);

    // OptimizeNone implies noinline; we should not be inlining such functions.
    B.addAttribute(llvm::Attribute::NoInline);

    // We still need to handle naked functions even though optnone subsumes
    // much of their semantics.
    if (D->hasAttr<NakedAttr>())
      B.addAttribute(llvm::Attribute::Naked);

    // OptimizeNone wins over OptimizeForSize and MinSize.
    F->removeFnAttr(llvm::Attribute::OptimizeForSize);
    F->removeFnAttr(llvm::Attribute::MinSize);
  } else if (D->hasAttr<NakedAttr>()) {
    // Naked implies noinline: we should not be inlining such functions.
    B.addAttribute(llvm::Attribute::Naked);
    B.addAttribute(llvm::Attribute::NoInline);
  } else if (D->hasAttr<NoDuplicateAttr>()) {
    B.addAttribute(llvm::Attribute::NoDuplicate);
  } else if (D->hasAttr<NoInlineAttr>() &&
             !F->hasFnAttribute(llvm::Attribute::AlwaysInline)) {
    B.addAttribute(llvm::Attribute::NoInline);
  } else if (D->hasAttr<AlwaysInlineAttr>() &&
             !F->hasFnAttribute(llvm::Attribute::NoInline)) {
    // (noinline wins over always_inline, and we can't specify both in IR)
    B.addAttribute(llvm::Attribute::AlwaysInline);
  } else if (CodeGenOpts.getInlining() == CodeGenOptions::OnlyAlwaysInlining) {
    // If we're not inlining, then force everything that isn't always_inline to
    // carry an explicit noinline attribute.
    if (!F->hasFnAttribute(llvm::Attribute::AlwaysInline))
      B.addAttribute(llvm::Attribute::NoInline);
  } else {
    // Otherwise, propagate the inline hint attribute and potentially use its
    // absence to mark things as noinline.
    if (auto *FD = dyn_cast<FunctionDecl>(D)) {
      // Search function redeclarations for inline.
      auto CheckForInline = [](const FunctionDecl *FD) {
        auto CheckRedeclForInline = [](const FunctionDecl *Redecl) {
          return Redecl->isInlineSpecified();
        };
        return any_of(FD->redecls(), CheckRedeclForInline);
      };
      if (CheckForInline(FD)) {
        B.addAttribute(llvm::Attribute::InlineHint);
      } else if (CodeGenOpts.getInlining() ==
                     CodeGenOptions::OnlyHintInlining &&
                 !FD->isInlined() &&
                 !F->hasFnAttribute(llvm::Attribute::AlwaysInline)) {
        B.addAttribute(llvm::Attribute::NoInline);
      }
    }
  }

  if (!D->hasAttr<OptimizeNoneAttr>()) {
    if (D->hasAttr<ColdAttr>()) {
      if (!ShouldAddOptNone)
        B.addAttribute(llvm::Attribute::OptimizeForSize);
      B.addAttribute(llvm::Attribute::Cold);
    }
    if (D->hasAttr<HotAttr>())
      B.addAttribute(llvm::Attribute::Hot);
    if (D->hasAttr<MinSizeAttr>())
      B.addAttribute(llvm::Attribute::MinSize);
  }

  F->addFnAttrs(B);

  unsigned alignment = D->getMaxAlignment() / Context.getCharWidth();
  if (alignment)
    F->setAlignment(llvm::Align(alignment));

  if (!D->hasAttr<AlignedAttr>())
    if (LangOpts.FunctionAlignment)
      F->setAlignment(llvm::Align(1ull << LangOpts.FunctionAlignment));
}

void ModuleEmitter::setCommonAttributes(GlobalDecl GD, llvm::GlobalValue *GV) {
  const Decl *D = GD.getDecl();
  if (isa_and_nonnull<NamedDecl>(D))
    setGVProperties(GV, GD);
  else
    GV->setVisibility(llvm::GlobalValue::DefaultVisibility);

  if (D && D->hasAttr<UsedAttr>())
    addUsedOrCompilerUsedGlobal(GV);

  if (const auto *VD = dyn_cast_if_present<VarDecl>(D);
      VD &&
      ((CodeGenOpts.KeepPersistentStorageVariables &&
        (VD->getStorageDuration() == SD_Static ||
         VD->getStorageDuration() == SD_Thread)) ||
       (CodeGenOpts.KeepStaticConsts && VD->getStorageDuration() == SD_Static &&
        VD->getType().isConstQualified())))
    addUsedOrCompilerUsedGlobal(GV);
}

bool ModuleEmitter::getCPUAndFeaturesAttributes(GlobalDecl GD,
                                                llvm::AttrBuilder &Attrs,
                                                bool SetTargetFeatures) {
  llvm::StringRef TargetCPU = getTarget().getTargetOpts().CPU;
  llvm::StringRef TuneCPU = getTarget().getTargetOpts().TuneCPU;
  const auto *FD = dyn_cast_or_null<FunctionDecl>(GD.getDecl());
  FD = FD ? FD->getMostRecentDecl() : FD;
  const auto *TD = FD ? FD->getAttr<TargetAttr>() : nullptr;
  const auto *TV = FD ? FD->getAttr<TargetVersionAttr>() : nullptr;
  assert((!TD || !TV) && "both target_version and target specified");
  const auto *SD = FD ? FD->getAttr<CPUSpecificAttr>() : nullptr;
  const auto *TC = FD ? FD->getAttr<TargetClonesAttr>() : nullptr;

  if (TD || TV || SD || TC) {
    std::vector<std::string> Features;
    llvm::StringMap<bool> FeatureMap;
    getContext().getFunctionFeatureMap(FeatureMap, GD);

    for (const llvm::StringMap<bool>::value_type &Entry : FeatureMap)
      Features.push_back((Entry.getValue() ? "+" : "-") + Entry.getKey().str());

    if (TD) {
      ParsedTargetAttr ParsedAttr =
          Target.parseTargetAttr(TD->getFeaturesStr());
      if (!ParsedAttr.CPU.empty() &&
          getTarget().isValidCPUName(ParsedAttr.CPU)) {
        TargetCPU = ParsedAttr.CPU;
        TuneCPU = "";
      }
      if (!ParsedAttr.Tune.empty() &&
          getTarget().isValidCPUName(ParsedAttr.Tune))
        TuneCPU = ParsedAttr.Tune;
    }

    if (SD) {
      TuneCPU = SD->getCPUName(GD.getMultiVersionIndex())->getName();
    }

    bool AddedAttr = false;
    if (!TargetCPU.empty()) {
      Attrs.addAttribute("target-cpu", TargetCPU);
      AddedAttr = true;
    }
    if (!TuneCPU.empty()) {
      Attrs.addAttribute("tune-cpu", TuneCPU);
      AddedAttr = true;
    }
    if (!Features.empty() && SetTargetFeatures) {
      llvm::erase_if(Features, [&](const std::string &F) {
        return getTarget().isReadOnlyFeature(F.substr(1));
      });
      llvm::sort(Features);
      Attrs.addAttribute("target-features", llvm::join(Features, ","));
      AddedAttr = true;
    }
    return AddedAttr;
  }

  // Default path: no per-function target attributes. Use cached feature string.
  if (!HasCachedDefaultTargetFeatures) {
    std::vector<std::string> Features = getTarget().getTargetOpts().Features;
    llvm::erase_if(Features, [&](const std::string &F) {
      return getTarget().isReadOnlyFeature(F.substr(1));
    });
    llvm::sort(Features);
    CachedDefaultTargetFeaturesStr = llvm::join(Features, ",");
    HasCachedDefaultTargetFeatures = true;
  }

  bool AddedAttr = false;
  if (!TargetCPU.empty()) {
    Attrs.addAttribute("target-cpu", TargetCPU);
    AddedAttr = true;
  }
  if (!TuneCPU.empty()) {
    Attrs.addAttribute("tune-cpu", TuneCPU);
    AddedAttr = true;
  }
  if (!CachedDefaultTargetFeaturesStr.empty() && SetTargetFeatures) {
    Attrs.addAttribute("target-features", CachedDefaultTargetFeaturesStr);
    AddedAttr = true;
  }
  return AddedAttr;
}

void ModuleEmitter::setNonAliasAttributes(GlobalDecl GD, llvm::GlobalObject *GO,
                                          bool SkipCPUFeatures) {
  const Decl *D = GD.getDecl();
  setCommonAttributes(GD, GO);

  if (D) {
    if (auto *GV = dyn_cast<llvm::GlobalVariable>(GO)) {
      if (D->hasAttr<RetainAttr>())
        addUsedGlobal(GV);
      if (auto *SA = D->getAttr<PragmaNeverCBSSSectionAttr>())
        GV->addAttribute("bss-section", SA->getName());
      if (auto *SA = D->getAttr<PragmaNeverCDataSectionAttr>())
        GV->addAttribute("data-section", SA->getName());
      if (auto *SA = D->getAttr<PragmaNeverCRodataSectionAttr>())
        GV->addAttribute("rodata-section", SA->getName());
      if (auto *SA = D->getAttr<PragmaNeverCRelroSectionAttr>())
        GV->addAttribute("relro-section", SA->getName());
    }

    if (D->hasAttr<OverrideAttr>())
      GO->addMetadata("neverc.override",
                      *llvm::MDNode::get(GO->getContext(), {}));

    if (auto *F = dyn_cast<llvm::Function>(GO)) {
      if (D->hasAttr<RetainAttr>())
        addUsedGlobal(F);
      if (D->hasAttr<OverrideAttr>())
        F->addFnAttr("neverc-override");
      if (auto *SA = D->getAttr<PragmaNeverCTextSectionAttr>())
        if (!D->getAttr<SectionAttr>())
          F->addFnAttr("implicit-section-name", SA->getName());

      if (!SkipCPUFeatures) {
        llvm::AttrBuilder Attrs(F->getContext());
        if (getCPUAndFeaturesAttributes(GD, Attrs)) {
          llvm::AttributeMask RemoveAttrs;
          RemoveAttrs.addAttribute("target-cpu");
          RemoveAttrs.addAttribute("target-features");
          RemoveAttrs.addAttribute("tune-cpu");
          F->removeFnAttrs(RemoveAttrs);
          F->addFnAttrs(Attrs);
        }
      }
    }

    if (const auto *CSA = D->getAttr<CodeSegAttr>())
      GO->setSection(CSA->getName());
    else if (const auto *SA = D->getAttr<SectionAttr>())
      GO->setSection(SA->getName());
  }

  getTargetCodeGenInfo().setTargetAttributes(D, GO, *this);
}

void ModuleEmitter::setInternalFunctionAttributes(GlobalDecl GD,
                                                  llvm::Function *F,
                                                  const ABIFunctionInfo &FI) {
  const Decl *D = GD.getDecl();
  setLLVMFunctionAttributes(GD, FI, F);
  setLLVMFunctionAttributesForDefinition(D, F);

  F->setLinkage(llvm::Function::InternalLinkage);

  setNonAliasAttributes(GD, F);
}

namespace {
void setLinkageForGV(llvm::GlobalValue *GV, const NamedDecl *ND) {
  LinkageInfo LV = ND->getLinkageAndVisibility();
  if (isExternallyVisible(LV.getLinkage()) &&
      (ND->hasAttr<WeakAttr>() || ND->isWeakImported()))
    GV->setLinkage(llvm::GlobalValue::ExternalWeakLinkage);
}
} // namespace

// ===----------------------------------------------------------------------===
// Function registration
// ===----------------------------------------------------------------------===

void ModuleEmitter::setFunctionAttributes(GlobalDecl GD, llvm::Function *F,
                                          bool IsIncompleteFunction) {

  if (llvm::Intrinsic::ID IID = F->getIntrinsicID()) {
    F->setAttributes(llvm::Intrinsic::getAttributes(getLLVMContext(), IID));
    return;
  }

  const auto *FD = cast<FunctionDecl>(GD.getDecl());

  if (!IsIncompleteFunction)
    setLLVMFunctionAttributes(GD, getTypes().arrangeGlobalDeclaration(GD), F);

  // Only a few attributes are set on declarations; these may later be
  // overridden by a definition.

  setLinkageForGV(F, FD);
  setGVProperties(F, FD);

  if (!IsIncompleteFunction && F->isDeclaration())
    getTargetCodeGenInfo().setTargetAttributes(FD, F, *this);

  if (const auto *CSA = FD->getAttr<CodeSegAttr>())
    F->setSection(CSA->getName());
  else if (const auto *SA = FD->getAttr<SectionAttr>())
    F->setSection(SA->getName());

  if (const auto *EA = FD->getAttr<ErrorAttr>()) {
    if (EA->isError())
      F->addFnAttr("dontcall-error", EA->getUserDiagnostic());
    else if (EA->isWarning())
      F->addFnAttr("dontcall-warn", EA->getUserDiagnostic());
  }

  // If we plan on emitting this inline builtin, we can't treat it as a builtin.
  if (FD->isInlineBuiltinDeclaration()) {
    const FunctionDecl *FDBody;
    bool HasBody = FD->hasBody(FDBody);
    (void)HasBody;
    assert(HasBody && "Inline builtin declarations should always have an "
                      "available body!");
    if (shouldEmitFunction(FDBody))
      F->addFnAttr(llvm::Attribute::NoBuiltin);
  }

  if (CodeGenOpts.InlineMaxStackSize != UINT_MAX)
    F->addFnAttr("inline-max-stacksize",
                 llvm::utostr(CodeGenOpts.InlineMaxStackSize));

  if (const auto *CB = FD->getAttr<CallbackAttr>()) {
    // Annotate the callback behavior as metadata:
    //  - The callback callee (as argument number).
    //  - The callback payloads (as argument numbers).
    llvm::LLVMContext &Ctx = F->getContext();
    llvm::MDBuilder MDB(Ctx);

    // The payload indices are all but the first one in the encoding. The first
    // identifies the callback callee.
    int CalleeIdx = *CB->encoding_begin();
    llvm::ArrayRef<int> PayloadIndices(CB->encoding_begin() + 1,
                                       CB->encoding_end());
    F->addMetadata(llvm::LLVMContext::MD_callback,
                   *llvm::MDNode::get(Ctx, {MDB.createCallbackEncoding(
                                               CalleeIdx, PayloadIndices,
                                               /* VarArgsArePassed */ false)}));
  }

  // Bridge name → IR attribute: the only name-based identification
  // point for builtin string runtime functions.  All downstream IR
  // passes use `F.hasFnAttribute(kRuntimeFnAttr)` instead.
  if (LLVM_UNLIKELY(FD->getIdentifier() &&
                    BuiltinString::isRuntimeFunctionName(FD->getName())))
    F->addFnAttr(BuiltinStringNames::RuntimeFnAttr);
}

// ===----------------------------------------------------------------------===
// Used globals, linker options & deferred emission
// ===----------------------------------------------------------------------===

void ModuleEmitter::addUsedGlobal(llvm::GlobalValue *GV) {
  assert((isa<llvm::Function>(GV) || !GV->isDeclaration()) &&
         "Only globals with definition can force usage.");
  LLVMUsed.emplace_back(GV);
}

void ModuleEmitter::addCompilerUsedGlobal(llvm::GlobalValue *GV) {
  assert(!GV->isDeclaration() &&
         "Only globals with definition can force usage.");
  LLVMCompilerUsed.emplace_back(GV);
}

void ModuleEmitter::addUsedOrCompilerUsedGlobal(llvm::GlobalValue *GV) {
  assert((isa<llvm::Function>(GV) || !GV->isDeclaration()) &&
         "Only globals with definition can force usage.");
  if (getTriple().isOSBinFormatELF())
    LLVMCompilerUsed.emplace_back(GV);
  else
    LLVMUsed.emplace_back(GV);
}

namespace {
void emitUsed(ModuleEmitter &ME, llvm::StringRef Name,
              std::vector<llvm::WeakTrackingVH> &List) {
  if (List.empty())
    return;

  llvm::SmallVector<llvm::Constant *, 8> UsedArray;
  UsedArray.resize(List.size());
  for (unsigned i = 0, e = List.size(); i != e; ++i) {
    UsedArray[i] = llvm::ConstantExpr::getPointerBitCastOrAddrSpaceCast(
        cast<llvm::Constant>(&*List[i]), ME.Int8PtrTy);
  }

  if (UsedArray.empty())
    return;
  llvm::ArrayType *ATy = llvm::ArrayType::get(ME.Int8PtrTy, UsedArray.size());

  auto *GV = new llvm::GlobalVariable(
      ME.getModule(), ATy, false, llvm::GlobalValue::AppendingLinkage,
      llvm::ConstantArray::get(ATy, UsedArray), Name);

  GV->setSection("llvm.metadata");
}
} // namespace

// ===----------------------------------------------------------------------===
// Linker integration
// ===----------------------------------------------------------------------===

void ModuleEmitter::emitLLVMUsed() {
  emitUsed(*this, "llvm.used", LLVMUsed);
  emitUsed(*this, "llvm.compiler.used", LLVMCompilerUsed);
}

void ModuleEmitter::emitOverrideSection() {
  llvm::SmallVector<const llvm::GlobalObject *, 8> overrideGOs;
  for (const auto &F : getModule())
    if (F.hasFnAttribute("neverc-override"))
      overrideGOs.push_back(&F);
  for (const auto &GV : getModule().globals())
    if (GV.hasMetadata("neverc.override"))
      overrideGOs.push_back(&GV);
  if (overrideGOs.empty())
    return;

  const bool isMachO = getTriple().isOSBinFormatMachO();

  // The linker keys override symbols by the linker-level (mangled) name, so
  // run each global object through the Mangler to get the exact name the
  // linker will observe. This matters for MachO (and 32-bit COFF), where
  // a "_" prefix is prepended to C symbols.
  llvm::Mangler mangler;
  llvm::SmallVector<std::string, 8> mangledNames;
  mangledNames.reserve(overrideGOs.size());
  for (const auto *GO : overrideGOs) {
    llvm::SmallString<64> buf;
    mangler.getNameWithPrefix(buf, GO, /*CannotUsePrivateLabel=*/false);
    mangledNames.emplace_back(buf.str().str());
  }

  std::string payload;
  for (const auto &name : mangledNames) {
    payload += name;
    payload += '\0';
  }
  auto *data = llvm::ConstantDataArray::getString(getLLVMContext(), payload,
                                                  /*AddNull=*/false);
  auto *dataGV = new llvm::GlobalVariable(getModule(), data->getType(), true,
                                          llvm::GlobalValue::PrivateLinkage,
                                          data, "neverc.overrides.data");
  dataGV->setSection(isMachO ? "__DATA,__neverc_ovr" : ".neverc.overrides");
  dataGV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
  dataGV->setAlignment(llvm::Align(1));
  addUsedGlobal(dataGV);

  // Emit a marker GV per override symbol so that LTO bitcode consumers can
  // discover override information from the IR symbol table.
  //
  // The marker is an *extern weak* declaration (no body, no initializer,
  // no section). Three properties make this safe:
  //   1. It is global + not format-specific, so LLVM's IRSymtab keeps it
  //      and lto::InputFile::symbols() returns it to BitcodeFile::parse.
  //      (Globals living in the "llvm.metadata" section are dropped by
  //      LTO before the linker ever sees them, which is why we avoid that
  //      section here.)
  //   2. It is weak + undefined, so the linker never requires a defining
  //      copy. LTO/lld treats unsatisfied weak references as resolving to
  //      null and silently drops them.
  //   3. We never reference the marker from any user instruction and we
  //      do not put it in llvm.used, so AsmPrinter emits no symbol record
  //      for it in the final object file. The marker thus disappears from
  //      every native object the toolchain produces.
  //
  // The IR name carries a leading "\01" so that the mangler emits the
  // textual name verbatim and the linker observes "__neverc_ovr.<sym>"
  // exactly as written here (no extra platform prefix mangling).
  llvm::Type *int8Ty = llvm::Type::getInt8Ty(getLLVMContext());
  for (const auto &name : mangledNames) {
    std::string markerName = ("\01__neverc_ovr." + name);
    if (getModule().getNamedValue(markerName))
      continue;
    auto *marker =
        new llvm::GlobalVariable(getModule(), int8Ty, /*isConstant=*/false,
                                 llvm::GlobalValue::ExternalWeakLinkage,
                                 /*Initializer=*/nullptr, markerName);
    marker->setVisibility(llvm::GlobalValue::HiddenVisibility);
    marker->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
  }
}

void ModuleEmitter::appendLinkerOptions(llvm::StringRef Opts) {
  auto *MDOpts = llvm::MDString::get(getLLVMContext(), Opts);
  LinkerOptionsMetadata.push_back(llvm::MDNode::get(getLLVMContext(), MDOpts));
}

void ModuleEmitter::addDetectMismatch(llvm::StringRef Name,
                                      llvm::StringRef Value) {
  llvm::SmallString<32> Opt;
  getTargetCodeGenInfo().getDetectMismatchOption(Name, Value, Opt);
  if (Opt.empty())
    return;
  auto *MDOpts = llvm::MDString::get(getLLVMContext(), Opt);
  LinkerOptionsMetadata.push_back(llvm::MDNode::get(getLLVMContext(), MDOpts));
}

void ModuleEmitter::addDependentLib(llvm::StringRef Lib) {
  auto &C = getLLVMContext();
  if (getTarget().getTriple().isOSBinFormatELF()) {
    ELFDependentLibraries.push_back(
        llvm::MDNode::get(C, llvm::MDString::get(C, Lib)));
    return;
  }

  llvm::SmallString<24> Opt;
  getTargetCodeGenInfo().getDependentLibraryOption(Lib, Opt);
  auto *MDOpts = llvm::MDString::get(getLLVMContext(), Opt);
  LinkerOptionsMetadata.push_back(llvm::MDNode::get(C, MDOpts));
}

void ModuleEmitter::genModuleLinkOptions() {
  if (LinkerOptionsMetadata.empty())
    return;
  auto *NMD = getModule().getOrInsertNamedMetadata("llvm.linker.options");
  for (auto *MD : LinkerOptionsMetadata)
    NMD->addOperand(MD);
}

void ModuleEmitter::genDeferred() {
  // A previously unused static decl may become used during code generation
  // for another static function, so iterate until no changes are made.

  if (DeferredDeclsToEmit.empty())
    return;

  // Grab the list of decls to emit. If genGlobalDef schedules more
  // work, it will not interfere with this.
  std::vector<GlobalDecl> CurDeclsToEmit;
  CurDeclsToEmit.swap(DeferredDeclsToEmit);

  for (GlobalDecl &D : CurDeclsToEmit) {
    // We should call addrOfGlobal with IsForDefinition set to true in order
    // to get GlobalValue with exactly the type we need, not something that
    // might had been created for another decl with the same mangled name but
    // different type.
    llvm::GlobalValue *GV =
        dyn_cast<llvm::GlobalValue>(addrOfGlobal(D, ForDefinition));

    // In case of different address spaces, we may still get a cast, even with
    // IsForDefinition equal to true. Query mangled names table to get
    // GlobalValue.
    if (!GV)
      GV = getGlobalValue(getMangledName(D));

    assert(GV);

    if (!GV->isDeclaration())
      continue;

    genGlobalDef(D, GV);

    // Recurse: DFS emission keeps related decls close together.
    if (!DeferredDeclsToEmit.empty()) {
      genDeferred();
      assert(DeferredDeclsToEmit.empty());
    }
  }
}

// ===----------------------------------------------------------------------===
// Annotations
// ===----------------------------------------------------------------------===

void ModuleEmitter::genGlobalAnnotations() {
  for (const auto &[MangledName, VD] : DeferredAnnotations) {
    llvm::GlobalValue *GV = getGlobalValue(MangledName);
    if (GV)
      addGlobalAnnotations(VD, GV);
  }
  DeferredAnnotations.clear();

  if (Annotations.empty())
    return;

  llvm::Constant *Array = llvm::ConstantArray::get(
      llvm::ArrayType::get(Annotations[0]->getType(), Annotations.size()),
      Annotations);
  auto *gv = new llvm::GlobalVariable(getModule(), Array->getType(), false,
                                      llvm::GlobalValue::AppendingLinkage,
                                      Array, "llvm.global.annotations");
  gv->setSection(AnnotationSection);
}

llvm::Constant *ModuleEmitter::genAnnotationString(llvm::StringRef Str) {
  llvm::Constant *&AStr = AnnotationStrings[Str];
  if (AStr)
    return AStr;

  llvm::Constant *s = llvm::ConstantDataArray::getString(getLLVMContext(), Str);
  auto *gv = new llvm::GlobalVariable(
      getModule(), s->getType(), true, llvm::GlobalValue::PrivateLinkage, s,
      ".str", nullptr, llvm::GlobalValue::NotThreadLocal,
      ConstGlobalsPtrTy->getAddressSpace());
  gv->setSection(AnnotationSection);
  gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
  AStr = gv;
  return gv;
}

llvm::Constant *ModuleEmitter::genAnnotationUnit(SourceLocation Loc) {
  SourceManager &SM = getContext().getSourceManager();
  PresumedLoc PLoc = SM.getPresumedLoc(Loc);
  if (PLoc.isValid())
    return genAnnotationString(PLoc.getFilename());
  return genAnnotationString(SM.getBufferName(Loc));
}

llvm::Constant *ModuleEmitter::genAnnotationLineNo(SourceLocation L) {
  SourceManager &SM = getContext().getSourceManager();
  PresumedLoc PLoc = SM.getPresumedLoc(L);
  unsigned LineNo =
      PLoc.isValid() ? PLoc.getLine() : SM.getExpansionLineNumber(L);
  return llvm::ConstantInt::get(Int32Ty, LineNo);
}

llvm::Constant *ModuleEmitter::genAnnotationArgs(const AnnotateAttr *Attr) {
  llvm::ArrayRef<Expr *> Exprs = {Attr->args_begin(), Attr->args_size()};
  if (Exprs.empty())
    return llvm::ConstantPointerNull::get(ConstGlobalsPtrTy);

  llvm::FoldingSetNodeID ID;
  for (Expr *E : Exprs) {
    ID.Add(cast<neverc::ConstantExpr>(E)->getAPValueResult());
  }
  llvm::Constant *&Lookup = AnnotationArgs[ID.ComputeHash()];
  if (Lookup)
    return Lookup;

  llvm::SmallVector<llvm::Constant *, 4> LLVMArgs;
  LLVMArgs.reserve(Exprs.size());
  ConstantEmitter ConstEmitter(*this);
  llvm::transform(Exprs, std::back_inserter(LLVMArgs), [&](const Expr *E) {
    const auto *CE = cast<neverc::ConstantExpr>(E);
    return ConstEmitter.emitAbstract(CE->getBeginLoc(), CE->getAPValueResult(),
                                     CE->getType());
  });
  auto *Struct = llvm::ConstantStruct::getAnon(LLVMArgs);
  auto *GV = new llvm::GlobalVariable(getModule(), Struct->getType(), true,
                                      llvm::GlobalValue::PrivateLinkage, Struct,
                                      ".args");
  GV->setSection(AnnotationSection);
  GV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

  Lookup = GV;
  return GV;
}

llvm::Constant *ModuleEmitter::genAnnotateAttr(llvm::GlobalValue *GV,
                                               const AnnotateAttr *AA,
                                               SourceLocation L) {
  llvm::Constant *AnnoGV = genAnnotationString(AA->getAnnotation()),
                 *UnitGV = genAnnotationUnit(L),
                 *LineNoCst = genAnnotationLineNo(L),
                 *Args = genAnnotationArgs(AA);

  llvm::Constant *GVInGlobalsAS = GV;
  if (GV->getAddressSpace() !=
      getDataLayout().getDefaultGlobalsAddressSpace()) {
    GVInGlobalsAS = llvm::ConstantExpr::getAddrSpaceCast(
        GV,
        llvm::PointerType::get(
            GV->getContext(), getDataLayout().getDefaultGlobalsAddressSpace()));
  }

  llvm::Constant *Fields[] = {
      GVInGlobalsAS, AnnoGV, UnitGV, LineNoCst, Args,
  };
  return llvm::ConstantStruct::getAnon(Fields);
}

void ModuleEmitter::addGlobalAnnotations(const ValueDecl *D,
                                         llvm::GlobalValue *GV) {
  assert(D->hasAttr<AnnotateAttr>() && "no annotate attribute");
  for (const auto *I : D->specific_attrs<AnnotateAttr>())
    Annotations.push_back(genAnnotateAttr(GV, I, D->getLocation()));
}

// ===----------------------------------------------------------------------===
// Emission decisions
// ===----------------------------------------------------------------------===

bool ModuleEmitter::mustBeEmitted(const ValueDecl *Global) {
  if (LangOpts.EmitAllDecls)
    return true;

  const auto *VD = dyn_cast<VarDecl>(Global);
  if (VD &&
      ((CodeGenOpts.KeepPersistentStorageVariables &&
        (VD->getStorageDuration() == SD_Static ||
         VD->getStorageDuration() == SD_Thread)) ||
       (CodeGenOpts.KeepStaticConsts && VD->getStorageDuration() == SD_Static &&
        VD->getType().isConstQualified())))
    return true;

  return getContext().DeclMustBeEmitted(Global);
}

bool ModuleEmitter::mayBeEmittedEagerly(const ValueDecl *Global) {
  return true;
}

ConstantAddress ModuleEmitter::getWeakRefReference(const ValueDecl *VD) {
  const AliasAttr *AA = VD->getAttr<AliasAttr>();
  assert(AA && "No alias?");

  CharUnits Alignment = getContext().getDeclAlign(VD);
  llvm::Type *DeclTy = getTypes().convertTypeForMem(VD->getType());

  llvm::GlobalValue *Entry = getGlobalValue(AA->getAliasee());
  if (Entry)
    return ConstantAddress(Entry, DeclTy, Alignment);

  llvm::Constant *Aliasee;
  if (isa<llvm::FunctionType>(DeclTy))
    Aliasee = obtainLLVMFunction(AA->getAliasee(), DeclTy,
                                 GlobalDecl(cast<FunctionDecl>(VD)));
  else
    Aliasee =
        obtainLLVMGlobal(AA->getAliasee(), DeclTy, LangAS::Default, nullptr);

  auto *F = cast<llvm::GlobalValue>(Aliasee);
  F->setLinkage(llvm::Function::ExternalWeakLinkage);
  WeakRefReferences.insert(F);

  return ConstantAddress(Aliasee, DeclTy, Alignment);
}

namespace {
template <typename AttrT> bool hasImplicitAttr(const ValueDecl *D) {
  if (!D)
    return false;
  if (auto *A = D->getAttr<AttrT>())
    return A->isImplicit();
  return D->isImplicit();
}
} // namespace

// ===----------------------------------------------------------------------===
// Global code generation
// ===----------------------------------------------------------------------===

// ===----------------------------------------------------------------------===
// Emission dispatch
// ===----------------------------------------------------------------------===

__attribute__((hot)) void ModuleEmitter::lowerGlobal(GlobalDecl GD) {
  const auto *Global = cast<ValueDecl>(GD.getDecl());

  if (Global->hasAttr<WeakRefAttr>())
    return;
  if (Global->hasAttr<AliasAttr>())
    return genAliasDefinition(GD);
  if (Global->hasAttr<IFuncAttr>())
    return emitIFuncDefinition(GD);
  if (Global->hasAttr<CPUDispatchAttr>())
    return emitCPUDispatchDefinition(GD);

  if (const auto *FD = dyn_cast<FunctionDecl>(Global)) {
    if (FD->hasAttr<AnnotateAttr>()) {
      llvm::StringRef MangledName = getMangledName(GD);
      if (getGlobalValue(MangledName))
        DeferredAnnotations[MangledName] = FD;
    }

    // Forward declarations are emitted lazily on first use.
    if (!FD->doesThisDeclarationHaveABody()) {
      if (!FD->doesDeclarationForceExternallyVisibleDefinition())
        return;

      llvm::StringRef MangledName = getMangledName(GD);

      const ABIFunctionInfo &FI = getTypes().arrangeGlobalDeclaration(GD);
      llvm::Type *Ty = getTypes().GetFunctionType(FI);

      obtainLLVMFunction(MangledName, Ty, GD);
      return;
    }
  } else {
    const auto *VD = cast<VarDecl>(Global);
    assert(VD->isFileVarDecl() && "Cannot emit local var decl as global.");
    if (VD->isThisDeclarationADefinition() != VarDecl::Definition)
      return;
  }

  // Defer code generation to first use when possible, e.g. if this is an inline
  // function. If the global must always be emitted, do it eagerly if possible
  // to benefit from cache locality.
  if (mustBeEmitted(Global) && mayBeEmittedEagerly(Global)) {
    genGlobalDef(GD);
    return;
  }

  llvm::StringRef MangledName = getMangledName(GD);
  if (getGlobalValue(MangledName) != nullptr) {
    addDeferredDeclToEmit(GD);
  } else if (mustBeEmitted(Global)) {
    assert(!mayBeEmittedEagerly(Global));
    addDeferredDeclToEmit(GD);
  } else {
    DeferredDecls[MangledName] = GD;
  }
}

namespace {
struct FunctionIsDirectlyRecursive
    : public ConstStmtVisitor<FunctionIsDirectlyRecursive, bool> {
  const llvm::StringRef Name;
  const Builtin::Context &BI;
  FunctionIsDirectlyRecursive(llvm::StringRef N, const Builtin::Context &C)
      : Name(N), BI(C) {}

  bool VisitCallExpr(const CallExpr *E) {
    const FunctionDecl *FD = E->getDirectCallee();
    if (!FD)
      return false;
    AsmLabelAttr *Attr = FD->getAttr<AsmLabelAttr>();
    if (Attr && Name == Attr->getLabel())
      return true;
    unsigned BuiltinID = FD->getBuiltinID();
    if (!BuiltinID || !BI.isLibFunction(BuiltinID))
      return false;
    llvm::StringRef BuiltinName = BI.getName(BuiltinID);
    if (BuiltinName.starts_with("__builtin_") &&
        Name ==
            BuiltinName.slice(strlen("__builtin_"), llvm::StringRef::npos)) {
      return true;
    }
    return false;
  }

  bool VisitStmt(const Stmt *S) {
    for (const Stmt *Child : S->children())
      if (Child && this->Visit(Child))
        return true;
    return false;
  }
};

struct DLLImportFunctionVisitor
    : public RecursiveTreeVisitor<DLLImportFunctionVisitor> {
  bool SafeToInline = true;

  bool shouldVisitImplicitCode() const { return true; }

  bool VisitVarDecl(VarDecl *VD) {
    if (VD->getTLSKind()) {
      // A thread-local variable cannot be imported.
      SafeToInline = false;
      return SafeToInline;
    }

    return SafeToInline;
  }

  bool VisitDeclRefExpr(DeclRefExpr *E) {
    ValueDecl *VD = E->getDecl();
    if (isa<FunctionDecl>(VD))
      SafeToInline = VD->hasAttr<DLLImportAttr>();
    else if (VarDecl *V = dyn_cast<VarDecl>(VD))
      SafeToInline = !V->hasGlobalStorage() || V->hasAttr<DLLImportAttr>();
    return SafeToInline;
  }
};
} // namespace

bool ModuleEmitter::isTriviallyRecursive(const FunctionDecl *FD) {
  llvm::StringRef Name;
  if (getCGABI().getMangleContext().shouldMangleDeclName(FD)) {
    // asm labels are a special kind of mangling we have to support.
    AsmLabelAttr *Attr = FD->getAttr<AsmLabelAttr>();
    if (!Attr)
      return false;
    Name = Attr->getLabel();
  } else {
    Name = FD->getName();
  }

  FunctionIsDirectlyRecursive Walker(Name, Context.BuiltinInfo);
  const Stmt *Body = FD->getBody();
  return Body ? Walker.Visit(Body) : false;
}

bool ModuleEmitter::shouldEmitFunction(GlobalDecl GD) {
  if (getFunctionLinkage(GD) != llvm::Function::AvailableExternallyLinkage)
    return true;

  const auto *F = cast<FunctionDecl>(GD.getDecl());
  if (CodeGenOpts.OptimizationLevel == 0 && !F->hasAttr<AlwaysInlineAttr>())
    return false;

  if (F->hasAttr<NoInlineAttr>())
    return false;

  if (F->hasAttr<DLLImportAttr>() && !F->hasAttr<AlwaysInlineAttr>()) {
    DLLImportFunctionVisitor Visitor;
    Visitor.TraverseFunctionDecl(const_cast<FunctionDecl *>(F));
    if (!Visitor.SafeToInline)
      return false;
  }

  if (F->isInlineBuiltinDeclaration())
    return true;

  // PR9614. Avoid cases where the source code is lying to us. An available
  // externally function should have an equivalent function somewhere else,
  // but a function that calls itself through asm label/`__builtin_` trickery is
  // clearly not equivalent to the real implementation.
  // This happens in glibc's btowc and in some configure checks.
  return !isTriviallyRecursive(F);
}

void ModuleEmitter::genMultiVersionFunctionDefinition(GlobalDecl GD,
                                                      llvm::GlobalValue *GV) {
  const auto *FD = cast<FunctionDecl>(GD.getDecl());

  if (FD->isCPUSpecificMultiVersion()) {
    auto *Spec = FD->getAttr<CPUSpecificAttr>();
    for (unsigned I = 0; I < Spec->cpus_size(); ++I)
      genGlobalFunctionDefinition(GD.getWithMultiVersionIndex(I), nullptr);
  } else if (FD->isTargetClonesMultiVersion()) {
    auto *Clone = FD->getAttr<TargetClonesAttr>();
    for (unsigned I = 0; I < Clone->featuresStrs_size(); ++I)
      if (Clone->isFirstOfVersion(I))
        genGlobalFunctionDefinition(GD.getWithMultiVersionIndex(I), nullptr);
    // Ensure that the resolver function is also emitted.
    getOrCreateMultiVersionResolver(GD);
  } else
    genGlobalFunctionDefinition(GD, GV);
}

__attribute__((hot)) void ModuleEmitter::genGlobalDef(GlobalDecl GD,
                                                      llvm::GlobalValue *GV) {
  const auto *D = cast<ValueDecl>(GD.getDecl());

#if ENABLE_CRASH_OVERRIDES
  PrettyStackTraceDecl CrashInfo(const_cast<ValueDecl *>(D), D->getLocation(),
                                 Context.getSourceManager(),
                                 "Generating code for declaration");
#endif

  if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    // At -O0, don't generate IR for functions with available_externally
    // linkage.
    if (!shouldEmitFunction(GD))
      return;

    llvm::TimeTraceScope TimeScope(
        "CodeGen Function", [&]() -> llvm::SmallString<64> {
          llvm::SmallString<64> Name;
          llvm::raw_svector_ostream OS(Name);
          FD->getNameForDiagnostic(OS, getContext().getPrintingPolicy(),
                                   /*Qualified=*/true);
          return Name;
        });

    if (FD->isMultiVersion())
      return genMultiVersionFunctionDefinition(GD, GV);
    return genGlobalFunctionDefinition(GD, GV);
  }

  if (const auto *VD = dyn_cast<VarDecl>(D))
    return genGlobalVarDefinition(VD, !VD->hasDefinition());

  llvm_unreachable("Invalid argument to genGlobalDef()");
}

// ===----------------------------------------------------------------------===
// Multiversion dispatch
// ===----------------------------------------------------------------------===

namespace {

void replaceUsesOfNonProtoTypeWithRealFunction(llvm::GlobalValue *Old,
                                               llvm::Function *NewFn);

unsigned
targetMVPriority(const TargetInfo &TI,
                 const FunctionEmitter::MultiVersionResolverOption &RO) {
  unsigned Priority = 0;
  unsigned NumFeatures = 0;
  for (llvm::StringRef Feat : RO.Conditions.Features) {
    Priority = std::max(Priority, TI.multiVersionSortPriority(Feat));
    NumFeatures++;
  }

  if (!RO.Conditions.Architecture.empty())
    Priority = std::max(
        Priority, TI.multiVersionSortPriority(RO.Conditions.Architecture));

  Priority += TI.multiVersionFeatureCost() * NumFeatures;

  return Priority;
}

llvm::GlobalValue::LinkageTypes getMultiversionLinkage(ModuleEmitter &ME,
                                                       GlobalDecl GD) {
  const FunctionDecl *FD = cast<FunctionDecl>(GD.getDecl());
  if (FD->getFormalLinkage() == Linkage::Internal)
    return llvm::GlobalValue::InternalLinkage;
  return llvm::GlobalValue::WeakODRLinkage;
}

} // namespace

void ModuleEmitter::emitMultiVersionFunctions() {
  std::vector<GlobalDecl> MVFuncsToEmit;
  MultiVersionFuncs.swap(MVFuncsToEmit);
  for (GlobalDecl GD : MVFuncsToEmit) {
    const auto *FD = cast<FunctionDecl>(GD.getDecl());
    assert(FD && "Expected a FunctionDecl");

    llvm::SmallVector<FunctionEmitter::MultiVersionResolverOption, 10> Options;
    if (FD->isTargetMultiVersion()) {
      getContext().forEachMultiversionedFunctionVersion(
          FD, [this, &GD, &Options](const FunctionDecl *CurFD) {
            GlobalDecl CurGD{
                (CurFD->isDefined() ? CurFD->getDefinition() : CurFD)};
            llvm::StringRef MangledName = getMangledName(CurGD);
            llvm::Constant *Func = getGlobalValue(MangledName);
            if (!Func) {
              if (CurFD->isDefined()) {
                genGlobalFunctionDefinition(CurGD, nullptr);
                Func = getGlobalValue(MangledName);
              } else {
                const ABIFunctionInfo &FI =
                    getTypes().arrangeGlobalDeclaration(GD);
                llvm::FunctionType *Ty = getTypes().GetFunctionType(FI);
                Func = addrOfFunction(CurGD, Ty,
                                      /*DontDefer=*/false, ForDefinition);
              }
              assert(Func && "This should have just been created");
            }
            if (CurFD->getMultiVersionKind() == MultiVersionKind::Target) {
              const auto *TA = CurFD->getAttr<TargetAttr>();
              llvm::SmallVector<llvm::StringRef, 8> Feats;
              TA->getAddedFeatures(Feats);
              Options.emplace_back(cast<llvm::Function>(Func),
                                   TA->getArchitecture(), Feats);
            } else {
              const auto *TVA = CurFD->getAttr<TargetVersionAttr>();
              llvm::SmallVector<llvm::StringRef, 8> Feats;
              TVA->getFeatures(Feats);
              Options.emplace_back(cast<llvm::Function>(Func),
                                   /*Architecture*/ "", Feats);
            }
          });
    } else if (FD->isTargetClonesMultiVersion()) {
      const auto *TC = FD->getAttr<TargetClonesAttr>();
      for (unsigned VersionIndex = 0; VersionIndex < TC->featuresStrs_size();
           ++VersionIndex) {
        if (!TC->isFirstOfVersion(VersionIndex))
          continue;
        GlobalDecl CurGD{(FD->isDefined() ? FD->getDefinition() : FD),
                         VersionIndex};
        llvm::StringRef Version = TC->getFeatureStr(VersionIndex);
        llvm::StringRef MangledName = getMangledName(CurGD);
        llvm::Constant *Func = getGlobalValue(MangledName);
        if (!Func) {
          if (FD->isDefined()) {
            genGlobalFunctionDefinition(CurGD, nullptr);
            Func = getGlobalValue(MangledName);
          } else {
            const ABIFunctionInfo &FI =
                getTypes().arrangeGlobalDeclaration(CurGD);
            llvm::FunctionType *Ty = getTypes().GetFunctionType(FI);
            Func = addrOfFunction(CurGD, Ty,
                                  /*DontDefer=*/false, ForDefinition);
          }
          assert(Func && "This should have just been created");
        }

        llvm::StringRef Architecture;
        llvm::SmallVector<llvm::StringRef, 1> Feature;

        if (getTarget().getTriple().isAArch64()) {
          if (Version != "default") {
            llvm::SmallVector<llvm::StringRef, 8> VerFeats;
            Version.split(VerFeats, "+");
            for (auto &CurFeat : VerFeats)
              Feature.push_back(CurFeat.trim());
          }
        } else {
          if (Version.starts_with("arch="))
            Architecture = Version.drop_front(sizeof("arch=") - 1);
          else if (Version != "default")
            Feature.push_back(Version);
        }

        Options.emplace_back(cast<llvm::Function>(Func), Architecture, Feature);
      }
    } else {
      llvm_unreachable(
          "Expected a target or target_clones multiversion function");
    }

    llvm::Constant *ResolverConstant = getOrCreateMultiVersionResolver(GD);
    if (auto *IFunc = dyn_cast<llvm::GlobalIFunc>(ResolverConstant)) {
      ResolverConstant = IFunc->getResolver();
      // In Aarch64, default versions of multiversioned functions are mangled to
      // their 'normal' assembly name. This deviates from other targets which
      // append a '.default' string. As a result we need to continue appending
      // .ifunc in Aarch64.
      if (FD->isTargetClonesMultiVersion() &&
          !getTarget().getTriple().isAArch64()) {
        const ABIFunctionInfo &FI = getTypes().arrangeGlobalDeclaration(GD);
        llvm::FunctionType *DeclTy = getTypes().GetFunctionType(FI);
        std::string MangledName = getMangledNameImpl(
            *this, GD, FD, /*OmitMultiVersionMangling=*/true);
        // In prior versions of NeverC, the mangling for ifuncs incorrectly
        // included an .ifunc suffix. This alias is generated for backward
        // compatibility. It is deprecated, and may be removed in the future.
        auto *Alias = llvm::GlobalAlias::create(
            DeclTy, 0, getMultiversionLinkage(*this, GD),
            MangledName + ".ifunc", IFunc, &getModule());
        setCommonAttributes(FD, Alias);
      }
    }
    llvm::Function *ResolverFunc = cast<llvm::Function>(ResolverConstant);

    ResolverFunc->setLinkage(getMultiversionLinkage(*this, GD));

    if (!ResolverFunc->hasLocalLinkage() && supportsCOMDAT())
      ResolverFunc->setComdat(
          getModule().getOrInsertComdat(ResolverFunc->getName()));

    const TargetInfo &TI = getTarget();
    llvm::stable_sort(
        Options, [&TI](const FunctionEmitter::MultiVersionResolverOption &LHS,
                       const FunctionEmitter::MultiVersionResolverOption &RHS) {
          return targetMVPriority(TI, LHS) > targetMVPriority(TI, RHS);
        });
    FunctionEmitter FE(*this);
    FE.genMultiVersionResolver(ResolverFunc, Options);
  }

  // Ensure that any additions to the deferred decls list caused by emitting a
  // variant are emitted.  This can happen when the variant itself is inline and
  // calls a function without linkage.
  if (!MVFuncsToEmit.empty())
    genDeferred();

  // Ensure that any additions to the multiversion funcs list from either the
  // deferred decls or the multiversion functions themselves are emitted.
  if (!MultiVersionFuncs.empty())
    emitMultiVersionFunctions();
}

void ModuleEmitter::emitCPUDispatchDefinition(GlobalDecl GD) {
  const auto *FD = cast<FunctionDecl>(GD.getDecl());
  assert(FD && "Not a FunctionDecl?");
  assert(FD->isCPUDispatchMultiVersion() && "Not a multiversion function?");
  const auto *DD = FD->getAttr<CPUDispatchAttr>();
  assert(DD && "Not a cpu_dispatch Function?");

  const ABIFunctionInfo &FI = getTypes().arrangeGlobalDeclaration(GD);
  llvm::FunctionType *DeclTy = getTypes().GetFunctionType(FI);

  llvm::StringRef ResolverName = getMangledName(GD);
  updateMultiVersionNames(GD, FD, ResolverName);

  llvm::Type *ResolverType;
  GlobalDecl ResolverGD;
  if (getTarget().supportsIFunc()) {
    ResolverType = llvm::FunctionType::get(
        llvm::PointerType::get(DeclTy,
                               getTypes().getTargetAddressSpace(FD->getType())),
        false);
  } else {
    ResolverType = DeclTy;
    ResolverGD = GD;
  }

  auto *ResolverFunc = cast<llvm::Function>(
      obtainLLVMFunction(ResolverName, ResolverType, ResolverGD));
  ResolverFunc->setLinkage(getMultiversionLinkage(*this, GD));
  if (supportsCOMDAT())
    ResolverFunc->setComdat(
        getModule().getOrInsertComdat(ResolverFunc->getName()));

  llvm::SmallVector<FunctionEmitter::MultiVersionResolverOption, 10> Options;
  const TargetInfo &Target = getTarget();
  unsigned Index = 0;
  for (const IdentifierInfo *II : DD->cpus()) {
    std::string MangledName = getMangledNameImpl(*this, GD, FD, true) +
                              getCPUSpecificMangling(*this, II->getName());

    llvm::Constant *Func = getGlobalValue(MangledName);

    if (!Func) {
      GlobalDecl ExistingDecl = Manglings.lookup(MangledName);
      if (ExistingDecl.getDecl() &&
          ExistingDecl.getDecl()->getAsFunction()->isDefined()) {
        genGlobalFunctionDefinition(ExistingDecl, nullptr);
        Func = getGlobalValue(MangledName);
      } else {
        if (!ExistingDecl.getDecl())
          ExistingDecl = GD.getWithMultiVersionIndex(Index);

        Func = obtainLLVMFunction(MangledName, DeclTy, ExistingDecl,
                                  /*DontDefer=*/true, llvm::AttributeList(),
                                  ForDefinition);
      }
    }

    llvm::SmallVector<llvm::StringRef, 32> Features;
    Target.getCPUSpecificCPUDispatchFeatures(II->getName(), Features);
    llvm::transform(Features, Features.begin(),
                    [](llvm::StringRef Str) { return Str.substr(1); });
    llvm::erase_if(Features, [&Target](llvm::StringRef Feat) {
      return !Target.validateCpuSupports(Feat);
    });
    Options.emplace_back(cast<llvm::Function>(Func), llvm::StringRef{},
                         Features);
    ++Index;
  }

  llvm::stable_sort(
      Options, [](const FunctionEmitter::MultiVersionResolverOption &LHS,
                  const FunctionEmitter::MultiVersionResolverOption &RHS) {
        return llvm::X86::getCpuSupportsMask(LHS.Conditions.Features) >
               llvm::X86::getCpuSupportsMask(RHS.Conditions.Features);
      });

  // If the list contains multiple 'default' versions, such as when it contains
  // 'x86-64' and 'generic', don't emit the call to the generic one. We do this
  // by deleting the 'least advanced' (read, lowest mangling letter).
  while (Options.size() > 1 &&
         llvm::all_of(llvm::X86::getCpuSupportsMask(
                          (Options.end() - 2)->Conditions.Features),
                      [](auto X) { return X == 0; })) {
    llvm::StringRef LHSName = (Options.end() - 2)->Function->getName();
    llvm::StringRef RHSName = (Options.end() - 1)->Function->getName();
    if (LHSName.compare(RHSName) < 0)
      Options.erase(Options.end() - 2);
    else
      Options.erase(Options.end() - 1);
  }

  FunctionEmitter FE(*this);
  FE.genMultiVersionResolver(ResolverFunc, Options);

  if (getTarget().supportsIFunc()) {
    llvm::GlobalValue::LinkageTypes Linkage = getMultiversionLinkage(*this, GD);
    auto *IFunc = cast<llvm::GlobalValue>(getOrCreateMultiVersionResolver(GD));

    // Fix up function declarations that were created for cpu_specific before
    // cpu_dispatch was known
    if (!isa<llvm::GlobalIFunc>(IFunc)) {
      assert(cast<llvm::Function>(IFunc)->isDeclaration());
      auto *GI = llvm::GlobalIFunc::create(DeclTy, 0, Linkage, "", ResolverFunc,
                                           &getModule());
      GI->takeName(IFunc);
      IFunc->replaceAllUsesWith(GI);
      IFunc->eraseFromParent();
      IFunc = GI;
    }

    std::string AliasName =
        getMangledNameImpl(*this, GD, FD, /*OmitMultiVersionMangling=*/true);
    llvm::Constant *AliasFunc = getGlobalValue(AliasName);
    if (!AliasFunc) {
      auto *GA = llvm::GlobalAlias::create(DeclTy, 0, Linkage, AliasName, IFunc,
                                           &getModule());
      setCommonAttributes(GD, GA);
    }
  }
}

llvm::Constant *ModuleEmitter::getOrCreateMultiVersionResolver(GlobalDecl GD) {
  const auto *FD = cast<FunctionDecl>(GD.getDecl());
  assert(FD && "Not a FunctionDecl?");

  std::string MangledName =
      getMangledNameImpl(*this, GD, FD, /*OmitMultiVersionMangling=*/true);

  // Holds the name of the resolver, in ifunc mode this is the ifunc (which has
  // a separate resolver).
  std::string ResolverName = MangledName;
  if (getTarget().supportsIFunc()) {
    // In Aarch64, default versions of multiversioned functions are mangled to
    // their 'normal' assembly name. This deviates from other targets which
    // append a '.default' string. As a result we need to continue appending
    // .ifunc in Aarch64.
    if (!FD->isTargetClonesMultiVersion() ||
        getTarget().getTriple().isAArch64())
      ResolverName += ".ifunc";
  } else if (FD->isTargetMultiVersion()) {
    ResolverName += ".resolver";
  }

  // If the resolver has already been created, just return it.
  if (llvm::GlobalValue *ResolverGV = getGlobalValue(ResolverName))
    return ResolverGV;

  const ABIFunctionInfo &FI = getTypes().arrangeGlobalDeclaration(GD);
  llvm::FunctionType *DeclTy = getTypes().GetFunctionType(FI);

  // The resolver needs to be created. For target and target_clones, defer
  // creation until the end of the TU.
  if (FD->isTargetMultiVersion() || FD->isTargetClonesMultiVersion())
    MultiVersionFuncs.push_back(GD);

  // For cpu_specific, don't create an ifunc yet because we don't know if the
  // cpu_dispatch will be emitted in this translation unit.
  if (getTarget().supportsIFunc() && !FD->isCPUSpecificMultiVersion()) {
    llvm::Type *ResolverType = llvm::FunctionType::get(
        llvm::PointerType::get(DeclTy,
                               getTypes().getTargetAddressSpace(FD->getType())),
        false);
    llvm::Constant *Resolver = obtainLLVMFunction(MangledName + ".resolver",
                                                  ResolverType, GlobalDecl{});
    llvm::GlobalIFunc *GIF =
        llvm::GlobalIFunc::create(DeclTy, 0, getMultiversionLinkage(*this, GD),
                                  "", Resolver, &getModule());
    GIF->setName(ResolverName);
    setCommonAttributes(FD, GIF);

    return GIF;
  }

  llvm::Constant *Resolver =
      obtainLLVMFunction(ResolverName, DeclTy, GlobalDecl{});
  assert(isa<llvm::GlobalValue>(Resolver) &&
         "Resolver should be created for the first time");
  setCommonAttributes(FD, cast<llvm::GlobalValue>(Resolver));
  return Resolver;
}

// ===----------------------------------------------------------------------===
// Symbol resolution & function/global creation
// ===----------------------------------------------------------------------===

__attribute__((hot)) llvm::Constant *ModuleEmitter::obtainLLVMFunction(
    llvm::StringRef MangledName, llvm::Type *Ty, GlobalDecl GD, bool DontDefer,
    llvm::AttributeList ExtraAttrs, ForDefinition_t IsForDefinition) {
  const Decl *D = GD.getDecl();

  if (const FunctionDecl *FD = cast_or_null<FunctionDecl>(D)) {
    if (FD->isMultiVersion()) {
      updateMultiVersionNames(GD, FD, MangledName);
      if (!IsForDefinition)
        return getOrCreateMultiVersionResolver(GD);
    }
  }

  llvm::GlobalValue *Entry = getGlobalValue(MangledName);
  if (Entry) {
    if (WeakRefReferences.erase(Entry)) {
      const FunctionDecl *FD = cast_or_null<FunctionDecl>(D);
      if (FD && !FD->hasAttr<WeakAttr>())
        Entry->setLinkage(llvm::Function::ExternalLinkage);
    }

    if (D && !D->hasAttr<DLLImportAttr>() && !D->hasAttr<DLLExportAttr>() &&
        !shouldMapVisibilityToDLLExport(cast_or_null<NamedDecl>(D))) {
      Entry->setDLLStorageClass(llvm::GlobalValue::DefaultStorageClass);
      setDSOLocal(Entry);
    }
    if (IsForDefinition && !Entry->isDeclaration()) {
      GlobalDecl OtherGD;
      // Only diagnose conflicting definitions once per GD.
      if (lookupRepresentativeDecl(MangledName, OtherGD) &&
          (GD.getCanonicalDecl().getDecl() !=
           OtherGD.getCanonicalDecl().getDecl()) &&
          DiagnosedConflictingDefinitions.insert(GD).second) {
        getDiags().Report(D->getLocation(), diag::err_duplicate_mangled_name)
            << MangledName;
        getDiags().Report(OtherGD.getDecl()->getLocation(),
                          diag::note_previous_definition);
      }
    }

    if ((isa<llvm::Function>(Entry) || isa<llvm::GlobalAlias>(Entry)) &&
        (Entry->getValueType() == Ty)) {
      return Entry;
    }

    if (!IsForDefinition)
      return Entry;
  }

  // Incomplete return type (e.g. forward-declared struct): use a void()
  // stub and skip attribute setup.
  bool IsIncompleteFunction = false;

  llvm::FunctionType *FTy;
  if (isa<llvm::FunctionType>(Ty)) {
    FTy = cast<llvm::FunctionType>(Ty);
  } else {
    FTy = llvm::FunctionType::get(VoidTy, false);
    IsIncompleteFunction = true;
  }

  llvm::Function *F = llvm::Function::Create(
      FTy, llvm::Function::ExternalLinkage,
      Entry ? llvm::StringRef() : MangledName, &getModule());

  if (D && D->hasAttr<AnnotateAttr>())
    DeferredAnnotations[MangledName] = cast<ValueDecl>(D);

  // If we already created a function with the same mangled name (but different
  // type) before, take its name and add it to the list of functions to be
  // replaced with F at the end of CodeGen.
  //
  // This happens if there is a prototype for a function (e.g. "int f()") and
  // then a definition of a different type (e.g. "int f(int x)").
  if (Entry) {
    F->takeName(Entry);

    // This might be an implementation of a function without a prototype, in
    // which case, try to do special replacement of calls which match the new
    // prototype.  The really key thing here is that we also potentially drop
    // arguments from the call site so as to make a direct call, which makes the
    // inliner happier and suppresses a number of optimizer warnings (!) about
    // dropping arguments.
    if (!Entry->use_empty()) {
      replaceUsesOfNonProtoTypeWithRealFunction(Entry, F);
      Entry->removeDeadConstantUsers();
    }

    addGlobalValReplacement(Entry, F);
  }

  assert(F->getName() == MangledName && "name was uniqued!");
  if (D)
    setFunctionAttributes(GD, F, IsIncompleteFunction);
  if (ExtraAttrs.hasFnAttrs()) {
    llvm::AttrBuilder B(F->getContext(), ExtraAttrs.getFnAttrs());
    F->addFnAttrs(B);
  }

  if (!DontDefer) {
    // This is the first use or definition of a mangled name.  If there is a
    // deferred decl with this name, remember that we need to emit it at the end
    // of the file.
    auto DDI = DeferredDecls.find(MangledName);
    if (DDI != DeferredDecls.end()) {
      // Move the potentially referenced deferred decl to the
      // DeferredDeclsToEmit list, and remove it from DeferredDecls (since we
      // don't need it anymore).
      addDeferredDeclToEmit(DDI->second);
      DeferredDecls.erase(DDI);
    }
  }

  // Make sure the result is of the requested type.
  if (!IsIncompleteFunction) {
    assert(F->getFunctionType() == Ty);
    return F;
  }

  return F;
}

llvm::Constant *ModuleEmitter::addrOfFunction(GlobalDecl GD, llvm::Type *Ty,
                                              bool DontDefer,
                                              ForDefinition_t IsForDefinition) {
  if (!Ty) {
    const auto *FD = cast<FunctionDecl>(GD.getDecl());
    Ty = getTypes().convertType(FD->getType());
  }

  llvm::StringRef MangledName = getMangledName(GD);
  auto *F = obtainLLVMFunction(MangledName, Ty, GD, DontDefer,
                               llvm::AttributeList(), IsForDefinition);
  return F;
}

llvm::Constant *ModuleEmitter::getFunctionStart(const ValueDecl *Decl) {
  llvm::GlobalValue *F =
      cast<llvm::GlobalValue>(addrOfFunction(Decl)->stripPointerCasts());

  return llvm::NoCFIValue::get(F);
}

// ===----------------------------------------------------------------------===
// Symbol resolution
// ===----------------------------------------------------------------------===

namespace {
const FunctionDecl *getRuntimeFunctionDecl(TreeContext &C,
                                           llvm::StringRef Name) {
  TranslationUnitDecl *TUDecl = C.getTranslationUnitDecl();
  DeclContext *DC = TranslationUnitDecl::castToDeclContext(TUDecl);

  IdentifierInfo &CII = C.Idents.get(Name);
  for (const auto *Result : DC->lookup(&CII))
    if (const auto *FD = dyn_cast<FunctionDecl>(Result))
      return FD;

  return nullptr;
}
} // namespace

llvm::FunctionCallee ModuleEmitter::createRuntimeFunction(
    llvm::FunctionType *FTy, llvm::StringRef Name,
    llvm::AttributeList ExtraAttrs, bool Local, bool AssumeConvergent) {
  if (AssumeConvergent) {
    ExtraAttrs =
        ExtraAttrs.addFnAttribute(VMContext, llvm::Attribute::Convergent);
  }

  llvm::Constant *C = obtainLLVMFunction(Name, FTy, GlobalDecl(),
                                         /*DontDefer=*/false, ExtraAttrs);

  if (auto *F = dyn_cast<llvm::Function>(C)) {
    if (F->empty()) {
      F->setCallingConv(getRuntimeCC());

      setDSOLocal(F);
    }
  }

  return {FTy, C};
}

llvm::Constant *
ModuleEmitter::obtainLLVMGlobal(llvm::StringRef MangledName, llvm::Type *Ty,
                                LangAS AddrSpace, const VarDecl *D,
                                ForDefinition_t IsForDefinition) {
  llvm::GlobalValue *Entry = getGlobalValue(MangledName);
  unsigned TargetAS = getContext().getTargetAddressSpace(AddrSpace);
  if (Entry) {
    if (WeakRefReferences.erase(Entry)) {
      if (D && !D->hasAttr<WeakAttr>())
        Entry->setLinkage(llvm::Function::ExternalLinkage);
    }

    if (D && !D->hasAttr<DLLImportAttr>() && !D->hasAttr<DLLExportAttr>() &&
        !shouldMapVisibilityToDLLExport(D))
      Entry->setDLLStorageClass(llvm::GlobalValue::DefaultStorageClass);

    if (Entry->getValueType() == Ty && Entry->getAddressSpace() == TargetAS)
      return Entry;

    if (IsForDefinition && !Entry->isDeclaration()) {
      GlobalDecl OtherGD;
      const VarDecl *OtherD;

      if (D && lookupRepresentativeDecl(MangledName, OtherGD) &&
          (D->getCanonicalDecl() != OtherGD.getCanonicalDecl().getDecl()) &&
          (OtherD = dyn_cast<VarDecl>(OtherGD.getDecl())) &&
          OtherD->hasInit() &&
          DiagnosedConflictingDefinitions.insert(D).second) {
        getDiags().Report(D->getLocation(), diag::err_duplicate_mangled_name)
            << MangledName;
        getDiags().Report(OtherGD.getDecl()->getLocation(),
                          diag::note_previous_definition);
      }
    }

    // Make sure the result is of the correct type.
    if (Entry->getType()->getAddressSpace() != TargetAS)
      return llvm::ConstantExpr::getAddrSpaceCast(
          Entry, llvm::PointerType::get(Ty->getContext(), TargetAS));

    // (If global is requested for a definition, we always need to create a new
    // global, not just return a bitcast.)
    if (!IsForDefinition)
      return Entry;
  }

  auto DAddrSpace = getGlobalVarAddressSpace(D);

  auto *GV = new llvm::GlobalVariable(
      getModule(), Ty, false, llvm::GlobalValue::ExternalLinkage, nullptr,
      MangledName, nullptr, llvm::GlobalVariable::NotThreadLocal,
      getContext().getTargetAddressSpace(DAddrSpace));

  // If we already created a global with the same mangled name (but different
  // type) before, take its name and remove it from its parent.
  if (Entry) {
    GV->takeName(Entry);

    if (!Entry->use_empty()) {
      Entry->replaceAllUsesWith(GV);
    }

    Entry->eraseFromParent();
  }

  // This is the first use or definition of a mangled name.  If there is a
  // deferred decl with this name, remember that we need to emit it at the end
  // of the file.
  auto DDI = DeferredDecls.find(MangledName);
  if (DDI != DeferredDecls.end()) {
    // Move the potentially referenced deferred decl to the DeferredDeclsToEmit
    // list, and remove it from DeferredDecls (since we don't need it anymore).
    addDeferredDeclToEmit(DDI->second);
    DeferredDecls.erase(DDI);
  }

  // Attributes that apply even to external declarations.
  if (D) {

    GV->setConstant(D->getType().isConstantStorage(getContext(), false, false));

    GV->setAlignment(getContext().getDeclAlign(D).getAsAlign());

    setLinkageForGV(GV, D);

    if (D->getTLSKind())
      setTLSMode(GV, *D);

    setGVProperties(GV, D);

    if (D->hasExternalStorage()) {
      if (const SectionAttr *SA = D->getAttr<SectionAttr>())
        GV->setSection(SA->getName());
    }
  }

  if (D &&
      D->isThisDeclarationADefinition(Context) == VarDecl::DeclarationOnly) {
    getTargetCodeGenInfo().setTargetAttributes(D, GV, *this);
  }

  LangAS ExpectedAS = D ? D->getType().getAddressSpace() : LangAS::Default;
  assert(getContext().getTargetAddressSpace(ExpectedAS) == TargetAS);
  if (DAddrSpace != ExpectedAS) {
    return getTargetCodeGenInfo().performAddrSpaceCast(
        *this, GV, DAddrSpace, ExpectedAS,
        llvm::PointerType::get(getLLVMContext(), TargetAS));
  }

  return GV;
}

llvm::Constant *ModuleEmitter::addrOfGlobal(GlobalDecl GD,
                                            ForDefinition_t IsForDefinition) {
  const Decl *D = GD.getDecl();

  if (isa<FunctionDecl>(D)) {
    const ABIFunctionInfo &FI = getTypes().arrangeGlobalDeclaration(GD);
    llvm::FunctionType *Ty = getTypes().GetFunctionType(FI);
    return addrOfFunction(GD, Ty, /*DontDefer=*/false, IsForDefinition);
  }

  return getGlobalVarAddr(cast<VarDecl>(D), /*Ty=*/nullptr, IsForDefinition);
}

llvm::Constant *
ModuleEmitter::getGlobalVarAddr(const VarDecl *D, llvm::Type *Ty,
                                ForDefinition_t IsForDefinition) {
  assert(D->hasGlobalStorage() && "Not a global variable");
  QualType ASTTy = D->getType();
  if (!Ty)
    Ty = getTypes().convertTypeForMem(ASTTy);

  llvm::StringRef MangledName = getMangledName(D);
  return obtainLLVMGlobal(MangledName, Ty, ASTTy.getAddressSpace(), D,
                          IsForDefinition);
}

llvm::Constant *ModuleEmitter::createRuntimeVariable(llvm::Type *Ty,
                                                     llvm::StringRef Name) {
  LangAS AddrSpace = LangAS::Default;
  auto *Ret = obtainLLVMGlobal(Name, Ty, AddrSpace, nullptr);
  setDSOLocal(cast<llvm::GlobalValue>(Ret->stripPointerCasts()));
  return Ret;
}

void ModuleEmitter::genTentativeDefinition(const VarDecl *D) {
  assert(!D->getInit() && "Cannot emit definite definitions here!");

  llvm::StringRef MangledName = getMangledName(D);
  llvm::GlobalValue *GV = getGlobalValue(MangledName);

  // We already have a definition, not declaration, with the same mangled name.
  // Emitting of declaration is not required (and actually overwrites emitted
  // definition).
  if (GV && !GV->isDeclaration())
    return;

  // If we have not seen a reference to this variable yet, place it into the
  // deferred declarations table to be emitted if needed later.
  if (!mustBeEmitted(D) && !GV) {
    DeferredDecls[MangledName] = D;
    return;
  }

  // The tentative definition is the only definition.
  genGlobalVarDefinition(D);
}

void ModuleEmitter::genExternalDeclaration(const VarDecl *D) {
  genExternalVarDeclaration(D);
}

CharUnits ModuleEmitter::getTargetTypeStoreSize(llvm::Type *Ty) const {
  return Context.toCharUnitsFromBits(
      getDataLayout().getTypeStoreSizeInBits(Ty));
}

LangAS ModuleEmitter::getGlobalVarAddressSpace(const VarDecl *D) {

  return getTargetCodeGenInfo().getGlobalVarAddressSpace(*this, D);
}

LangAS ModuleEmitter::getGlobalConstantAddressSpace() const {
  if (auto AS = getTarget().getConstantAddressSpace())
    return *AS;
  return LangAS::Default;
}

namespace {

llvm::Constant *
castStringLiteralToDefaultAddressSpace(ModuleEmitter &ME,
                                       llvm::GlobalVariable *GV) {
  llvm::Constant *Cast = GV;
  auto AS = ME.getGlobalConstantAddressSpace();
  if (AS != LangAS::Default)
    Cast = ME.getTargetCodeGenInfo().performAddrSpaceCast(
        ME, GV, AS, LangAS::Default,
        llvm::PointerType::get(
            ME.getLLVMContext(),
            ME.getContext().getTargetAddressSpace(LangAS::Default)));
  return Cast;
}

bool shouldBeInCOMDAT(ModuleEmitter &ME, const Decl &D) {
  if (!ME.supportsCOMDAT())
    return false;

  if (D.hasAttr<SelectAnyAttr>())
    return true;

  GVALinkage Linkage;
  if (auto *VD = dyn_cast<VarDecl>(&D))
    Linkage = ME.getContext().GetGVALinkageForVariable(VD);
  else
    Linkage = ME.getContext().GetGVALinkageForFunction(cast<FunctionDecl>(&D));

  switch (Linkage) {
  case GVA_Internal:
  case GVA_AvailableExternally:
  case GVA_StrongExternal:
    return false;
  case GVA_DiscardableODR:
  case GVA_StrongODR:
    return true;
  }
  llvm_unreachable("No such linkage");
}

} // namespace

// ===----------------------------------------------------------------------===
// Definitions, constants & string literals
// ===----------------------------------------------------------------------===

// ===----------------------------------------------------------------------===
// Global variable definitions
// ===----------------------------------------------------------------------===

bool ModuleEmitter::supportsCOMDAT() const {
  return getTriple().supportsCOMDAT();
}

void ModuleEmitter::maybeSetTrivialComdat(const Decl &D,
                                          llvm::GlobalObject &GO) {
  if (!shouldBeInCOMDAT(*this, D))
    return;
  GO.setComdat(TheModule.getOrInsertComdat(GO.getName()));
}

void ModuleEmitter::genGlobalVarDefinition(const VarDecl *D, bool IsTentative) {
  QualType ASTTy = D->getType();
  llvm::TrackingVH<llvm::Constant> Init;
  bool NeedsGlobalCtor = false;
  bool NeedsGlobalDtor = false;

  const VarDecl *InitDecl;
  const Expr *InitExpr = D->getAnyInitializer(InitDecl);

  std::optional<ConstantEmitter> emitter;

  if (D->hasAttr<LoaderUninitializedAttr>())
    Init = llvm::UndefValue::get(getTypes().convertTypeForMem(ASTTy));
  else if (!InitExpr) {
    // This is a tentative definition; tentative definitions are
    // implicitly initialized with { 0 }.
    //
    // Note that tentative definitions are only emitted at the end of
    // a translation unit, so they should never have incomplete
    // type. In addition, genTentativeDefinition makes sure that we
    // never attempt to emit a tentative definition if a real one
    // exists. A use may still exists, however, so we still may need
    // to do a RAUW.
    assert(!ASTTy->isIncompleteType() && "Unexpected incomplete type");
    Init = genNullConstant(D->getType());
  } else {
    initializedGlobalDecl = GlobalDecl(D);
    emitter.emplace(*this);
    llvm::Constant *Initializer = emitter->tryEmitForInitializer(*InitDecl);
    if (!Initializer) {
      QualType T = InitExpr->getType();
      {
        errorUnsupported(D, "static initializer");
        Init = llvm::UndefValue::get(getTypes().convertType(T));
      }
    } else {
      Init = Initializer;

#ifndef NDEBUG
      CharUnits VarSize = getContext().getTypeSizeInChars(ASTTy) +
                          InitDecl->getFlexibleArrayInitChars(getContext());
      CharUnits CstSize = CharUnits::fromQuantity(
          getDataLayout().getTypeAllocSize(Init->getType()));
      assert(VarSize == CstSize && "Emitted constant has unexpected size");
#endif
    }
  }

  llvm::Type *InitType = Init->getType();
  llvm::Constant *Entry =
      getGlobalVarAddr(D, InitType, ForDefinition_t(!IsTentative));

  Entry = Entry->stripPointerCasts();
  auto *GV = dyn_cast<llvm::GlobalVariable>(Entry);

  // Type mismatch: a prior declaration/tentative had a different type
  // (e.g. "extern int x[]" then "int x[10]", or union initializers).
  if (!GV || GV->getValueType() != InitType ||
      GV->getType()->getAddressSpace() !=
          getContext().getTargetAddressSpace(getGlobalVarAddressSpace(D))) {

    Entry->setName(llvm::StringRef());
    GV = cast<llvm::GlobalVariable>(
        getGlobalVarAddr(D, InitType, ForDefinition_t(!IsTentative))
            ->stripPointerCasts());

    llvm::Constant *NewPtrForOldDecl =
        llvm::ConstantExpr::getPointerBitCastOrAddrSpaceCast(GV,
                                                             Entry->getType());
    Entry->replaceAllUsesWith(NewPtrForOldDecl);

    cast<llvm::GlobalValue>(Entry)->eraseFromParent();
  }

  if (D->hasAttr<AnnotateAttr>())
    addGlobalAnnotations(D, GV);

  llvm::GlobalValue::LinkageTypes Linkage = getLLVMLinkageVarDefinition(D);

  GV->setInitializer(Init);
  if (emitter)
    emitter->finalize(GV);

  GV->setConstant(!NeedsGlobalCtor && !NeedsGlobalDtor &&
                  D->getType().isConstantStorage(getContext(), true, true));

  if (const SectionAttr *SA = D->getAttr<SectionAttr>()) {
    const TreeContext::SectionInfo &SI = Context.SectionInfos[SA->getName()];
    if ((SI.SectionFlags & TreeContext::PSF_Write) == 0)
      GV->setConstant(true);
  }

  CharUnits AlignVal = getContext().getDeclAlign(D);
  GV->setAlignment(AlignVal.getAsAlign());

  // Darwin TLS: the thread-wrapper helper lives next to the definition; other
  // targets also emit it at call sites. Here the backing symbol can be internal
  // if only the wrapper is referenced—except when the variable may be used
  // without the wrapper, or when linkage is not plain external (weak /
  // linkonce), where we preserve the existing linkage.
  if (D->getTLSKind() == VarDecl::TLS_Dynamic &&
      Linkage == llvm::GlobalValue::ExternalLinkage &&
      Context.getTargetInfo().getTriple().isOSDarwin())
    Linkage = llvm::GlobalValue::InternalLinkage;

  GV->setLinkage(Linkage);
  if (D->hasAttr<DLLImportAttr>())
    GV->setDLLStorageClass(llvm::GlobalVariable::DLLImportStorageClass);
  else if (D->hasAttr<DLLExportAttr>())
    GV->setDLLStorageClass(llvm::GlobalVariable::DLLExportStorageClass);
  else
    GV->setDLLStorageClass(llvm::GlobalVariable::DefaultStorageClass);

  if (Linkage == llvm::GlobalVariable::CommonLinkage) {
    GV->setConstant(false);
    // Common linkage requires zero initializer; promote to weak if non-zero.
    if (!GV->getInitializer()->isNullValue())
      GV->setLinkage(llvm::GlobalVariable::WeakAnyLinkage);
  }

  setNonAliasAttributes(D, GV);

  if (D->getTLSKind() && !GV->isThreadLocal())
    setTLSMode(GV, *D);

  maybeSetTrivialComdat(*D, *GV);

  if (DebugEmitter *DI = getModuleDebugInfo())
    if (getCodeGenOpts().hasReducedDebugInfo())
      DI->genGlobalVariable(GV, D);
}

void ModuleEmitter::genExternalVarDeclaration(const VarDecl *D) {
  if (DebugEmitter *DI = getModuleDebugInfo())
    if (getCodeGenOpts().hasReducedDebugInfo()) {
      QualType ASTTy = D->getType();
      llvm::Type *Ty = getTypes().convertTypeForMem(D->getType());
      llvm::Constant *GV =
          obtainLLVMGlobal(D->getName(), Ty, ASTTy.getAddressSpace(), D);
      DI->genExternalVariable(
          cast<llvm::GlobalVariable>(GV->stripPointerCasts()), D);
    }
}

namespace {
bool isVarDeclStrongDefinition(const TreeContext &Context, ModuleEmitter &ME,
                               const VarDecl *D, bool NoCommon) {
  // Don't give variables common linkage if -fno-common was specified unless it
  // was overridden by a NoCommon attribute.
  if ((NoCommon || D->hasAttr<NoCommonAttr>()) && !D->hasAttr<CommonAttr>())
    return true;

  // Tentative definitions: file-scope without initializer and without
  // explicit storage class (or with 'static').
  if (D->getInit() || D->hasExternalStorage())
    return true;

  if (D->hasAttr<SectionAttr>())
    return true;

  // Pragma section attrs also imply a specific section, incompatible with
  // common linkage.
  if (D->hasAttr<PragmaNeverCBSSSectionAttr>() ||
      D->hasAttr<PragmaNeverCDataSectionAttr>() ||
      D->hasAttr<PragmaNeverCRelroSectionAttr>() ||
      D->hasAttr<PragmaNeverCRodataSectionAttr>())
    return true;

  // Thread local vars aren't considered common linkage.
  if (D->getTLSKind())
    return true;

  // Tentative definitions marked with WeakImportAttr are true definitions.
  if (D->hasAttr<WeakImportAttr>())
    return true;

  // A variable cannot be both common and exist in a comdat.
  if (shouldBeInCOMDAT(ME, *D))
    return true;

  // Microsoft's link.exe doesn't support alignments greater than 32 bytes for
  // common symbols, so symbols with greater alignment requirements cannot be
  // common.
  // Other COFF linkers (ld.bfd and LLD) support arbitrary power-of-two
  // alignments for common symbols via the aligncomm directive, so this
  // restriction only applies to MSVC environments.
  if (Context.getTargetInfo().getTriple().isKnownWindowsMSVCEnvironment() &&
      Context.getTypeAlignIfKnown(D->getType()) >
          Context.toBits(CharUnits::fromQuantity(32)))
    return true;

  return false;
}
} // namespace

llvm::GlobalValue::LinkageTypes
ModuleEmitter::getLLVMLinkageForDeclarator(const DeclaratorDecl *D,
                                           GVALinkage Linkage) {
  if (Linkage == GVA_Internal)
    return llvm::Function::InternalLinkage;

  if (D->hasAttr<WeakAttr>())
    return llvm::GlobalVariable::WeakAnyLinkage;

  if (const auto *FD = D->getAsFunction())
    if (FD->isMultiVersion() && Linkage == GVA_AvailableExternally)
      return llvm::GlobalVariable::LinkOnceAnyLinkage;

  // We are guaranteed to have a strong definition somewhere else,
  // so we can use available_externally linkage.
  if (Linkage == GVA_AvailableExternally)
    return llvm::GlobalValue::AvailableExternallyLinkage;

  // Note that Apple's kernel linker doesn't support symbol
  // coalescing, so we need to avoid linkonce and weak linkages there.
  // Normally, this means we just map to internal, with special cases mapped
  // to external.

  // Discardable ODR: merge duplicate definitions across TUs; elide if this TU
  // ends up not needing a copy.
  if (Linkage == GVA_DiscardableODR)
    return llvm::Function::LinkOnceODRLinkage;

  // Strong ODR: required definition even if unreferenced in this TU.
  if (Linkage == GVA_StrongODR)
    return llvm::Function::WeakODRLinkage;

  if (isa<VarDecl>(D) &&
      !isVarDeclStrongDefinition(Context, *this, cast<VarDecl>(D),
                                 CodeGenOpts.NoCommon))
    return llvm::GlobalVariable::CommonLinkage;

  // selectany symbols are externally visible, so use weak instead of
  // linkonce.  MSVC optimizes away references to const selectany globals, so
  // all definitions should be the same and ODR linkage should be used.
  // http://msdn.microsoft.com/en-us/library/5tkz6s71.aspx
  if (D->hasAttr<SelectAnyAttr>())
    return llvm::GlobalVariable::WeakODRLinkage;

  // Otherwise, we have strong external linkage.
  assert(Linkage == GVA_StrongExternal);
  return llvm::GlobalVariable::ExternalLinkage;
}

llvm::GlobalValue::LinkageTypes
ModuleEmitter::getLLVMLinkageVarDefinition(const VarDecl *VD) {
  GVALinkage Linkage = getContext().GetGVALinkageForVariable(VD);
  return getLLVMLinkageForDeclarator(VD, Linkage);
}

// ===----------------------------------------------------------------------===
// Function & alias definitions
// ===----------------------------------------------------------------------===

namespace {
void replaceUsesOfNonProtoConstant(llvm::Constant *old, llvm::Function *newFn) {
  if (old->use_empty())
    return;

  llvm::Type *newRetTy = newFn->getReturnType();
  llvm::SmallVector<llvm::Value *, 4> newArgs;

  for (llvm::Value::use_iterator ui = old->use_begin(), ue = old->use_end();
       ui != ue;) {
    llvm::Value::use_iterator use = ui++; // Increment before the use is erased.
    llvm::User *user = use->getUser();

    if (auto *bitcast = dyn_cast<llvm::ConstantExpr>(user)) {
      if (bitcast->getOpcode() == llvm::Instruction::BitCast)
        replaceUsesOfNonProtoConstant(bitcast, newFn);
      continue;
    }

    llvm::CallBase *callSite = dyn_cast<llvm::CallBase>(user);
    if (!callSite)
      continue;
    if (!callSite->isCallee(&*use))
      continue;

    if (callSite->getType() != newRetTy && !callSite->use_empty())
      continue;

    llvm::SmallVector<llvm::AttributeSet, 8> newArgAttrs;
    llvm::AttributeList oldAttrs = callSite->getAttributes();

    unsigned newNumArgs = newFn->arg_size();
    if (callSite->arg_size() < newNumArgs)
      continue;

    unsigned argNo = 0;
    bool dontTransform = false;
    for (llvm::Argument &A : newFn->args()) {
      if (callSite->getArgOperand(argNo)->getType() != A.getType()) {
        dontTransform = true;
        break;
      }

      newArgAttrs.push_back(oldAttrs.getParamAttrs(argNo));
      argNo++;
    }
    if (dontTransform)
      continue;

    newArgs.append(callSite->arg_begin(), callSite->arg_begin() + argNo);

    llvm::SmallVector<llvm::OperandBundleDef, 1> newBundles;
    callSite->getOperandBundlesAsDefs(newBundles);

    llvm::CallBase *newCall;
    if (isa<llvm::CallInst>(callSite)) {
      newCall =
          llvm::CallInst::Create(newFn, newArgs, newBundles, "", callSite);
    } else {
      auto *oldInvoke = cast<llvm::InvokeInst>(callSite);
      newCall = llvm::InvokeInst::Create(newFn, oldInvoke->getNormalDest(),
                                         oldInvoke->getUnwindDest(), newArgs,
                                         newBundles, "", callSite);
    }
    newArgs.clear(); // for the next iteration

    if (!newCall->getType()->isVoidTy())
      newCall->takeName(callSite);
    newCall->setAttributes(
        llvm::AttributeList::get(newFn->getContext(), oldAttrs.getFnAttrs(),
                                 oldAttrs.getRetAttrs(), newArgAttrs));
    newCall->setCallingConv(callSite->getCallingConv());

    if (!callSite->use_empty())
      callSite->replaceAllUsesWith(newCall);
    if (callSite->getDebugLoc())
      newCall->setDebugLoc(callSite->getDebugLoc());

    callSite->eraseFromParent();
  }
}

void replaceUsesOfNonProtoTypeWithRealFunction(llvm::GlobalValue *Old,
                                               llvm::Function *NewFn) {
  if (!isa<llvm::Function>(Old))
    return;

  replaceUsesOfNonProtoConstant(Old, NewFn);
}
} // namespace

__attribute__((hot)) void
ModuleEmitter::genGlobalFunctionDefinition(GlobalDecl GD,
                                           llvm::GlobalValue *GV) {
  const auto *D = cast<FunctionDecl>(GD.getDecl());

  const ABIFunctionInfo &FI = getTypes().arrangeGlobalDeclaration(GD);
  llvm::FunctionType *Ty = getTypes().GetFunctionType(FI);

  if (!GV || (GV->getValueType() != Ty))
    GV = cast<llvm::GlobalValue>(
        addrOfFunction(GD, Ty, /*DontDefer=*/true, ForDefinition));

  // Already emitted.
  if (!GV->isDeclaration())
    return;

  // We need to set linkage and visibility on the function before
  // generating code for it because various parts of IR generation
  // want to propagate this information down (e.g. to local static
  // declarations).
  auto *Fn = cast<llvm::Function>(GV);
  setFunctionLinkage(GD, Fn);

  setGVProperties(Fn, GD);

  maybeSetTrivialComdat(*D, *Fn);

  FunctionEmitter(*this).generateCode(GD, Fn, FI);

  setNonAliasAttributes(GD, Fn, /*SkipCPUFeatures=*/true);
  setLLVMFunctionAttributesForDefinition(D, Fn);

  if (const ConstructorAttr *CA = D->getAttr<ConstructorAttr>())
    addGlobalCtor(Fn, CA->getPriority());
  if (const DestructorAttr *DA = D->getAttr<DestructorAttr>())
    addGlobalDtor(Fn, DA->getPriority(), true);
}

void ModuleEmitter::genAliasDefinition(GlobalDecl GD) {
  const auto *D = cast<ValueDecl>(GD.getDecl());
  const AliasAttr *AA = D->getAttr<AliasAttr>();
  assert(AA && "Not an alias?");

  llvm::StringRef MangledName = getMangledName(GD);

  if (AA->getAliasee() == MangledName) {
    Diags.Report(AA->getLocation(), diag::err_cyclic_alias) << 0;
    return;
  }

  // If there is a definition in the module, then it wins over the alias.
  // This is dubious, but allow it to be safe.  Just ignore the alias.
  llvm::GlobalValue *Entry = getGlobalValue(MangledName);
  if (Entry && !Entry->isDeclaration())
    return;

  Aliases.push_back(GD);

  llvm::Type *DeclTy = getTypes().convertTypeForMem(D->getType());

  // Referencing the value forces emission of deferred decls.
  llvm::Constant *Aliasee;
  llvm::GlobalValue::LinkageTypes LT;
  if (isa<llvm::FunctionType>(DeclTy)) {
    Aliasee = obtainLLVMFunction(AA->getAliasee(), DeclTy, GD);
    LT = getFunctionLinkage(GD);
  } else {
    Aliasee = obtainLLVMGlobal(AA->getAliasee(), DeclTy, LangAS::Default,
                               /*D=*/nullptr);
    if (const auto *VD = dyn_cast<VarDecl>(GD.getDecl()))
      LT = getLLVMLinkageVarDefinition(VD);
    else
      LT = getFunctionLinkage(GD);
  }

  // Name is set below after the old global is replaced.
  unsigned AS = Aliasee->getType()->getPointerAddressSpace();
  auto *GA =
      llvm::GlobalAlias::create(DeclTy, AS, LT, "", Aliasee, &getModule());

  if (Entry) {
    if (GA->getAliasee() == Entry) {
      Diags.Report(AA->getLocation(), diag::err_cyclic_alias) << 0;
      return;
    }

    assert(Entry->isDeclaration());

    // If there is a declaration in the module, then we had an extern followed
    // by the alias, as in:
    //   extern int test6();
    //   ...
    //   int test6() __attribute__((alias("test7")));
    //
    GA->takeName(Entry);

    Entry->replaceAllUsesWith(GA);
    Entry->eraseFromParent();
  } else {
    GA->setName(MangledName);
  }

  // Alias-specific subset of the attributes that global variables/functions
  // can carry.
  if (D->hasAttr<WeakAttr>() || D->hasAttr<WeakRefAttr>() ||
      D->isWeakImported()) {
    GA->setLinkage(llvm::Function::WeakAnyLinkage);
  }

  if (const auto *VD = dyn_cast<VarDecl>(D))
    if (VD->getTLSKind())
      setTLSMode(GA, *VD);

  setCommonAttributes(GD, GA);

  if (isa<VarDecl>(D))
    if (DebugEmitter *DI = getModuleDebugInfo())
      DI->genGlobalAlias(
          cast<llvm::GlobalValue>(GA->getAliasee()->stripPointerCasts()), GD);
}

void ModuleEmitter::emitIFuncDefinition(GlobalDecl GD) {
  const auto *D = cast<ValueDecl>(GD.getDecl());
  const IFuncAttr *IFA = D->getAttr<IFuncAttr>();
  assert(IFA && "Not an ifunc?");

  llvm::StringRef MangledName = getMangledName(GD);

  if (IFA->getResolver() == MangledName) {
    Diags.Report(IFA->getLocation(), diag::err_cyclic_alias) << 1;
    return;
  }

  // Report an error if some definition overrides ifunc.
  llvm::GlobalValue *Entry = getGlobalValue(MangledName);
  if (Entry && !Entry->isDeclaration()) {
    GlobalDecl OtherGD;
    if (lookupRepresentativeDecl(MangledName, OtherGD) &&
        DiagnosedConflictingDefinitions.insert(GD).second) {
      Diags.Report(D->getLocation(), diag::err_duplicate_mangled_name)
          << MangledName;
      Diags.Report(OtherGD.getDecl()->getLocation(),
                   diag::note_previous_definition);
    }
    return;
  }

  Aliases.push_back(GD);

  llvm::Type *DeclTy = getTypes().convertTypeForMem(D->getType());
  llvm::Type *ResolverTy = llvm::GlobalIFunc::getResolverFunctionType(DeclTy);
  llvm::Constant *Resolver =
      obtainLLVMFunction(IFA->getResolver(), ResolverTy, {});
  llvm::GlobalIFunc *GIF = llvm::GlobalIFunc::create(
      DeclTy, 0, llvm::Function::ExternalLinkage, "", Resolver, &getModule());
  if (Entry) {
    if (GIF->getResolver() == Entry) {
      Diags.Report(IFA->getLocation(), diag::err_cyclic_alias) << 1;
      return;
    }
    assert(Entry->isDeclaration());

    // If there is a declaration in the module, then we had an extern followed
    // by the ifunc, as in:
    //   extern int test();
    //   ...
    //   int test() __attribute__((ifunc("resolver")));
    //
    GIF->takeName(Entry);

    Entry->replaceAllUsesWith(GIF);
    Entry->eraseFromParent();
  } else
    GIF->setName(MangledName);
  setCommonAttributes(GD, GIF);
}

llvm::Function *ModuleEmitter::getIntrinsic(unsigned IID,
                                            llvm::ArrayRef<llvm::Type *> Tys) {
  return llvm::Intrinsic::getDeclaration(&getModule(), (llvm::Intrinsic::ID)IID,
                                         Tys);
}

bool ModuleEmitter::getExpressionLocationsEnabled() const {
  return CodeGenOpts.DebugColumnInfo;
}

llvm::Constant *
ModuleEmitter::getConstantArrayFromStringLiteral(const StringLiteral *E) {
  assert(!E->getType()->isPointerType() && "Strings are always arrays");

  // Don't emit it as the address of the string, emit the string data itself
  // as an inline array.
  if (E->getCharByteWidth() == 1) {
    llvm::SmallString<64> Str(E->getString());

    const ConstantArrayType *CAT = Context.getAsConstantArrayType(E->getType());
    assert(CAT && "String literal not of constant array type!");
    Str.resize(CAT->getSize().getZExtValue());
    return llvm::ConstantDataArray::getString(VMContext, Str, false);
  }

  auto *AType = cast<llvm::ArrayType>(getTypes().convertType(E->getType()));
  llvm::Type *ElemTy = AType->getElementType();
  unsigned NumElements = AType->getNumElements();

  // Wide strings have either 2-byte or 4-byte elements.
  if (ElemTy->getPrimitiveSizeInBits() == 16) {
    llvm::SmallVector<uint16_t, 32> Elements;
    Elements.reserve(NumElements);

    for (unsigned i = 0, e = E->getLength(); i != e; ++i)
      Elements.push_back(E->getCodeUnit(i));
    Elements.resize(NumElements);
    return llvm::ConstantDataArray::get(VMContext, Elements);
  }

  assert(ElemTy->getPrimitiveSizeInBits() == 32);
  llvm::SmallVector<uint32_t, 32> Elements;
  Elements.reserve(NumElements);

  for (unsigned i = 0, e = E->getLength(); i != e; ++i)
    Elements.push_back(E->getCodeUnit(i));
  Elements.resize(NumElements);
  return llvm::ConstantDataArray::get(VMContext, Elements);
}

// ===----------------------------------------------------------------------===
// Constants & metadata
// ===----------------------------------------------------------------------===

namespace {
llvm::GlobalVariable *generateStringLiteral(llvm::Constant *C,
                                            llvm::GlobalValue::LinkageTypes LT,
                                            ModuleEmitter &ME,
                                            llvm::StringRef GlobalName,
                                            CharUnits Alignment) {
  unsigned AddrSpace =
      ME.getContext().getTargetAddressSpace(ME.getGlobalConstantAddressSpace());

  llvm::Module &M = ME.getModule();
  auto *GV = new llvm::GlobalVariable(
      M, C->getType(), !ME.getLangOpts().WritableStrings, LT, C, GlobalName,
      nullptr, llvm::GlobalVariable::NotThreadLocal, AddrSpace);
  GV->setAlignment(Alignment.getAsAlign());
  GV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
  if (GV->isWeakForLinker()) {
    assert(ME.supportsCOMDAT() && "Only COFF uses weak string literals");
    GV->setComdat(M.getOrInsertComdat(GV->getName()));
  }
  ME.setDSOLocal(GV);

  return GV;
}
} // namespace

ConstantAddress
ModuleEmitter::addrOfConstantStringFromLiteral(const StringLiteral *S,
                                               llvm::StringRef Name) {
  CharUnits Alignment = getContext().getAlignOfGlobalVarInChars(S->getType());

  llvm::Constant *C = getConstantArrayFromStringLiteral(S);
  llvm::GlobalVariable **Entry = nullptr;
  if (!LangOpts.WritableStrings) {
    Entry = &ConstantStringMap[C];
    if (auto GV = *Entry) {
      if (uint64_t(Alignment.getQuantity()) > GV->getAlignment())
        GV->setAlignment(Alignment.getAsAlign());
      return ConstantAddress(castStringLiteralToDefaultAddressSpace(*this, GV),
                             GV->getValueType(), Alignment);
    }
  }

  llvm::SmallString<256> MangledNameBuffer;
  llvm::StringRef GlobalVariableName;
  llvm::GlobalValue::LinkageTypes LT;

  {
    LT = llvm::GlobalValue::PrivateLinkage;
    GlobalVariableName = Name;
  }

  auto GV = generateStringLiteral(C, LT, *this, GlobalVariableName, Alignment);

  DebugEmitter *DI = getModuleDebugInfo();
  if (DI && getCodeGenOpts().hasReducedDebugInfo())
    DI->AddStringLiteralDebugInfo(GV, S);

  if (Entry)
    *Entry = GV;

  return ConstantAddress(castStringLiteralToDefaultAddressSpace(*this, GV),
                         GV->getValueType(), Alignment);
}

ConstantAddress ModuleEmitter::addrOfConstantCString(const std::string &Str,
                                                     const char *GlobalName) {
  llvm::StringRef StrWithNull(Str.c_str(), Str.size() + 1);
  CharUnits Alignment =
      getContext().getAlignOfGlobalVarInChars(getContext().CharTy);

  llvm::Constant *C =
      llvm::ConstantDataArray::getString(getLLVMContext(), StrWithNull, false);

  llvm::GlobalVariable **Entry = nullptr;
  if (!LangOpts.WritableStrings) {
    Entry = &ConstantStringMap[C];
    if (auto GV = *Entry) {
      if (uint64_t(Alignment.getQuantity()) > GV->getAlignment())
        GV->setAlignment(Alignment.getAsAlign());
      return ConstantAddress(castStringLiteralToDefaultAddressSpace(*this, GV),
                             GV->getValueType(), Alignment);
    }
  }

  if (!GlobalName)
    GlobalName = ".str";
  auto GV = generateStringLiteral(C, llvm::GlobalValue::PrivateLinkage, *this,
                                  GlobalName, Alignment);
  if (Entry)
    *Entry = GV;

  return ConstantAddress(castStringLiteralToDefaultAddressSpace(*this, GV),
                         GV->getValueType(), Alignment);
}

// ===----------------------------------------------------------------------===
// Top-level declaration dispatch & metadata
// ===----------------------------------------------------------------------===

void ModuleEmitter::genDeclContext(const DeclContext *DC) {
  for (auto *I : DC->decls()) {
    lowerTopLevel(I);
  }
}

// ===----------------------------------------------------------------------===
// Top-level declaration lowering
// ===----------------------------------------------------------------------===

__attribute__((hot)) void ModuleEmitter::lowerTopLevel(Decl *D) {

  switch (D->getKind()) {
  case Decl::Function:
    lowerGlobal(cast<FunctionDecl>(D));
    break;

  case Decl::Var:
    lowerGlobal(cast<VarDecl>(D));
    break;

  // Indirect fields from global anonymous structs and unions can be
  // ignored; only the actual variable requires IR gen support.
  case Decl::IndirectField:
    break;

  case Decl::Empty:
    break;

  case Decl::StaticAssert:
    // Nothing to do.
    break;

  case Decl::PragmaComment: {
    const auto *PCD = cast<PragmaCommentDecl>(D);
    switch (PCD->getCommentKind()) {
    case PCK_Unknown:
      llvm_unreachable("unexpected pragma comment kind");
    case PCK_Linker:
      appendLinkerOptions(PCD->getArg());
      break;
    case PCK_Lib:
      addDependentLib(PCD->getArg());
      break;
    case PCK_Compiler:
    case PCK_ExeStr:
    case PCK_User:
      break; // We ignore all of these.
    }
    break;
  }

  case Decl::PragmaDetectMismatch: {
    const auto *PDMD = cast<PragmaDetectMismatchDecl>(D);
    addDetectMismatch(PDMD->getName(), PDMD->getValue());
    break;
  }

  case Decl::FileScopeAsm: {
    auto *AD = cast<FileScopeAsmDecl>(D);
    getModule().appendModuleInlineAsm(AD->getAsmString()->getString());
    break;
  }

  case Decl::Typedef:
    if (DebugEmitter *DI = getModuleDebugInfo())
      DI->genAndRetainType(
          getContext().getTypedefType(cast<TypedefNameDecl>(D)));
    break;

  case Decl::Record:
    if (DebugEmitter *DI = getModuleDebugInfo())
      if (cast<RecordDecl>(D)->getDefinition())
        DI->genAndRetainType(getContext().getRecordType(cast<RecordDecl>(D)));
    break;

  case Decl::Enum:
    if (DebugEmitter *DI = getModuleDebugInfo())
      if (cast<EnumDecl>(D)->getDefinition())
        DI->genAndRetainType(getContext().getEnumType(cast<EnumDecl>(D)));
    break;

  default:
    // Make sure we handled everything we should, every other kind is a
    assert(isa<TypeDecl>(D) && "Unsupported decl kind");
    break;
  }
}

void ModuleEmitter::genMainVoidAlias() {
  // In order to transition away from "__original_main" gracefully, emit an
  // alias for "main" in the no-argument case so that libc can detect when
  // new-style no-argument main is in used.
  if (llvm::Function *F = getModule().getFunction("main")) {
    if (!F->isDeclaration() && F->arg_size() == 0 && !F->isVarArg() &&
        F->getReturnType()->isIntegerTy(
            Context.getTargetInfo().getIntWidth())) {
      auto *GA = llvm::GlobalAlias::create("__main_void", F);
      GA->setVisibility(llvm::GlobalValue::HiddenVisibility);
    }
  }
}

bool ModuleEmitter::checkAndReplaceExternCIFuncs(llvm::GlobalValue *Elem,
                                                 llvm::GlobalValue *Func) {
  llvm::SmallVector<llvm::GlobalIFunc *> IFuncs;
  // List of ConstantExprs that we should be able to delete when we're done
  // here.
  llvm::SmallVector<llvm::ConstantExpr *> CEs;

  // It isn't valid to replace the extern-C ifuncs if all we find is itself!
  if (Elem == Func)
    return false;

  // First make sure that all users of this are ifuncs (or ifuncs via a
  // bitcast), and collect the list of ifuncs and CEs so we can work on them
  // later.
  for (llvm::User *User : Elem->users()) {
    // Users can either be a bitcast ConstExpr that is used by the ifuncs, OR an
    // ifunc directly. In any other case, just give up, as we don't know what we
    // could break by changing those.
    if (auto *ConstExpr = dyn_cast<llvm::ConstantExpr>(User)) {
      if (ConstExpr->getOpcode() != llvm::Instruction::BitCast)
        return false;

      for (llvm::User *CEUser : ConstExpr->users()) {
        if (auto *IFunc = dyn_cast<llvm::GlobalIFunc>(CEUser)) {
          IFuncs.push_back(IFunc);
        } else {
          return false;
        }
      }
      CEs.push_back(ConstExpr);
    } else if (auto *IFunc = dyn_cast<llvm::GlobalIFunc>(User)) {
      IFuncs.push_back(IFunc);
    } else {
      // This user is one we don't know how to handle, so fail redirection. This
      // will result in an ifunc retaining a resolver name that will ultimately
      // fail to be resolved to a defined function.
      return false;
    }
  }

  // Now we know this is a valid case where we can do this alias replacement, we
  // need to remove all of the references to Elem (and the bitcasts!) so we can
  // delete it.
  for (llvm::GlobalIFunc *IFunc : IFuncs)
    IFunc->setResolver(nullptr);
  for (llvm::ConstantExpr *ConstExpr : CEs)
    ConstExpr->destroyConstant();

  // We should now be out of uses for the 'old' version of this function, so we
  // can erase it as well.
  Elem->eraseFromParent();

  for (llvm::GlobalIFunc *IFunc : IFuncs) {
    // The type of the resolver is always just a function-type that returns the
    // type of the IFunc, so create that here. If the type of the actual
    // resolver doesn't match, it just gets bitcast to the right thing.
    auto *ResolverTy =
        llvm::FunctionType::get(IFunc->getType(), /*isVarArg*/ false);
    llvm::Constant *Resolver =
        obtainLLVMFunction(Func->getName(), ResolverTy, {});
    IFunc->setResolver(Resolver);
  }
  return true;
}

void ModuleEmitter::genStaticExternCAliases() {
  if (!getTargetCodeGenInfo().shouldEmitStaticExternCAliases())
    return;
  for (auto &I : StaticExternCValues) {
    IdentifierInfo *Name = I.first;
    llvm::GlobalValue *Val = I.second;

    if (!Val)
      break;

    llvm::GlobalValue *ExistingElem =
        getModule().getNamedValue(Name->getName());

    // If there is either not something already by this name, or we were able to
    // replace all uses from IFuncs, create the alias.
    if (!ExistingElem || checkAndReplaceExternCIFuncs(ExistingElem, Val))
      addCompilerUsedGlobal(llvm::GlobalAlias::create(Name->getName(), Val));
  }
}

bool ModuleEmitter::lookupRepresentativeDecl(llvm::StringRef MangledName,
                                             GlobalDecl &Result) const {
  auto Res = Manglings.find(MangledName);
  if (Res == Manglings.end())
    return false;
  Result = Res->getValue();
  return true;
}

// ===----------------------------------------------------------------------===
// Version & command-line metadata
// ===----------------------------------------------------------------------===

void ModuleEmitter::genVersionIdentMetadata() {
  llvm::NamedMDNode *IdentMetadata =
      TheModule.getOrInsertNamedMetadata("llvm.ident");
  std::string Version = getNeverCFullVersion();
  llvm::LLVMContext &Ctx = TheModule.getContext();

  llvm::Metadata *IdentNode[] = {llvm::MDString::get(Ctx, Version)};
  IdentMetadata->addOperand(llvm::MDNode::get(Ctx, IdentNode));
}

void ModuleEmitter::genCommandLineMetadata() {
  llvm::NamedMDNode *CommandLineMetadata =
      TheModule.getOrInsertNamedMetadata("llvm.commandline");
  std::string CommandLine = getCodeGenOpts().RecordCommandLine;
  llvm::LLVMContext &Ctx = TheModule.getContext();

  llvm::Metadata *CommandLineNode[] = {llvm::MDString::get(Ctx, CommandLine)};
  CommandLineMetadata->addOperand(llvm::MDNode::get(Ctx, CommandLineNode));
}

llvm::Metadata *
ModuleEmitter::createMetadataIdentifierImpl(QualType T, MetadataTypeMap &Map,
                                            llvm::StringRef Suffix) {
  if (auto *FnType = T->getAs<FunctionProtoType>())
    T = getContext().getFunctionType(
        FnType->getReturnType(), FnType->getParamTypes(),
        FnType->getExtProtoInfo().withExceptionSpec(EST_None));

  llvm::Metadata *&InternalId = Map[T.getCanonicalType()];
  if (InternalId)
    return InternalId;

  if (isExternallyVisible(T->getLinkage())) {
    std::string OutName;
    llvm::raw_string_ostream Out(OutName);
    getCGABI().getMangleContext().mangleCanonicalTypeName(T, Out);

    Out << Suffix;

    InternalId = llvm::MDString::get(getLLVMContext(), Out.str());
  } else {
    InternalId = llvm::MDNode::getDistinct(getLLVMContext(),
                                           llvm::ArrayRef<llvm::Metadata *>());
  }

  return InternalId;
}

llvm::Metadata *ModuleEmitter::createMetadataIdentifierForType(QualType T) {
  return createMetadataIdentifierImpl(T, MetadataIdMap, "");
}

// Generalize pointer types to a void pointer with the qualifiers of the
// originally pointed-to type, e.g. 'const char *' and 'char * const *'
namespace {
QualType generalizeType(TreeContext &Ctx, QualType Ty) {
  if (!Ty->isPointerType())
    return Ty;

  return Ctx.getPointerType(
      QualType(Ctx.VoidTy)
          .withCVRQualifiers(Ty->getPointeeType().getCVRQualifiers()));
}

QualType generalizeFunctionType(TreeContext &Ctx, QualType Ty) {
  if (auto *FnType = Ty->getAs<FunctionProtoType>()) {
    llvm::SmallVector<QualType, 8> GeneralizedParams;
    for (auto &Param : FnType->param_types())
      GeneralizedParams.push_back(generalizeType(Ctx, Param));

    return Ctx.getFunctionType(generalizeType(Ctx, FnType->getReturnType()),
                               GeneralizedParams, FnType->getExtProtoInfo());
  }

  if (auto *FnType = Ty->getAs<FunctionNoProtoType>())
    return Ctx.getFunctionNoProtoType(
        generalizeType(Ctx, FnType->getReturnType()));

  llvm_unreachable("Encountered unknown FunctionType");
}
} // namespace

llvm::Metadata *ModuleEmitter::createMetadataIdentifierGeneralized(QualType T) {
  return createMetadataIdentifierImpl(generalizeFunctionType(getContext(), T),
                                      GeneralizedMetadataIdMap, ".generalized");
}

// ===----------------------------------------------------------------------===
// Type alignment
// ===----------------------------------------------------------------------===

CharUnits ModuleEmitter::getNaturalPointeeTypeAlignment(
    QualType T, LValueBaseInfo *BaseInfo, TBAAAccessInfo *TBAAInfo) {
  return getNaturalTypeAlignment(T->getPointeeType(), BaseInfo, TBAAInfo,
                                 /* forPointeeType= */ true);
}

CharUnits ModuleEmitter::getNaturalTypeAlignment(QualType T,
                                                 LValueBaseInfo *BaseInfo,
                                                 TBAAAccessInfo *TBAAInfo,
                                                 bool forPointeeType) {
  if (TBAAInfo)
    *TBAAInfo = getTBAAAccessInfo(T);

  // Honor alignment typedef attributes even on incomplete types.
  // Also honor them for record types (including as pointees) where the typedef
  // carries alignment alone.
  if (auto TT = T->getAs<TypedefType>()) {
    if (auto Align = TT->getDecl()->getMaxAlignment()) {
      if (BaseInfo)
        *BaseInfo = LValueBaseInfo(AlignmentSource::AttributedType);
      return getContext().toCharUnitsFromBits(Align);
    }
  }

  // Analyze the base element type, so we don't get confused by incomplete
  // array types.
  T = getContext().getBaseElementType(T);

  if (T->isIncompleteType()) {
    // We could try to replicate the logic from
    // TreeContext::getTypeAlignIfKnown, but nothing uses the alignment if the
    // type is incomplete, so it's impossible to test. We could try to reuse
    // getTypeAlignIfKnown, but that doesn't return the information we need
    // to set BaseInfo.  So just ignore the possibility that the alignment is
    // greater than one.
    if (BaseInfo)
      *BaseInfo = LValueBaseInfo(AlignmentSource::Type);
    return CharUnits::One();
  }

  if (BaseInfo)
    *BaseInfo = LValueBaseInfo(AlignmentSource::Type);

  CharUnits Alignment;
  if (T.getQualifiers().hasUnaligned()) {
    Alignment = CharUnits::One();
  } else {
    Alignment = getContext().getTypeAlignInChars(T);
  }

  // Cap to the global maximum type alignment unless the alignment
  // was somehow explicit on the type.
  if (unsigned MaxAlign = getLangOpts().MaxTypeAlign) {
    if (Alignment.getQuantity() > MaxAlign &&
        !getContext().isAlignmentRequired(T))
      Alignment = CharUnits::fromQuantity(MaxAlign);
  }
  return Alignment;
}

bool ModuleEmitter::stopAutoInit() {
  unsigned StopAfter = getContext().getLangOpts().TrivialAutoVarInitStopAfter;
  if (StopAfter) {
    // This number is positive only when -ftrivial-auto-var-init-stop-after=* is
    // used
    if (NumAutoVarInit >= StopAfter) {
      return true;
    }
    if (!NumAutoVarInit) {
      unsigned DiagID = getDiags().getCustomDiagID(
          DiagnosticsEngine::Warning,
          "-ftrivial-auto-var-init-stop-after=%0 has been enabled to limit the "
          "number of times ftrivial-auto-var-init=%1 gets applied.");
      getDiags().Report(DiagID)
          << StopAfter
          << (getContext().getLangOpts().getTrivialAutoVarInit() ==
                      LangOptions::TrivialAutoVarInitKind::Zero
                  ? "zero"
                  : "pattern");
    }
    ++NumAutoVarInit;
  }
  return false;
}

void ModuleEmitter::moveLazyEmissionStates(ModuleEmitter *NewBuilder) {
  assert(DeferredDeclsToEmit.empty() &&
         "Should have emitted all decls deferred to emit.");
  assert(NewBuilder->DeferredDecls.empty() &&
         "Newly created module should not have deferred decls");
  NewBuilder->DeferredDecls = std::move(DeferredDecls);

  assert(NewBuilder->MangledDeclNames.empty() &&
         "Newly created module should not have mangled decl names");
  assert(NewBuilder->Manglings.empty() &&
         "Newly created module should not have manglings");
  NewBuilder->Manglings = std::move(Manglings);

  NewBuilder->WeakRefReferences = std::move(WeakRefReferences);

  NewBuilder->TBAA = std::move(TBAA);

  NewBuilder->ABI->MangleCtx = std::move(ABI->MangleCtx);
}
