#include "neverc/Emit/Backend/BackendUtil.h"
#include "Backend/BackendConsumer.h"
#include "Backend/LinkInModulesPass.h"
#include "Backend/Runtime/MimallocRuntimeLinker.h"
#include "Backend/Runtime/NvkKernelRuntimeLinker.h"
#include "Backend/Runtime/StdRuntimeLinker.h"
#include "Backend/Runtime/StringRuntimeLinker.h"
#include "Core/AndroidKernelEmitter.h"
#include "neverc/Compiler/Utils.h"
#include "neverc/DynCode/Pipeline/Pipeline.h"
#include "neverc/Emit/Backend/ParallelCodeGenMerge.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Foundation/Diagnostic/DiagnosticFrontend.h"
#include "neverc/Foundation/LangOpts/CodeGenOptions.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetOptions.h"
#include "neverc/Invoke/LLVMCommandLine.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "neverc/Plugin/Host/CodeGenRoutePlanner.h"
#include "neverc/Plugin/Host/IRPluginBridge.h"
#include "neverc/Plugin/Host/IRPassPlugin.h"
#include "neverc/Plugin/Host/IROptimizationProvider.h"
#include "neverc/Plugin/Host/LLVMComponentProviderBridge.h"
#include "neverc/Plugin/Host/MCEmissionPlan.h"
#include "neverc/Plugin/Host/MIRPassPlugin.h"
#include "neverc/Plugin/Host/CallingConventionMaterialize.h"
#include "neverc/Plugin/Host/ObjectPhaseHooks.h"
#include "neverc/Plugin/Host/ObjectReaderProvider.h"
#include "neverc/Plugin/Host/PluginCodeGenPipeline.h"
#include "neverc/Plugin/Host/PluginCodeGenProvider.h"
#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTargetDescriptor.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Scan/HeaderIndexOptions.h"
#include "neverc/Transforms/StrHash/StrHashFoldPass.h"
#include "neverc/Transforms/XorStr/EncryptCallStringsPass.h"
#include "neverc/Transforms/XorStr/XorStrCleanupPass.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Bitcode/BitcodeAutoGeneratorPass.h"
#include "llvm/Bitcode/BitcodeWriterPass.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/TargetPassConfig.h"
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
#include "llvm/MC/MCContext.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
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
#include "llvm/Transforms/Scalar/SROA.h"
#include "llvm/Transforms/Utils/Debugify.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <memory>
#include <optional>
#include <vector>
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

class CompositeMachinePipelineHooks final : public MachinePipelineHooks {
public:
  explicit CompositeMachinePipelineHooks(
      std::vector<std::shared_ptr<MachinePipelineHooks>> HooksValue)
      : Hooks(std::move(HooksValue)) {}

  void addPasses(TargetPassConfig &TPC,
                 MachinePipelineHookPoint Point) override {
    for (const auto &Hook : Hooks)
      Hook->addPasses(TPC, Point);
  }

private:
  std::vector<std::shared_ptr<MachinePipelineHooks>> Hooks;
};

class GenAssemblyHelper {
  DiagnosticsEngine &Diags;
  const HeaderIndexOptions &HSOpts;
  const CodeGenOptions &CodeGenOpts;
  const neverc::TargetOptions &TargetOpts;
  const LangOptions &LangOpts;
  llvm::Module *TheModule;
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS;
  plugin::PluginTaskContext *PluginTask;
  std::unique_ptr<plugin::PluginIROptimizationProviderRuntime>
      OptimizationRuntime;
  std::shared_ptr<plugin::MIRPassPlan> MachinePasses;
  std::shared_ptr<plugin::PluginCodeGenPipelineRuntime> CodeGenPipeline;
  std::unique_ptr<plugin::LLVMComponentProviderBridge>
      MCComponentProvider;
  std::unique_ptr<plugin::MCEmissionPlan> MCEmissionHooks;
  bool MachinePassesPrepared = false;

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
  bool prepareMachinePasses();

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

  llvm::Error runBuiltinOptimizationPipeline(BackendAction Action,
                                             EmitterConsumer *BC);
  bool runOptimizationPipeline(BackendAction Action,
                               std::unique_ptr<raw_pwrite_stream> &OS,
                               EmitterConsumer *BC);
  void emitOptimizedIR(BackendAction Action,
                       std::unique_ptr<raw_pwrite_stream> &OS);
  void runCodegenPipeline(BackendAction Action,
                          std::unique_ptr<raw_pwrite_stream> &OS,
                          std::unique_ptr<llvm::ToolOutputFile> &DwoOS);
  const plugin::PluginTargetSnapshot::CodeGenEdgeRecord *
  findCoarseObjectEdge() const;
  bool runCoarseObjectCodeGen(raw_pwrite_stream &Output);
  bool runPluginObjectPipeline(ArrayRef<char> Input,
                               raw_pwrite_stream &Output);

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
                    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS,
                    plugin::PluginTaskContext *PluginTaskValue)
      : Diags(_Diags), HSOpts(HeaderIdxOpts), CodeGenOpts(CGOpts),
        TargetOpts(TOpts), LangOpts(LOpts), TheModule(M), VFS(std::move(VFS)),
        PluginTask(PluginTaskValue),
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
  Options.MCOptions.DwarfVersion = CodeGenOpts.DwarfVersion;
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
  neverc::parseLLVMCommandLineOptions(BackendArgs.size() - 1, BackendArgs.data());
}

} // namespace

