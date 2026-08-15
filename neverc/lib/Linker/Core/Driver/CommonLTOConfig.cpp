#include "Linker/Core/Driver/CommonLTOConfig.h"
#include "Linker/Core/Driver/ArgList.h"
#include "Linker/Core/Driver/CodegenFlags.h"
#include "Linker/Core/Driver/Dispatcher.h"
#include "Linker/Core/Driver/LTOCache.h"
#include "Linker/Core/Runtime/Diagnostic.h"
#include "neverc/Emit/AndroidKernelKCFI.h"
#include "neverc/Emit/Backend/ParallelCodeGenMerge.h"
#include "neverc/Emit/NvkKernelRuntimeLinker.h"
#include "neverc/Foundation/AndroidKernelRuntimeContract.h"
#include "neverc/Plugin/Host/IROptimizationProvider.h"
#include "neverc/Plugin/Host/IRPassPlugin.h"
#include "neverc/Plugin/Host/MIRPassPlugin.h"
#include "neverc/Plugin/Host/PluginLLVMOptionSnapshot.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Transforms/XorStr/EncryptCallStringsPass.h"
#include "neverc/Transforms/XorStr/XorStrCleanupPass.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include <mutex>
#include <optional>

using namespace llvm;

// Keyword modes of -lto-basic-block-sections; every other non-empty value
// names a function-list file.  Single definition point for the keyword
// strings, shared with ltoBasicBlockSectionsIsListFile().
static std::optional<BasicBlockSection> bbSectionsKeywordMode(StringRef BBS) {
  return StringSwitch<std::optional<BasicBlockSection>>(BBS)
      .Case("all", BasicBlockSection::All)
      .Case("labels", BasicBlockSection::Labels)
      .Case("none", BasicBlockSection::None)
      .Default(std::nullopt);
}

namespace {

struct LTOPluginContext {
  std::shared_ptr<neverc::plugin::PluginTaskContext> Task;
  std::shared_ptr<neverc::plugin::IRPassPlan> IRPasses;
  std::shared_ptr<neverc::plugin::MIRPassPlan> MIRPasses;
  std::shared_ptr<neverc::ParallelOptimizationHooks> ParallelHooks;
  std::mutex FinishMutex;
  bool Finished = false;

  void finish() {
    std::lock_guard<std::mutex> Lock(FinishMutex);
    if (Finished)
      return;
    Finished = true;
    IRPasses.reset();
    if (Task) {
      if (Error E = Task->end())
        linker::error("failed to end LTO plugin task: " +
                      toString(std::move(E)));
      Task.reset();
    }
  }
};

NevercIROptimizationLevel pluginOptimizationLevel(unsigned Level) {
  switch (Level) {
  case 0:
    return NEVERC_IR_OPTIMIZATION_O0;
  case 1:
    return NEVERC_IR_OPTIMIZATION_O1;
  case 2:
    return NEVERC_IR_OPTIMIZATION_O2;
  default:
    return NEVERC_IR_OPTIMIZATION_O3;
  }
}

} // namespace

