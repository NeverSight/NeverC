//===- llvm/ADT/FoldingSet.h - Uniquing Hash Set ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines a hash set that can be used to remove duplication of nodes
/// in a graph.  This code was originally created by Chris Lattner for use with
/// SelectionDAGCSEMap, but was isolated to provide use across the llvm code
/// set.
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_FOLDINGSET_H
#define LLVM_ADT_FOLDINGSET_H

#include "csupport/lfolding_lset.h"
#include "llvm/ADT/EpochTracker.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/bit.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/SwapByteOrder.h"
#include <algorithm>
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <type_traits>
#include <utility>

namespace llvm {

/// This folding set used for two purposes:
///   1. Given information about a node we want to create, look up the unique
///      instance of the node in the set.  If the node already exists, return
///      it, otherwise return a token that makes the insertion cheap.
///   2. Given a node that has already been created, remove it from the set.
///
/// The hash table is linear-probing open addressing with tombstone-free
/// deletion, power-of-two capacity, and a 0.75 maximum load factor.
///
/// Any node that is to be included in the folding set must be a subclass of
/// FoldingSetNode.  The node class must also define a Profile method used to
/// establish the unique bits of data for the node.  The Profile method is
/// passed a FoldingSetNodeID object which is used to gather the bits.  Just
/// call one of the Add* functions defined in the FoldingSetBase::NodeID class.
/// NOTE: That the folding set does not own the nodes and it is the
/// responsibility of the user to dispose of the nodes.
///
/// Eg.
///    class MyNode : public FoldingSetNode {
///    private:
///      std::string Name;
///      unsigned Value;
///    public:
///      MyNode(const char *N, unsigned V) : Name(N), Value(V) {}
///       ...
///      void Profile(FoldingSetNodeID &ID) const {
///        ID.AddString(Name);
///        ID.AddInteger(Value);
///      }
///      ...
///    };
///
/// To define the folding set itself use the FoldingSet template;
///
/// Eg.
///    FoldingSet<MyNode> MyFoldingSet;
///
/// Four public methods are available to manipulate the folding set;
///
/// 1) If you have an existing node that you want add to the set but unsure
/// that the node might already exist then call;
///
///    MyNode *M = MyFoldingSet.GetOrInsertNode(N);
///
/// If The result is equal to the input then the node has been inserted.
/// Otherwise, the result is the node existing in the folding set, and the
/// input can be discarded (use the result instead.)
///
/// 2) If you are ready to construct a node but want to check if it already
/// exists, then call FindNodeOrInsertPos with a FoldingSetNodeID of the bits to
/// check;
///
///   FoldingSetNodeID ID;
///   ID.AddString(Name);
///   ID.AddInteger(Value);
///   void *InsertPoint;
///
///    MyNode *M = MyFoldingSet.FindNodeOrInsertPos(ID, InsertPoint);
///
/// If found then M will be non-NULL, else InsertPoint will point to where it
/// should be inserted using InsertNode.
///
/// 3) If you get a NULL result from FindNodeOrInsertPos then you can insert a
/// new node with InsertNode;
///
///    MyFoldingSet.InsertNode(M, InsertPoint);
///
/// InsertPoint survives intervening insertions, but M must profile identically
/// to the ID that produced it, or M becomes unfindable.
///
/// 4) Finally, if you want to remove a node from the folding set call;
///
///    bool WasRemoved = MyFoldingSet.RemoveNode(M);
///
/// The result indicates whether the node existed in the folding set.

class FoldingSetNodeID;
class FoldingSetIteratorImpl;
class FoldingSetBucketIteratorImpl;
class StringRef;

constexpr unsigned FoldingSetNotAHash = 0;

//===----------------------------------------------------------------------===//
/// Non-templated base class for FoldingSet and ContextualFoldingSet, holding
/// memory management and probing that do not depend on the node type.
class FoldingSetBase : public DebugEpochBase {
protected:
  /// Array of node pointers; a null entry marks an empty slot.
  void **Buckets = nullptr;

  /// Length of the Buckets array.  Always a power of 2.
  unsigned NumBuckets = 0;

  /// Number of nodes in the folding set.
  unsigned NumNodes = 0;

  explicit FoldingSetBase(unsigned Log2InitSize = 6);
  FoldingSetBase(FoldingSetBase &&Arg);
  FoldingSetBase &operator=(FoldingSetBase &&RHS);
  ~FoldingSetBase();

public:
  //===--------------------------------------------------------------------===//
  /// This class stores the state needed while a node belongs to a folding set.
  class Node {
  private:
    /// Hash of the node profile, cached so growth and removal never rerun
    /// Profile(). NotAHash marks a node that belongs to no set.
    uint32_t FoldingSetHash = FoldingSetNotAHash;

  public:
    Node() = default;

    uint32_t getFoldingSetHash() const { return FoldingSetHash; }
    void setFoldingSetHash(uint32_t Hash) { FoldingSetHash = Hash; }
  };

  /// clear - Remove all nodes from the folding set.
  void clear();

  /// size - Returns the number of nodes in the folding set.
  unsigned size() const { return NumNodes; }

  /// empty - Returns true if there are no nodes in the folding set.
  bool empty() const { return NumNodes == 0; }

  /// capacity - Returns the number of nodes permitted in the folding set
  /// before a rebucket operation is performed.
  unsigned capacity() const { return NumBuckets - NumBuckets / 4; }

protected:
  /// Functions provided by the derived class to compute folding properties.
  /// This is effectively a vtable for FoldingSetBase, except that we don't
  /// actually store a pointer to it in the object.
  struct FoldingSetInfo {
    /// GetNodeProfile - Instantiations of the FoldingSet template implement
    /// this function to gather data bits for the given node.
    void (*GetNodeProfile)(const FoldingSetBase *Self, Node *N,
                           FoldingSetNodeID &ID);

