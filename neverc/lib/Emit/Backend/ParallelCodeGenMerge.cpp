#include "neverc/Emit/Backend/ParallelCodeGenMerge.h"
#include "neverc/Merge/Merger.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/thread.h"
#include "llvm/Support/xxhash.h"

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <thread>

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

bool mergePartitionObjects(const Triple &TT,
                           ArrayRef<SmallVector<char, 0>> Bufs,
                           raw_pwrite_stream &OS) {
  using namespace neverc::merge;
  // Test-only fault injection: pretend the merge failed so the serial-codegen
  // safety net (finalizeResults' bail -> restoreLinkage -> the LTO backend's
  // serial codegen fallback) can be exercised end to end and proven to still
  // produce a correct binary.  Production never sets this; under
  // NEVERC_PCG_STRICT a forced failure still aborts, exactly as any real merge
  // failure would, so the variable cannot mask a regression when strict is on.
  if (::getenv("NEVERC_PCG_FORCE_MERGE_FAIL") != nullptr)
    return false;
  if (TT.isOSBinFormatCOFF())
    return mergeObjects(Bufs, OS, Format::COFF);
  if (TT.isOSBinFormatELF())
    return mergeObjects(Bufs, OS, Format::ELF64LE);
  if (TT.isOSBinFormatMachO())
    return mergeObjects(Bufs, OS, Format::MachO64);
  return false;
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
};

struct PartitionResult {
  SmallVector<char, 0> ObjBuffer;
  bool Success = false;
};

struct PreparedPartition {
  std::unique_ptr<LLVMContext> Ctx;
  std::unique_ptr<Module> M;
  std::unique_ptr<TargetMachine> PTM;
  SmallVector<char, 0> *ObjBuf = nullptr;
  /// Partition cache entry key; empty when caching is off or the key
  /// could not be computed.  Set by preparePartitions on a miss, consumed
  /// by the codegen worker to store the produced object.
  std::string CacheKey;
  PreparedPartition() = default;
};

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

  std::vector<PartitionResult> Results;
  CodeModel::Model SharedCM;
  CodeGenOptLevel SharedOptLevel;
  std::string SharedFeatures;
  TargetOptions SharedTgtOpts;

  SmallVector<LinkageEntry, 64> SavedLinkage;

  struct SavedNamedMD {
    std::string Name;
    SmallVector<MDNode *, 4> Operands;
  };
  SmallVector<SavedNamedMD, 4> SavedMD;

  SmallVector<SmallVector<std::string, 0>, 8> Assignments;
  DenseMap<StringRef, unsigned> FuncPartition;
  SmallString<0> FullBC;

  std::unique_ptr<TargetLibraryInfoImpl> SharedTLII;
  std::vector<std::unique_ptr<PreparedPartition>> Parts;

  /// Optional per-partition object cache (linker-injected) and the
  /// pipeline tag distinguishing the two public entry points, whose
  /// outputs differ for identical partition bitcode.
  const PartitionCacheHooks *Cache = nullptr;
  StringRef PipeTag;

  bool init(Module &Mod, TargetMachine &TM);
  bool resolvePartitions(unsigned WeightDiv, unsigned LoopDiv, unsigned MaxParts);
  void externalizeAndSerialize(Module &Mod);
  void preparePartitions(StringRef BCRef, TargetMachine &TM);
  bool finalizeResults(Module &Mod, raw_pwrite_stream &OS);
  void restoreLinkage(Module &Mod);
};

