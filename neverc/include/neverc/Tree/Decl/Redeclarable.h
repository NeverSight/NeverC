#ifndef NEVERC_TREE_REDECLARABLE_H
#define NEVERC_TREE_REDECLARABLE_H

#include "neverc/Tree/Core/ExternalTreeSource.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Casting.h"
#include <cassert>
#include <cstddef>
#include <iterator>

namespace neverc {

class TreeContext;
class Decl;

// Some notes on redeclarables:
//
//  - Every redeclarable is on a circular linked list.
//
//  - Every decl has a pointer to the first element of the chain _and_ a
//    DeclLink that may point to one of 3 possible states:
//      - the "previous" (temporal) element in the chain
//      - the "latest" (temporal) element in the chain
//      - the "uninitialized-latest" value (when newly-constructed)
//
//  - The first element is also often called the canonical element. Every
//    element has a pointer to it so that "getCanonical" can be fast.
//
//  - Most links in the chain point to previous, except the link out of
//    the first; it points to latest.
//
//  - Elements are called "first", "previous", "latest" or
//    "most-recent" when referring to temporal order: order of addition
//    to the chain.
//
//  - It's easiest to just ignore the implementation of DeclLink when making
//    sense of the redeclaration chain.
//
//  - There's also a "definition" link for several types of
//    redeclarable, where only one definition should exist at any given
//    time (and the defn pointer is stored in the decl's "data" which
//    is copied to every element on the chain when it's changed).
//
//    Here is some ASCII art:
//
//      "first"                                     "latest"
//      "canonical"                                 "most recent"
//      +------------+         first                +--------------+
//      |            | <--------------------------- |              |
//      |            |                              |              |
//      |            |                              |              |
//      |            |       +--------------+       |              |
//      |            | first |              |       |              |
//      |            | <---- |              |       |              |
//      |            |       |              |       |              |
//      | Decl A     |  link | Decl A'      |  link | Decl A''     |
//      | seen first | <---- | seen second  | <---- | seen third   |
//      |            |       |              |       |              |
//      +------------+       +--------------+       +--------------+
//      | data       | defn  | data         |  defn | data         |
//      |            | ----> |              | <---- |              |
//      +------------+       +--------------+       +--------------+
//        |                     |     ^                  ^
//        |                     |defn |                  |
//        | link                +-----+                  |
//        +-->-------------------------------------------+

template <typename decl_type> class Redeclarable {
protected:
  class DeclLink {
    using KnownLatest = LatestDeclPtr<const Decl *, Decl *>;

    /// We store a pointer to the TreeContext in the UninitializedLatest
    /// pointer, but to avoid circular type dependencies when we steal the low
    /// bits of this pointer, we use a raw void* here.
    using UninitializedLatest = const void *;

    using Previous = Decl *;

    /// A pointer to either an uninitialized latest declaration (where either
    /// we've not yet set the previous decl or there isn't one), or to a known
    /// previous declaration.
    using NotKnownLatest = llvm::PointerUnion<Previous, UninitializedLatest>;

    mutable llvm::PointerUnion<NotKnownLatest, KnownLatest> Link;

  public:
    enum PreviousTag { PreviousLink };
    enum LatestTag { LatestLink };

    DeclLink(LatestTag, const TreeContext &Ctx)
        : Link(NotKnownLatest(reinterpret_cast<UninitializedLatest>(&Ctx))) {}
    DeclLink(PreviousTag, decl_type *D) : Link(NotKnownLatest(Previous(D))) {}

    bool isFirst() const {
      return Link.is<KnownLatest>() ||
             Link.get<NotKnownLatest>().template is<UninitializedLatest>();
    }

    decl_type *getPrevious(const decl_type *D) const {
      if (Link.is<NotKnownLatest>()) {
        NotKnownLatest NKL = Link.get<NotKnownLatest>();
        if (NKL.is<Previous>())
          return static_cast<decl_type *>(NKL.get<Previous>());

        // Allocate the generational 'most recent' cache now, if needed.
        Link = KnownLatest(*reinterpret_cast<const TreeContext *>(
                               NKL.get<UninitializedLatest>()),
                           const_cast<decl_type *>(D));
      }

      return static_cast<decl_type *>(Link.get<KnownLatest>().get(D));
    }

    void setPrevious(decl_type *D) {
      assert(!isFirst() && "decl became non-canonical unexpectedly");
      Link = Previous(D);
    }

    void setLatest(decl_type *D) {
      assert(isFirst() && "decl became canonical unexpectedly");
      if (Link.is<NotKnownLatest>()) {
        NotKnownLatest NKL = Link.get<NotKnownLatest>();
        Link = KnownLatest(*reinterpret_cast<const TreeContext *>(
                               NKL.get<UninitializedLatest>()),
                           D);
      } else {
        auto Latest = Link.get<KnownLatest>();
        Latest.set(D);
        Link = Latest;
      }
    }

    void markIncomplete() { Link.get<KnownLatest>().markIncomplete(); }

    Decl *getLatestNotUpdated() const {
      assert(isFirst() && "expected a canonical decl");
      if (Link.is<NotKnownLatest>())
        return nullptr;
      return Link.get<KnownLatest>().getNotUpdated();
    }
  };

  static DeclLink PreviousDeclLink(decl_type *D) {
    return DeclLink(DeclLink::PreviousLink, D);
  }

  static DeclLink LatestDeclLink(const TreeContext &Ctx) {
    return DeclLink(DeclLink::LatestLink, Ctx);
  }

  DeclLink RedeclLink;

  decl_type *First;

  decl_type *getNextRedeclaration() const {
    return RedeclLink.getPrevious(static_cast<const decl_type *>(this));
  }

public:
  Redeclarable(const TreeContext &Ctx)
      : RedeclLink(LatestDeclLink(Ctx)), First(static_cast<decl_type *>(this)) {
  }

  decl_type *getPreviousDecl() {
    if (!RedeclLink.isFirst())
      return getNextRedeclaration();
    return nullptr;
  }
  const decl_type *getPreviousDecl() const {
    return const_cast<decl_type *>(static_cast<const decl_type *>(this))
        ->getPreviousDecl();
  }

  decl_type *getFirstDecl() { return First; }

  const decl_type *getFirstDecl() const { return First; }

  bool isFirstDecl() const { return RedeclLink.isFirst(); }

  decl_type *getMostRecentDecl() {
    return getFirstDecl()->getNextRedeclaration();
  }

  const decl_type *getMostRecentDecl() const {
    return getFirstDecl()->getNextRedeclaration();
  }

  void setPreviousDecl(decl_type *PrevDecl);

  class redecl_iterator {
    /// Current - The current declaration.
    decl_type *Current = nullptr;
    decl_type *Starter = nullptr;
    bool PassedFirst = false;

  public:
    using value_type = decl_type *;
    using reference = decl_type *;
    using pointer = decl_type *;
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;

    redecl_iterator() = default;
    explicit redecl_iterator(decl_type *C) : Current(C), Starter(C) {}

    reference operator*() const { return Current; }
    pointer operator->() const { return Current; }

    redecl_iterator &operator++() {
      assert(Current && "Advancing while iterator has reached end");
      // Make sure we don't infinitely loop on an invalid redecl chain. This
      // should never happen.
      if (Current->isFirstDecl()) {
        if (PassedFirst) {
          assert(0 && "Passed first decl twice, invalid redecl chain!");
          Current = nullptr;
          return *this;
        }
        PassedFirst = true;
      }

      // Get either previous decl or latest decl.
      decl_type *Next = Current->getNextRedeclaration();
      Current = (Next != Starter) ? Next : nullptr;
      return *this;
    }

    redecl_iterator operator++(int) {
      redecl_iterator tmp(*this);
      ++(*this);
      return tmp;
    }

    friend bool operator==(redecl_iterator x, redecl_iterator y) {
      return x.Current == y.Current;
    }
    friend bool operator!=(redecl_iterator x, redecl_iterator y) {
      return x.Current != y.Current;
    }
  };

  using redecl_range = llvm::iterator_range<redecl_iterator>;

  redecl_range redecls() const {
    return redecl_range(redecl_iterator(const_cast<decl_type *>(
                            static_cast<const decl_type *>(this))),
                        redecl_iterator());
  }

  redecl_iterator redecls_begin() const { return redecls().begin(); }
  redecl_iterator redecls_end() const { return redecls().end(); }
};

template <typename decl_type> class Mergeable {
public:
  Mergeable() = default;

  decl_type *getFirstDecl() { return static_cast<decl_type *>(this); }

  const decl_type *getFirstDecl() const {
    return static_cast<const decl_type *>(this);
  }

  bool isFirstDecl() const { return getFirstDecl() == this; }
};

template <typename decl_type> class CanonicalDeclPtr {
public:
  CanonicalDeclPtr() = default;
  CanonicalDeclPtr(decl_type *Ptr)
      : Ptr(Ptr ? Ptr->getCanonicalDecl() : nullptr) {}
  CanonicalDeclPtr(const CanonicalDeclPtr &) = default;
  CanonicalDeclPtr &operator=(const CanonicalDeclPtr &) = default;

  operator decl_type *() { return Ptr; }
  operator const decl_type *() const { return Ptr; }

  decl_type *operator->() { return Ptr; }
  const decl_type *operator->() const { return Ptr; }

  decl_type &operator*() { return *Ptr; }
  const decl_type &operator*() const { return *Ptr; }

  friend bool operator==(CanonicalDeclPtr LHS, CanonicalDeclPtr RHS) {
    return LHS.Ptr == RHS.Ptr;
  }
  friend bool operator!=(CanonicalDeclPtr LHS, CanonicalDeclPtr RHS) {
    return LHS.Ptr != RHS.Ptr;
  }

private:
  friend struct llvm::DenseMapInfo<CanonicalDeclPtr<decl_type>>;
  friend struct llvm::PointerLikeTypeTraits<CanonicalDeclPtr<decl_type>>;

  decl_type *Ptr = nullptr;
};

} // namespace neverc

