//===- Construction of pass pipelines -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file provides the implementation of the PassBuilder based on our
/// static pass registry as well as related functionality. It also provides
/// helpers to aid in analyzing, debugging, and testing passes and pass
/// pipelines.
///
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/InlineAdvisor.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/ScopedNoAliasAA.h"
#include "llvm/Analysis/TypeBasedAliasAnalysis.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/PGOOptions.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/AggressiveInstCombine/AggressiveInstCombine.h"
#include "llvm/Transforms/IPO/AlwaysInliner.h"
#include "llvm/Transforms/IPO/Annotation2Metadata.h"
#include "llvm/Transforms/IPO/ArgumentPromotion.h"
#include "llvm/Transforms/IPO/CalledValuePropagation.h"
#include "llvm/Transforms/IPO/ConstantMerge.h"
#include "llvm/Transforms/IPO/DeadArgumentElimination.h"
#include "llvm/Transforms/IPO/ElimAvailExtern.h"
#include "llvm/Transforms/IPO/EmbedBitcodePass.h"
#include "llvm/Transforms/IPO/ForceFunctionAttrs.h"
#include "llvm/Transforms/IPO/FunctionAttrs.h"
#include "llvm/Transforms/IPO/GlobalDCE.h"
#include "llvm/Transforms/IPO/GlobalOpt.h"
#include "llvm/Transforms/IPO/InferFunctionAttrs.h"
#include "llvm/Transforms/IPO/Inliner.h"
#include "llvm/Transforms/IPO/LowerTypeTests.h"
#include "llvm/Transforms/IPO/SCCP.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Instrumentation/CGProfile.h"
#include "llvm/Transforms/Instrumentation/PGOInstrumentation.h"
#include "llvm/Transforms/Scalar/ADCE.h"
#include "llvm/Transforms/Scalar/AlignmentFromAssumptions.h"
#include "llvm/Transforms/Scalar/AnnotationRemarks.h"
#include "llvm/Transforms/Scalar/BDCE.h"
#include "llvm/Transforms/Scalar/CallSiteSplitting.h"
#include "llvm/Transforms/Scalar/ConstraintElimination.h"
#include "llvm/Transforms/Scalar/CorrelatedValuePropagation.h"
#include "llvm/Transforms/Scalar/DeadStoreElimination.h"
#include "llvm/Transforms/Scalar/DivRemPairs.h"
#include "llvm/Transforms/Scalar/EarlyCSE.h"
#include "llvm/Transforms/Scalar/Float2Int.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/IndVarSimplify.h"
#include "llvm/Transforms/Scalar/InferAlignment.h"
#include "llvm/Transforms/Scalar/InstSimplifyPass.h"
#include "llvm/Transforms/Scalar/JumpThreading.h"
#include "llvm/Transforms/Scalar/LICM.h"
#include "llvm/Transforms/Scalar/LoopDeletion.h"
#include "llvm/Transforms/Scalar/LoopDistribute.h"
#include "llvm/Transforms/Scalar/LoopIdiomRecognize.h"
#include "llvm/Transforms/Scalar/LoopInstSimplify.h"
#include "llvm/Transforms/Scalar/LoopLoadElimination.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Scalar/LoopRotation.h"
#include "llvm/Transforms/Scalar/LoopSimplifyCFG.h"
#include "llvm/Transforms/Scalar/LoopSink.h"
#include "llvm/Transforms/Scalar/LoopUnrollPass.h"
#include "llvm/Transforms/Scalar/LowerConstantIntrinsics.h"
#include "llvm/Transforms/Scalar/LowerExpectIntrinsic.h"
#include "llvm/Transforms/Scalar/MemCpyOptimizer.h"
#include "llvm/Transforms/Scalar/MergedLoadStoreMotion.h"
#include "llvm/Transforms/Scalar/Reassociate.h"
#include "llvm/Transforms/Scalar/SCCP.h"
#include "llvm/Transforms/Scalar/SROA.h"
#include "llvm/Transforms/Scalar/SimpleLoopUnswitch.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"
#include "llvm/Transforms/Scalar/TailRecursionElimination.h"
#include "llvm/Transforms/Scalar/WarnMissedTransforms.h"
#include "llvm/Transforms/Utils/AssumeBundleBuilder.h"
#include "llvm/Transforms/Utils/CanonicalizeAliases.h"
#include "llvm/Transforms/Utils/InjectTLIMappings.h"
#include "llvm/Transforms/Utils/LibCallsShrinkWrap.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"
#include "llvm/Transforms/Utils/MoveAutoInit.h"
#include "llvm/Transforms/Utils/NameAnonGlobals.h"
#include "llvm/Transforms/Utils/RelLookupTableConverter.h"
#include "llvm/Transforms/Utils/SimplifyCFGOptions.h"
#include "llvm/Transforms/Vectorize/LoopVectorize.h"
#include "llvm/Transforms/Vectorize/SLPVectorizer.h"
#include "llvm/Transforms/Vectorize/VectorCombine.h"

using namespace llvm;

/// Flag to enable inline deferral during PGO.
static cl::opt<bool>
    EnablePGOInlineDeferral("enable-npm-pgo-inline-deferral", cl::init(true),
                            cl::Hidden,
                            cl::desc("Enable inline deferral during PGO"));

static cl::opt<bool> PerformMandatoryInliningsFirst(
    "mandatory-inlining-first", cl::init(false), cl::Hidden,
    cl::desc("Perform mandatory inlinings module-wide, before performing "
             "inlining"));

static cl::opt<bool> EnableEagerlyInvalidateAnalyses(
    "eagerly-invalidate-analyses", cl::init(true), cl::Hidden,
    cl::desc("Eagerly invalidate more analyses in default pipelines"));

static cl::opt<bool>
    EnableGlobalAnalyses("enable-global-analyses", cl::init(true), cl::Hidden,
                         cl::desc("Enable inter-procedural analyses"));

static cl::opt<bool> ExtraVectorizerPasses(
    "extra-vectorizer-passes", cl::init(false), cl::Hidden,
    cl::desc("Run cleanup optimization passes after vectorization"));

static cl::opt<bool>
    DisablePreInliner("disable-preinline", cl::init(false), cl::Hidden,
                      cl::desc("Disable pre-instrumentation inliner"));

static cl::opt<int> PreInlineThreshold(
    "preinline-threshold", cl::Hidden, cl::init(75),
    cl::desc("Control the amount of inlining in pre-instrumentation inliner "
             "(default = 75)"));

static cl::opt<int> PreInlineHintThreshold(
    "preinline-hint-threshold", cl::Hidden, cl::init(150),
    cl::desc("Hint threshold for pre-instrumentation inliner "
             "(default = 150)"));

static cl::opt<bool> EnableConstraintElimination(
    "enable-constraint-elimination", cl::init(true), cl::Hidden,
    cl::desc(
        "Enable pass to eliminate conditions based on linear constraints"));

namespace llvm {

extern cl::opt<bool> EnableInferAlignmentPass;
} // namespace llvm

PipelineTuningOptions::PipelineTuningOptions() {
  LoopInterleaving = true;
  LoopVectorization = true;
  SLPVectorization = false;
  LoopUnrolling = true;
  ForgetAllSCEVInLoopUnroll = ForgetSCEVInLoopUnroll;
  LicmMssaOptCap = SetLicmMssaOptCap;
  LicmMssaNoAccForPromotionCap = SetLicmMssaNoAccForPromotionCap;
  CallGraphProfile = true;
  UnifiedLTO = false;
  InlinerThreshold = -1;
  EagerlyInvalidateAnalyses = EnableEagerlyInvalidateAnalyses;
}

namespace llvm {
extern cl::opt<unsigned> MaxDevirtIterations;
} // namespace llvm