    /// NodeEquals - Instantiations of the FoldingSet template implement
    /// this function to compare the given node with the given ID.
    bool (*NodeEquals)(const FoldingSetBase *Self, Node *N,
                       const FoldingSetNodeID &ID, unsigned IDHash,
                       FoldingSetNodeID &TempID);

    /// ComputeNodeHash - Instantiations of the FoldingSet template implement
    /// this function to compute a hash value for the given node.
    unsigned (*ComputeNodeHash)(const FoldingSetBase *Self, Node *N,
                                FoldingSetNodeID &TempID);
  };

private:
  static constexpr uint64_t MaxNumBuckets = uint64_t{1} << 31;

  /// Put N in the first empty slot following its home bucket.
  void placeNode(Node *N, uint32_t Hash);

  /// Compare N with ID out of line so the temporary profile storage does not
  /// inflate the hot probe loop's stack frame.
  static bool nodeEquals(const FoldingSetInfo &Info, const FoldingSetBase *Self,
                         Node *N, const FoldingSetNodeID &ID, unsigned IDHash);

  /// Rehash into at least MinNumBuckets buckets.
  void grow(unsigned MinNumBuckets);

  friend class FoldingSetIteratorImpl;
  friend class FoldingSetBucketIteratorImpl;

protected:
  // The below methods are protected to encourage subclasses to provide a more
  // type-safe API.

  /// reserve - Increase the number of buckets such that adding the
  /// EltCount-th node won't cause a rebucket operation. reserve is permitted
  /// to allocate more space than requested by EltCount.
  void reserve(unsigned EltCount);

  /// RemoveNode - Remove a node from the folding set, returning true if one
  /// was removed or false if the node was not in the folding set.
  bool RemoveNode(Node *N);

  /// GetOrInsertNode - If there is an existing simple Node exactly
  /// equal to the specified node, return it.  Otherwise, insert 'N' and return
  /// it instead.
  Node *GetOrInsertNode(Node *N, const FoldingSetInfo &Info);

  /// FindNodeOrInsertPos - Look up the node specified by ID.  If it exists,
  /// return it.  If not, return the insertion token that will make insertion
  /// faster.
  Node *FindNodeOrInsertPos(const FoldingSetNodeID &ID, void *&InsertPos,
                            const FoldingSetInfo &Info);

  /// InsertNode - Insert the specified node into the folding set, knowing that
  /// it is not already in the folding set.  InsertPos must be obtained from
  /// FindNodeOrInsertPos for an ID that N profiles identically to.
  void InsertNode(Node *N, void *InsertPos);
};

//===----------------------------------------------------------------------===//

/// DefaultFoldingSetTrait - This class provides default implementations
/// for FoldingSetTrait implementations.
template <typename T> struct DefaultFoldingSetTrait {
  static void Profile(const T &X, FoldingSetNodeID &ID) { X.Profile(ID); }
  static void Profile(T &X, FoldingSetNodeID &ID) { X.Profile(ID); }

  // Equals - Test if the profile for X would match ID, using TempID
  // to compute a temporary ID if necessary. The default implementation
  // just calls Profile and does a regular comparison. Implementations
  // can override this to provide more efficient implementations.
  static inline bool Equals(T &X, const FoldingSetNodeID &ID, unsigned IDHash,
                            FoldingSetNodeID &TempID);

  // ComputeHash - Compute a hash value for X, using TempID to
  // compute a temporary ID if necessary. The default implementation
  // just calls Profile and does a regular hash computation.
  // Implementations can override this to provide more efficient
  // implementations.
  static inline unsigned ComputeHash(T &X, FoldingSetNodeID &TempID);
};

/// FoldingSetTrait - This trait class is used to define behavior of how
/// to "profile" (in the FoldingSet parlance) an object of a given type.
/// The default behavior is to invoke a 'Profile' method on an object, but
/// through template specialization the behavior can be tailored for specific
/// types.  Combined with the FoldingSetNodeWrapper class, one can add objects
/// to FoldingSets that were not originally designed to have that behavior.
template <typename T, typename Enable = void>
struct FoldingSetTrait : public DefaultFoldingSetTrait<T> {};

/// DefaultContextualFoldingSetTrait - Like DefaultFoldingSetTrait, but
/// for ContextualFoldingSets.
template <typename T, typename Ctx> struct DefaultContextualFoldingSetTrait {
  static void Profile(T &X, FoldingSetNodeID &ID, Ctx Context) {
    X.Profile(ID, Context);
  }

  static inline bool Equals(T &X, const FoldingSetNodeID &ID, unsigned IDHash,
                            FoldingSetNodeID &TempID, Ctx Context);
  static inline unsigned ComputeHash(T &X, FoldingSetNodeID &TempID,
                                     Ctx Context);
};

/// ContextualFoldingSetTrait - Like FoldingSetTrait, but for
/// ContextualFoldingSets.
template <typename T, typename Ctx>
struct ContextualFoldingSetTrait
    : public DefaultContextualFoldingSetTrait<T, Ctx> {};

//===--------------------------------------------------------------------===//
/// FoldingSetNodeIDRef - This class describes a reference to an interned
/// FoldingSetNodeID, which can be a useful to store node id data rather
/// than using plain FoldingSetNodeIDs, since the 32-element SmallVector
/// is often much larger than necessary, and the possibility of heap
/// allocation means it requires a non-trivial destructor call.
class FoldingSetNodeIDRef {
  const unsigned *Data = nullptr;
  size_t Size = 0;

public:
  FoldingSetNodeIDRef() = default;
  FoldingSetNodeIDRef(const unsigned *D, size_t S) : Data(D), Size(S) {}

