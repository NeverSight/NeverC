//===- llvm/InitializePasses.h - Initialize All Passes ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations for the pass initialization routines
// for the entire LLVM project.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_INITIALIZEPASSES_H
#define LLVM_INITIALIZEPASSES_H

namespace llvm {

class PassRegistry;

/// Initialize all passes linked into the CodeGen library.
void initializeCodeGen(PassRegistry &);

/// Initialize all passes linked into the GlobalISel library.
void initializeGlobalISel(PassRegistry &);

/// Initialize all passes linked into the CodeGen library.

void initializeAAResultsWrapperPassPass(PassRegistry &);
void initializeAssignmentTrackingAnalysisPass(PassRegistry &);
void initializeAssumptionCacheTrackerPass(PassRegistry &);
void initializeAtomicExpandPass(PassRegistry &);
void initializeBasicBlockPathCloningPass(PassRegistry &);
void initializeBasicBlockSectionsProfileReaderPass(PassRegistry &);
void initializeBasicBlockSectionsPass(PassRegistry &);
void initializeBasicAAWrapperPassPass(PassRegistry &);
void initializeBlockFrequencyInfoWrapperPassPass(PassRegistry &);
void initializeBranchFolderPassPass(PassRegistry &);
void initializeBranchProbabilityInfoWrapperPassPass(PassRegistry &);
void initializeBranchRelaxationPass(PassRegistry &);
void initializeBreakCriticalEdgesPass(PassRegistry &);
void initializeBreakFalseDepsPass(PassRegistry &);
void initializeCanonicalizeFreezeInLoopsPass(PassRegistry &);
void initializeCFGSimplifyPassPass(PassRegistry &);
void initializeCFGuardPass(PassRegistry &);
void initializeCFGuardLongjmpPass(PassRegistry &);
void initializeCFIFixupPass(PassRegistry &);
void initializeCFIInstrInserterPass(PassRegistry &);
void initializeCallBrPreparePass(PassRegistry &);
void initializeCallGraphWrapperPassPass(PassRegistry &);
void initializeCodeGenPreparePass(PassRegistry &);
void initializeComplexDeinterleavingLegacyPassPass(PassRegistry &);
void initializeConstantHoistingLegacyPassPass(PassRegistry &);
void initializeDeadMachineInstructionElimPass(PassRegistry &);
void initializeDetectDeadLanesPass(PassRegistry &);
void initializeDominanceFrontierWrapperPassPass(PassRegistry &);
void initializeDominatorTreeWrapperPassPass(PassRegistry &);
void initializeDwarfEHPrepareLegacyPassPass(PassRegistry &);
void initializeEarlyCSELegacyPassPass(PassRegistry &);
void initializeEarlyCSEMemSSALegacyPassPass(PassRegistry &);
void initializeEarlyIfConverterPass(PassRegistry &);
void initializeEarlyMachineLICMPass(PassRegistry &);
void initializeEarlyTailDuplicatePass(PassRegistry &);
void initializeEdgeBundlesPass(PassRegistry &);
void initializeEHContGuardCatchretPass(PassRegistry &);
void initializeExpandLargeFpConvertLegacyPassPass(PassRegistry &);
void initializeExpandLargeDivRemLegacyPassPass(PassRegistry &);
void initializeExpandMemCmpLegacyPassPass(PassRegistry &);
void initializeExpandPostRAPass(PassRegistry &);
void initializeExpandReductionsPass(PassRegistry &);
void initializeExternalAAWrapperPassPass(PassRegistry &);
void initializeFEntryInserterPass(PassRegistry &);
void initializeFinalizeISelPass(PassRegistry &);
void initializeFuncletLayoutPass(PassRegistry &);
void initializeGCModuleInfoPass(PassRegistry &);
void initializeGlobalMergePass(PassRegistry &);
void initializeGlobalsAAWrapperPassPass(PassRegistry &);
void initializeIRTranslatorPass(PassRegistry &);
void initializeIVUsersWrapperPassPass(PassRegistry &);
void initializeIfConverterPass(PassRegistry &);
void initializeImmutableModuleSummaryIndexWrapperPassPass(PassRegistry &);
void initializeIndirectBrExpandLegacyPassPass(PassRegistry &);
void initializeInstructionCombiningPassPass(PassRegistry &);
void initializeInstructionSelectPass(PassRegistry &);
void initializeInterleavedAccessPass(PassRegistry &);
void initializeJMCInstrumenterPass(PassRegistry &);
void initializeLCSSAVerificationPassPass(PassRegistry &);
void initializeLCSSAWrapperPassPass(PassRegistry &);
void initializeLazyBlockFrequencyInfoPassPass(PassRegistry &);
void initializeLazyBranchProbabilityInfoPassPass(PassRegistry &);
void initializeLazyMachineBlockFrequencyInfoPassPass(PassRegistry &);
void initializeLazyValueInfoWrapperPassPass(PassRegistry &);
void initializeLegacyLICMPassPass(PassRegistry &);
void initializeLegalizerPass(PassRegistry &);
void initializeGISelCSEAnalysisWrapperPassPass(PassRegistry &);
void initializeGISelKnownBitsAnalysisPass(PassRegistry &);
void initializeLiveDebugValuesPass(PassRegistry &);
void initializeLiveDebugVariablesPass(PassRegistry &);
void initializeLiveIntervalsPass(PassRegistry &);
void initializeLiveRangeShrinkPass(PassRegistry &);
void initializeLiveRegMatrixPass(PassRegistry &);
void initializeLiveStacksPass(PassRegistry &);
void initializeLiveVariablesPass(PassRegistry &);
void initializeLoadStoreOptPass(PassRegistry &);
void initializeLocalStackSlotPassPass(PassRegistry &);
void initializeLocalizerPass(PassRegistry &);
void initializeLoopDataPrefetchLegacyPassPass(PassRegistry &);
void initializeLoopInfoWrapperPassPass(PassRegistry &);
void initializeLoopPassPass(PassRegistry &);
void initializeLoopSimplifyPass(PassRegistry &);
void initializeLoopStrengthReducePass(PassRegistry &);
void initializeLowerConstantIntrinsicsPass(PassRegistry &);
void initializeLowerEmuTLSPass(PassRegistry &);
void initializeLowerGlobalDtorsLegacyPassPass(PassRegistry &);
void initializeLowerInvokeLegacyPassPass(PassRegistry &);
void initializeMachineBlockFrequencyInfoPass(PassRegistry &);
void initializeMachineBlockPlacementPass(PassRegistry &);
void initializeMachineBranchProbabilityInfoPass(PassRegistry &);
void initializeMachineCSEPass(PassRegistry &);
void initializeMachineCombinerPass(PassRegistry &);
void initializeMachineCopyPropagationPass(PassRegistry &);
void initializeMachineCycleInfoWrapperPassPass(PassRegistry &);
void initializeMachineDominanceFrontierPass(PassRegistry &);
void initializeMachineDominatorTreePass(PassRegistry &);
void initializeMachineFunctionSplitterPass(PassRegistry &);
void initializeMachineLateInstrsCleanupPass(PassRegistry &);
void initializeMachineLICMPass(PassRegistry &);
void initializeMachineLoopInfoPass(PassRegistry &);
void initializeMachineModuleInfoWrapperPassPass(PassRegistry &);
void initializeMachineOptimizationRemarkEmitterPassPass(PassRegistry &);
void initializeMachineOutlinerPass(PassRegistry &);
void initializeMachinePostDominatorTreePass(PassRegistry &);
void initializeMachineSchedulerPass(PassRegistry &);
void initializeMachineSinkingPass(PassRegistry &);
void initializeMachineTraceMetricsPass(PassRegistry &);
void initializeMachineUniformityAnalysisPassPass(PassRegistry &);
void initializeMachineVerifierPassPass(PassRegistry &);
void initializeMemoryDependenceWrapperPassPass(PassRegistry &);
void initializeMemorySSAWrapperPassPass(PassRegistry &);
void initializeMergeICmpsLegacyPassPass(PassRegistry &);
void initializeModuleSummaryIndexWrapperPassPass(PassRegistry &);
void initializeOptimizationRemarkEmitterWrapperPassPass(PassRegistry &);
void initializeOptimizePHIsPass(PassRegistry &);
void initializePEIPass(PassRegistry &);
void initializePHIEliminationPass(PassRegistry &);
void initializePartiallyInlineLibCallsLegacyPassPass(PassRegistry &);
void initializePatchableFunctionPass(PassRegistry &);
void initializePeepholeOptimizerPass(PassRegistry &);
void initializePostDominatorTreeWrapperPassPass(PassRegistry &);
void initializePostMachineSchedulerPass(PassRegistry &);
void initializePostRAHazardRecognizerPass(PassRegistry &);
void initializePostRAMachineSinkingPass(PassRegistry &);
void initializePostRASchedulerPass(PassRegistry &);
void initializePreISelIntrinsicLoweringLegacyPassPass(PassRegistry &);
void initializePrintFunctionPassWrapperPass(PassRegistry &);
void initializePrintModulePassWrapperPass(PassRegistry &);
void initializeProcessImplicitDefsPass(PassRegistry &);
void initializeProfileSummaryInfoWrapperPassPass(PassRegistry &);
void initializeRABasicPass(PassRegistry &);

void initializeRAGreedyPass(PassRegistry &);
void initializeReachingDefAnalysisPass(PassRegistry &);
void initializeRegAllocEvictionAdvisorAnalysisPass(PassRegistry &);
void initializeRegAllocFastPass(PassRegistry &);
void initializeRegAllocPriorityAdvisorAnalysisPass(PassRegistry &);
void initializeRegBankSelectPass(PassRegistry &);
void initializeRegisterCoalescerPass(PassRegistry &);
void initializeRemoveRedundantDebugValuesPass(PassRegistry &);
void initializeRenameIndependentSubregsPass(PassRegistry &);
void initializeReplaceWithVeclibLegacyPass(PassRegistry &);
void initializeResetMachineFunctionPass(PassRegistry &);
void initializeSCEVAAWrapperPassPass(PassRegistry &);
void initializeSelectOptimizePass(PassRegistry &);
void initializeScalarEvolutionWrapperPassPass(PassRegistry &);
void initializeScalarizeMaskedMemIntrinLegacyPassPass(PassRegistry &);
void initializeScopedNoAliasAAWrapperPassPass(PassRegistry &);
void initializeSeparateConstOffsetFromGEPLegacyPassPass(PassRegistry &);
void initializeShrinkWrapPass(PassRegistry &);
void initializeSlotIndexesPass(PassRegistry &);
void initializeSpillPlacementPass(PassRegistry &);
void initializeStackColoringPass(PassRegistry &);
void initializeStackFrameLayoutAnalysisPassPass(PassRegistry &);
void initializeStackProtectorPass(PassRegistry &);
void initializeStackSlotColoringPass(PassRegistry &);
void initializeTailDuplicatePass(PassRegistry &);
void initializeTargetLibraryInfoWrapperPassPass(PassRegistry &);
void initializeTargetPassConfigPass(PassRegistry &);
void initializeTargetTransformInfoWrapperPassPass(PassRegistry &);
void initializeTLSVariableHoistLegacyPassPass(PassRegistry &);
void initializeTwoAddressInstructionPassPass(PassRegistry &);
void initializeTypeBasedAAWrapperPassPass(PassRegistry &);
void initializeTypePromotionLegacyPass(PassRegistry &);
void initializeUnpackMachineBundlesPass(PassRegistry &);
void initializeUnreachableBlockElimLegacyPassPass(PassRegistry &);
void initializeUnreachableMachineBlockElimPass(PassRegistry &);
void initializeVerifierLegacyPassPass(PassRegistry &);
void initializeVirtRegMapPass(PassRegistry &);
void initializeVirtRegRewriterPass(PassRegistry &);
void initializeWinEHPreparePass(PassRegistry &);

} // end namespace llvm

#endif // LLVM_INITIALIZEPASSES_H
