#include "neverc/Emit/Backend/BackendUtil.h"
#include "Backend/BackendConsumer.h"
#include "Backend/LinkInModulesPass.h"
#include "Backend/MimallocRuntimeLinker.h"
#include "Backend/StringRuntimeLinker.h"
#include "neverc/Transforms/XorStr/EncryptCallStringsPass.h"
#include "neverc/Transforms/XorStr/XorStrCleanupPass.h"
#include "neverc/Compiler/FrontendDiag.h"
#include "neverc/Compiler/Utils.h"
#include "neverc/Emit/Backend/ParallelCodeGenMerge.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Foundation/LangOpts/CodeGenOptions.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetOptions.h"
#include "neverc/Scan/HeaderIndexOptions.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Bitcode/BitcodeAutoGeneratorPass.h"
#include "llvm/Bitcode/BitcodeWriterPass.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Frontend/Driver/CodeGenOptions.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRPrinter/IRAutoGeneratorPass.h"
#include "llvm/IRPrinter/IRPrintingPasses.h"
#include "llvm/LTO/LTOBackend.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Support/BuryPointer.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/IPO/Annotation2Metadata.h"
#include "llvm/Transforms/IPO/MSVCMacroRebuilding.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/Transforms/Utils/Debugify.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <memory>
#include <optional>
using namespace neverc;
using namespace llvm;

namespace llvm {
extern cl::opt<bool> PrintPipelinePasses;

// Re-link builtin bitcodes after optimization
cl::opt<bool> ClRelinkBuiltinBitcodePostop(
    "relink-builtin-bitcode-postop", cl::Optional,
    cl::desc("Re-link builtin bitcodes after optimization."), cl::init(false));
} // namespace llvm

namespace {

// Default filename used for profile generation.
std::string getDefaultProfileGenName() { return "default_%m.profraw"; }

class GenAssemblyHelper {
  DiagnosticsEngine &Diags;
  const HeaderIndexOptions &HSOpts;
  const CodeGenOptions &CodeGenOpts;
  const neverc::TargetOptions &TargetOpts;
  const LangOptions &LangOpts;
  llvm::Module *TheModule;
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS;

  Timer CodeGenerationTime;

  std::unique_ptr<raw_pwrite_stream> OS;

  Triple TargetTriple;

  TargetIRAnalysis getTargetIRAnalysis() const {
    if (TM)
      return TM->getTargetIRAnalysis();

    return TargetIRAnalysis();
  }

  void createTargetMachine(bool MustCreateTM);

  bool addEmitPasses(legacy::PassManager &CodeGenPasses, BackendAction Action,
                     raw_pwrite_stream &OS, raw_pwrite_stream *DwoOS);

  std::unique_ptr<llvm::ToolOutputFile> openOutputFile(llvm::StringRef Path) {
    std::error_code EC;
    auto F = std::make_unique<llvm::ToolOutputFile>(Path, EC,
                                                    llvm::sys::fs::OF_None);
    if (EC) {
      Diags.Report(diag::err_fe_unable_to_open_output) << Path << EC.message();
      F.reset();
    }
    return F;
  }

  void runOptimizationPipeline(BackendAction Action,
                               std::unique_ptr<raw_pwrite_stream> &OS,
                               EmitterConsumer *BC);
  void runCodegenPipeline(BackendAction Action,
                          std::unique_ptr<raw_pwrite_stream> &OS,
                          std::unique_ptr<llvm::ToolOutputFile> &DwoOS);

  // NeverC always uses Full LTO (auto-lto); the linker loads each bitcode
  // module in its entirety via parseModule(), so the per-file module summary
  // index is never emitted.
  static constexpr bool EmitLTOSummary = false;

public:
  GenAssemblyHelper(DiagnosticsEngine &_Diags,
                    const HeaderIndexOptions &HeaderIdxOpts,
                    const CodeGenOptions &CGOpts,
                    const neverc::TargetOptions &TOpts,
                    const LangOptions &LOpts, llvm::Module *M,
                    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS)
      : Diags(_Diags), HSOpts(HeaderIdxOpts), CodeGenOpts(CGOpts),
        TargetOpts(TOpts), LangOpts(LOpts), TheModule(M), VFS(std::move(VFS)),
        CodeGenerationTime("codegen", "Code Generation Time"),
        TargetTriple(TheModule->getTargetTriple()) {}