  static constexpr unsigned NotAHash = FoldingSetNotAHash;

  /// ComputeHash - Compute a strong hash value for this FoldingSetNodeIDRef,
  /// used to lookup the node in the FoldingSetBase.
  unsigned ComputeHash() const {
    unsigned Hash =
        static_cast<unsigned>(hash_combine_range(Data, Data + Size));
    return Hash == NotAHash ? 1 : Hash;
  }

  bool operator==(FoldingSetNodeIDRef) const;

  bool operator!=(FoldingSetNodeIDRef RHS) const { return !(*this == RHS); }

  /// Used to compare the "ordering" of two nodes as defined by the
  /// profiled bits and their ordering defined by memcmp().
  bool operator<(FoldingSetNodeIDRef) const;

  const unsigned *getData() const { return Data; }
  size_t getSize() const { return Size; }
};

//===--------------------------------------------------------------------===//
/// FoldingSetNodeID - This class is used to gather all the unique data bits of
/// a node.  When all the bits are gathered this class is used to produce a
/// hash value for the node.
class FoldingSetNodeID {
  /// Bits - Vector of all the data bits that make the node unique.
  /// Use a SmallVector to avoid a heap allocation in the common case.
  SmallVector<unsigned, 32> Bits;

public:
  FoldingSetNodeID() = default;

  FoldingSetNodeID(FoldingSetNodeIDRef Ref)
      : Bits(Ref.getData(), Ref.getData() + Ref.getSize()) {}

  /// Add* - Add various data types to Bit data.
  void AddPointer(const void *Ptr) {
    // Note: this adds pointers to the hash using sizes and endianness that
    // depend on the host. It doesn't matter, however, because hashing on
    // pointer values is inherently unstable. Nothing should depend on the
    // ordering of nodes in the folding set.
    static_assert(sizeof(uintptr_t) <= sizeof(unsigned long long),
                  "unexpected pointer size");
    AddInteger(reinterpret_cast<uintptr_t>(Ptr));
  }
  void AddInteger(signed I) { Bits.push_back(I); }
  void AddInteger(unsigned I) { Bits.push_back(I); }
  void AddInteger(long I) { AddInteger((unsigned long)I); }
  void AddInteger(unsigned long I) {
    if (sizeof(long) == sizeof(int))
      AddInteger(unsigned(I));
    else if (sizeof(long) == sizeof(long long)) {
      AddInteger((unsigned long long)I);
    } else {
      llvm_unreachable("unexpected sizeof(long)");
    }
  }
  void AddInteger(long long I) { AddInteger((unsigned long long)I); }
  void AddInteger(unsigned long long I) {
    AddInteger(unsigned(I));
    AddInteger(unsigned(I >> 32));
  }

  void AddBoolean(bool B) { AddInteger(B ? 1U : 0U); }
  void AddString(::llvm::StringRef String);
  void AddNodeID(const FoldingSetNodeID &ID);

  template <typename T> inline void Add(const T &x) {
    FoldingSetTrait<T>::Profile(x, *this);
  }

  /// clear - Clear the accumulated profile, allowing this FoldingSetNodeID
  /// object to be used to compute a new profile.
  inline void clear() { Bits.clear(); }

  /// ComputeHash - Compute a strong hash value for this FoldingSetNodeID, used
  /// to lookup the node in the FoldingSetBase.
  unsigned ComputeHash() const {
    return FoldingSetNodeIDRef(Bits.data(), Bits.size()).ComputeHash();
  }

  /// operator== - Used to compare two nodes to each other.
  bool operator==(const FoldingSetNodeID &RHS) const;
  bool operator==(const FoldingSetNodeIDRef RHS) const;

  bool operator!=(const FoldingSetNodeID &RHS) const { return !(*this == RHS); }
  bool operator!=(const FoldingSetNodeIDRef RHS) const {
    return !(*this == RHS);
  }

  /// Used to compare the "ordering" of two nodes as defined by the
  /// profiled bits and their ordering defined by memcmp().
  bool operator<(const FoldingSetNodeID &RHS) const;
  bool operator<(const FoldingSetNodeIDRef RHS) const;

