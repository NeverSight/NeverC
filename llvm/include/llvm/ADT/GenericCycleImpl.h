//===- GenericCycleImpl.h -------------------------------------*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This template implementation resides in a separate file so that it
/// does not get injected into every .cpp file that includes the
/// generic header.
///
/// DO NOT INCLUDE THIS FILE WHEN MERELY USING CYCLEINFO.
///
/// This file should only be included by files that implement a
/// specialization of the relevant templates. Currently these are:
/// - llvm/lib/CodeGen/MachineCycleAnalysis.cpp
/// - llvm/lib/IR/ConvergenceVerifier.cpp
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_GENERICCYCLEIMPL_H
#define LLVM_ADT_GENERICCYCLEIMPL_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/GenericCycleInfo.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/STLExtras.h"

#include <iterator>
#include <utility>

#define DEBUG_TYPE "generic-cycle-impl"

namespace llvm {

template <typename ContextT>
bool GenericCycle<ContextT>::contains(const GenericCycle *C) const {
  if (!C)
    return false;

  if (Depth > C->Depth)
    return false;
  while (Depth < C->Depth)
    C = C->ParentCycle;
  return this == C;
}

template <typename ContextT>
void GenericCycle<ContextT>::getExitBlocks(
    SmallVectorImpl<BlockT *> &TmpStorage) const {
  TmpStorage.clear();

  size_t NumExitBlocks = 0;
  for (BlockT *Block : blocks()) {
    llvm::append_range(TmpStorage, successors(Block));

    for (size_t Idx = NumExitBlocks, End = TmpStorage.size(); Idx < End;
         ++Idx) {
      BlockT *Succ = TmpStorage[Idx];
      if (!contains(Succ)) {
        auto ExitEndIt = TmpStorage.begin() + NumExitBlocks;
        if (std::find(TmpStorage.begin(), ExitEndIt, Succ) == ExitEndIt)
          TmpStorage[NumExitBlocks++] = Succ;
      }
    }

    TmpStorage.resize(NumExitBlocks);
  }
}

template <typename ContextT>
auto GenericCycle<ContextT>::getCyclePreheader() const -> BlockT * {
  BlockT *Predecessor = getCyclePredecessor();
  if (!Predecessor)
    return nullptr;

  assert(isReducible() && "Cycle Predecessor must be in a reducible cycle!");

  if (succ_size(Predecessor) != 1)
    return nullptr;

  // Make sure we are allowed to hoist instructions into the predecessor.
  if (!Predecessor->isLegalToHoistInto())
    return nullptr;

  return Predecessor;
}

template <typename ContextT>
auto GenericCycle<ContextT>::getCyclePredecessor() const -> BlockT * {
  if (!isReducible())
    return nullptr;

  BlockT *Out = nullptr;

  // Loop over the predecessors of the header node...
  BlockT *Header = getHeader();
  for (const auto Pred : predecessors(Header)) {
    if (!contains(Pred)) {
      if (Out && Out != Pred)
        return nullptr;
      Out = Pred;
    }
  }

  return Out;
}

/// \brief Helper class for computing cycle information.
///
/// Identifies (possibly irreducible) loops with the single-pass DFS of Wei,
/// Mao, Zou and Chen, "A New Algorithm for Identifying Loops in Decompilation"
/// (SAS 2007), then materializes the pointer-based cycle forest used by this
/// tree API.
template <typename ContextT> class GenericCycleInfoCompute {
  using BlockT = typename ContextT::BlockT;
  using FunctionT = typename ContextT::FunctionT;
  using CycleInfoT = GenericCycleInfo<ContextT>;
  using CycleT = typename CycleInfoT::CycleT;

  CycleInfoT &Info;

  // Sentinel header-preorder rank meaning "no cycle".
  static constexpr unsigned NoCycle = ~0u;
  // Sentinel block number meaning "no block".
  static constexpr unsigned NoBlock = ~0u;

  // Per-block state indexed by a dense function-local block number.
  struct BlockInfo {
    // The block this entry describes (non-null once visited by DFS), packed
    // with a bit for whether it heads a loop. Opaque null means unvisited.
    PointerIntPair<BlockT *, 1, bool> BlockAndHeader;
    union {
      // (Live only during DFS) 1-based position on the current DFS path; 0 if
      // off path.
      unsigned Pos = 0;
      // (Live after DFS) Header-preorder rank of the innermost loop containing
      // this block (the loop it heads if IsHeader); NoCycle if none.
      unsigned LoopIdx;
    };
    // Block number of the innermost loop header; NoBlock if none. Set to
    // NoBlock by open() on first visit, then woven by tagLoopHeader.
    unsigned LoopHeader = 0;

    BlockT *getBlock() const { return BlockAndHeader.getPointer(); }
    bool isHeader() const { return BlockAndHeader.getInt(); }
    void setHeader() { BlockAndHeader.setInt(true); }
    bool visited() const { return BlockAndHeader.getOpaqueValue() != nullptr; }
  };

  // Per-cycle scratch built in run() and consumed by materialize(), keyed by
  // header-preorder rank.
  struct CycleBuild {
    BlockT *Header;
    unsigned ChildHead;
    unsigned NextSibling;
  };

  DenseMap<const BlockT *, unsigned> BlockNums;
  SmallVector<BlockInfo, 8> BlockInfos;
  // Reachable block numbers in DFS preorder.
  SmallVector<unsigned, 8> Preorder;
  // Number of loop headers found by dfs().
  unsigned NumHeaders = 0;
  // Records (header H, block B): an edge from outside re-enters the closed
  // cycle headed by H at B, making B a non-header entry of it.
  SmallVector<std::pair<unsigned, unsigned>, 8> Reentries;

  GenericCycleInfoCompute(const GenericCycleInfoCompute &) = delete;
  GenericCycleInfoCompute &operator=(const GenericCycleInfoCompute &) = delete;

  unsigned num(const BlockT *B) const {
    auto It = BlockNums.find(B);
    assert(It != BlockNums.end() && "block missing from function numbering");
    return It->second;
  }

  BlockInfo &info(unsigned Number) { return BlockInfos[Number]; }

  // Weave loop header \p H (and its own header chain) into the loop header
  // chain of \p B, keeping the chain ordered from innermost to outermost by
  // DFS-path position. Building this chain on the fly is why the algorithm
  // needs no union-find (used in the Havlak algorithm) at all.
  void tagLoopHeader(unsigned B, unsigned H) {
    assert(H != NoBlock);
    // Invariant: info(B).Pos >= info(H).Pos.
    while (B != H) {
      unsigned IH = info(B).LoopHeader;
      if (IH == NoBlock) {
        // B's chain ended: append the rest of H's chain.
        info(B).LoopHeader = H;
        return;
      }
      // Keep whichever candidate header is inner (larger DFS-path position).
      if (info(IH).Pos >= info(H).Pos)
        B = IH;
      else {
        info(B).LoopHeader = H;
        B = H;
        H = IH;
      }
    }
  }

  void dfs(BlockT *EntryBlock);
  void materialize(ArrayRef<CycleBuild> Build, unsigned TopHead);

public:
  GenericCycleInfoCompute(CycleInfoT &Info) : Info(Info) {}

  void run(BlockT *EntryBlock);

  static void updateDepth(CycleT *SubTree);
};

template <typename ContextT>
auto GenericCycleInfo<ContextT>::getTopLevelParentCycle(BlockT *Block)
    -> CycleT * {
  auto Cycle = BlockMapTopLevel.find(Block);
  if (Cycle != BlockMapTopLevel.end())
    return Cycle->second;

  auto MapIt = BlockMap.find(Block);
  if (MapIt == BlockMap.end())
    return nullptr;

  auto *C = MapIt->second;
  while (C->ParentCycle)
    C = C->ParentCycle;
  BlockMapTopLevel.try_emplace(Block, C);
  return C;
}

template <typename ContextT>
void GenericCycleInfo<ContextT>::moveTopLevelCycleToNewParent(CycleT *NewParent,
                                                              CycleT *Child) {
  assert((!Child->ParentCycle && !NewParent->ParentCycle) &&
         "NewParent and Child must be both top level cycle!\n");
  auto &CurrentContainer =
      Child->ParentCycle ? Child->ParentCycle->Children : TopLevelCycles;
  auto Pos = llvm::find_if(CurrentContainer, [=](const auto &Ptr) -> bool {
    return Child == Ptr.get();
  });
  assert(Pos != CurrentContainer.end());
  NewParent->Children.push_back(std::move(*Pos));
  *Pos = std::move(CurrentContainer.back());
  CurrentContainer.pop_back();
  Child->ParentCycle = NewParent;

  NewParent->Blocks.insert(Child->block_begin(), Child->block_end());

  for (auto &It : BlockMapTopLevel)
    if (It.second == Child)
      It.second = NewParent;
}

template <typename ContextT>
void GenericCycleInfo<ContextT>::addBlockToCycle(BlockT *Block, CycleT *Cycle) {
  // FixMe: Appending NewBlock is fine as a set of blocks in a cycle. When
  // printing, cycle NewBlock is at the end of list but it should be in the
  // middle to represent actual traversal of a cycle.
  Cycle->appendBlock(Block);
  BlockMap.try_emplace(Block, Cycle);

  CycleT *ParentCycle = Cycle->getParentCycle();
  while (ParentCycle) {
    Cycle = ParentCycle;
    Cycle->appendBlock(Block);
    ParentCycle = Cycle->getParentCycle();
  }

  BlockMapTopLevel.try_emplace(Block, Cycle);
}

/// Materialize the pointer-owned cycle forest from header-preorder ranks.
template <typename ContextT>
void GenericCycleInfoCompute<ContextT>::materialize(ArrayRef<CycleBuild> Build,
                                                    unsigned TopHead) {
  const unsigned N = Build.size();
  SmallVector<std::unique_ptr<CycleT>, 8> Owned(N);
  SmallVector<CycleT *, 8> Cycles(N);
  for (unsigned I = 0; I != N; ++I) {
    Owned[I] = std::make_unique<CycleT>();
    Cycles[I] = Owned[I].get();
    Cycles[I]->appendEntry(Build[I].Header);
    Cycles[I]->appendBlock(Build[I].Header);
    Info.BlockMap.try_emplace(Build[I].Header, Cycles[I]);
  }

  // Transfer ownership along the ChildHead / NextSibling lists (decreasing
  // header preorder), then place top-level cycles.
  for (unsigned I = 0; I != N; ++I) {
    for (unsigned C = Build[I].ChildHead; C != NoCycle;
         C = Build[C].NextSibling) {
      Cycles[C]->ParentCycle = Cycles[I];
      Cycles[I]->Children.push_back(std::move(Owned[C]));
    }
  }
  for (unsigned T = TopHead; T != NoCycle; T = Build[T].NextSibling)
    Info.TopLevelCycles.push_back(std::move(Owned[T]));

  // Place every block into its innermost cycle and all ancestors; record the
  // top-level parent for BlockMapTopLevel.
  for (unsigned BN : Preorder) {
    BlockInfo &BI = info(BN);
    if (BI.LoopIdx == NoCycle)
      continue;
    CycleT *Innermost = Cycles[BI.LoopIdx];
    BlockT *Block = BI.getBlock();
    Info.BlockMap.try_emplace(Block, Innermost);
    for (CycleT *C = Innermost; C; C = C->ParentCycle)
      C->appendBlock(Block);
    CycleT *Top = Innermost;
    while (Top->ParentCycle)
      Top = Top->ParentCycle;
    Info.BlockMapTopLevel.try_emplace(Block, Top);
  }

  // Non-header entries recorded during DFS.
  if (!Reentries.empty()) {
    SmallVector<unsigned, 8> Rank(BlockInfos.size());
    for (auto [R, BN] : enumerate(Preorder))
      Rank[BN] = R;
    for (auto &E : Reentries)
      E.second = Rank[E.second];
    llvm::sort(Reentries);
    for (unsigned I = 0, E = Reentries.size(); I != E; ++I) {
      if (I && Reentries[I] == Reentries[I - 1])
        continue;
      unsigned H = Reentries[I].first;
      unsigned R = Reentries[I].second;
      CycleT *C = Cycles[info(H).LoopIdx];
      BlockT *Entry = info(Preorder[R]).getBlock();
      if (!C->isEntry(Entry))
        C->appendEntry(Entry);
    }
  }

  for (auto *TLC : Info.toplevel_cycles()) {
    LLVM_DEBUG(dbgs() << "top-level cycle: "
                      << Info.Context.print(TLC->getHeader()) << "\n");
    TLC->ParentCycle = nullptr;
    updateDepth(TLC);
  }
}

/// \brief Main function of the cycle info computations.
template <typename ContextT>
void GenericCycleInfoCompute<ContextT>::run(BlockT *EntryBlock) {
  LLVM_DEBUG(dbgs() << "Entry block: " << Info.Context.print(EntryBlock)
                    << "\n");

  FunctionT *F = EntryBlock->getParent();
  BlockNums.reserve(F->size());
  BlockInfos.assign(F->size(), BlockInfo{});
  unsigned Idx = 0;
  for (BlockT &BB : *F)
    BlockNums[&BB] = Idx++;

  dfs(EntryBlock);
  if (!NumHeaders)
    return;

  // Number the cycles by their header's preorder rank and resolve every
  // block's innermost cycle in one pass: a block's LoopHeader is a DFS
  // ancestor and so already numbered, and parents get smaller ranks than
  // their children.
  SmallVector<CycleBuild, 8> Build;
  // Exact reserve so the Head reference below survives each push_back.
  Build.reserve(NumHeaders);
  unsigned TopHead = NoCycle;
  for (unsigned BN : Preorder) {
    BlockInfo &BI = info(BN);
    if (BI.isHeader()) {
      unsigned I = Build.size();
      BI.LoopIdx = I;
      unsigned &Head = BI.LoopHeader != NoBlock
                           ? Build[info(BI.LoopHeader).LoopIdx].ChildHead
                           : TopHead;
      Build.push_back({BI.getBlock(), NoCycle, Head});
      Head = I;
      LLVM_DEBUG(dbgs() << "Found cycle for header: "
                        << Info.Context.print(BI.getBlock()) << "\n");
    } else if (BI.LoopHeader != NoBlock) {
      BI.LoopIdx = info(BI.LoopHeader).LoopIdx;
    } else {
      BI.LoopIdx = NoCycle;
    }
  }
  materialize(Build, TopHead);
}

/// \brief Recompute depth values of \p SubTree and all descendants.
template <typename ContextT>
void GenericCycleInfoCompute<ContextT>::updateDepth(CycleT *SubTree) {
  for (CycleT *Cycle : depth_first(SubTree))
    Cycle->Depth = Cycle->ParentCycle ? Cycle->ParentCycle->Depth + 1 : 1;
}

/// Identify (possibly irreducible) loops using a single-pass DFS algorithm of
/// "A New Algorithm for Identifying Loops in Decompilation" (SAS 2007). The
/// cycle forest is then reconstructed from the per-block header tags.
template <typename ContextT>
void GenericCycleInfoCompute<ContextT>::dfs(BlockT *EntryBlock) {
  // Successors are visited in reverse order to match the legacy
  // single-LIFO-stack traversal, keeping cycle identification and block order
  // unchanged.
  using SuccIt = decltype(successors(EntryBlock).begin());
  struct Frame {
    unsigned Block;
    std::reverse_iterator<SuccIt> Cur, End;
  };
  SmallVector<Frame, 8> Stack;
  unsigned Counter = 0;
  Preorder.reserve(BlockInfos.size());

  auto open = [&](BlockT *Block) {
    unsigned N = num(Block);
    Preorder.push_back(N);
    BlockInfo &BI = info(N);
    BI.BlockAndHeader.setPointerAndInt(Block, false);
    BI.Pos = ++Counter;
    BI.LoopHeader = NoBlock;
    auto Succs = successors(Block);
    Stack.push_back({N, std::make_reverse_iterator(Succs.end()),
                     std::make_reverse_iterator(Succs.begin())});
  };

  open(EntryBlock);
  while (!Stack.empty()) {
    Frame &Top = Stack.back();
    if (Top.Cur != Top.End) {
      unsigned B0 = Top.Block;
      BlockT *B1P = *Top.Cur++;
      unsigned B1 = num(B1P);
      BlockInfo &B1Info = info(B1);
      if (!B1Info.visited()) {
        // Tree edge; the weaving happens when B1's frame is popped.
        open(B1P);
      } else if (B1Info.Pos > 0) {
        // B1 is a loop header (including self-edge).
        if (!B1Info.isHeader()) {
          B1Info.setHeader();
          ++NumHeaders;
        }
        tagLoopHeader(B0, B1);
      } else {
        // Climb B1's header chain: each enclosing header still off the DFS path
        // heads a closed cycle this edge re-enters, so B1 is a non-header entry
        // of it (and it is irreducible). Stop at the first on-path header and
        // attribute B0 to it.
        for (unsigned H = B1Info.LoopHeader; H != NoBlock;
             H = info(H).LoopHeader) {
          if (info(H).Pos > 0) {
            tagLoopHeader(B0, H);
            break;
          }
          Reentries.push_back({H, B1});
        }
      }
    } else {
      // Leave the DFS path.
      unsigned B0 = Top.Block;
      info(B0).Pos = 0;
      Stack.pop_back();
      // And weave into the parent's chain (continue the "Tree edge" case).
      if (!Stack.empty() && info(B0).LoopHeader != NoBlock)
        tagLoopHeader(Stack.back().Block, info(B0).LoopHeader);
    }
  }
}

/// \brief Reset the object to its initial state.
template <typename ContextT> void GenericCycleInfo<ContextT>::clear() {
  TopLevelCycles.clear();
  BlockMap.clear();
  BlockMapTopLevel.clear();
}

/// \brief Compute the cycle info for a function.
template <typename ContextT>
void GenericCycleInfo<ContextT>::compute(FunctionT &F) {
  GenericCycleInfoCompute<ContextT> Compute(*this);
  Context = ContextT(&F);

  LLVM_DEBUG(dbgs() << "Computing cycles for function: " << F.getName()
                    << "\n");
  Compute.run(&F.front());

  assert(validateTree());
}

template <typename ContextT>
void GenericCycleInfo<ContextT>::splitCriticalEdge(BlockT *Pred, BlockT *Succ,
                                                   BlockT *NewBlock) {
  // Edge Pred-Succ is replaced by edges Pred-NewBlock and NewBlock-Succ, all
  // cycles that had blocks Pred and Succ also get NewBlock.
  CycleT *Cycle = getSmallestCommonCycle(getCycle(Pred), getCycle(Succ));
  if (!Cycle)
    return;

  addBlockToCycle(NewBlock, Cycle);
  assert(validateTree());
}

/// \brief Find the innermost cycle containing a given block.
///
/// \returns the innermost cycle containing \p Block or nullptr if
///          it is not contained in any cycle.
template <typename ContextT>
auto GenericCycleInfo<ContextT>::getCycle(const BlockT *Block) const
    -> CycleT * {
  return BlockMap.lookup(Block);
}

/// \brief Find the innermost cycle containing both given cycles.
///
/// \returns the innermost cycle containing both \p A and \p B
///          or nullptr if there is no such cycle.
template <typename ContextT>
auto GenericCycleInfo<ContextT>::getSmallestCommonCycle(CycleT *A,
                                                        CycleT *B) const
    -> CycleT * {
  if (!A || !B)
    return nullptr;

  // If cycles A and B have different depth replace them with parent cycle
  // until they have the same depth.
  while (A->getDepth() > B->getDepth())
    A = A->getParentCycle();
  while (B->getDepth() > A->getDepth())
    B = B->getParentCycle();

  // Cycles A and B are at same depth but may be disjoint, replace them with
  // parent cycles until we find cycle that contains both or we run out of
  // parent cycles.
  while (A != B) {
    A = A->getParentCycle();
    B = B->getParentCycle();
  }

  return A;
}

/// \brief get the depth for the cycle which containing a given block.
///
/// \returns the depth for the innermost cycle containing \p Block or 0 if it is
///          not contained in any cycle.
template <typename ContextT>
unsigned GenericCycleInfo<ContextT>::getCycleDepth(const BlockT *Block) const {
  CycleT *Cycle = getCycle(Block);
  if (!Cycle)
    return 0;
  return Cycle->getDepth();
}

#ifndef NDEBUG
/// \brief Validate the internal consistency of the cycle tree.
///
/// Note that this does \em not check that cycles are really cycles in the CFG,
/// or that the right set of cycles in the CFG were found.
template <typename ContextT>
bool GenericCycleInfo<ContextT>::validateTree() const {
  DenseSet<BlockT *> Blocks;
  DenseSet<BlockT *> Entries;

  auto reportError = [](const char *File, int Line, const char *Cond) {
    errs() << File << ':' << Line
           << ": GenericCycleInfo::validateTree: " << Cond << '\n';
  };
#define check(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      reportError(__FILE__, __LINE__, #cond);                                  \
      return false;                                                            \
    }                                                                          \
  } while (false)

  for (const auto *TLC : toplevel_cycles()) {
    for (const CycleT *Cycle : depth_first(TLC)) {
      if (Cycle->ParentCycle)
        check(is_contained(Cycle->ParentCycle->children(), Cycle));

      for (BlockT *Block : Cycle->Blocks) {
        auto MapIt = BlockMap.find(Block);
        check(MapIt != BlockMap.end());
        check(Cycle->contains(MapIt->second));
        check(Blocks.insert(Block).second); // duplicates in block list?
      }
      Blocks.clear();

      check(!Cycle->Entries.empty());
      for (BlockT *Entry : Cycle->Entries) {
        check(Entries.insert(Entry).second); // duplicate entry?
        check(is_contained(Cycle->Blocks, Entry));
      }
      Entries.clear();

      unsigned ChildDepth = 0;
      for (const CycleT *Child : Cycle->children()) {
        check(Child->Depth > Cycle->Depth);
        if (!ChildDepth) {
          ChildDepth = Child->Depth;
        } else {
          check(ChildDepth == Child->Depth);
        }
      }
    }
  }

  for (const auto &Entry : BlockMap) {
    BlockT *Block = Entry.first;
    for (const CycleT *Cycle = Entry.second; Cycle;
         Cycle = Cycle->ParentCycle) {
      check(is_contained(Cycle->Blocks, Block));
    }
  }

#undef check

  return true;
}
#endif

/// \brief Print the cycle info.
template <typename ContextT>
void GenericCycleInfo<ContextT>::print(raw_ostream &Out) const {
  for (const auto *TLC : toplevel_cycles()) {
    for (const CycleT *Cycle : depth_first(TLC)) {
      for (unsigned I = 0; I < Cycle->Depth; ++I)
        Out << "    ";

      Out << Cycle->print(Context) << '\n';
    }
  }
}

} // namespace llvm

#undef DEBUG_TYPE

#endif // LLVM_ADT_GENERICCYCLEIMPL_H
