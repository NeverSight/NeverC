#include "BridgeCastHelpers.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"

using namespace llvm;

namespace neverc {
namespace plugin {

// ===----------------------------------------------------------------------===
//  DominatorTree -- on-demand CFG analysis
// ===----------------------------------------------------------------------===

static NevercDomTreeRef bridgeFunctionBuildDomTree(NevercValueRef F) {
  if (LLVM_UNLIKELY(!F))
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn || Fn->isDeclaration()))
    return nullptr;
  auto *DT = new (std::nothrow) DominatorTree();
  if (LLVM_UNLIKELY(!DT))
    return nullptr;
  DT->recalculate(*Fn);
  return reinterpret_cast<NevercDomTreeRef>(DT);
}

static void bridgeDomTreeDestroy(NevercDomTreeRef DT) {
  if (LLVM_UNLIKELY(!DT))
    return;
  delete reinterpret_cast<DominatorTree *>(DT);
}

static int bridgeDomTreeDominates(NevercDomTreeRef DT,
                                  NevercBasicBlockRef A,
                                  NevercBasicBlockRef B) {
  if (LLVM_UNLIKELY(!DT || !A || !B))
    return 0;
  return reinterpret_cast<DominatorTree *>(DT)->dominates(unwrapBB(A),
                                                          unwrapBB(B));
}

static int bridgeDomTreeProperlyDominates(NevercDomTreeRef DT,
                                          NevercBasicBlockRef A,
                                          NevercBasicBlockRef B) {
  if (LLVM_UNLIKELY(!DT || !A || !B))
    return 0;
  return reinterpret_cast<DominatorTree *>(DT)->properlyDominates(
      unwrapBB(A), unwrapBB(B));
}

static NevercBasicBlockRef bridgeDomTreeGetIDom(NevercDomTreeRef DT,
                                                NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!DT || !BB))
    return nullptr;
  auto *Tree = reinterpret_cast<DominatorTree *>(DT);
  auto *Node = Tree->getNode(unwrapBB(BB));
  if (LLVM_UNLIKELY(!Node))
    return nullptr;
  auto *IDomNode = Node->getIDom();
  return IDomNode ? wrapBB(IDomNode->getBlock()) : nullptr;
}

static int bridgeDomTreeIsReachable(NevercDomTreeRef DT,
                                    NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!DT || !BB))
    return 0;
  return reinterpret_cast<DominatorTree *>(DT)->isReachableFromEntry(
      unwrapBB(BB));
}

static unsigned bridgeDomTreeGetDepth(NevercDomTreeRef DT,
                                      NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!DT || !BB))
    return 0;
  auto *Node =
      reinterpret_cast<DominatorTree *>(DT)->getNode(unwrapBB(BB));
  return LLVM_LIKELY(Node) ? Node->getLevel() : 0;
}

// ===----------------------------------------------------------------------===
//  LoopInfo -- on-demand loop nest analysis
//  Bundles DominatorTree + LoopInfo so the plugin never manages the DomTree
//  dependency.  The DomTree is kept alive because LoopInfo may reference it.
// ===----------------------------------------------------------------------===

namespace {
struct LoopInfoState {
  DominatorTree DT;
  LoopInfo LI;

  explicit LoopInfoState(Function &F) {
    DT.recalculate(F);
    LI.analyze(DT);
  }
};
} // namespace

static NevercLoopInfoRef bridgeFunctionBuildLoopInfo(NevercValueRef F) {
  if (LLVM_UNLIKELY(!F))
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn || Fn->isDeclaration()))
    return nullptr;
  auto *State = new (std::nothrow) LoopInfoState(*Fn);
  if (LLVM_UNLIKELY(!State))
    return nullptr;
  return reinterpret_cast<NevercLoopInfoRef>(State);
}

static void bridgeLoopInfoDestroy(NevercLoopInfoRef LI) {
  if (LLVM_UNLIKELY(!LI))
    return;
  delete reinterpret_cast<LoopInfoState *>(LI);
}

static NevercLoopRef bridgeLoopInfoGetLoopFor(NevercLoopInfoRef LI,
                                              NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!LI || !BB))
    return nullptr;
  auto *State = reinterpret_cast<LoopInfoState *>(LI);
  auto *L = State->LI.getLoopFor(unwrapBB(BB));
  return reinterpret_cast<NevercLoopRef>(L);
}

