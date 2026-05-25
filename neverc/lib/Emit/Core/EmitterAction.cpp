#include "neverc/Emit/Core/EmitterAction.h"
#include "Backend/BackendConsumer.h"
#include "Core/ModuleEmitter.h"
#include "Stmt/CallEmitterInfo.h"
#include "neverc/Compiler/CompilerInstance.h"
#include "neverc/Compiler/FrontendDiag.h"
#include "neverc/Emit/Backend/BackendUtil.h"
#include "neverc/Emit/Core/ModuleBuilder.h"
#include "neverc/Foundation/Core/FileManager.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Diagnostic/DiagnosticFrontend.h"
#include "neverc/Foundation/LangOpts/LangStandard.h"
#include "neverc/Invoke/DriverDiagnostic.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Tree/Core/TreeConsumer.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Decl/DeclGroup.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"
#include "llvm/Config/config.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LLVMRemarkStreamer.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/LTO/LTOBackend.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Transforms/IPO/Internalize.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include <optional>
using namespace neverc;
using namespace llvm;

#define DEBUG_TYPE "emitter-action"

namespace llvm {
extern cl::opt<bool> ClRelinkBuiltinBitcodePostop;
}

// ===----------------------------------------------------------------------===
// NeverCDiagnosticHandler
// ===----------------------------------------------------------------------===

namespace neverc {
class EmitterConsumer;
class NeverCDiagnosticHandler final : public DiagnosticHandler {
public:
  NeverCDiagnosticHandler(const CodeGenOptions &CGOpts, EmitterConsumer *BCon)
      : CodeGenOpts(CGOpts), BackendCon(BCon) {}

  bool handleDiagnostics(const DiagnosticInfo &DI) override;

  bool isAnalysisRemarkEnabled(llvm::StringRef PassName) const override {
    return CodeGenOpts.OptimizationRemarkAnalysis.patternMatches(PassName);
  }
  bool isMissedOptRemarkEnabled(llvm::StringRef PassName) const override {
    return CodeGenOpts.OptimizationRemarkMissed.patternMatches(PassName);
  }
  bool isPassedOptRemarkEnabled(llvm::StringRef PassName) const override {
    return CodeGenOpts.OptimizationRemark.patternMatches(PassName);
  }

