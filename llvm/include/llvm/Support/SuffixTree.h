//===- llvm/ADT/SuffixTree.h - Tree for substrings --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// A data structure for fast substring queries.
//
// Suffix trees represent the suffixes of their input strings in their leaves.
// A suffix tree is a type of compressed trie structure where each node
// represents an entire substring rather than a single character. Each leaf
// of the tree is a suffix.
//
// A suffix tree can be seen as a type of state machine where each state is a
// substring of the full string. The tree is structured so that, for a string
// of length N, there are exactly N leaves in the tree. This structure allows
// us to quickly find repeated substrings of the input string.
//
// In this implementation, a "string" is a vector of unsigned integers.
// These integers may result from hashing some data type. A suffix tree can
// contain 1 or many strings, which can then be queried as one large string.
//
// The suffix tree is implemented using Ukkonen's algorithm for linear-time
// suffix tree construction. Ukkonen's algorithm is explained in more detail
// in the paper by Esko Ukkonen "On-line construction of suffix trees. The
// paper is available at
//
// https://www.cs.helsinki.fi/u/ukkonen/SuffixT1withFigs.pdf
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SUFFIXTREE_H
#define LLVM_SUPPORT_SUFFIXTREE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/SuffixTreeNode.h"

namespace llvm {
class SuffixTree {
public:
  /// Each element is an integer representing an instruction in the module.
  ArrayRef<unsigned> Str;

  /// A repeated substring in the tree.
  struct RepeatedSubstring {
    /// The length of the string.
    unsigned Length;

    /// The start indices of each occurrence.
    SmallVector<unsigned> StartIndices;
  };

private:
  /// Maintains internal nodes in the tree.
  SpecificBumpPtrAllocator<SuffixTreeInternalNode> InternalNodeAllocator;
  /// Maintains leaf nodes in the tree.
  SpecificBumpPtrAllocator<SuffixTreeLeafNode> LeafNodeAllocator;

  /// The root of the suffix tree.
  ///
  /// The root represents the empty string. It is maintained by the
  /// \p NodeAllocator like every other node in the tree.
  SuffixTreeInternalNode *Root = nullptr;

  /// The end index of each leaf in the tree.
  unsigned LeafEndIdx = SuffixTreeNode::EmptyIdx;

  /// Helper struct which keeps track of the next insertion point in
  /// Ukkonen's algorithm.
  struct ActiveState {
    /// The next node to insert at.
    SuffixTreeInternalNode *Node = nullptr;

    /// The index of the first character in the substring currently being added.
    unsigned Idx = SuffixTreeNode::EmptyIdx;

    /// The length of the substring we have to add at the current step.
    unsigned Len = 0;
  };

  /// The point the next insertion will take place at in the
  /// construction algorithm.
  ActiveState Active;

  /// Allocate a leaf node and add it to the tree.
  ///
  /// \param Parent The parent of this node.
  /// \param StartIdx The start index of this node's associated string.
  /// \param Edge The label on the edge leaving \p Parent to this node.
  ///
  /// \returns A pointer to the allocated leaf node.
  SuffixTreeNode *insertLeaf(SuffixTreeInternalNode &Parent, unsigned StartIdx,
                             unsigned Edge);

  /// Allocate an internal node and add it to the tree.
  ///
  /// \param Parent The parent of this node. Only null when allocating the root.
  /// \param StartIdx The start index of this node's associated string.
  /// \param EndIdx The end index of this node's associated string.
  /// \param Edge The label on the edge leaving \p Parent to this node.
  ///
  /// \returns A pointer to the allocated internal node.
  SuffixTreeInternalNode *insertInternalNode(SuffixTreeInternalNode *Parent,
                                             unsigned StartIdx, unsigned EndIdx,
                                             unsigned Edge);

  /// Allocate the root node and add it to the tree.
  ///
  /// \returns A pointer to the root.
  SuffixTreeInternalNode *insertRoot();

  /// Set the suffix indices of the leaves to the start indices of their
  /// respective suffixes.
  void setSuffixIndices();