  /// Intern - Copy this node's data to a memory region allocated from the
  /// given allocator and return a FoldingSetNodeIDRef describing the
  /// interned data.
  FoldingSetNodeIDRef Intern(BumpPtrAllocator &Allocator) const;
};

// Convenience type to hide the implementation of the folding set.
using FoldingSetNode = FoldingSetBase::Node;
template <class T> class FoldingSetIterator;
template <class T> class FoldingSetBucketIterator;

// Definitions of FoldingSetTrait and ContextualFoldingSetTrait functions, which
// require the definition of FoldingSetNodeID.
template <typename T>
inline bool DefaultFoldingSetTrait<T>::Equals(T &X, const FoldingSetNodeID &ID,
                                              unsigned /*IDHash*/,
                                              FoldingSetNodeID &TempID) {
  FoldingSetTrait<T>::Profile(X, TempID);
  return TempID == ID;
}
template <typename T>
inline unsigned
DefaultFoldingSetTrait<T>::ComputeHash(T &X, FoldingSetNodeID &TempID) {
  FoldingSetTrait<T>::Profile(X, TempID);
  return TempID.ComputeHash();
}
template <typename T, typename Ctx>
inline bool DefaultContextualFoldingSetTrait<T, Ctx>::Equals(
    T &X, const FoldingSetNodeID &ID, unsigned /*IDHash*/,
    FoldingSetNodeID &TempID, Ctx Context) {
  ContextualFoldingSetTrait<T, Ctx>::Profile(X, TempID, Context);
  return TempID == ID;
}
template <typename T, typename Ctx>
inline unsigned DefaultContextualFoldingSetTrait<T, Ctx>::ComputeHash(
    T &X, FoldingSetNodeID &TempID, Ctx Context) {
  ContextualFoldingSetTrait<T, Ctx>::Profile(X, TempID, Context);
  return TempID.ComputeHash();
}

//===----------------------------------------------------------------------===//
/// FoldingSetImpl - An implementation detail that lets us share code between
/// FoldingSet and ContextualFoldingSet.
template <class Derived, class T> class FoldingSetImpl : public FoldingSetBase {
protected:
  explicit FoldingSetImpl(unsigned Log2InitSize)
      : FoldingSetBase(Log2InitSize) {}

  FoldingSetImpl(FoldingSetImpl &&Arg) = default;
  FoldingSetImpl &operator=(FoldingSetImpl &&RHS) = default;
  ~FoldingSetImpl() = default;

public:
  using iterator = FoldingSetIterator<T>;

  iterator begin() { return iterator(this, 0); }
  iterator end() { return iterator(this, NumBuckets); }

  using const_iterator = FoldingSetIterator<const T>;

  const_iterator begin() const { return const_iterator(this, 0); }
  const_iterator end() const { return const_iterator(this, NumBuckets); }

  using bucket_iterator = FoldingSetBucketIterator<T>;

  bucket_iterator bucket_begin(unsigned hash) {
    return bucket_iterator(this, hash, false);
  }

  bucket_iterator bucket_end(unsigned hash) {
    return bucket_iterator(this, hash, true);
  }

  /// reserve - Increase the number of buckets such that adding the
  /// EltCount-th node won't cause a rebucket operation. reserve is permitted
  /// to allocate more space than requested by EltCount.
  void reserve(unsigned EltCount) { FoldingSetBase::reserve(EltCount); }

  /// RemoveNode - Remove a node from the folding set, returning true if one
  /// was removed or false if the node was not in the folding set.
  bool RemoveNode(T *N) { return FoldingSetBase::RemoveNode(N); }

  /// GetOrInsertNode - If there is an existing simple Node exactly
  /// equal to the specified node, return it.  Otherwise, insert 'N' and
  /// return it instead.
  T *GetOrInsertNode(T *N) {
    return static_cast<T *>(
        FoldingSetBase::GetOrInsertNode(N, Derived::getFoldingSetInfo()));
  }

  /// FindNodeOrInsertPos - Look up the node specified by ID.  If it exists,
  /// return it.  If not, return the insertion token that will make insertion
  /// faster.
  T *FindNodeOrInsertPos(const FoldingSetNodeID &ID, void *&InsertPos) {
    return static_cast<T *>(FoldingSetBase::FindNodeOrInsertPos(
        ID, InsertPos, Derived::getFoldingSetInfo()));
  }

  /// InsertNode - Insert the specified node into the folding set, knowing that
  /// it is not already in the folding set.  InsertPos must be obtained from
  /// FindNodeOrInsertPos.
  void InsertNode(T *N, void *InsertPos) {
    FoldingSetBase::InsertNode(N, InsertPos);
  }

  /// InsertNode - Insert the specified node into the folding set, knowing that
  /// it is not already in the folding set.
  void InsertNode(T *N) {
    T *Inserted = GetOrInsertNode(N);
    (void)Inserted;
    assert(Inserted == N && "Node already inserted!");
  }
};

//===----------------------------------------------------------------------===//
/// FoldingSet - This template class is used to instantiate a specialized
/// implementation of the folding set to the node class T.  T must be a
/// subclass of FoldingSetNode and implement a Profile function.
///
/// Note that this set type is movable and move-assignable. However, its
/// moved-from state is not a valid state for anything other than
/// move-assigning and destroying. This is primarily to enable movable APIs
/// that incorporate these objects.
template <class T> class FoldingSet : public FoldingSetImpl<FoldingSet<T>, T> {
  using Super = FoldingSetImpl<FoldingSet, T>;
  using Node = typename Super::Node;

  /// GetNodeProfile - Each instantiation of the FoldingSet needs to provide a
  /// way to convert nodes into a unique specifier.
  static void GetNodeProfile(const FoldingSetBase *, Node *N,
                             FoldingSetNodeID &ID) {
    T *TN = static_cast<T *>(N);
    FoldingSetTrait<T>::Profile(*TN, ID);
  }

  /// NodeEquals - Instantiations may optionally provide a way to compare a
  /// node with a specified ID.
  static bool NodeEquals(const FoldingSetBase *, Node *N,
                         const FoldingSetNodeID &ID, unsigned IDHash,
                         FoldingSetNodeID &TempID) {
    T *TN = static_cast<T *>(N);
    return FoldingSetTrait<T>::Equals(*TN, ID, IDHash, TempID);
  }

  /// ComputeNodeHash - Instantiations may optionally provide a way to compute a
  /// hash value directly from a node.
  static unsigned ComputeNodeHash(const FoldingSetBase *, Node *N,
                                  FoldingSetNodeID &TempID) {
    T *TN = static_cast<T *>(N);
    return FoldingSetTrait<T>::ComputeHash(*TN, TempID);
  }

  static const FoldingSetBase::FoldingSetInfo &getFoldingSetInfo() {
    static constexpr FoldingSetBase::FoldingSetInfo Info = {
        GetNodeProfile, NodeEquals, ComputeNodeHash};
    return Info;
  }
  friend Super;

public:
  explicit FoldingSet(unsigned Log2InitSize = 6) : Super(Log2InitSize) {}
  FoldingSet(FoldingSet &&Arg) = default;
  FoldingSet &operator=(FoldingSet &&RHS) = default;
};

//===----------------------------------------------------------------------===//
/// ContextualFoldingSet - This template class is a further refinement
/// of FoldingSet which provides a context argument when calling
/// Profile on its nodes.  Currently, that argument is fixed at
/// initialization time.
///
/// T must be a subclass of FoldingSetNode and implement a Profile
/// function with signature
///   void Profile(FoldingSetNodeID &, Ctx);
template <class T, class Ctx>
class ContextualFoldingSet
    : public FoldingSetImpl<ContextualFoldingSet<T, Ctx>, T> {
  // Unfortunately, this can't derive from FoldingSet<T> because the
  // construction of the vtable for FoldingSet<T> requires
  // FoldingSet<T>::GetNodeProfile to be instantiated, which in turn
  // requires a single-argument T::Profile().

  using Super = FoldingSetImpl<ContextualFoldingSet, T>;
  using Node = typename Super::Node;

  Ctx Context;

  static const Ctx &getContext(const FoldingSetBase *Base) {
    return static_cast<const ContextualFoldingSet *>(Base)->Context;
  }

  /// GetNodeProfile - Each instantiatation of the FoldingSet needs to provide a
  /// way to convert nodes into a unique specifier.
  static void GetNodeProfile(const FoldingSetBase *Base, Node *N,
                             FoldingSetNodeID &ID) {
    T *TN = static_cast<T *>(N);
    ContextualFoldingSetTrait<T, Ctx>::Profile(*TN, ID, getContext(Base));
  }

  static bool NodeEquals(const FoldingSetBase *Base, Node *N,
                         const FoldingSetNodeID &ID, unsigned IDHash,
                         FoldingSetNodeID &TempID) {
    T *TN = static_cast<T *>(N);
    return ContextualFoldingSetTrait<T, Ctx>::Equals(*TN, ID, IDHash, TempID,
                                                     getContext(Base));
  }

  static unsigned ComputeNodeHash(const FoldingSetBase *Base, Node *N,
                                  FoldingSetNodeID &TempID) {
    T *TN = static_cast<T *>(N);
    return ContextualFoldingSetTrait<T, Ctx>::ComputeHash(*TN, TempID,
                                                          getContext(Base));
  }

  static const FoldingSetBase::FoldingSetInfo &getFoldingSetInfo() {
    static constexpr FoldingSetBase::FoldingSetInfo Info = {
        GetNodeProfile, NodeEquals, ComputeNodeHash};
    return Info;
  }
  friend Super;

public:
  explicit ContextualFoldingSet(Ctx Context, unsigned Log2InitSize = 6)
      : Super(Log2InitSize), Context(Context) {}

  Ctx getContext() const { return Context; }
};

//===----------------------------------------------------------------------===//
/// FoldingSetVector - This template class combines a FoldingSet and a vector
/// to provide the interface of FoldingSet but with deterministic iteration
/// order based on the insertion order. T must be a subclass of FoldingSetNode
/// and implement a Profile function.
template <class T, class VectorT = SmallVector<T *, 8>> class FoldingSetVector {
  FoldingSet<T> Set;
  VectorT Vector;

public:
  explicit FoldingSetVector(unsigned Log2InitSize = 6) : Set(Log2InitSize) {}

  using iterator = pointee_iterator<typename VectorT::iterator>;

  iterator begin() { return Vector.begin(); }
  iterator end() { return Vector.end(); }

  using const_iterator = pointee_iterator<typename VectorT::const_iterator>;

  const_iterator begin() const { return Vector.begin(); }
  const_iterator end() const { return Vector.end(); }

  /// clear - Remove all nodes from the folding set.
  void clear() {
    Set.clear();
    Vector.clear();
  }

  /// FindNodeOrInsertPos - Look up the node specified by ID.  If it exists,
  /// return it.  If not, return the insertion token that will make insertion
  /// faster.
  T *FindNodeOrInsertPos(const FoldingSetNodeID &ID, void *&InsertPos) {
    return Set.FindNodeOrInsertPos(ID, InsertPos);
  }

  /// GetOrInsertNode - If there is an existing simple Node exactly
  /// equal to the specified node, return it.  Otherwise, insert 'N' and
  /// return it instead.
  T *GetOrInsertNode(T *N) {
    T *Result = Set.GetOrInsertNode(N);
    if (Result == N)
      Vector.push_back(N);
    return Result;
  }

  /// InsertNode - Insert the specified node into the folding set, knowing that
  /// it is not already in the folding set.  InsertPos must be obtained from
  /// FindNodeOrInsertPos.
  void InsertNode(T *N, void *InsertPos) {
    Set.InsertNode(N, InsertPos);
    Vector.push_back(N);
  }

  /// InsertNode - Insert the specified node into the folding set, knowing that
  /// it is not already in the folding set.
  void InsertNode(T *N) {
    Set.InsertNode(N);
    Vector.push_back(N);
  }

  /// size - Returns the number of nodes in the folding set.
  unsigned size() const { return Set.size(); }

  /// empty - Returns true if there are no nodes in the folding set.
  bool empty() const { return Set.empty(); }
};

//===----------------------------------------------------------------------===//
/// FoldingSetIteratorImpl - This is the common iterator support shared by all
/// folding sets, which knows how to walk the folding set hash table.
class FoldingSetIteratorImpl : DebugEpochBase::HandleBase {
protected:
  const FoldingSetBase *Set = nullptr;
  unsigned Index = 0;