static unsigned bridgeLoopInfoGetTopLevelLoopCount(NevercLoopInfoRef LI) {
  if (LLVM_UNLIKELY(!LI))
    return 0;
  auto *State = reinterpret_cast<LoopInfoState *>(LI);
  return static_cast<unsigned>(State->LI.end() - State->LI.begin());
}

static NevercBasicBlockRef bridgeLoopGetHeader(NevercLoopRef L) {
  if (LLVM_UNLIKELY(!L))
    return nullptr;
  return wrapBB(reinterpret_cast<Loop *>(L)->getHeader());
}

static unsigned bridgeLoopGetDepth(NevercLoopRef L) {
  if (LLVM_UNLIKELY(!L))
    return 0;
  return reinterpret_cast<Loop *>(L)->getLoopDepth();
}

static NevercLoopRef bridgeLoopGetParentLoop(NevercLoopRef L) {
  if (LLVM_UNLIKELY(!L))
    return nullptr;
  return reinterpret_cast<NevercLoopRef>(
      reinterpret_cast<Loop *>(L)->getParentLoop());
}

static int bridgeLoopContains(NevercLoopRef L, NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!L || !BB))
    return 0;
  return reinterpret_cast<Loop *>(L)->contains(unwrapBB(BB));
}

static unsigned bridgeLoopGetNumBlocks(NevercLoopRef L) {
  if (LLVM_UNLIKELY(!L))
    return 0;
  return reinterpret_cast<Loop *>(L)->getNumBlocks();
}

static unsigned bridgeLoopGetNumSubLoops(NevercLoopRef L) {
  if (LLVM_UNLIKELY(!L))
    return 0;
  return reinterpret_cast<Loop *>(L)->getSubLoops().size();
}

static int bridgeLoopIsInnermost(NevercLoopRef L) {
  if (LLVM_UNLIKELY(!L))
    return 0;
  return reinterpret_cast<Loop *>(L)->isInnermost();
}

// ===----------------------------------------------------------------------===
//  PostDominatorTree -- reverse-CFG dominance analysis
// ===----------------------------------------------------------------------===

static NevercPostDomTreeRef bridgeFunctionBuildPostDomTree(NevercValueRef F) {
  if (LLVM_UNLIKELY(!F))
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn || Fn->isDeclaration()))
    return nullptr;
  auto *PDT = new (std::nothrow) PostDominatorTree();
  if (LLVM_UNLIKELY(!PDT))
    return nullptr;
  PDT->recalculate(*Fn);
  return reinterpret_cast<NevercPostDomTreeRef>(PDT);
}

static void bridgePostDomTreeDestroy(NevercPostDomTreeRef PDT) {
  if (LLVM_UNLIKELY(!PDT))
    return;
  delete reinterpret_cast<PostDominatorTree *>(PDT);
}

static int bridgePostDomTreeDominates(NevercPostDomTreeRef PDT,
                                      NevercBasicBlockRef A,
                                      NevercBasicBlockRef B) {
  if (LLVM_UNLIKELY(!PDT || !A || !B))
    return 0;
  return reinterpret_cast<PostDominatorTree *>(PDT)->dominates(unwrapBB(A),
                                                               unwrapBB(B));
}

static int bridgePostDomTreeProperlyDominates(NevercPostDomTreeRef PDT,
                                              NevercBasicBlockRef A,
                                              NevercBasicBlockRef B) {
  if (LLVM_UNLIKELY(!PDT || !A || !B))
    return 0;
  return reinterpret_cast<PostDominatorTree *>(PDT)->properlyDominates(
      unwrapBB(A), unwrapBB(B));
}

static NevercBasicBlockRef bridgePostDomTreeGetIPDom(NevercPostDomTreeRef PDT,
                                                     NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!PDT || !BB))
    return nullptr;
  auto *Tree = reinterpret_cast<PostDominatorTree *>(PDT);
  auto *Node = Tree->getNode(unwrapBB(BB));
  if (LLVM_UNLIKELY(!Node))
    return nullptr;
  auto *IPDomNode = Node->getIDom();
  return IPDomNode ? wrapBB(IPDomNode->getBlock()) : nullptr;
}

