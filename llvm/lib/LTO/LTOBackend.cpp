//===-LTOBackend.cpp - LLVM Link Time Optimizer Backend -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the "backend" phase of LTO, i.e. it performs
// optimization and code generation on a loaded module. It is generally used
// internally by the LTO class but can also be used independently.
//
//===----------------------------------------------------------------------===//

#include "llvm/LTO/LTOBackend.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/ModuleSummaryAnalysis.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/LTO/LTO.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/Transforms/IPO/ConstantMerge.h"
#include "llvm/Transforms/IPO/DeadArgumentElimination.h"
#include "llvm/Transforms/IPO/GlobalDCE.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include <optional>

using namespace llvm;
using namespace lto;

// Merged-module size (defined functions) at which the LTO ParallelOpt serial
// phase switches from the CGSCC inliner to the flat module inliner.  See the
// comment in runNewPMPasses.  0 disables the module inliner entirely.
//
// The default is anchored on two measured data points:
//  - Redis 7.4.2 (real project, 4649 defined functions, benign call graph):
//    the CGSCC inliner is not a bottleneck there (~0.5s of an 8s link) and
//    produces a 5% smaller binary than the module inliner, so real projects
//    of this size must stay below the threshold.
//  - A synthetic 8000-function project with a deep call cycle: the CGSCC
//    inliner's incremental call-graph maintenance explodes superlinearly
//    (86s vs 2.9s link), so huge modules must be above it.
// CGSCC blowup is really a property of pathological call-graph shape, not
// function count alone; the count is a cheap proxy.  Projects that hit the
// blowup below the threshold can lower it via
// -mllvm -neverc-module-inliner-threshold=N on the link (the driver
// forwards -mllvm to the link job); size-sensitive huge projects can raise
// it or pass 0.
static cl::opt<unsigned> NevercModuleInlinerThreshold(
    "neverc-module-inliner-threshold", cl::init(6000), cl::Hidden,
    cl::desc("Number of defined functions in the merged LTO module above "
             "which the module inliner replaces the CGSCC inliner "
             "(0 = never)"));

// Auto-LTO compiles the frontend at -O0 (with SROA only), producing IR
// whose functions appear smaller to the inliner cost model than the same
// functions compiled at -O2 with full loop rotation/unrolling.  The default
// threshold (225) therefore over-inlines, bloating __text and hurting
// icache utilisation on real projects.
//
// Measured on Redis 7.4.2 (4649 functions, -j12) vs homebrew clang 22.1.6
// -flto=full, sweeping thresholds 100/150/200/225:
//   t=100: __text -22% vs c22, but SET/GET p50 +12% slower (under-inline)
//   t=150: __text -14% vs c22, avg p50 0.348ms (c22 = 0.351ms) — best
//   t=200: __text -8% vs c22, similar speed but more icache pressure
//   t=225: __text -1% vs c22, no speed gain over t=150 despite 15% bigger
// t=150 maximises the speed/size Pareto front: equal or faster than
// clang-22 full LTO at 14% smaller code.
// 0 = use the standard opt-level default (225 at O2).
static cl::opt<int> NevercAutoLTOInlineThreshold(
    "neverc-auto-lto-inline-threshold", cl::init(150), cl::Hidden,
    cl::desc("Inliner threshold for the auto-LTO serial phase "
             "(0 = use the standard opt-level default)"));

// Whether the LTO ParallelOpt serial phase uses the trimmed per-SCC
// function simplification (SROA + InstCombine + LoopRotate/LICM + ADCE)
// instead of the full O2/O3 function simplification pipeline.
//
// The lite pipeline must keep LoopRotate + LICM: nothing downstream re-runs
// LICM before the vectorizers, and without scalar promotion loops reach
// LoopVectorize/SLP with loop-invariant loads/stores still in the body,
// which defeats vectorization outright (2.2x runtime regression on
// FP-reduction kernels vs explicit -flto=full -O2).
//
// Measured on Redis 7.4.2 (4649 functions, full cold build, -j12):
//   lite+LICM: 6.1s link, binary 3,598,960, vector code = full pipeline
//   full:      8.0s link, binary 3,548,864 (-1.4%, extra Reassociate/GVN/
//              loop-idiom simplification), same kernel runtime
// The lite pipeline is the default; pass
// -mllvm -neverc-inliner-lite-fsimpl=0 on the link to run the full
// pipeline when the last percent of binary size matters more than link
// time.
static cl::opt<bool> NevercInlinerLiteFSimplOpt(
    "neverc-inliner-lite-fsimpl", cl::init(true), cl::Hidden,
    cl::desc("Use the trimmed per-SCC function simplification pipeline in "
             "the LTO ParallelOpt serial phase (0 = full O2 function "
             "simplification: slower link, slightly smaller binary)"));

