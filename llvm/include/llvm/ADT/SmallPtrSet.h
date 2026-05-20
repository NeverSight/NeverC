//===- llvm/ADT/SmallPtrSet.h - 'Normally small' pointer set ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the SmallPtrSet class.  See the doxygen comment for
/// SmallPtrSetImplBase for more details on the algorithm used.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_SMALLPTRSET_H
#define LLVM_ADT_SMALLPTRSET_H

#include "llvm/ADT/EpochTracker.h"
#include "llvm/ADT/bit.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemAlloc.h"
#include "llvm/Support/ReverseIteration.h"
#include "llvm/Support/type_traits.h"
#include <algorithm>
#include <assert.h>
#include <initializer_list>
#include <iterator>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <type_traits>
#include <utility>

namespace llvm {

/// SmallPtrSetImplBase - This is the common code shared among all the
/// SmallPtrSet<>'s, which is almost everything.  SmallPtrSet has two modes, one
/// for small and one for large sets.
///
/// Small sets use an array of pointers allocated in the SmallPtrSet object,
/// which is treated as a simple array of pointers.  When a pointer is added to
/// the set, the array is scanned to see if the element already exists, if not
/// the element is 'pushed back' onto the array.  If we run out of space in the
/// array, we grow into the 'large set' case.  SmallSet should be used when the
/// sets are often small.  In this case, no memory allocation is used, and only
/// light-weight and cache-efficient scanning is used.
///
/// Large sets use a linear-probed hash table with deletion implemented using
/// Knuth TAOCP 6.4 Algorithm R: `erase` opens a hole, walks forward sliding
/// each following entry whose probe path crosses the hole back into it (the
/// hole moves with each slide), and stops at the next empty slot.  Empty
/// buckets are represented with an illegal pointer value (-1) to allow null
/// pointers to be inserted; no tombstone state is needed.  The hash table is
/// resized when the table is 2/3 or more.  When this happens, the table is
/// doubled in size.
class SmallPtrSetImplBase : public DebugEpochBase {
  friend class SmallPtrSetIteratorImpl;

protected:
  /// The current set of buckets, in either small or big representation.
  const void **CurArray;
  /// CurArraySize - The allocated size of CurArray, always a power of two.
  unsigned CurArraySize;

  /// Number of elements in CurArray that contain a value.
  /// If small, all these elements are at the beginning of CurArray and the rest
  /// is uninitialized.
  unsigned NumEntries;
  /// Whether the set is in small representation.
  bool IsSmall;

  // Helpers to copy and move construct a SmallPtrSet.
  SmallPtrSetImplBase(const void **SmallStorage,
                      const SmallPtrSetImplBase &that);
  SmallPtrSetImplBase(const void **SmallStorage, unsigned SmallSize,
                      const void **RHSSmallStorage,
                      SmallPtrSetImplBase &&that);

  explicit SmallPtrSetImplBase(const void **SmallStorage, unsigned SmallSize)
      : CurArray(SmallStorage), CurArraySize(SmallSize), NumEntries(0),
        IsSmall(true) {
    assert(llvm::has_single_bit(SmallSize) &&
           "Initial size must be a power of two!");
  }

  ~SmallPtrSetImplBase() {
    if (!isSmall())
      free(CurArray);
  }

public:
  using size_type = unsigned;

  SmallPtrSetImplBase &operator=(const SmallPtrSetImplBase &) = delete;

  [[nodiscard]] bool empty() const { return size() == 0; }
  [[nodiscard]] size_type size() const { return NumEntries; }
  [[nodiscard]] size_type capacity() const { return CurArraySize; }

  void clear() {
    incrementEpoch();
    if (!isSmall()) {
      if (size() * 4 < CurArraySize && CurArraySize > 32)
        return shrink_and_clear();
      memset(CurArray, -1, CurArraySize * sizeof(void *));
    }
    NumEntries = 0;
  }

  void reserve(size_type NewNumEntries) {
    incrementEpoch();
    if (NewNumEntries == 0)
      return;
    if (isSmall() && NewNumEntries <= CurArraySize)
      return;
    if (!isSmall() && ((NewNumEntries - 1) * 3) < (CurArraySize * 2))
      return;
    size_type NewSize = NewNumEntries + (NewNumEntries / 2);
    NewSize = llvm::bit_ceil(NewSize);
    NewSize = std::max(128u, NewSize);
    Grow(NewSize);
  }

protected:
  static void *getEmptyMarker() {
    return reinterpret_cast<void *>(-1);
  }