void PassBuilder::invokePeepholeEPCallbacks(FunctionPassManager &FPM,
                                            OptimizationLevel Level) {
  for (auto &C : PeepholeEPCallbacks)
    C(FPM, Level);
}
void PassBuilder::invokeLateLoopOptimizationsEPCallbacks(
    LoopPassManager &LPM, OptimizationLevel Level) {
  for (auto &C : LateLoopOptimizationsEPCallbacks)
    C(LPM, Level);
}
void PassBuilder::invokeLoopOptimizerEndEPCallbacks(LoopPassManager &LPM,
                                                    OptimizationLevel Level) {
  for (auto &C : LoopOptimizerEndEPCallbacks)
    C(LPM, Level);
}
void PassBuilder::invokeScalarOptimizerLateEPCallbacks(
    FunctionPassManager &FPM, OptimizationLevel Level) {
  for (auto &C : ScalarOptimizerLateEPCallbacks)
    C(FPM, Level);
}
void PassBuilder::invokeCGSCCOptimizerLateEPCallbacks(CGSCCPassManager &CGPM,
                                                      OptimizationLevel Level) {
  for (auto &C : CGSCCOptimizerLateEPCallbacks)
    C(CGPM, Level);
}
void PassBuilder::invokeVectorizerStartEPCallbacks(FunctionPassManager &FPM,
                                                   OptimizationLevel Level) {
  for (auto &C : VectorizerStartEPCallbacks)
    C(FPM, Level);
}
void PassBuilder::invokeOptimizerEarlyEPCallbacks(ModulePassManager &MPM,
                                                  OptimizationLevel Level) {
  for (auto &C : OptimizerEarlyEPCallbacks)
    C(MPM, Level);
}
void PassBuilder::invokeOptimizerLastEPCallbacks(ModulePassManager &MPM,
                                                 OptimizationLevel Level) {
  for (auto &C : OptimizerLastEPCallbacks)
    C(MPM, Level);
}
void PassBuilder::invokeFullLinkTimeOptimizationEarlyEPCallbacks(
    ModulePassManager &MPM, OptimizationLevel Level) {
  for (auto &C : FullLinkTimeOptimizationEarlyEPCallbacks)
    C(MPM, Level);
}
void PassBuilder::invokeFullLinkTimeOptimizationLastEPCallbacks(
    ModulePassManager &MPM, OptimizationLevel Level) {
  for (auto &C : FullLinkTimeOptimizationLastEPCallbacks)
    C(MPM, Level);
}
void PassBuilder::invokePipelineStartEPCallbacks(ModulePassManager &MPM,
                                                 OptimizationLevel Level) {
  for (auto &C : PipelineStartEPCallbacks)
    C(MPM, Level);
}
void PassBuilder::invokePipelineEarlySimplificationEPCallbacks(
    ModulePassManager &MPM, OptimizationLevel Level) {
  for (auto &C : PipelineEarlySimplificationEPCallbacks)
    C(MPM, Level);
}

// Helper to add AnnotationRemarksPass.
static void addAnnotationRemarksPass(ModulePassManager &MPM) {
  MPM.addPass(createModuleToFunctionPassAdaptor(AnnotationRemarksPass()));
}

// Helper to check if the current compilation phase is preparing for LTO
static bool isLTOPreLink(ThinOrFullLTOPhase Phase) {
  return Phase == ThinOrFullLTOPhase::FullLTOPreLink;
}

// TODO: Investigate the cost/benefit of tail call elimination on debugging.
FunctionPassManager
PassBuilder::buildO1FunctionSimplificationPipeline(OptimizationLevel Level,
                                                   ThinOrFullLTOPhase Phase) {

  FunctionPassManager FPM;

  // Form SSA out of local memory accesses after breaking apart aggregates into
  // scalars.
  FPM.addPass(SROAPass(SROAOptions::ModifyCFG));

  // Catch trivial redundancies
  FPM.addPass(EarlyCSEPass(true /* Enable mem-ssa. */));

  // Hoisting of scalars and load expressions.
  FPM.addPass(
      SimplifyCFGPass(SimplifyCFGOptions().convertSwitchRangeToICmp(true)));
  FPM.addPass(InstCombinePass());

  FPM.addPass(LibCallsShrinkWrapPass());

  invokePeepholeEPCallbacks(FPM, Level);

  FPM.addPass(
      SimplifyCFGPass(SimplifyCFGOptions().convertSwitchRangeToICmp(true)));

  // Form canonically associated expression trees, and simplify the trees using
  // basic mathematical properties. For example, this will form (nearly)
  // minimal multiplication trees.
  FPM.addPass(ReassociatePass());

  // Add the primary loop simplification pipeline.
  // FIXME: Currently this is split into two loop pass pipelines because we run
  // some function passes in between them. These can and should be removed
  // and/or replaced by scheduling the loop pass equivalents in the correct
  // positions. But those equivalent passes aren't powerful enough yet.
  // Specifically, `SimplifyCFGPass` and `InstCombinePass` are currently still
  // used. We have `LoopSimplifyCFGPass` which isn't yet powerful enough yet to
  // fully replace `SimplifyCFGPass`, and the closest to the other we have is
  // `LoopInstSimplify`.
  LoopPassManager LPM1, LPM2;

  // Simplify the loop body. We do this initially to clean up after other loop
  // passes run, either when iterating on a loop or on inner loops with
  // implications on the outer loop.
  LPM1.addPass(LoopInstSimplifyPass());
  LPM1.addPass(LoopSimplifyCFGPass());

  // Try to remove as much code from the loop header as possible,
  // to reduce amount of IR that will have to be duplicated. However,
  // do not perform speculative hoisting the first time as LICM
  // will destroy metadata that may not need to be destroyed if run
  // after loop rotation.
  // TODO: Investigate promotion cap for O1.
  LPM1.addPass(LICMPass(PTO.LicmMssaOptCap, PTO.LicmMssaNoAccForPromotionCap,
                        /*AllowSpeculation=*/false));

  LPM1.addPass(LoopRotatePass(/* Disable header duplication */ true,
                              isLTOPreLink(Phase)));
  // TODO: Investigate promotion cap for O1.
  LPM1.addPass(LICMPass(PTO.LicmMssaOptCap, PTO.LicmMssaNoAccForPromotionCap,
                        /*AllowSpeculation=*/true));
  LPM1.addPass(SimpleLoopUnswitchPass());

  LPM2.addPass(LoopIdiomRecognizePass());
  LPM2.addPass(IndVarSimplifyPass());

  invokeLateLoopOptimizationsEPCallbacks(LPM2, Level);

  LPM2.addPass(LoopDeletionPass());

  LPM2.addPass(LoopFullUnrollPass(Level.getSpeedupLevel(),
                                  /* OnlyWhenForced= */ !PTO.LoopUnrolling,
                                  PTO.ForgetAllSCEVInLoopUnroll));

  invokeLoopOptimizerEndEPCallbacks(LPM2, Level);

  FPM.addPass(createFunctionToLoopPassAdaptor(std::move(LPM1),
                                              /*UseMemorySSA=*/true,
                                              /*UseBlockFrequencyInfo=*/true));
  FPM.addPass(
      SimplifyCFGPass(SimplifyCFGOptions().convertSwitchRangeToICmp(true)));
  FPM.addPass(InstCombinePass());
  // The loop passes in LPM2 (LoopFullUnrollPass) do not preserve MemorySSA.
  // *All* loop passes must preserve it, in order to be able to use it.
  FPM.addPass(createFunctionToLoopPassAdaptor(std::move(LPM2),
                                              /*UseMemorySSA=*/false,
                                              /*UseBlockFrequencyInfo=*/false));

  // Delete small array after loop unroll.
  FPM.addPass(SROAPass(SROAOptions::ModifyCFG));

  // Specially optimize memory movement as it doesn't look like dataflow in SSA.
  FPM.addPass(MemCpyOptPass());

  // Sparse conditional constant propagation.
  // FIXME: It isn't clear why we do this *after* loop passes rather than
  // before...
  FPM.addPass(SCCPPass());

  // Delete dead bit computations (instcombine runs after to fold away the dead
  // computations, and then ADCE will run later to exploit any new DCE
  // opportunities that creates).
  FPM.addPass(BDCEPass());

  // Run instcombine after redundancy and dead bit elimination to exploit
  // opportunities opened up by them.
  FPM.addPass(InstCombinePass());
  invokePeepholeEPCallbacks(FPM, Level);

  invokeScalarOptimizerLateEPCallbacks(FPM, Level);

  // Finally, do an expensive DCE pass to catch all the dead code exposed by
  // the simplifications and basic cleanup after all the simplifications.
  // TODO: Investigate if this is too expensive.
  FPM.addPass(ADCEPass());
  FPM.addPass(
      SimplifyCFGPass(SimplifyCFGOptions().convertSwitchRangeToICmp(true)));
  invokePeepholeEPCallbacks(FPM, Level);

  return FPM;
}