#define DEBUG_TYPE "lto-backend"

[[noreturn]] static void reportOpenError(StringRef Path, Twine Msg) {
  errs() << "failed to open " << Path << ": " << Msg << '\n';
  errs().flush();
  exit(1);
}

Error Config::addSaveTemps(std::string OutputFileName, bool UseInputModulePath,
                           const DenseSet<StringRef> &SaveTempsArgs) {
  ShouldDiscardValueNames = false;

  std::error_code EC;
  if (SaveTempsArgs.empty() || SaveTempsArgs.contains("resolution")) {
    ResolutionFile =
        std::make_unique<raw_fd_ostream>(OutputFileName + "resolution.txt", EC,
                                         sys::fs::OpenFlags::OF_TextWithCRLF);
    if (EC) {
      ResolutionFile.reset();
      return errorCodeToError(EC);
    }
  }

  auto setHook = [&](std::string PathSuffix, ModuleHookFn &Hook) {
    // Keep track of the hook provided by the linker, which also needs to run.
    ModuleHookFn LinkerHook = Hook;
    Hook = [=](unsigned Task, const Module &M) {
      // If the linker's hook returned false, we need to pass that result
      // through.
      if (LinkerHook && !LinkerHook(Task, M))
        return false;

      std::string PathPrefix;
      // Emit to a file named from the provided OutputFileName with the
      // Task ID appended.
      if (M.getModuleIdentifier() == "ld-temp.o" || !UseInputModulePath) {
        PathPrefix = OutputFileName;
        if (Task != (unsigned)-1)
          PathPrefix += utostr(Task) + ".";
      } else
        PathPrefix = M.getModuleIdentifier() + ".";
      std::string Path = PathPrefix + PathSuffix + ".bc";
      std::error_code EC;
      raw_fd_ostream OS(Path, EC, sys::fs::OpenFlags::OF_None);
      // Because -save-temps is a debugging feature, we report the error
      // directly and exit.
      if (EC)
        reportOpenError(Path, EC.message());
      WriteBitcodeToFile(M, OS, /*ShouldPreserveUseListOrder=*/false);
      return true;
    };
  };

  auto SaveCombinedIndex =
      [=](const ModuleSummaryIndex &Index,
          const DenseSet<GlobalValue::GUID> &GUIDPreservedSymbols) {
        std::string Path = OutputFileName + "index.bc";
        std::error_code EC;
        raw_fd_ostream OS(Path, EC, sys::fs::OpenFlags::OF_None);
        // Because -save-temps is a debugging feature, we report the error
        // directly and exit.
        if (EC)
          reportOpenError(Path, EC.message());
        writeIndexToFile(Index, OS);

        Path = OutputFileName + "index.dot";
        raw_fd_ostream OSDot(Path, EC, sys::fs::OpenFlags::OF_None);
        if (EC)
          reportOpenError(Path, EC.message());
        Index.exportToDot(OSDot, GUIDPreservedSymbols);
        return true;
      };

  if (SaveTempsArgs.empty()) {
    setHook("0.preopt", PreOptModuleHook);
    setHook("2.internalize", PostInternalizeModuleHook);
    setHook("4.opt", PostOptModuleHook);
    setHook("5.precodegen", PreCodeGenModuleHook);
    CombinedIndexHook = SaveCombinedIndex;
  } else {
    if (SaveTempsArgs.contains("preopt"))
      setHook("0.preopt", PreOptModuleHook);
    if (SaveTempsArgs.contains("internalize"))
      setHook("2.internalize", PostInternalizeModuleHook);
    if (SaveTempsArgs.contains("opt"))
      setHook("4.opt", PostOptModuleHook);
    if (SaveTempsArgs.contains("precodegen"))
      setHook("5.precodegen", PreCodeGenModuleHook);
    if (SaveTempsArgs.contains("combinedindex"))
      CombinedIndexHook = SaveCombinedIndex;
  }

  return Error::success();
}