  bool isAnyRemarkEnabled() const override {
    return CodeGenOpts.OptimizationRemarkAnalysis.hasValidPattern() ||
           CodeGenOpts.OptimizationRemarkMissed.hasValidPattern() ||
           CodeGenOpts.OptimizationRemark.hasValidPattern();
  }

private:
  const CodeGenOptions &CodeGenOpts;
  EmitterConsumer *BackendCon;
};

namespace {

void reportOptRecordError(Error E, DiagnosticsEngine &Diags,
                          const CodeGenOptions &CodeGenOpts) {
  handleAllErrors(
      std::move(E),
      [&](const LLVMRemarkSetupFileError &E) {
        Diags.Report(diag::err_cannot_open_file)
            << CodeGenOpts.OptRecordFile << E.message();
      },
      [&](const LLVMRemarkSetupPatternError &E) {
        Diags.Report(diag::err_drv_optimization_remark_pattern)
            << E.message() << CodeGenOpts.OptRecordPasses;
      },
      [&](const LLVMRemarkSetupFormatError &E) {
        Diags.Report(diag::err_drv_optimization_remark_format)
            << CodeGenOpts.OptRecordFormat;
      });
}

} // namespace

// ===----------------------------------------------------------------------===
// EmitterConsumer - construction & module access
// ===----------------------------------------------------------------------===

EmitterConsumer::EmitterConsumer(
    BackendAction Action, DiagnosticsEngine &Diags,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS,
    const HeaderIndexOptions &HeaderIdxOpts, const PrepOptions &PPOpts,
    const CodeGenOptions &CodeGenOpts, const TargetOptions &TargetOpts,
    const LangOptions &LangOpts, const std::string &InFile,
    llvm::SmallVector<LinkModule, 4> LinkModules,
    std::unique_ptr<raw_pwrite_stream> OS, LLVMContext &C)
    : Diags(Diags), Action(Action), HeaderIdxOpts(HeaderIdxOpts),
      CodeGenOpts(CodeGenOpts), TargetOpts(TargetOpts), LangOpts(LangOpts),
      AsmOutStream(std::move(OS)), Context(nullptr), FS(VFS),
      LLVMIRGeneration("irgen", "LLVM IR Generation Time"),
      LLVMIRGenerationRefCount(0),
      Gen(CreateIRGenerator(Diags, InFile, std::move(VFS), HeaderIdxOpts,
                            PPOpts, CodeGenOpts, C)),
      LinkModules(std::move(LinkModules)) {
  TimerIsEnabled = CodeGenOpts.TimePasses;
  llvm::TimePassesIsEnabled = CodeGenOpts.TimePasses;
  llvm::TimePassesPerRun = CodeGenOpts.TimePassesPerRun;
}

/// Lightweight constructor for IR-input diagnostic handling (no output stream).
EmitterConsumer::EmitterConsumer(
    BackendAction Action, DiagnosticsEngine &Diags,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS,
    const HeaderIndexOptions &HeaderIdxOpts, const PrepOptions &PPOpts,
    const CodeGenOptions &CodeGenOpts, const TargetOptions &TargetOpts,
    const LangOptions &LangOpts, llvm::Module *Module,
    llvm::SmallVector<LinkModule, 4> LinkModules, LLVMContext &C)
    : Diags(Diags), Action(Action), HeaderIdxOpts(HeaderIdxOpts),
      CodeGenOpts(CodeGenOpts), TargetOpts(TargetOpts), LangOpts(LangOpts),
      Context(nullptr), FS(VFS),
      LLVMIRGeneration("irgen", "LLVM IR Generation Time"),
      LLVMIRGenerationRefCount(0),
      Gen(CreateIRGenerator(Diags, "", std::move(VFS), HeaderIdxOpts, PPOpts,
                            CodeGenOpts, C)),
      LinkModules(std::move(LinkModules)), CurLinkModule(Module) {
  TimerIsEnabled = CodeGenOpts.TimePasses;
  llvm::TimePassesIsEnabled = CodeGenOpts.TimePasses;
  llvm::TimePassesPerRun = CodeGenOpts.TimePassesPerRun;
}

llvm::Module *EmitterConsumer::getModule() const { return Gen->getModule(); }

std::unique_ptr<llvm::Module> EmitterConsumer::takeModule() {
  return std::unique_ptr<llvm::Module>(Gen->releaseModule());
}

IRGenerator *EmitterConsumer::getCodeGenerator() { return Gen.get(); }

// ===----------------------------------------------------------------------===
// AST processing callbacks
// ===----------------------------------------------------------------------===

void EmitterConsumer::Initialize(TreeContext &Ctx) {
  assert(!Context && "initialized multiple times");

  Context = &Ctx;

  if (TimerIsEnabled)
    LLVMIRGeneration.startTimer();

  Gen->Initialize(Ctx);

  if (TimerIsEnabled)
    LLVMIRGeneration.stopTimer();
}

bool EmitterConsumer::ProcessTopLevelDecl(DeclGroupRef D) {
#if ENABLE_CRASH_OVERRIDES
  PrettyStackTraceDecl CrashInfo(*D.begin(), SourceLocation(),
                                 Context->getSourceManager(),
                                 "LLVM IR generation of declaration");
#endif

  if (TimerIsEnabled) {
    LLVMIRGenerationRefCount += 1;
    if (LLVMIRGenerationRefCount == 1)
      LLVMIRGeneration.startTimer();
  }

  Gen->ProcessTopLevelDecl(D);

  if (TimerIsEnabled) {
    LLVMIRGenerationRefCount -= 1;
    if (LLVMIRGenerationRefCount == 0)
      LLVMIRGeneration.stopTimer();
  }

  return true;
}

void EmitterConsumer::ProcessInlineFunctionDefinition(FunctionDecl *D) {
#if ENABLE_CRASH_OVERRIDES
  PrettyStackTraceDecl CrashInfo(D, SourceLocation(),
                                 Context->getSourceManager(),
                                 "LLVM IR generation of inline function");
#endif
  if (TimerIsEnabled)
    LLVMIRGeneration.startTimer();

  Gen->ProcessInlineFunctionDefinition(D);

  if (TimerIsEnabled)
    LLVMIRGeneration.stopTimer();
}

void EmitterConsumer::ProcessInterestingDecl(DeclGroupRef D) {
  // Ignore interesting decls from the AST reader after IRGen is finished.
  if (!IRGenFinished)
    ProcessTopLevelDecl(D);
}

// ===----------------------------------------------------------------------===
// Module linking
// ===----------------------------------------------------------------------===

bool EmitterConsumer::LinkInModules(llvm::Module *M, bool ShouldLinkFiles) {

  for (auto &LM : LinkModules) {
    assert(LM.Module && "LinkModule does not actually have a module");

    // If ShouldLinkFiles is not set, skip files added via the
    // -mlink-bitcode-files, only linking -mlink-builtin-bitcode
    if (!LM.Internalize && !ShouldLinkFiles)
      continue;

    CurLinkModule = LM.Module.get();

    if (Error E = CurLinkModule->materializeAll())
      return false;

    if (LM.PropagateAttrs)
      for (Function &F : *LM.Module) {
        // Skip intrinsics. Keep consistent with how intrinsics are created
        // in LLVM IR.
        if (F.isIntrinsic())
          continue;
        Emit::mergeDefaultFunctionDefinitionAttributes(
            F, CodeGenOpts, LangOpts, TargetOpts, LM.Internalize);
      }

    bool Err;

    auto DoLink = [&](auto &Mod) {
      if (LM.Internalize) {
        Err = Linker::linkModules(
            *M, std::move(Mod), LM.LinkFlags,
            [](llvm::Module &M, const llvm::StringSet<> &GVS) {
              internalizeModule(M, [&GVS](const llvm::GlobalValue &GV) {
                return !GV.hasName() || !GVS.contains(GV.getName());
              });
            });
      } else
        Err = Linker::linkModules(*M, std::move(Mod), LM.LinkFlags);
    };

    if (ClRelinkBuiltinBitcodePostop) {
      std::unique_ptr<llvm::Module> Clone = llvm::CloneModule(*LM.Module);
      DoLink(Clone);
    } else {
      DoLink(LM.Module);
    }
  }

  return false; // success
}

void EmitterConsumer::ProcessTranslationUnit(TreeContext &C) {
  {
    llvm::TimeTraceScope TimeScope("Frontend");
    PrettyStackTraceString CrashInfo("Per-file LLVM IR generation");
    if (TimerIsEnabled) {
      LLVMIRGenerationRefCount += 1;
      if (LLVMIRGenerationRefCount == 1)
        LLVMIRGeneration.startTimer();
    }

    Gen->ProcessTranslationUnit(C);

    if (TimerIsEnabled) {
      LLVMIRGenerationRefCount -= 1;
      if (LLVMIRGenerationRefCount == 0)
        LLVMIRGeneration.stopTimer();
    }

    IRGenFinished = true;
  }

  // Silently ignore if we weren't initialized for some reason.
  if (!getModule())
    return;

  LLVMContext &Ctx = getModule()->getContext();
  std::unique_ptr<DiagnosticHandler> OldDiagnosticHandler =
      Ctx.getDiagnosticHandler();
  Ctx.setDiagnosticHandler(
      std::make_unique<NeverCDiagnosticHandler>(CodeGenOpts, this));

  llvm::Expected<std::unique_ptr<llvm::ToolOutputFile>> OptRecordFileOrErr =
      setupLLVMOptimizationRemarks(
          Ctx, CodeGenOpts.OptRecordFile, CodeGenOpts.OptRecordPasses,
          CodeGenOpts.OptRecordFormat, CodeGenOpts.DiagnosticsWithHotness,
          CodeGenOpts.DiagnosticsHotnessThreshold);

  if (Error E = OptRecordFileOrErr.takeError()) {
    reportOptRecordError(std::move(E), Diags, CodeGenOpts);
    return;
  }

  std::unique_ptr<llvm::ToolOutputFile> OptRecordFile =
      std::move(*OptRecordFileOrErr);

  // Link each LinkModule into our module.
  if (LinkInModules(getModule()))
    return;

  for (auto &F : getModule()->functions()) {
    if (const Decl *FD = Gen->getDeclForMangledName(F.getName())) {
      auto Loc = FD->getTreeContext().getFullLoc(FD->getLocation());
      auto NameHash = llvm::hash_value(F.getName());
      ManglingFullSourceLocs.push_back(std::make_pair(NameHash, Loc));
    }
  }

  if (CodeGenOpts.ClearASTBeforeBackend) {
    LLVM_DEBUG(llvm::dbgs() << "Clearing AST...\n");
    C.cleanup();
    C.getAllocator().Reset();
  }

  genBackendOutput(Diags, HeaderIdxOpts, CodeGenOpts, TargetOpts, LangOpts,
                   C.getTargetInfo().getDataLayoutString(), getModule(), Action,
                   FS, std::move(AsmOutStream), this);

  Ctx.setDiagnosticHandler(std::move(OldDiagnosticHandler));

  if (OptRecordFile)
    OptRecordFile->keep();
}

void EmitterConsumer::ProcessTagDeclDefinition(TagDecl *D) {
#if ENABLE_CRASH_OVERRIDES
  PrettyStackTraceDecl CrashInfo(D, SourceLocation(),
                                 Context->getSourceManager(),
                                 "LLVM IR generation of declaration");
#endif
  Gen->ProcessTagDeclDefinition(D);
}

void EmitterConsumer::ProcessTagDeclRequiredDefinition(const TagDecl *D) {
  Gen->ProcessTagDeclRequiredDefinition(D);
}

void EmitterConsumer::FinalizeTentativeDefinition(VarDecl *D) {
  Gen->FinalizeTentativeDefinition(D);
}

void EmitterConsumer::FinalizeExternalDeclaration(VarDecl *D) {
  Gen->FinalizeExternalDeclaration(D);
}

void EmitterConsumer::anchor() {}

} // namespace neverc