bool linker::ltoBasicBlockSectionsIsListFile(StringRef BBS) {
  return !BBS.empty() && !bbSectionsKeywordMode(BBS).has_value();
}

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
  // Emit static constructors/destructors into .init_array/.fini_array rather
  // than the legacy .ctors/.dtors.  TargetOptions defaults UseInitArray to
  // false, but modern crt startup (glibc, musl) only walks .init_array, so
  // without this LTO-compiled __attribute__((constructor/destructor)) routines
  // are silently dropped into an ignored .ctors section and never run.  The
  // non-LTO backend already sets this from CodeGenOpts.UseInitArray; mirror it
  // here.  Ignored by the MachO/COFF backends, so it is safe to set always.
  c.Options.UseInitArray = true;

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
  if (Cfg.androidKernelModule) {
    // Embedded runtime bitcode is deliberately stripped of build-host target
    // attributes before it is linked into the consumer module.  Restore the
    // AArch64 kernel code-generation contract at the final LTO boundary: SIMD
    // state is unavailable in ordinary kernel C paths and x18 is reserved.
    neverc::AndroidKernelRuntimeContract::forEachRequiredAArch64Feature(
        [&](StringRef Feature) { c.MAttrs.push_back(Feature.str()); });
  }

  // An Android kernel link may consume precompiled full-LTO inputs.  If none
  // of them were compiled in Android kernel mode, the merged module has no
  // profile flag and the otherwise idempotent KCFI finalizer would treat it as
  // an unrelated module.  Reject that boundary explicitly instead of silently
  // emitting callbacks without loader-visible type prefixes.
  if (Cfg.androidKernelModule) {
    c.PreLinkModuleHook = [](const Module &M) -> Error {
      if (neverc::Emit::AndroidKernel::getContract(M))
        return Error::success();
      return createStringError(
          inconvertibleErrorCode(),
          "Android kernel LTO input is missing the profile contract; "
          "recompile it with -fandroid-kernel-driver-mode");
    };
    c.PreOptModuleHook = [](unsigned, const Module &M) {
      if (neverc::Emit::AndroidKernel::getContract(M))
        return true;
      linker::error("Android kernel LTO input is missing the profile contract; "
                    "recompile it with -fandroid-kernel-driver-mode");
      return false;
    };
  }

  c.PTO.LoopVectorization = OptLevel > 1;
  c.PTO.SLPVectorization = OptLevel > 1;

  std::shared_ptr<LTOPluginContext> PluginContext;
  if (Cfg.pluginSession) {
    auto Task = Cfg.pluginSession->createTask(NEVERC_TASK_LTO, Cfg.pluginTask);
    if (!Task) {
      error("failed to create LTO plugin task: " + toString(Task.takeError()));
    } else {
      PluginContext = std::make_shared<LTOPluginContext>();
      PluginContext->Task =
          std::shared_ptr<neverc::plugin::PluginTaskContext>(std::move(*Task));

      auto IRPlan = neverc::plugin::IRPassPlan::create(*PluginContext->Task);
      if (!IRPlan) {
        error("failed to create LTO IR pass plan: " +
              toString(IRPlan.takeError()));
      } else {
        PluginContext->IRPasses =
            std::shared_ptr<neverc::plugin::IRPassPlan>(std::move(*IRPlan));
      }

      auto MIRPlan = neverc::plugin::MIRPassPlan::create(*PluginContext->Task);
      if (!MIRPlan) {
        error("failed to create LTO MIR pass plan: " +
              toString(MIRPlan.takeError()));
      } else {
        PluginContext->MIRPasses = std::move(*MIRPlan);
        if (!PluginContext->MIRPasses->empty())
          c.MachinePassHooks = PluginContext->MIRPasses;
      }
      c.HostContext = PluginContext;
      c.ModuleOptimizeHook = [PluginContext, OptLevel,
                              AndroidKernelModule = Cfg.androidKernelModule,
                              XorStrKeySeed = Cfg.xorStrKeySeed,
                              EncryptCallStrings = Cfg.encryptCallStrings,
                              EncryptCallStringsMaxLen =
                                  Cfg.encryptCallStringsMaxLen](
                                 std::unique_ptr<Module> &ModuleValue,
                                 bool &SkipBuiltin) {
        // Seal inputs before a replacement provider can erase literal
        // provenance, and materialize volatile wipes before it can discard
        // the buffer metadata used to discover them.  The mandatory tail
        // below remains necessary for literals introduced by the provider.
        if (EncryptCallStrings) {
          ModuleAnalysisManager MAM;
          (void)neverc::xorstr::EncryptCallStringsPass(EncryptCallStringsMaxLen,
                                                       XorStrKeySeed)
              .run(*ModuleValue, MAM);
        }
        FunctionAnalysisManager FAM;
        for (Function &F : *ModuleValue)
          if (!F.isDeclaration())
            (void)neverc::xorstr::XorStrCleanupPass().run(F, FAM);

        const std::optional<neverc::Emit::AndroidKernel::Contract>
            RequiredAndroidContract =
                neverc::Emit::AndroidKernel::getContract(*ModuleValue);
        auto Runtime =
            neverc::plugin::PluginIROptimizationProviderRuntime::create(
                *PluginContext->Task, *ModuleValue, OptLevel,
                /*DisableLLVMPasses=*/false,
                [](Module &) { return Error::success(); });
        if (!Runtime) {
          linker::error("failed to create LTO optimization transition: " +
                        toString(Runtime.takeError()));
          return false;
        }
        if (Error E = (*Runtime)->execute()) {
          linker::error("LTO optimization transition failed: " +
                        toString(std::move(E)));
          return false;
        }
        SkipBuiltin = !(*Runtime)->ranBuiltinPipeline();
        if ((*Runtime)->ownsModule())
          ModuleValue = (*Runtime)->releaseOwnedModule();
        if (!ModuleValue)
          return false;
        if (RequiredAndroidContract) {
          const std::optional<neverc::Emit::AndroidKernel::Contract>
              PublishedContract =
                  neverc::Emit::AndroidKernel::getContract(*ModuleValue);
          if (!PublishedContract ||
              *PublishedContract != *RequiredAndroidContract) {
            linker::error("LTO optimization provider dropped or changed the "
                          "Android kernel profile contract");
            return false;
          }
        }
        // A replacement provider can introduce a fresh NVK declaration
        // after the frontend and the ordinary LTO pipeline have already
        // linked runtimes.  Materialize it from the module the provider
        // actually published, before either builtin optimization or the
        // provider-owned native boundary proceeds.
        if (AndroidKernelModule) {
          ModuleAnalysisManager MAM;
          (void)neverc::NvkKernelRuntimeLinkerPass(/*PreLink=*/false)
              .run(*ModuleValue, MAM);
          (void)neverc::Emit::AndroidKernel::KernelFunctionAttrsPass().run(
              *ModuleValue, MAM);
        }
        // A provider that owns the complete optimization transition asks
        // LTO to bypass opt(), which also bypasses PostOptPassHook.  KCFI
        // prefix materialization is a code-generation invariant, so seal
        // the provider's final module here on that path.
        if (SkipBuiltin) {
          ModuleAnalysisManager MAM;
          if (EncryptCallStrings)
            (void)neverc::xorstr::EncryptCallStringsPass(
                EncryptCallStringsMaxLen, XorStrKeySeed)
                .run(*ModuleValue, MAM);
          FunctionAnalysisManager FAM;
          for (Function &F : *ModuleValue)
            if (!F.isDeclaration())
              (void)neverc::xorstr::XorStrCleanupPass().run(F, FAM);
          (void)neverc::xorstr::FinalizeXorStrPass(XorStrKeySeed)
              .run(*ModuleValue, MAM);
          neverc::Emit::AndroidKernel::finalizeKCFIPrefixes(*ModuleValue);
        }
        return true;
      };
      c.BackendDoneHook = [PluginContext] { PluginContext->finish(); };
      c.DisableVerify = false;
    }
  }

  NevercIROptimizationLevel PluginLevel = pluginOptimizationLevel(OptLevel);
  const bool HasIRPluginPasses = PluginContext && PluginContext->IRPasses &&
                                 !PluginContext->IRPasses->empty();
  auto AddNvkWholeModuleLowering =
      [AndroidKernelModule = Cfg.androidKernelModule](ModulePassManager &MPM) {
        if (!AndroidKernelModule)
          return;
        MPM.addPass(neverc::NvkKernelRuntimeLinkerPass(/*PreLink=*/false));
        MPM.addPass(neverc::Emit::AndroidKernel::KernelFunctionAttrsPass());
      };
  auto AddPostOptimizationFinalPasses =
      [AndroidKernelModule = Cfg.androidKernelModule,
       XorStrKeySeed = Cfg.xorStrKeySeed,
       EncryptCallStrings = Cfg.encryptCallStrings,
       EncryptCallStringsMaxLen =
           Cfg.encryptCallStringsMaxLen](ModulePassManager &MPM) {
        // NVK runtime materialization is intentionally absent here: it runs
        // once on the intact LTO module before partitioning.  The remaining
        // passes are valid on ordinary PCG ownership slices when no external IR
        // plugin is present.
        if (AndroidKernelModule)
          MPM.addPass(neverc::Emit::AndroidKernel::KernelFunctionAttrsPass());
        if (EncryptCallStrings)
          MPM.addPass(neverc::xorstr::EncryptCallStringsPass(
              EncryptCallStringsMaxLen, XorStrKeySeed));
        MPM.addPass(createModuleToFunctionPassAdaptor(
            neverc::xorstr::XorStrCleanupPass()));
        MPM.addPass(neverc::xorstr::FinalizeXorStrPass(XorStrKeySeed));
        MPM.addPass(neverc::Emit::AndroidKernel::FinalizeKCFIPrefixesPass());
      };
  c.PreOptPassHook = AddNvkWholeModuleLowering;
  c.PostOptPassHook = AddPostOptimizationFinalPasses;

  auto ParallelHooks = std::make_shared<neverc::ParallelOptimizationHooks>();
  ParallelHooks->PostOpt = AddPostOptimizationFinalPasses;
  if (HasIRPluginPasses) {
    neverc::plugin::IRPassPlan *IRPasses = PluginContext->IRPasses.get();
    auto AddPreOpt = [IRPasses, PluginLevel](ModulePassManager &MPM) {
      IRPasses->addPasses(
          MPM,
          {NEVERC_PHASE_IR_PASS_PRE_OPT_HIGH, NEVERC_PHASE_IR_PASS_PRE_OPT_LOW},
          PluginLevel);
      IRPasses->addPasses(MPM,
                          {NEVERC_PHASE_IR_PASS_PIPELINE_START_HIGH,
                           NEVERC_PHASE_IR_PASS_PIPELINE_START_LOW},
                          PluginLevel);
    };
    auto AddOptimizerLast = [IRPasses, PluginLevel](ModulePassManager &MPM) {
      IRPasses->addPasses(MPM,
                          {NEVERC_PHASE_IR_PASS_OPTIMIZER_LAST_HIGH,
                           NEVERC_PHASE_IR_PASS_OPTIMIZER_LAST_LOW},
                          PluginLevel);
    };
    auto AddPostOpt = [IRPasses, PluginLevel](ModulePassManager &MPM) {
      IRPasses->addPasses(MPM,
                          {NEVERC_PHASE_IR_PASS_POST_OPT_HIGH,
                           NEVERC_PHASE_IR_PASS_POST_OPT_LOW},
                          PluginLevel);
      IRPasses->addPasses(MPM,
                          {NEVERC_PHASE_IR_PASS_PRE_CODEGEN_HIGH,
                           NEVERC_PHASE_IR_PASS_PRE_CODEGEN_LOW},
                          PluginLevel);
    };
    c.PreOptPassHook = [AddPreOpt,
                        AddNvkWholeModuleLowering](ModulePassManager &MPM) {
      AddPreOpt(MPM);
      // Semantic lowering stays on the intact module.  This also catches NVK
      // references introduced by PRE_OPT / PIPELINE_START plugin passes.
      AddNvkWholeModuleLowering(MPM);
    };
    c.PostOptPassHook =
        [AddOptimizerLast, AddPostOpt, AddNvkWholeModuleLowering,
         AddPostOptimizationFinalPasses](ModulePassManager &MPM) {
          AddOptimizerLast(MPM);
          AddPostOpt(MPM);
          // POST_OPT / PRE_CODEGEN plugins may introduce fresh runtime
          // references. This second invocation is a no-op unless they did;
          // materialization is still performed once for each newly published
          // definition graph.
          AddNvkWholeModuleLowering(MPM);
          AddPostOptimizationFinalPasses(MPM);
        };
    ParallelHooks->WholeModulePostOpt =
        [AddOptimizerLast, AddPostOpt, AddNvkWholeModuleLowering,
         AddPostOptimizationFinalPasses](ModulePassManager &MPM) {
          // All third-party IR passes execute against a complete module.  Even
          // a function- or loop-level pass may create module-owned helpers,
          // aliases, globals, or registration tables, so replaying it
          // independently in PCG partitions is not a sound isolation boundary.
          AddOptimizerLast(MPM);
          AddPostOpt(MPM);
          AddNvkWholeModuleLowering(MPM);
          AddPostOptimizationFinalPasses(MPM);
        };
    // No external IR callback or module-owning tail may execute in a PCG
    // partition.  The callback above is the sole post-optimization boundary.
    ParallelHooks->PostOpt = {};
    PluginContext->ParallelHooks = ParallelHooks;
  }

  // Per-partition object cache: with stable partition assignment, an
  // incremental relink re-runs optimization + codegen only for the
  // partitions whose post-IPO bitcode actually changed.  The hooks own
  // the storage and the configuration digest; the partitioner only sees
  // content-addressed Lookup/Store callbacks.
  std::shared_ptr<neverc::PartitionCacheHooks> pcgCache;
  bool HasMutatingPluginPasses =
      PluginContext &&
      (HasIRPluginPasses ||
       (PluginContext->MIRPasses && !PluginContext->MIRPasses->empty()));
  if (!HasMutatingPluginPasses && ltoPartitionCacheUsable(Cfg)) {
    pcgCache = std::make_shared<neverc::PartitionCacheHooks>();
    pcgCache->BypassForUnseededXorStr = Cfg.xorStrKeySeed == 0;
    pcgCache->AutomaticXorStrEnabled = Cfg.encryptCallStrings;
    pcgCache->Lookup = [salt = ltoPartitionCacheSalt(Cfg, EmitAddrsig)](
                           StringRef pipeTag, StringRef bitcode,
                           std::string &keyOut, SmallVectorImpl<char> &obj) {
      return ltoPartitionCacheLookup(salt, pipeTag, bitcode, keyOut, obj);
    };
    pcgCache->Store = ltoPartitionCacheStore;
  }
  c.ParallelCodeGenHook = [pcgCache,
                           PluginContext](Module &M, TargetMachine &TM,
                                          raw_pwrite_stream &OS, unsigned NP) {
    if (PluginContext && PluginContext->MIRPasses &&
        PluginContext->MIRPasses->requiresSerialCodeGen())
      return false;
    return neverc::runParallelCodeGen(M, TM, neverc::ParallelCodeGenOutputs{OS},
                                      NP, pcgCache.get());
  };
  c.ParallelOptCodeGenHook = [pcgCache, PluginContext,
                              ParallelHooks](Module &M, TargetMachine &TM,
                                             raw_pwrite_stream &OS, unsigned NP,
                                             unsigned OL) {
    if (PluginContext && PluginContext->MIRPasses &&
        PluginContext->MIRPasses->requiresSerialCodeGen())
      return false;
    const neverc::ParallelOptimizationHooks *Hooks = ParallelHooks.get();
    return neverc::runParallelOptAndCodeGen(M, TM,
                                            neverc::ParallelCodeGenOutputs{OS},
                                            NP, OL, pcgCache.get(), Hooks);
  };
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

  if (!Cfg.ltoBasicBlockSections.empty()) {
    StringRef BBS = Cfg.ltoBasicBlockSections;
    if (std::optional<BasicBlockSection> Mode = bbSectionsKeywordMode(BBS))
      c.Options.BBSections = *Mode;
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
  neverc::plugin::PluginLLVMOptionExclusiveLease Lock(
      neverc::plugin::pluginLLVMOptionGate());
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
  llvm::cl::ResetAllOptionOccurrences();
  llvm::cl::ParseCommandLineOptions(Argv.size(), Argv.data());
}