FunctionPassManager
PassBuilder::buildFunctionSimplificationPipeline(OptimizationLevel Level,
                                                 ThinOrFullLTOPhase Phase) {
  assert(Level != OptimizationLevel::O0 && "Must request optimizations!");

  // The O1 pipeline has a separate pipeline creation function to simplify
  // construction readability.
  if (Level.getSpeedupLevel() == 1)
    return buildO1FunctionSimplificationPipeline(Level, Phase);

  FunctionPassManager FPM;

  // NeverC LTO Inliner-Lite: the inliner cost model only needs a canonical
  // view of each callee.  SROA + InstCombine + ADCE give a clean IR shape
  // without the expense of JumpThreading, CVP, AggressiveInstCombine,
  // TailCallElim, Reassociate, ConstraintElimination, or loop/memory
  // passes.  All of these re-run per partition in the parallel post-link
  // buildModuleOptimizationPipeline.
  if (PTO.NevercInlinerLiteFSimpl) {
    FPM.addPass(SROAPass(SROAOptions::ModifyCFG));
    FPM.addPass(InstCombinePass());
    FPM.addPass(ADCEPass());
    return FPM;
  }

  // Form SSA out of local memory accesses after breaking apart aggregates into
  // scalars.
  FPM.addPass(SROAPass(SROAOptions::ModifyCFG));

  // Catch trivial redundancies. At O2, skip MemorySSA to reduce analysis
  // overhead; GVN later provides thorough load/store redundancy elimination.
  FPM.addPass(EarlyCSEPass(Level == OptimizationLevel::O3));
  if (EnableKnowledgeRetention)
    FPM.addPass(AssumeSimplifyPass());

  // Optimize based on known information about branches, and cleanup afterward.
  // At O2, JumpThreading + CVP are deferred: IPSCCP already handles
  // interprocedural constant propagation, and GVN + InstCombine cover most
  // value-range narrowing. CVP's LazyValueInfo is expensive (3.6% compile
  // time).
  if (Level == OptimizationLevel::O3) {
    FPM.addPass(JumpThreadingPass());
    FPM.addPass(CorrelatedValuePropagationPass());
  }

  FPM.addPass(
      SimplifyCFGPass(SimplifyCFGOptions().convertSwitchRangeToICmp(true)));
  FPM.addPass(InstCombinePass());
  if (Level == OptimizationLevel::O3) {
    FPM.addPass(AggressiveInstCombinePass());
    FPM.addPass(LibCallsShrinkWrapPass());
  }

  invokePeepholeEPCallbacks(FPM, Level);

  FPM.addPass(TailCallElimPass());

  // Form canonically associated expression trees, and simplify the trees using
  // basic mathematical properties.
  FPM.addPass(ReassociatePass());

  if (EnableConstraintElimination && Level == OptimizationLevel::O3)
    FPM.addPass(ConstraintEliminationPass());

  // Add the primary loop simplification pipeline.
  // FIXME: Currently this is split into two loop pass pipelines because we run
  // some function passes in between them. These can and should be removed
  // and/or replaced by scheduling the loop pass equivalents in the correct
  // positions. But those equivalent passes aren't powerful enough yet.
  // Specifically, `SimplifyCFGPass` and `InstCombinePass` are currently still
  // used. We have `LoopSimplifyCFGPass` which isn't yet powerful enough yet to
  // fully replace `SimplifyCFGPass`, and the closest to the other we have is
  // `LoopInstSimplify`.
  LoopPassManager LPM1, LPM2;

  // Simplify the loop body. We do this initially to clean up after other loop
  // passes run, either when iterating on a loop or on inner loops with
  // implications on the outer loop.
  LPM1.addPass(LoopInstSimplifyPass());
  LPM1.addPass(LoopSimplifyCFGPass());

  // Try to remove as much code from the loop header as possible,
  // to reduce amount of IR that will have to be duplicated. However,
  // do not perform speculative hoisting the first time as LICM
  // will destroy metadata that may not need to be destroyed if run
  // after loop rotation.
  // TODO: Investigate promotion cap for O1.
  LPM1.addPass(LICMPass(PTO.LicmMssaOptCap, PTO.LicmMssaNoAccForPromotionCap,
                        /*AllowSpeculation=*/false));

  // Disable header duplication in loop rotation at -Oz.
  LPM1.addPass(
      LoopRotatePass(Level != OptimizationLevel::Oz, isLTOPreLink(Phase)));
  // TODO: Investigate promotion cap for O1.
  LPM1.addPass(LICMPass(PTO.LicmMssaOptCap, PTO.LicmMssaNoAccForPromotionCap,
                        /*AllowSpeculation=*/true));
  LPM1.addPass(
      SimpleLoopUnswitchPass(/* NonTrivial */ Level == OptimizationLevel::O3));

  LPM2.addPass(LoopIdiomRecognizePass());
  if (Level == OptimizationLevel::O3)
    LPM2.addPass(IndVarSimplifyPass());

  invokeLateLoopOptimizationsEPCallbacks(LPM2, Level);

  LPM2.addPass(LoopDeletionPass());

  LPM2.addPass(LoopFullUnrollPass(Level.getSpeedupLevel(),
                                  /* OnlyWhenForced= */ !PTO.LoopUnrolling,
                                  PTO.ForgetAllSCEVInLoopUnroll));

  invokeLoopOptimizerEndEPCallbacks(LPM2, Level);

  FPM.addPass(createFunctionToLoopPassAdaptor(std::move(LPM1),
                                              /*UseMemorySSA=*/true,
                                              /*UseBlockFrequencyInfo=*/true));
  if (Level == OptimizationLevel::O3) {
    FPM.addPass(
        SimplifyCFGPass(SimplifyCFGOptions().convertSwitchRangeToICmp(true)));
    FPM.addPass(InstCombinePass());
  }
  // The loop passes in LPM2 (LoopIdiomRecognizePass, IndVarSimplifyPass,
  // LoopDeletionPass and LoopFullUnrollPass) do not preserve MemorySSA.
  // *All* loop passes must preserve it, in order to be able to use it.
  FPM.addPass(createFunctionToLoopPassAdaptor(std::move(LPM2),
                                              /*UseMemorySSA=*/false,
                                              /*UseBlockFrequencyInfo=*/false));

  // Delete small array after loop unroll.
  FPM.addPass(SROAPass(SROAOptions::ModifyCFG));

  // Try vectorization/scalarization transforms that are both improvements
  // themselves and can allow further folds with GVN and InstCombine.
  if (Level == OptimizationLevel::O3)
    FPM.addPass(VectorCombinePass(/*TryEarlyFoldsOnly=*/true));

  // Eliminate redundancies.
  if (Level == OptimizationLevel::O3)
    FPM.addPass(MergedLoadStoreMotionPass());
  // NeverC: GVN inside the inliner's per-SCC function simplification
  // accounts for ~10% of LTO simplify-only wall time on Redis.  It is run
  // here so that the inliner cost model sees the post-redundancy-elimination
  // IR.  In the LTO ParallelOpt path the post-link module optimization
  // pipeline runs on each partition in parallel and also performs
  // redundancy elimination via InstCombine + later cleanup; the marginal
  // benefit of running GVN serially here is small relative to the wall
  // time cost.  Skip when PTO.NevercFastIPO is set.
  if (!PTO.NevercFastIPO)
    FPM.addPass(GVNPass());

  // At O2, IPSCCP already ran at module level; function-level SCCP finds
  // diminishing additional constants. Defer to O3.
  if (Level == OptimizationLevel::O3)
    FPM.addPass(SCCPPass());

  // Delete dead bit computations. At O2, ADCE later handles most dead code
  // and the extra InstCombine cleanup from BDCE is marginal.
  if (Level == OptimizationLevel::O3)
    FPM.addPass(BDCEPass());

  // Run instcombine after redundancy and dead bit elimination to exploit
  // opportunities opened up by them.
  FPM.addPass(InstCombinePass());
  invokePeepholeEPCallbacks(FPM, Level);

  // Re-consider control flow based optimizations after redundancy elimination,
  // redo DCE, etc. At O3, run a second round of JumpThreading + CVP to catch
  // opportunities exposed by GVN/SCCP/BDCE. At O2, the first round is enough.
  if (Level == OptimizationLevel::O3) {
    FPM.addPass(JumpThreadingPass());
    FPM.addPass(CorrelatedValuePropagationPass());
  }

  // Finally, do an expensive DCE pass to catch all the dead code exposed by
  // the simplifications and basic cleanup after all the simplifications.
  // TODO: Investigate if this is too expensive.
  FPM.addPass(ADCEPass());

  // Specially optimize memory movement as it doesn't look like dataflow in SSA.
  FPM.addPass(MemCpyOptPass());

  FPM.addPass(DSEPass());
  if (Level == OptimizationLevel::O3) {
    FPM.addPass(MoveAutoInitPass());
    FPM.addPass(createFunctionToLoopPassAdaptor(
        LICMPass(PTO.LicmMssaOptCap, PTO.LicmMssaNoAccForPromotionCap,
                 /*AllowSpeculation=*/true),
        /*UseMemorySSA=*/true, /*UseBlockFrequencyInfo=*/false));
  }

  invokeScalarOptimizerLateEPCallbacks(FPM, Level);

  {
    auto Opts = SimplifyCFGOptions().convertSwitchRangeToICmp(true);
    if (Level == OptimizationLevel::O3)
      Opts.hoistCommonInsts(true).sinkCommonInsts(true);
    FPM.addPass(SimplifyCFGPass(Opts));
  }
  if (Level == OptimizationLevel::O3) {
    FPM.addPass(InstCombinePass());
    invokePeepholeEPCallbacks(FPM, Level);
  }

  return FPM;
}