// ===----------------------------------------------------------------------===
// EmitterConsumer - diagnostic handling
// ===----------------------------------------------------------------------===

bool NeverCDiagnosticHandler::handleDiagnostics(const DiagnosticInfo &DI) {
  BackendCon->DiagnosticHandlerImpl(DI);
  return true;
}

namespace {

/// Map an LLVM SMDiagnostic location into NeverC's SourceManager by copying
/// the underlying memory buffer (both source managers need ownership).
FullSourceLoc convertBackendLocation(const llvm::SMDiagnostic &D,
                                     SourceManager &CSM) {
  const llvm::SourceMgr &LSM = *D.getSourceMgr();
  const MemoryBuffer *LBuf =
      LSM.getMemoryBuffer(LSM.FindBufferContainingLoc(D.getLoc()));

  std::unique_ptr<llvm::MemoryBuffer> CBuf =
      llvm::MemoryBuffer::getMemBufferCopy(LBuf->getBuffer(),
                                           LBuf->getBufferIdentifier());
  FileID FID = CSM.createFileID(std::move(CBuf));

  unsigned Offset = D.getLoc().getPointer() - LBuf->getBufferStart();
  SourceLocation NewLoc =
      CSM.getLocForStartOfFile(FID).getLocWithOffset(Offset);
  return FullSourceLoc(NewLoc, CSM);
}

} // namespace

