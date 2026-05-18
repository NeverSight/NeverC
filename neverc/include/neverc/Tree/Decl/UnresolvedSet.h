#ifndef NEVERC_AST_UNRESOLVEDSET_H
#define NEVERC_AST_UNRESOLVEDSET_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include <cstddef>
#include <iterator>

namespace neverc {

class NamedDecl;

class UnresolvedSetImpl {
  using DeclsTy = llvm::SmallVectorImpl<NamedDecl *>;

private:
  template <unsigned N> friend class UnresolvedSet;

  UnresolvedSetImpl() = default;
  UnresolvedSetImpl(const UnresolvedSetImpl &) = default;
  UnresolvedSetImpl &operator=(const UnresolvedSetImpl &) = default;

  UnresolvedSetImpl(UnresolvedSetImpl &&) {}
  UnresolvedSetImpl &operator=(UnresolvedSetImpl &&) { return *this; }

public:
  using iterator = NamedDecl **;
  using const_iterator = NamedDecl *const *;

  iterator begin() { return decls().begin(); }
  iterator end() { return decls().end(); }

  const_iterator begin() const { return decls().begin(); }
  const_iterator end() const { return decls().end(); }

  void addDecl(NamedDecl *D) { decls().push_back(D); }

  bool replace(const NamedDecl *Old, NamedDecl *New) {
    for (auto I = decls().begin(), E = decls().end(); I != E; ++I)
      if (*I == Old) {
        *I = New;
        return true;
      }
    return false;
  }

  void replace(iterator I, NamedDecl *New) { *I = New; }

  void erase(unsigned I) {
    auto val = decls().pop_back_val();
    if (I < size())
      decls()[I] = val;
  }

  void erase(iterator I) {
    auto val = decls().pop_back_val();
    if (I != end())
      *I = val;
  }

  void clear() { decls().clear(); }
  void truncate(unsigned N) { decls().truncate(N); }

  bool empty() const { return decls().empty(); }
  unsigned size() const { return decls().size(); }

  void append(iterator I, iterator E) { decls().append(I, E); }

  template <typename Iter> void assign(Iter I, Iter E) { decls().assign(I, E); }

  NamedDecl *&operator[](unsigned I) { return decls()[I]; }
  NamedDecl *const &operator[](unsigned I) const { return decls()[I]; }

private:
  DeclsTy &decls() { return *reinterpret_cast<DeclsTy *>(this); }
  const DeclsTy &decls() const {
    return *reinterpret_cast<const DeclsTy *>(this);
  }
};

template <unsigned InlineCapacity>
class UnresolvedSet : public UnresolvedSetImpl {
  llvm::SmallVector<NamedDecl *, InlineCapacity> Decls;
};

} // namespace neverc

#endif // NEVERC_AST_UNRESOLVEDSET_H