  FoldingSetIteratorImpl(const FoldingSetBase *Set, unsigned Index);

  void advance();

  FoldingSetNode *getNode() const {
    assert(isHandleInSync() && "invalid iterator access!");
    return static_cast<FoldingSetNode *>(Set->Buckets[Index]);
  }

public:
  bool operator==(const FoldingSetIteratorImpl &RHS) const {
    assert(isHandleInSync() && RHS.isHandleInSync() && "handle not in sync!");
    return Set == RHS.Set && Index == RHS.Index;
  }
  bool operator!=(const FoldingSetIteratorImpl &RHS) const {
    return !(*this == RHS);
  }
};

template <class T> class FoldingSetIterator : public FoldingSetIteratorImpl {
public:
  explicit FoldingSetIterator(const FoldingSetBase *Set, unsigned Index)
      : FoldingSetIteratorImpl(Set, Index) {}

  T &operator*() const { return *static_cast<T *>(getNode()); }

  T *operator->() const { return static_cast<T *>(getNode()); }

  inline FoldingSetIterator &operator++() { // Preincrement
    advance();
    return *this;
  }
  FoldingSetIterator operator++(int) { // Postincrement
    FoldingSetIterator tmp = *this;
    ++*this;
    return tmp;
  }
};

//===----------------------------------------------------------------------===//
/// FoldingSetBucketIteratorImpl - This is the common bucket iterator support
/// shared by all folding sets, which knows how to walk a particular bucket
/// of a folding set hash table.
class FoldingSetBucketIteratorImpl : DebugEpochBase::HandleBase {
protected:
  const FoldingSetBase *Set = nullptr;
  unsigned HomeBucket = 0;
  unsigned Index = 0;