  ~GenAssemblyHelper() {
    if (CodeGenOpts.DisableFree)
      BuryPointer(std::move(TM));
  }

  std::unique_ptr<TargetMachine> TM;

  void genAssembly(BackendAction Action, std::unique_ptr<raw_pwrite_stream> OS,
                   EmitterConsumer *BC);
};
} // namespace

namespace {

std::optional<llvm::CodeModel::Model>
getCodeModel(const CodeGenOptions &CodeGenOpts) {
  unsigned CodeModel = llvm::StringSwitch<unsigned>(CodeGenOpts.CodeModel)
                           .Case("tiny", llvm::CodeModel::Tiny)
                           .Case("small", llvm::CodeModel::Small)
                           .Case("kernel", llvm::CodeModel::Kernel)
                           .Case("medium", llvm::CodeModel::Medium)
                           .Case("large", llvm::CodeModel::Large)
                           .Case("default", ~1u)
                           .Default(~0u);
  assert(CodeModel != ~0u && "invalid code model!");
  if (CodeModel == ~1u)
    return std::nullopt;
  return static_cast<llvm::CodeModel::Model>(CodeModel);
}

CodeGenFileType getCodeGenFileType(BackendAction Action) {
  if (Action == Backend_EmitObj)
    return CodeGenFileType::ObjectFile;
  else if (Action == Backend_EmitMCNull)
    return CodeGenFileType::Null;
  else {
    assert(Action == Backend_EmitAssembly && "Invalid action!");
    return CodeGenFileType::AssemblyFile;
  }
}

bool actionRequiresCodeGen(BackendAction Action) {
  return Action != Backend_EmitNothing && Action != Backend_EmitBC &&
         Action != Backend_EmitLL;
}

bool initTargetOptions(DiagnosticsEngine &Diags, llvm::TargetOptions &Options,
                       const CodeGenOptions &CodeGenOpts,
                       const neverc::TargetOptions &TargetOpts,
                       const LangOptions &LangOpts,
                       const HeaderIndexOptions &HSOpts) {
  switch (LangOpts.getThreadModel()) {
  case LangOptions::ThreadModelKind::POSIX:
    Options.ThreadModel = llvm::ThreadModel::POSIX;
    break;
  case LangOptions::ThreadModelKind::Single:
    Options.ThreadModel = llvm::ThreadModel::Single;
    break;
  }

  assert((CodeGenOpts.FloatABI == "soft" || CodeGenOpts.FloatABI == "softfp" ||
          CodeGenOpts.FloatABI == "hard" || CodeGenOpts.FloatABI.empty()) &&
         "Invalid Floating Point ABI!");
  Options.FloatABIType =
      llvm::StringSwitch<llvm::FloatABI::ABIType>(CodeGenOpts.FloatABI)
          .Case("soft", llvm::FloatABI::Soft)
          .Case("softfp", llvm::FloatABI::Soft)
          .Case("hard", llvm::FloatABI::Hard)
          .Default(llvm::FloatABI::Default);

  switch (LangOpts.getDefaultFPContractMode()) {
  case LangOptions::FPM_Off:
    // Preserve any contraction performed by the front-end.  (Strict performs
    // splitting of the muladd intrinsic in the backend.)
    Options.AllowFPOpFusion = llvm::FPOpFusion::Standard;
    break;
  case LangOptions::FPM_On:
  case LangOptions::FPM_FastHonorPragmas:
    Options.AllowFPOpFusion = llvm::FPOpFusion::Standard;
    break;
  case LangOptions::FPM_Fast:
    Options.AllowFPOpFusion = llvm::FPOpFusion::Fast;
    break;
  }

  Options.BinutilsVersion =
      llvm::TargetMachine::parseBinutilsVersion(CodeGenOpts.BinutilsVersion);
  Options.UseInitArray = CodeGenOpts.UseInitArray;
  Options.DisableIntegratedAS = CodeGenOpts.DisableIntegratedAS;
  Options.CompressDebugSections = CodeGenOpts.getCompressDebugSections();
  Options.RelaxELFRelocations = CodeGenOpts.RelaxELFRelocations;

  if (LangOpts.hasSEHExceptions())
    Options.ExceptionModel = llvm::ExceptionHandling::WinEH;
  if (LangOpts.hasDWARFExceptions())
    Options.ExceptionModel = llvm::ExceptionHandling::DwarfCFI;

  Options.NoInfsFPMath = LangOpts.NoHonorInfs;
  Options.NoNaNsFPMath = LangOpts.NoHonorNaNs;
  Options.NoZerosInBSS = CodeGenOpts.NoZeroInitializedInBSS;
  Options.UnsafeFPMath = LangOpts.AllowFPReassoc && LangOpts.AllowRecip &&
                         LangOpts.NoSignedZero && LangOpts.ApproxFunc &&
                         (LangOpts.getDefaultFPContractMode() ==
                              LangOptions::FPModeKind::FPM_Fast ||
                          LangOpts.getDefaultFPContractMode() ==
                              LangOptions::FPModeKind::FPM_FastHonorPragmas);
  Options.ApproxFuncFPMath = LangOpts.ApproxFunc;

  Options.BBSections =
      llvm::StringSwitch<llvm::BasicBlockSection>(CodeGenOpts.BBSections)
          .Case("all", llvm::BasicBlockSection::All)
          .Case("labels", llvm::BasicBlockSection::Labels)
          .StartsWith("list=", llvm::BasicBlockSection::List)
          .Case("none", llvm::BasicBlockSection::None)
          .Default(llvm::BasicBlockSection::None);

  if (Options.BBSections == llvm::BasicBlockSection::List) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> MBOrErr =
        MemoryBuffer::getFile(CodeGenOpts.BBSections.substr(5));
    if (!MBOrErr) {
      Diags.Report(diag::err_fe_unable_to_load_basic_block_sections_file)
          << MBOrErr.getError().message();
      return false;
    }
    Options.BBSectionsFuncListBuf = std::move(*MBOrErr);
  }