static unsigned bridgePostDomTreeGetDepth(NevercPostDomTreeRef PDT,
                                          NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!PDT || !BB))
    return 0;
  auto *Node =
      reinterpret_cast<PostDominatorTree *>(PDT)->getNode(unwrapBB(BB));
  return LLVM_LIKELY(Node) ? Node->getLevel() : 0;
}

// ===----------------------------------------------------------------------===
//  Function cloning
// ===----------------------------------------------------------------------===

static NevercValueRef bridgeFunctionClone(NevercValueRef F,
                                          const char *NewName) {
  if (LLVM_UNLIKELY(!F))
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn || Fn->isDeclaration()))
    return nullptr;
  ValueToValueMapTy VMap;
  Function *Clone = CloneFunction(Fn, VMap);
  if (LLVM_UNLIKELY(!Clone))
    return nullptr;
  if (NewName && *NewName)
    Clone->setName(NewName);
  Clone->setLinkage(GlobalValue::InternalLinkage);
  return wrapV(Clone);
}

// ===----------------------------------------------------------------------===
//  SCEV -- on-demand ScalarEvolution analysis
//  Bundles all dependencies (TLI, AC, DT, LI, SE) into one state object.
//  SE is heap-allocated after DT/LI are populated to avoid referencing
//  uninitialized analysis results.
// ===----------------------------------------------------------------------===

namespace {
struct SCEVState {
  TargetLibraryInfoImpl TLIImpl;
  TargetLibraryInfo TLI;
  AssumptionCache AC;
  DominatorTree DT;
  LoopInfo LI;
  std::unique_ptr<ScalarEvolution> SE;

  explicit SCEVState(Function &F)
      : TLIImpl(Triple(F.getParent()->getTargetTriple())), TLI(TLIImpl),
        AC(F) {
    DT.recalculate(F);
    LI.analyze(DT);
    SE = std::make_unique<ScalarEvolution>(F, TLI, AC, DT, LI);
  }
};
} // namespace

static NevercSCEVInfoRef bridgeFunctionBuildSCEV(NevercValueRef F) {
  if (LLVM_UNLIKELY(!F))
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn || Fn->isDeclaration()))
    return nullptr;
  auto *State = new (std::nothrow) SCEVState(*Fn);
  if (LLVM_UNLIKELY(!State))
    return nullptr;
  return reinterpret_cast<NevercSCEVInfoRef>(State);
}

static void bridgeSCEVInfoDestroy(NevercSCEVInfoRef SI) {
  if (LLVM_UNLIKELY(!SI))
    return;
  delete reinterpret_cast<SCEVState *>(SI);
}

static unsigned bridgeSCEVGetTripCount(NevercSCEVInfoRef SI,
                                       NevercBasicBlockRef LoopHeader) {
  if (LLVM_UNLIKELY(!SI || !LoopHeader))
    return 0;
  auto *State = reinterpret_cast<SCEVState *>(SI);
  auto *L = State->LI.getLoopFor(unwrapBB(LoopHeader));
  if (LLVM_UNLIKELY(!L || L->getHeader() != unwrapBB(LoopHeader)))
    return 0;
  return State->SE->getSmallConstantTripCount(L);
}

static unsigned bridgeSCEVGetMaxTripCount(NevercSCEVInfoRef SI,
                                          NevercBasicBlockRef LoopHeader) {
  if (LLVM_UNLIKELY(!SI || !LoopHeader))
    return 0;
  auto *State = reinterpret_cast<SCEVState *>(SI);
  auto *L = State->LI.getLoopFor(unwrapBB(LoopHeader));
  if (LLVM_UNLIKELY(!L || L->getHeader() != unwrapBB(LoopHeader)))
    return 0;
  return State->SE->getSmallConstantMaxTripCount(L);
}

// ===----------------------------------------------------------------------===
//  CallGraph -- on-demand module-wide call graph with SCC recursion cache
// ===----------------------------------------------------------------------===

namespace {
struct CallGraphState {
  CallGraph CG;
  SmallPtrSet<Function *, 32> RecursiveFns;

  explicit CallGraphState(Module &M) : CG(M) {
    for (scc_iterator<CallGraph *> I = scc_begin(&CG), E = scc_end(&CG);
         I != E; ++I) {
      if (!I.hasCycle())
        continue;
      for (CallGraphNode *Node : *I) {
        if (Function *Fn = Node->getFunction())
          RecursiveFns.insert(Fn);
      }
    }
  }
};
} // namespace