  FoldingSetBucketIteratorImpl(const FoldingSetBase *Set, unsigned Hash,
                               bool End);

  void advance();

  FoldingSetNode *getNode() const {
    assert(isHandleInSync() && "invalid iterator access!");
    return static_cast<FoldingSetNode *>(Set->Buckets[Index]);
  }

public:
  bool operator==(const FoldingSetBucketIteratorImpl &RHS) const {
    assert(isHandleInSync() && RHS.isHandleInSync() && "handle not in sync!");
    return Set == RHS.Set && HomeBucket == RHS.HomeBucket && Index == RHS.Index;
  }
  bool operator!=(const FoldingSetBucketIteratorImpl &RHS) const {
    return !(*this == RHS);
  }
};

template <class T>
class FoldingSetBucketIterator : public FoldingSetBucketIteratorImpl {
public:
  FoldingSetBucketIterator(const FoldingSetBase *Set, unsigned Hash, bool End)
      : FoldingSetBucketIteratorImpl(Set, Hash, End) {}

  T &operator*() const { return *static_cast<T *>(getNode()); }
  T *operator->() const { return static_cast<T *>(getNode()); }

  inline FoldingSetBucketIterator &operator++() { // Preincrement
    advance();
    return *this;
  }
  FoldingSetBucketIterator operator++(int) { // Postincrement
    FoldingSetBucketIterator tmp = *this;
    ++*this;
    return tmp;
  }
};

//===----------------------------------------------------------------------===//
/// FoldingSetNodeWrapper - This template class is used to "wrap" arbitrary
/// types in an enclosing object so that they can be inserted into FoldingSets.
template <typename T> class FoldingSetNodeWrapper : public FoldingSetNode {
  T data;

public:
  template <typename... Ts>
  explicit FoldingSetNodeWrapper(Ts &&...Args)
      : data(std::forward<Ts>(Args)...) {}

  void Profile(FoldingSetNodeID &ID) { FoldingSetTrait<T>::Profile(data, ID); }

  T &getValue() { return data; }
  const T &getValue() const { return data; }

  operator T &() { return data; }
  operator const T &() const { return data; }
};

//===----------------------------------------------------------------------===//
/// FastFoldingSetNode - This is a subclass of FoldingSetNode which stores
/// a FoldingSetNodeID value rather than requiring the node to recompute it
/// each time it is needed. This trades space for speed (which can be
/// significant if the ID is long), and it also permits nodes to drop
/// information that would otherwise only be required for recomputing an ID.
class FastFoldingSetNode : public FoldingSetNode {
  FoldingSetNodeID FastID;

protected:
  explicit FastFoldingSetNode(const FoldingSetNodeID &ID) : FastID(ID) {}

public:
  void Profile(FoldingSetNodeID &ID) const { ID.AddNodeID(FastID); }
};

//===----------------------------------------------------------------------===//
// Partial specializations of FoldingSetTrait.

template <typename T> struct FoldingSetTrait<T *> {
  static inline void Profile(T *X, FoldingSetNodeID &ID) { ID.AddPointer(X); }
};
template <typename T1, typename T2> struct FoldingSetTrait<std::pair<T1, T2>> {
  static inline void Profile(const std::pair<T1, T2> &P, FoldingSetNodeID &ID) {
    ID.Add(P.first);
    ID.Add(P.second);
  }
};

template <typename T>
struct FoldingSetTrait<T, std::enable_if_t<std::is_enum<T>::value>> {
  static void Profile(const T &X, FoldingSetNodeID &ID) {
    ID.AddInteger(to_underlying(X));
  }
};

/*== Inline implementations (moved from cpp_bridge.cpp) ==*/

inline bool FoldingSetNodeIDRef::operator==(FoldingSetNodeIDRef RHS) const {
  if (Size != RHS.Size)
    return false;
  return memcmp(Data, RHS.Data, Size * sizeof(*Data)) == 0;
}

inline bool FoldingSetNodeIDRef::operator<(FoldingSetNodeIDRef RHS) const {
  if (Size != RHS.Size)
    return Size < RHS.Size;
  return memcmp(Data, RHS.Data, Size * sizeof(*Data)) < 0;
}

inline void FoldingSetNodeID::AddString(::llvm::StringRef String) {
  unsigned Size = String.size();
  unsigned NumInserts = 1 + divideCeil(Size, 4);
  unsigned buf[1024];
  unsigned *dst = buf;
  unsigned cap = 1024;
  if (NumInserts > 1024) {
    dst = (unsigned *)malloc(NumInserts * sizeof(unsigned));
    cap = NumInserts;
  }
  unsigned count = csupport_folding_set_id_add_string(
      dst, cap, 0, String.data(), Size, ::llvm::sys::IsBigEndianHost ? 1 : 0);
  Bits.append(dst, dst + count);
  if (dst != buf)
    free(dst);
}

inline void FoldingSetNodeID::AddNodeID(const FoldingSetNodeID &ID) {
  Bits.append(ID.Bits.begin(), ID.Bits.end());
}

inline bool FoldingSetNodeID::operator==(const FoldingSetNodeID &RHS) const {
  return *this == FoldingSetNodeIDRef(RHS.Bits.data(), RHS.Bits.size());
}

inline bool FoldingSetNodeID::operator==(FoldingSetNodeIDRef RHS) const {
  return FoldingSetNodeIDRef(Bits.data(), Bits.size()) == RHS;
}

inline bool FoldingSetNodeID::operator<(const FoldingSetNodeID &RHS) const {
  return *this < FoldingSetNodeIDRef(RHS.Bits.data(), RHS.Bits.size());
}

inline bool FoldingSetNodeID::operator<(FoldingSetNodeIDRef RHS) const {
  return FoldingSetNodeIDRef(Bits.data(), Bits.size()) < RHS;
}

inline FoldingSetNodeIDRef
FoldingSetNodeID::Intern(BumpPtrAllocator &Allocator) const {
  unsigned *New = Allocator.Allocate<unsigned>(Bits.size());
  memcpy(New, Bits.begin(), Bits.size() * sizeof(unsigned));
  return FoldingSetNodeIDRef(New, Bits.size());
}

#define AllocateBuckets(n) csupport_folding_set_allocate_buckets(n)

/// Encode a 32-bit hash as an opaque non-null token for InsertPos.
static inline void *encodeFoldingSetHash(uint32_t Hash) {
  return reinterpret_cast<void *>(static_cast<uintptr_t>(Hash));
}

static inline uint32_t decodeFoldingSetHash(void *InsertPos) {
  return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InsertPos));
}

