#include "neverc/Emit/Backend/ParallelCodeGenMerge.h"
#include "neverc/Merge/Merger.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticHandler.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/ModuleSymbolTable.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/thread.h"
#include "llvm/Support/xxhash.h"

#include <atomic>
#include <cstdlib>
#include <mutex>

#ifdef __APPLE__
// For the performance-core query in pcgWorkerThreads (hw.perflevel0); see
// there.
#include <sys/sysctl.h>
#endif

using namespace llvm;

namespace neverc {

namespace {

// ===----------------------------------------------------------------------===
// Parallel-codegen engagement / partitioning tunables
//
// The decision of *whether* to split a merged LTO module for parallel codegen,
// and into *how many* partitions, was originally driven purely by the post-IPO
// instruction count (TotalWeight).  Instruction count is a poor predictor of
// compile cost for loop-dense modules: ScalarEvolution (computeSCEVAtScope and
// friends) is superlinear in the number/coupling of loops, so a module with
// few instructions but many loops can take tens of seconds to optimize while
// its TotalWeight stays under the "worth parallelizing" floor -- the expensive
// optimization then runs serially on the main thread and the 15 other cores sit
// idle.  We add a loop signal (back-edge count, a cheap DFS-only proxy for the
// loop count that drives SCEV) so loop-dense modules both *engage* the parallel
// optimization path and get *enough* partitions to actually spread the SCEV
// work across cores.
//
// Every knob is exposed for -mllvm tuning and CI bisection.  Correctness is
// independent of all of them: any partition count produces a byte-correct merge
// (the independent verifier + serial-codegen fallback + NEVERC_PCG_STRICT
// tripwire guarantee it), so these only trade compile wall-time, never output.
static llvm::cl::opt<unsigned> PcgMinFuncs(
    "neverc-pcg-min-funcs", llvm::cl::init(8), llvm::cl::Hidden,
    llvm::cl::desc("Minimum number of defined functions in a merged LTO module "
                   "before parallel codegen is considered"));
static llvm::cl::opt<unsigned> PcgWeightFloor(
    "neverc-pcg-weight-floor", llvm::cl::init(10000), llvm::cl::Hidden,
    llvm::cl::desc("Engage parallel codegen when the post-IPO instruction "
                   "count reaches this floor"));
static llvm::cl::opt<unsigned> PcgLoopFloor(
    "neverc-pcg-loop-floor", llvm::cl::init(56), llvm::cl::Hidden,
    llvm::cl::desc("Engage parallel codegen when the module's loop (back-edge) "
                   "count reaches this floor, even if the instruction count is "
                   "below -neverc-pcg-weight-floor; loop-dense modules are "
                   "SCEV-superlinear and benefit from parallelism despite a low "
                   "instruction count (0 = disable the loop signal)"));
static llvm::cl::opt<unsigned> PcgOptWeightDiv(
    "neverc-pcg-opt-weight-div", llvm::cl::init(12000), llvm::cl::Hidden,
    llvm::cl::desc("Parallel opt+codegen: one partition per this many "
                   "instructions"));
static llvm::cl::opt<unsigned> PcgOptLoopDiv(
    "neverc-pcg-opt-loop-div", llvm::cl::init(16), llvm::cl::Hidden,
    llvm::cl::desc("Parallel opt+codegen: one partition per this many loops "
                   "(back-edges); takes the max with the instruction-based "
                   "count so loop-dense modules get more partitions "
                   "(0 = disable)"));
static llvm::cl::opt<unsigned> PcgOptMaxParts(
    "neverc-pcg-opt-max-parts", llvm::cl::init(16), llvm::cl::Hidden,
    llvm::cl::desc("Parallel opt+codegen: maximum partition count"));
static llvm::cl::opt<unsigned> PcgCgWeightDiv(
    "neverc-pcg-cg-weight-div", llvm::cl::init(5000), llvm::cl::Hidden,
    llvm::cl::desc("Parallel codegen-only: one partition per this many "
                   "instructions"));
static llvm::cl::opt<unsigned> PcgCgLoopDiv(
    "neverc-pcg-cg-loop-div", llvm::cl::init(0), llvm::cl::Hidden,
    llvm::cl::desc("Parallel codegen-only: one partition per this many loops "
                   "(0 = disable; codegen is not SCEV-bound so the loop signal "
                   "is off by default here)"));
static llvm::cl::opt<unsigned> PcgCgMaxParts(
    "neverc-pcg-cg-max-parts", llvm::cl::init(16), llvm::cl::Hidden,
    llvm::cl::desc("Parallel codegen-only: maximum partition count.  A fixed "
                   "ceiling (not the host core count) so the partition count -- "
                   "and therefore the emitted object's layout -- is reproducible "
                   "across machines; execution parallelism is bounded "
                   "separately by the worker pool"));

// Auto-LTO IV-widening cost bound. Whole-program inlining can concentrate
// dozens of loops in one partition-straggler function; widening every IV in
// that shape repeatedly drives expensive ScalarEvolution reasoning. Keep all
// other IndVarSimplify work, but withdraw widening once the function's initial
// LoopInfo forest exceeds this limit. The production default keeps unlimited
// widening because real-project ablation did not justify applying the bound
// universally. Setting 31 is the diagnosed pathological-workload mode: the
// inliner stops at 32 loops, so 32 is the first shape it must cover.
static llvm::cl::opt<unsigned> PcgIndVarWidenMaxFunctionLoops(
    "neverc-auto-lto-indvars-widen-max-function-loops", llvm::cl::init(0),
    llvm::cl::Hidden,
    llvm::cl::desc(
        "Auto-LTO only: disable IndVarSimplify IV widening for functions "
        "whose initial loop count exceeds this limit (0 = unlimited)"));

// Auto-LTO SCEV cost bound. Full-LTO post-link inlining folds many loop-bearing
// leaves into one body, so post-IPO functions are far larger than any single
// TU's. Tightening the threshold during per-partition optimization makes huge
// expressions fall back to their conservative, already-correct unsimplified
// form sooner. The default is the lowest sweep value that preserved ordinary
// project code shape while retaining the pathological compile-cost benefit.
// Different thresholds may legitimately change optimization opportunities and
// code shape, but a fixed setting remains deterministic and never affects
// partition count or assignment. Zero leaves ScalarEvolution's own default.
static llvm::cl::opt<unsigned> PcgScevHugeExprThreshold(
    "neverc-auto-lto-scev-huge-expr-threshold", llvm::cl::init(64),
    llvm::cl::Hidden,
    llvm::cl::desc("Auto-LTO only: SCEV huge-expression node threshold applied "
                   "during per-partition optimization to bound superlinear "
                   "ScalarEvolution simplification on whole-program functions "
                   "(0 = leave ScalarEvolution's default)"));

// RAII: tighten ScalarEvolution's huge-expression threshold for the lifetime of
// the per-partition optimization phase, then restore it.  Set on the driver
// thread before the workers start and restored after they join, so the workers
// (which only ever read the threshold) never observe it changing -- no race.
// Because the value is established before any worker runs and is identical for
// all of them, it cannot make the output depend on thread scheduling.
struct ScevHugeThresholdGuard {
  unsigned Saved = 0;
  bool Active;
  explicit ScevHugeThresholdGuard(unsigned NewVal) : Active(NewVal != 0) {
    if (Active) {
      Saved = llvm::getScevHugeExprThreshold();
      llvm::setScevHugeExprThreshold(NewVal);
    }
  }
  ~ScevHugeThresholdGuard() {
    if (Active)
      llvm::setScevHugeExprThreshold(Saved);
  }
  ScevHugeThresholdGuard(const ScevHugeThresholdGuard &) = delete;
  ScevHugeThresholdGuard &operator=(const ScevHugeThresholdGuard &) = delete;
};

// Worker-pool work proportioning (execution parallelism only; see
// pcgWorkerThreads / pcgWorkCapThreads).  A light parallel workload is
// memory-bandwidth bound and saturates far fewer cores than the machine has:
// measured on a 12P+4E Apple M4 Max, a 16-partition Lua 5.4 link peaks at ~4
// worker threads and is ~15% SLOWER at 16 (bandwidth contention + the four
// efficiency cores becoming barrier stragglers), while a loop-heavy
// 1200-function link keeps scaling to the performance-core ceiling.  The two
// workloads differ almost entirely in loop density -- the per-partition
// ScalarEvolution / loop-optimization cost that is the parallelized work -- so
// the worker count is derived from the module's loop and instruction totals and
// clamped into [floor, performance-core ceiling].  Like every knob here this is
// a pure compile-wall-time control: it feeds only the thread-pool size, never
// the partition count or the function-to-partition assignment, so the emitted
// object is byte-identical regardless of it (the MergeParallelCodegen
// determinism oracle pins this).  Defaults err toward MORE threads (so a
// workload that does need parallelism is never starved); NEVERC_PCG_THREADS
// overrides everything.
static llvm::cl::opt<unsigned> PcgLoopsPerThread(
    "neverc-pcg-loops-per-thread", llvm::cl::init(96), llvm::cl::Hidden,
    llvm::cl::desc("Auto-LTO parallel opt: target loop (back-edge) count per "
                   "worker thread; the pool is sized to the module's loop work "
                   "and clamped to the performance-core count "
                   "(0 = disable work proportioning, use the full core count)"));
static llvm::cl::opt<unsigned> PcgWeightPerThread(
    "neverc-pcg-weight-per-thread", llvm::cl::init(10000), llvm::cl::Hidden,
    llvm::cl::desc("Auto-LTO parallel opt: target instruction count per worker "
                   "thread; taken as the max with the loop-based estimate so a "
                   "loop-light but instruction-heavy module still gets enough "
                   "threads for its codegen"));
static llvm::cl::opt<unsigned> PcgWorkThreadFloor(
    "neverc-pcg-work-thread-floor", llvm::cl::init(6), llvm::cl::Hidden,
    llvm::cl::desc("Auto-LTO parallel opt: minimum worker threads work "
                   "proportioning may choose (still clamped by the partition "
                   "count) so a parallel-worthy workload is never starved"));

// Number of high-performance cores, when the OS exposes it cheaply, else 0
// ("unknown" -> caller falls back to the full hardware concurrency).  A
// read-only query with a clean failure path on every branch, so a missing key
// or a sandbox simply leaves the caller on the existing default.
unsigned pcgPerformanceCoreCount() {
#ifdef __APPLE__
  // hw.perflevel0 is the highest-performance core class on an Apple-silicon
  // hybrid CPU (the P-cores); on a homogeneous Mac it is simply every physical
  // core, so reading it there returns the full count and changes nothing.
  uint32_t N = 0;
  size_t Sz = sizeof(N);
  if (::sysctlbyname("hw.perflevel0.physicalcpu", &N, &Sz, nullptr, 0) == 0 &&
      N > 0)
    return N;
#endif
  return 0;
}

// Worker-thread count for the partition pools.  This is *execution* parallelism
// only: it bounds how many partitions are optimized/codegen'd concurrently and
// never feeds the partition count or the function-to-partition assignment, so
// it cannot change a single emitted byte (the MergeParallelCodegen determinism
// oracle pins this, so every choice below is a pure compile-wall-time knob).
//
// The per-partition phase is *barrier-synchronized*: the merge waits for every
// partition, so the wall time is the slowest partition, not the average.  On a
// heterogeneous CPU (Apple-silicon P+E cores, Intel hybrid) a partition that
// lands on a slow efficiency core becomes the straggler that stalls the whole
// merge, so sizing the pool to *every* logical core makes the typical link both
// slower and less predictable than sizing it to the performance cores alone
// (measured on a 12P+4E machine, real 33-TU Lua link: all 16 threads vs the 12
// P-cores was ~3% worse median and far more high-tail runs -- 4/9 vs 1/9 slow
// -- because the four E-core partitions are the recurring stragglers).  Prefer
// the performance-core count where it is cheaply known and fall back to the
// full hardware concurrency otherwise; on a homogeneous machine the two are
// equal, so this never reduces parallelism there.  NEVERC_PCG_THREADS overrides
// everything (clamped to >= 1) for users who want to cap peak memory /
// parallelism and for the cross-thread-count determinism regression test.
// Worker count a module's parallel work justifies, or 0 ("no proportioning")
// when the loop signal is disabled.  Pure function of the module (loop +
// instruction totals), so it cannot make the worker count -- and therefore the
// output -- depend on the host or on scheduling.  Returns the larger of the
// loop- and instruction-based estimates, floored so a parallel-worthy module is
// never starved; the caller clamps it down to the performance-core ceiling.
unsigned pcgWorkCapThreads(unsigned LoopCount, unsigned TotalWeight) {
  if (PcgLoopsPerThread == 0)
    return 0; // work proportioning disabled -> caller uses the full core count
  unsigned ByLoops = LoopCount / PcgLoopsPerThread;
  unsigned ByWeight = PcgWeightPerThread ? TotalWeight / PcgWeightPerThread : 0;
  return std::max({ByLoops, ByWeight, (unsigned)PcgWorkThreadFloor});
}

unsigned pcgWorkerThreads(unsigned NumPartitions, unsigned WorkCap) {
  unsigned HW = llvm::thread::hardware_concurrency();
  if (HW < 1)
    HW = 1;
  unsigned N = pcgPerformanceCoreCount();
  if (N < 1 || N > HW)
    N = HW;
  // Reduce the pool to what the module's parallel work justifies (WorkCap == 0
  // means "no proportioning", e.g. the codegen-only path that does not estimate
  // work).  This only ever lowers the count -- never above the P-core/HW
  // ceiling -- and the env override below skips it entirely so the determinism
  // regression test (which pins behavior across explicit thread counts) is
  // unaffected.
  bool Overridden = false;
  if (const char *E = ::getenv("NEVERC_PCG_THREADS")) {
    unsigned Override = 0;
    if (!StringRef(E).getAsInteger(10, Override) && Override >= 1) {
      N = Override;
      Overridden = true;
    }
  }
  if (!Overridden && WorkCap >= 1 && WorkCap < N)
    N = WorkCap;
  if (N < 1)
    N = 1;
  return std::min(N, NumPartitions);
}

bool mergePartitionObjects(const Triple &TT,
                           ArrayRef<SmallVector<char, 0>> Bufs,
                           SmallVectorImpl<char> &Output,
                           const merge::Options &Opts = {}) {
  using namespace neverc::merge;
  // Test-only fault injection: pretend the merge failed so the serial-codegen
  // safety net (finalizeResults' bail -> restoreLinkage -> the LTO backend's
  // serial codegen fallback) can be exercised end to end and proven to still
  // produce a correct binary.  Production never sets this; under
  // NEVERC_PCG_STRICT a forced failure still aborts, exactly as any real merge
  // failure would, so the variable cannot mask a regression when strict is on.
  if (::getenv("NEVERC_PCG_FORCE_MERGE_FAIL") != nullptr)
    return false;
  raw_svector_ostream OS(Output);
  if (TT.isOSBinFormatCOFF())
    return mergeObjects(Bufs, OS, Format::COFF, Opts);
  if (TT.isOSBinFormatELF())
    return mergeObjects(Bufs, OS, Format::ELF64LE, Opts);
  if (TT.isOSBinFormatMachO())
    return mergeObjects(Bufs, OS, Format::MachO64, Opts);
  return false;
}

std::optional<merge::Format> mergeFormatForTriple(const Triple &TT) {
  if (TT.isOSBinFormatCOFF())
    return merge::Format::COFF;
  if (TT.isOSBinFormatELF())
    return merge::Format::ELF64LE;
  if (TT.isOSBinFormatMachO())
    return merge::Format::MachO64;
  return std::nullopt;
}

// ===----------------------------------------------------------------------===
// Shared infrastructure for both parallel codegen paths
// ===----------------------------------------------------------------------===

struct FuncEntry {
  Function *Fn;
  unsigned Weight;
};

struct LinkageEntry {
  GlobalValue *GV;
  std::string OrigName;
  GlobalValue::LinkageTypes Linkage;
  GlobalValue::VisibilityTypes Visibility;
  bool AddedOriginalLocalAttr = false;
  bool AddedOriginalAddressTakenAttr = false;
};

// Parallel partitioning has to promote file-local functions to hidden external
// symbols so another partition can refer to them.  Preserve the source-level
// facts KCFI uses to decide whether a local definition needs a prefix: after
// the split, the promoted linkage and the per-partition use-list can no longer
// answer either question reliably.
constexpr StringLiteral PCGOriginalLocalAttr = "neverc.pcg.original-local";
constexpr StringLiteral PCGOriginalAddressTakenAttr =
    "neverc.pcg.original-address-taken";

struct PartitionResult {
  SmallVector<char, 0> ObjBuffer;
  SmallVector<char, 0> DwoBuffer;
  bool Success = false;
  /// Errors raised against this partition's own context, carried back to the
  /// caller's thread instead of being reported where they happen.  See
  /// PartitionDiagnosticHandler.  Written by the one worker that owns this
  /// partition, read after the workers join.
  SmallVector<std::string, 0> Errors;
};

/// A partition is optimized and codegen'd on a worker thread, against an
/// LLVMContext of its own.  A context nobody has given a handler to answers an
/// error by writing it to stderr and calling exit() -- on that worker thread.
/// Two partitions failing together therefore means two concurrent exit() calls,
/// which interleave their text and then deadlock in atexit handling: one thread
/// runs the handlers while the other waits on a lock the first will not
/// release.
///
/// Recording the error keeps it on the only path that can report it properly.
/// The partition counts as failed, and finalizeResults re-raises the text
/// against the module the caller owns -- the one context here with a diagnostic
/// consumer behind it.
class PartitionDiagnosticHandler final : public DiagnosticHandler {
public:
  explicit PartitionDiagnosticHandler(PartitionResult &Result)
      : Result(Result) {}