static void RegisterPassPlugins(ArrayRef<std::string> PassPlugins,
                                PassBuilder &PB) {
  // Load requested pass plugins and let them register pass builder callbacks
  for (auto &PluginFN : PassPlugins) {
    auto PassPlugin = PassPlugin::Load(PluginFN);
    if (!PassPlugin) {
      errs() << "Failed to load passes from '" << PluginFN
             << "'. Request ignored.\n";
      continue;
    }

    PassPlugin->registerPassBuilderCallbacks(PB);
  }
}

static std::unique_ptr<TargetMachine>
createTargetMachine(const Config &Conf, const Target *TheTarget, Module &M) {
  StringRef TheTriple = M.getTargetTriple();
  SubtargetFeatures Features;
  Features.getDefaultSubtargetFeatures(Triple(TheTriple));
  for (const std::string &A : Conf.MAttrs)
    Features.AddFeature(A);

  std::optional<CodeModel::Model> CodeModel;
  if (Conf.CodeModel)
    CodeModel = *Conf.CodeModel;
  else
    CodeModel = M.getCodeModel();

  std::unique_ptr<TargetMachine> TM(
      TheTarget->createTargetMachine(TheTriple, Conf.CPU, Features.getString(),
                                     Conf.Options, CodeModel, Conf.CGOptLevel));

  assert(TM && "Failed to create target machine");
  if (Conf.MachinePassHooks)
    static_cast<LLVMTargetMachine *>(TM.get())->setMachinePipelineHooks(
        Conf.MachinePassHooks);

  if (std::optional<uint64_t> LargeDataThreshold = M.getLargeDataThreshold())
    TM->setLargeDataThreshold(*LargeDataThreshold);

  return TM;
}

static void runNewPMPasses(const Config &Conf, Module &Mod, TargetMachine *TM,
                           unsigned OptLevel,
                           ModuleSummaryIndex *ExportSummary) {
  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;

  PassInstrumentationCallbacks PIC;
  StandardInstrumentations SI(Mod.getContext(), Conf.DebugPassManager,
                              Conf.VerifyEach);
  SI.registerCallbacks(PIC, &MAM);
  PipelineTuningOptions LocalPTO = Conf.PTO;
  if (Conf.LTOParallelOpt && Conf.ParallelOptCodeGenHook) {
    LocalPTO.NevercFastIPO = true;
    LocalPTO.NevercInlinerLiteFSimpl = NevercInlinerLiteFSimplOpt;
    if (NevercAutoLTOInlineThreshold != 0)
      LocalPTO.InlinerThreshold = NevercAutoLTOInlineThreshold;
    // The CGSCC inliner's incremental call-graph maintenance is superlinear
    // in the merged module's call-graph size (measured 99% of a 145s link on
    // a 1000-module project, vs 0.15% spent in actual inlining).  Switch to
    // the flat module inliner once the merged module is big enough for that
    // overhead to dominate; below the threshold the CGSCC inliner's
    // interleaved per-SCC simplification yields slightly better code at
    // negligible framework cost.
    if (NevercModuleInlinerThreshold != 0) {
      unsigned DefinedFns = 0;
      for (Function &F : Mod)
        if (!F.isDeclaration())
          ++DefinedFns;
      LocalPTO.NevercModuleInliner = DefinedFns >= NevercModuleInlinerThreshold;
    }
  }
  PassBuilder PB(TM, LocalPTO, /*PGOOpt=*/std::nullopt, &PIC);

  RegisterPassPlugins(Conf.PassPlugins, PB);
  if (Conf.PassBuilderHook)
    Conf.PassBuilderHook(PB);

  std::unique_ptr<TargetLibraryInfoImpl> TLII(
      new TargetLibraryInfoImpl(Triple(TM->getTargetTriple())));
  FAM.registerPass([&] { return TargetLibraryAnalysis(*TLII); });

  // Parse a custom AA pipeline if asked to.
  if (!Conf.AAPipeline.empty()) {
    AAManager AA;
    if (auto Err = PB.parseAAPipeline(AA, Conf.AAPipeline)) {
      report_fatal_error(Twine("unable to parse AA pipeline description '") +
                         Conf.AAPipeline + "': " + toString(std::move(Err)));
    }
    // Register the AA manager first so that our version is the one used.
    FAM.registerPass([&] { return std::move(AA); });
  }

  // Register all the basic analyses with the managers.
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  ModulePassManager MPM;

  if (!Conf.DisableVerify)
    MPM.addPass(VerifierPass());

  OptimizationLevel OL;

  switch (OptLevel) {
  default:
    llvm_unreachable("Invalid optimization level");
  case 0:
    OL = OptimizationLevel::O0;
    break;
  case 1:
    OL = OptimizationLevel::O1;
    break;
  case 2:
    OL = OptimizationLevel::O2;
    break;
  case 3:
    OL = OptimizationLevel::O3;
    break;
  }

  // Let the host inject module passes before the LTO optimization pipeline.
  // Routed through a Config hook so core LTO has no dependency on the host
  // (e.g. neverc's out-of-tree C-ABI plugin system).
  if (Conf.PreOptPassHook)
    Conf.PreOptPassHook(MPM);

  // Parse a custom pipeline if asked to.
  if (!Conf.OptPipeline.empty()) {
    if (auto Err = PB.parsePassPipeline(MPM, Conf.OptPipeline)) {
      report_fatal_error(Twine("unable to parse pass pipeline description '") +
                         Conf.OptPipeline + "': " + toString(std::move(Err)));
    }
  } else if (Conf.UseDefaultPipeline) {
    MPM.addPass(PB.buildPerModuleDefaultPipeline(OL));
  } else if (Conf.LTOParallelOpt && Conf.ParallelOptCodeGenHook) {
    if (OL != OptimizationLevel::O0) {
      MPM.addPass(PB.buildModuleSimplificationPipeline(
          OL, ThinOrFullLTOPhase::FullLTOPostLink));
      MPM.addPass(ConstantMergePass());
      MPM.addPass(DeadArgumentEliminationPass());
      MPM.addPass(GlobalDCEPass(/*InLTOPostLink=*/true));
    }
  } else {
    MPM.addPass(PB.buildLTODefaultPipeline(OL, ExportSummary));
  }

  // With parallel optimization the pipeline above is only the whole-module
  // simplification half.  The real post-optimization barrier is reached after
  // the deferred function pipeline (or after PCG recombines its optimized
  // partitions), so do not fire a final hook prematurely here.
  if (Conf.PostOptPassHook && !Conf.LTOParallelOpt)
    Conf.PostOptPassHook(MPM);

  if (!Conf.DisableVerify)
    MPM.addPass(VerifierPass());

  MPM.run(Mod, MAM);
}