void PassBuilder::addRequiredLTOPreLinkPasses(ModulePassManager &MPM) {
  MPM.addPass(CanonicalizeAliasesPass());
  MPM.addPass(NameAnonGlobalPass());
}

void PassBuilder::addPreInlinerPasses(ModulePassManager &MPM,
                                      OptimizationLevel Level,
                                      ThinOrFullLTOPhase LTOPhase) {
  assert(Level != OptimizationLevel::O0 && "Not expecting O0 here!");
  if (DisablePreInliner)
    return;
  InlineParams IP;

  IP.DefaultThreshold = PreInlineThreshold;

  IP.HintThreshold =
      Level.isOptimizingForSize() ? PreInlineThreshold : PreInlineHintThreshold;
  ModuleInlinerWrapperPass MIWP(
      IP, /* MandatoryFirst */ true,
      InlineContext{LTOPhase, InlinePass::EarlyInliner});
  CGSCCPassManager &CGPipeline = MIWP.getPM();

  FunctionPassManager FPM;
  FPM.addPass(SROAPass(SROAOptions::ModifyCFG));
  FPM.addPass(EarlyCSEPass()); // Catch trivial redundancies.
  FPM.addPass(SimplifyCFGPass(SimplifyCFGOptions().convertSwitchRangeToICmp(
      true)));                    // Merge & remove basic blocks.
  FPM.addPass(InstCombinePass()); // Combine silly sequences.
  invokePeepholeEPCallbacks(FPM, Level);

  CGPipeline.addPass(createCGSCCToFunctionPassAdaptor(
      std::move(FPM), PTO.EagerlyInvalidateAnalyses));

  MPM.addPass(std::move(MIWP));

  // Delete anything that is now dead to make sure that we don't instrument
  // dead code. Instrumentation can end up keeping dead code around and
  // dramatically increase code size.
  MPM.addPass(GlobalDCEPass());
}

void PassBuilder::addPGOInstrPasses(ModulePassManager &, OptimizationLevel,
                                    bool, bool, bool, StringRef,
                                    IntrusiveRefCntPtr<vfs::FileSystem>) {
  llvm_unreachable("PGO not available: built with NEVERC_ENABLE_PROFILING=OFF");
}
void PassBuilder::addPGOInstrPassesForO0(ModulePassManager &, bool, bool, bool,
                                         StringRef,
                                         IntrusiveRefCntPtr<vfs::FileSystem>) {
  llvm_unreachable("PGO not available: built with NEVERC_ENABLE_PROFILING=OFF");
}

static InlineParams getInlineParamsFromOptLevel(OptimizationLevel Level) {
  return getInlineParams(Level.getSpeedupLevel(), Level.getSizeLevel());
}

ModuleInlinerWrapperPass
PassBuilder::buildInlinerPipeline(OptimizationLevel Level,
                                  ThinOrFullLTOPhase Phase) {
  InlineParams IP;
  if (PTO.InlinerThreshold == -1)
    IP = getInlineParamsFromOptLevel(Level);
  else
    IP = getInlineParams(PTO.InlinerThreshold);
  if (PGOOpt)
    IP.EnableDeferral = EnablePGOInlineDeferral;

  ModuleInlinerWrapperPass MIWP(IP, PerformMandatoryInliningsFirst,
                                InlineContext{Phase, InlinePass::CGSCCInliner},
                                InliningAdvisorMode::Default,
                                MaxDevirtIterations);

  // Require the GlobalsAA analysis for the module so we can query it within
  // the CGSCC pipeline.
  if (EnableGlobalAnalyses) {
    MIWP.addModulePass(RequireAnalysisPass<GlobalsAA, Module>());
    // Invalidate AAManager so it can be recreated and pick up the newly
    // available GlobalsAA.
    MIWP.addModulePass(
        createModuleToFunctionPassAdaptor(InvalidateAnalysisPass<AAManager>()));
  }

  // Require the ProfileSummaryAnalysis for the module so we can query it within
  // the inliner pass.
  MIWP.addModulePass(RequireAnalysisPass<ProfileSummaryAnalysis, Module>());

  // Now begin the main postorder CGSCC pipeline.
  // FIXME: The current CGSCC pipeline has its origins in the legacy pass
  // manager and trying to emulate its precise behavior. Much of this doesn't
  // make a lot of sense and we should revisit the core CGSCC structure.
  CGSCCPassManager &MainCGPipeline = MIWP.getPM();

  // Note: historically, the PruneEH pass was run first to deduce nounwind and
  // generally clean up exception handling overhead. It isn't clear this is
  // valuable as the inliner doesn't currently care whether it is inlining an
  // invoke or a call.

  // Deduce function attributes. We do another run of this after the function
  // simplification pipeline, so this only needs to run when it could affect the
  // function simplification pipeline, which is only the case with recursive
  // functions.
  MainCGPipeline.addPass(PostOrderFunctionAttrsPass(/*SkipNonRecursive*/ true));

  // When at O3 add argument promotion to the pass pipeline.
  // FIXME: It isn't at all clear why this should be limited to O3.
  if (Level == OptimizationLevel::O3)
    MainCGPipeline.addPass(ArgumentPromotionPass());

  invokeCGSCCOptimizerLateEPCallbacks(MainCGPipeline, Level);

  // Add the core function simplification pipeline nested inside the
  // CGSCC walk.
  MainCGPipeline.addPass(createCGSCCToFunctionPassAdaptor(
      buildFunctionSimplificationPipeline(Level, Phase),
      PTO.EagerlyInvalidateAnalyses, /*NoRerun=*/true));

  // Finally, deduce any function attributes based on the fully simplified
  // function. At O2, skip expensive recursive SCC analysis; the first
  // SkipNonRecursive pass already handles the common case.
  MainCGPipeline.addPass(
      PostOrderFunctionAttrsPass(Level != OptimizationLevel::O3));

  // Mark that the function is fully simplified and that it shouldn't be
  // simplified again if we somehow revisit it due to CGSCC mutations unless
  // it's been modified since.
  MainCGPipeline.addPass(createCGSCCToFunctionPassAdaptor(
      RequireAnalysisPass<ShouldNotRunFunctionPassesAnalysis, Function>()));

  // Make sure we don't affect potential future NoRerun CGSCC adaptors.
  MIWP.addLateModulePass(createModuleToFunctionPassAdaptor(
      InvalidateAnalysisPass<ShouldNotRunFunctionPassesAnalysis>()));

  return MIWP;
}

