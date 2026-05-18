#include "neverc/Analyze/DeclSpec.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Core/FileManager.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Scan/IncludeResolver.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Tree/Decl/DeclLookups.h"
#include "neverc/Tree/Type/TypeOrdering.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/Support/Casting.h"
#include <list>
#include <optional>
#include <utility>

using namespace neverc;
using namespace sema;

// ===----------------------------------------------------------------------===
// Name lookup
// ===----------------------------------------------------------------------===

LookupResult::~LookupResult() {
  if (DiagnoseAmbiguous)
    diagnoseAmbiguous();
}

void LookupResult::diagnoseAmbiguous() {
  if (isAmbiguous())
    getSema().DiagnoseAmbiguousLookup(*this);
}

void TypoCorrection::setCorrectionRange(const DeclarationNameInfo &TypoName) {
  CorrectionRange = TypoName.getSourceRange();
}

namespace {
LLVM_ATTRIBUTE_ALWAYS_INLINE
unsigned getIDNS(neverc::ResolveNameKind NameKind, bool Redeclaration) {
  static constexpr unsigned Table[] = {
      Decl::IDNS_Ordinary,
      Decl::IDNS_Tag,
      Decl::IDNS_Label,
      Decl::IDNS_Member,
      Decl::IDNS_Ordinary,
      Decl::IDNS_Ordinary | Decl::IDNS_Tag | Decl::IDNS_Member |
          Decl::IDNS_Type,
  };
  static constexpr unsigned TableWithLocal[] = {
      Decl::IDNS_Ordinary | Decl::IDNS_LocalExtern,
      Decl::IDNS_Tag,
      Decl::IDNS_Label,
      Decl::IDNS_Member,
      Decl::IDNS_Ordinary | Decl::IDNS_LocalExtern,
      Decl::IDNS_Ordinary | Decl::IDNS_Tag | Decl::IDNS_Member |
          Decl::IDNS_Type,
  };
  if (LLVM_UNLIKELY(Redeclaration &&
                    (NameKind == neverc::ResolveOrdinary ||
                     NameKind == neverc::ResolveRedeclWithLinkage)))
    return TableWithLocal[NameKind];
  return Table[NameKind];
}
} // namespace

void LookupResult::configure() {
  IDNS = getIDNS(LookupKind, isForRedeclaration());

  if (IdentifierInfo *Id = NameInfo.getName().getAsIdentifierInfo()) {
    if (unsigned BuiltinID = Id->getBuiltinID()) {
      if (!getSema().Context.BuiltinInfo.isPredefinedLibFunction(BuiltinID))
        AllowHidden = true;
    }
  }
}

bool LookupResult::checkDebugAssumptions() const {
  // This function is never called by NDEBUG builds.
  assert(ResultKind != NotFound || Decls.size() == 0);
  assert(ResultKind != Found || Decls.size() == 1);
  assert(ResultKind != FoundOverloaded || Decls.size() > 1);
  assert(ResultKind != Ambiguous || Decls.size() > 1);
  return true;
}

namespace {
const DeclContext *getScopeMatchContext(const Decl *D) {
  if (const DeclContext *DC = D->getLexicalDeclContext();
      DC->isFunctionOrMethod())
    return DC;
  return D->getDeclContext()->getRedeclContext();
}

bool isPreferredLookupResult(Sema &S, neverc::ResolveNameKind Kind,
                             const NamedDecl *D, const NamedDecl *Existing) {
  const auto *DUnderlying = D->getUnderlyingDecl();
  const auto *EUnderlying = Existing->getUnderlyingDecl();

  if (DUnderlying->getCanonicalDecl() != EUnderlying->getCanonicalDecl()) {
    assert(isa<TypeDecl>(DUnderlying) && isa<TypeDecl>(EUnderlying));
    bool HaveTag = isa<TagDecl>(EUnderlying);
    bool WantTag = Kind == neverc::ResolveTag;
    return HaveTag != WantTag;
  }

  if (const auto *DVD = dyn_cast<VarDecl>(DUnderlying)) {
    const auto *EVD = cast<VarDecl>(EUnderlying);
    if (EVD->getType()->isIncompleteType() &&
        !DVD->getType()->isIncompleteType())
      return S.isVisible(DVD);
    return false;
  }

  if (!isa<FunctionDecl>(DUnderlying) && !isa<VarDecl>(DUnderlying))
    return !S.isVisible(Existing);

  for (const Decl *Prev = DUnderlying->getPreviousDecl(); Prev;
       Prev = Prev->getPreviousDecl())
    if (Prev == EUnderlying)
      return true;
  return false;
}

bool canHideTag(const NamedDecl *D) {
  D = D->getUnderlyingDecl();
  return isa<VarDecl>(D) || isa<EnumConstantDecl>(D) || isa<FunctionDecl>(D) ||
         isa<FieldDecl>(D);
}
} // namespace

// ===----------------------------------------------------------------------===
// Lookup result resolution
// ===----------------------------------------------------------------------===