  bool handleDiagnostics(const DiagnosticInfo &DI) override {
    // Only an error takes the exit() path, so only an error has to be taken off
    // it; anything milder keeps the route it already had.
    if (DI.getSeverity() != DS_Error)
      return false;
    std::string Text;
    raw_string_ostream Stream(Text);
    DiagnosticPrinterRawOStream Printer(Stream);
    DI.print(Printer);
    Result.Errors.push_back(std::move(Text));
    return true;
  }

private:
  PartitionResult &Result;
};

struct PreparedPartition {
  std::unique_ptr<LLVMContext> Ctx;
  std::unique_ptr<Module> M;
  std::unique_ptr<TargetMachine> PTM;
  SmallVector<char, 0> *ObjBuf = nullptr;
  SmallVector<char, 0> *DwoBuf = nullptr;
  /// Partition cache entry key; empty when caching is off or the key
  /// could not be computed.  Set by preparePartitions on a miss, consumed
  /// by the codegen worker to store the produced object.
  std::string CacheKey;
  PreparedPartition() = default;
};

// ===----------------------------------------------------------------------===
// Module-wide constructs
//
// A partition module starts as a copy of the whole merged module, so whatever
// that module carries outside of its symbols -- named metadata the linker
// consumes, file-scope inline assembly, the appending globals -- exists in all
// N copies, and codegen emits it out of every one of them.  The merge is then
// handed N copies of one .drectve, N copies of one asm block, N registrations
// of one initializer.  Partition 0 owns these constructs; every other
// partition gives them up.
//
// The retain markers are the exception, and the reason this is a policy rather
// than a single erase loop.  Their entries name individual symbols, and
// codegen turns each entry into a property of the section holding that
// symbol's *definition* (SHF_GNU_RETAIN on ELF, .no_dead_strip on Mach-O,
// /INCLUDE: on COFF).  A marker is only expressible where the body is, so
// rather than surrender them a partition narrows them to what it still
// defines.
// ===----------------------------------------------------------------------===

/// Named metadata codegen turns into output of its own -- an .ident string, a
/// .drectve fragment, a DT_NEEDED entry, a recorded command line -- once for
/// each module it is handed, attached to no symbol in it.
constexpr StringLiteral PerModuleOutputMetadata[] = {
    "llvm.ident", "llvm.linker.options", "llvm.dependent-libraries",
    "llvm.commandline"};

/// Gives up everything partition 0 owns.  Erasing is safe for all of it:
/// partition 0 is retained even when empty (see the binning below), so each
/// construct dropped here still has exactly one module left to be emitted
/// from.
void surrenderModuleWideConstructs(Module &M) {
  for (StringRef MDName : PerModuleOutputMetadata)
    if (auto *NMD = M.getNamedMetadata(MDName))
      M.eraseNamedMetadata(NMD);

  // AsmPrinter copies this to the head of its output verbatim, once for every
  // module it is handed (doInitialization), so N partitions put N copies into
  // the merge.  A block that only references symbols would survive that; one
  // that *defines* anything -- `.globl f; f: ...`, a `.section` with contents,
  // a `.set` -- becomes N definitions of one name, which the merge has to
  // refuse.
  M.setModuleInlineAsm("");

  // The retain markers are the appending globals LLVM's own accessor knows,
  // which is why their names are not spelled out here.
  SmallVector<GlobalValue *, 0> Entries;
  SmallPtrSet<const GlobalVariable *, 2> RetainMarkers;
  for (bool CompilerUsed : {false, true})
    if (auto *List = collectUsedGlobalVariables(M, Entries, CompilerUsed))
      RetainMarkers.insert(List);

  for (GlobalVariable &GV : make_early_inc_range(M.globals()))
    if (GV.hasAppendingLinkage() && !GV.isDeclaration() &&
        !RetainMarkers.contains(&GV))
      GV.eraseFromParent();
}

/// True when the module's file-scope assembly is what stands between it and a
/// split.
///
/// Leaving the block to partition 0 costs nothing as long as the symbols it
/// defines are ones the object merge can hand to the other partitions, which
/// is to say global or weak ones -- the same currency every ordinary
/// cross-partition reference is settled in.  A definition with local binding
/// is not that.  It comes into being only when the assembler runs, under a
/// name no IR value owns, so externalizeAndSerialize cannot promote it the way
/// it promotes a `static` function that other partitions came to need; a
/// reference to it from anywhere but partition 0 simply goes unresolved.  The
/// module is left whole instead.
///
/// The block is put to LLVM's assembler rather than read: CollectAsmSymbols
/// parses it for real and reports each symbol's binding, and a definition that
/// is neither global nor weak is a local one.
bool moduleAsmForbidsSplitting(const Module &M) {
  if (M.getModuleInlineAsm().empty())
    return false;

  bool Reported = false;
  bool LocalDefinition = false;
  ModuleSymbolTable::CollectAsmSymbols(
      M, [&](StringRef, object::BasicSymbolRef::Flags Flags) {
        Reported = true;
        LocalDefinition |= !(Flags & (object::BasicSymbolRef::SF_Global |
                                      object::BasicSymbolRef::SF_Weak));
      });
  // A block that names nothing and a block the parser gave up on both come
  // back empty, and CollectAsmSymbols does not distinguish them.  Decline on
  // either: the first has no parallelism worth chasing, and the second is a
  // question left unanswered.
  return !Reported || LocalDefinition;
}

/// Narrows this partition's retain markers to the definitions it actually
/// emits.  A marker naming a body that was binned elsewhere would be dropped
/// by codegen anyway (there is no section to flag), while the partition that
/// does hold the body would emit none at all -- so without this the whole
/// module's markers survive only for whatever landed in partition 0, and
/// `__attribute__((used, retain))` stops holding against --gc-sections.
/// Across partitions the surviving entries reunite into the original list,
/// each named exactly once, so the object merge sees no duplicates.
void retainOnlyLocalDefinitions(Module &M) {
  removeFromUsedLists(M, [](Constant *Entry) {
    auto *GV = dyn_cast<GlobalValue>(Entry->stripPointerCasts());
    return !GV || GV->isDeclarationForLinker();
  });
}

/// Erases declarations nothing in this partition refers to.  Each partition
/// module starts as a copy of the full merged module, so without this the
/// partition's bitcode (and thus its cache key) would change whenever any
/// symbol is added or renamed anywhere in the program, not just when the
/// partition's own code changes.  Metadata references (ValueAsMetadata,
/// e.g. debug info) are not uses, so check isUsedByMetadata separately:
/// erasing such a declaration would null out those metadata operands.
static void stripUnreferencedDeclarations(Module &M) {
  for (Function &F : make_early_inc_range(M))
    if (F.isDeclaration() && F.use_empty() && !F.isUsedByMetadata())
      F.eraseFromParent();
  for (GlobalVariable &GV : make_early_inc_range(M.globals()))
    if (GV.isDeclaration() && GV.use_empty() && !GV.isUsedByMetadata())
      GV.eraseFromParent();
}

struct ParallelCGContext {
  const Target *TheTarget;
  std::string TripleStr;
  Triple TT;
  SmallVector<FuncEntry, 0> FuncList;
  unsigned TotalWeight = 0;
  unsigned LoopCount = 0;
  unsigned FuncCount = 0;
  unsigned NumPartitions = 0;
  // Worker-pool ceiling this module's parallel work justifies; 0 = no
  // proportioning (the codegen-only path, which does not estimate work).  Set
  // by the opt+codegen path after the work signals are known; consumed by every
  // pcgWorkerThreads() call so prepare and opt/codegen size their pools alike.
  unsigned WorkerThreadCap = 0;