  Options.EnableMachineFunctionSplitter = CodeGenOpts.SplitMachineFunctions;
  Options.FunctionSections = CodeGenOpts.FunctionSections;
  Options.DataSections = CodeGenOpts.DataSections;
  Options.UniqueSectionNames = CodeGenOpts.UniqueSectionNames;
  Options.UniqueBasicBlockSectionNames =
      CodeGenOpts.UniqueBasicBlockSectionNames;
  Options.TLSSize = CodeGenOpts.TLSSize;
  Options.EmulatedTLS = CodeGenOpts.EmulatedTLS;
  Options.DebuggerTuning = CodeGenOpts.getDebuggerTuning();
  Options.EmitStackSizeSection = CodeGenOpts.StackSizeSection;
  Options.StackUsageOutput = CodeGenOpts.StackUsageOutput;
  Options.EmitAddrsig = CodeGenOpts.Addrsig;
  Options.ForceDwarfFrameSection = CodeGenOpts.ForceDwarfFrameSection;
  Options.EmitCallSiteInfo = CodeGenOpts.EmitCallSiteInfo;
  Options.LoopAlignment = CodeGenOpts.LoopAlignment;
  Options.DebugStrictDwarf = CodeGenOpts.DebugStrictDwarf;
  Options.ObjectFilenameForDebug = CodeGenOpts.ObjectFilenameForDebug;
  Options.Hotpatch = CodeGenOpts.HotPatch;
  Options.JMCInstrument = CodeGenOpts.JMCInstrument;
  Options.MCOptions.SplitDwarfFile = CodeGenOpts.SplitDwarfFile;
  Options.MCOptions.EmitDwarfUnwind = CodeGenOpts.getEmitDwarfUnwind();
  Options.MCOptions.EmitCompactUnwindNonCanonical =
      CodeGenOpts.EmitCompactUnwindNonCanonical;
  Options.MCOptions.MCRelaxAll = CodeGenOpts.RelaxAll;
  Options.MCOptions.MCSaveTempLabels = CodeGenOpts.SaveTempLabels;
  Options.MCOptions.MCUseDwarfDirectory =
      CodeGenOpts.NoDwarfDirectoryAsm
          ? llvm::MCTargetOptions::DisableDwarfDirectory
          : llvm::MCTargetOptions::EnableDwarfDirectory;
  Options.MCOptions.MCNoExecStack = CodeGenOpts.NoExecStack;
  Options.MCOptions.MCIncrementalLinkerCompatible =
      CodeGenOpts.IncrementalLinkerCompatible;
  Options.MCOptions.MCFatalWarnings = CodeGenOpts.FatalWarnings;
  Options.MCOptions.MCNoWarn = CodeGenOpts.NoWarn;
  Options.MCOptions.AsmVerbose = CodeGenOpts.AsmVerbose;
  Options.MCOptions.Dwarf64 = CodeGenOpts.Dwarf64;
  Options.MCOptions.PreserveAsmComments = CodeGenOpts.PreserveAsmComments;
  Options.MCOptions.ABIName = TargetOpts.ABI;
  for (const auto &Entry : HSOpts.UserEntries)
    if (!Entry.IsFramework &&
        (Entry.Group == frontend::IncludeDirGroup::Quoted ||
         Entry.Group == frontend::IncludeDirGroup::Angled ||
         Entry.Group == frontend::IncludeDirGroup::System))
      Options.MCOptions.IASSearchPaths.push_back(
          Entry.IgnoreSysRoot ? Entry.Path : HSOpts.Sysroot + Entry.Path);
  Options.MCOptions.Argv0 = CodeGenOpts.Argv0;
  Options.MCOptions.CommandLineArgs = CodeGenOpts.CommandLineArgs;
  Options.MCOptions.AsSecureLogFile = CodeGenOpts.AsSecureLogFile;