void LookupResult::resolveKind() {
  unsigned N = Decls.size();

  if (LLVM_LIKELY(N <= 1)) {
    if (N == 0)
      assert(ResultKind == NotFound);
    return;
  }

  if (LLVM_UNLIKELY(ResultKind == Ambiguous))
    return;

  // Fast path for N==2: handle the two most common multi-result patterns
  // without constructing DenseMaps or BitVector.
  if (N == 2) {
    const NamedDecl *D0 = Decls[0]->getUnderlyingDecl();
    const NamedDecl *D1 = Decls[1]->getUnderlyingDecl();
    const NamedDecl *C0 = cast<NamedDecl>(D0->getCanonicalDecl());
    const NamedDecl *C1 = cast<NamedDecl>(D1->getCanonicalDecl());

    // Case 1: both resolve to the same canonical decl (redeclaration).
    if (C0 == C1) {
      if (isPreferredLookupResult(getSema(), getLookupKind(), Decls[1],
                                  Decls[0]))
        Decls[0] = Decls[1];
      Decls.truncate(1);
      ResultKind = LookupResult::Found;
      return;
    }

    // Case 2: one tag + one non-tag in the same scope — hide the tag.
    if (HideTags && !D0->isInvalidDecl() && !D1->isInvalidDecl()) {
      bool Tag0 = isa<TagDecl>(C0), Tag1 = isa<TagDecl>(C1);
      if (Tag0 != Tag1) {
        unsigned TagIdx = Tag0 ? 0 : 1;
        unsigned OtherIdx = 1 - TagIdx;
        if (canHideTag(Decls[OtherIdx]) &&
            getScopeMatchContext(Decls[OtherIdx])
                ->Equals(getScopeMatchContext(Decls[TagIdx]))) {
          Decls[0] = Decls[OtherIdx];
          Decls.truncate(1);
          ResultKind = LookupResult::Found;
          return;
        }
      }
    }
    // Fall through to general path for other N==2 cases.
  }

  llvm::SmallDenseMap<const NamedDecl *, unsigned, 16> Unique;
  llvm::SmallDenseMap<QualType, unsigned, 16> UniqueTypes;

  bool Ambiguous = false;
  bool HasTag = false, HasFunction = false;
  const NamedDecl *HasNonFunction = nullptr;

  llvm::BitVector RemovedDecls(N);

  for (unsigned I = 0; I < N; I++) {
    const NamedDecl *D = Decls[I]->getUnderlyingDecl();
    D = cast<NamedDecl>(D->getCanonicalDecl());

    // Ignore an invalid declaration unless it's the only one left.
    if (D->isInvalidDecl() && N - RemovedDecls.count() > 1) {
      RemovedDecls.set(I);
      continue;
    }

    // Tag vs non-tag in the same scope (hide tag when appropriate).
    if (HideTags && isa<TagDecl>(D)) {
      bool Hidden = false;
      for (auto *OtherDecl : Decls) {
        if (canHideTag(OtherDecl) && !OtherDecl->isInvalidDecl() &&
            getScopeMatchContext(OtherDecl)->Equals(
                getScopeMatchContext(Decls[I]))) {
          RemovedDecls.set(I);
          Hidden = true;
          break;
        }
      }
      if (Hidden)
        continue;
    }

    std::optional<unsigned> ExistingI;

    // Multiple typedefs/name-for-type: no ambiguity if same canonical type.
    if (const auto *TD = dyn_cast<TypeDecl>(D)) {
      QualType T = getSema().Context.getTypeDeclType(TD);
      auto UniqueResult = UniqueTypes.insert(
          std::make_pair(getSema().Context.getCanonicalType(T), I));
      if (!UniqueResult.second) {
        // The type is not unique.
        ExistingI = UniqueResult.first->second;
      }
    }

    // For non-type declarations, check for a prior lookup result naming this
    // canonical declaration.
    if (!ExistingI) {
      auto UniqueResult = Unique.insert(std::make_pair(D, I));
      if (!UniqueResult.second) {
        // We've seen this entity before.
        ExistingI = UniqueResult.first->second;
      }
    }

    if (ExistingI) {
      // This is not a unique lookup result. Pick one of the results and
      // discard the other.
      if (isPreferredLookupResult(getSema(), getLookupKind(), Decls[I],
                                  Decls[*ExistingI]))
        Decls[*ExistingI] = Decls[I];
      RemovedDecls.set(I);
      continue;
    }

    // Otherwise, do some decl type analysis and then continue.

    if (isa<TagDecl>(D)) {
      if (HasTag)
        Ambiguous = true;
      HasTag = true;
    } else if (isa<FunctionDecl>(D)) {
      HasFunction = true;
    } else {
      if (HasNonFunction)
        Ambiguous = true;
      HasNonFunction = D;
    }
  }

  // Remove decls by replacing them with decls from the end (which
  // means that we need to iterate from the end) and then truncating
  // to the new size.
  for (int I = RemovedDecls.find_last(); I >= 0; I = RemovedDecls.find_prev(I))
    Decls[I] = Decls[--N];
  Decls.truncate(N);

  if ((HasNonFunction && HasFunction) ||
      (HideTags && HasTag && (HasFunction || HasNonFunction)))
    Ambiguous = true;

  if (Ambiguous)
    ResultKind = LookupResult::Ambiguous;
  else if (N > 1)
    ResultKind = LookupResult::FoundOverloaded;
  else
    ResultKind = LookupResult::Found;
}

LLVM_DUMP_METHOD void LookupResult::dump() {
  llvm::errs() << "lookup results for " << getLookupName().getAsString()
               << ":\n";
  for (NamedDecl *D : *this)
    D->dump();
}

// ===----------------------------------------------------------------------===
// Builtin & scope lookup
// ===----------------------------------------------------------------------===

bool Sema::LookupBuiltin(LookupResult &R) {
  neverc::ResolveNameKind NameKind = R.getLookupKind();

  if (NameKind == neverc::ResolveOrdinary ||
      NameKind == neverc::ResolveRedeclWithLinkage) {
    IdentifierInfo *II = R.getLookupName().getAsIdentifierInfo();
    if (II) {
      if (unsigned BuiltinID = II->getBuiltinID()) {
        if (NamedDecl *D =
                LazilyCreateBuiltin(II, BuiltinID, TUScope,
                                    R.isForRedeclaration(), R.getNameLoc())) {
          R.addDecl(D);
          return true;
        }
      }
    }
  }
  return false;
}