  /// Construct the suffix tree for the prefix of the input ending at
  /// \p EndIdx.
  ///
  /// Used to construct the full suffix tree iteratively. At the end of each
  /// step, the constructed suffix tree is either a valid suffix tree, or a
  /// suffix tree with implicit suffixes. At the end of the final step, the
  /// suffix tree is a valid tree.
  ///
  /// \param EndIdx The end index of the current prefix in the main string.
  /// \param SuffixesToAdd The number of suffixes that must be added
  /// to complete the suffix tree at the current phase.
  ///
  /// \returns The number of suffixes that have not been added at the end of
  /// this step.
  unsigned extend(unsigned EndIdx, unsigned SuffixesToAdd);

public:
  /// Construct a suffix tree from a sequence of unsigned integers.
  ///
  /// \param Str The string to construct the suffix tree for.
  SuffixTree(const ArrayRef<unsigned> &Str);

  /// Iterator for finding all repeated substrings in the suffix tree.
  struct RepeatedSubstringIterator {
  private:
    /// The current node we're visiting.
    SuffixTreeNode *N = nullptr;

    /// The repeated substring associated with this node.
    RepeatedSubstring RS;

    /// The nodes left to visit.
    SmallVector<SuffixTreeInternalNode *> InternalNodesToVisit;

    /// The minimum length of a repeated substring to find.
    /// Since we're outlining, we want at least two instructions in the range.
    /// FIXME: This may not be true for targets like X86 which support many
    /// instruction lengths.
    const unsigned MinLength = 2;

    /// Move the iterator to the next repeated substring.
    void advance();

  public:
    /// Return the current repeated substring.
    RepeatedSubstring &operator*() { return RS; }

    RepeatedSubstringIterator &operator++() {
      advance();
      return *this;
    }

    RepeatedSubstringIterator operator++(int I) {
      RepeatedSubstringIterator It(*this);
      advance();
      return It;
    }

    bool operator==(const RepeatedSubstringIterator &Other) const {
      return N == Other.N;
    }
    bool operator!=(const RepeatedSubstringIterator &Other) const {
      return !(*this == Other);
    }

    RepeatedSubstringIterator(SuffixTreeInternalNode *N) : N(N) {
      // Do we have a non-null node?
      if (!N)
        return;
      // Yes. At the first step, we need to visit all of N's children.
      // Note: This means that we visit N last.
      InternalNodesToVisit.push_back(N);
      advance();
    }
  };

  typedef RepeatedSubstringIterator iterator;
  iterator begin() { return iterator(Root); }
  iterator end() { return iterator(nullptr); }
};

} // namespace llvm

// === Inline implementations (moved from cpp_bridge.cpp) ===

