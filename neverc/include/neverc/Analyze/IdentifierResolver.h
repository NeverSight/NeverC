#ifndef NEVERC_ANALYZE_IDENTIFIERRESOLVER_H
#define NEVERC_ANALYZE_IDENTIFIERRESOLVER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace neverc {

class Decl;
class DeclarationName;
class DeclContext;
class LangOptions;
class NamedDecl;
class PrepEngine;
class Scope;

class IdentifierResolver {
  class IdDeclInfo {
  public:
    using DeclsTy = llvm::SmallVector<NamedDecl *, 2>;

    DeclsTy::iterator decls_begin() { return Decls.begin(); }
    DeclsTy::iterator decls_end() { return Decls.end(); }

    void AddDecl(NamedDecl *D);
    void RemoveDecl(NamedDecl *D);
    void InsertDecl(DeclsTy::iterator Pos, NamedDecl *D);

    /// Bloom filter: false means definitely no decl whose redecl-context
    /// primary matches Ctx; true means maybe (check the chain).
    bool mayHaveDeclInContext(const DeclContext *Ctx) const;

  private:
    DeclsTy Decls;
    uint64_t ContextBitmap = 0;
  };

public:
  class iterator {
  public:
    friend class IdentifierResolver;

    using value_type = NamedDecl *;
    using reference = NamedDecl *;
    using pointer = NamedDecl *;
    using iterator_category = std::input_iterator_tag;
    using difference_type = std::ptrdiff_t;

    /// Ptr - There are 2 forms that 'Ptr' represents:
    /// 1) A single NamedDecl. (Ptr & 0x1 == 0)
    /// 2) A IdDeclInfo::DeclsTy::iterator that traverses only the decls of the
    ///    same declaration context. (Ptr & 0x1 == 0x1)
    uintptr_t Ptr = 0;
    using BaseIter = IdDeclInfo::DeclsTy::iterator;

    /// A single NamedDecl. (Ptr & 0x1 == 0)
    iterator(NamedDecl *D) {
      Ptr = reinterpret_cast<uintptr_t>(D);
      assert((Ptr & 0x1) == 0 && "Invalid Ptr!");
    }

    /// A IdDeclInfo::DeclsTy::iterator that walks or not the parent declaration
    /// contexts depending on 'LookInParentCtx'.
    iterator(BaseIter I) { Ptr = reinterpret_cast<uintptr_t>(I) | 0x1; }

    bool isIterator() const { return (Ptr & 0x1); }

    BaseIter getIterator() const {
      assert(isIterator() && "Ptr not an iterator!");
      return reinterpret_cast<BaseIter>(Ptr & ~0x1);
    }

    void incrementSlowCase();

  public:
    iterator() = default;

    NamedDecl *operator*() const {
      if (isIterator())
        return *getIterator();
      else
        return reinterpret_cast<NamedDecl *>(Ptr);
    }

    bool operator==(const iterator &RHS) const { return Ptr == RHS.Ptr; }
    bool operator!=(const iterator &RHS) const { return Ptr != RHS.Ptr; }

    // Preincrement.
    iterator &operator++() {
      if (!isIterator()) // common case.
        Ptr = 0;
      else
        incrementSlowCase();
      return *this;
    }
  };

  explicit IdentifierResolver(PrepEngine &PP);
  ~IdentifierResolver();

  IdentifierResolver(const IdentifierResolver &) = delete;
  IdentifierResolver &operator=(const IdentifierResolver &) = delete;

  llvm::iterator_range<iterator> decls(DeclarationName Name);

  iterator begin(DeclarationName Name);

  iterator end() { return iterator(); }

  bool isDeclInScope(Decl *D, DeclContext *Ctx, Scope *S = nullptr) const;

  void AddDecl(NamedDecl *D);

  void RemoveDecl(NamedDecl *D);

  void InsertDeclAfter(iterator Pos, NamedDecl *D);

  bool tryAddTopLevelDecl(NamedDecl *D, DeclarationName Name);

  /// Bloom-filter pre-check: false means no decl for Name could be in Ctx.
  bool mayHaveDeclsInContext(DeclarationName Name, DeclContext *Ctx);

private:
  const LangOptions &LangOpt;
  PrepEngine &PP;

  class IdDeclInfoMap;
  IdDeclInfoMap *IdDeclInfos;

  static inline bool isDeclPtr(void *Ptr) {
    return (reinterpret_cast<uintptr_t>(Ptr) & 0x1) == 0;
  }

  static inline IdDeclInfo *toIdDeclInfo(void *Ptr) {
    assert((reinterpret_cast<uintptr_t>(Ptr) & 0x1) == 1 &&
           "Ptr not a IdDeclInfo* !");
    return reinterpret_cast<IdDeclInfo *>(reinterpret_cast<uintptr_t>(Ptr) &
                                          ~0x1);
  }
};

} // namespace neverc

#endif // NEVERC_ANALYZE_IDENTIFIERRESOLVER_H