namespace {
bool searchDeclContext(Sema &S, LookupResult &R, const DeclContext *DC) {
  bool Found = false;

  DeclContext::lookup_result DR = DC->lookup(R.getLookupName());
  for (NamedDecl *D : DR) {
    if ((D = R.getAcceptableDecl(D))) {
      R.addDecl(D);
      Found = true;
    }
  }

  if (!Found && DC->isTranslationUnit() && S.LookupBuiltin(R))
    return true;

  return Found;
}

bool isAtFileScope(Scope *S) {
  if (DeclContext *Ctx = S->getEntity())
    return Ctx->isFileContext();
  return false;
}

struct FindLocalExternScope {
  FindLocalExternScope(LookupResult &R)
      : R(R), OldFindLocalExtern(R.getIdentifierNamespace() &
                                 Decl::IDNS_LocalExtern) {
    R.setFindLocalExtern(R.getIdentifierNamespace() & Decl::IDNS_Ordinary);
  }
  void restore() { R.setFindLocalExtern(OldFindLocalExtern); }
  ~FindLocalExternScope() { restore(); }
  LookupResult &R;
  bool OldFindLocalExtern;
};
} // namespace

// ===----------------------------------------------------------------------===
// Visibility & availability
// ===----------------------------------------------------------------------===

bool LookupResult::isVisibleSlow(Sema &SemaRef, NamedDecl *D) { return true; }

bool Sema::shouldLinkPossiblyHiddenDecl(LookupResult &R, const NamedDecl *New) {
  for (auto *D : R) {
    if (isVisible(D))
      return true;
    assert(D->isExternallyDeclarable() &&
           "hidden non-externally-declarable result");
  }

  // No visible prior decl found — safe to check external declarability
  // directly since attaching a prior wouldn't change linkage for
  // externally-declarable decls.
  return New->isExternallyDeclarable();
}

namespace {
NamedDecl *findVisibleRedecl(Sema &SemaRef, NamedDecl *D, unsigned IDNS) {
  assert(!LookupResult::isAvailableForLookup(SemaRef, D) && "not in slow case");

  for (auto *RD : D->redecls()) {
    if (RD == D)
      continue;
    auto ND = cast<NamedDecl>(RD);
    if (ND->isInIdentifierNamespace(IDNS) &&
        LookupResult::isAvailableForLookup(SemaRef, ND))
      return ND;
  }
  return nullptr;
}
} // namespace

NamedDecl *LookupResult::getAcceptableDeclSlow(NamedDecl *D) const {
  return findVisibleRedecl(getSema(), D, IDNS);
}

LLVM_ATTRIBUTE_ALWAYS_INLINE
bool LookupResult::isVisible(Sema &SemaRef, NamedDecl *D) { return true; }

LLVM_ATTRIBUTE_ALWAYS_INLINE
bool LookupResult::isAvailableForLookup(Sema &SemaRef, NamedDecl *ND) {
  return true;
}

// ===----------------------------------------------------------------------===
// Name resolution
// ===----------------------------------------------------------------------===

__attribute__((hot, flatten)) bool
Sema::ResolveName(LookupResult &R, Scope *S, bool AllowBuiltinCreation) {
  DeclarationName Name = R.getLookupName();
  if (LLVM_UNLIKELY(!Name))
    return false;

  ResolveNameKind NameKind = R.getLookupKind();

  {
    if (LLVM_UNLIKELY(NameKind == neverc::ResolveRedeclWithLinkage)) {
      while (!(S->getFlags() & Scope::DeclScope) ||
             (S->getEntity() && S->getEntity()->isTransparentContext()))
        S = S->getParent();
    }

    FindLocalExternScope FindLocals(R);
    bool LeftStartingScope = false;

    for (IdentifierResolver::iterator I = IdResolver.begin(Name),
                                      IEnd = IdResolver.end();
         I != IEnd; ++I)
      if (NamedDecl *D = R.getAcceptableDecl(*I)) {
        if (LLVM_UNLIKELY(NameKind == ResolveRedeclWithLinkage)) {
          if (!LeftStartingScope && !S->isDeclScope(*I))
            LeftStartingScope = true;
          if (LeftStartingScope && !((*I)->hasLinkage())) {
            R.setShadowed();
            continue;
          }
        }
        R.addDecl(D);

        {
          IdentifierResolver::iterator NextI = I;
          ++NextI;
          if (LLVM_LIKELY(NextI == IEnd))
            return true;

          while (S && !S->isDeclScope(D))
            S = S->getParent();
          if (S && isAtFileScope(S))
            S = nullptr;
          DeclContext *DC = nullptr;
          if (!S)
            DC = (*I)->getDeclContext()->getRedeclContext();

          for (; NextI != IEnd; ++NextI) {
            if (S) {
              if (!S->isDeclScope(*NextI))
                break;
            } else {
              DeclContext *LastDC =
                  (*NextI)->getDeclContext()->getRedeclContext();
              if (!LastDC->Equals(DC))
                break;
            }

            if (NamedDecl *LastD = R.getAcceptableDecl(*NextI))
              R.addDecl(LastD);
          }

          R.resolveKind();
        }

        return true;
      }
  }

  if (AllowBuiltinCreation && LookupBuiltin(R))
    return true;

  return false;
}

__attribute__((hot)) bool Sema::LookupQualifiedName(LookupResult &R,
                                                    DeclContext *LookupCtx) {
  assert(LookupCtx && "Sema::LookupQualifiedName requires a lookup context");

  if (!R.getLookupName())
    return false;

  assert((!isa<TagDecl>(LookupCtx) ||
          cast<TagDecl>(LookupCtx)->isCompleteDefinition() ||
          cast<TagDecl>(LookupCtx)->isBeingDefined()) &&
         "Declaration context must already be complete!");

  struct QualifiedLookupInScope {
    bool oldVal;
    DeclContext *Context;
    QualifiedLookupInScope(DeclContext *ctx)
        : oldVal(ctx->shouldUseQualifiedLookup()), Context(ctx) {
      ctx->setUseQualifiedLookup();
    }
    ~QualifiedLookupInScope() { Context->setUseQualifiedLookup(oldVal); }
  } QL(LookupCtx);

  if (searchDeclContext(*this, R, LookupCtx)) {
    R.resolveKind();
    return true;
  }

  // C-only: lookups never walk enclosing/base contexts here.
  return false;
}