#define ComputeDiagID(Severity, GroupName, DiagID)                             \
  do {                                                                         \
    switch (Severity) {                                                        \
    case llvm::DS_Error:                                                       \
      DiagID = diag::err_fe_##GroupName;                                       \
      break;                                                                   \
    case llvm::DS_Warning:                                                     \
      DiagID = diag::warn_fe_##GroupName;                                      \
      break;                                                                   \
    case llvm::DS_Remark:                                                      \
      llvm_unreachable("'remark' severity not expected");                      \
      break;                                                                   \
    case llvm::DS_Note:                                                        \
      DiagID = diag::note_fe_##GroupName;                                      \
      break;                                                                   \
    }                                                                          \
  } while (false)

#define ComputeDiagRemarkID(Severity, GroupName, DiagID)                       \
  do {                                                                         \
    switch (Severity) {                                                        \
    case llvm::DS_Error:                                                       \
      DiagID = diag::err_fe_##GroupName;                                       \
      break;                                                                   \
    case llvm::DS_Warning:                                                     \
      DiagID = diag::warn_fe_##GroupName;                                      \
      break;                                                                   \
    case llvm::DS_Remark:                                                      \
      DiagID = diag::remark_fe_##GroupName;                                    \
      break;                                                                   \
    case llvm::DS_Note:                                                        \
      DiagID = diag::note_fe_##GroupName;                                      \
      break;                                                                   \
    }                                                                          \
  } while (false)

void EmitterConsumer::SrcMgrDiagHandler(const llvm::DiagnosticInfoSrcMgr &DI) {
  const llvm::SMDiagnostic &D = DI.getSMDiag();

  unsigned DiagID;
  if (DI.isInlineAsmDiag())
    ComputeDiagID(DI.getSeverity(), inline_asm, DiagID);
  else
    ComputeDiagID(DI.getSeverity(), source_mgr, DiagID);

  // This is for the empty EmitterConsumer that uses the NeverC diagnostic
  // handler for IR input files.
  if (!Context) {
    D.print(nullptr, llvm::errs());
    Diags.Report(DiagID).AddString("cannot compile inline asm");
    return;
  }

  llvm::StringRef Message = D.getMessage();
  (void)Message.consume_front("error: ");

  FullSourceLoc Loc;
  if (D.getLoc() != SMLoc())
    Loc = convertBackendLocation(D, Context->getSourceManager());

  if (DI.isInlineAsmDiag()) {
    SourceLocation LocCookie =
        SourceLocation::getFromRawEncoding(DI.getLocCookie());
    if (LocCookie.isValid()) {
      Diags.Report(LocCookie, DiagID).AddString(Message);

      if (D.getLoc().isValid()) {
        DiagnosticBuilder B = Diags.Report(Loc, diag::note_fe_inline_asm_here);
        for (const auto &Range : D.getRanges()) {
          unsigned Column = D.getColumnNo();
          B << SourceRange(Loc.getLocWithOffset(Range.first - Column),
                           Loc.getLocWithOffset(Range.second - Column));
        }
      }
      return;
    }
  }

  Diags.Report(Loc, DiagID).AddString(Message);
}

bool EmitterConsumer::InlineAsmDiagHandler(
    const llvm::DiagnosticInfoInlineAsm &D) {
  unsigned DiagID;
  ComputeDiagID(D.getSeverity(), inline_asm, DiagID);
  std::string Message = D.getMsgStr().str();

  SourceLocation LocCookie =
      SourceLocation::getFromRawEncoding(D.getLocCookie());
  if (LocCookie.isValid())
    Diags.Report(LocCookie, DiagID).AddString(Message);
  else {
    FullSourceLoc Loc;
    Diags.Report(Loc, DiagID).AddString(Message);
  }
  return true;
}

bool EmitterConsumer::StackSizeDiagHandler(
    const llvm::DiagnosticInfoStackSize &D) {
  if (D.getSeverity() != llvm::DS_Warning)
    return false;

  auto Loc = getFunctionSourceLocation(D.getFunction());
  if (!Loc)
    return false;

  Diags.Report(*Loc, diag::warn_fe_frame_larger_than)
      << D.getStackSize() << D.getStackLimit()
      << D.getFunction().getName().str();
  return true;
}

bool EmitterConsumer::ResourceLimitDiagHandler(
    const llvm::DiagnosticInfoResourceLimit &D) {
  auto Loc = getFunctionSourceLocation(D.getFunction());
  if (!Loc)
    return false;
  unsigned DiagID = diag::err_fe_backend_resource_limit;
  ComputeDiagID(D.getSeverity(), backend_resource_limit, DiagID);

  Diags.Report(*Loc, DiagID)
      << D.getResourceName() << D.getResourceSize() << D.getResourceLimit()
      << D.getFunction().getName().str();
  return true;
}