  const void **EndPointer() const {
    return isSmall() ? CurArray + NumEntries : CurArray + CurArraySize;
  }

  iterator_range<const void **> small_buckets() {
    return make_range(CurArray, CurArray + NumEntries);
  }

  iterator_range<const void *const *> small_buckets() const {
    return {CurArray, CurArray + NumEntries};
  }

  iterator_range<const void **> buckets() {
    return make_range(CurArray, EndPointer());
  }

  iterator_range<const void *const *> buckets() const {
    return make_range(CurArray, EndPointer());
  }

  std::pair<const void *const *, bool> insert_imp(const void *Ptr) {
    if (isSmall()) {
      for (const void *&Bucket : small_buckets()) {
        if (Bucket == Ptr)
          return {&Bucket, false};
      }

      if (NumEntries < CurArraySize) {
        CurArray[NumEntries++] = Ptr;
        incrementEpoch();
        return {CurArray + (NumEntries - 1), true};
      }
    }
    return insert_imp_big(Ptr);
  }

  bool erase_imp(const void *Ptr) {
    if (isSmall()) {
      for (const void *&Bucket : small_buckets()) {
        if (Bucket == Ptr) {
          Bucket = CurArray[--NumEntries];
          incrementEpoch();
          return true;
        }
      }
      return false;
    }

    auto *Bucket = doFind(Ptr);
    if (!Bucket)
      return false;

    eraseFromBucket(const_cast<const void **>(Bucket));
    --NumEntries;
    incrementEpoch();
    return true;
  }

  const void *const *find_imp(const void *Ptr) const {
    if (isSmall()) {
      for (const void *const &Bucket : small_buckets())
        if (Bucket == Ptr)
          return &Bucket;
      return EndPointer();
    }

    if (auto *Bucket = doFind(Ptr))
      return Bucket;
    return EndPointer();
  }

  bool contains_imp(const void *Ptr) const {
    if (isSmall()) {
      for (const void *const &Bucket : small_buckets())
        if (Bucket == Ptr)
          return true;
      return false;
    }
    return doFind(Ptr) != nullptr;
  }

  bool isSmall() const { return IsSmall; }

private:
  std::pair<const void *const *, bool> insert_imp_big(const void *Ptr);

  const void *const *doFind(const void *Ptr) const;
  const void *const *FindBucketFor(const void *Ptr) const;
  void shrink_and_clear();

protected:
  void eraseFromBucket(const void **Bucket);

  void Grow(unsigned NewSize);

  void swap(const void **SmallStorage, const void **RHSSmallStorage,
            SmallPtrSetImplBase &RHS);

  void copyFrom(const void **SmallStorage, const SmallPtrSetImplBase &RHS);
  void moveFrom(const void **SmallStorage, unsigned SmallSize,
                const void **RHSSmallStorage, SmallPtrSetImplBase &&RHS);

private:
  void moveHelper(const void **SmallStorage, unsigned SmallSize,
                  const void **RHSSmallStorage, SmallPtrSetImplBase &&RHS);
  void copyHelper(const SmallPtrSetImplBase &RHS);
};

/// SmallPtrSetIteratorImpl - This is the common base class shared between all
/// instances of SmallPtrSetIterator.
class LLVM_DEBUGEPOCHBASE_HANDLEBASE_EMPTYBASE SmallPtrSetIteratorImpl
    : public DebugEpochBase::HandleBase {
public:
  explicit SmallPtrSetIteratorImpl(const void *const *BP, const void *const *E,
                                   const DebugEpochBase &Epoch)
      : DebugEpochBase::HandleBase(&Epoch), Bucket(BP), End(E) {
    AdvanceIfNotValid();
  }

  bool operator==(const SmallPtrSetIteratorImpl &RHS) const {
    return Bucket == RHS.Bucket;
  }
  bool operator!=(const SmallPtrSetIteratorImpl &RHS) const {
    return Bucket != RHS.Bucket;
  }

protected:
  void *dereference() const {
    assert(isHandleInSync() && "invalid iterator access!");
    assert(Bucket < End);
    return const_cast<void *>(*Bucket);
  }
  void increment() {
    assert(isHandleInSync() && "invalid iterator access!");
    ++Bucket;
    AdvanceIfNotValid();
  }

private:
  void AdvanceIfNotValid() {
    assert(Bucket <= End);
    while (Bucket != End &&
           *Bucket == SmallPtrSetImplBase::getEmptyMarker())
      ++Bucket;
  }

  using BucketItTy =
      std::conditional_t<shouldReverseIterate(),
                         std::reverse_iterator<const void *const *>,
                         const void *const *>;

  BucketItTy Bucket;
  BucketItTy End;
};

/// SmallPtrSetIterator - This implements a const_iterator for SmallPtrSet.
template <typename PtrTy>
class SmallPtrSetIterator : public SmallPtrSetIteratorImpl {
  using PtrTraits = PointerLikeTypeTraits<PtrTy>;

public:
  using value_type = PtrTy;
  using reference = PtrTy;
  using pointer = PtrTy;
  using difference_type = std::ptrdiff_t;
  using iterator_category = std::forward_iterator_tag;