__attribute__((hot)) bool Sema::LookupParsedName(LookupResult &R, Scope *S,
                                                 bool AllowBuiltinCreation) {
  return ResolveName(R, S, AllowBuiltinCreation);
}

void Sema::DiagnoseAmbiguousLookup(LookupResult &Result) {
  assert(Result.isAmbiguous() && "Lookup result must be ambiguous");

  DeclarationName Name = Result.getLookupName();
  SourceLocation NameLoc = Result.getNameLoc();
  SourceRange LookupRange = Result.getContextRange();

  Diag(NameLoc, diag::err_ambiguous_reference) << Name << LookupRange;
  for (auto *D : Result)
    Diag(D->getLocation(), diag::note_ambiguous_candidate) << D;
}

NamedDecl *Sema::LookupSingleName(Scope *S, DeclarationName Name,
                                  SourceLocation Loc, ResolveNameKind NameKind,
                                  RedeclarationKind Redecl) {
  LookupResult R(*this, Name, Loc, NameKind, Redecl);
  ResolveName(R, S);
  return R.getAsSingle<NamedDecl>();
}

// ===----------------------------------------------------------------------===
// Typo correction
// ===----------------------------------------------------------------------===

namespace {

class ShadowContextRAII;

class VisibleDeclsRecord {
public:
  typedef llvm::TinyPtrVector<NamedDecl *> ShadowMapEntry;

private:
  typedef llvm::DenseMap<DeclarationName, ShadowMapEntry> ShadowMap;

  std::list<ShadowMap> ShadowMaps;

  llvm::SmallPtrSet<DeclContext *, 8> VisitedContexts;

  friend class ShadowContextRAII;

public:
  bool visitedContext(DeclContext *Ctx) {
    return !VisitedContexts.insert(Ctx).second;
  }

  bool alreadyVisitedContext(DeclContext *Ctx) {
    return VisitedContexts.contains(Ctx);
  }

  NamedDecl *checkHidden(NamedDecl *ND);

  void add(NamedDecl *ND) {
    ShadowMaps.back()[ND->getDeclName()].push_back(ND);
  }
};

class ShadowContextRAII {
  VisibleDeclsRecord &Visible;

  typedef VisibleDeclsRecord::ShadowMap ShadowMap;

public:
  ShadowContextRAII(VisibleDeclsRecord &Visible) : Visible(Visible) {
    Visible.ShadowMaps.emplace_back();
  }

  ~ShadowContextRAII() { Visible.ShadowMaps.pop_back(); }
};

} // end anonymous namespace

NamedDecl *VisibleDeclsRecord::checkHidden(NamedDecl *ND) {
  unsigned IDNS = ND->getIdentifierNamespace();
  std::list<ShadowMap>::reverse_iterator SM = ShadowMaps.rbegin();
  for (std::list<ShadowMap>::reverse_iterator SMEnd = ShadowMaps.rend();
       SM != SMEnd; ++SM) {
    ShadowMap::iterator Pos = SM->find(ND->getDeclName());
    if (Pos == SM->end())
      continue;

    for (auto *D : Pos->second) {
      if (D->hasTagIdentifierNamespace() &&
          (IDNS & (Decl::IDNS_Member | Decl::IDNS_Ordinary)))
        continue;

      const Decl *DU = D->getUnderlyingDecl();
      const Decl *NU = ND->getUnderlyingDecl();
      if (isa<FunctionDecl>(DU) && isa<FunctionDecl>(NU) &&
          SM == ShadowMaps.rbegin())
        continue;

      return D;
    }
  }

  return nullptr;
}

namespace {
class LookupVisibleHelper {
public:
  explicit LookupVisibleHelper(TypoCorrectionConsumer &Consumer)
      : Consumer(Consumer) {}

  void lookupVisibleDecls(Sema &SemaRef, DeclContext *Ctx,
                          neverc::ResolveNameKind Kind) {
    LookupResult Result(SemaRef, DeclarationName(), SourceLocation(), Kind);
    Result.setAllowHidden(true);

    ShadowContextRAII Shadow(Visited);
    lookupInDeclContext(Ctx, Result);
  }

private:
  void lookupInDeclContext(DeclContext *Ctx, LookupResult &Result) {
    if (!Ctx)
      return;

    if (Visited.visitedContext(Ctx->getPrimaryContext()))
      return;

    if (isa<TranslationUnitDecl>(Ctx)) {
      auto &S = Result.getSema();
      auto &Idents = S.Context.Idents;

      if (IdentifierInfoLookup *External =
              Idents.getExternalIdentifierLookup()) {
        std::unique_ptr<IdentifierIterator> Iter(External->getIdentifiers());
        for (llvm::StringRef Name = Iter->Next(); !Name.empty();
             Name = Iter->Next())
          Idents.get(Name);
      }

      for (const auto &Ident : Idents) {
        if (!S.IdResolver.mayHaveDeclsInContext(Ident.getValue(), Ctx))
          continue;
        for (auto I = S.IdResolver.begin(Ident.getValue()),
                  E = S.IdResolver.end();
             I != E; ++I) {
          if (S.IdResolver.isDeclInScope(*I, Ctx)) {
            if (NamedDecl *ND = Result.getAcceptableDecl(*I)) {
              Consumer.FoundDecl(ND, Visited.checkHidden(ND));
              Visited.add(ND);
            }
          }
        }
      }

      return;
    }

    llvm::SmallVector<NamedDecl *, 4> DeclsToVisit;
    for (DeclContextLookupResult R : Ctx->lookups())
      for (auto *D : R)
        DeclsToVisit.push_back(D);

    for (auto *D : DeclsToVisit)
      if (auto *ND = Result.getAcceptableDecl(D)) {
        Consumer.FoundDecl(ND, Visited.checkHidden(ND));
        Visited.add(ND);
      }

    DeclsToVisit.clear();
  }