bool lto::opt(const Config &Conf, TargetMachine *TM, unsigned Task, Module &Mod,
              ModuleSummaryIndex *ExportSummary) {
  runNewPMPasses(Conf, Mod, TM, Conf.OptLevel, ExportSummary);
  return !Conf.PostOptModuleHook || Conf.PostOptModuleHook(Task, Mod);
}

static void codegen(const Config &Conf, TargetMachine *TM,
                    AddStreamFn AddStream, unsigned Task, Module &Mod,
                    const ModuleSummaryIndex &CombinedIndex) {
  if (Conf.PreCodeGenModuleHook && !Conf.PreCodeGenModuleHook(Task, Mod))
    return;

  Expected<std::unique_ptr<CachedFileStream>> StreamOrErr =
      AddStream(Task, Mod.getModuleIdentifier());
  if (Error Err = StreamOrErr.takeError())
    report_fatal_error(std::move(Err));
  std::unique_ptr<CachedFileStream> &Stream = *StreamOrErr;
  TM->Options.ObjectFilenameForDebug =
      std::string(Stream->ObjectPathName.data(), Stream->ObjectPathName.size());

  legacy::PassManager CodeGenPasses;
  TargetLibraryInfoImpl TLII(Triple(Mod.getTargetTriple()));
  CodeGenPasses.add(new TargetLibraryInfoWrapperPass(TLII));
  CodeGenPasses.add(
      createImmutableModuleSummaryIndexWrapperPass(&CombinedIndex));
  if (Conf.PreCodeGenPassesHook)
    Conf.PreCodeGenPassesHook(CodeGenPasses);
  if (TM->addPassesToEmitFile(CodeGenPasses, *Stream->OS,
                              /*DwoOut=*/nullptr, Conf.CGFileType))
    report_fatal_error("Failed to setup codegen");
  CodeGenPasses.run(Mod);
}

static Expected<const Target *> initAndLookupTarget(const Config &C,
                                                    Module &Mod) {
  if (!C.OverrideTriple.empty())
    Mod.setTargetTriple(C.OverrideTriple);
  else if (Mod.getTargetTriple().empty())
    Mod.setTargetTriple(C.DefaultTriple);

  std::string Msg;
  const Target *T = TargetRegistry::lookupTarget(Mod.getTargetTriple(), Msg);
  if (!T)
    return make_error<StringError>(Msg, inconvertibleErrorCode());
  return T;
}