ModulePassManager
PassBuilder::buildModuleSimplificationPipeline(OptimizationLevel Level,
                                               ThinOrFullLTOPhase Phase) {
  assert(Level != OptimizationLevel::O0 &&
         "Should not be used for O0 pipeline");

  // NeverC's LTO ParallelOpt path intentionally calls this with
  // FullLTOPostLink to run simplification-only before parallel codegen.

  ModulePassManager MPM;

  // Do basic inference of function attributes from known properties of system
  // libraries and other oracles.
  MPM.addPass(InferFunctionAttrsPass());

  FunctionPassManager EarlyFPM;
  EarlyFPM.addPass(LowerExpectIntrinsicPass());
  EarlyFPM.addPass(SimplifyCFGPass());
  EarlyFPM.addPass(SROAPass(SROAOptions::ModifyCFG));
  EarlyFPM.addPass(EarlyCSEPass());
  if (Level == OptimizationLevel::O3)
    EarlyFPM.addPass(CallSiteSplittingPass());
  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(EarlyFPM),
                                                PTO.EagerlyInvalidateAnalyses));

  invokePipelineEarlySimplificationEPCallbacks(MPM, Level);

  // NeverC: In LTO ParallelOpt mode, IPSCCP (151ms) and GlobalOpt (117ms)
  // dominate the serial simplification pipeline (~72% of 325ms on Redis).
  // Per-partition buildModuleOptimizationPipeline re-runs IPSCCP, so the
  // serial run mainly benefits inlining decisions.  Skip both when
  // NevercFastIPO is set.
  if (!PTO.NevercFastIPO)
    MPM.addPass(IPSCCPPass());

  if (!PTO.NevercFastIPO)
    MPM.addPass(CalledValuePropagationPass());

  if (!PTO.NevercFastIPO)
    MPM.addPass(GlobalOptPass());

  // Create a small function pass pipeline to cleanup after all the global
  // optimizations.
  FunctionPassManager GlobalCleanupPM;
  // FIXME: Should this instead by a run of SROA?
  GlobalCleanupPM.addPass(PromotePass());
  if (Level == OptimizationLevel::O3)
    GlobalCleanupPM.addPass(InstCombinePass());
  invokePeepholeEPCallbacks(GlobalCleanupPM, Level);
  GlobalCleanupPM.addPass(
      SimplifyCFGPass(SimplifyCFGOptions().convertSwitchRangeToICmp(true)));
  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(GlobalCleanupPM),
                                                PTO.EagerlyInvalidateAnalyses));

  // Invoke the pre-inliner passes for instrumentation PGO or MemProf.
  if (PGOOpt &&
      (PGOOpt->Action == PGOOptions::IRInstr ||
       PGOOpt->Action == PGOOptions::IRUse || !PGOOpt->MemoryProfile.empty()))
    addPreInlinerPasses(MPM, Level, Phase);

  // Add all the requested passes for instrumentation PGO, if requested.
  if (PGOOpt && (PGOOpt->Action == PGOOptions::IRInstr ||
                 PGOOpt->Action == PGOOptions::IRUse)) {
    addPGOInstrPasses(MPM, Level,
                      /*RunProfileGen=*/PGOOpt->Action == PGOOptions::IRInstr,
                      /*IsCS=*/false, PGOOpt->AtomicCounterUpdate,
                      PGOOpt->ProfileFile, PGOOpt->FS);
    MPM.addPass(PGOIndirectCallPromotion(false, false));
  }

  MPM.addPass(AlwaysInlinerPass(/*InsertLifetimeIntrinsics=*/true));

  MPM.addPass(buildInlinerPipeline(Level, Phase));

  // Remove any dead arguments exposed by cleanups, constant folding globals,
  // and argument promotion.
  MPM.addPass(DeadArgumentEliminationPass());

  // Optimize globals now that functions are fully simplified.
  // NeverC: the second GlobalOpt (after Inliner) is recaptured per-partition
  // by the parallel post-link pipeline.  Skip when NevercFastIPO is set.
  if (!PTO.NevercFastIPO)
    MPM.addPass(GlobalOptPass());
  MPM.addPass(GlobalDCEPass());

  return MPM;
}

/// TODO: Should LTO cause any differences to this set of passes?
void PassBuilder::addVectorPasses(OptimizationLevel Level,
                                  FunctionPassManager &FPM, bool IsFullLTO) {
  // Disable vector passes on windows.
  if (TM->getTargetTriple().isOSWindows())
    return;

  FPM.addPass(LoopVectorizePass(
      LoopVectorizeOptions(!PTO.LoopInterleaving, !PTO.LoopVectorization)));

  if (EnableInferAlignmentPass)
    FPM.addPass(InferAlignmentPass());
  if (IsFullLTO) {
    // The vectorizer may have significantly shortened a loop body; unroll
    // again. Unroll small loops to hide loop backedge latency and saturate any
    // parallel execution resources of an out-of-order processor. We also then
    // need to clean up redundancies and loop invariant code.
    // FIXME: It would be really good to use a loop-integrated instruction
    // combiner for cleanup here so that the unrolling and LICM can be pipelined
    // across the loop nests.
    FPM.addPass(LoopUnrollPass(LoopUnrollOptions(
        Level.getSpeedupLevel(), /*OnlyWhenForced=*/!PTO.LoopUnrolling,
        PTO.ForgetAllSCEVInLoopUnroll)));
    if (Level == OptimizationLevel::O3)
      FPM.addPass(WarnMissedTransformationsPass());
    FPM.addPass(SROAPass(SROAOptions::PreserveCFG));
  }

  if (!IsFullLTO && Level == OptimizationLevel::O3) {
    // Eliminate loads by forwarding stores from the previous iteration to loads
    // of the current iteration.
    FPM.addPass(LoopLoadEliminationPass());
  }
  // Cleanup after the loop optimization passes. At O2, defer to the final
  // InstCombine after LoopUnroll+SROA which handles all remaining cleanup.
  if (Level == OptimizationLevel::O3)
    FPM.addPass(InstCombinePass());

  if (Level.getSpeedupLevel() > 1 && ExtraVectorizerPasses) {
    ExtraVectorPassManager ExtraPasses;
    // At higher optimization levels, try to clean up any runtime overlap and
    // alignment checks inserted by the vectorizer. We want to track correlated
    // runtime checks for two inner loops in the same outer loop, fold any
    // common computations, hoist loop-invariant aspects out of any outer loop,
    // and unswitch the runtime checks if possible. Once hoisted, we may have
    // dead (or speculatable) control flows or more combining opportunities.
    ExtraPasses.addPass(EarlyCSEPass());
    ExtraPasses.addPass(CorrelatedValuePropagationPass());
    ExtraPasses.addPass(InstCombinePass());
    LoopPassManager LPM;
    LPM.addPass(LICMPass(PTO.LicmMssaOptCap, PTO.LicmMssaNoAccForPromotionCap,
                         /*AllowSpeculation=*/true));
    LPM.addPass(SimpleLoopUnswitchPass(/* NonTrivial */ Level ==
                                       OptimizationLevel::O3));
    ExtraPasses.addPass(
        createFunctionToLoopPassAdaptor(std::move(LPM), /*UseMemorySSA=*/true,
                                        /*UseBlockFrequencyInfo=*/true));
    ExtraPasses.addPass(
        SimplifyCFGPass(SimplifyCFGOptions().convertSwitchRangeToICmp(true)));
    ExtraPasses.addPass(InstCombinePass());
    FPM.addPass(std::move(ExtraPasses));
  }

  // Now that we've formed fast to execute loop structures, we do further
  // optimizations. These are run afterward as they might block doing complex
  // analyses and transforms such as what are needed for loop vectorization.

  // Cleanup after loop vectorization, etc. Simplification passes like CVP and
  // GVN, loop transforms, and others have already run, so it's now better to
  // convert to more optimized IR using more aggressive simplify CFG options.
  // The extra sinking transform can create larger basic blocks, so do this
  // before SLP vectorization.
  {
    auto VecCFGOpts = SimplifyCFGOptions()
                          .forwardSwitchCondToPhi(true)
                          .convertSwitchRangeToICmp(true)
                          .convertSwitchToLookupTable(true)
                          .needCanonicalLoops(false);
    if (Level == OptimizationLevel::O3)
      VecCFGOpts.hoistCommonInsts(true).sinkCommonInsts(true);
    FPM.addPass(SimplifyCFGPass(VecCFGOpts));
  }

  if (IsFullLTO) {
    FPM.addPass(SCCPPass());
    FPM.addPass(InstCombinePass());
    FPM.addPass(BDCEPass());
  }

  // Optimize parallel scalar instruction chains into SIMD instructions.
  if (PTO.SLPVectorization) {
    FPM.addPass(SLPVectorizerPass());
    if (Level.getSpeedupLevel() > 1 && ExtraVectorizerPasses) {
      FPM.addPass(EarlyCSEPass());
    }
  }
  // Enhance/cleanup vector code. At O2, defer VectorCombine since the
  // vectorizer produces minimal changes for typical scalar C code.
  if (Level == OptimizationLevel::O3 || IsFullLTO)
    FPM.addPass(VectorCombinePass());

  if (!IsFullLTO) {
    // At O2, defer this InstCombine; the final one after LoopUnroll+SROA
    // handles all cleanup. At O3, keep both for thorough optimization.
    if (Level == OptimizationLevel::O3)
      FPM.addPass(InstCombinePass());
    FPM.addPass(LoopUnrollPass(LoopUnrollOptions(
        Level.getSpeedupLevel(), /*OnlyWhenForced=*/!PTO.LoopUnrolling,
        PTO.ForgetAllSCEVInLoopUnroll)));
    if (Level == OptimizationLevel::O3)
      FPM.addPass(WarnMissedTransformationsPass());
    FPM.addPass(SROAPass(SROAOptions::PreserveCFG));
  }

  if (EnableInferAlignmentPass)
    FPM.addPass(InferAlignmentPass());
  FPM.addPass(InstCombinePass());

  // This is needed for two reasons:
  //   1. It works around problems that instcombine introduces, such as sinking
  //      expensive FP divides into loops containing multiplications using the
  //      divide result.
  //   2. It helps to clean up some loop-invariant code created by the loop
  //      unroll pass when IsFullLTO=false.
  FPM.addPass(createFunctionToLoopPassAdaptor(
      LICMPass(PTO.LicmMssaOptCap, PTO.LicmMssaNoAccForPromotionCap,
               /*AllowSpeculation=*/true),
      /*UseMemorySSA=*/true, /*UseBlockFrequencyInfo=*/false));

  // Now that we've vectorized and unrolled loops, we may have more refined
  // alignment information, try to re-derive it here.
  if (Level == OptimizationLevel::O3)
    FPM.addPass(AlignmentFromAssumptionsPass());
}