  VisibleDeclsRecord Visited;
  TypoCorrectionConsumer &Consumer;
};
} // namespace

LabelDecl *Sema::LookupOrCreateLabel(IdentifierInfo *II, SourceLocation Loc,
                                     SourceLocation GnuLabelLoc) {
  NamedDecl *Res = nullptr;

  if (GnuLabelLoc.isValid()) {
    Res = LabelDecl::Create(Context, CurContext, Loc, II, GnuLabelLoc);
    PushOnScopeChains(Res, CurScope, true);
    return cast<LabelDecl>(Res);
  }

  Res = LookupSingleName(CurScope, II, Loc, ResolveLabel, NotForRedeclaration);
  if (Res && Res->getDeclContext() != CurContext)
    Res = nullptr;
  if (!Res) {
    Res = LabelDecl::Create(Context, CurContext, Loc, II);
    Scope *S = CurScope->getFnParent();
    assert(S && "Not in a function?");
    PushOnScopeChains(Res, S, true);
  }
  return cast<LabelDecl>(Res);
}

namespace {
bool isTypoCandidateViable(CorrectionCandidateCallback &CCC,
                           TypoCorrection &Candidate) {
  Candidate.setCallbackDistance(CCC.RankCandidate(Candidate));
  return Candidate.getEditDistance(false) != TypoCorrection::InvalidDistance;
}

void searchForTypoCandidate(Sema &SemaRef, LookupResult &Res,
                            IdentifierInfo *Name, Scope *S,
                            DeclContext *MemberContext, bool FindHidden);

void filterCorrectionByVisibility(Sema &SemaRef, TypoCorrection &TC) {
  TypoCorrection::decl_iterator DI = TC.begin(), DE = TC.end();

  for (/**/; DI != DE; ++DI)
    if (!LookupResult::isVisible(SemaRef, *DI))
      break;
  if (DI == DE) {
    TC.setRequiresImport(false);
    return;
  }

  llvm::SmallVector<NamedDecl *, 4> NewDecls(TC.begin(), DI);
  bool AnyVisibleDecls = !NewDecls.empty();

  for (/**/; DI != DE; ++DI) {
    if (LookupResult::isVisible(SemaRef, *DI)) {
      if (!AnyVisibleDecls) {
        AnyVisibleDecls = true;
        NewDecls.clear();
      }
      NewDecls.push_back(*DI);
    } else if (!AnyVisibleDecls)
      NewDecls.push_back(*DI);
  }

  if (NewDecls.empty())
    TC = TypoCorrection();
  else {
    TC.setCorrectionDecls(NewDecls);
    TC.setRequiresImport(!AnyVisibleDecls);
  }
}
} // namespace

void TypoCorrectionConsumer::FoundDecl(NamedDecl *ND, NamedDecl *Hiding) {
  if (Hiding)
    return;

  IdentifierInfo *Name = ND->getIdentifier();
  if (!Name)
    return;

  if (!LookupResult::isVisible(SemaRef, ND) && Name != Typo)
    return;

  FoundName(Name->getName());
}

void TypoCorrectionConsumer::FoundName(llvm::StringRef Name) {
  addName(Name, nullptr);
}

void TypoCorrectionConsumer::addKeywordResult(llvm::StringRef Keyword) {
  addName(Keyword, nullptr, true);
}

void TypoCorrectionConsumer::addName(llvm::StringRef Name, NamedDecl *ND,
                                     bool isKeyword) {
  llvm::StringRef TypoStr = Typo->getName();
  unsigned MinED = abs((int)Name.size() - (int)TypoStr.size());
  if (MinED && TypoStr.size() / MinED < 3)
    return;

  unsigned UpperBound = (TypoStr.size() + 2) / 3;
  unsigned ED = TypoStr.edit_distance(Name, true, UpperBound);
  if (ED > UpperBound)
    return;

  TypoCorrection TC(&SemaRef.Context.Idents.get(Name), ND, ED);
  if (isKeyword)
    TC.makeKeyword();
  TC.setCorrectionRange(Result.getLookupNameInfo());
  addCorrection(TC);
}

namespace {
const unsigned MaxTypoDistanceResultSets = 5;
} // namespace

void TypoCorrectionConsumer::addCorrection(TypoCorrection Correction) {
  llvm::StringRef TypoStr = Typo->getName();
  llvm::StringRef Name = Correction.getCorrectionAsIdentifierInfo()->getName();

  if (TypoStr.size() < 3 &&
      (Name != TypoStr || Correction.getEditDistance(true) > TypoStr.size()))
    return;

  if (Correction.isResolved()) {
    filterCorrectionByVisibility(SemaRef, Correction);
    if (!Correction || !isTypoCandidateViable(*CorrectionValidator, Correction))
      return;
  }

  TypoResultList &CList =
      CorrectionResults[Correction.getEditDistance(false)][Name];

  if (!CList.empty() && !CList.back().isResolved())
    CList.pop_back();
  if (NamedDecl *NewND = Correction.getCorrectionDecl()) {
    auto RI = llvm::find_if(CList, [NewND](const TypoCorrection &TypoCorr) {
      return TypoCorr.getCorrectionDecl() == NewND;
    });
    if (RI != CList.end()) {
      auto IsDeprecated = [](Decl *D) { return D && D->isDeprecated(); };
      std::pair<bool, std::string> NewKey = {
          IsDeprecated(Correction.getFoundDecl()),
          Correction.getAsString(SemaRef.getLangOpts())};

      std::pair<bool, std::string> PrevKey = {
          IsDeprecated(RI->getFoundDecl()),
          RI->getAsString(SemaRef.getLangOpts())};

      if (NewKey < PrevKey)
        *RI = Correction;
      return;
    }
  }
  if (CList.empty() || Correction.isResolved())
    CList.push_back(Correction);

  while (CorrectionResults.size() > MaxTypoDistanceResultSets)
    CorrectionResults.erase(std::prev(CorrectionResults.end()));
}