Error lto::finalizeOptimizationRemarks(
    std::unique_ptr<ToolOutputFile> DiagOutputFile) {
  // Make sure we flush the diagnostic remarks file in case the linker doesn't
  // call the global destructors before exiting.
  if (!DiagOutputFile)
    return Error::success();
  DiagOutputFile->keep();
  DiagOutputFile->os().flush();
  return Error::success();
}

Error lto::backend(const Config &C, AddStreamFn AddStream,
                   unsigned ParallelCodeGenParallelismLevel, Module &Mod,
                   ModuleSummaryIndex &CombinedIndex, bool SkipOptimization) {
  auto BackendDone = make_scope_exit([&] {
    if (C.BackendDoneHook)
      C.BackendDoneHook();
  });
  Expected<const Target *> TOrErr = initAndLookupTarget(C, Mod);
  if (!TOrErr)
    return TOrErr.takeError();

  std::unique_ptr<TargetMachine> TM = createTargetMachine(C, *TOrErr, Mod);
  bool CodeGenOnly = C.CodeGenOnly || SkipOptimization;

  LLVM_DEBUG(dbgs() << "Running regular LTO\n");
  if (!CodeGenOnly) {
    if (!opt(C, TM.get(), 0, Mod, /*ExportSummary=*/&CombinedIndex))
      return Error::success();
  }

  auto RunDeferredFuncOpt = [&]() {
    PipelineTuningOptions PTO;
    PTO.NevercFastIPO = true;
    PTO.LoopUnrolling = C.OptLevel >= 2;
    PTO.LoopInterleaving = C.OptLevel >= 2;
    PTO.LoopVectorization = C.PTO.LoopVectorization;
    PTO.SLPVectorization = C.PTO.SLPVectorization;
    PassBuilder PB(TM.get(), PTO);
    LoopAnalysisManager LAM;
    FunctionAnalysisManager FAM;
    CGSCCAnalysisManager CGAM;
    ModuleAnalysisManager MAM;
    TargetLibraryInfoImpl TLII(Triple(Mod.getTargetTriple()));
    FAM.registerPass([&] { return TargetLibraryAnalysis(TLII); });
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);
    OptimizationLevel OL;
    switch (C.OptLevel) {
    case 0:
      OL = OptimizationLevel::O0;
      break;
    case 1:
      OL = OptimizationLevel::O1;
      break;
    case 2:
      OL = OptimizationLevel::O2;
      break;
    default:
      OL = OptimizationLevel::O3;
      break;
    }
    ModulePassManager MPM;
    MPM.addPass(PB.buildModuleOptimizationPipeline(
        OL, ThinOrFullLTOPhase::FullLTOPostLink));
    MPM.addPass(GlobalDCEPass(/*InLTOPostLink=*/true));
    if (C.PostOptPassHook)
      C.PostOptPassHook(MPM);
    MPM.run(Mod, MAM);
  };

  bool DeferredFuncOptDone = false;

  if (ParallelCodeGenParallelismLevel > 1 && C.ParallelCodeGenHook) {
    Expected<std::unique_ptr<CachedFileStream>> StreamOrErr =
        AddStream(0, Mod.getModuleIdentifier());
    if (Error Err = StreamOrErr.takeError())
      report_fatal_error(std::move(Err));
    auto &Stream = *StreamOrErr;

    if (!CodeGenOnly && C.LTOParallelOpt && C.ParallelOptCodeGenHook) {
      if (C.ParallelOptCodeGenHook(Mod, *TM, *Stream->OS,
                                   ParallelCodeGenParallelismLevel, C.OptLevel))
        return Error::success();
    }

    if (C.LTOParallelOpt && !CodeGenOnly) {
      RunDeferredFuncOpt();
      DeferredFuncOptDone = true;
    }

    if (C.ParallelCodeGenHook(Mod, *TM, *Stream->OS,
                              ParallelCodeGenParallelismLevel))
      return Error::success();
  }

  if (C.LTOParallelOpt && !CodeGenOnly && !DeferredFuncOptDone)
    RunDeferredFuncOpt();

  codegen(C, TM.get(), AddStream, 0, Mod, CombinedIndex);
  return Error::success();
}
