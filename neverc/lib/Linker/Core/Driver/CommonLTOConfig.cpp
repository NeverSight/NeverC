#include "Linker/Core/Driver/CommonLTOConfig.h"
#include "Linker/Core/Driver/ArgList.h"
#include "Linker/Core/Driver/CodegenFlags.h"
#include "Linker/Core/Driver/Dispatcher.h"
#include "Linker/Core/Runtime/Diagnostic.h"
#include "neverc/Emit/Backend/ParallelCodeGenMerge.h"
#include "neverc/Plugin/PluginLoader.h"
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

lto::Config linker::createLTOConfig(const LinkerDriverConfig &Cfg,
                                    DiagnosticHandlerFunction DiagHandler,
                                    bool EmitAddrsig) {
  lto::Config c;

  // Fast path: build TargetOptions directly from LinkerDriverConfig
  // without reading cl::opt globals.  Only fall back to the cl::opt-based
  // InitTargetOptionsFromCodeGenFlags when user mllvm flags are present
  // (those flags require the cl::opt registry to propagate values).
  if (Cfg.mllvmOpts.empty()) {
    c.Options = TargetOptions();
    c.CodeModel = std::nullopt;
  } else {
    c.Options = codegen::initTargetOptions();
    c.CodeModel = codegen::codeModel();
  }

  c.Options.EmitAddrsig = EmitAddrsig;
  c.Options.FunctionSections = true;
  c.Options.DataSections = true;

  if (Cfg.globalISel >= 0) {
    c.Options.EnableGlobalISel = Cfg.globalISel != 0;
    if (c.Options.EnableGlobalISel)
      c.Options.GlobalISelAbort = GlobalISelAbortMode::Disable;
  }
  if (Cfg.debuggerTuning == 1)
    c.Options.DebuggerTuning = DebuggerKind::GDB;
  else if (Cfg.debuggerTuning == 2)
    c.Options.DebuggerTuning = DebuggerKind::LLDB;
  c.Options.EnableMachineFunctionSplitter = Cfg.splitMachineFunctions;
  c.Options.JMCInstrument = Cfg.jmcInstrument;
  c.Options.EmulatedTLS = Cfg.emulatedTLS;
  c.Options.EmitStackSizeSection = Cfg.stackSizeSection;

  unsigned OptLevel = Cfg.ltoOptLevel >= 0 ? Cfg.ltoOptLevel : 2;
  if (OptLevel > 3)
    error("invalid optimization level for LTO: " + Twine(OptLevel));

  unsigned cgo = Cfg.ltoCGOLevel >= 0 ? static_cast<unsigned>(Cfg.ltoCGOLevel)
                                      : args::getCGOptLevel(OptLevel);
  if (auto L = CodeGenOpt::getLevel(cgo))
    c.CGOptLevel = *L;
  else
    error("invalid codegen optimization level for LTO: " + Twine(cgo));

  c.DisableVerify = true;
  c.DiagHandler = std::move(DiagHandler);
  c.OptLevel = OptLevel;
  c.CPU = Cfg.cpu;

  c.PTO.LoopVectorization = OptLevel > 1;
  c.PTO.SLPVectorization = OptLevel > 1;

  c.ParallelCodeGenHook = neverc::runParallelCodeGen;
  c.ParallelOptCodeGenHook = neverc::runParallelOptAndCodeGen;
  c.LTOParallelOpt = true;

  c.TimeTraceEnabled = Cfg.timeTraceEnabled;
  c.TimeTraceGranularity = Cfg.timeTraceGranularity;

  if (!Cfg.optRemarksFilename.empty())
    c.RemarksFilename = Cfg.optRemarksFilename;
  if (!Cfg.optRemarksPasses.empty())
    c.RemarksPasses = Cfg.optRemarksPasses;
  if (!Cfg.optRemarksFormat.empty())
    c.RemarksFormat = Cfg.optRemarksFormat;
  c.RemarksWithHotness = Cfg.optRemarksWithHotness;
  if (!Cfg.optRemarksHotnessThreshold.empty()) {
    unsigned long long Val;
    if (!StringRef(Cfg.optRemarksHotnessThreshold).getAsInteger(10, Val))
      c.RemarksHotnessThreshold = Val;
  }

  if (!Cfg.nevercPluginPaths.empty()) {
    auto &PL = neverc::plugin::getGlobalPluginLoader();
    for (const auto &Path : Cfg.nevercPluginPaths) {
      std::string Err;
      if (!PL.loadPlugin(Path, Err))
        error("failed to load neverc plugin: " + Err);
    }
    // Wire plugin module passes into the LTO optimization pipeline through the
    // Config hooks.  Keeping core LLVM LTO decoupled from neverc: the backend
    // only sees std::function hooks, never the plugin loader.  The lambdas
    // capture nothing and re-fetch the singleton, so they stay valid for the
    // lifetime of the Config regardless of copies.
    if (PL.hasPlugins()) {
      c.PreOptPassHook = [](ModulePassManager &MPM) {
        neverc::plugin::addPluginModulePasses(
            MPM, NEVERC_HOOK_LTO_PRE_OPT,
            neverc::plugin::getGlobalPluginLoader());
      };
      c.PostOptPassHook = [](ModulePassManager &MPM) {
        neverc::plugin::addPluginModulePasses(
            MPM, NEVERC_HOOK_LTO_POST_OPT,
            neverc::plugin::getGlobalPluginLoader());
      };
    }
  }

  if (!Cfg.ltoBasicBlockSections.empty()) {
    StringRef BBS = Cfg.ltoBasicBlockSections;
    if (BBS == "all")
      c.Options.BBSections = BasicBlockSection::All;
    else if (BBS == "labels")
      c.Options.BBSections = BasicBlockSection::Labels;
    else if (BBS == "none")
      c.Options.BBSections = BasicBlockSection::None;
    else {
      ErrorOr<std::unique_ptr<MemoryBuffer>> MBOrErr =
          MemoryBuffer::getFile(BBS.str());
      if (!MBOrErr)
        error("cannot open " + BBS + ":" + MBOrErr.getError().message());
      else
        c.Options.BBSectionsFuncListBuf = std::move(*MBOrErr);
      c.Options.BBSections = BasicBlockSection::List;
    }
  }
  c.Options.UniqueBasicBlockSectionNames = Cfg.ltoUniqueBasicBlockSectionNames;

  if (Cfg.saveTemps)
    checkError(c.addSaveTemps(Cfg.outputFile + ".",
                              /*UseInputModulePath=*/true));
  return c;
}

void linker::parseMllvmOptions(const LinkerDriverConfig &Cfg) {
  if (Cfg.mllvmOpts.empty()) {
    auto &Opts = cl::getRegisteredOptions();
    if (auto *O = Opts.lookup("enable-linkonceodr-outlining"))
      O->addOccurrence(0, O->ArgStr, "");
    return;
  }

  // Slow path: register codegen flags so cl::Parse can accept them.
  llvm::codegen::RegisterCodeGenFlags Flags;

  SmallVector<const char *, 16> Argv;
  Argv.push_back("neverc");
  Argv.push_back("-enable-linkonceodr-outlining");
  for (const auto &Opt : Cfg.mllvmOpts)
    Argv.push_back(Opt.c_str());
  cl::ResetAllOptionOccurrences();
  cl::ParseCommandLineOptions(Argv.size(), Argv.data());
}