const FullSourceLoc EmitterConsumer::getBestLocationFromDebugLoc(
    const llvm::DiagnosticInfoWithLocationBase &D, bool &BadDebugInfo,
    llvm::StringRef &Filename, unsigned &Line, unsigned &Column) const {
  SourceManager &SourceMgr = Context->getSourceManager();
  FileManager &FileMgr = SourceMgr.getFileManager();
  SourceLocation DILoc;

  if (D.isLocationAvailable()) {
    D.getLocation(Filename, Line, Column);
    if (Line > 0) {
      auto FE = FileMgr.getFile(Filename);
      if (!FE)
        FE = FileMgr.getFile(D.getAbsolutePath());
      if (FE) {
        // If -gcolumn-info was not used, Column will be 0. This upsets the
        // source manager, so pass 1 if Column is not set.
        DILoc = SourceMgr.translateFileLineCol(*FE, Line, Column ? Column : 1);
      }
    }
    BadDebugInfo = DILoc.isInvalid();
  }

  // If a location isn't available, try to approximate it using the associated
  // function definition. We use the definition's right brace to differentiate
  // from diagnostics that genuinely relate to the function itself.
  FullSourceLoc Loc(DILoc, SourceMgr);
  if (Loc.isInvalid()) {
    if (auto MaybeLoc = getFunctionSourceLocation(D.getFunction()))
      Loc = *MaybeLoc;
  }

  // #line directives can make file:line:col untranslatable; emit a note.
  if (DILoc.isInvalid() && D.isLocationAvailable())
    Diags.Report(Loc, diag::note_fe_backend_invalid_loc)
        << Filename << Line << Column;

  return Loc;
}

std::optional<FullSourceLoc>
EmitterConsumer::getFunctionSourceLocation(const Function &F) const {
  auto Hash = llvm::hash_value(F.getName());
  for (const auto &Pair : ManglingFullSourceLocs) {
    if (Pair.first == Hash)
      return Pair.second;
  }
  return std::nullopt;
}

void EmitterConsumer::UnsupportedDiagHandler(
    const llvm::DiagnosticInfoUnsupported &D) {
  assert(D.getSeverity() == llvm::DS_Error ||
         D.getSeverity() == llvm::DS_Warning);

  llvm::StringRef Filename;
  unsigned Line, Column;
  bool BadDebugInfo = false;
  FullSourceLoc Loc;
  std::string Msg;
  raw_string_ostream MsgStream(Msg);

  if (Context != nullptr) {
    Loc = getBestLocationFromDebugLoc(D, BadDebugInfo, Filename, Line, Column);
    MsgStream << D.getMessage();
  } else {
    DiagnosticPrinterRawOStream DP(MsgStream);
    D.print(DP);
  }

  auto DiagType = D.getSeverity() == llvm::DS_Error
                      ? diag::err_fe_backend_unsupported
                      : diag::warn_fe_backend_unsupported;
  Diags.Report(Loc, DiagType) << MsgStream.str();

  if (BadDebugInfo)
    Diags.Report(Loc, diag::note_fe_backend_invalid_loc)
        << Filename << Line << Column;
}

void EmitterConsumer::genOptimizationMessage(
    const llvm::DiagnosticInfoOptimizationBase &D, unsigned DiagID) {
  assert(D.getSeverity() == llvm::DS_Remark ||
         D.getSeverity() == llvm::DS_Warning);

  llvm::StringRef Filename;
  unsigned Line, Column;
  bool BadDebugInfo = false;
  FullSourceLoc Loc;
  std::string Msg;
  raw_string_ostream MsgStream(Msg);

  if (Context != nullptr) {
    Loc = getBestLocationFromDebugLoc(D, BadDebugInfo, Filename, Line, Column);
    MsgStream << D.getMsg();
  } else {
    DiagnosticPrinterRawOStream DP(MsgStream);
    D.print(DP);
  }

  if (D.getHotness())
    MsgStream << " (hotness: " << *D.getHotness() << ")";

  Diags.Report(Loc, DiagID) << AddFlagValue(D.getPassName()) << MsgStream.str();

  if (BadDebugInfo)
    Diags.Report(Loc, diag::note_fe_backend_invalid_loc)
        << Filename << Line << Column;
}

void EmitterConsumer::OptimizationRemarkHandler(
    const llvm::DiagnosticInfoOptimizationBase &D) {
  // Without hotness information, don't show noisy remarks.
  if (D.isVerbose() && !D.getHotness())
    return;

  if (D.isPassed()) {
    if (CodeGenOpts.OptimizationRemark.patternMatches(D.getPassName()))
      genOptimizationMessage(D, diag::remark_fe_backend_optimization_remark);
  } else if (D.isMissed()) {
    if (CodeGenOpts.OptimizationRemarkMissed.patternMatches(D.getPassName()))
      genOptimizationMessage(
          D, diag::remark_fe_backend_optimization_remark_missed);
  } else {
    assert(D.isAnalysis() && "Unknown remark type");

    bool ShouldAlwaysPrint = false;
    if (auto *ORA = dyn_cast<llvm::OptimizationRemarkAnalysis>(&D))
      ShouldAlwaysPrint = ORA->shouldAlwaysPrint();

    if (ShouldAlwaysPrint ||
        CodeGenOpts.OptimizationRemarkAnalysis.patternMatches(D.getPassName()))
      genOptimizationMessage(
          D, diag::remark_fe_backend_optimization_remark_analysis);
  }
}

void EmitterConsumer::OptimizationRemarkHandler(
    const llvm::OptimizationRemarkAnalysisFPCommute &D) {
  if (D.shouldAlwaysPrint() ||
      CodeGenOpts.OptimizationRemarkAnalysis.patternMatches(D.getPassName()))
    genOptimizationMessage(
        D, diag::remark_fe_backend_optimization_remark_analysis_fpcommute);
}