static NevercCallGraphRef bridgeModuleBuildCallGraph(NevercModuleRef M) {
  if (LLVM_UNLIKELY(!M))
    return nullptr;
  auto *State = new (std::nothrow) CallGraphState(*unwrap(M));
  if (LLVM_UNLIKELY(!State))
    return nullptr;
  return reinterpret_cast<NevercCallGraphRef>(State);
}

static void bridgeCallGraphDestroy(NevercCallGraphRef CG) {
  if (LLVM_UNLIKELY(!CG))
    return;
  delete reinterpret_cast<CallGraphState *>(CG);
}

static unsigned bridgeCallGraphGetCalleeCount(NevercCallGraphRef CG,
                                              NevercValueRef F) {
  if (LLVM_UNLIKELY(!CG || !F))
    return 0;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn))
    return 0;
  auto *State = reinterpret_cast<CallGraphState *>(CG);
  CallGraphNode *Node = State->CG[Fn];
  if (LLVM_UNLIKELY(!Node))
    return 0;
  SmallPtrSet<Function *, 16> Seen;
  for (const auto &CR : *Node) {
    if (Function *Callee = CR.second->getFunction())
      Seen.insert(Callee);
  }
  return static_cast<unsigned>(Seen.size());
}

static NevercValueRef *bridgeCallGraphCollectCallees(NevercCallGraphRef CG,
                                                     NevercValueRef F,
                                                     unsigned *OutCount) {
  if (LLVM_UNLIKELY(!OutCount))
    return nullptr;
  *OutCount = 0;
  if (LLVM_UNLIKELY(!CG || !F))
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn))
    return nullptr;
  auto *State = reinterpret_cast<CallGraphState *>(CG);
  CallGraphNode *Node = State->CG[Fn];
  if (LLVM_UNLIKELY(!Node))
    return nullptr;
  SmallPtrSet<Function *, 16> Seen;
  for (const auto &CR : *Node) {
    if (Function *Callee = CR.second->getFunction())
      Seen.insert(Callee);
  }
  if (Seen.empty())
    return nullptr;
  auto *Out = static_cast<NevercValueRef *>(
      bridgeAlloc(static_cast<uint64_t>(Seen.size()) * sizeof(NevercValueRef)));
  if (LLVM_UNLIKELY(!Out))
    return nullptr;
  unsigned Idx = 0;
  for (Function *Callee : Seen)
    Out[Idx++] = wrapV(Callee);
  *OutCount = Idx;
  return Out;
}

static int bridgeCallGraphIsRecursive(NevercCallGraphRef CG,
                                      NevercValueRef F) {
  if (LLVM_UNLIKELY(!CG || !F))
    return 0;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn))
    return 0;
  return reinterpret_cast<CallGraphState *>(CG)->RecursiveFns.count(Fn) != 0;
}

// ===----------------------------------------------------------------------===
//  CFG mutation helpers
// ===----------------------------------------------------------------------===

static NevercBasicBlockRef bridgeSplitEdge(NevercBasicBlockRef From,
                                           NevercBasicBlockRef To) {
  if (LLVM_UNLIKELY(!From || !To))
    return nullptr;
  BasicBlock *New =
      llvm::SplitEdge(unwrapBB(From), unwrapBB(To));
  return New ? wrapBB(New) : nullptr;
}

static int bridgeMergeBlockIntoPredecessor(NevercBasicBlockRef BB) {
  if (LLVM_UNLIKELY(!BB))
    return 0;
  return llvm::MergeBlockIntoPredecessor(unwrapBB(BB)) ? 1 : 0;
}

// ===----------------------------------------------------------------------===
//  Loop invariant query
// ===----------------------------------------------------------------------===

static int bridgeLoopIsLoopInvariant(NevercLoopRef L, NevercValueRef V) {
  if (LLVM_UNLIKELY(!L || !V))
    return 0;
  return reinterpret_cast<Loop *>(L)->isLoopInvariant(unwrapV(V)) ? 1 : 0;
}

// ===----------------------------------------------------------------------===
//  Arena-backed callee collection
//  Mirrors CallGraphCollectCallees but allocates from the BumpPtrAllocator.
// ===----------------------------------------------------------------------===