inline FoldingSetBase::FoldingSetBase(unsigned Log2InitSize) {
  assert(5 < Log2InitSize && Log2InitSize < 32 &&
         "Initial hash table size out of range");
  NumBuckets = 1u << Log2InitSize;
  Buckets = AllocateBuckets(NumBuckets);
  NumNodes = 0;
}

inline FoldingSetBase::FoldingSetBase(FoldingSetBase &&Arg)
    : Buckets(Arg.Buckets), NumBuckets(Arg.NumBuckets), NumNodes(Arg.NumNodes) {
  Arg.incrementEpoch();
  Arg.Buckets = nullptr;
  Arg.NumBuckets = 0;
  Arg.NumNodes = 0;
}

inline FoldingSetBase &FoldingSetBase::operator=(FoldingSetBase &&RHS) {
  if (this == &RHS)
    return *this;

  incrementEpoch();
  RHS.incrementEpoch();
  free(Buckets);
  Buckets = RHS.Buckets;
  NumBuckets = RHS.NumBuckets;
  NumNodes = RHS.NumNodes;
  RHS.Buckets = nullptr;
  RHS.NumBuckets = 0;
  RHS.NumNodes = 0;
  return *this;
}

inline FoldingSetBase::~FoldingSetBase() { free(Buckets); }

inline void FoldingSetBase::clear() {
  incrementEpoch();
  if (NumBuckets)
    memset(Buckets, 0, NumBuckets * sizeof(void *));
  NumNodes = 0;
}

inline void FoldingSetBase::placeNode(Node *N, uint32_t Hash) {
  unsigned Mask = NumBuckets - 1;
  unsigned I = Hash & Mask;
  while (Buckets[I]) {
    assert(Buckets[I] != N && "Node already in the folding set");
    I = (I + 1) & Mask;
  }
  Buckets[I] = N;
  ++NumNodes;
}

inline void FoldingSetBase::grow(unsigned MinNumBuckets) {
  unsigned NewBucketCount = std::max(64u, llvm::bit_ceil(MinNumBuckets));
  assert(NewBucketCount > NumBuckets && "Can't shrink a folding set");

  FoldingSetBase Tmp(llvm::Log2_32(NewBucketCount));
  for (unsigned I = 0; I != NumBuckets; ++I) {
    if (void *N = Buckets[I]) {
      Node *FoldingNode = static_cast<Node *>(N);
      Tmp.placeNode(FoldingNode, FoldingNode->getFoldingSetHash());
    }
  }
  *this = std::move(Tmp);
}