namespace llvm {

/// \returns the number of elements in the substring associated with \p N.
inline static size_t numElementsInSubstring(const SuffixTreeNode *N) {
  assert(N && "Got a null node?");
  if (auto *Internal = dyn_cast<SuffixTreeInternalNode>(N))
    if (Internal->isRoot())
      return 0;
  return N->getEndIdx() - N->getStartIdx() + 1;
}

inline SuffixTree::SuffixTree(const ArrayRef<unsigned> &Str) : Str(Str) {
  Root = insertRoot();
  Active.Node = Root;

  // Keep track of the number of suffixes we have to add of the current
  // prefix.
  unsigned SuffixesToAdd = 0;

  // Construct the suffix tree iteratively on each prefix of the string.
  // PfxEndIdx is the end index of the current prefix.
  // End is one past the last element in the string.
  for (unsigned PfxEndIdx = 0, End = Str.size(); PfxEndIdx < End; PfxEndIdx++) {
    SuffixesToAdd++;
    LeafEndIdx = PfxEndIdx; // Extend each of the leaves.
    SuffixesToAdd = extend(PfxEndIdx, SuffixesToAdd);
  }

  // Set the suffix indices of each leaf.
  assert(Root && "Root node can't be 0!");
  setSuffixIndices();
}

inline SuffixTreeNode *SuffixTree::insertLeaf(SuffixTreeInternalNode &Parent,
                                              unsigned StartIdx,
                                              unsigned Edge) {
  assert(StartIdx <= LeafEndIdx && "String can't start after it ends!");
  auto *N = new (LeafNodeAllocator.Allocate())
      SuffixTreeLeafNode(StartIdx, &LeafEndIdx);
  Parent.Children[Edge] = N;
  return N;
}

inline SuffixTreeInternalNode *
SuffixTree::insertInternalNode(SuffixTreeInternalNode *Parent,
                               unsigned StartIdx, unsigned EndIdx,
                               unsigned Edge) {
  assert(StartIdx <= EndIdx && "String can't start after it ends!");
  assert(!(!Parent && StartIdx != SuffixTreeNode::EmptyIdx) &&
         "Non-root internal nodes must have parents!");
  auto *N = new (InternalNodeAllocator.Allocate())
      SuffixTreeInternalNode(StartIdx, EndIdx, Root);
  if (Parent)
    Parent->Children[Edge] = N;
  return N;
}

inline SuffixTreeInternalNode *SuffixTree::insertRoot() {
  return insertInternalNode(/*Parent = */ 0, SuffixTreeNode::EmptyIdx,
                            SuffixTreeNode::EmptyIdx, /*Edge = */ 0);
}

inline void SuffixTree::setSuffixIndices() {
  // List of nodes we need to visit along with the current length of the
  // string.
  struct NodeLenEntry_ {
    SuffixTreeNode *first;
    unsigned second;
  };
  SmallVector<NodeLenEntry_> ToVisit;

  // Current node being visited.
  SuffixTreeNode *CurrNode = Root;

  // Sum of the lengths of the nodes down the path to the current one.
  unsigned CurrNodeLen = 0;
  ToVisit.push_back({CurrNode, CurrNodeLen});
  while (!ToVisit.empty()) {
    CurrNode = ToVisit.back().first;
    CurrNodeLen = ToVisit.back().second;
    ToVisit.pop_back();
    // Length of the current node from the root down to here.
    CurrNode->setConcatLen(CurrNodeLen);
    if (auto *InternalNode = dyn_cast<SuffixTreeInternalNode>(CurrNode))
      for (auto &ChildPair : InternalNode->Children) {
        assert(ChildPair.second && "Node had a null child!");
        ToVisit.push_back({ChildPair.second,
                           (unsigned)(CurrNodeLen + numElementsInSubstring(
                                                        ChildPair.second))});
      }
    // No children, so we are at the end of the string.
    if (auto *LeafNode = dyn_cast<SuffixTreeLeafNode>(CurrNode))
      LeafNode->setSuffixIdx(Str.size() - CurrNodeLen);
  }
}

inline unsigned SuffixTree::extend(unsigned EndIdx, unsigned SuffixesToAdd) {
  SuffixTreeInternalNode *NeedsLink = 0;

  while (SuffixesToAdd > 0) {

    // Are we waiting to add anything other than just the last character?
    if (Active.Len == 0) {
      // If not, then say the active index is the end index.
      Active.Idx = EndIdx;
    }

    assert(Active.Idx <= EndIdx && "Start index can't be after end index!");

    // The first character in the current substring we're looking at.
    unsigned FirstChar = Str[Active.Idx];

    // Have we inserted anything starting with FirstChar at the current node?
    if (Active.Node->Children.count(FirstChar) == 0) {
      // If not, then we can just insert a leaf and move to the next step.
      insertLeaf(*Active.Node, EndIdx, FirstChar);

      // The active node is an internal node, and we visited it, so it must
      // need a link if it doesn't have one.
      if (NeedsLink) {
        NeedsLink->setLink(Active.Node);
        NeedsLink = 0;
      }
    } else {
      // There's a match with FirstChar, so look for the point in the tree to
      // insert a new node.
      SuffixTreeNode *NextNode = Active.Node->Children[FirstChar];

      unsigned SubstringLen = numElementsInSubstring(NextNode);

      // Is the current suffix we're trying to insert longer than the size of
      // the child we want to move to?
      if (Active.Len >= SubstringLen) {
        // If yes, then consume the characters we've seen and move to the next
        // node.
        assert(isa<SuffixTreeInternalNode>(NextNode) &&
               "Expected an internal node?");
        Active.Idx += SubstringLen;
        Active.Len -= SubstringLen;
        Active.Node = cast<SuffixTreeInternalNode>(NextNode);
        continue;
      }

      // Otherwise, the suffix we're trying to insert must be contained in the
      // next node we want to move to.
      unsigned LastChar = Str[EndIdx];

      // Is the string we're trying to insert a substring of the next node?
      if (Str[NextNode->getStartIdx() + Active.Len] == LastChar) {
        // If yes, then we're done for this step. Remember our insertion point
        // and move to the next end index. At this point, we have an implicit
        // suffix tree.
        if (NeedsLink && !Active.Node->isRoot()) {
          NeedsLink->setLink(Active.Node);
          NeedsLink = 0;
        }

        Active.Len++;
        break;
      }

      // The string we're trying to insert isn't a substring of the next node,
      // but matches up to a point. Split the node.
      //
      // For example, say we ended our search at a node n and we're trying to
      // insert ABD. Then we'll create a new node s for AB, reduce n to just
      // representing C, and insert a new leaf node l to represent d. This
      // allows us to ensure that if n was a leaf, it remains a leaf.
      //
      //   | ABC  ---split--->  | AB
      //   n                    s
      //                     C / \ D
      //                      n   l

      // The node s from the diagram
      SuffixTreeInternalNode *SplitNode = insertInternalNode(
          Active.Node, NextNode->getStartIdx(),
          NextNode->getStartIdx() + Active.Len - 1, FirstChar);

      // Insert the new node representing the new substring into the tree as
      // a child of the split node. This is the node l from the diagram.
      insertLeaf(*SplitNode, EndIdx, LastChar);

      // Make the old node a child of the split node and update its start
      // index. This is the node n from the diagram.
      NextNode->incrementStartIdx(Active.Len);
      SplitNode->Children[Str[NextNode->getStartIdx()]] = NextNode;

      // SplitNode is an internal node, update the suffix link.
      if (NeedsLink)
        NeedsLink->setLink(SplitNode);

      NeedsLink = SplitNode;
    }

    // We've added something new to the tree, so there's one less suffix to
    // add.
    SuffixesToAdd--;

    if (Active.Node->isRoot()) {
      if (Active.Len > 0) {
        Active.Len--;
        Active.Idx = EndIdx - SuffixesToAdd + 1;
      }
    } else {
      // Start the next phase at the next smallest suffix.
      Active.Node = Active.Node->getLink();
    }
  }

  return SuffixesToAdd;
}

inline void SuffixTree::RepeatedSubstringIterator::advance() {
  // Clear the current state. If we're at the end of the range, then this
  // is the state we want to be in.
  RS = RepeatedSubstring();
  N = 0;

  // Each leaf node represents a repeat of a string.
  SmallVector<unsigned> RepeatedSubstringStarts;

  // Continue visiting nodes until we find one which repeats more than once.
  while (!InternalNodesToVisit.empty()) {
    RepeatedSubstringStarts.clear();
    auto *Curr = InternalNodesToVisit.back();
    InternalNodesToVisit.pop_back();

    // Keep track of the length of the string associated with the node. If
    // it's too short, we'll quit.
    unsigned Length = Curr->getConcatLen();

    // Iterate over each child, saving internal nodes for visiting, and
    // leaf nodes in LeafChildren. Internal nodes represent individual
    // strings, which may repeat.
    for (auto &ChildPair : Curr->Children) {
      // Save all of this node's children for processing.
      if (auto *InternalChild =
              dyn_cast<SuffixTreeInternalNode>(ChildPair.second)) {
        InternalNodesToVisit.push_back(InternalChild);
        continue;
      }

      if (Length < MinLength)
        continue;

      // Have an occurrence of a potentially repeated string. Save it.
      auto *Leaf = cast<SuffixTreeLeafNode>(ChildPair.second);
      RepeatedSubstringStarts.push_back(Leaf->getSuffixIdx());
    }

    // The root never represents a repeated substring. If we're looking at
    // that, then skip it.
    if (Curr->isRoot())
      continue;

    // Do we have any repeated substrings?
    if (RepeatedSubstringStarts.size() < 2)
      continue;

    // Yes. Update the state to reflect this, and then bail out.
    N = Curr;
    RS.Length = Length;
    for (unsigned StartIdx : RepeatedSubstringStarts)
      RS.StartIndices.push_back(StartIdx);
    break;
  }
  // At this point, either NewRS is an empty RepeatedSubstring, or it was
  // set in the above loop. Similarly, N is either 0, or the node
  // associated with NewRS.
}

} // namespace llvm

#endif // LLVM_SUPPORT_SUFFIXTREE_H