bool ParallelCGContext::init(Module &Mod, TargetMachine &TM) {
  TheTarget = &TM.getTarget();
  TripleStr = Mod.getTargetTriple();
  TT = Triple(TripleStr);

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
  // one function) and HW/MaxParts bound it from above.
  unsigned ByWeight = WeightDiv ? TotalWeight / WeightDiv : 0;
  unsigned ByLoops = LoopDiv ? LoopCount / LoopDiv : 0;
  unsigned WorkParts = std::max({ByWeight, ByLoops, 2u});
  if (NumPartitions == 0) {
    unsigned HW = llvm::thread::hardware_concurrency();
    NumPartitions = std::min({HW, WorkParts, FuncCount, MaxParts});
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

void ParallelCGContext::externalizeAndSerialize(Module &Mod) {
  SmallString<32> PCGSuffix;
  {
    auto H = hash_value(Mod.getModuleIdentifier());
    raw_svector_ostream(PCGSuffix) << merge::PcgSymbolMarker << (H & 0xFFFFFFFF);
  }

  auto ExternalizeGV = [&](GlobalValue &GV) {
    if (!GV.hasLocalLinkage())
      return;
    SavedLinkage.push_back(
        {&GV, GV.getName().str(), GV.getLinkage(), GV.getVisibility()});
    SmallString<64> NewName(GV.getName());
    NewName += PCGSuffix;
    GV.setName(NewName);
    GV.setLinkage(GlobalValue::ExternalLinkage);
    GV.setVisibility(GlobalValue::HiddenVisibility);
  };
  for (Function &F : Mod)
    ExternalizeGV(F);
  for (GlobalVariable &GV : Mod.globals())
    ExternalizeGV(GV);
  for (GlobalAlias &GA : Mod.aliases())
    ExternalizeGV(GA);
  for (GlobalIFunc &IF : Mod.ifuncs())
    ExternalizeGV(IF);

  DenseSet<StringRef> PinnedToP0;
  for (GlobalAlias &GA : Mod.aliases())
    if (auto *F = dyn_cast<Function>(GA.getAliasee()->stripPointerCasts()))
      PinnedToP0.insert(F->getName());
  for (GlobalIFunc &IF : Mod.ifuncs())
    if (auto *F = dyn_cast<Function>(IF.getResolver()->stripPointerCasts()))
      PinnedToP0.insert(F->getName());

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
    // Drop empty bins (possible at small function counts).  Bin 0 keeps
    // its slot whenever it is non-empty, so alias/ifunc targets stay in
    // the partition that retains aliases and global initializers.
    Assignments.clear();
    for (auto &Bin : Bins)
      if (!Bin.empty())
        Assignments.push_back(std::move(Bin));
    NumPartitions = Assignments.size();
    Results.resize(NumPartitions);
    for (unsigned p = 0; p < NumPartitions; ++p)
      for (auto &N : Assignments[p])
        FuncPartition[N] = p;
  }

  Mod.dropTriviallyDeadConstantArrays();
  for (StringRef MDName :
       {"llvm.ident", "llvm.linker.options", "llvm.dependent-libraries"})
    if (auto *NMD = Mod.getNamedMetadata(MDName)) {
      SavedNamedMD S;
      S.Name = MDName.str();
      for (unsigned i = 0; i < NMD->getNumOperands(); ++i)
        S.Operands.push_back(NMD->getOperand(i));
      SavedMD.push_back(std::move(S));
      Mod.eraseNamedMetadata(NMD);
    }
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
}

void ParallelCGContext::preparePartitions(StringRef BCRef, TargetMachine &TM) {
  Parts.resize(NumPartitions);
  unsigned PrepThreadCount =
      std::min(llvm::thread::hardware_concurrency(), NumPartitions);
  std::atomic<unsigned> PrepNextPart{0};
  // Use llvm::thread, not std::thread: these workers run lazy bitcode
  // materialization and (in the opt path) the full optimization + codegen
  // pipeline, all of which recurse deeply (InstCombine, SCEV, value tracking,
  // SelectionDAG ISel).  std::thread inherits the platform default stack, which
  // on macOS is only 512 KiB and overflows -- crashing a worker with SIGILL --
  // on pathologically deep IR (e.g. a long inlined call chain).  llvm::thread
  // defaults to an 8 MiB stack for exactly this reason and is what LLVM's own
  // parallel codegen (splitCodeGen) uses; this brings the workers to parity
  // with the main thread.
  std::vector<llvm::thread> PrepWorkers;
  PrepWorkers.reserve(PrepThreadCount);

  auto PrepWorker = [&]() {
    while (true) {
      unsigned p = PrepNextPart.fetch_add(1, std::memory_order_relaxed);
      if (p >= NumPartitions)
        break;
      auto PP = std::make_unique<PreparedPartition>();
      PP->ObjBuf = &Results[p].ObjBuffer;
      PP->Ctx = std::make_unique<LLVMContext>();
      PP->Ctx->setDiscardValueNames(true);
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
        for (GlobalVariable &GV : make_early_inc_range(MPart.globals())) {
          if (GV.isDeclaration())
            continue;
          if (GV.hasAppendingLinkage()) {
            GV.eraseFromParent();
            continue;
          }
          GV.setInitializer(nullptr);
          GV.setLinkage(GlobalValue::ExternalLinkage);
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
        F.deleteBody();
        F.setComdat(nullptr);
      }
      if (p != 0) {
        for (GlobalAlias &GA : make_early_inc_range(MPart.aliases()))
          GA.eraseFromParent();
        for (GlobalIFunc &IF : make_early_inc_range(MPart.ifuncs()))
          IF.eraseFromParent();
      }

      PP->PTM.reset(TheTarget->createTargetMachine(
          TripleStr, TM.getTargetCPU().str(), SharedFeatures, SharedTgtOpts,
          SharedCM, SharedOptLevel));
      if (!PP->PTM)
        continue;
      MPart.setDataLayout(PP->PTM->createDataLayout());

      if (Cache && Cache->enabled()) {
        // Key = this partition's exact post-IPO bitcode.  Serializing it
        // requires releasing the lazy materializer (assigned bodies are
        // already parsed, every other body was dropped, so this only
        // drains the module tail).  On failure fall through uncached.
        if (Error Err = MPart.materializeAll()) {
          consumeError(std::move(Err));
          continue;
        }
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

bool ParallelCGContext::finalizeResults(Module &Mod, raw_pwrite_stream &OS) {
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
    errs() << "[pcg] all " << NumPartitions << " partitions ok, NonEmpty="
           << NonEmpty << "\n";
  if (NonEmpty == 1) {
    // Exactly one partition produced an object — it is already a complete,
    // self-contained .o (no cross-partition references to stitch), so emit it
    // verbatim without invoking the merger.
    OS.write(Results[SingleIdx].ObjBuffer.data(),
             Results[SingleIdx].ObjBuffer.size());
    return true;
  }
  if (NonEmpty == 0)
    // Every partition reported success yet produced no bytes.  Codegen always
    // emits at least a header, so this is never expected; rather than write an
    // empty object, fall back (or, under strict mode, surface the anomaly).
    return bail("every partition succeeded but produced no object bytes");

  SmallVector<SmallVector<char, 0>, 8> Bufs;
  for (unsigned i = 0; i < NumPartitions; ++i)
    Bufs.push_back(std::move(Results[i].ObjBuffer));

  // A merge/verify failure must leave the module exactly as lto::backend's
  // serial fallback expects: every symbol externalized for cross-partition
  // references (the ".__pcg<hash>" rename to ExternalLinkage/HiddenVisibility)
  // restored to its original local linkage, visibility, and name (bail() does
  // this).  Without it, the deferred function-opt + serial codegen downstream
  // would run on a polluted module and emit what should be local symbols as
  // externalized ".__pcg" globals — the exact silent symbol-table corruption
  // the merge verifier exists to refuse.
  if (!mergePartitionObjects(TT, Bufs, OS))
    return bail("partition object merge/self-verify failed");
  if (Dbg)
    errs() << "[pcg] SUCCESS: merged " << NonEmpty << " partition objects\n";
  return true;
}

void ParallelCGContext::restoreLinkage(Module &Mod) {
  for (auto &E : SavedLinkage) {
    E.GV->setName(E.OrigName);
    E.GV->setLinkage(E.Linkage);
    E.GV->setVisibility(E.Visibility);
  }
  for (auto &S : SavedMD) {
    auto *NMD = Mod.getOrInsertNamedMetadata(S.Name);
    for (auto *Op : S.Operands)
      NMD->addOperand(Op);
  }
}

} // namespace

// ===----------------------------------------------------------------------===
// Public API: parallel codegen (no per-partition optimization)
// ===----------------------------------------------------------------------===

// Legacy PM pass configuration (addPassesToEmitFile) touches LLVM global
// state (pass registry, target-specific lazy init).  Serialize it so
// only the actual PM.run() is concurrent.
static std::mutex PassConfigMutex;

bool runParallelCodeGen(Module &Mod, TargetMachine &TM, raw_pwrite_stream &OS,
                        unsigned /*NumPartitions*/,
                        const PartitionCacheHooks *Cache) {
  ParallelCGContext Ctx;
  if (!Ctx.init(Mod, TM))
    return false;
  Ctx.Cache = Cache;
  Ctx.PipeTag = "p-cg";

  if (!Ctx.resolvePartitions(/*WeightDiv=*/PcgCgWeightDiv,
                             /*LoopDiv=*/PcgCgLoopDiv,
                             /*MaxParts=*/Ctx.FuncCount))
    return false;

  Ctx.externalizeAndSerialize(Mod);
  StringRef BCRef(Ctx.FullBC.data(), Ctx.FullBC.size());
  Ctx.preparePartitions(BCRef, TM);

  {
    unsigned ThreadCount =
        std::min(llvm::thread::hardware_concurrency(), Ctx.NumPartitions);
    std::atomic<unsigned> NextPart{0};
    // llvm::thread (8 MiB default stack), not std::thread (512 KiB on macOS):
    // see the rationale in preparePartitions -- the codegen/opt pipeline
    // recurses deeply and overflows the small default stack on adversarial IR.
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
        legacy::PassManager PM;
        {
          std::lock_guard<std::mutex> Lock(PassConfigMutex);
          PM.add(createTargetTransformInfoWrapperPass(
              PP.PTM->getTargetIRAnalysis()));
          PM.add(new TargetLibraryInfoWrapperPass(*Ctx.SharedTLII));
          if (PP.PTM->addPassesToEmitFile(PM, ObjOS, nullptr,
                                          CodeGenFileType::ObjectFile, true))
            continue;
        }
        PM.run(*PP.M);
        Ctx.Results[p].Success = true;
        if (Ctx.Cache && !PP.CacheKey.empty())
          Ctx.Cache->Store(PP.CacheKey, *PP.ObjBuf);
      }
    };
    for (unsigned i = 0; i < ThreadCount; ++i)
      Workers.emplace_back(Worker);
    for (auto &T : Workers)
      T.join();
  }

  return Ctx.finalizeResults(Mod, OS);
}

// ===----------------------------------------------------------------------===
// Public API: parallel optimization + codegen
// ===----------------------------------------------------------------------===

bool runParallelOptAndCodeGen(Module &Mod, TargetMachine &TM,
                              raw_pwrite_stream &OS, unsigned /*NumPartitions*/,
                              unsigned OptLevel,
                              const PartitionCacheHooks *Cache) {
  if (OptLevel == 0)
    return false;

  ParallelCGContext Ctx;
  if (!Ctx.init(Mod, TM)) {
    if (::getenv("NEVERC_PCG_DEBUG"))
      errs() << "[pcg] p-opt declined (FuncCount=" << Ctx.FuncCount
             << " TotalWeight=" << Ctx.TotalWeight
             << " LoopCount=" << Ctx.LoopCount << ")\n";
    return false;
  }
  Ctx.Cache = Cache;
  Ctx.PipeTag = "p-opt";

  if (!Ctx.resolvePartitions(/*WeightDiv=*/PcgOptWeightDiv,
                             /*LoopDiv=*/PcgOptLoopDiv,
                             /*MaxParts=*/PcgOptMaxParts))
    return false;
  if (::getenv("NEVERC_PCG_DEBUG"))
    errs() << "[pcg] p-opt engaged: FuncCount=" << Ctx.FuncCount
           << " TotalWeight=" << Ctx.TotalWeight
           << " LoopCount=" << Ctx.LoopCount
           << " NumPartitions=" << Ctx.NumPartitions << "\n";

  Ctx.externalizeAndSerialize(Mod);
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

  // Phase 2: per-partition optimization + codegen.
  {
    unsigned ThreadCount =
        std::min(llvm::thread::hardware_concurrency(), Ctx.NumPartitions);
    std::atomic<unsigned> NextPart{0};
    // llvm::thread (8 MiB default stack), not std::thread (512 KiB on macOS):
    // see the rationale in preparePartitions -- the codegen/opt pipeline
    // recurses deeply and overflows the small default stack on adversarial IR.
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
          FAM.registerPass(
              [&] { return TargetLibraryAnalysis(*Ctx.SharedTLII); });
          PB.registerModuleAnalyses(MAM);
          PB.registerCGSCCAnalyses(CGAM);
          PB.registerFunctionAnalyses(FAM);
          PB.registerLoopAnalyses(LAM);
          PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

          ModulePassManager MPM;
          MPM.addPass(PB.buildModuleOptimizationPipeline(
              OL, ThinOrFullLTOPhase::FullLTOPostLink));
          MPM.run(MPart, MAM);
        }

        raw_svector_ostream ObjOS(*PP.ObjBuf);
        legacy::PassManager PM;
        {
          std::lock_guard<std::mutex> Lock(PassConfigMutex);
          PM.add(createTargetTransformInfoWrapperPass(
              PP.PTM->getTargetIRAnalysis()));
          PM.add(new TargetLibraryInfoWrapperPass(*Ctx.SharedTLII));
          if (PP.PTM->addPassesToEmitFile(PM, ObjOS, nullptr,
                                          CodeGenFileType::ObjectFile, true))
            continue;
        }
        PM.run(MPart);
        Ctx.Results[p].Success = true;
        if (Ctx.Cache && !PP.CacheKey.empty())
          Ctx.Cache->Store(PP.CacheKey, *PP.ObjBuf);
      }
    };
    for (unsigned i = 0; i < ThreadCount; ++i)
      Workers.emplace_back(Worker);
    for (auto &T : Workers)
      T.join();
  }

  return Ctx.finalizeResults(Mod, OS);
}

} // namespace neverc