ModulePassManager
PassBuilder::buildModuleOptimizationPipeline(OptimizationLevel Level,
                                             ThinOrFullLTOPhase LTOPhase) {
  const bool LTOPreLink = isLTOPreLink(LTOPhase);
  ModulePassManager MPM;

  // Remove avail extern fns and globals definitions since we aren't compiling
  // an object file for later LTO. For LTO we want to preserve these so they
  // are eligible for inlining at link-time. Note if they are unreferenced they
  // will be removed by GlobalDCE later, so this only impacts referenced
  // available externally globals. Eventually they will be suppressed during
  // codegen, but eliminating here enables more opportunity for GlobalDCE as it
  // may make globals referenced by available external functions dead and saves
  // running remaining passes on the eliminated functions. These should be
  // preserved during prelinking for link-time inlining decisions.
  //
  // NeverC: in the LTO ParallelOpt path this pipeline is invoked once per
  // partition (typically 16) on partitions whose cross-partition function
  // bodies have been stripped to declarations.  The simplification pipeline
  // already ran EliminateAvailableExternallyPass implicitly via GlobalDCE on
  // the merged module before partitioning, so the per-partition rerun is
  // redundant and only adds wall time to the critical path.  Skip when
  // PTO.NevercFastIPO=true.
  if (!LTOPreLink && !PTO.NevercFastIPO)
    MPM.addPass(EliminateAvailableExternallyPass());

  // Do RPO function attribute inference across the module to forward-propagate
  // attributes where applicable.
  // FIXME: Is this really an optimization rather than a canonicalization?
  //
  // NeverC: RPO function attribute propagation walks the entire callgraph.
  // On a stripped-down partition the callgraph view is different from the
  // merged module that the simplification pipeline already ran
  // PostOrderFunctionAttrs on, so the propagation done here is only
  // partition-local.  This pass is reported as ~47% of the per-partition
  // wall time on Redis-sized partitions; skipping it shaves ~25ms off the
  // phase-2 critical path with no measurable effect on generated code
  // (the per-function attributes were already inferred during simplification).
  if (!PTO.NevercFastIPO)
    MPM.addPass(ReversePostOrderFunctionAttrsPass());

  // Do a post inline PGO instrumentation and use pass. This is a context
  // sensitive PGO pass. We don't want to do this in LTOPreLink phrase as
  // cross-module inline has not been done yet. The context sensitive
  // instrumentation is after all the inlines are done.
  if (!LTOPreLink && PGOOpt) {
    if (PGOOpt->CSAction == PGOOptions::CSIRInstr)
      addPGOInstrPasses(MPM, Level, /*RunProfileGen=*/true,
                        /*IsCS=*/true, PGOOpt->AtomicCounterUpdate,
                        PGOOpt->CSProfileGenFile, PGOOpt->FS);
    else if (PGOOpt->CSAction == PGOOptions::CSIRUse)
      addPGOInstrPasses(MPM, Level, /*RunProfileGen=*/false,
                        /*IsCS=*/true, PGOOpt->AtomicCounterUpdate,
                        PGOOpt->ProfileFile, PGOOpt->FS);
  }

  // Re-compute GlobalsAA here prior to function passes. This is particularly
  // useful as the above will have inlined, DCE'ed, and function-attr
  // propagated everything. We should at this point have a reasonably minimal
  // and richly annotated call graph. By computing aliasing and mod/ref
  // information for all local globals here, the late loop passes and notably
  // the vectorizer will be able to use them to help recognize vectorizable
  // memory operations.
  //
  // NeverC: in the LTO ParallelOpt path, GlobalsAA was already computed and
  // used during the merged-module simplification pipeline; partition-local
  // recomputation only sees the partition's restricted callgraph and offers
  // little additional benefit for our scalar-c-heavy workloads.
  if (EnableGlobalAnalyses && !PTO.NevercFastIPO)
    MPM.addPass(RecomputeGlobalsAAPass());

  invokeOptimizerEarlyEPCallbacks(MPM, Level);

  FunctionPassManager OptimizePM;
  if (Level == OptimizationLevel::O3)
    OptimizePM.addPass(Float2IntPass());
  OptimizePM.addPass(LowerConstantIntrinsicsPass());

  // NeverC: in the LTO ParallelOpt path the simplification pipeline skipped
  // GVN inside the inliner's per-SCC function simplification to keep the
  // serial critical path short.  Run GVN here (per partition, in parallel)
  // before the vectorizer kicks in so that redundancy elimination still
  // benefits loop vectorization / SLP without burning serial wall time.
  if (PTO.NevercFastIPO && Level.getSpeedupLevel() >= 2)
    OptimizePM.addPass(GVNPass());

  // FIXME: We need to run some loop optimizations to re-rotate loops after
  // simplifycfg and others undo their rotation.

  // Optimize the loop execution. These passes operate on entire loop nests
  // rather than on each loop in an inside-out manner, and so they are actually
  // function passes.

  invokeVectorizerStartEPCallbacks(OptimizePM, Level);

  LoopPassManager LPM;
  // First rotate loops that may have been un-rotated by prior passes.
  // Disable header duplication at -Oz.
  LPM.addPass(LoopRotatePass(Level != OptimizationLevel::Oz, LTOPreLink));
  // Some loops may have become dead by now. Try to delete them.
  // FIXME: see discussion in https://reviews.llvm.org/D112851,
  //        this may need to be revisited once we run GVN before loop deletion
  //        in the simplification pipeline.
  LPM.addPass(LoopDeletionPass());
  OptimizePM.addPass(createFunctionToLoopPassAdaptor(
      std::move(LPM), /*UseMemorySSA=*/false, /*UseBlockFrequencyInfo=*/false));

  if (!TM || !TM->getTargetTriple().isOSWindows()) {
    if (Level == OptimizationLevel::O3)
      OptimizePM.addPass(LoopDistributePass());
    OptimizePM.addPass(InjectTLIMappings());
    addVectorPasses(Level, OptimizePM, /* IsFullLTO */ false);
  }

  // LoopSink pass sinks instructions hoisted by LICM, which serves as a
  // canonicalization pass that enables other optimizations. As a result,
  // LoopSink pass needs to be a very late IR pass to avoid undoing LICM
  // result too early.
  if (Level == OptimizationLevel::O3)
    OptimizePM.addPass(LoopSinkPass());

  // And finally clean up LCSSA form before generating code.
  OptimizePM.addPass(InstSimplifyPass());

  // This hoists/decomposes div/rem ops. It should run after other sink/hoist
  // passes to avoid re-sinking, but before SimplifyCFG because it can allow
  // flattening of blocks.
  if (Level == OptimizationLevel::O3)
    OptimizePM.addPass(DivRemPairsPass());

  // Try to annotate calls that were created during optimization.
  // At O2, TailCallElim already ran in function simplification; the late
  // re-run primarily catches calls created by vectorization/unrolling.
  if (Level == OptimizationLevel::O3)
    OptimizePM.addPass(TailCallElimPass());

  // LoopSink (and other loop passes since the last simplifyCFG) might have
  // resulted in single-entry-single-exit or empty blocks. Clean up the CFG.
  OptimizePM.addPass(
      SimplifyCFGPass(SimplifyCFGOptions().convertSwitchRangeToICmp(true)));

  // Add the core optimizing pipeline.
  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(OptimizePM),
                                                PTO.EagerlyInvalidateAnalyses));

  invokeOptimizerLastEPCallbacks(MPM, Level);

  // Now we need to do some global optimization transforms.
  // FIXME: It would seem like these should come first in the optimization
  // pipeline and maybe be the bottom of the canonicalization pipeline? Weird
  // ordering here.
  MPM.addPass(GlobalDCEPass());
  MPM.addPass(ConstantMergePass());

  if (PTO.CallGraphProfile && !LTOPreLink)
    MPM.addPass(CGProfilePass());

  // TODO: Relative look table converter pass caused an issue when full lto is
  // enabled. See https://reviews.llvm.org/D94355 for more details.
  // Until the issue fixed, disable this pass during pre-linking phase.
  if (!LTOPreLink)
    MPM.addPass(RelLookupTableConverterPass());

  return MPM;
}

