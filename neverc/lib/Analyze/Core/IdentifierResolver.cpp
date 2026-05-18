#include "neverc/Analyze/IdentifierResolver.h"
#include "neverc/Analyze/Scope.h"
#include "neverc/Foundation/Core/IdentifierTable.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Decl/DeclarationName.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstdint>

using namespace neverc;

// ===----------------------------------------------------------------------===
// Internal helpers
// ===----------------------------------------------------------------------===

namespace {

inline uint64_t contextBit(const DeclContext *DC) {
  auto P =
      reinterpret_cast<uintptr_t>(DC->getRedeclContext()->getPrimaryContext());
  return 1ULL << ((P >> 4) & 63);
}

} // namespace

// ===----------------------------------------------------------------------===
// IdDeclInfoMap: pooled allocator for IdDeclInfo entries
// ===----------------------------------------------------------------------===

class IdentifierResolver::IdDeclInfoMap {
  static constexpr unsigned POOL_SIZE = 1024;
  static constexpr unsigned PREFETCH_DISTANCE = 2;

  struct alignas(64) IdDeclInfoPool {
    IdDeclInfoPool *Next;
    IdDeclInfo Pool[POOL_SIZE];

    IdDeclInfoPool(IdDeclInfoPool *Next) : Next(Next) {}
  };

  IdDeclInfoPool *CurPool = nullptr;
  unsigned CurIndex = POOL_SIZE;
  unsigned PoolCount = 0;

public:
  IdDeclInfoMap() = default;

  ~IdDeclInfoMap() {
    IdDeclInfoPool *Cur = CurPool;
    while (IdDeclInfoPool *P = Cur) {
      Cur = Cur->Next;
      delete P;
    }
  }

  IdDeclInfoMap(const IdDeclInfoMap &) = delete;
  IdDeclInfoMap &operator=(const IdDeclInfoMap &) = delete;

  LLVM_ATTRIBUTE_ALWAYS_INLINE
  IdDeclInfo &operator[](DeclarationName Name);
};

// ===----------------------------------------------------------------------===
// IdDeclInfo: per-name decl chain
// ===----------------------------------------------------------------------===

void IdentifierResolver::IdDeclInfo::AddDecl(NamedDecl *D) {
  Decls.push_back(D);
  ContextBitmap |= contextBit(D->getDeclContext());
}

void IdentifierResolver::IdDeclInfo::InsertDecl(DeclsTy::iterator Pos,
                                                NamedDecl *D) {
  Decls.insert(Pos, D);
  ContextBitmap |= contextBit(D->getDeclContext());
}

bool IdentifierResolver::IdDeclInfo::mayHaveDeclInContext(
    const DeclContext *Ctx) const {
  return ContextBitmap & contextBit(Ctx);
}

void IdentifierResolver::IdDeclInfo::RemoveDecl(NamedDecl *D) {
  const auto Sz = Decls.size();
  if (LLVM_LIKELY(Sz > 0 && Decls.back() == D)) {
    Decls.pop_back();
    return;
  }
  for (auto I = Sz; I != 0; --I) {
    if (Decls[I - 1] == D) {
      Decls.erase(Decls.begin() + (I - 1));
      return;
    }
  }

  llvm_unreachable("Didn't find this decl on its identifier's chain!");
}

// ===----------------------------------------------------------------------===
// IdentifierResolver: construction & scope queries
// ===----------------------------------------------------------------------===

IdentifierResolver::IdentifierResolver(PrepEngine &PP)
    : LangOpt(PP.getLangOpts()), PP(PP), IdDeclInfos(new IdDeclInfoMap) {}

IdentifierResolver::~IdentifierResolver() { delete IdDeclInfos; }

bool IdentifierResolver::isDeclInScope(Decl *D, DeclContext *Ctx,
                                       Scope *S) const {
  Ctx = Ctx->getRedeclContext();
  if (Ctx->isFunctionOrMethod() || (S && S->isFunctionPrototypeScope())) {
    // Ignore the scopes associated within transparent declaration contexts.
    while (S->getEntity() && (S->getEntity()->isTransparentContext() ||
                              isa<RecordDecl>(S->getEntity())))
      S = S->getParent();

    if (S->isDeclScope(D))
      return true;
    return false;
  }

  // If D is a local extern declaration, this check doesn't make sense;
  // we should be checking its lexical context instead in that case, because
  // that is its scope.
  DeclContext *DCtx = D->getDeclContext()->getRedeclContext();
  return Ctx->Equals(DCtx);
}