// ===----------------------------------------------------------------------===
// Target machine & codegen passes
// ===----------------------------------------------------------------------===

void GenAssemblyHelper::createTargetMachine(bool MustCreateTM) {
  std::string ErrorMessage;
  std::string Triple = TheModule->getTargetTriple();
  const llvm::Target *TheTarget = nullptr;
  const plugin::BuiltinTargetRoute *BuiltinRoute = nullptr;
  bool UseBuiltinProvider = false;
  if (PluginTask) {
    auto Snapshot = plugin::findPluginTargetSnapshot(
        PluginTask->processServices(), PluginTask->session().handle());
    UseBuiltinProvider =
        Snapshot &&
        (Snapshot->targetCount() != 0 || Snapshot->mcSchemaCount() != 0 ||
         Snapshot->objectFormatCount() != 0);
  }

  if (UseBuiltinProvider) {
    BuiltinRoute = plugin::findBuiltinTargetRoute(Triple);
    if (!BuiltinRoute) {
      ErrorMessage =
          "selected plugin target has no built-in LLVM target route; "
          "a plugin codegen provider is required";
    } else {
      auto Lookup = plugin::lookupBuiltinLLVMTarget(*BuiltinRoute);
      if (!Lookup)
        ErrorMessage = toString(Lookup.takeError()).str().str();
      else
        TheTarget = *Lookup;
    }
  } else {
    TheTarget = TargetRegistry::lookupTarget(Triple, ErrorMessage);
  }
  if (!TheTarget) {
    if (MustCreateTM)
      Diags.Report(diag::err_fe_unable_to_create_target)
          << ErrorMessage;
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
  if (!TM) {
    if (MustCreateTM)
      Diags.Report(diag::err_fe_unable_to_create_target)
          << "target machine factory returned null";
    return;
  }
  if (BuiltinRoute)
    if (Error E = plugin::validateBuiltinTargetPipeline(
            *BuiltinRoute, *TheModule, *TM, TargetOpts.CPU, FeaturesStr)) {
      Diags.Report(diag::err_fe_unable_to_create_target)
          << toString(std::move(E));
      TM.reset();
      return;
    }
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

  if (!prepareMachinePasses())
    return false;

  auto *MMI = new MachineModuleInfoWrapperPass(
      static_cast<LLVMTargetMachine *>(TM.get()));
  if (MCComponentProvider)
    MMI->getMMI().getContext().setComponentProvider(
        MCComponentProvider.get());
  if (MCEmissionHooks)
    MMI->getMMI().getContext().setEmissionObserver(
        MCEmissionHooks.get());
  if (TM->addPassesToEmitFile(CodeGenPasses, OS, DwoOS, CGFT,
                              /*DisableVerify=*/!CodeGenOpts.VerifyModule,
                              MMI)) {
    Diags.Report(diag::err_fe_unable_to_interface_with_target);
    return false;
  }

  return true;
}

bool GenAssemblyHelper::prepareMachinePasses() {
  if (MachinePassesPrepared)
    return true;
  MachinePassesPrepared = true;

  std::vector<std::shared_ptr<MachinePipelineHooks>> Hooks;
  if (PluginTask) {
    auto Plan = plugin::MIRPassPlan::create(*PluginTask);
    if (!Plan) {
      Diags.Report(diag::err_fe_error_backend) << toString(Plan.takeError());
      return false;
    }
    MachinePasses = std::move(*Plan);
    if (!MachinePasses->empty())
      Hooks.push_back(MachinePasses);
  }

  if (auto DynCodeHooks = dyncode::createDynCodeMachinePipelineHooks(
          dyncode::getCurrentDynCodeOptions()))
    Hooks.push_back(std::move(DynCodeHooks));

  std::shared_ptr<MachinePipelineHooks> Combined;
  if (!Hooks.empty())
    Combined =
        std::make_shared<CompositeMachinePipelineHooks>(std::move(Hooks));
  static_cast<LLVMTargetMachine *>(TM.get())
      ->setMachinePipelineHooks(std::move(Combined));

  if (PluginTask) {
    auto Emission = plugin::MCEmissionPlan::create(*PluginTask);
    if (!Emission) {
      Diags.Report(diag::err_fe_error_backend)
          << toString(Emission.takeError());
      return false;
    }
    if (!(*Emission)->empty())
      MCEmissionHooks = std::move(*Emission);

    auto Snapshot = plugin::findPluginTargetSnapshot(
        PluginTask->processServices(), PluginTask->session().handle());
    if (Snapshot && Snapshot->selectedTarget()) {
      auto Components =
          plugin::LLVMComponentProviderBridge::create(
              *PluginTask, Snapshot);
      if (!Components) {
        Diags.Report(diag::err_fe_error_backend)
            << toString(Components.takeError());
        return false;
      }
      if ((*Components)->hasReplacements())
        MCComponentProvider = std::move(*Components);
      auto Runtime = plugin::PluginCodeGenPipelineRuntime::create(
          *PluginTask, std::move(Snapshot));
      if (!Runtime) {
        Diags.Report(diag::err_fe_error_backend)
            << toString(Runtime.takeError());
        return false;
      }
      CodeGenPipeline = std::move(*Runtime);
      CodeGenPipeline->install(
          *static_cast<LLVMTargetMachine *>(TM.get()),
          CodeGenOpts.VerifyModule);
    }
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

NevercIROptimizationLevel
toPluginOptimizationLevel(OptimizationLevel Level) {
  if (Level == OptimizationLevel::O0)
    return NEVERC_IR_OPTIMIZATION_O0;
  if (Level == OptimizationLevel::O1)
    return NEVERC_IR_OPTIMIZATION_O1;
  if (Level == OptimizationLevel::O3)
    return NEVERC_IR_OPTIMIZATION_O3;
  if (Level == OptimizationLevel::Os)
    return NEVERC_IR_OPTIMIZATION_OS;
  if (Level == OptimizationLevel::Oz)
    return NEVERC_IR_OPTIMIZATION_OZ;
  return NEVERC_IR_OPTIMIZATION_O2;
}
} // namespace

// ===----------------------------------------------------------------------===
// Optimization & codegen pipelines
// ===----------------------------------------------------------------------===

Error GenAssemblyHelper::runBuiltinOptimizationPipeline(
    BackendAction Action, EmitterConsumer *BC) {
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

  std::unique_ptr<plugin::IRPassPlan> PluginPasses;
  if (PluginTask) {
    auto Plan = plugin::IRPassPlan::create(*PluginTask);
    if (!Plan)
      return Plan.takeError();
    PluginPasses = std::move(*Plan);
    if (!PluginPasses->empty()) {
      PB.registerPipelineStartEPCallback(
          [&](ModulePassManager &MPM, OptimizationLevel Level) {
            PluginPasses->addPasses(
                MPM,
                {NEVERC_PHASE_IR_PASS_PIPELINE_START_HIGH,
                 NEVERC_PHASE_IR_PASS_PIPELINE_START_LOW},
                toPluginOptimizationLevel(Level));
          });
      PB.registerOptimizerLastEPCallback(
          [&](ModulePassManager &MPM, OptimizationLevel Level) {
            PluginPasses->addPasses(
                MPM,
                {NEVERC_PHASE_IR_PASS_OPTIMIZER_LAST_HIGH,
                 NEVERC_PHASE_IR_PASS_OPTIMIZER_LAST_LOW},
                toPluginOptimizationLevel(Level));
          });
    }
  }

  if (LangOpts.BuiltinString) {
    bool IsPreLinkStr = CodeGenOpts.PrepareForLTO;
    PB.registerPipelineStartEPCallback(
        [IsPreLinkStr](ModulePassManager &MPM, OptimizationLevel) {
          MPM.addPass(StringRuntimeLinkerPass(IsPreLinkStr));
        });
  }

  if (LangOpts.BuiltinMimalloc) {
    bool IsPreLinkMi = CodeGenOpts.PrepareForLTO;
    PB.registerPipelineStartEPCallback(
        [IsPreLinkMi](ModulePassManager &MPM, OptimizationLevel) {
          MPM.addPass(MimallocRuntimeLinkerPass(IsPreLinkMi));
        });
  }

  if (LangOpts.BuiltinStd) {
    bool IsPreLinkStd = CodeGenOpts.PrepareForLTO;
    PB.registerPipelineStartEPCallback(
        [IsPreLinkStd](ModulePassManager &MPM, OptimizationLevel) {
          MPM.addPass(StdRuntimeLinkerPass(IsPreLinkStd));
        });
  }

  if (CodeGenOpts.AndroidKernelDriverMode) {
    bool IsPreLink = CodeGenOpts.PrepareForLTO;
    PB.registerPipelineStartEPCallback(
        [IsPreLink](ModulePassManager &MPM, OptimizationLevel) {
          MPM.addPass(NvkKernelRuntimeLinkerPass(IsPreLink));
          MPM.addPass(
              neverc::Emit::AndroidKernel::KernelFunctionAttrsPass());
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
  // Set plugin arguments before loading so they're available during registration.
  for (const auto &PassCallback : CodeGenOpts.PassBuilderCallbacks)
    PassCallback(PB);
  dyncode::registerDynCodePasses(PB, dyncode::getCurrentDynCodeOptions());

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
  NevercIROptimizationLevel PluginOptimizationLevel =
      toPluginOptimizationLevel(mapToLevel(CodeGenOpts));
#ifndef NDEBUG
  if (CodeGenOpts.VerifyModule)
    MPM.addPass(VerifierPass());
#endif

  // Pre pass — runs before the optimization pipeline.
  {
    if (LangOpts.MicrosoftExt || LangOpts.MSVCCompat)
      MPM.addPassToFront(MSVCMacroRebuildingPass());

    if (CodeGenOpts.hasDownstreamOptimization())
      MPM.addPassToFront(Annotation2MetadataPass());

    if (CodeGenOpts.AutoGenerateBitcode)
      MPM.addPassToFront(
          BitcodeAutoGeneratorPrePass(true, "BitcodeAutoGeneratorPre"));

    if (CodeGenOpts.AutoGenerateIR)
      MPM.addPassToFront(IRAutoGeneratorPrePass(true, "IRAutoGeneratorPre"));

    if (PluginPasses)
      PluginPasses->addPasses(
          MPM,
          {NEVERC_PHASE_IR_PASS_PRE_OPT_HIGH,
           NEVERC_PHASE_IR_PASS_PRE_OPT_LOW},
          PluginOptimizationLevel);
  }

  if (!CodeGenOpts.DisableLLVMPasses) {
    // Map our optimization levels into one of the distinct levels used to
    // configure the pipeline.
    OptimizationLevel Level = mapToLevel(CodeGenOpts);

    const bool PrepareForLTO = CodeGenOpts.PrepareForLTO;

    if (PrepareForLTO) {
      MPM.addPass(PB.buildLTOPreLinkDefaultPipeline(Level));
      // Auto-LTO runs the frontend at -O0, so each function is alloca-heavy
      // (~3-5x more IR than simplified form).  That inflated IR is then
      // processed by every *serial* LTO stage: bitcode parse, IRMover merge,
      // and inliner body cloning.  Run SROA here, in the parallel per-file
      // frontend, to shrink the IR before it reaches the serial bottleneck.
      if (Level == OptimizationLevel::O0)
        MPM.addPass(
            createModuleToFunctionPassAdaptor(SROAPass(SROAOptions::ModifyCFG)));
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

  // Post pass — runs after the optimization pipeline.
  {
    if (LangOpts.StrHashFold) {
      MPM.addPass(neverc::strhash::StrHashFoldPass());
    }

    if (LangOpts.EncryptCallStrings) {
      MPM.addPass(
          neverc::xorstr::EncryptCallStringsPass(LangOpts.EncryptCallStringsMaxLen));
      MPM.addPass(createModuleToFunctionPassAdaptor(
          neverc::xorstr::XorStrCleanupPass()));
    }

    if (PluginPasses)
      PluginPasses->addPasses(
          MPM,
          {NEVERC_PHASE_IR_PASS_POST_OPT_HIGH,
           NEVERC_PHASE_IR_PASS_POST_OPT_LOW},
          PluginOptimizationLevel);

    if (CodeGenOpts.AutoGenerateIR)
      MPM.addPass(IRAutoGeneratorPostPass(true, "IRAutoGeneratorPost"));

    if (CodeGenOpts.AutoGenerateBitcode)
      MPM.addPass(
          BitcodeAutoGeneratorPostPass(true, "BitcodeAutoGeneratorPost"));
  }

  if (PluginPasses)
    PluginPasses->addPasses(
        MPM,
        {NEVERC_PHASE_IR_PASS_PRE_CODEGEN_HIGH,
         NEVERC_PHASE_IR_PASS_PRE_CODEGEN_LOW},
        PluginOptimizationLevel);

  if (PluginTask)
    MPM.addPass(VerifierPass());

  // Print a textual, '-passes=' compatible, representation of pipeline if
  // requested.
  if (PrintPipelinePasses) {
    MPM.printPipeline(outs(), [&PIC](llvm::StringRef ClassName) {
      auto PassName = PIC.getPassNameForClassName(ClassName);
      return PassName.empty() ? ClassName : PassName;
    });
    outs() << "\n";
    return Error::success();
  }

  // Now that we have all of the passes ready, run them.
  {
    PrettyStackTraceString CrashInfo("Optimizer");
    llvm::TimeTraceScope TimeScope("Optimizer");
    MPM.run(*TheModule, MAM);
  }
  return Error::success();
}

bool GenAssemblyHelper::runOptimizationPipeline(
    BackendAction Action, std::unique_ptr<raw_pwrite_stream> &OS,
    EmitterConsumer *BC) {
  auto RunBuiltin = [this, Action, BC](Module &Candidate) -> Error {
    Module *Previous = TheModule;
    TheModule = &Candidate;
    auto Restore = make_scope_exit([&] { TheModule = Previous; });
    return runBuiltinOptimizationPipeline(Action, BC);
  };

  if (PluginTask) {
    auto Created = plugin::PluginIROptimizationProviderRuntime::create(
        *PluginTask, *TheModule,
        toPluginOptimizationLevel(mapToLevel(CodeGenOpts)),
        CodeGenOpts.DisableLLVMPasses, RunBuiltin);
    if (!Created) {
      Diags.Report(diag::err_fe_error_backend)
          << toString(Created.takeError());
      return false;
    }
    OptimizationRuntime = std::move(*Created);
    if (Error E = OptimizationRuntime->execute()) {
      Diags.Report(diag::err_fe_error_backend) << toString(std::move(E));
      return false;
    }
    TheModule = OptimizationRuntime->module();
    if (!TheModule) {
      Diags.Report(diag::err_fe_error_backend)
          << "IR optimization provider published no module";
      return false;
    }
  } else if (Error E = RunBuiltin(*TheModule)) {
    Diags.Report(diag::err_fe_error_backend) << toString(std::move(E));
    return false;
  }

  if (TM)
    if (Error E = plugin::materializeCallingConventionPlans(
            *TheModule, *TM, PluginTask)) {
      Diags.Report(diag::err_fe_error_backend)
          << toString(std::move(E));
      return false;
    }

  emitOptimizedIR(Action, OS);
  return true;
}

void GenAssemblyHelper::emitOptimizedIR(
    BackendAction Action, std::unique_ptr<raw_pwrite_stream> &OS) {
  if (PrintPipelinePasses || (Action != Backend_EmitBC &&
                              Action != Backend_EmitLL))
    return;
  if (Action == Backend_EmitBC)
    WriteBitcodeToFile(*TheModule, *OS, CodeGenOpts.EmitLLVMUseLists);
  else
    TheModule->print(*OS, nullptr, CodeGenOpts.EmitLLVMUseLists);
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

const plugin::PluginTargetSnapshot::CodeGenEdgeRecord *
GenAssemblyHelper::findCoarseObjectEdge() const {
  if (!PluginTask)
    return nullptr;
  auto Snapshot = plugin::findPluginTargetSnapshot(
      PluginTask->processServices(), PluginTask->session().handle());
  if (!Snapshot)
    return nullptr;
  const auto *Selected = Snapshot->selectedTarget();
  if (!Selected)
    Selected = Snapshot->matchTarget(TargetOpts.Triple);
  if (!Selected)
    Selected = Snapshot->matchTarget(TheModule->getTargetTriple());
  if (!Selected)
    return nullptr;
  for (const auto &Edge : Snapshot->codeGenEdges())
    if (Edge.TargetID.High == Selected->ID.High &&
        Edge.TargetID.Low == Selected->ID.Low &&
        Edge.InputKind == NEVERC_CODEGEN_PRODUCT_IR &&
        Edge.OutputKind == NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE &&
        (Edge.Flags & NEVERC_CODEGEN_EDGE_COARSE) != 0 &&
        Edge.CoarseLower && Edge.VerifyProduct)
      return &Edge;
  return nullptr;
}

bool GenAssemblyHelper::runCoarseObjectCodeGen(
    raw_pwrite_stream &Output) {
  const auto *Edge = findCoarseObjectEdge();
  auto Snapshot = plugin::findPluginTargetSnapshot(
      PluginTask->processServices(), PluginTask->session().handle());
  if (!Edge || !Snapshot)
    return false;
  const auto *Selected = Snapshot->selectedTarget();
  if (!Selected)
    Selected = Snapshot->matchTarget(TargetOpts.Triple);
  if (!Selected)
    Selected = Snapshot->matchTarget(TheModule->getTargetTriple());
  if (!Selected)
    return false;

  const auto &Machine = Selected->Machine;
  plugin::TargetKeyBuilder KeyBuilder;
  KeyBuilder
      .setTargetID(Selected->ID)
      .setTriple(TargetOpts.Triple.empty() ? Machine.RawTriple
                                           : TargetOpts.Triple,
                 Machine.Architecture, Machine.Vendor,
                 Machine.OperatingSystem, Machine.Environment)
      .setCPU(TargetOpts.CPU.empty() ? Machine.DefaultCPU : TargetOpts.CPU,
              TargetOpts.TuneCPU.empty() ? Machine.TuneCPU
                                          : TargetOpts.TuneCPU)
      .setFeatures(TargetOpts.Features)
      .setABI(Selected->DefaultABI)
      .setCallingConvention(Selected->DefaultCallingConvention)
      .setObjectFormat(Selected->DefaultObjectFormatID)
      .setCodeGeneration(Machine.DefaultRelocationModel,
                         Machine.DefaultCodeModel)
      .setExecution(Machine.DefaultExecutionLevel, Machine.PointerWidth,
                    Machine.Endianness)
      .setSchemaDigest(Machine.SchemaDigest);
  auto OwnedKey = KeyBuilder.build();
  if (!OwnedKey) {
    Diags.Report(diag::err_fe_error_backend)
        << toString(OwnedKey.takeError());
    return false;
  }
  NevercTargetKey Target = OwnedKey->view();

  plugin::CodeGenRouteRequest RouteRequest;
  RouteRequest.TargetID = Target.TargetID;
  RouteRequest.InputKind = NEVERC_CODEGEN_PRODUCT_IR;
  RouteRequest.OutputKind = NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE;
  RouteRequest.CompatibilityKey = Edge->CompatibilityKey;
  auto Route =
      plugin::CodeGenRoutePlanner::plan(*Snapshot, RouteRequest);
  if (!Route) {
    Diags.Report(diag::err_fe_error_backend)
        << toString(Route.takeError());
    return false;
  }

  auto Bridge =
      plugin::IRPluginBridge::borrow(*PluginTask, *TheModule);
  if (!Bridge) {
    Diags.Report(diag::err_fe_error_backend)
        << toString(Bridge.takeError());
    return false;
  }
  auto ModuleHandle = (*Bridge)->wrapModule(*TheModule);
  if (!ModuleHandle) {
    Diags.Report(diag::err_fe_error_backend)
        << toString(ModuleHandle.takeError());
    return false;
  }

  plugin::CodeGenExecutionRequest Request;
  Request.TaskContext = PluginTask;
  Request.Task = PluginTask->handle();
  Request.Target = Target;
  Request.Input = *ModuleHandle;
  Request.InputKind = NEVERC_CODEGEN_PRODUCT_IR;
  Request.OutputKind = NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE;
  Request.CompatibilityKey = Edge->CompatibilityKey;
  Request.HasFinalIRProof = true;
  Request.OptimizationLevel =
      CodeGenOpts.OptimizationLevel == 0
          ? NEVERC_CODEGEN_OPT_NONE
          : CodeGenOpts.OptimizationLevel == 1
                ? NEVERC_CODEGEN_OPT_LESS
                : CodeGenOpts.OptimizationLevel == 2
                      ? NEVERC_CODEGEN_OPT_DEFAULT
                      : NEVERC_CODEGEN_OPT_AGGRESSIVE;
  auto Product = plugin::PluginCodeGenProviderRuntime::execute(
      *Route, Request, {});
  if (!Product) {
    Diags.Report(diag::err_fe_error_backend)
        << toString(Product.takeError());
    return false;
  }

  auto Staged = plugin::inspectPluginOutputSeal(
      *PluginTask, Product->Candidate.Artifact);
  if (!Staged) {
    Diags.Report(diag::err_fe_error_backend)
        << toString(Staged.takeError());
    return false;
  }
  if (Staged->State != NEVERC_OUTPUT_FINISHED ||
      Staged->Kind != NEVERC_OUTPUT_MEMORY) {
    Diags.Report(diag::err_fe_error_backend)
        << "coarse object Provider must publish a finished memory output";
    return false;
  }

  NevercOutputSeal Seal{};
  Seal.Header = {sizeof(Seal), NEVERC_IO_API_MAJOR,
                 NEVERC_IO_API_MINOR, 0};
  Seal.Handle = Product->Candidate.Artifact;
  Seal.Kind = Staged->Kind;
  Seal.Size = Staged->Size;
  std::copy(Staged->Digest.begin(), Staged->Digest.end(),
            Seal.Digest);
  NevercObjectLayoutProofInfo LayoutReport{};
  LayoutReport.Header = {sizeof(LayoutReport), NEVERC_OBJECT_API_MAJOR,
                         NEVERC_OBJECT_API_MINOR, 0};
  LayoutReport.GraphGeneration = 1;
  LayoutReport.TargetID = Target.TargetID;
  LayoutReport.FormatID = Target.ObjectFormatID;
  const std::string Provenance =
      "coarse:" + Edge->PluginID + ":" + Edge->ProviderID;
  auto Image = plugin::PluginObjectImage::create(
      *PluginTask, Target.ObjectFormatID, Target.TargetID, 1, Seal,
      Provenance, LayoutReport);
  if (!Image) {
    Diags.Report(diag::err_fe_error_backend)
        << toString(Image.takeError());
    return false;
  }
  auto Pipeline =
      plugin::ObjectPhasePipeline::create(*PluginTask, Snapshot);
  if (!Pipeline) {
    Diags.Report(diag::err_fe_error_backend)
        << toString(Pipeline.takeError());
    return false;
  }
  auto Committed = (*Pipeline)->verifyAndCommitFinished(
      Target, std::shared_ptr<plugin::PluginObjectImage>(
                  std::move(*Image)));
  if (!Committed) {
    Diags.Report(diag::err_fe_error_backend)
        << toString(Committed.takeError());
    return false;
  }
  auto Result = plugin::findPluginMemoryOutput(
      *PluginTask, Staged->Destination);
  if (!Result) {
    Diags.Report(diag::err_fe_error_backend)
        << "coarse object Provider committed no memory output";
    return false;
  }
  Output.write(reinterpret_cast<const char *>(Result->Bytes.data()),
               Result->Bytes.size());
  return true;
}

bool GenAssemblyHelper::runPluginObjectPipeline(
    ArrayRef<char> Input, raw_pwrite_stream &Output) {
  auto Snapshot = plugin::findPluginTargetSnapshot(
      PluginTask->processServices(), PluginTask->session().handle());
  if (!Snapshot) {
    Output.write(Input.data(), Input.size());
    return true;
  }
  auto Pipeline =
      plugin::ObjectPhasePipeline::create(*PluginTask, Snapshot);
  if (!Pipeline) {
    Diags.Report(diag::err_fe_error_backend)
        << toString(Pipeline.takeError());
    return false;
  }
  if (!(*Pipeline)->hasPluginBindings()) {
    Output.write(Input.data(), Input.size());
    return true;
  }

  std::optional<plugin::OwnedTargetKey> BuiltinTargetKey;
  const plugin::OwnedTargetKey *TargetKey = Snapshot->targetKey();
  if (!TargetKey) {
    const plugin::BuiltinTargetRoute *BuiltinRoute =
        plugin::findBuiltinTargetRoute(TheModule->getTargetTriple());
    if (!BuiltinRoute) {
      Output.write(Input.data(), Input.size());
      return true;
    }
    Triple Parsed(TheModule->getTargetTriple());
    plugin::TargetKeyBuilder Builder;
    auto Built = Builder
                     .setTargetID(BuiltinRoute->TargetID)
                     .setTriple(
                         TheModule->getTargetTriple(),
                         Parsed.getArchName().str(),
                         Parsed.getVendorName().str(),
                         Parsed.getOSName().str(),
                         Parsed.getEnvironmentName().str())
                     .setCPU(
                         TargetOpts.CPU.empty()
                             ? BuiltinRoute->DefaultCPU.str()
                             : TargetOpts.CPU,
                         TargetOpts.CPU.empty()
                             ? BuiltinRoute->DefaultCPU.str()
                             : TargetOpts.CPU)
                     .setFeatures(TargetOpts.Features)
                     .setABI(BuiltinRoute->ABIID)
                     .setCallingConvention(
                         {UINT64_C(0x4e43504243430001),
                          BuiltinRoute->TargetID.Low})
                     .setObjectFormat(BuiltinRoute->ObjectFormatID)
                     .setCodeGeneration(
                         NEVERC_TARGET_RELOCATION_PIC,
                         NEVERC_TARGET_CODE_MODEL_SMALL)
                     .setExecution(
                         NEVERC_TARGET_EXECUTION_USER, 64,
                         NEVERC_TARGET_ENDIAN_LITTLE)
                     .setSchemaDigest(std::string(64, '0'))
                     .build();
    if (!Built) {
      Diags.Report(diag::err_fe_error_backend)
          << toString(Built.takeError());
      return false;
    }
    BuiltinTargetKey.emplace(std::move(*Built));
    TargetKey = &*BuiltinTargetKey;
  }

  auto Reader = plugin::ObjectReaderProvider::create(Snapshot);
  if (!Reader) {
    Diags.Report(diag::err_fe_error_backend)
        << toString(Reader.takeError());
    return false;
  }

  NevercTargetKey Target = TargetKey->view();
  std::optional<NevercObjectFormatID> RequiredFormat;
  if (Target.ObjectFormatID.High != 0 || Target.ObjectFormatID.Low != 0)
    RequiredFormat = Target.ObjectFormatID;
  ArrayRef<uint8_t> Bytes(
      reinterpret_cast<const uint8_t *>(Input.data()), Input.size());
  auto Graph = (*Reader)->read(
      *PluginTask, Bytes, "<neverc-native-object>",
      *TargetKey, RequiredFormat);
  if (!Graph) {
    Diags.Report(diag::err_fe_error_backend)
        << toString(Graph.takeError());
    return false;
  }

  std::string LogicalName =
      (Twine("neverc-backend-object-") +
       Twine(PluginTask->handle().Owner) + "-" +
       Twine(PluginTask->handle().Value))
          .str();
  uint64_t Budget =
      std::max<uint64_t>(uint64_t(Input.size()) * 16,
                         UINT64_C(64) * 1024 * 1024);
  auto Image = (*Pipeline)->executeNative(
      **Graph, Bytes,
      plugin::ObjectOutputDestination::memory(LogicalName, Budget));
  if (!Image) {
    Diags.Report(diag::err_fe_error_backend)
        << toString(Image.takeError());
    return false;
  }

  auto Result =
      plugin::findPluginMemoryOutput(*PluginTask, LogicalName);
  if (!Result) {
    Diags.Report(diag::err_fe_error_backend)
        << "object pipeline committed no memory output";
    return false;
  }
  Output.write(reinterpret_cast<const char *>(Result->Bytes.data()),
               Result->Bytes.size());
  return true;
}

void GenAssemblyHelper::genAssembly(BackendAction Action,
                                    std::unique_ptr<raw_pwrite_stream> OS,
                                    EmitterConsumer *BC) {
  TimeRegion Region(CodeGenOpts.TimePasses ? &CodeGenerationTime : nullptr);
  setCommandLineOpts(CodeGenOpts);

  bool RequiresCodeGen = actionRequiresCodeGen(Action);
  const bool UseCoarseObjectProvider =
      RequiresCodeGen && Action == Backend_EmitObj &&
      findCoarseObjectEdge();
  createTargetMachine(RequiresCodeGen && !UseCoarseObjectProvider);

  if (RequiresCodeGen && !TM && !UseCoarseObjectProvider)
    return;
  if (TM)
    TheModule->setDataLayout(TM->createDataLayout());
  if (RequiresCodeGen && !UseCoarseObjectProvider &&
      !prepareMachinePasses())
    return;

  cl::PrintOptionValues();

  // Parallel codegen: 0 = auto-detect, 1 = off, >=2 = explicit N.
  // The threshold/partition logic lives inside runParallelCodeGen() —
  // no need to scan the module here.
  unsigned ParallelN = CodeGenOpts.ParallelCodeGen;
  bool UseParallel = RequiresCodeGen && !UseCoarseObjectProvider &&
                     Action == Backend_EmitObj &&
                     !CodeGenOpts.PrepareForLTO && ParallelN != 1 &&
                     !MCComponentProvider &&
                     !MCEmissionHooks &&
                     (!MachinePasses ||
                      !MachinePasses->requiresSerialCodeGen());

  std::unique_ptr<llvm::ToolOutputFile> DwoOS;

  // Run the full optimization pipeline before splitting into partitions.
  // Running function-level optimization in parallel threads risks LLVM
  // global-state contention (PassBuilder pipeline construction, cl::opt
  // reads, ManagedStatic init), so we complete all optimization on the
  // main thread and only parallelize codegen.
  if (!runOptimizationPipeline(Action, OS, BC))
    return;

  if (UseCoarseObjectProvider) {
    (void)runCoarseObjectCodeGen(*OS);
    return;
  }

  std::unique_ptr<raw_pwrite_stream> FinalOutput;
  SmallVector<char, 0> NativeObject;
  if (Action == Backend_EmitObj && PluginTask) {
    FinalOutput = std::move(OS);
    OS = std::make_unique<raw_svector_ostream>(NativeObject);
  }

  if (UseParallel) {
    if (!neverc::runParallelCodeGen(*TheModule, *TM, *OS, ParallelN))
      runCodegenPipeline(Action, OS, DwoOS);
  } else {
    runCodegenPipeline(Action, OS, DwoOS);
  }

  if (FinalOutput) {
    OS->flush();
    if (!runPluginObjectPipeline(NativeObject, *FinalOutput))
      return;
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
    std::unique_ptr<raw_pwrite_stream> OS, EmitterConsumer *BC,
    plugin::PluginTaskContext *PluginTask) {

  llvm::TimeTraceScope TimeScope("Backend");

  GenAssemblyHelper AsmHelper(Diags, HeaderOpts, CGOpts, TOpts, LOpts, M, VFS,
                              PluginTask);
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