ModulePassManager
PassBuilder::buildPerModuleDefaultPipeline(OptimizationLevel Level,
                                           bool LTOPreLink) {
  if (Level == OptimizationLevel::O0)
    return buildO0DefaultPipeline(Level, LTOPreLink);

  ModulePassManager MPM;

  // Force any function attributes we want the rest of the pipeline to observe.
  MPM.addPass(ForceFunctionAttrsPass());

  // Apply module pipeline start EP callback.
  invokePipelineStartEPCallbacks(MPM, Level);

  const ThinOrFullLTOPhase LTOPhase = LTOPreLink
                                          ? ThinOrFullLTOPhase::FullLTOPreLink
                                          : ThinOrFullLTOPhase::None;
  // Add the core simplification pipeline.
  MPM.addPass(buildModuleSimplificationPipeline(Level, LTOPhase));

  // Now add the optimization pipeline.
  MPM.addPass(buildModuleOptimizationPipeline(Level, LTOPhase));

  // Emit annotation remarks.
  addAnnotationRemarksPass(MPM);

  if (LTOPreLink)
    addRequiredLTOPreLinkPasses(MPM);
  return MPM;
}

ModulePassManager
PassBuilder::buildFatLTODefaultPipeline(OptimizationLevel Level) {
  ModulePassManager MPM;
  MPM.addPass(buildLTOPreLinkDefaultPipeline(Level));
  MPM.addPass(EmbedBitcodePass());

  MPM.addPass(buildModuleOptimizationPipeline(Level, ThinOrFullLTOPhase::None));
  addAnnotationRemarksPass(MPM);
  return MPM;
}

ModulePassManager
PassBuilder::buildLTOPreLinkDefaultPipeline(OptimizationLevel Level) {
  // FIXME: We should use a customized pre-link pipeline!
  return buildPerModuleDefaultPipeline(Level,
                                       /* LTOPreLink */ true);
}

ModulePassManager
PassBuilder::buildLTODefaultPipeline(OptimizationLevel Level,
                                     ModuleSummaryIndex *ExportSummary) {
  ModulePassManager MPM;

  invokeFullLinkTimeOptimizationEarlyEPCallbacks(MPM, Level);

  if (Level == OptimizationLevel::O0) {
    // The LowerTypeTest pass needs to run at -O0 to lower type metadata and
    // intrinsics.
    MPM.addPass(LowerTypeTestsPass(ExportSummary, nullptr));
    // Run a second time to clean up any type tests left behind for use in ICP.
    MPM.addPass(LowerTypeTestsPass(nullptr, nullptr, true));

    invokeFullLinkTimeOptimizationLastEPCallbacks(MPM, Level);

    // Emit annotation remarks.
    addAnnotationRemarksPass(MPM);

    return MPM;
  }

  // Remove unused virtual tables to improve the quality of code generated by
  // whole-program devirtualization and bitset lowering.
  MPM.addPass(GlobalDCEPass(/*InLTOPostLink=*/true));

  // Do basic inference of function attributes from known properties of system
  // libraries and other oracles.
  MPM.addPass(InferFunctionAttrsPass());

  if (Level.getSpeedupLevel() > 1) {
    MPM.addPass(createModuleToFunctionPassAdaptor(
        CallSiteSplittingPass(), PTO.EagerlyInvalidateAnalyses));

    // Indirect call promotion. This should promote all the targets that are
    // left by the earlier promotion pass that promotes intra-module targets.
    // This two-step promotion is to save the compile time. For LTO, it should
    // produce the same result as if we only do promotion here.
    MPM.addPass(PGOIndirectCallPromotion(
        true /* InLTO */, PGOOpt && PGOOpt->Action == PGOOptions::SampleUse));

    // Propagate constants at call sites into the functions they call.  This
    // opens opportunities for globalopt (and inlining) by substituting function
    // pointers passed as arguments to direct uses of functions.
    MPM.addPass(IPSCCPPass());

    // Attach metadata to indirect call sites indicating the set of functions
    // they may target at run-time. This should follow IPSCCP.
    MPM.addPass(CalledValuePropagationPass());
  }

  // Now deduce any function attributes based in the current code.
  MPM.addPass(
      createModuleToPostOrderCGSCCPassAdaptor(PostOrderFunctionAttrsPass()));

  // Do RPO function attribute inference across the module to forward-propagate
  // attributes where applicable.
  // FIXME: Is this really an optimization rather than a canonicalization?
  MPM.addPass(ReversePostOrderFunctionAttrsPass());

  // Stop here at -O1.
  if (Level == OptimizationLevel::O1) {
    // The LowerTypeTestsPass needs to run to lower type metadata and the
    // type.test intrinsics. The pass does nothing if CFI is disabled.
    MPM.addPass(LowerTypeTestsPass(ExportSummary, nullptr));
    // Run a second time to clean up any type tests left behind by WPD for use
    // in ICP (which is performed earlier than this in the regular LTO
    // pipeline).
    MPM.addPass(LowerTypeTestsPass(nullptr, nullptr, true));

    invokeFullLinkTimeOptimizationLastEPCallbacks(MPM, Level);

    // Emit annotation remarks.
    addAnnotationRemarksPass(MPM);

    return MPM;
  }

  // Optimize globals to try and fold them into constants.
  MPM.addPass(GlobalOptPass());

  // Promote any localized globals to SSA registers.
  MPM.addPass(createModuleToFunctionPassAdaptor(PromotePass()));

  // Linking modules together can lead to duplicate global constant, only
  // keep one copy of each constant.
  MPM.addPass(ConstantMergePass());

  // Remove unused arguments from functions.
  MPM.addPass(DeadArgumentEliminationPass());

  // Reduce the code after globalopt and ipsccp.  Both can open up significant
  // simplification opportunities, and both can propagate functions through
  // function pointers.  When this happens, we often have to resolve varargs
  // calls, etc, so let instcombine do this.
  FunctionPassManager PeepholeFPM;
  PeepholeFPM.addPass(InstCombinePass());
  if (Level.getSpeedupLevel() > 1)
    PeepholeFPM.addPass(AggressiveInstCombinePass());
  invokePeepholeEPCallbacks(PeepholeFPM, Level);

  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(PeepholeFPM),
                                                PTO.EagerlyInvalidateAnalyses));

  // Note: historically, the PruneEH pass was run first to deduce nounwind and
  // generally clean up exception handling overhead. It isn't clear this is
  // valuable as the inliner doesn't currently care whether it is inlining an
  // invoke or a call.
  // Run the inliner now.
  MPM.addPass(ModuleInlinerWrapperPass(
      getInlineParamsFromOptLevel(Level),
      /* MandatoryFirst */ true,
      InlineContext{ThinOrFullLTOPhase::FullLTOPostLink,
                    InlinePass::CGSCCInliner}));

  // Perform context disambiguation after inlining, since that would reduce the
  // amount of additional cloning required to distinguish the allocation
  // contexts.

  // Optimize globals again after we ran the inliner.
  MPM.addPass(GlobalOptPass());

  // Garbage collect dead functions.
  MPM.addPass(GlobalDCEPass(/*InLTOPostLink=*/true));

  // If we didn't decide to inline a function, check to see if we can
  // transform it to pass arguments by value instead of by reference.
  MPM.addPass(createModuleToPostOrderCGSCCPassAdaptor(ArgumentPromotionPass()));

  FunctionPassManager FPM;
  // The IPO Passes may leave cruft around. Clean up after them.
  FPM.addPass(InstCombinePass());
  invokePeepholeEPCallbacks(FPM, Level);

  if (EnableConstraintElimination)
    FPM.addPass(ConstraintEliminationPass());

  FPM.addPass(JumpThreadingPass());

  // Do a post inline PGO instrumentation and use pass. This is a context
  // sensitive PGO pass.
  if (PGOOpt) {
    if (PGOOpt->CSAction == PGOOptions::CSIRInstr)
      addPGOInstrPasses(MPM, Level, /*RunProfileGen=*/true,
                        /*IsCS=*/true, PGOOpt->AtomicCounterUpdate,
                        PGOOpt->CSProfileGenFile, PGOOpt->FS);
    else if (PGOOpt->CSAction == PGOOptions::CSIRUse)
      addPGOInstrPasses(MPM, Level, /*RunProfileGen=*/false,
                        /*IsCS=*/true, PGOOpt->AtomicCounterUpdate,
                        PGOOpt->ProfileFile, PGOOpt->FS);
  }

  // Break up allocas
  FPM.addPass(SROAPass(SROAOptions::ModifyCFG));

  // LTO provides additional opportunities for tailcall elimination due to
  // link-time inlining, and visibility of nocapture attribute.
  FPM.addPass(TailCallElimPass());

  // Run a few AA driver optimizations here and now to cleanup the code.
  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM),
                                                PTO.EagerlyInvalidateAnalyses));

  MPM.addPass(
      createModuleToPostOrderCGSCCPassAdaptor(PostOrderFunctionAttrsPass()));

  // Require the GlobalsAA analysis for the module so we can query it within
  // MainFPM.
  if (EnableGlobalAnalyses) {
    MPM.addPass(RequireAnalysisPass<GlobalsAA, Module>());
    // Invalidate AAManager so it can be recreated and pick up the newly
    // available GlobalsAA.
    MPM.addPass(
        createModuleToFunctionPassAdaptor(InvalidateAnalysisPass<AAManager>()));
  }

  FunctionPassManager MainFPM;
  MainFPM.addPass(createFunctionToLoopPassAdaptor(
      LICMPass(PTO.LicmMssaOptCap, PTO.LicmMssaNoAccForPromotionCap,
               /*AllowSpeculation=*/true),
      /*USeMemorySSA=*/true, /*UseBlockFrequencyInfo=*/false));

  MainFPM.addPass(GVNPass());

  // Remove dead memcpy()'s.
  MainFPM.addPass(MemCpyOptPass());

  // Nuke dead stores.
  MainFPM.addPass(DSEPass());
  MainFPM.addPass(MoveAutoInitPass());
  MainFPM.addPass(MergedLoadStoreMotionPass());

  LoopPassManager LPM;
  LPM.addPass(IndVarSimplifyPass());
  LPM.addPass(LoopDeletionPass());
  // FIXME: Add loop interchange.

  // Unroll small loops and perform peeling.
  LPM.addPass(LoopFullUnrollPass(Level.getSpeedupLevel(),
                                 /* OnlyWhenForced= */ !PTO.LoopUnrolling,
                                 PTO.ForgetAllSCEVInLoopUnroll));
  // The loop passes in LPM (LoopFullUnrollPass) do not preserve MemorySSA.
  // *All* loop passes must preserve it, in order to be able to use it.
  MainFPM.addPass(createFunctionToLoopPassAdaptor(
      std::move(LPM), /*UseMemorySSA=*/false, /*UseBlockFrequencyInfo=*/true));

  if (!TM || !TM->getTargetTriple().isOSWindows()) {
    MainFPM.addPass(LoopDistributePass());
    addVectorPasses(Level, MainFPM, /* IsFullLTO */ true);
  }

  invokePeepholeEPCallbacks(MainFPM, Level);
  MainFPM.addPass(JumpThreadingPass());
  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(MainFPM),
                                                PTO.EagerlyInvalidateAnalyses));

  // Lower type metadata and the type.test intrinsic. This pass supports
  // NeverC's control flow integrity mechanisms (-fsanitize=cfi*) and needs
  // to be run at link time if CFI is enabled. This pass does nothing if
  // CFI is disabled.
  MPM.addPass(LowerTypeTestsPass(ExportSummary, nullptr));
  // Run a second time to clean up any type tests left behind by WPD for use
  // in ICP (which is performed earlier than this in the regular LTO pipeline).
  MPM.addPass(LowerTypeTestsPass(nullptr, nullptr, true));

  // Add late LTO optimization passes.
  FunctionPassManager LateFPM;

  // LoopSink pass sinks instructions hoisted by LICM, which serves as a
  // canonicalization pass that enables other optimizations. As a result,
  // LoopSink pass needs to be a very late IR pass to avoid undoing LICM
  // result too early.
  LateFPM.addPass(LoopSinkPass());

  // This hoists/decomposes div/rem ops. It should run after other sink/hoist
  // passes to avoid re-sinking, but before SimplifyCFG because it can allow
  // flattening of blocks.
  LateFPM.addPass(DivRemPairsPass());

  // Delete basic blocks, which optimization passes may have killed.
  LateFPM.addPass(SimplifyCFGPass(
      SimplifyCFGOptions().convertSwitchRangeToICmp(true).hoistCommonInsts(
          true)));
  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(LateFPM)));

  // Drop bodies of available eternally objects to improve GlobalDCE.
  MPM.addPass(EliminateAvailableExternallyPass());

  // Now that we have optimized the program, discard unreachable functions.
  MPM.addPass(GlobalDCEPass(/*InLTOPostLink=*/true));

  if (PTO.CallGraphProfile)
    MPM.addPass(CGProfilePass());

  invokeFullLinkTimeOptimizationLastEPCallbacks(MPM, Level);

  // Emit annotation remarks.
  addAnnotationRemarksPass(MPM);

  return MPM;
}