// ===----------------------------------------------------------------------===
// Decl insertion / removal
// ===----------------------------------------------------------------------===

void IdentifierResolver::AddDecl(NamedDecl *D) {
  DeclarationName Name = D->getDeclName();

  void *Ptr = Name.getFETokenInfo();

  if (!Ptr) {
    Name.setFETokenInfo(D);
    return;
  }

  IdDeclInfo *IDI;

  if (isDeclPtr(Ptr)) {
    Name.setFETokenInfo(nullptr);
    IDI = &(*IdDeclInfos)[Name];
    NamedDecl *PrevD = static_cast<NamedDecl *>(Ptr);
    IDI->AddDecl(PrevD);
  } else
    IDI = toIdDeclInfo(Ptr);

  IDI->AddDecl(D);
}

void IdentifierResolver::InsertDeclAfter(iterator Pos, NamedDecl *D) {
  DeclarationName Name = D->getDeclName();

  void *Ptr = Name.getFETokenInfo();

  if (!Ptr) {
    AddDecl(D);
    return;
  }

  if (isDeclPtr(Ptr)) {
    // We only have a single declaration: insert before or after it,
    // as appropriate.
    if (Pos == iterator()) {
      // Add the new declaration before the existing declaration.
      NamedDecl *PrevD = static_cast<NamedDecl *>(Ptr);
      RemoveDecl(PrevD);
      AddDecl(D);
      AddDecl(PrevD);
    } else {
      // Add new declaration after the existing declaration.
      AddDecl(D);
    }

    return;
  }

  // General case: insert the declaration at the appropriate point in the
  // list, which already has at least two elements.
  IdDeclInfo *IDI = toIdDeclInfo(Ptr);
  if (Pos.isIterator()) {
    IDI->InsertDecl(Pos.getIterator() + 1, D);
  } else
    IDI->InsertDecl(IDI->decls_begin(), D);
}

void IdentifierResolver::RemoveDecl(NamedDecl *D) {
  assert(D && "null param passed");
  DeclarationName Name = D->getDeclName();

  void *Ptr = Name.getFETokenInfo();
  assert(Ptr && "Didn't find this decl on its identifier's chain!");

  if (LLVM_LIKELY(isDeclPtr(Ptr))) {
    assert(D == Ptr && "Didn't find this decl on its identifier's chain!");
    Name.setFETokenInfo(nullptr);
    return;
  }

  toIdDeclInfo(Ptr)->RemoveDecl(D);
}

// ===----------------------------------------------------------------------===
// Decl iteration
// ===----------------------------------------------------------------------===

llvm::iterator_range<IdentifierResolver::iterator>
IdentifierResolver::decls(DeclarationName Name) {
  return {begin(Name), end()};
}

IdentifierResolver::iterator IdentifierResolver::begin(DeclarationName Name) {
  void *Ptr = Name.getFETokenInfo();
  if (!Ptr)
    return end();

  if (isDeclPtr(Ptr))
    return iterator(static_cast<NamedDecl *>(Ptr));

  IdDeclInfo *IDI = toIdDeclInfo(Ptr);

  IdDeclInfo::DeclsTy::iterator I = IDI->decls_end();
  if (I != IDI->decls_begin())
    return iterator(I - 1);
  // No decls found.
  return end();
}

// ===----------------------------------------------------------------------===
// Top-level decl matching
// ===----------------------------------------------------------------------===

namespace {

enum DeclMatchKind { DMK_Different, DMK_Replace, DMK_Ignore };

DeclMatchKind compareDeclarations(NamedDecl *Existing, NamedDecl *New) {
  // If the declarations are identical, ignore the new one.
  if (Existing == New)
    return DMK_Ignore;

  // If the declarations have different kinds, they're obviously different.
  if (Existing->getKind() != New->getKind())
    return DMK_Different;

  // If the declarations are redeclarations of each other, keep the newest one.
  if (Existing->getCanonicalDecl() == New->getCanonicalDecl()) {
    // If either of these is the most recent declaration, use it.
    Decl *MostRecent = Existing->getMostRecentDecl();
    if (Existing == MostRecent)
      return DMK_Ignore;

    if (New == MostRecent)
      return DMK_Replace;

    // If the existing declaration is somewhere in the previous declaration
    // chain of the new declaration, then prefer the new declaration.
    for (auto *RD : New->redecls()) {
      if (RD == Existing)
        return DMK_Replace;

      if (RD->isCanonicalDecl())
        break;
    }

    return DMK_Ignore;
  }

  return DMK_Different;
}
} // namespace