  return true;
}

void setCommandLineOpts(const CodeGenOptions &CodeGenOpts) {
  llvm::SmallVector<const char *, 16> BackendArgs;
  BackendArgs.push_back("neverc"); // Fake program name.
  if (!CodeGenOpts.DebugPass.empty()) {
    BackendArgs.push_back("-debug-pass");
    BackendArgs.push_back(CodeGenOpts.DebugPass.c_str());
  }
  if (!CodeGenOpts.LimitFloatPrecision.empty()) {
    BackendArgs.push_back("-limit-float-precision");
    BackendArgs.push_back(CodeGenOpts.LimitFloatPrecision.c_str());
  }
  // Check for the default "neverc" invocation that won't set any cl::opt
  // values. Skip trying to parse the command line invocation to avoid the
  // issues described below.
  if (BackendArgs.size() == 1)
    return;
  BackendArgs.push_back(nullptr);
  llvm::cl::ParseCommandLineOptions(BackendArgs.size() - 1, BackendArgs.data());
}

} // namespace

// ===----------------------------------------------------------------------===
// Target machine & codegen passes
// ===----------------------------------------------------------------------===

void GenAssemblyHelper::createTargetMachine(bool MustCreateTM) {
  std::string Error;
  std::string Triple = TheModule->getTargetTriple();
  const llvm::Target *TheTarget = TargetRegistry::lookupTarget(Triple, Error);
  if (!TheTarget) {
    if (MustCreateTM)
      Diags.Report(diag::err_fe_unable_to_create_target) << Error;
    return;
  }

  std::optional<llvm::CodeModel::Model> CM = getCodeModel(CodeGenOpts);
  std::string FeaturesStr =
      llvm::join(TargetOpts.Features.begin(), TargetOpts.Features.end(), ",");
  std::optional<CodeGenOptLevel> OptLevelOrNone =
      CodeGenOpt::getLevel(CodeGenOpts.OptimizationLevel);
  assert(OptLevelOrNone && "Invalid optimization level!");
  CodeGenOptLevel OptLevel = *OptLevelOrNone;

  llvm::TargetOptions Options;
  if (!initTargetOptions(Diags, Options, CodeGenOpts, TargetOpts, LangOpts,
                         HSOpts))
    return;
  TM.reset(TheTarget->createTargetMachine(Triple, TargetOpts.CPU, FeaturesStr,
                                          Options, CM, OptLevel));
  TM->setLargeDataThreshold(CodeGenOpts.LargeDataThreshold);
}

