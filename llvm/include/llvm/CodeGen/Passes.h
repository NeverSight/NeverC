//===-- Passes.h - Target independent code generation passes ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines interfaces to access the target independent code generation
// passes provided by the LLVM backend.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_PASSES_H
#define LLVM_CODEGEN_PASSES_H

#include "llvm/CodeGen/RegAllocCommon.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Discriminator.h"

#include <functional>
#include <string>

namespace llvm {

class FunctionPass;
class MachineFunction;
class MachineFunctionPass;
class ModulePass;
class Pass;
class TargetMachine;
class raw_ostream;

template <typename T> class IntrusiveRefCntPtr;
namespace vfs {
class FileSystem;
} // namespace vfs

} // namespace llvm

// List of target independent CodeGen pass IDs.
namespace llvm {

/// AtomicExpandPass - At IR level this pass replace atomic instructions with
/// __atomic_* library calls, or target specific instruction which implement the
/// same semantics in a way which better fits the target backend.
FunctionPass *createAtomicExpandPass();

/// createUnreachableBlockEliminationPass - The LLVM code generator does not
/// work well with unreachable basic blocks (what live ranges make sense for a
/// block that cannot be reached?).  As such, a code generator should either
/// not instruction select unreachable blocks, or run this pass as its
/// last LLVM modifying pass to clean up blocks that are not reachable from
/// the entry block.
FunctionPass *createUnreachableBlockEliminationPass();

/// createGCEmptyBasicblocksPass - Empty basic blocks (basic blocks without
/// real code) appear as the result of optimization passes removing
/// instructions. These blocks confuscate profile analysis (e.g., basic block
/// sections) since they will share the address of their fallthrough blocks.
/// This pass garbage-collects such basic blocks.
/// createBasicBlockSections Pass - This pass assigns sections to machine
/// basic blocks and is enabled with -fbasic-block-sections.
MachineFunctionPass *createBasicBlockSectionsPass();

MachineFunctionPass *createBasicBlockPathCloningPass();

/// createMachineFunctionSplitterPass - This pass splits machine functions
/// using profile information.
MachineFunctionPass *createMachineFunctionSplitterPass();

/// StackFramePrinter pass - This pass prints out the machine function's
/// stack frame to the given stream as a debugging tool.
MachineFunctionPass *createStackFrameLayoutAnalysisPass();

/// This pass resets a MachineFunction when it has the FailedISel property
/// as if it was just created.
/// If EmitFallbackDiag is true, the pass will emit a
/// DiagnosticInfoISelFallback for every MachineFunction it resets.
/// If AbortOnFailedISel is true, abort compilation instead of resetting.
MachineFunctionPass *createResetMachineFunctionPass(bool EmitFallbackDiag,
                                                    bool AbortOnFailedISel);

/// createCodeGenPreparePass - Transform the code to expose more pattern
/// matching during instruction selection.
FunctionPass *createCodeGenPreparePass();

/// This pass implements generation of target-specific intrinsics to support
/// handling of complex number arithmetic
FunctionPass *createComplexDeinterleavingPass(const TargetMachine *TM);

/// AtomicExpandID -- Lowers atomic operations in terms of either cmpxchg
/// load-linked/store-conditional loops.
extern char &AtomicExpandID;

/// MachineLoopInfo - This pass is a loop analysis pass.
extern char &MachineLoopInfoID;

/// MachineDominators - This pass is a machine dominators analysis pass.
extern char &MachineDominatorsID;

/// MachineDominanaceFrontier - This pass is a machine dominators analysis.
extern char &MachineDominanceFrontierID;

/// EdgeBundles analysis - Bundle machine CFG edges.
extern char &EdgeBundlesID;

/// LiveVariables pass - This pass computes the set of blocks in which each
/// variable is life and sets machine operand kill flags.
extern char &LiveVariablesID;

/// PHIElimination - This pass eliminates machine instruction PHI nodes
/// by inserting copy instructions.  This destroys SSA information, but is the
/// desired input for some register allocators.  This pass is "required" by
/// these register allocator like this: AU.addRequiredID(PHIEliminationID);
extern char &PHIEliminationID;

/// LiveIntervals - This analysis keeps track of the live ranges of virtual
/// and physical registers.
extern char &LiveIntervalsID;

/// LiveStacks pass. An analysis keeping track of the liveness of stack slots.
extern char &LiveStacksID;

/// TwoAddressInstruction - This pass reduces two-address instructions to
/// use two operands. This destroys SSA information but it is desired by
/// register allocators.
extern char &TwoAddressInstructionPassID;

/// ProcessImpicitDefs pass - This pass removes IMPLICIT_DEFs.
extern char &ProcessImplicitDefsID;

/// RegisterCoalescer - This pass merges live ranges to eliminate copies.
extern char &RegisterCoalescerID;

/// MachineScheduler - This pass schedules machine instructions.
extern char &MachineSchedulerID;

/// PostMachineScheduler - This pass schedules machine instructions postRA.
extern char &PostMachineSchedulerID;

/// SpillPlacement analysis. Suggest optimal placement of spill code between
/// basic blocks.
extern char &SpillPlacementID;

/// ShrinkWrap pass. Look for the best place to insert save and restore
// instruction and update the MachineFunctionInfo with that information.
extern char &ShrinkWrapID;

/// LiveRangeShrink pass. Move instruction close to its definition to shrink
/// the definition's live range.
extern char &LiveRangeShrinkID;

/// Greedy register allocator.
extern char &RAGreedyID;

/// Basic register allocator.
extern char &RABasicID;

/// VirtRegRewriter pass. Rewrite virtual registers to physical registers as
/// assigned in VirtRegMap.
extern char &VirtRegRewriterID;
FunctionPass *createVirtRegRewriter(bool ClearVirtRegs = true);

/// UnreachableMachineBlockElimination - This pass removes unreachable
/// machine basic blocks.
extern char &UnreachableMachineBlockElimID;

/// DeadMachineInstructionElim - This pass removes dead machine instructions.
extern char &DeadMachineInstructionElimID;

/// This pass adds dead/undef flags after analyzing subregister lanes.
extern char &DetectDeadLanesID;

/// This pass perform post-ra machine sink for COPY instructions.
extern char &PostRAMachineSinkingID;

/// FastRegisterAllocation Pass - This pass register allocates as fast as
/// possible. It is best suited for debug code where live ranges are short.
///
FunctionPass *createFastRegisterAllocator();
FunctionPass *createFastRegisterAllocator(RegClassFilterFunc F,
                                          bool ClearVirtRegs);

/// BasicRegisterAllocation Pass - This pass implements a degenerate global
/// register allocator using the basic regalloc framework.
///
FunctionPass *createBasicRegisterAllocator();
FunctionPass *createBasicRegisterAllocator(RegClassFilterFunc F);

/// Greedy register allocation pass - This pass implements a global register
/// allocator for optimized builds.
///
FunctionPass *createGreedyRegisterAllocator();
FunctionPass *createGreedyRegisterAllocator(RegClassFilterFunc F);

/// PrologEpilogCodeInserter - This pass inserts prolog and epilog code,
/// and eliminates abstract frame references.
extern char &PrologEpilogCodeInserterID;
MachineFunctionPass *createPrologEpilogInserterPass();

/// ExpandPostRAPseudos - This pass expands pseudo instructions after
/// register allocation.
extern char &ExpandPostRAPseudosID;

/// PostRAHazardRecognizer - This pass runs the post-ra hazard
/// recognizer.
extern char &PostRAHazardRecognizerID;

/// PostRAScheduler - This pass performs post register allocation
/// scheduling.
extern char &PostRASchedulerID;

/// BranchFolding - This pass performs machine code CFG based
/// optimizations to delete branches to branches, eliminate branches to
/// successor blocks (creating fall throughs), and eliminating branches over
/// branches.
extern char &BranchFolderPassID;

/// BranchRelaxation - This pass replaces branches that need to jump further
/// than is supported by a branch instruction.
extern char &BranchRelaxationPassID;

/// TailDuplicate - Duplicate blocks with unconditional branches
/// into tails of their predecessors.
extern char &TailDuplicateID;

/// Duplicate blocks with unconditional branches into tails of their
/// predecessors. Variant that works before register allocation.
extern char &EarlyTailDuplicateID;

/// MachineTraceMetrics - This pass computes critical path and CPU resource
/// usage in an ensemble of traces.
extern char &MachineTraceMetricsID;

/// EarlyIfConverter - This pass performs if-conversion on SSA form by
/// inserting cmov instructions.
extern char &EarlyIfConverterID;

/// This pass performs instruction combining using trace metrics to estimate
/// critical-path and resource depth.
extern char &MachineCombinerID;

/// StackSlotColoring - This pass performs stack coloring and merging.
/// It merges disjoint allocas to reduce the stack size.
extern char &StackColoringID;

/// StackFramePrinter - This pass prints the stack frame layout and variable
/// mappings.
extern char &StackFrameLayoutAnalysisPassID;

/// IfConverter - This pass performs machine code if conversion.
extern char &IfConverterID;

FunctionPass *
createIfConverter(std::function<bool(const MachineFunction &)> Ftor);

/// MachineBlockPlacement - This pass places basic blocks based on branch
/// probabilities.
extern char &MachineBlockPlacementID;

/// MachineCSE - This pass performs global CSE on machine instructions.
extern char &MachineCSEID;

/// This pass performs loop invariant code motion on machine instructions.
extern char &MachineLICMID;

/// This pass performs loop invariant code motion on machine instructions.
/// This variant works before register allocation. \see MachineLICMID.
extern char &EarlyMachineLICMID;

/// MachineSinking - This pass performs sinking on machine instructions.
extern char &MachineSinkingID;

/// MachineCopyPropagation - This pass performs copy propagation on
/// machine instructions.
extern char &MachineCopyPropagationID;

MachineFunctionPass *createMachineCopyPropagationPass(bool UseCopyInstr);

/// MachineLateInstrsCleanup - This pass removes redundant identical
/// instructions after register allocation and rematerialization.
extern char &MachineLateInstrsCleanupID;

/// PeepholeOptimizer - This pass performs peephole optimizations -
/// like extension and comparison eliminations.
extern char &PeepholeOptimizerID;

/// OptimizePHIs - This pass optimizes machine instruction PHIs
/// to take advantage of opportunities created during DAG legalization.
extern char &OptimizePHIsID;

/// StackSlotColoring - This pass performs stack slot coloring.
extern char &StackSlotColoringID;

/// This pass lays out funclets contiguously.
extern char &FuncletLayoutID;

/// This pass inserts FEntry calls
extern char &FEntryInserterID;

/// This pass implements the "patchable-function" attribute.
extern char &PatchableFunctionID;

/// createStackProtectorPass - This pass adds stack protectors to functions.
///
FunctionPass *createStackProtectorPass();

/// createMachineVerifierPass - This pass verifies cenerated machine code
/// instructions for correctness.
///
FunctionPass *createMachineVerifierPass(const std::string &Banner);

/// createDwarfEHPass - This pass mulches exception handling code into a form
/// adapted to code generation.  Required if using dwarf exception handling.
FunctionPass *createDwarfEHPass(CodeGenOptLevel OptLevel);

/// createWinEHPass - Prepares personality functions used by MSVC on Windows,
/// in addition to the Itanium LSDA based personalities.
FunctionPass *createWinEHPass(bool DemoteCatchSwitchPHIOnly = false);

/// LocalStackSlotAllocation - This pass assigns local frame indices to stack
/// slots relative to one another and allocates base registers to access them
/// when it is estimated by the target to be out of range of normal frame
/// pointer or stack pointer index addressing.
extern char &LocalStackSlotAllocationID;

/// This pass expands pseudo-instructions, reserves registers and adjusts
/// machine frame information.
extern char &FinalizeISelID;

/// UnpackMachineBundles - This pass unpack machine instruction bundles.
extern char &UnpackMachineBundlesID;

FunctionPass *
createUnpackMachineBundles(std::function<bool(const MachineFunction &)> Ftor);

/// RemoveRedundantDebugValues pass.
extern char &RemoveRedundantDebugValuesID;

/// LiveDebugValues pass
extern char &LiveDebugValuesID;

/// InterleavedAccess Pass - This pass identifies and matches interleaved
/// memory accesses to target specific intrinsics.
///
FunctionPass *createInterleavedAccessPass();

/// LowerEmuTLS - This pass generates __emutls_[vt].xyz variables for all
/// TLS variables for the emulated TLS model.
///
ModulePass *createLowerEmuTLSPass();

/// This pass lowers the \@llvm.load.relative intrinsic to
/// instructions.  This is unsafe to do earlier because a pass may combine the
/// constant initializer into the load, which may result in an overflowing
/// evaluation.
ModulePass *createPreISelIntrinsicLoweringPass();

/// GlobalMerge - This pass merges internal (by default) globals into structs
/// to enable reuse of a base pointer by indexed addressing modes.
/// It can also be configured to focus on size optimizations only.
///
Pass *createGlobalMergePass(const TargetMachine *TM, unsigned MaximalOffset,
                            bool OnlyOptimizeForSize = false,
                            bool MergeExternalByDefault = false);

/// This pass detects subregister lanes in a virtual register that are used
/// independently of other lanes and splits them into separate virtual
/// registers.
extern char &RenameIndependentSubregsID;

/// This pass frees the memory occupied by the MachineFunction.
FunctionPass *createFreeMachineFunctionPass();

/// This pass performs outlining on machine instructions directly before
/// printing assembly.
ModulePass *createMachineOutlinerPass(bool RunOnAllFunctions = true);

/// This pass expands the reduction intrinsics into sequences of shuffles.
FunctionPass *createExpandReductionsPass();

// This pass replaces intrinsics operating on vector operands with calls to
// the corresponding function in a vector library (e.g., SVML, libmvec).
FunctionPass *createReplaceWithVeclibLegacyPass();

// Expands large div/rem instructions.
FunctionPass *createExpandLargeDivRemPass();

// Expands large div/rem instructions.
FunctionPass *createExpandLargeFpConvertPass();

// This pass expands memcmp() to load/stores.
FunctionPass *createExpandMemCmpLegacyPass();

/// Creates Break False Dependencies pass. \see BreakFalseDeps.cpp
FunctionPass *createBreakFalseDeps();

// This pass expands indirectbr instructions.
FunctionPass *createIndirectBrExpandPass();

/// Creates CFI Fixup pass. \see CFIFixup.cpp
FunctionPass *createCFIFixup();

/// Creates CFI Instruction Inserter pass. \see CFIInstrInserter.cpp
FunctionPass *createCFIInstrInserter();

/// Creates CFGuard longjmp target identification pass.
/// \see CFGuardLongjmp.cpp
FunctionPass *createCFGuardLongjmpPass();

/// Creates EHContGuard catchret target identification pass.
/// \see EHContGuardCatchret.cpp
FunctionPass *createEHContGuardCatchretPass();

/// Create IR Type Promotion pass. \see TypePromotion.cpp
FunctionPass *createTypePromotionLegacyPass();

/// The pass transforms load/store <256 x i32> to AMX load/store intrinsics
/// or split the data to two <128 x i32>.
FunctionPass *createX86LowerAMXTypePass();

/// The pass transforms amx intrinsics to scalar operation if the function has
/// optnone attribute or it is O0.
FunctionPass *createX86LowerAMXIntrinsicsPass();

/// JMC instrument pass.
ModulePass *createJMCInstrumenterPass();

/// This pass converts conditional moves to conditional jumps when profitable.
FunctionPass *createSelectOptimizePass();

FunctionPass *createCallBrPass();
} // namespace llvm

#endif