const TypoCorrection &TypoCorrectionConsumer::getNextCorrection() {
  if (++CurrentTCIndex < ValidatedCorrections.size())
    return ValidatedCorrections[CurrentTCIndex];

  CurrentTCIndex = ValidatedCorrections.size();
  while (!CorrectionResults.empty()) {
    auto DI = CorrectionResults.begin();
    if (DI->second.empty()) {
      CorrectionResults.erase(DI);
      continue;
    }

    auto RI = DI->second.begin();
    if (RI->second.empty()) {
      DI->second.erase(RI);
      continue;
    }

    TypoCorrection TC = RI->second.pop_back_val();
    if (TC.isResolved() || TC.requiresImport() || resolveCorrection(TC)) {
      ValidatedCorrections.push_back(TC);
      return ValidatedCorrections[CurrentTCIndex];
    }
  }
  return ValidatedCorrections[0]; // The empty correction.
}

bool TypoCorrectionConsumer::resolveCorrection(TypoCorrection &Candidate) {
  IdentifierInfo *Name = Candidate.getCorrectionAsIdentifierInfo();
  DeclContext *TempMemberContext = MemberContext;
retry_lookup:
  searchForTypoCandidate(SemaRef, Result, Name, S, TempMemberContext,
                         Name == Typo);
  switch (Result.getResultKind()) {
  case LookupResult::NotFound:
    if (TempMemberContext) {
      TempMemberContext = nullptr;
      goto retry_lookup;
    }
    break;

  case LookupResult::Ambiguous:
    // We don't deal with ambiguities.
    break;

  case LookupResult::Found:
  case LookupResult::FoundOverloaded:
    // Store all of the Decls for overloaded symbols
    for (auto *TRD : Result)
      Candidate.addCorrectionDecl(TRD);
    filterCorrectionByVisibility(SemaRef, Candidate);
    if (!isTypoCandidateViable(*CorrectionValidator, Candidate)) {
      break;
    }
    Candidate.setCorrectionRange(Result.getLookupNameInfo());
    return true;
  }
  return false;
}

namespace {
void searchForTypoCandidate(Sema &SemaRef, LookupResult &Res,
                            IdentifierInfo *Name, Scope *S,
                            DeclContext *MemberContext, bool FindHidden) {
  Res.suppressDiagnostics();
  Res.clear();
  Res.setLookupName(Name);
  Res.setAllowHidden(FindHidden);
  if (MemberContext) {
    SemaRef.LookupQualifiedName(Res, MemberContext);
    return;
  }

  SemaRef.LookupParsedName(Res, S, /*AllowBuiltinCreation=*/false);
}

void populateKeywordCandidates(Sema &SemaRef, TypoCorrectionConsumer &Consumer,
                               Scope *S, CorrectionCandidateCallback &CCC) {
  if (CCC.WantTypeSpecifiers) {
    static constexpr tok::TokenKind CTypeSpecKinds[] = {
        tok::kw_char, tok::kw_const, tok::kw_double, tok::kw_enum,
        tok::kw_float, tok::kw_int, tok::kw_long, tok::kw_short, tok::kw_signed,
        tok::kw_struct, tok::kw_union, tok::kw_unsigned, tok::kw_void,
        tok::kw_volatile, tok::kw__Complex, tok::kw__Imaginary,
        // storage-specifiers as well
        tok::kw_extern, tok::kw_inline, tok::kw_static, tok::kw_typedef};

    for (auto Kind : CTypeSpecKinds)
      Consumer.addKeywordResult(tok::getKeywordSpelling(Kind));

    if (SemaRef.getLangOpts().C99)
      Consumer.addKeywordResult(tok::getKeywordSpelling(tok::kw_restrict));
    if (SemaRef.getLangOpts().Bool)
      Consumer.addKeywordResult(tok::getKeywordSpelling(tok::kw_bool));
    else if (SemaRef.getLangOpts().C99)
      Consumer.addKeywordResult(tok::getKeywordSpelling(tok::kw__Bool));

    if (SemaRef.getLangOpts().GNUKeywords)
      Consumer.addKeywordResult(tok::getKeywordSpelling(tok::kw_typeof));
  } else if (CCC.WantFunctionLikeCasts) {
    static constexpr tok::TokenKind CastableKinds[] = {
        tok::kw_char,   tok::kw_double,   tok::kw_float,
        tok::kw_int,    tok::kw_long,     tok::kw_short,
        tok::kw_signed, tok::kw_unsigned, tok::kw_void};
    for (auto Kind : CastableKinds)
      Consumer.addKeywordResult(tok::getKeywordSpelling(Kind));
  }

  if (CCC.WantExpressionKeywords) {
    Consumer.addKeywordResult(tok::getKeywordSpelling(tok::kw_sizeof));
    if (SemaRef.getLangOpts().Bool) {
      Consumer.addKeywordResult(tok::getKeywordSpelling(tok::kw_false));
      Consumer.addKeywordResult(tok::getKeywordSpelling(tok::kw_true));
    }

    if (SemaRef.getLangOpts().C11) {
      Consumer.addKeywordResult(tok::getKeywordSpelling(tok::kw__Alignof));
    }
  }

  if (CCC.WantRemainingKeywords) {
    if (SemaRef.getCurFunctionDecl()) {
      static constexpr tok::TokenKind CStmtKinds[] = {
          tok::kw_do, tok::kw_else,   tok::kw_for,    tok::kw_goto,
          tok::kw_if, tok::kw_return, tok::kw_switch, tok::kw_while};
      for (auto Kind : CStmtKinds)
        Consumer.addKeywordResult(tok::getKeywordSpelling(Kind));

      if (S && S->getBreakParent())
        Consumer.addKeywordResult(tok::getKeywordSpelling(tok::kw_break));

      if (S && S->getContinueParent())
        Consumer.addKeywordResult(tok::getKeywordSpelling(tok::kw_continue));

      if (SemaRef.getCurFunction() &&
          !SemaRef.getCurFunction()->SwitchStack.empty()) {
        Consumer.addKeywordResult(tok::getKeywordSpelling(tok::kw_case));
        Consumer.addKeywordResult(tok::getKeywordSpelling(tok::kw_default));
      }
    }
  }
}
} // namespace