  using SmallPtrSetIteratorImpl::SmallPtrSetIteratorImpl;

  [[nodiscard]] const PtrTy operator*() const {
    return PtrTraits::getFromVoidPointer(dereference());
  }

  inline SmallPtrSetIterator &operator++() {
    increment();
    return *this;
  }

  SmallPtrSetIterator operator++(int) {
    SmallPtrSetIterator tmp = *this;
    increment();
    return tmp;
  }
};

/// A templated base class for \c SmallPtrSet which provides the
/// typesafe interface that is common across all small sizes.
///
/// This is particularly useful for passing around between interface boundaries
/// to avoid encoding a particular small size in the interface boundary.
template <typename PtrType> class SmallPtrSetImpl : public SmallPtrSetImplBase {
  using ConstPtrType = typename add_const_past_pointer<PtrType>::type;
  using PtrTraits = PointerLikeTypeTraits<PtrType>;
  using ConstPtrTraits = PointerLikeTypeTraits<ConstPtrType>;

protected:
  using SmallPtrSetImplBase::SmallPtrSetImplBase;

public:
  using iterator = SmallPtrSetIterator<PtrType>;
  using const_iterator = SmallPtrSetIterator<PtrType>;
  using key_type = ConstPtrType;
  using value_type = PtrType;

  SmallPtrSetImpl(const SmallPtrSetImpl &) = delete;

  /// Inserts Ptr if and only if there is no element in the container equal to
  /// Ptr. The bool component of the returned pair is true if and only if the
  /// insertion takes place, and the iterator component of the pair points to
  /// the element equal to Ptr.
  std::pair<iterator, bool> insert(PtrType Ptr) {
    auto p = insert_imp(PtrTraits::getAsVoidPointer(Ptr));
    return {makeIterator(p.first), p.second};
  }

  /// Insert the given pointer with an iterator hint that is ignored. This is
  /// identical to calling insert(Ptr), but allows SmallPtrSet to be used by
  /// std::insert_iterator and std::inserter().
  iterator insert(iterator, PtrType Ptr) { return insert(Ptr).first; }

  /// Remove pointer from the set.
  ///
  /// Returns whether the pointer was in the set. Invalidates iterators if
  /// true is returned. To remove elements while iterating over the set, use
  /// remove_if() instead.
  bool erase(PtrType Ptr) {
    return erase_imp(PtrTraits::getAsVoidPointer(Ptr));
  }

  /// Remove elements that match the given predicate.
  ///
  /// Returns whether anything was removed. The predicate must not access the
  /// set being modified.
  template <typename UnaryPredicate> bool remove_if(UnaryPredicate P) {
    bool Removed = false;
    if (isSmall()) {
      auto Buckets = small_buckets();
      const void **APtr = Buckets.begin(), **E = Buckets.end();
      while (APtr != E) {
        PtrType Ptr = PtrTraits::getFromVoidPointer(const_cast<void *>(*APtr));
        if (P(Ptr)) {
          *APtr = *--E;
          --NumEntries;
          incrementEpoch();
          Removed = true;
        } else {
          ++APtr;
        }
      }
      return Removed;
    }

    // Mark-then-rebuild: one pass to clear matches without sliding (which
    // would re-walk the cluster on every erase), then a single rehash to
    // restore the linear-probe invariant. O(N) total, vs O(N * cluster)
    // for repeated per-match Algorithm R erases.
    for (const void *&Bucket : buckets()) {
      if (Bucket == getEmptyMarker())
        continue;
      PtrType Ptr = PtrTraits::getFromVoidPointer(const_cast<void *>(Bucket));
      if (P(Ptr)) {
        Bucket = getEmptyMarker();
        --NumEntries;
        Removed = true;
      }
    }
    if (Removed) {
      incrementEpoch();
      Grow(CurArraySize);
    }
    return Removed;
  }

  /// count - Return 1 if the specified pointer is in the set, 0 otherwise.
  [[nodiscard]] size_type count(ConstPtrType Ptr) const {
    return contains_imp(ConstPtrTraits::getAsVoidPointer(Ptr));
  }
  [[nodiscard]] iterator find(ConstPtrType Ptr) const {
    return makeIterator(find_imp(ConstPtrTraits::getAsVoidPointer(Ptr)));
  }
  [[nodiscard]] bool contains(ConstPtrType Ptr) const {
    return contains_imp(ConstPtrTraits::getAsVoidPointer(Ptr));
  }

  template <typename IterT> void insert(IterT I, IterT E) {
    for (; I != E; ++I)
      insert(*I);
  }

  void insert(std::initializer_list<PtrType> IL) {
    insert(IL.begin(), IL.end());
  }

  [[nodiscard]] iterator begin() const {
    if constexpr (shouldReverseIterate())
      return makeIterator(EndPointer() - 1);
    else
      return makeIterator(CurArray);
  }
  [[nodiscard]] iterator end() const { return makeIterator(EndPointer()); }

private:
  iterator makeIterator(const void *const *P) const {
    if constexpr (shouldReverseIterate())
      return iterator(P == EndPointer() ? CurArray : P + 1, CurArray, *this);
    else
      return iterator(P, EndPointer(), *this);
  }
};

/// Equality comparison for SmallPtrSet.
template <typename PtrType>
[[nodiscard]] bool operator==(const SmallPtrSetImpl<PtrType> &LHS,
                              const SmallPtrSetImpl<PtrType> &RHS) {
  if (LHS.size() != RHS.size())
    return false;

  for (const auto *KV : LHS)
    if (!RHS.count(KV))
      return false;

  return true;
}

/// Inequality comparison for SmallPtrSet.
template <typename PtrType>
[[nodiscard]] bool operator!=(const SmallPtrSetImpl<PtrType> &LHS,
                              const SmallPtrSetImpl<PtrType> &RHS) {
  return !(LHS == RHS);
}

/// SmallPtrSet - This class implements a set which is optimized for holding
/// SmallSize or less elements.  This internally rounds up SmallSize to the next
/// power of two if it is not already a power of two.  See the comments above
/// SmallPtrSetImplBase for details of the algorithm.
template <class PtrType, unsigned SmallSize>
class SmallPtrSet : public SmallPtrSetImpl<PtrType> {
  static_assert(SmallSize <= 32, "SmallSize should be small");