bool GenAssemblyHelper::addEmitPasses(legacy::PassManager &CodeGenPasses,
                                      BackendAction Action,
                                      raw_pwrite_stream &OS,
                                      raw_pwrite_stream *DwoOS) {
  std::unique_ptr<TargetLibraryInfoImpl> TLII(
      llvm::driver::createTLII(TargetTriple, CodeGenOpts.getVecLib()));
  CodeGenPasses.add(new TargetLibraryInfoWrapperPass(*TLII));

  CodeGenFileType CGFT = getCodeGenFileType(Action);

  if (TM->addPassesToEmitFile(CodeGenPasses, OS, DwoOS, CGFT,
                              /*DisableVerify=*/!CodeGenOpts.VerifyModule)) {
    Diags.Report(diag::err_fe_unable_to_interface_with_target);
    return false;
  }

  return true;
}

namespace {
OptimizationLevel mapToLevel(const CodeGenOptions &Opts) {
  switch (Opts.OptimizationLevel) {
  default:
    llvm_unreachable("Invalid optimization level!");

  case 0:
    return OptimizationLevel::O0;

  case 1:
    return OptimizationLevel::O1;

  case 2:
    switch (Opts.OptimizeSize) {
    default:
      llvm_unreachable("Invalid optimization level for size!");

    case 0:
      return OptimizationLevel::O2;

    case 1:
      return OptimizationLevel::Os;

    case 2:
      return OptimizationLevel::Oz;
    }

  case 3:
    return OptimizationLevel::O3;
  }
}
} // namespace

// ===----------------------------------------------------------------------===
// Optimization & codegen pipelines
// ===----------------------------------------------------------------------===