  std::vector<PartitionResult> Results;
  CodeModel::Model SharedCM;
  CodeGenOptLevel SharedOptLevel;
  std::string SharedFeatures;
  TargetOptions SharedTgtOpts;
  DebugCompressionType FinalDebugCompression = DebugCompressionType::None;
  bool FinalizeDebugCompression = false;
  bool EmitSplitDwarf = false;

  SmallVector<LinkageEntry, 64> SavedLinkage;

  struct SavedNamedMD {
    std::string Name;
    SmallVector<MDNode *, 4> Operands;
  };
  SmallVector<SavedNamedMD, 4> SavedMD;

  SmallVector<SmallVector<std::string, 0>, 8> Assignments;
  DenseMap<StringRef, unsigned> FuncPartition;
  // Names of the functions some alias or ifunc resolves to.  Their bodies are
  // kept in every partition, not just the one they were binned into: see the
  // deleteBody loop in preparePartitions.
  StringSet<> IndirectTargets;
  SmallString<0> FullBC;

  std::unique_ptr<TargetLibraryInfoImpl> SharedTLII;
  std::vector<std::unique_ptr<PreparedPartition>> Parts;

  /// Optional per-partition object cache (linker-injected) and the
  /// pipeline tag distinguishing the two public entry points, whose
  /// outputs differ for identical partition bitcode.
  const PartitionCacheHooks *Cache = nullptr;
  StringRef PipeTag;