  using BaseT = SmallPtrSetImpl<PtrType>;

  static constexpr size_t SmallSizePowTwo = llvm::bit_ceil_constexpr(SmallSize);
  /// SmallStorage - Fixed size storage used in 'small mode'.
  const void *SmallStorage[SmallSizePowTwo];

public:
  SmallPtrSet() : BaseT(SmallStorage, SmallSizePowTwo) {}
  SmallPtrSet(const SmallPtrSet &that) : BaseT(SmallStorage, that) {}
  SmallPtrSet(SmallPtrSet &&that)
      : BaseT(SmallStorage, SmallSizePowTwo, that.SmallStorage,
              std::move(that)) {}

  template <typename It>
  SmallPtrSet(It I, It E) : BaseT(SmallStorage, SmallSizePowTwo) {
    this->insert(I, E);
  }

  SmallPtrSet(std::initializer_list<PtrType> IL)
      : BaseT(SmallStorage, SmallSizePowTwo) {
    this->insert(IL.begin(), IL.end());
  }

  SmallPtrSet<PtrType, SmallSize> &
  operator=(const SmallPtrSet<PtrType, SmallSize> &RHS) {
    if (&RHS != this)
      this->copyFrom(SmallStorage, RHS);
    return *this;
  }

  SmallPtrSet<PtrType, SmallSize> &
  operator=(SmallPtrSet<PtrType, SmallSize> &&RHS) {
    if (&RHS != this)
      this->moveFrom(SmallStorage, SmallSizePowTwo, RHS.SmallStorage,
                     std::move(RHS));
    return *this;
  }

  SmallPtrSet<PtrType, SmallSize> &
  operator=(std::initializer_list<PtrType> IL) {
    this->clear();
    this->insert(IL.begin(), IL.end());
    return *this;
  }