inline void FoldingSetBase::reserve(unsigned EltCount) {
  if (EltCount <= capacity())
    return;

  uint64_t MinNumBuckets = uint64_t{EltCount} + (uint64_t{EltCount} + 2) / 3;
  if (LLVM_UNLIKELY(MinNumBuckets > MaxNumBuckets))
    report_bad_alloc_error("FoldingSet capacity exceeds maximum");
  grow(static_cast<unsigned>(MinNumBuckets));
}

LLVM_ATTRIBUTE_NOINLINE inline bool
FoldingSetBase::nodeEquals(const FoldingSetInfo &Info,
                           const FoldingSetBase *Self, Node *N,
                           const FoldingSetNodeID &ID, unsigned IDHash) {
  FoldingSetNodeID TempID;
  return Info.NodeEquals(Self, N, ID, IDHash, TempID);
}

inline FoldingSetBase::Node *FoldingSetBase::FindNodeOrInsertPos(
    const FoldingSetNodeID &ID, void *&InsertPos, const FoldingSetInfo &Info) {
  unsigned IDHash = ID.ComputeHash();
  unsigned Mask = NumBuckets - 1;
  for (unsigned I = IDHash & Mask; Buckets[I]; I = (I + 1) & Mask) {
    Node *N = static_cast<Node *>(Buckets[I]);
    if (N->getFoldingSetHash() == IDHash &&
        nodeEquals(Info, this, N, ID, IDHash)) {
      InsertPos = nullptr;
      return N;
    }
  }

  InsertPos = encodeFoldingSetHash(IDHash);
  return nullptr;
}

inline void FoldingSetBase::InsertNode(Node *N, void *InsertPos) {
  assert(N && "Cannot insert a null node");
  assert(InsertPos && "Invalid InsertPos!");
  incrementEpoch();
  if (LLVM_UNLIKELY(NumNodes + 1 > capacity())) {
    if (LLVM_UNLIKELY(NumBuckets >= MaxNumBuckets))
      report_bad_alloc_error("FoldingSet capacity exceeds maximum");
    grow(NumBuckets * 2);
  }
  uint32_t Hash = decodeFoldingSetHash(InsertPos);
  placeNode(N, Hash);
  N->setFoldingSetHash(Hash);
}

inline bool FoldingSetBase::RemoveNode(Node *N) {
  assert(N && "Cannot remove a null node");
  uint32_t Hash = N->getFoldingSetHash();
  if (Hash == FoldingSetNodeIDRef::NotAHash)
    return false;

  unsigned Mask = NumBuckets - 1;
  unsigned I = Hash & Mask;
  while (Buckets[I] != N) {
    if (LLVM_UNLIKELY(!Buckets[I]))
      return false;
    I = (I + 1) & Mask;
  }

  incrementEpoch();
  for (unsigned J = (I + 1) & Mask; Buckets[J]; J = (J + 1) & Mask) {
    unsigned Ideal = static_cast<Node *>(Buckets[J])->getFoldingSetHash();
    if (((I - Ideal) & Mask) < ((J - Ideal) & Mask)) {
      Buckets[I] = Buckets[J];
      I = J;
    }
  }
  Buckets[I] = nullptr;
  N->setFoldingSetHash(FoldingSetNodeIDRef::NotAHash);
  --NumNodes;
  return true;
}

inline FoldingSetBase::Node *
FoldingSetBase::GetOrInsertNode(FoldingSetBase::Node *N,
                                const FoldingSetInfo &Info) {
  FoldingSetNodeID ID;
  Info.GetNodeProfile(this, N, ID);
  void *IP;
  if (Node *E = FindNodeOrInsertPos(ID, IP, Info))
    return E;
  InsertNode(N, IP);
  return N;
}

inline FoldingSetIteratorImpl::FoldingSetIteratorImpl(const FoldingSetBase *Set,
                                                      unsigned Index)
    : DebugEpochBase::HandleBase(Set), Set(Set), Index(Index) {
  while (this->Index < Set->NumBuckets && !Set->Buckets[this->Index])
    ++this->Index;
}

inline void FoldingSetIteratorImpl::advance() {
  assert(isHandleInSync() && "invalid iterator access!");
  do
    ++Index;
  while (Index < Set->NumBuckets && !Set->Buckets[Index]);
}

inline FoldingSetBucketIteratorImpl::FoldingSetBucketIteratorImpl(
    const FoldingSetBase *Set, unsigned Hash, bool End)
    : DebugEpochBase::HandleBase(Set), Set(Set),
      HomeBucket(Hash & (Set->NumBuckets - 1)),
      Index(End ? Set->NumBuckets : HomeBucket) {
  if (End)
    return;

  unsigned Mask = Set->NumBuckets - 1;
  while (
      Set->Buckets[Index] &&
      (static_cast<FoldingSetNode *>(Set->Buckets[Index])->getFoldingSetHash() &
       Mask) != HomeBucket)
    Index = (Index + 1) & Mask;
  if (!Set->Buckets[Index])
    Index = Set->NumBuckets;
}

inline void FoldingSetBucketIteratorImpl::advance() {
  assert(isHandleInSync() && "invalid iterator access!");
  unsigned Mask = Set->NumBuckets - 1;
  Index = (Index + 1) & Mask;
  while (
      Set->Buckets[Index] &&
      (static_cast<FoldingSetNode *>(Set->Buckets[Index])->getFoldingSetHash() &
       Mask) != HomeBucket)
    Index = (Index + 1) & Mask;
  if (!Set->Buckets[Index])
    Index = Set->NumBuckets;
}

} // end namespace llvm

#endif // LLVM_ADT_FOLDINGSET_H