void GenAssemblyHelper::runOptimizationPipeline(
    BackendAction Action, std::unique_ptr<raw_pwrite_stream> &OS,
    EmitterConsumer *BC) {
  std::optional<PGOOptions> PGOOpt;

  PipelineTuningOptions PTO;
  PTO.LoopUnrolling = CodeGenOpts.UnrollLoops;
  // For historical reasons, loop interleaving is set to mirror setting for loop
  // unrolling.
  PTO.LoopInterleaving = CodeGenOpts.UnrollLoops;
  PTO.LoopVectorization = CodeGenOpts.VectorizeLoop;
  PTO.SLPVectorization = CodeGenOpts.VectorizeSLP;
  // Only enable CGProfilePass when using integrated assembler, since
  // non-integrated assemblers don't recognize .cgprofile section.
  PTO.CallGraphProfile = !CodeGenOpts.DisableIntegratedAS;

  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;

  bool DebugPassStructure = CodeGenOpts.DebugPass == "Structure";
  PassInstrumentationCallbacks PIC;
  PrintPassOptions PrintPassOpts;
  PrintPassOpts.Indent = DebugPassStructure;
  PrintPassOpts.SkipAnalyses = DebugPassStructure;
  StandardInstrumentations SI(
      TheModule->getContext(),
      (CodeGenOpts.DebugPassManager || DebugPassStructure),
      CodeGenOpts.VerifyEach, PrintPassOpts);
  SI.registerCallbacks(PIC, &MAM);
  PassBuilder PB(TM.get(), PTO, PGOOpt, &PIC);

  if (LangOpts.BuiltinString) {
    // Register the linker pass in BOTH hosted and shellcode modes.  In
    // hosted mode it performs the bitcode merge fast path; in shellcode
    // mode it auto-detects the legacy source-prelude path and just
    // stamps `kRuntimeFnAttr` so downstream passes (StringRuntimePass,
    // future obfuscation) can uniformly identify runtime functions via
    // `F.hasFnAttribute(kRuntimeFnAttr)` instead of string-prefix matching.
    PB.registerPipelineStartEPCallback(
        [](ModulePassManager &MPM, OptimizationLevel) {
          MPM.addPass(StringRuntimeLinkerPass());
        });
  }

  if (LangOpts.BuiltinMimalloc) {
    PB.registerPipelineStartEPCallback(
        [](ModulePassManager &MPM, OptimizationLevel) {
          MPM.addPass(MimallocRuntimeLinkerPass());
        });
  }

  if (LangOpts.EncryptCallStrings) {
    unsigned MaxLen = LangOpts.EncryptCallStringsMaxLen;
    PB.registerOptimizerLastEPCallback(
        [MaxLen](ModulePassManager &MPM, OptimizationLevel) {
          MPM.addPass(
              neverc::xorstr::EncryptCallStringsPass(MaxLen));
          MPM.addPass(createModuleToFunctionPassAdaptor(
              neverc::xorstr::XorStrCleanupPass()));
        });
  }

  switch (CodeGenOpts.getAssignmentTrackingMode()) {
  case CodeGenOptions::AssignmentTrackingOpts::Forced:
    PB.registerPipelineStartEPCallback(
        [&](ModulePassManager &MPM, OptimizationLevel Level) {
          MPM.addPass(AssignmentTrackingPass());
        });
    break;
  case CodeGenOptions::AssignmentTrackingOpts::Enabled:
    // Disable assignment tracking in LTO builds for now as the performance
    // cost is too high. Disable for LLDB tuning due to llvm.org/PR43126.
    if (!CodeGenOpts.PrepareForLTO &&
        CodeGenOpts.getDebuggerTuning() != llvm::DebuggerKind::LLDB) {
      PB.registerPipelineStartEPCallback(
          [&](ModulePassManager &MPM, OptimizationLevel Level) {
            // Only use assignment tracking if optimisations are enabled.
            if (Level != OptimizationLevel::O0)
              MPM.addPass(AssignmentTrackingPass());
          });
    }
    break;
  case CodeGenOptions::AssignmentTrackingOpts::Disabled:
    break;
  }

  // Enable verify-debuginfo-preserve-each for new PM.
  DebugifyEachInstrumentation Debugify;
  DebugInfoPerPass DebugInfoBeforePass;
  if (CodeGenOpts.EnableDIPreservationVerify) {
    Debugify.setDebugifyMode(DebugifyMode::OriginalDebugInfo);
    Debugify.setDebugInfoBeforePass(DebugInfoBeforePass);

    if (!CodeGenOpts.DIBugsReportFilePath.empty())
      Debugify.setOrigDIVerifyBugsReportFilePath(
          CodeGenOpts.DIBugsReportFilePath);
    Debugify.registerCallbacks(PIC, MAM);
  }
  // Attempt to load pass plugins and register their callbacks with PB.
  for (auto &PluginFN : CodeGenOpts.PassPlugins) {
    auto PassPlugin = PassPlugin::Load(PluginFN);
    if (PassPlugin) {
      PassPlugin->registerPassBuilderCallbacks(PB);
    } else {
      Diags.Report(diag::err_fe_unable_to_load_plugin)
          << PluginFN << toString(PassPlugin.takeError());
    }
  }
  for (const auto &PassCallback : CodeGenOpts.PassBuilderCallbacks)
    PassCallback(PB);
  for (auto PassCallback : ListRegisterPassBuilderCallbacks) {
    PassCallback(PB);
  }

  // Register the target library analysis directly and give it a customized
  // preset TLI.
  std::unique_ptr<TargetLibraryInfoImpl> TLII(
      llvm::driver::createTLII(TargetTriple, CodeGenOpts.getVecLib()));
  FAM.registerPass([&] { return TargetLibraryAnalysis(*TLII); });

  // Register all the basic analyses with the managers.
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  ModulePassManager MPM;
#ifndef NDEBUG
  if (CodeGenOpts.VerifyModule)
    MPM.addPass(VerifierPass());
#endif

  if (!CodeGenOpts.DisableLLVMPasses) {
    // Map our optimization levels into one of the distinct levels used to
    // configure the pipeline.
    OptimizationLevel Level = mapToLevel(CodeGenOpts);

    const bool PrepareForLTO = CodeGenOpts.PrepareForLTO;

    if (PrepareForLTO) {
      MPM.addPass(PB.buildLTOPreLinkDefaultPipeline(Level));
    } else {
      MPM.addPass(PB.buildPerModuleDefaultPipeline(Level));
    }
  }

  // Re-link against any bitcodes supplied via the -mlink-builtin-bitcode
  // option. Some optimizations may generate new function calls that would not
  // have been linked pre-optimization.
  if (ClRelinkBuiltinBitcodePostop)
    MPM.addPass(LinkInModulesPass(BC, false));

  // Add a verifier pass if requested. We don't have to do this if the action
  // requires code generation because there will already be a verifier pass in
  // the code-generation pipeline.
  // Since we already added a verifier pass above, this
  // might even not run the analysis, if previous passes caused no changes.
  if (!actionRequiresCodeGen(Action) && CodeGenOpts.VerifyModule)
    MPM.addPass(VerifierPass());

  // Pre pass
  {
    if (CodeGenOpts.AutoGenerateIR)
      MPM.addPassToFront(IRAutoGeneratorPrePass(true, "IRAutoGeneratorPre"));

    if (CodeGenOpts.AutoGenerateBitcode)
      MPM.addPassToFront(
          BitcodeAutoGeneratorPrePass(true, "BitcodeAutoGeneratorPre"));

    if (CodeGenOpts.OptimizationLevel > 0)
      MPM.addPassToFront(Annotation2MetadataPass());

    if (LangOpts.MicrosoftExt || LangOpts.MSVCCompat)
      MPM.addPassToFront(MSVCMacroRebuildingPass());
  }

  // Post pass
  {
    if (CodeGenOpts.AutoGenerateIR)
      MPM.addPass(IRAutoGeneratorPostPass(true, "IRAutoGeneratorPost"));

    if (CodeGenOpts.AutoGenerateBitcode)
      MPM.addPass(
          BitcodeAutoGeneratorPostPass(true, "BitcodeAutoGeneratorPost"));
  }

  if (Action == Backend_EmitBC || Action == Backend_EmitLL) {
    if (Action == Backend_EmitBC)
      MPM.addPass(
          BitcodeWriterPass(*OS, CodeGenOpts.EmitLLVMUseLists, EmitLTOSummary));
    else
      MPM.addPass(PrintModulePass(*OS, "", CodeGenOpts.EmitLLVMUseLists,
                                  EmitLTOSummary));
  }
  // Print a textual, '-passes=' compatible, representation of pipeline if
  // requested.
  if (PrintPipelinePasses) {
    MPM.printPipeline(outs(), [&PIC](llvm::StringRef ClassName) {
      auto PassName = PIC.getPassNameForClassName(ClassName);
      return PassName.empty() ? ClassName : PassName;
    });
    outs() << "\n";
    return;
  }

  // Now that we have all of the passes ready, run them.
  {
    PrettyStackTraceString CrashInfo("Optimizer");
    llvm::TimeTraceScope TimeScope("Optimizer");
    MPM.run(*TheModule, MAM);
  }
}