void EmitterConsumer::OptimizationRemarkHandler(
    const llvm::OptimizationRemarkAnalysisAliasing &D) {
  if (D.shouldAlwaysPrint() ||
      CodeGenOpts.OptimizationRemarkAnalysis.patternMatches(D.getPassName()))
    genOptimizationMessage(
        D, diag::remark_fe_backend_optimization_remark_analysis_aliasing);
}

void EmitterConsumer::OptimizationFailureHandler(
    const llvm::DiagnosticInfoOptimizationFailure &D) {
  genOptimizationMessage(D, diag::warn_fe_backend_optimization_failure);
}

void EmitterConsumer::DontCallDiagHandler(const DiagnosticInfoDontCall &D) {
  SourceLocation LocCookie =
      SourceLocation::getFromRawEncoding(D.getLocCookie());

  if (!LocCookie.isValid())
    return;

  Diags.Report(LocCookie, D.getSeverity() == DiagnosticSeverity::DS_Error
                              ? diag::err_fe_backend_error_attr
                              : diag::warn_fe_backend_warning_attr)
      << D.getFunctionName().str() << D.getNote();
}

void EmitterConsumer::DiagnosticHandlerImpl(const DiagnosticInfo &DI) {
  unsigned DiagID = diag::err_fe_inline_asm;
  llvm::DiagnosticSeverity Severity = DI.getSeverity();
  switch (DI.getKind()) {
  case llvm::DK_InlineAsm:
    if (InlineAsmDiagHandler(cast<DiagnosticInfoInlineAsm>(DI)))
      return;
    ComputeDiagID(Severity, inline_asm, DiagID);
    break;
  case llvm::DK_SrcMgr:
    SrcMgrDiagHandler(cast<DiagnosticInfoSrcMgr>(DI));
    return;
  case llvm::DK_StackSize:
    if (StackSizeDiagHandler(cast<DiagnosticInfoStackSize>(DI)))
      return;
    ComputeDiagID(Severity, backend_frame_larger_than, DiagID);
    break;
  case llvm::DK_ResourceLimit:
    if (ResourceLimitDiagHandler(cast<DiagnosticInfoResourceLimit>(DI)))
      return;
    ComputeDiagID(Severity, backend_resource_limit, DiagID);
    break;
  case llvm::DK_OptimizationRemark:
    OptimizationRemarkHandler(cast<OptimizationRemark>(DI));
    return;
  case llvm::DK_OptimizationRemarkMissed:
    OptimizationRemarkHandler(cast<OptimizationRemarkMissed>(DI));
    return;
  case llvm::DK_OptimizationRemarkAnalysis:
    OptimizationRemarkHandler(cast<OptimizationRemarkAnalysis>(DI));
    return;
  case llvm::DK_OptimizationRemarkAnalysisFPCommute:
    OptimizationRemarkHandler(cast<OptimizationRemarkAnalysisFPCommute>(DI));
    return;
  case llvm::DK_OptimizationRemarkAnalysisAliasing:
    OptimizationRemarkHandler(cast<OptimizationRemarkAnalysisAliasing>(DI));
    return;
  case llvm::DK_MachineOptimizationRemark:
    OptimizationRemarkHandler(cast<MachineOptimizationRemark>(DI));
    return;
  case llvm::DK_MachineOptimizationRemarkMissed:
    OptimizationRemarkHandler(cast<MachineOptimizationRemarkMissed>(DI));
    return;
  case llvm::DK_MachineOptimizationRemarkAnalysis:
    OptimizationRemarkHandler(cast<MachineOptimizationRemarkAnalysis>(DI));
    return;
  case llvm::DK_OptimizationFailure:
    OptimizationFailureHandler(cast<DiagnosticInfoOptimizationFailure>(DI));
    return;
  case llvm::DK_Unsupported:
    UnsupportedDiagHandler(cast<DiagnosticInfoUnsupported>(DI));
    return;
  case llvm::DK_DontCall:
    DontCallDiagHandler(cast<DiagnosticInfoDontCall>(DI));
    return;
  default:
    // Plugin IDs are not bound to any value as they are set dynamically.
    ComputeDiagRemarkID(Severity, backend_plugin, DiagID);
    break;
  }
  std::string MsgStorage;
  {
    raw_string_ostream Stream(MsgStorage);
    DiagnosticPrinterRawOStream DP(Stream);
    DI.print(DP);
  }

  FullSourceLoc Loc;
  Diags.Report(Loc, DiagID).AddString(MsgStorage);
}
#undef ComputeDiagID
#undef ComputeDiagRemarkID

// ===----------------------------------------------------------------------===
// EmitterAction
// ===----------------------------------------------------------------------===

EmitterAction::EmitterAction(unsigned _Act, LLVMContext *_VMContext)
    : Act(_Act), VMContext(_VMContext ? _VMContext : new LLVMContext),
      OwnsVMContext(!_VMContext) {}

EmitterAction::~EmitterAction() {
  TheModule.reset();
  if (OwnsVMContext)
    delete VMContext;
}