std::unique_ptr<TypoCorrectionConsumer> Sema::makeTypoCorrectionConsumer(
    const DeclarationNameInfo &TypoName, neverc::ResolveNameKind LookupKind,
    Scope *S, CorrectionCandidateCallback &CCC, DeclContext *MemberContext,
    bool ErrorRecovery) {
  if (Diags.hasFatalErrorOccurred() || !getLangOpts().SpellChecking ||
      DisableTypoCorrection)
    return nullptr;

  IdentifierInfo *Typo = TypoName.getName().getAsIdentifierInfo();
  if (!Typo)
    return nullptr;

  IdentifierSourceLocations::iterator locs = TypoCorrectionFailures.find(Typo);
  if (locs != TypoCorrectionFailures.end() &&
      locs->second.contains(TypoName.getLoc()))
    return nullptr;

  unsigned Limit = getDiagnostics().getDiagnosticOptions().SpellCheckingLimit;
  if (Limit && TyposCorrected >= Limit)
    return nullptr;
  ++TyposCorrected;

  std::unique_ptr<CorrectionCandidateCallback> ClonedCCC = CCC.clone();
  auto Consumer = std::make_unique<TypoCorrectionConsumer>(
      *this, TypoName, LookupKind, S, std::move(ClonedCCC), MemberContext);

  bool IsUnqualifiedLookup = false;
  if (MemberContext) {
    LookupVisibleHelper H(*Consumer);
    H.lookupVisibleDecls(*this, MemberContext, LookupKind);
  } else {
    IsUnqualifiedLookup = true;
  }

  if (IsUnqualifiedLookup) {
    for (const auto &I : Context.Idents)
      Consumer->FoundName(I.getKey());

    if (IdentifierInfoLookup *External =
            Context.Idents.getExternalIdentifierLookup()) {
      std::unique_ptr<IdentifierIterator> Iter(External->getIdentifiers());
      do {
        llvm::StringRef Name = Iter->Next();
        if (Name.empty())
          break;

        Consumer->FoundName(Name);
      } while (true);
    }
  }

  populateKeywordCandidates(*this, *Consumer, S,
                            *Consumer->getCorrectionValidator());

  return Consumer;
}

TypoCorrection Sema::CorrectTypo(const DeclarationNameInfo &TypoName,
                                 neverc::ResolveNameKind LookupKind, Scope *S,
                                 CorrectionCandidateCallback &CCC,
                                 CorrectTypoKind Mode,
                                 DeclContext *MemberContext,
                                 bool RecordFailure) {

  IdentifierInfo *Typo = TypoName.getName().getAsIdentifierInfo();
  auto Consumer = makeTypoCorrectionConsumer(
      TypoName, LookupKind, S, CCC, MemberContext, Mode == CTK_ErrorRecovery);

  if (!Consumer)
    return TypoCorrection();

  if (Consumer->empty())
    return FailedCorrection(Typo, TypoName.getLoc(), RecordFailure);

  unsigned ED = Consumer->getBestEditDistance(true);
  unsigned TypoLen = Typo->getName().size();
  if (ED > 0 && TypoLen / ED < 3)
    return FailedCorrection(Typo, TypoName.getLoc(), RecordFailure);

  TypoCorrection BestTC = Consumer->getNextCorrection();
  TypoCorrection SecondBestTC = Consumer->getNextCorrection();
  if (!BestTC)
    return FailedCorrection(Typo, TypoName.getLoc(), RecordFailure);

  ED = BestTC.getEditDistance();

  if (TypoLen >= 3 && ED > 0 && TypoLen / ED < 3)
    return FailedCorrection(Typo, TypoName.getLoc(), RecordFailure);

  if (!SecondBestTC ||
      SecondBestTC.getEditDistance(false) > BestTC.getEditDistance(false)) {
    const TypoCorrection &Result = BestTC;

    if (ED == 0 && Result.isKeyword())
      return FailedCorrection(Typo, TypoName.getLoc(), RecordFailure);

    TypoCorrection TC = Result;
    TC.setCorrectionRange(TypoName);
    filterCorrectionByVisibility(*this, TC);
    return TC;
  }

  return FailedCorrection(Typo, TypoName.getLoc(),
                          RecordFailure && !SecondBestTC);
}

TypoExpr *Sema::CorrectTypoDelayed(const DeclarationNameInfo &TypoName,
                                   neverc::ResolveNameKind LookupKind, Scope *S,
                                   CorrectionCandidateCallback &CCC,
                                   TypoDiagnosticGenerator TDG,
                                   TypoRecoveryCallback TRC,
                                   CorrectTypoKind Mode,
                                   DeclContext *MemberContext) {
  auto Consumer = makeTypoCorrectionConsumer(
      TypoName, LookupKind, S, CCC, MemberContext, Mode == CTK_ErrorRecovery);

  // Give the external sema source a chance to correct the typo.
  TypoCorrection ExternalTypo;
  if (!Consumer || Consumer->empty())
    return nullptr;

  unsigned ED = Consumer->getBestEditDistance(true);
  IdentifierInfo *Typo = TypoName.getName().getAsIdentifierInfo();
  if (ED > 0 && Typo->getName().size() / ED < 3)
    return nullptr;
  ExprEvalContexts.back().NumTypos++;
  return createDelayedTypo(std::move(Consumer), std::move(TDG), std::move(TRC),
                           TypoName.getLoc());
}