  /// swap - Swaps the elements of two sets.
  void swap(SmallPtrSet<PtrType, SmallSize> &RHS) {
    SmallPtrSetImplBase::swap(SmallStorage, RHS.SmallStorage, RHS);
  }
};

} // end namespace llvm

namespace std {

/// Implement std::swap in terms of SmallPtrSet swap.
template <class T, unsigned N>
inline void swap(llvm::SmallPtrSet<T, N> &LHS, llvm::SmallPtrSet<T, N> &RHS) {
  LHS.swap(RHS);
}

} // end namespace std

extern "C" {
void csupport_sps_shrink_and_clear(const void ***cur_array,
                                   unsigned *cur_array_size,
                                   unsigned *num_entries, unsigned size);
void csupport_sps_grow(const void ***cur_array, unsigned *cur_array_size,
                       unsigned old_end_offset, int was_small,
                       unsigned new_size);
void csupport_sps_erase_from_bucket(const void **cur_array,
                                    unsigned cur_array_size,
                                    const void **bucket);
}

namespace llvm {

inline void SmallPtrSetImplBase::shrink_and_clear() {
  assert(!isSmall() && "Can't shrink a small set!");
  csupport_sps_shrink_and_clear(&CurArray, &CurArraySize, &NumEntries, size());
}

inline std::pair<const void *const *, bool>
SmallPtrSetImplBase::insert_imp_big(const void *Ptr) {
  if (LLVM_UNLIKELY(size() * 3 >= CurArraySize * 2)) {
    Grow(CurArraySize < 64 ? 128 : CurArraySize * 2);
  }

  const void **Bucket = const_cast<const void **>(FindBucketFor(Ptr));
  if (*Bucket == Ptr)
    return {Bucket, false};

  ++NumEntries;
  *Bucket = Ptr;
  incrementEpoch();
  return {Bucket, true};
}

inline const void *const *
SmallPtrSetImplBase::doFind(const void *Ptr) const {
  unsigned Mask = CurArraySize - 1;
  unsigned BucketNo = DenseMapInfo<void *>::getHashValue(Ptr) & Mask;
  while (true) {
    const void *const *Bucket = CurArray + BucketNo;
    if (LLVM_LIKELY(*Bucket == Ptr))
      return Bucket;
    if (LLVM_LIKELY(*Bucket == getEmptyMarker()))
      return nullptr;
    BucketNo = (BucketNo + 1) & Mask;
  }
}

inline const void *const *
SmallPtrSetImplBase::FindBucketFor(const void *Ptr) const {
  unsigned Mask = CurArraySize - 1;
  unsigned Bucket = DenseMapInfo<void *>::getHashValue(Ptr) & Mask;
  const void *const *Array = CurArray;
  while (true) {
    if (LLVM_LIKELY(Array[Bucket] == getEmptyMarker()))
      return Array + Bucket;
    if (LLVM_LIKELY(Array[Bucket] == Ptr))
      return Array + Bucket;
    Bucket = (Bucket + 1) & Mask;
  }
}

inline void SmallPtrSetImplBase::eraseFromBucket(const void **Bucket) {
  csupport_sps_erase_from_bucket(CurArray, CurArraySize, Bucket);
}

inline void SmallPtrSetImplBase::Grow(unsigned NewSize) {
  unsigned OldEndOffset = IsSmall ? NumEntries : CurArraySize;
  bool WasSmall = IsSmall;
  csupport_sps_grow(&CurArray, &CurArraySize, OldEndOffset, WasSmall, NewSize);
  IsSmall = false;
}

inline SmallPtrSetImplBase::SmallPtrSetImplBase(
    const void **SmallStorage, const SmallPtrSetImplBase &that) {
  IsSmall = that.isSmall();
  if (IsSmall) {
    CurArray = SmallStorage;
  } else {
    CurArray = (const void **)safe_malloc(sizeof(void *) * that.CurArraySize);
  }
  copyHelper(that);
}

inline SmallPtrSetImplBase::SmallPtrSetImplBase(const void **SmallStorage,
                                                unsigned SmallSize,
                                                const void **RHSSmallStorage,
                                                SmallPtrSetImplBase &&that) {
  moveHelper(SmallStorage, SmallSize, RHSSmallStorage, std::move(that));
}

inline void SmallPtrSetImplBase::copyFrom(const void **SmallStorage,
                                          const SmallPtrSetImplBase &RHS) {
  assert(&RHS != this && "Self-copy should be handled by the caller.");
  if (isSmall() && RHS.isSmall())
    assert(CurArraySize == RHS.CurArraySize &&
           "Cannot assign sets with different small sizes");

  if (RHS.isSmall()) {
    if (!isSmall())
      free(CurArray);
    CurArray = SmallStorage;
    IsSmall = true;
  } else if (CurArraySize != RHS.CurArraySize) {
    if (isSmall())
      CurArray = (const void **)safe_malloc(sizeof(void *) * RHS.CurArraySize);
    else
      CurArray = (const void **)safe_realloc(CurArray,
                                             sizeof(void *) * RHS.CurArraySize);
    IsSmall = false;
  }
  copyHelper(RHS);
}

inline void SmallPtrSetImplBase::copyHelper(const SmallPtrSetImplBase &RHS) {
  CurArraySize = RHS.CurArraySize;
  std::copy(RHS.buckets().begin(), RHS.buckets().end(), CurArray);
  NumEntries = RHS.NumEntries;
}

inline void SmallPtrSetImplBase::moveFrom(const void **SmallStorage,
                                          unsigned SmallSize,
                                          const void **RHSSmallStorage,
                                          SmallPtrSetImplBase &&RHS) {
  if (!isSmall())
    free(CurArray);
  moveHelper(SmallStorage, SmallSize, RHSSmallStorage, std::move(RHS));
}

inline void SmallPtrSetImplBase::moveHelper(const void **SmallStorage,
                                            unsigned SmallSize,
                                            const void **RHSSmallStorage,
                                            SmallPtrSetImplBase &&RHS) {
  assert(&RHS != this && "Self-move should be handled by the caller.");
  if (RHS.isSmall()) {
    CurArray = SmallStorage;
    std::copy(RHS.small_buckets().begin(), RHS.small_buckets().end(),
              CurArray);
  } else {
    CurArray = RHS.CurArray;
    RHS.CurArray = RHSSmallStorage;
  }
  CurArraySize = RHS.CurArraySize;
  NumEntries = RHS.NumEntries;
  IsSmall = RHS.IsSmall;

  RHS.CurArraySize = SmallSize;
  RHS.NumEntries = 0;
  RHS.IsSmall = true;
}

inline void SmallPtrSetImplBase::swap(const void **SmallStorage,
                                      const void **RHSSmallStorage,
                                      SmallPtrSetImplBase &RHS) {
  if (this == &RHS)
    return;

  if (!this->isSmall() && !RHS.isSmall()) {
    std::swap(this->CurArray, RHS.CurArray);
    std::swap(this->CurArraySize, RHS.CurArraySize);
    std::swap(this->NumEntries, RHS.NumEntries);
    return;
  }

  if (this->isSmall() && RHS.isSmall()) {
    unsigned MinEntries = std::min(this->NumEntries, RHS.NumEntries);
    std::swap_ranges(this->CurArray, this->CurArray + MinEntries, RHS.CurArray);
    if (this->NumEntries > MinEntries) {
      std::copy(this->CurArray + MinEntries,
                this->CurArray + this->NumEntries,
                RHS.CurArray + MinEntries);
    } else {
      std::copy(RHS.CurArray + MinEntries, RHS.CurArray + RHS.NumEntries,
                this->CurArray + MinEntries);
    }
    assert(this->CurArraySize == RHS.CurArraySize);
    std::swap(this->NumEntries, RHS.NumEntries);
    return;
  }

  SmallPtrSetImplBase &SmallSide = this->isSmall() ? *this : RHS;
  SmallPtrSetImplBase &LargeSide = this->isSmall() ? RHS : *this;
  const void **LargeSideInlineStorage =
      this->isSmall() ? RHSSmallStorage : SmallStorage;
  std::copy(SmallSide.small_buckets().begin(), SmallSide.small_buckets().end(),
            LargeSideInlineStorage);
  std::swap(LargeSide.CurArraySize, SmallSide.CurArraySize);
  std::swap(LargeSide.NumEntries, SmallSide.NumEntries);
  SmallSide.CurArray = LargeSide.CurArray;
  SmallSide.IsSmall = false;
  LargeSide.CurArray = LargeSideInlineStorage;
  LargeSide.IsSmall = true;
}

} // end namespace llvm

#endif // LLVM_ADT_SMALLPTRSET_H