void GenAssemblyHelper::runCodegenPipeline(
    BackendAction Action, std::unique_ptr<raw_pwrite_stream> &OS,
    std::unique_ptr<llvm::ToolOutputFile> &DwoOS) {
  // We still use the legacy PM to run the codegen pipeline since the new PM
  // does not work with the codegen pipeline.
  legacy::PassManager CodeGenPasses;

  switch (Action) {
  case Backend_EmitAssembly:
  case Backend_EmitMCNull:
  case Backend_EmitObj:
    CodeGenPasses.add(
        createTargetTransformInfoWrapperPass(getTargetIRAnalysis()));
    if (!CodeGenOpts.SplitDwarfOutput.empty()) {
      DwoOS = openOutputFile(CodeGenOpts.SplitDwarfOutput);
      if (!DwoOS)
        return;
    }
    if (!addEmitPasses(CodeGenPasses, Action, *OS,
                       DwoOS ? &DwoOS->os() : nullptr))
      return;
    break;
  default:
    return;
  }

  if (PrintPipelinePasses) {
    return;
  }

  {
    PrettyStackTraceString CrashInfo("Code generation");
    llvm::TimeTraceScope TimeScope("CodeGenPasses");
    CodeGenPasses.run(*TheModule);
  }
}

void GenAssemblyHelper::genAssembly(BackendAction Action,
                                    std::unique_ptr<raw_pwrite_stream> OS,
                                    EmitterConsumer *BC) {
  TimeRegion Region(CodeGenOpts.TimePasses ? &CodeGenerationTime : nullptr);
  setCommandLineOpts(CodeGenOpts);

  bool RequiresCodeGen = actionRequiresCodeGen(Action);
  createTargetMachine(RequiresCodeGen);

  if (RequiresCodeGen && !TM)
    return;
  if (TM)
    TheModule->setDataLayout(TM->createDataLayout());

  cl::PrintOptionValues();

  // Parallel codegen: 0 = auto-detect, 1 = off, >=2 = explicit N.
  // The threshold/partition logic lives inside runParallelCodeGen() —
  // no need to scan the module here.
  unsigned ParallelN = CodeGenOpts.ParallelCodeGen;
  bool UseParallel = RequiresCodeGen && Action == Backend_EmitObj &&
                     !CodeGenOpts.PrepareForLTO && ParallelN != 1;

  std::unique_ptr<llvm::ToolOutputFile> DwoOS;

  // Run the full optimization pipeline before splitting into partitions.
  // Running function-level optimization in parallel threads risks LLVM
  // global-state contention (PassBuilder pipeline construction, cl::opt
  // reads, ManagedStatic init), so we complete all optimization on the
  // main thread and only parallelize codegen.
  runOptimizationPipeline(Action, OS, BC);

  if (UseParallel) {
    if (!neverc::runParallelCodeGen(*TheModule, *TM, *OS, ParallelN))
      runCodegenPipeline(Action, OS, DwoOS);
  } else {
    runCodegenPipeline(Action, OS, DwoOS);
  }

  if (DwoOS)
    DwoOS->keep();
}

// ===----------------------------------------------------------------------===
// Public API
// ===----------------------------------------------------------------------===

void neverc::genBackendOutput(
    DiagnosticsEngine &Diags, const HeaderIndexOptions &HeaderOpts,
    const CodeGenOptions &CGOpts, const neverc::TargetOptions &TOpts,
    const LangOptions &LOpts, llvm::StringRef TDesc, llvm::Module *M,
    BackendAction Action, llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS,
    std::unique_ptr<raw_pwrite_stream> OS, EmitterConsumer *BC) {

  llvm::TimeTraceScope TimeScope("Backend");

  GenAssemblyHelper AsmHelper(Diags, HeaderOpts, CGOpts, TOpts, LOpts, M, VFS);
  AsmHelper.genAssembly(Action, std::move(OS), BC);

  if (AsmHelper.TM) {
    std::string DLDesc = M->getDataLayout().getStringRepresentation();
    if (DLDesc != TDesc) {
      unsigned DiagID = Diags.getCustomDiagID(
          DiagnosticsEngine::Error, "backend data layout '%0' does not match "
                                    "expected target description '%1'");
      Diags.Report(DiagID) << DLDesc << TDesc;
    }
  }
}