  bool init(Module &Mod, TargetMachine &TM, bool WithSplitDwarf);
  bool resolvePartitions(unsigned WeightDiv, unsigned LoopDiv,
                         unsigned MaxParts);
  bool externalizeAndSerialize(Module &Mod);
  void preparePartitions(StringRef BCRef, TargetMachine &TM);
  bool finalizeResults(Module &Mod, ParallelCodeGenOutputs Outputs);
  void restoreLinkage(Module &Mod);
};

bool ParallelCGContext::init(Module &Mod, TargetMachine &TM,
                             bool WithSplitDwarf) {
  TheTarget = &TM.getTarget();
  TripleStr = Mod.getTargetTriple();
  TT = Triple(TripleStr);
  // Embedded (`-gsplit-dwarf=single`) output has no independent package
  // destination. Keep it serial even when a caller bypasses BackendUtil's
  // engagement gate; concatenating partition DWO sections is not a valid
  // embedded multi-unit representation.
  if (!WithSplitDwarf && !TM.Options.MCOptions.SplitDwarfFile.empty())
    return false;
  EmitSplitDwarf = WithSplitDwarf;

  // This package builder intentionally supports only standard DWARF 5
  // CU/TU indexes. Mach-O split-DWARF packaging is left on the serial path;
  // the parallel merger supports the ELF/COFF containers requested here.
  const unsigned EmittedDwarfVersion = TM.Options.MCOptions.DwarfVersion
                                           ? TM.Options.MCOptions.DwarfVersion
                                           : Mod.getDwarfVersion();
  if (EmitSplitDwarf && (EmittedDwarfVersion != 5 ||
                         (!TT.isOSBinFormatELF() && !TT.isOSBinFormatCOFF())))
    return false;

  // A precondition, not one of the engagement heuristics below: no amount of
  // work in this module makes it splittable if its file-scope assembly names a
  // definition the split would strand.
  if (moduleAsmForbidsSplitting(Mod))
    return false;

  // Back-edge count is a cheap (DFS-only, no dominator tree) proxy for the
  // loop count.  We use it, not exact LoopInfo, because this runs before the
  // per-partition pipeline and only needs to be good enough to tell a
  // loop-dense module from a straight-line one.
  SmallVector<std::pair<const BasicBlock *, const BasicBlock *>, 32> BackEdges;
  for (auto &F : Mod)
    if (!F.isDeclaration()) {
      unsigned W = 0;
      for (auto &BB : F)
        W += BB.size();
      TotalWeight += W;
      FuncList.push_back({&F, W});
      BackEdges.clear();
      FindFunctionBackedges(F, BackEdges);
      LoopCount += BackEdges.size();
    }
  FuncCount = FuncList.size();

  // Engage on either signal: enough instructions (TotalWeight) OR enough loops
  // (LoopCount).  The loop floor catches SCEV-superlinear modules whose
  // instruction count alone would (wrongly) decline parallelism and run the
  // expensive optimization serially.
  if (FuncCount < PcgMinFuncs)
    return false;
  bool WeightOK = TotalWeight >= PcgWeightFloor;
  bool LoopsOK = PcgLoopFloor != 0 && LoopCount >= PcgLoopFloor;
  if (!WeightOK && !LoopsOK)
    return false;

  SharedCM = TM.getCodeModel();
  SharedOptLevel = TM.getOptLevel();
  SharedFeatures = TM.getTargetFeatureString().str();
  SharedTgtOpts = TM.Options;
  FinalDebugCompression = SharedTgtOpts.CompressDebugSections;
  FinalizeDebugCompression =
      FinalDebugCompression != DebugCompressionType::None &&
      (EmitSplitDwarf || TT.isOSBinFormatELF());
  if (FinalizeDebugCompression)
    SharedTgtOpts.CompressDebugSections = DebugCompressionType::None;
  SharedTgtOpts.EmitAddrsig = false;
  SharedTLII = std::make_unique<TargetLibraryInfoImpl>(TT);
  return true;
}

bool ParallelCGContext::resolvePartitions(unsigned WeightDiv, unsigned LoopDiv,
                                          unsigned MaxParts) {
  // Desired partition count from the work estimate: the larger of the
  // instruction-based and loop-based counts.  Loops drive the superlinear SCEV
  // cost, so a loop-dense module gets more partitions than its instruction
  // count alone would suggest -- spreading that cost across cores instead of
  // serializing it.  FuncCount is the hard ceiling (a partition needs at least
  // one function) and MaxParts bounds it from above.
  //
  // The host core count is deliberately NOT an input here.  Partitioning
  // decides how the program is *decomposed* (which function lands in which
  // partition, hence the output object's symbol/section layout and each
  // partition's cache key); decomposing by hardware_concurrency() would make
  // the very bytes of the emitted object depend on the machine it was built on,
  // breaking reproducible builds and zeroing the per-partition object cache hit
  // rate across heterogeneous machines.  Every term below (TotalWeight,
  // LoopCount, FuncCount, MaxParts) is a pure function of the input module, so
  // the partition count is identical on a 4-core CI box and a 64-core
  // workstation.  Execution parallelism is bounded separately, where it belongs
  // -- in the worker pools (pcgWorkerThreads) -- so a small machine still only
  // runs as many partitions at once as it has cores; it just processes the same
  // deterministic set of partitions in more waves.  This is the ThinLTO
  // discipline: decompose by work, schedule by cores.
  unsigned ByWeight = WeightDiv ? TotalWeight / WeightDiv : 0;
  unsigned ByLoops = LoopDiv ? LoopCount / LoopDiv : 0;
  unsigned WorkParts = std::max({ByWeight, ByLoops, 2u});
  if (NumPartitions == 0) {
    NumPartitions = std::min({WorkParts, FuncCount, MaxParts});
    if (NumPartitions < 2)
      return false;
  } else if (NumPartitions < 2) {
    return false;
  } else {
    NumPartitions = std::min({NumPartitions, WorkParts, MaxParts});
  }
  if (FuncCount < NumPartitions)
    NumPartitions = std::max(1u, FuncCount);
  if (NumPartitions < 2)
    return false;
  Results.resize(NumPartitions);
  return true;
}

bool ParallelCGContext::externalizeAndSerialize(Module &Mod) {
  // The LTO reader may hand this path a lazy module.  The transformations
  // below inspect use-lists (for example while dropping dead constants), which
  // are incomplete until the module materializer has been drained.
  if (Error Err = Mod.materializeAll()) {
    consumeError(std::move(Err));
    return false;
  }

  SmallString<32> PCGSuffix;
  {
    auto H = hash_value(Mod.getModuleIdentifier());
    raw_svector_ostream(PCGSuffix) << merge::PcgSymbolMarker << (H & 0xFFFFFFFF);
  }

  auto ExternalizeGV = [&](GlobalValue &GV) {
    if (!GV.hasLocalLinkage())
      return;
    LinkageEntry Saved{&GV, GV.getName().str(), GV.getLinkage(),
                       GV.getVisibility()};
    if (auto *F = dyn_cast<Function>(&GV)) {
      if (!F->hasFnAttribute(PCGOriginalLocalAttr)) {
        F->addFnAttr(PCGOriginalLocalAttr);
        Saved.AddedOriginalLocalAttr = true;
      }
      if (F->hasAddressTaken() &&
          !F->hasFnAttribute(PCGOriginalAddressTakenAttr)) {
        F->addFnAttr(PCGOriginalAddressTakenAttr);
        Saved.AddedOriginalAddressTakenAttr = true;
      }
    }
    SavedLinkage.push_back(std::move(Saved));
    SmallString<64> NewName(GV.getName());
    NewName += PCGSuffix;
    GV.setName(NewName);
    GV.setLinkage(GlobalValue::ExternalLinkage);
    GV.setVisibility(GlobalValue::HiddenVisibility);
  };
  for (Function &F : Mod)
    ExternalizeGV(F);
  for (GlobalVariable &GV : Mod.globals()) {
    // unnamed_addr constants (string literals, constant arrays) are safe to
    // duplicate across partitions — their identity is defined by content, not
    // address.  Keeping them as local definitions in every partition avoids
    // cross-partition symbol resolution in the merger, which has known issues
    // with large constant pools.  This matches LLVM's SplitModule behavior.
    if (GV.hasGlobalUnnamedAddr() && GV.isConstant())
      continue;
    ExternalizeGV(GV);
  }
  for (GlobalAlias &GA : Mod.aliases())
    ExternalizeGV(GA);
  for (GlobalIFunc &IF : Mod.ifuncs())
    ExternalizeGV(IF);

  // Aliases and ifuncs are kept only in partition 0, but the rewrite that
  // replaces them elsewhere cannot run until materializeAll() -- which is
  // itself what runs the verifier.  Whatever they resolve to must therefore
  // survive the body and initializer dropping below in *every* partition, or
  // the module is briefly malformed and the verifier rejects it outright.
  //
  // getAliaseeObject() is what makes this complete: it walks alias chains and
  // constant expressions, and it reaches global variables, not just functions.
  // A `__thread int a __attribute__((alias("b")))` names a definition just as
  // much as a function alias does.
  DenseSet<StringRef> PinnedToP0;
  auto NoteIndirectTarget = [&](const GlobalObject *Target) {
    if (!Target)
      return;
    IndirectTargets.insert(Target->getName());
    // Only functions are binned, so only they need pinning; a variable's
    // definition already lives in partition 0 by construction.
    if (isa<Function>(Target))
      PinnedToP0.insert(Target->getName());
  };
  for (GlobalAlias &GA : Mod.aliases())
    NoteIndirectTarget(GA.getAliaseeObject());
  for (GlobalIFunc &IF : Mod.ifuncs())
    NoteIndirectTarget(IF.getResolverFunction());

  // A `blockaddress(@F, %BB)` is only valid in the module that holds @F's
  // body: if @F becomes a declaration, LLVM rewrites every blockaddress into
  // it to the sentinel `inttoptr(i32 1)`.  Computed-goto interpreters (Lua's
  // luaV_execute, CPython's ceval, QEMU TCG, ...) keep these block addresses in
  // a `static const void *disptab[]` dispatch table.  Global initializers all
  // live in partition 0, so if @F's body is binned into any other partition,
  // partition 0's copy of the table collapses to all-`1` and the program jumps
  // to address 1 at runtime (a silent miscompile the object merge/self-verify
  // cannot catch — the partition object is already wrong before it is merged).
  // Pin every function whose blocks have their address taken to partition 0 so
  // its body stays co-located with the dispatch table that references it.  In
  // valid C a label address never crosses a function boundary, so this is the
  // only co-location the split must enforce for correctness.
  for (Function &F : Mod) {
    if (F.isDeclaration())
      continue;
    for (BasicBlock &BB : F)
      if (BB.hasAddressTaken()) {
        PinnedToP0.insert(F.getName());
        break;
      }
  }

  // A SEH funclet reaches its parent's frame through a pair of intrinsics:
  // llvm.localescape names the slots in the parent, llvm.localrecover reads
  // them back from the filter or handler.  The symbol tying the two together
  // (`<parent>$frame_escape_N`) is emitted into whichever object holds the
  // parent, so a filter binned into another partition would reference a label
  // that object never defines and the assembler rejects the whole partition.
  // Pin both ends of every such pair.
  auto PinFrameEscapeUsers = [&](Intrinsic::ID Id, bool PinFrameOwner) {
    // Walking the intrinsic's uses keeps this off the critical path entirely
    // for the targets (everything but Windows SEH) that never declare it.
    Function *Intrin = Mod.getFunction(Intrinsic::getName(Id));
    if (!Intrin)
      return;
    for (User *U : Intrin->users()) {
      auto *Call = dyn_cast<CallBase>(U);
      if (!Call)
        continue;
      if (const Function *Caller = Call->getFunction())
        PinnedToP0.insert(Caller->getName());
      if (!PinFrameOwner)
        continue;
      // localrecover's first operand names the frame being recovered.
      if (const auto *Owner = dyn_cast<Function>(
              Call->getArgOperand(0)->stripPointerCasts()))
        PinnedToP0.insert(Owner->getName());
    }
  };
  PinFrameEscapeUsers(Intrinsic::localescape, /*PinFrameOwner=*/false);
  PinFrameEscapeUsers(Intrinsic::localrecover, /*PinFrameOwner=*/true);

  // A structor record's third field is its COMDAT key: the symbol whose
  // section the .init_array / .fini_array entry joins, so that discarding one
  // copy of a coalesced initializer discards its registration with it.
  // AsmPrinter refuses to emit a record whose key is a declaration in the
  // module at hand, and the structor lists have appending linkage, so only
  // partition 0 still carries them.  A key binned elsewhere is therefore a
  // declaration exactly where the list lives, and the record is dropped
  // without a diagnostic -- the initializer is emitted, nothing registers it,
  // and it silently never runs.  (An embedded allocator runtime whose
  // process-attach hook is skipped this way hands out heap pointers from a
  // heap that was never brought up.)  Keep every key's body in partition 0
  // alongside the list that names it.
  for (StringRef ListName : {"llvm.global_ctors", "llvm.global_dtors"}) {
    auto *List = Mod.getGlobalVariable(ListName);
    if (!List || !List->hasInitializer())
      continue;
    auto *Records = dyn_cast<ConstantArray>(List->getInitializer());
    if (!Records)
      continue;
    for (const Use &Record : Records->operands()) {
      auto *Fields = dyn_cast<ConstantStruct>(Record.get());
      if (!Fields || Fields->getNumOperands() < 3)
        continue;
      // Only functions are binned; an alias or variable key is a definition
      // in partition 0 by construction, as for the indirect targets above.
      if (auto *Key =
              dyn_cast<Function>(Fields->getOperand(2)->stripPointerCasts()))
        PinnedToP0.insert(Key->getName());
    }
  }

  // Stable assignment: bin by function-name hash instead of greedy
  // weight balancing.  Greedy reshuffles every partition whenever one
  // function's weight changes, which would zero the per-partition object
  // cache hit rate on incremental rebuilds; name binning keeps an edit
  // confined to the partitions whose contents actually changed.  The lost
  // load balance is bounded: bins receive ~FuncCount/NumPartitions random
  // functions each, and a single oversized function dominates wall time
  // under either policy.
  {
    SmallVector<SmallVector<std::string, 0>, 8> Bins(NumPartitions);
    for (auto &FE : FuncList) {
      StringRef Name = FE.Fn->getName();
      unsigned B = PinnedToP0.count(Name)
                       ? 0
                       : unsigned(xxh3_64bits(Name) % NumPartitions);
      Bins[B].push_back(Name.str());
    }
    // Always retain slot 0: it owns the module-wide constructs every other
    // partition surrenders, plus the aliases pinned above.  Dropping an empty
    // bin 0 used to renumber a former non-zero bin into partition 0 — that bin
    // still kept named metadata, but on some COFF hosts the resulting .drectve
    // was lost during merge when partition 0 had no "real" ownership of the
    // metadata relative to how codegen ordered sections.  Keeping a (possibly
    // empty) partition 0 makes "partition 0 owns them" a stable invariant.
    Assignments.clear();
    Assignments.push_back(std::move(Bins[0]));
    for (unsigned i = 1, e = Bins.size(); i != e; ++i)
      if (!Bins[i].empty())
        Assignments.push_back(std::move(Bins[i]));
    NumPartitions = Assignments.size();
    Results.resize(NumPartitions);
    for (unsigned p = 0; p < NumPartitions; ++p)
      for (auto &N : Assignments[p])
        FuncPartition[N] = p;
  }

  Mod.dropTriviallyDeadConstantArrays();
  // Every partition context reads this buffer with value names discarded, so
  // shedding them here keeps them out of the bitcode all N of them parse.
  for (Function &F : Mod) {
    if (F.isDeclaration())
      continue;
    for (BasicBlock &BB : F) {
      BB.setName("");
      for (Instruction &I : BB)
        I.setName("");
    }
  }

  FullBC.reserve(TotalWeight * 40);
  {
    raw_svector_ostream BCOS(FullBC);
    WriteBitcodeToFile(Mod, BCOS, false);
  }

  // This metadata had to reach the buffer above so that partition 0 still
  // emits it exactly once.  It comes off the mother module only now, and only
  // so far as restoreLinkage can put it back if the merge bails to serial
  // codegen.
  for (StringRef MDName : PerModuleOutputMetadata)
    if (auto *NMD = Mod.getNamedMetadata(MDName)) {
      SavedNamedMD S;
      S.Name = MDName.str();
      for (unsigned i = 0; i < NMD->getNumOperands(); ++i)
        S.Operands.push_back(NMD->getOperand(i));
      SavedMD.push_back(std::move(S));
      Mod.eraseNamedMetadata(NMD);
    }
  return true;
}

void ParallelCGContext::preparePartitions(StringRef BCRef, TargetMachine &TM) {
  Parts.resize(NumPartitions);
  unsigned PrepThreadCount = pcgWorkerThreads(NumPartitions, WorkerThreadCap);
  std::atomic<unsigned> PrepNextPart{0};
  // Use llvm::thread, not std::thread: these workers run lazy bitcode
  // materialization and (in the opt path) the full optimization + codegen
  // pipeline, all of which recurse deeply (InstCombine, SCEV, value tracking,
  // SelectionDAG ISel).  std::thread inherits the platform default stack, which
  // on macOS is only 512 KiB and overflows -- crashing a worker with SIGILL --
  // on pathologically deep IR (e.g. a long inlined call chain).  llvm::thread
  // sizes its worker stacks for exactly this reason (DefaultStackSize: 8 MiB on
  // Linux/macOS, 64 MiB on Windows, whose fatter x64 frames overflowed 8 MiB on
  // adversarial caps-off IR) and is what LLVM's own parallel codegen
  // (splitCodeGen) uses; this brings the workers to parity with the main
  // thread.
  std::vector<llvm::thread> PrepWorkers;
  PrepWorkers.reserve(PrepThreadCount);

  auto PrepWorker = [&]() {
    while (true) {
      unsigned p = PrepNextPart.fetch_add(1, std::memory_order_relaxed);
      if (p >= NumPartitions)
        break;
      auto PP = std::make_unique<PreparedPartition>();
      PP->ObjBuf = &Results[p].ObjBuffer;
      if (EmitSplitDwarf)
        PP->DwoBuf = &Results[p].DwoBuffer;
      PP->Ctx = std::make_unique<LLVMContext>();
      PP->Ctx->setDiscardValueNames(true);
      PP->Ctx->setDiagnosticHandler(
          std::make_unique<PartitionDiagnosticHandler>(Results[p]));
      auto MOrErr =
          getLazyBitcodeModule(MemoryBufferRef(BCRef, "lto-pcg"), *PP->Ctx);
      if (!MOrErr) {
        consumeError(MOrErr.takeError());
        continue;
      }
      PP->M = std::move(*MOrErr);
      auto &MPart = *PP->M;

      // Materialize only the functions assigned to this partition; every
      // other body stays unparsed in the bitcode buffer, keeping prepare
      // CPU at O(total IR) instead of O(partitions x total IR).
      // Global-variable initializers are not lazy (the reader resolves
      // them when module parsing suspends at the first function block),
      // so the p != 0 initializer dropping below needs no materialization.
      bool Failed = false;
      for (const std::string &Name : Assignments[p]) {
        Function *F = MPart.getFunction(Name);
        if (!F || F->isDeclaration())
          continue;
        if (auto Err = F->materialize()) {
          consumeError(std::move(Err));
          Failed = true;
          break;
        }
      }
      if (Failed)
        continue;
      if (p != 0) {
        surrenderModuleWideConstructs(MPart);

        for (GlobalVariable &GV : make_early_inc_range(MPart.globals())) {
          // A declaration has no initializer to give up, and the appending
          // globals were settled above.
          if (GV.isDeclaration() || GV.hasAppendingLinkage())
            continue;
          // unnamed_addr constants (string literals) are kept as local
          // definitions in every partition — skip the initializer drop.
          if (GV.hasGlobalUnnamedAddr() && GV.isConstant())
            continue;
          // An alias still names this variable when materializeAll() runs the
          // verifier below, and an alias may not point at a declaration.
          if (IndirectTargets.contains(GV.getName()))
            continue;
          GV.setInitializer(nullptr);
          GV.setLinkage(GlobalValue::ExternalLinkage);
          // The variable is a declaration now, and a declaration may not sit
          // in a comdat.  Functions get the same treatment where their bodies
          // are dropped below; COFF is where it shows, because that is where
          // LLVM gives every weak_odr definition a comdat to begin with.
          GV.setComdat(nullptr);
        }
      }
      // deleteBody() also clears the materializable bit, so unassigned
      // functions (still lazy, hence !isDeclaration()) become true
      // declarations without their bodies ever being parsed.
      for (Function &F : MPart) {
        if (F.isDeclaration())
          continue;
        auto It = FuncPartition.find(F.getName());
        if (It != FuncPartition.end() && It->second == p)
          continue;
        // An alias is always a definition and must resolve to one.  Aliases
        // live in partition 0 and are replaced by declarations everywhere
        // else, but that replacement needs a complete use-list and so cannot
        // run until materializeAll() below -- which is itself what verifies
        // the module.  Dropping the target's body here would therefore leave
        // a dangling alias for exactly as long as it takes the verifier to
        // reject it.  So keep it for now and drop it right after the aliases
        // go, further down: an ordinary target is a strong definition, and one
        // per partition is a duplicate the merge refuses.
        if (IndirectTargets.contains(F.getName()))
          continue;
        F.deleteBody();
        F.setComdat(nullptr);
      }
      // Assigned bodies are materialized and unassigned bodies have been
      // dropped, so draining the remaining module tail is cheap.  It must
      // happen before any use-list operation or optimization pass: LLVM
      // deliberately rejects use-list queries while a module still owns a
      // materializer because those lists may be incomplete.
      if (Error Err = MPart.materializeAll()) {
        consumeError(std::move(Err));
        continue;
      }
      // externalizeAndSerialize renames symbols with a ".__pcg" suffix, but
      // Comdat keys keep their pre-rename names.  COFF lowering then looks up
      // the Comdat name as a GlobalValue and fatals with "Associative COMDAT
      // symbol 'X' does not exist" (Windows -fbuiltin-string under PCG).  Drop
      // every Comdat whose key no longer resolves to a GV that still owns it.
      // Within a single TU, linkonce_odr coalescing is already done; the
      // native multi-TU link sees the merged object.
      for (GlobalObject &GO : MPart.global_objects()) {
        Comdat *C = GO.getComdat();
        if (!C)
          continue;
        GlobalValue *Key = MPart.getNamedValue(C->getName());
        if (!Key || Key->getComdat() != C)
          GO.setComdat(nullptr);
      }
      if (p != 0) {
        // Aliases / ifuncs are retained only in partition 0 (with their
        // targets pinned there).  Other partitions still contain call sites
        // that named the alias; eraseFromParent() would leave dangling uses
        // and crash codegen.  Replace each with an external declaration of
        // the same name/type so cross-partition references stay resolvable
        // at object-merge / native-link time.  Preserve visibility /
        // DLL-storage / dso_local so the replacement emits the same reloc
        // form the alias would have.
        auto ReplaceIndirectWithExternal = [](GlobalValue &Old) {
          Type *Ty = Old.getValueType();
          unsigned AS = Old.getAddressSpace();
          Module *M = Old.getParent();
          GlobalValue *Repl = nullptr;
          if (auto *FTy = dyn_cast<FunctionType>(Ty))
            Repl = Function::Create(FTy, GlobalValue::ExternalLinkage, AS,
                                    /*Name=*/"", M);
          else
            // Carry the thread-local mode over: a declaration that loses it
            // is accessed through an ordinary symbol reference, which emits a
            // different relocation than the TLS access the alias stood for.
            Repl = new GlobalVariable(
                *M, Ty, /*isConstant=*/false, GlobalValue::ExternalLinkage,
                /*Init=*/nullptr, /*Name=*/"", /*InsertBefore=*/nullptr,
                Old.getThreadLocalMode(), AS);
          Repl->setVisibility(Old.getVisibility());
          Repl->setDLLStorageClass(Old.getDLLStorageClass());
          Repl->setUnnamedAddr(Old.getUnnamedAddr());
          Repl->setDSOLocal(Old.isDSOLocal());
          Repl->setLinkage(GlobalValue::ExternalLinkage);
          Repl->takeName(&Old);
          if (Repl->getType() != Old.getType())
            Old.replaceAllUsesWith(
                ConstantExpr::getPointerCast(Repl, Old.getType()));
          else
            Old.replaceAllUsesWith(Repl);
          Old.eraseFromParent();
        };
        for (GlobalAlias &GA : make_early_inc_range(MPart.aliases()))
          ReplaceIndirectWithExternal(GA);
        for (GlobalIFunc &GI : make_early_inc_range(MPart.ifuncs()))
          ReplaceIndirectWithExternal(GI);

        // No alias names them any more, so their definitions have served their
        // purpose: they existed only to keep the module well-formed for the
        // verifier that materializeAll() runs, which is too early to have
        // removed the aliases.  Carrying them into codegen would put one
        // definition per partition into the merge, and for an ordinary
        // (non-coalescible) target that is a duplicate the merge must refuse.
        for (StringRef Name : IndirectTargets.keys()) {
          GlobalValue *Target = MPart.getNamedValue(Name);
          if (!Target || Target->isDeclaration())
            continue;
          if (auto *F = dyn_cast<Function>(Target)) {
            auto It = FuncPartition.find(Name);
            if (It != FuncPartition.end() && It->second == p)
              continue; // this partition owns the body
            F->deleteBody();
            F->setComdat(nullptr);
          } else if (auto *Var = dyn_cast<GlobalVariable>(Target)) {
            // unnamed_addr constants stay defined everywhere on purpose (see
            // the initializer loop above); they are local, so they cannot
            // collide.
            if (Var->hasGlobalUnnamedAddr() && Var->isConstant())
              continue;
            Var->setInitializer(nullptr);
            Var->setLinkage(GlobalValue::ExternalLinkage);
            Var->setComdat(nullptr);
          }
        }
      }
      // Every body this partition gives up has been given up by now, so
      // "still a definition here" finally means what the object will say.
      retainOnlyLocalDefinitions(MPart);

      PP->PTM.reset(TheTarget->createTargetMachine(
          TripleStr, TM.getTargetCPU().str(), SharedFeatures, SharedTgtOpts,
          SharedCM, SharedOptLevel));
      if (!PP->PTM)
        continue;
      static_cast<LLVMTargetMachine *>(PP->PTM.get())
          ->setMachinePipelineHooks(
              static_cast<LLVMTargetMachine &>(TM).getMachinePipelineHooks());
      MPart.setDataLayout(PP->PTM->createDataLayout());

      if (Cache && Cache->enabled()) {
        // Key = this partition's exact post-IPO bitcode.  Serializing it
        // is now safe because the lazy materializer was drained above.
        stripUnreferencedDeclarations(MPart);
        SmallString<0> PartBC;
        {
          raw_svector_ostream BCOS(PartBC);
          WriteBitcodeToFile(MPart, BCOS, false);
        }
        if (Cache->Lookup(PipeTag, StringRef(PartBC.data(), PartBC.size()),
                          PP->CacheKey, Results[p].ObjBuffer)) {
          // Hit: object restored; leave Parts[p] empty so the codegen
          // worker skips this partition.
          Results[p].Success = true;
          continue;
        }
      }
      Parts[p] = std::move(PP);
    }
  };
  for (unsigned i = 0; i < PrepThreadCount; ++i)
    PrepWorkers.emplace_back(PrepWorker);
  for (auto &T : PrepWorkers)
    T.join();
}

bool ParallelCGContext::finalizeResults(Module &Mod,
                                        ParallelCodeGenOutputs Outputs) {
  bool Dbg = ::getenv("NEVERC_PCG_DEBUG") != nullptr;

  // Bail out of the parallel path *after* we have committed to it (the module
  // is already externalized and the partitions are codegen'd).  Restoring
  // linkage and returning false lets the caller silently fall back to serial
  // codegen, which guarantees a correct object — but that very silence is a
  // blind spot: a build that should exercise the merger keeps passing, just
  // slower, so a reintroduced offset-collapse bug (the historical one) turns no
  // test red.  NEVERC_PCG_STRICT closes the gap: set it in CI / differential
  // runs and any post-commit failure aborts loudly with the precise reason,
  // instead of degrading into a fallback that hides the regression.  Unset (the
  // default) the behavior is byte-for-byte the old fallback.
  auto bail = [&](const Twine &Reason) -> bool {
    if (Dbg)
      errs() << "[pcg] FALLBACK: " << Reason << "\n";
    if (::getenv("NEVERC_PCG_STRICT") != nullptr)
      report_fatal_error(
          "neverc: NEVERC_PCG_STRICT is set but parallel codegen could not "
          "emit a merged object (" +
              Reason +
              "); refusing the serial fallback that would mask the regression",
          /*gen_crash_diag=*/false);
    restoreLinkage(Mod);
    return false;
  };

  // What the partitions recorded is re-raised here and nowhere else: this is
  // the thread the caller is waiting on, and its module carries the only
  // context in the parallel path with a diagnostic consumer behind it (see
  // PartitionDiagnosticHandler).  The partitions are one module split N ways,
  // so N copies of one message describe one problem; each distinct text is
  // reported once rather than once per partition that ran into it.
  StringSet<> Reported;
  for (unsigned i = 0; i < NumPartitions; ++i)
    for (const std::string &Message : Results[i].Errors)
      if (Reported.insert(Message).second)
        Mod.getContext().emitError(Message);

  // Standing down because the compilation failed, not because the parallel path
  // did.  This deliberately does not go through bail(): NEVERC_PCG_STRICT is
  // there to catch the merge quietly degrading into the serial fallback, and an
  // error in the program being compiled is not that.
  if (!Reported.empty()) {
    if (Dbg)
      errs() << "[pcg] standing down: " << Reported.size()
             << " partition error(s) reported\n";
    restoreLinkage(Mod);
    return false;
  }

  bool AllOK = true;
  for (unsigned i = 0; i < NumPartitions; ++i)
    if (!Results[i].Success) {
      AllOK = false;
      if (Dbg)
        errs() << "[pcg] partition " << i << " did not succeed\n";
    }
  if (!AllOK)
    return bail("not all " + Twine(NumPartitions) + " partitions succeeded");

  unsigned NonEmpty = 0, SingleIdx = 0;
  for (unsigned i = 0; i < NumPartitions; ++i)
    if (!Results[i].ObjBuffer.empty()) {
      NonEmpty++;
      SingleIdx = i;
    }
  if (Dbg)
    errs() << "[pcg] all " << NumPartitions
           << " partitions ok, NonEmpty=" << NonEmpty << "\n";
  if (NonEmpty == 0)
    // Every partition reported success yet produced no bytes.  Codegen always
    // emits at least a header, so this is never expected; rather than write an
    // empty object, fall back (or, under strict mode, surface the anomaly).
    return bail("every partition succeeded but produced no object bytes");

  SmallVector<SmallVector<char, 0>, 8> ObjectBufs;
  for (unsigned i = 0; i < NumPartitions; ++i)
    ObjectBufs.push_back(std::move(Results[i].ObjBuffer));

  // A merge/verify failure must leave the module exactly as lto::backend's
  // serial fallback expects: every symbol externalized for cross-partition
  // references (the ".__pcg<hash>" rename to ExternalLinkage/HiddenVisibility)
  // restored to its original local linkage, visibility, and name (bail() does
  // this).  Without it, the deferred function-opt + serial codegen downstream
  // would run on a polluted module and emit what should be local symbols as
  // externalized ".__pcg" globals — the exact silent symbol-table corruption
  // the merge verifier exists to refuse.
  SmallVector<char, 0> MergedObject;
  merge::Options ObjectOpts;
  if (FinalizeDebugCompression)
    ObjectOpts.debugCompression = FinalDebugCompression;
  if (NonEmpty == 1 && !EmitSplitDwarf && !FinalizeDebugCompression) {
    // No cross-partition references exist, and this partition was already
    // compressed (if requested) by its object writer.
    MergedObject = std::move(ObjectBufs[SingleIdx]);
  } else if (!mergePartitionObjects(TT, ObjectBufs, MergedObject, ObjectOpts)) {
    return bail("partition object merge/self-verify failed");
  }

  SmallVector<char, 0> DwarfPackage;
  if (EmitSplitDwarf) {
    if (!Outputs.DwarfPackage)
      return bail("split-DWARF mode has no destination stream");

    SmallVector<SmallVector<char, 0>, 8> DwoBufs;
    unsigned NonEmptyDwo = 0;
    for (unsigned I = 0; I < NumPartitions; ++I) {
      NonEmptyDwo += !Results[I].DwoBuffer.empty();
      DwoBufs.push_back(std::move(Results[I].DwoBuffer));
    }
    if (NonEmptyDwo == 0)
      return bail("every partition omitted its split-DWARF object");

    merge::Options DwoOpts;
    DwoOpts.artifact = merge::ArtifactKind::SplitDwarf;
    DwoOpts.debugCompression = FinalDebugCompression;
    if (!mergePartitionObjects(TT, DwoBufs, DwarfPackage, DwoOpts))
      return bail("partition split-DWARF merge/self-verify failed");

    std::optional<merge::Format> Fmt = mergeFormatForTriple(TT);
    if (!Fmt)
      return bail("unsupported split-DWARF object format");
    std::string VerifyError;
    if (!merge::verifySplitDwarfPair(MergedObject, DwarfPackage, *Fmt,
                                     &VerifyError)) {
      return bail("main/DWP cross-artifact verification failed: " +
                  VerifyError);
    }
  }

  // Commit only after both in-memory merges and the pair verification succeed.
  // Any earlier failure therefore leaves both real streams untouched for the
  // serial fallback.
  Outputs.Object.write(MergedObject.data(), MergedObject.size());
  if (Outputs.DwarfPackage)
    Outputs.DwarfPackage->write(DwarfPackage.data(), DwarfPackage.size());
  if (Dbg)
    errs() << "[pcg] SUCCESS: merged " << NonEmpty << " partition objects"
           << (EmitSplitDwarf ? " and split-DWARF contributions" : "") << "\n";
  return true;
}

void ParallelCGContext::restoreLinkage(Module &Mod) {
  for (auto &E : SavedLinkage) {
    E.GV->setName(E.OrigName);
    E.GV->setLinkage(E.Linkage);
    E.GV->setVisibility(E.Visibility);
    if (auto *F = dyn_cast<Function>(E.GV)) {
      if (E.AddedOriginalLocalAttr)
        F->removeFnAttr(PCGOriginalLocalAttr);
      if (E.AddedOriginalAddressTakenAttr)
        F->removeFnAttr(PCGOriginalAddressTakenAttr);
    }
  }
  for (auto &S : SavedMD) {
    auto *NMD = Mod.getOrInsertNamedMetadata(S.Name);
    for (auto *Op : S.Operands)
      NMD->addOperand(Op);
  }
}

} // namespace

bool finalizeSplitDwarfArtifacts(const Triple &Target, ArrayRef<char> Object,
                                 ArrayRef<char> Dwo,
                                 DebugCompressionType DebugCompression,
                                 ParallelCodeGenOutputs Outputs,
                                 std::string *Error) {
  auto fail = [&](const Twine &Reason) {
    if (Error)
      *Error = Reason.str();
    return false;
  };
  if (!Outputs.DwarfPackage)
    return fail("split-DWARF package has no destination stream");
  if (Object.empty() || Dwo.empty())
    return fail("serial codegen produced an empty split-DWARF artifact");

  const std::optional<merge::Format> Fmt = mergeFormatForTriple(Target);
  if (!Fmt || (*Fmt != merge::Format::ELF64LE && *Fmt != merge::Format::COFF))
    return fail("unsupported split-DWARF package object format");

  SmallVector<char, 0> FinalObject;
  if (DebugCompression == DebugCompressionType::None) {
    FinalObject.append(Object.begin(), Object.end());
  } else {
    SmallVector<StringRef, 1> ObjectInputs;
    ObjectInputs.emplace_back(Object.data(), Object.size());
    merge::Options ObjectOpts;
    ObjectOpts.debugCompression = DebugCompression;
    raw_svector_ostream ObjectOS(FinalObject);
    if (!merge::mergeObjects(ObjectInputs, ObjectOS, *Fmt, ObjectOpts))
      return fail("serial main-object compression/self-verification failed");
  }

  SmallVector<StringRef, 1> DwoInputs;
  DwoInputs.emplace_back(Dwo.data(), Dwo.size());
  SmallVector<char, 0> Package;
  raw_svector_ostream PackageOS(Package);
  merge::Options PackageOpts;
  PackageOpts.artifact = merge::ArtifactKind::SplitDwarf;
  PackageOpts.debugCompression = DebugCompression;
  if (!merge::mergeObjects(DwoInputs, PackageOS, *Fmt, PackageOpts))
    return fail("serial split-DWARF packaging/self-verification failed");

  std::string VerifyError;
  if (!merge::verifySplitDwarfPair(FinalObject, Package, *Fmt, &VerifyError))
    return fail("serial main/DWP verification failed: " + VerifyError);

  Outputs.Object.write(FinalObject.data(), FinalObject.size());
  Outputs.DwarfPackage->write(Package.data(), Package.size());
  return true;
}

// ===----------------------------------------------------------------------===
// Public API: parallel codegen (no per-partition optimization)
// ===----------------------------------------------------------------------===

// Legacy PM pass configuration (addPassesToEmitFile) touches LLVM global
// state (pass registry, target-specific lazy init).  Serialize it so
// only the actual PM.run() is concurrent.
static std::mutex PassConfigMutex;

bool runParallelCodeGen(Module &Mod, TargetMachine &TM,
                        ParallelCodeGenOutputs Outputs,
                        unsigned /*NumPartitions*/,
                        const PartitionCacheHooks *Cache) {
  ParallelCGContext Ctx;
  if (!Ctx.init(Mod, TM, Outputs.DwarfPackage != nullptr))
    return false;
  // The partition cache stores only one object image. A cache hit in fission
  // mode would omit its matching DWO payload.
  Ctx.Cache = Ctx.EmitSplitDwarf ? nullptr : Cache;
  Ctx.PipeTag = "p-cg";

  if (!Ctx.resolvePartitions(/*WeightDiv=*/PcgCgWeightDiv,
                             /*LoopDiv=*/PcgCgLoopDiv,
                             /*MaxParts=*/PcgCgMaxParts))
    return false;

  if (!Ctx.externalizeAndSerialize(Mod))
    return false;
  StringRef BCRef(Ctx.FullBC.data(), Ctx.FullBC.size());
  Ctx.preparePartitions(BCRef, TM);

  {
    // Codegen-only path: no per-partition optimization runs here, so leave
    // WorkerThreadCap at 0 (no work proportioning) and use the full P-core/HW
    // pool -- codegen is the cheap half and is not the over-threading risk the
    // proportioning targets.
    unsigned ThreadCount =
        pcgWorkerThreads(Ctx.NumPartitions, Ctx.WorkerThreadCap);
    std::atomic<unsigned> NextPart{0};
    // llvm::thread (DefaultStackSize: 8 MiB Linux/macOS, 64 MiB Windows), not
    // std::thread (512 KiB on macOS): see the rationale in preparePartitions --
    // the codegen/opt pipeline recurses deeply and overflows a small stack on
    // adversarial IR.
    std::vector<llvm::thread> Workers;
    Workers.reserve(ThreadCount);
    auto Worker = [&]() {
      while (true) {
        unsigned p = NextPart.fetch_add(1, std::memory_order_relaxed);
        if (p >= Ctx.NumPartitions)
          break;
        if (!Ctx.Parts[p])
          continue;
        auto &PP = *Ctx.Parts[p];
        raw_svector_ostream ObjOS(*PP.ObjBuf);
        SmallVector<char, 0> UnusedDwo;
        raw_svector_ostream DwoOS(PP.DwoBuf ? *PP.DwoBuf : UnusedDwo);
        legacy::PassManager PM;
        {
          std::lock_guard<std::mutex> Lock(PassConfigMutex);
          PM.add(createTargetTransformInfoWrapperPass(
              PP.PTM->getTargetIRAnalysis()));
          PM.add(new TargetLibraryInfoWrapperPass(*Ctx.SharedTLII));
          if (PP.PTM->addPassesToEmitFile(PM, ObjOS,
                                          PP.DwoBuf ? &DwoOS : nullptr,
                                          CodeGenFileType::ObjectFile, true))
            continue;
        }
        PM.run(*PP.M);
        // A recorded error condemns whatever the pipeline went on to produce,
        // however finished the object looks.
        Ctx.Results[p].Success = Ctx.Results[p].Errors.empty();
        if (Ctx.Results[p].Success && Ctx.Cache && !PP.CacheKey.empty())
          Ctx.Cache->Store(PP.CacheKey, *PP.ObjBuf);
      }
    };
    for (unsigned i = 0; i < ThreadCount; ++i)
      Workers.emplace_back(Worker);
    for (auto &T : Workers)
      T.join();
  }

  return Ctx.finalizeResults(Mod, Outputs);
}

// ===----------------------------------------------------------------------===
// Public API: parallel optimization + codegen
// ===----------------------------------------------------------------------===

bool runParallelOptAndCodeGen(Module &Mod, TargetMachine &TM,
                              ParallelCodeGenOutputs Outputs,
                              unsigned /*NumPartitions*/, unsigned OptLevel,
                              const PartitionCacheHooks *Cache,
                              const ParallelOptimizationHooks *Hooks) {
  if (OptLevel == 0)
    return false;

  ParallelCGContext Ctx;
  if (!Ctx.init(Mod, TM, Outputs.DwarfPackage != nullptr)) {
    if (::getenv("NEVERC_PCG_DEBUG"))
      errs() << "[pcg] p-opt declined (FuncCount=" << Ctx.FuncCount
             << " TotalWeight=" << Ctx.TotalWeight
             << " LoopCount=" << Ctx.LoopCount << ")\n";
    return false;
  }
  Ctx.Cache = Ctx.EmitSplitDwarf ? nullptr : Cache;
  Ctx.PipeTag = "p-opt";

  if (!Ctx.resolvePartitions(/*WeightDiv=*/PcgOptWeightDiv,
                             /*LoopDiv=*/PcgOptLoopDiv,
                             /*MaxParts=*/PcgOptMaxParts))
    return false;
  // Size the worker pool to the parallel work this module justifies (loop- and
  // instruction-driven), clamped to the performance-core ceiling inside
  // pcgWorkerThreads.  Set before prepare so prepare and opt/codegen agree.
  // Pure compile-wall-time knob: the cap feeds only thread counts, never the
  // partition count or assignment, so the merged object stays byte-identical.
  Ctx.WorkerThreadCap = pcgWorkCapThreads(Ctx.LoopCount, Ctx.TotalWeight);

  if (::getenv("NEVERC_PCG_DEBUG"))
    errs() << "[pcg] p-opt engaged: FuncCount=" << Ctx.FuncCount
           << " TotalWeight=" << Ctx.TotalWeight
           << " LoopCount=" << Ctx.LoopCount
           << " NumPartitions=" << Ctx.NumPartitions
           << " WorkerThreadCap=" << Ctx.WorkerThreadCap << "\n";

  if (!Ctx.externalizeAndSerialize(Mod))
    return false;
  StringRef BCRef(Ctx.FullBC.data(), Ctx.FullBC.size());
  Ctx.preparePartitions(BCRef, TM);

  OptimizationLevel OL;
  switch (OptLevel) {
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

  PipelineTuningOptions SharedPTO;
  SharedPTO.LoopUnrolling = OptLevel >= 2;
  SharedPTO.LoopInterleaving = OptLevel >= 2;
  SharedPTO.LoopVectorization = OptLevel >= 2;
  SharedPTO.SLPVectorization = OptLevel >= 2;
  SharedPTO.CallGraphProfile = false;
  SharedPTO.NevercFastIPO = true;
  SharedPTO.NevercIndVarWidenMaxFunctionLoops =
      PcgIndVarWidenMaxFunctionLoops;

  // Bound SCEV's huge-expression cost for every partition's optimization (see
  // PcgScevHugeExprThreshold).  Established here, before any worker starts, and
  // restored when this guard leaves scope after the workers below have joined.
  ScevHugeThresholdGuard ScevGuard(PcgScevHugeExprThreshold);

  // Phase 2: per-partition optimization + codegen.
  {
    unsigned ThreadCount =
        pcgWorkerThreads(Ctx.NumPartitions, Ctx.WorkerThreadCap);
    std::atomic<unsigned> NextPart{0};
    // llvm::thread (DefaultStackSize: 8 MiB Linux/macOS, 64 MiB Windows), not
    // std::thread (512 KiB on macOS): see the rationale in preparePartitions --
    // the codegen/opt pipeline recurses deeply and overflows a small stack on
    // adversarial IR.
    std::vector<llvm::thread> Workers;
    Workers.reserve(ThreadCount);
    auto Worker = [&]() {
      while (true) {
        unsigned p = NextPart.fetch_add(1, std::memory_order_relaxed);
        if (p >= Ctx.NumPartitions)
          break;
        if (!Ctx.Parts[p])
          continue;
        auto &PP = *Ctx.Parts[p];
        auto &MPart = *PP.M;

        {
          LoopAnalysisManager LAM;
          FunctionAnalysisManager FAM;
          CGSCCAnalysisManager CGAM;
          ModuleAnalysisManager MAM;
          PassInstrumentationCallbacks PIC;
          StandardInstrumentations SI(MPart.getContext(), false, false);
          SI.registerCallbacks(PIC, &MAM);
          PassBuilder PB(PP.PTM.get(), SharedPTO, std::nullopt, &PIC);
          if (Hooks && Hooks->ConfigurePassBuilder)
            Hooks->ConfigurePassBuilder(PB);
          FAM.registerPass(
              [&] { return TargetLibraryAnalysis(*Ctx.SharedTLII); });
          PB.registerModuleAnalyses(MAM);
          PB.registerCGSCCAnalyses(CGAM);
          PB.registerFunctionAnalyses(FAM);
          PB.registerLoopAnalyses(LAM);
          PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

          ModulePassManager MPM;
          if (Hooks && Hooks->PreOpt)
            Hooks->PreOpt(MPM);
          MPM.addPass(PB.buildModuleOptimizationPipeline(
              OL, ThinOrFullLTOPhase::FullLTOPostLink));
          if (Hooks && Hooks->PostOpt)
            Hooks->PostOpt(MPM);
          MPM.run(MPart, MAM);
        }

        raw_svector_ostream ObjOS(*PP.ObjBuf);
        SmallVector<char, 0> UnusedDwo;
        raw_svector_ostream DwoOS(PP.DwoBuf ? *PP.DwoBuf : UnusedDwo);
        legacy::PassManager PM;
        {
          std::lock_guard<std::mutex> Lock(PassConfigMutex);
          PM.add(createTargetTransformInfoWrapperPass(
              PP.PTM->getTargetIRAnalysis()));
          PM.add(new TargetLibraryInfoWrapperPass(*Ctx.SharedTLII));
          if (PP.PTM->addPassesToEmitFile(PM, ObjOS,
                                          PP.DwoBuf ? &DwoOS : nullptr,
                                          CodeGenFileType::ObjectFile, true))
            continue;
        }
        PM.run(MPart);
        // A recorded error condemns whatever the pipeline went on to produce,
        // however finished the object looks.
        Ctx.Results[p].Success = Ctx.Results[p].Errors.empty();
        if (Ctx.Results[p].Success && Ctx.Cache && !PP.CacheKey.empty())
          Ctx.Cache->Store(PP.CacheKey, *PP.ObjBuf);
      }
    };
    for (unsigned i = 0; i < ThreadCount; ++i)
      Workers.emplace_back(Worker);
    for (auto &T : Workers)
      T.join();
  }

  return Ctx.finalizeResults(Mod, Outputs);
}

} // namespace neverc