namespace llvm {

template <typename decl_type>
struct DenseMapInfo<neverc::CanonicalDeclPtr<decl_type>> {
  using CanonicalDeclPtr = neverc::CanonicalDeclPtr<decl_type>;
  using BaseInfo = DenseMapInfo<decl_type *>;

  static CanonicalDeclPtr getEmptyKey() {
    // Construct our CanonicalDeclPtr this way because the regular constructor
    // would dereference P.Ptr, which is not allowed.
    CanonicalDeclPtr P;
    P.Ptr = BaseInfo::getEmptyKey();
    return P;
  }

  static CanonicalDeclPtr getTombstoneKey() {
    CanonicalDeclPtr P;
    P.Ptr = BaseInfo::getTombstoneKey();
    return P;
  }

  static unsigned getHashValue(const CanonicalDeclPtr &P) {
    return BaseInfo::getHashValue(P);
  }

  static bool isEqual(const CanonicalDeclPtr &LHS,
                      const CanonicalDeclPtr &RHS) {
    return BaseInfo::isEqual(LHS, RHS);
  }
};

template <typename decl_type>
struct PointerLikeTypeTraits<neverc::CanonicalDeclPtr<decl_type>> {
  static inline void *getAsVoidPointer(neverc::CanonicalDeclPtr<decl_type> P) {
    return P.Ptr;
  }
  static inline neverc::CanonicalDeclPtr<decl_type>
  getFromVoidPointer(void *P) {
    neverc::CanonicalDeclPtr<decl_type> C;
    C.Ptr = PointerLikeTypeTraits<decl_type *>::getFromVoidPtr(P);
    return C;
  }
  static constexpr int NumLowBitsAvailable =
      PointerLikeTypeTraits<decl_type *>::NumLowBitsAvailable;
};

} // namespace llvm

#endif // NEVERC_TREE_REDECLARABLE_H