ModulePassManager PassBuilder::buildO0DefaultPipeline(OptimizationLevel Level,
                                                      bool LTOPreLink) {
  assert(Level == OptimizationLevel::O0 &&
         "buildO0DefaultPipeline should only be used with O0");

  ModulePassManager MPM;

  // Perform pseudo probe instrumentation in O0 mode. This is for the
  // consistency between different build modes. For example, a LTO build can be
  // mixed with an O0 prelink and an O2 postlink. Loading a sample profile in
  // the postlink will require pseudo probe instrumentation in the prelink.

  if (PGOOpt && (PGOOpt->Action == PGOOptions::IRInstr ||
                 PGOOpt->Action == PGOOptions::IRUse))
    addPGOInstrPassesForO0(
        MPM,
        /*RunProfileGen=*/(PGOOpt->Action == PGOOptions::IRInstr),
        /*IsCS=*/false, PGOOpt->AtomicCounterUpdate, PGOOpt->ProfileFile,
        PGOOpt->FS);

  invokePipelineStartEPCallbacks(MPM, Level);

  invokePipelineEarlySimplificationEPCallbacks(MPM, Level);

  // Build a minimal pipeline based on the semantics required by LLVM,
  // which is just that always inlining occurs. Further, disable generating
  // lifetime intrinsics to avoid enabling further optimizations during
  // code generation.
  MPM.addPass(AlwaysInlinerPass(
      /*InsertLifetimeIntrinsics=*/false));

  if (!CGSCCOptimizerLateEPCallbacks.empty()) {
    CGSCCPassManager CGPM;
    invokeCGSCCOptimizerLateEPCallbacks(CGPM, Level);
    if (!CGPM.isEmpty())
      MPM.addPass(createModuleToPostOrderCGSCCPassAdaptor(std::move(CGPM)));
  }
  if (!LateLoopOptimizationsEPCallbacks.empty()) {
    LoopPassManager LPM;
    invokeLateLoopOptimizationsEPCallbacks(LPM, Level);
    if (!LPM.isEmpty()) {
      MPM.addPass(createModuleToFunctionPassAdaptor(
          createFunctionToLoopPassAdaptor(std::move(LPM))));
    }
  }
  if (!LoopOptimizerEndEPCallbacks.empty()) {
    LoopPassManager LPM;
    invokeLoopOptimizerEndEPCallbacks(LPM, Level);
    if (!LPM.isEmpty()) {
      MPM.addPass(createModuleToFunctionPassAdaptor(
          createFunctionToLoopPassAdaptor(std::move(LPM))));
    }
  }
  if (!ScalarOptimizerLateEPCallbacks.empty()) {
    FunctionPassManager FPM;
    invokeScalarOptimizerLateEPCallbacks(FPM, Level);
    if (!FPM.isEmpty())
      MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
  }

  invokeOptimizerEarlyEPCallbacks(MPM, Level);

  if (!VectorizerStartEPCallbacks.empty()) {
    FunctionPassManager FPM;
    invokeVectorizerStartEPCallbacks(FPM, Level);
    if (!FPM.isEmpty())
      MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
  }

  invokeOptimizerLastEPCallbacks(MPM, Level);

  if (LTOPreLink)
    addRequiredLTOPreLinkPasses(MPM);

  MPM.addPass(createModuleToFunctionPassAdaptor(AnnotationRemarksPass()));

  return MPM;
}

AAManager PassBuilder::buildDefaultAAPipeline() {
  AAManager AA;

  // The order in which these are registered determines their priority when
  // being queried.

  // First we register the basic alias analysis that provides the majority of
  // per-function local AA logic. This is a stateless, on-demand local set of
  // AA techniques.
  AA.registerFunctionAnalysis<BasicAA>();

  // Next we query fast, specialized alias analyses that wrap IR-embedded
  // information about aliasing.
  AA.registerFunctionAnalysis<ScopedNoAliasAA>();
  AA.registerFunctionAnalysis<TypeBasedAA>();

  // Add support for querying global aliasing information when available.
  // Because the `AAManager` is a function analysis and `GlobalsAA` is a module
  // analysis, all that the `AAManager` can do is query for any *cached*
  // results from `GlobalsAA` through a readonly proxy.
  if (EnableGlobalAnalyses)
    AA.registerModuleAnalysis<GlobalsAA>();

  // Add target-specific alias analyses.
  if (TM)
    TM->registerDefaultAliasAnalyses(AA);

  return AA;
}