static NevercValueRef *
bridgeArenaCollectCallees(NevercArenaRef Arena, NevercCallGraphRef CG,
                          NevercValueRef F, unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!Arena || !CG || !F || !OutCount))
    return nullptr;
  auto *Fn = dyn_cast<Function>(unwrapV(F));
  if (LLVM_UNLIKELY(!Fn))
    return nullptr;
  auto *State = reinterpret_cast<CallGraphState *>(CG);
  CallGraphNode *Node = State->CG[Fn];
  if (LLVM_UNLIKELY(!Node))
    return nullptr;
  SmallPtrSet<Function *, 16> Seen;
  for (const auto &CR : *Node) {
    if (Function *Callee = CR.second->getFunction())
      Seen.insert(Callee);
  }
  if (Seen.empty())
    return nullptr;
  size_t SeenCount = Seen.size();
  if (LLVM_UNLIKELY(SeenCount > SIZE_MAX / sizeof(NevercValueRef)))
    return nullptr;
  auto *Buf = static_cast<NevercValueRef *>(unwrapArena(Arena)->Alloc.Allocate(
      SeenCount * sizeof(NevercValueRef), alignof(NevercValueRef)));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  unsigned Idx = 0;
  for (Function *Callee : Seen)
    Buf[Idx++] = wrapV(Callee);
  *OutCount = Idx;
  return Buf;
}

void populateAnalysisBridge(NevercHostAPI &API) {
  API.FunctionBuildDomTree = bridgeFunctionBuildDomTree;
  API.DomTreeDestroy = bridgeDomTreeDestroy;
  API.DomTreeDominates = bridgeDomTreeDominates;
  API.DomTreeProperlyDominates = bridgeDomTreeProperlyDominates;
  API.DomTreeGetIDom = bridgeDomTreeGetIDom;
  API.DomTreeIsReachable = bridgeDomTreeIsReachable;
  API.DomTreeGetDepth = bridgeDomTreeGetDepth;

  API.FunctionBuildLoopInfo = bridgeFunctionBuildLoopInfo;
  API.LoopInfoDestroy = bridgeLoopInfoDestroy;
  API.LoopInfoGetLoopFor = bridgeLoopInfoGetLoopFor;
  API.LoopInfoGetTopLevelLoopCount = bridgeLoopInfoGetTopLevelLoopCount;
  API.LoopGetHeader = bridgeLoopGetHeader;
  API.LoopGetDepth = bridgeLoopGetDepth;
  API.LoopGetParentLoop = bridgeLoopGetParentLoop;
  API.LoopContains = bridgeLoopContains;
  API.LoopGetNumBlocks = bridgeLoopGetNumBlocks;
  API.LoopGetNumSubLoops = bridgeLoopGetNumSubLoops;
  API.LoopIsInnermost = bridgeLoopIsInnermost;

  API.FunctionBuildPostDomTree = bridgeFunctionBuildPostDomTree;
  API.PostDomTreeDestroy = bridgePostDomTreeDestroy;
  API.PostDomTreeDominates = bridgePostDomTreeDominates;
  API.PostDomTreeProperlyDominates = bridgePostDomTreeProperlyDominates;
  API.PostDomTreeGetIPDom = bridgePostDomTreeGetIPDom;
  API.PostDomTreeGetDepth = bridgePostDomTreeGetDepth;

  API.FunctionClone = bridgeFunctionClone;

  API.FunctionBuildSCEV = bridgeFunctionBuildSCEV;
  API.SCEVInfoDestroy = bridgeSCEVInfoDestroy;
  API.SCEVGetTripCount = bridgeSCEVGetTripCount;
  API.SCEVGetMaxTripCount = bridgeSCEVGetMaxTripCount;

  API.ModuleBuildCallGraph = bridgeModuleBuildCallGraph;
  API.CallGraphDestroy = bridgeCallGraphDestroy;
  API.CallGraphGetCalleeCount = bridgeCallGraphGetCalleeCount;
  API.CallGraphCollectCallees = bridgeCallGraphCollectCallees;
  API.CallGraphIsRecursive = bridgeCallGraphIsRecursive;

  API.SplitEdge = bridgeSplitEdge;
  API.MergeBlockIntoPredecessor = bridgeMergeBlockIntoPredecessor;

  API.LoopIsLoopInvariant = bridgeLoopIsLoopInvariant;

  API.ArenaCollectCallees = bridgeArenaCollectCallees;
}

} // namespace plugin
} // namespace neverc