bool EmitterAction::loadLinkModules(CompilerInstance &CI) {
  if (!LinkModules.empty())
    return false;

  for (const CodeGenOptions::BitcodeFileToLink &F :
       CI.getCodeGenOpts().LinkBitcodeFiles) {
    auto BCBuf = CI.getFileManager().getBufferForFile(F.Filename);
    if (!BCBuf) {
      CI.getDiagnostics().Report(diag::err_cannot_open_file)
          << F.Filename << BCBuf.getError().message();
      LinkModules.clear();
      return true;
    }

    llvm::Expected<std::unique_ptr<llvm::Module>> ModuleOrErr =
        getOwningLazyBitcodeModule(std::move(*BCBuf), *VMContext);
    if (!ModuleOrErr) {
      handleAllErrors(ModuleOrErr.takeError(), [&](ErrorInfoBase &EIB) {
        CI.getDiagnostics().Report(diag::err_cannot_open_file)
            << F.Filename << EIB.message();
      });
      LinkModules.clear();
      return true;
    }
    LinkModules.push_back({std::move(ModuleOrErr.get()), F.PropagateAttrs,
                           F.Internalize, F.LinkFlags});
  }
  return false;
}

bool EmitterAction::hasIRSupport() const { return true; }

void EmitterAction::EndSourceFileAction() {
  if (!getCompilerInstance().hasTreeConsumer())
    return;

  TheModule = BEConsumer->takeModule();
}

std::unique_ptr<llvm::Module> EmitterAction::takeModule() {
  return std::move(TheModule);
}

llvm::LLVMContext *EmitterAction::takeLLVMContext() {
  OwnsVMContext = false;
  return VMContext;
}

IRGenerator *EmitterAction::getCodeGenerator() const {
  return BEConsumer->getCodeGenerator();
}

namespace {

std::unique_ptr<raw_pwrite_stream> getOutputStream(CompilerInstance &CI,
                                                   llvm::StringRef InFile,
                                                   BackendAction Action) {
  switch (Action) {
  case Backend_EmitAssembly:
    return CI.createDefaultOutputFile(false, InFile, "s");
  case Backend_EmitLL:
    return CI.createDefaultOutputFile(false, InFile, "ll");
  case Backend_EmitBC:
    return CI.createDefaultOutputFile(true, InFile, "bc");
  case Backend_EmitNothing:
    return nullptr;
  case Backend_EmitMCNull:
    return CI.createNullOutputFile();
  case Backend_EmitObj:
    return CI.createDefaultOutputFile(true, InFile, "o");
  }

  llvm_unreachable("Invalid action!");
}

} // namespace

std::unique_ptr<TreeConsumer>
EmitterAction::CreateTreeConsumer(CompilerInstance &CI,
                                  llvm::StringRef InFile) {
  BackendAction BA = static_cast<BackendAction>(Act);
  std::unique_ptr<raw_pwrite_stream> OS = CI.takeOutputStream();
  if (!OS)
    OS = getOutputStream(CI, InFile, BA);

  if (BA != Backend_EmitNothing && !OS)
    return nullptr;

  if (loadLinkModules(CI))
    return nullptr;

  std::unique_ptr<EmitterConsumer> Result(new EmitterConsumer(
      BA, CI.getDiagnostics(), &CI.getVirtualFileSystem(),
      CI.getHeaderIdxOpts(), CI.getPrepOpts(), CI.getCodeGenOpts(),
      CI.getTargetOpts(), CI.getLangOpts(), std::string(InFile),
      std::move(LinkModules), std::move(OS), *VMContext));
  BEConsumer = Result.get();

  return std::move(Result);
}

std::unique_ptr<llvm::Module>
EmitterAction::importModule(MemoryBufferRef MBRef) {
  CompilerInstance &CI = getCompilerInstance();
  SourceManager &SM = CI.getSourceManager();

  auto DiagErrors = [&](Error E) -> std::unique_ptr<llvm::Module> {
    unsigned DiagID =
        CI.getDiagnostics().getCustomDiagID(DiagnosticsEngine::Error, "%0");
    handleAllErrors(std::move(E), [&](ErrorInfoBase &EIB) {
      CI.getDiagnostics().Report(DiagID) << EIB.message();
    });
    return {};
  };

  if (loadLinkModules(CI))
    return nullptr;

  llvm::SMDiagnostic Err;
  if (std::unique_ptr<llvm::Module> M = parseIR(MBRef, Err, *VMContext))
    return M;

  // If MBRef is a bitcode with multiple modules (e.g., -fsplit-lto-unit
  // output), place the extra modules (actually only one, a regular LTO module)
  // into LinkModules as if we are using -mlink-bitcode-file.
  llvm::Expected<std::vector<BitcodeModule>> BMsOrErr =
      getBitcodeModuleList(MBRef);
  if (BMsOrErr && BMsOrErr->size()) {
    std::unique_ptr<llvm::Module> FirstM;
    for (auto &BM : *BMsOrErr) {
      llvm::Expected<std::unique_ptr<llvm::Module>> MOrErr =
          BM.parseModule(*VMContext);
      if (!MOrErr)
        return DiagErrors(MOrErr.takeError());
      if (FirstM)
        LinkModules.push_back({std::move(*MOrErr), /*PropagateAttrs=*/false,
                               /*Internalize=*/false, /*LinkFlags=*/{}});
      else
        FirstM = std::move(*MOrErr);
    }
    if (FirstM)
      return FirstM;
  }
  consumeError(BMsOrErr.takeError());

  SourceLocation Loc;
  if (Err.getLineNo() > 0) {
    assert(Err.getColumnNo() >= 0);
    Loc = SM.translateFileLineCol(SM.getFileEntryForID(SM.getMainFileID()),
                                  Err.getLineNo(), Err.getColumnNo() + 1);
  }

  llvm::StringRef Msg = Err.getMessage();
  Msg.consume_front("error: ");

  unsigned DiagID =
      CI.getDiagnostics().getCustomDiagID(DiagnosticsEngine::Error, "%0");

  CI.getDiagnostics().Report(Loc, DiagID) << Msg;
  return {};
}