void TypoCorrection::addCorrectionDecl(NamedDecl *CDecl) {
  if (!CDecl)
    return;

  if (isKeyword())
    CorrectionDecls.clear();

  CorrectionDecls.push_back(CDecl);

  if (!CorrectionName)
    CorrectionName = CDecl->getDeclName();
}

std::string TypoCorrection::getAsString(const LangOptions &) const {
  return CorrectionName.getAsString();
}

bool CorrectionCandidateCallback::ValidateCandidate(
    const TypoCorrection &candidate) {
  if (!candidate.isResolved())
    return true;

  if (candidate.isKeyword())
    return WantTypeSpecifiers || WantExpressionKeywords ||
           WantRemainingKeywords;

  bool HasNonType = false;
  for (Decl *D : candidate) {
    if (!isa<TypeDecl>(D))
      HasNonType = true;
  }

  return WantTypeSpecifiers || HasNonType;
}

FunctionCallFilterCCC::FunctionCallFilterCCC(unsigned NumArgs)
    : NumArgs(NumArgs) {
  WantTypeSpecifiers = false;
  WantFunctionLikeCasts = false;
  WantRemainingKeywords = false;
}

bool FunctionCallFilterCCC::ValidateCandidate(const TypoCorrection &candidate) {
  if (!candidate.getCorrectionDecl())
    return candidate.isKeyword();

  for (auto *C : candidate) {
    FunctionDecl *FD = nullptr;
    NamedDecl *ND = C->getUnderlyingDecl();
    if (!(FD = dyn_cast<FunctionDecl>(ND)) && isa<ValueDecl>(ND)) {
      // If the Decl is not a function, determine if it is a pointer to a
      // function. If so, check against the number of arguments expected for
      // the pointee.
      QualType ValType = cast<ValueDecl>(ND)->getType();
      if (ValType.isNull())
        continue;
      if (ValType->isAnyPointerType())
        ValType = ValType->getPointeeType();
      if (const FunctionProtoType *FPT = ValType->getAs<FunctionProtoType>())
        if (FPT->getNumParams() == NumArgs)
          return true;
    }

    if (!FD || !(FD->getNumParams() >= NumArgs &&
                 FD->getMinRequiredArguments() <= NumArgs))
      continue;

    return true;
  }
  return false;
}

// ===----------------------------------------------------------------------===
// Typo diagnosis & pragma dump
// ===----------------------------------------------------------------------===

void Sema::diagnoseTypo(const TypoCorrection &Correction,
                        const PartialDiagnostic &TypoDiag, bool ErrorRecovery) {
  diagnoseTypo(Correction, TypoDiag, PDiag(diag::note_previous_decl),
               ErrorRecovery);
}

namespace {
const NamedDecl *locateImportableDefinition(const NamedDecl *D) {
  if (const auto *VD = dyn_cast<VarDecl>(D))
    return VD->getDefinition();
  if (const auto *FD = dyn_cast<FunctionDecl>(D))
    return FD->getDefinition();
  if (const auto *TD = dyn_cast<TagDecl>(D))
    return TD->getDefinition();
  return nullptr;
}
} // namespace

void Sema::diagnoseTypo(const TypoCorrection &Correction,
                        const PartialDiagnostic &TypoDiag,
                        const PartialDiagnostic &PrevNote, bool ErrorRecovery) {
  std::string CorrectedStr = Correction.getAsString(getLangOpts());
  std::string CorrectedQuotedStr = Correction.getQuoted(getLangOpts());
  FixItHint FixTypo = FixItHint::CreateReplacement(
      Correction.getCorrectionRange(), CorrectedStr);

  Diag(Correction.getCorrectionRange().getBegin(), TypoDiag)
      << CorrectedQuotedStr << (ErrorRecovery ? FixTypo : FixItHint());

  NamedDecl *ChosenDecl =
      Correction.isKeyword() ? nullptr : Correction.getFoundDecl();
  if (PrevNote.getDiagID() && ChosenDecl)
    Diag(ChosenDecl->getLocation(), PrevNote)
        << CorrectedQuotedStr << (ErrorRecovery ? FixItHint() : FixTypo);

  for (const PartialDiagnostic &PD : Correction.getExtraDiagnostics())
    Diag(Correction.getCorrectionRange().getBegin(), PD);
}

TypoExpr *Sema::createDelayedTypo(std::unique_ptr<TypoCorrectionConsumer> TCC,
                                  TypoDiagnosticGenerator TDG,
                                  TypoRecoveryCallback TRC,
                                  SourceLocation TypoLoc) {
  assert(TCC && "createDelayedTypo requires a valid TypoCorrectionConsumer");
  auto TE = new (Context) TypoExpr(Context.DependentTy, TypoLoc);
  auto &State = DelayedTypos[TE];
  State.Consumer = std::move(TCC);
  State.DiagHandler = std::move(TDG);
  State.RecoveryHandler = std::move(TRC);
  if (TE)
    TypoExprs.push_back(TE);
  return TE;
}

const Sema::TypoExprState &Sema::getTypoExprState(TypoExpr *TE) const {
  auto Entry = DelayedTypos.find(TE);
  assert(Entry != DelayedTypos.end() &&
         "Failed to get the state for a TypoExpr!");
  return Entry->second;
}

void Sema::clearDelayedTypo(TypoExpr *TE) { DelayedTypos.erase(TE); }

void Sema::OnPragmaDump(Scope *S, SourceLocation IILoc, IdentifierInfo *II) {
  DeclarationNameInfo Name(II, IILoc);
  LookupResult R(*this, Name, ResolveAny, neverc::NotForRedeclaration);
  R.suppressDiagnostics();
  R.setHideTags(false);
  ResolveName(R, S);
  R.dump();
}

void Sema::OnPragmaDump(Expr *E) { E->dump(); }