bool IdentifierResolver::tryAddTopLevelDecl(NamedDecl *D,
                                            DeclarationName Name) {
  void *Ptr = Name.getFETokenInfo();

  if (!Ptr) {
    Name.setFETokenInfo(D);
    return true;
  }

  IdDeclInfo *IDI;

  if (isDeclPtr(Ptr)) {
    NamedDecl *PrevD = static_cast<NamedDecl *>(Ptr);

    switch (compareDeclarations(PrevD, D)) {
    case DMK_Different:
      break;

    case DMK_Ignore:
      return false;

    case DMK_Replace:
      Name.setFETokenInfo(D);
      return true;
    }

    Name.setFETokenInfo(nullptr);
    IDI = &(*IdDeclInfos)[Name];

    // If the existing declaration is not visible in translation unit scope,
    // then add the new top-level declaration first.
    if (!PrevD->getDeclContext()->getRedeclContext()->isTranslationUnit()) {
      IDI->AddDecl(D);
      IDI->AddDecl(PrevD);
    } else {
      IDI->AddDecl(PrevD);
      IDI->AddDecl(D);
    }
    return true;
  }

  IDI = toIdDeclInfo(Ptr);

  // See whether this declaration is identical to any existing declarations.
  // If not, find the right place to insert it.
  for (IdDeclInfo::DeclsTy::iterator I = IDI->decls_begin(),
                                     IEnd = IDI->decls_end();
       I != IEnd; ++I) {

    switch (compareDeclarations(*I, D)) {
    case DMK_Different:
      break;

    case DMK_Ignore:
      return false;

    case DMK_Replace:
      *I = D;
      return true;
    }

    if (!(*I)->getDeclContext()->getRedeclContext()->isTranslationUnit()) {
      // We've found a declaration that is not visible from the translation
      // unit (it's in an inner scope). Insert our declaration here.
      IDI->InsertDecl(I, D);
      return true;
    }
  }

  // Add the declaration to the end.
  IDI->AddDecl(D);
  return true;
}

// ===----------------------------------------------------------------------===
// IdDeclInfoMap allocation
// ===----------------------------------------------------------------------===

IdentifierResolver::IdDeclInfo &
IdentifierResolver::IdDeclInfoMap::operator[](DeclarationName Name) {
  void *Ptr = Name.getFETokenInfo();

  if (LLVM_LIKELY(Ptr != nullptr))
    return *toIdDeclInfo(Ptr);

  if (LLVM_UNLIKELY(CurIndex == POOL_SIZE)) {
    CurPool = new IdDeclInfoPool(CurPool);
    CurIndex = 0;
    ++PoolCount;
  }
  IdDeclInfo *IDI = &CurPool->Pool[CurIndex];
  if (LLVM_LIKELY(CurIndex + PREFETCH_DISTANCE < POOL_SIZE))
    __builtin_prefetch(&CurPool->Pool[CurIndex + PREFETCH_DISTANCE], 1, 1);
  Name.setFETokenInfo(
      reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(IDI) | 0x1));
  ++CurIndex;
  return *IDI;
}

// ===----------------------------------------------------------------------===
// Iterator implementation
// ===----------------------------------------------------------------------===

void IdentifierResolver::iterator::incrementSlowCase() {
  NamedDecl *D = **this;
  void *InfoPtr = D->getDeclName().getFETokenInfo();
  assert(!isDeclPtr(InfoPtr) && "Decl with wrong id ?");
  IdDeclInfo *Info = toIdDeclInfo(InfoPtr);

  BaseIter I = getIterator();
  if (I != Info->decls_begin())
    *this = iterator(I - 1);
  else // No more decls.
    *this = iterator();
}

// ===----------------------------------------------------------------------===
// Context-bitmap fast path
// ===----------------------------------------------------------------------===

bool IdentifierResolver::mayHaveDeclsInContext(DeclarationName Name,
                                               DeclContext *Ctx) {
  void *Ptr = Name.getFETokenInfo();
  if (!Ptr)
    return false;
  if (isDeclPtr(Ptr))
    return true;
  return toIdDeclInfo(Ptr)->mayHaveDeclInContext(Ctx);
}