void EmitterAction::ExecuteAction() {
  if (getCurrentFileKind().getLanguage() != Language::LLVM_IR) {
    this->ASTFrontendAction::ExecuteAction();
    return;
  }

  BackendAction BA = static_cast<BackendAction>(Act);
  CompilerInstance &CI = getCompilerInstance();
  auto &CodeGenOpts = CI.getCodeGenOpts();
  auto &Diagnostics = CI.getDiagnostics();
  std::unique_ptr<raw_pwrite_stream> OS =
      getOutputStream(CI, getCurrentFileOrBufferName(), BA);
  if (BA != Backend_EmitNothing && !OS)
    return;

  SourceManager &SM = CI.getSourceManager();
  FileID FID = SM.getMainFileID();
  std::optional<MemoryBufferRef> MainFile = SM.getBufferOrNone(FID);
  if (!MainFile)
    return;

  TheModule = importModule(*MainFile);
  if (!TheModule)
    return;

  const TargetOptions &TargetOpts = CI.getTargetOpts();
  if (TheModule->getTargetTriple() != TargetOpts.Triple) {
    Diagnostics.Report(SourceLocation(), diag::warn_fe_override_module)
        << TargetOpts.Triple;
    TheModule->setTargetTriple(TargetOpts.Triple);
  }

  LLVMContext &Ctx = TheModule->getContext();

  struct RAII {
    LLVMContext &Ctx;
    std::unique_ptr<DiagnosticHandler> PrevHandler = Ctx.getDiagnosticHandler();
    ~RAII() { Ctx.setDiagnosticHandler(std::move(PrevHandler)); }
  } _{Ctx};

  EmitterConsumer Result(BA, CI.getDiagnostics(), &CI.getVirtualFileSystem(),
                         CI.getHeaderIdxOpts(), CI.getPrepOpts(),
                         CI.getCodeGenOpts(), CI.getTargetOpts(),
                         CI.getLangOpts(), TheModule.get(),
                         std::move(LinkModules), *VMContext);

  if (Result.LinkInModules(&*TheModule))
    return;

  // PR44896: Force DiscardValueNames as false. DiscardValueNames cannot be
  // true here because the valued names are needed for reading textual IR.
  Ctx.setDiscardValueNames(false);
  Ctx.setDiagnosticHandler(
      std::make_unique<NeverCDiagnosticHandler>(CodeGenOpts, &Result));

  llvm::Expected<std::unique_ptr<llvm::ToolOutputFile>> OptRecordFileOrErr =
      setupLLVMOptimizationRemarks(
          Ctx, CodeGenOpts.OptRecordFile, CodeGenOpts.OptRecordPasses,
          CodeGenOpts.OptRecordFormat, CodeGenOpts.DiagnosticsWithHotness,
          CodeGenOpts.DiagnosticsHotnessThreshold);

  if (Error E = OptRecordFileOrErr.takeError()) {
    reportOptRecordError(std::move(E), Diagnostics, CodeGenOpts);
    return;
  }
  std::unique_ptr<llvm::ToolOutputFile> OptRecordFile =
      std::move(*OptRecordFileOrErr);

  genBackendOutput(
      Diagnostics, CI.getHeaderIdxOpts(), CodeGenOpts, TargetOpts,
      CI.getLangOpts(), CI.getTarget().getDataLayoutString(), TheModule.get(),
      BA, CI.getFileManager().getVirtualFileSystemPtr(), std::move(OS));
  if (OptRecordFile)
    OptRecordFile->keep();
}

// ===----------------------------------------------------------------------===
// Concrete action subclasses
// ===----------------------------------------------------------------------===

void GenAssemblyAction::anchor() {}
GenAssemblyAction::GenAssemblyAction(llvm::LLVMContext *_VMContext)
    : EmitterAction(Backend_EmitAssembly, _VMContext) {}

void GenBCAction::anchor() {}
GenBCAction::GenBCAction(llvm::LLVMContext *_VMContext)
    : EmitterAction(Backend_EmitBC, _VMContext) {}

void GenLLVMAction::anchor() {}
GenLLVMAction::GenLLVMAction(llvm::LLVMContext *_VMContext)
    : EmitterAction(Backend_EmitLL, _VMContext) {}

void GenObjAction::anchor() {}
GenObjAction::GenObjAction(llvm::LLVMContext *_VMContext)
    : EmitterAction(Backend_EmitObj, _VMContext) {}
