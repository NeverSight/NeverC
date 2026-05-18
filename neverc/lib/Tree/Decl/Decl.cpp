#include "neverc/Tree/Decl/Decl.h"
#include "Core/Linkage.h"
#include "neverc/Foundation/Builtin/Builtins.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Core/TreeDiag.h"
#include "neverc/Tree/Core/TreeMutationListener.h"
#include "neverc/Tree/Decl/PrettyDeclStackTrace.h"
#include "neverc/Tree/Decl/Redeclarable.h"
#include "neverc/Tree/Expr/Expr.h"
#include "neverc/Tree/Format/OSLog.h"
#include "neverc/Tree/Stmt/Stmt.h"
#include "neverc/Tree/Type/StructLayout.h"
#include "neverc/Tree/Type/Type.h"
#include "neverc/Tree/Type/TypeLoc.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>
#include <string>

using namespace neverc;

// ===----------------------------------------------------------------------===
// Linkage & visibility computation
// ===----------------------------------------------------------------------===

void PrettyDeclStackTraceEntry::print(llvm::raw_ostream &OS) const {
  SourceLocation Loc = this->Loc;
  if (!Loc.isValid() && TheDecl)
    Loc = TheDecl->getLocation();
  if (Loc.isValid()) {
    Loc.print(OS, Context.getSourceManager());
    OS << ": ";
  }
  OS << Message;

  if (auto *ND = dyn_cast_if_present<NamedDecl>(TheDecl)) {
    OS << " '";
    ND->getNameForDiagnostic(OS, Context.getPrintingPolicy(), true);
    OS << "'";
  }

  OS << '\n';
}

TranslationUnitDecl::TranslationUnitDecl(TreeContext &ctx)
    : Decl(TranslationUnit, nullptr, SourceLocation()),
      DeclContext(TranslationUnit), redeclarable_base(ctx), Ctx(ctx) {}

// Visibility rules aren't rigorously externally specified, but here
// are the basic principles behind what we implement:
//
// 1. An explicit visibility attribute is generally a direct expression
// of the user's intent and should be honored.  Only the innermost
// visibility attribute applies.  If no visibility attribute applies,
// global visibility settings are considered.
//
// 2. A variable that does not otherwise have explicit visibility can
// be restricted by the visibility of its type.
//
// 3. A visibility restriction is explicit if it comes from an
// attribute (or something like it), not a global visibility setting.
// When emitting a reference to an external symbol, visibility
// restrictions are ignored unless they are explicit.
//
// 4. When computing the visibility of a non-type, only non-type visibility
// restrictions are considered: the 'visibility' attribute, global
// value-visibility settings, and a few special cases like __private_extern.
//
// 5. When computing the visibility of a type, only type visibility
// restrictions are considered:
// the 'type_visibility' attribute and global type-visibility settings.
// However, a 'visibility' attribute counts as a 'type_visibility'
// attribute on any declaration that only has the former.
//
// The visibility of a "secondary" entity is computed using the kind of
// that entity, not the kind of the primary entity for which we are
// computing visibility.

namespace {
bool hasExplicitVisibilityAlready(LVComputationKind computation) {
  return computation.IgnoreExplicitVisibility;
}

LVComputationKind withExplicitVisibilityAlready(LVComputationKind Kind) {
  Kind.IgnoreExplicitVisibility = true;
  return Kind;
}

std::optional<Visibility> getExplicitVisibility(const NamedDecl *D,
                                                LVComputationKind kind) {
  assert(!kind.IgnoreExplicitVisibility &&
         "asking for explicit visibility when we shouldn't be");
  return D->getExplicitVisibility(kind.getExplicitVisibilityKind());
}

bool usesTypeVisibility(const NamedDecl *D) { return isa<TypeDecl>(D); }

template <class T> Visibility getVisibilityFromAttr(const T *attr) {
  switch (attr->getVisibility()) {
  case T::Default:
    return DefaultVisibility;
  case T::Hidden:
    return HiddenVisibility;
  case T::Protected:
    return ProtectedVisibility;
  }
  llvm_unreachable("bad visibility kind");
}

std::optional<Visibility>
getVisibilityOf(const NamedDecl *D, NamedDecl::ExplicitVisibilityKind kind) {
  // If we're ultimately computing the visibility of a type, look for
  // a 'type_visibility' attribute before looking for 'visibility'.
  if (kind == NamedDecl::VisibilityForType) {
    if (const auto *A = D->getAttr<TypeVisibilityAttr>()) {
      return getVisibilityFromAttr(A);
    }
  }

  // If this declaration has an explicit visibility attribute, use it.
  if (const auto *A = D->getAttr<VisibilityAttr>()) {
    return getVisibilityFromAttr(A);
  }

  return std::nullopt;
}
} // namespace

LinkageInfo LinkageComputer::getLVForType(const Type &T,
                                          LVComputationKind computation) {
  if (computation.IgnoreAllVisibility)
    return LinkageInfo(T.getLinkage(), DefaultVisibility, true);
  return getTypeLinkageAndVisibility(&T);
}

namespace {
LinkageInfo getExternalLinkageFor(const NamedDecl *D) {
  return LinkageInfo::external();
}

LLVM_ATTRIBUTE_ALWAYS_INLINE
StorageClass getStorageClass(const Decl *D) {
  if (LLVM_LIKELY(D)) {
    if (const auto *VD = dyn_cast<VarDecl>(D))
      return VD->getStorageClass();
    if (const auto *FD = dyn_cast<FunctionDecl>(D))
      return FD->getStorageClass();
  }
  return SC_None;
}
} // namespace

LinkageInfo
LinkageComputer::getLVForFileScopeDecl(const NamedDecl *D,
                                       LVComputationKind computation,
                                       bool IgnoreVarTypeLinkage) {
  assert(D->getDeclContext()->getRedeclContext()->isFileContext() &&
         "Not a file-scope name");
  TreeContext &Context = D->getTreeContext();

  // File-scope names declared `static` have internal linkage (C99 6.2.2p3).
  if (getStorageClass(D->getCanonicalDecl()) == SC_Static) {
    return LinkageInfo::internal();
  }

  if (const auto *Var = dyn_cast<VarDecl>(D)) {
    // - a variable of non-volatile const-qualified type, unless
    //   - it is explicitly declared extern, or
    //   - it is inline, or
    //   - it was previously declared and the prior declaration did not have
    //     internal linkage
    for (const VarDecl *PrevVar = Var->getPreviousDecl(); PrevVar;
         PrevVar = PrevVar->getPreviousDecl()) {
      if (PrevVar->getStorageClass() == SC_PrivateExtern &&
          Var->getStorageClass() == SC_None)
        return getDeclLinkageAndVisibility(PrevVar);
      // Explicitly declared static.
      if (PrevVar->getStorageClass() == SC_Static)
        return LinkageInfo::internal();
    }
  } else if (const auto *IFD = dyn_cast<IndirectFieldDecl>(D)) {
    //   - a data member of an anonymous union.
    const VarDecl *VD = IFD->getVarDecl();
    assert(VD && "Expected a VarDecl in this IndirectFieldDecl!");
    return getLVForFileScopeDecl(VD, computation, IgnoreVarTypeLinkage);
  }
  assert(!isa<FieldDecl>(D) && "Didn't expect a FieldDecl!");

  // Set up the defaults.

  // C99 6.2.2p5:
  //   If the declaration of an identifier for an object has file
  //   scope and no storage-class specifier, its linkage is
  //   external.
  LinkageInfo LV = getExternalLinkageFor(D);

  if (!hasExplicitVisibilityAlready(computation)) {
    if (std::optional<Visibility> Vis = getExplicitVisibility(D, computation)) {
      LV.mergeVisibility(*Vis, true);
    }

    // Add in global settings if the above didn't give us direct visibility.
    if (!LV.isVisibilityExplicit()) {
      // Use global type/value visibility as appropriate.
      Visibility globalVisibility =
          computation.isValueVisibility()
              ? Context.getLangOpts().getValueVisibilityMode()
              : Context.getLangOpts().getTypeVisibilityMode();
      LV.mergeVisibility(globalVisibility, /*explicit*/ false);

      // If we're paying attention to global visibility, apply
      // -finline-visibility-hidden if this is an inline method.
    }
  }

  // Other file-scope names: LV starts as external; adjust per decl kind.
  if (const auto *Var = dyn_cast<VarDecl>(D)) {
    if (Var->getStorageClass() == SC_PrivateExtern)
      LV.mergeVisibility(HiddenVisibility, true);

  } else if (const auto *Function = dyn_cast<FunctionDecl>(D)) {
    if (Function->getStorageClass() == SC_PrivateExtern)
      LV.mergeVisibility(HiddenVisibility, true);

  } else if (const auto *Tag = dyn_cast<TagDecl>(D)) {
    // Unnamed tags have no linkage.
    if (!Tag->hasNameForLinkage())
      return LinkageInfo::none();
  } else if (isa<EnumConstantDecl>(D)) {
    LinkageInfo EnumLV =
        getLVForDecl(cast<NamedDecl>(D->getDeclContext()), computation);
    if (!isExternalFormalLinkage(EnumLV.getLinkage()))
      return LinkageInfo::none();
    LV.merge(EnumLV);

  } else if (auto *TD = dyn_cast<TypedefNameDecl>(D)) {
    // A typedef declaration has linkage if it gives a type a name for
    // linkage purposes.
    if (!TD->getAnonDeclWithTypedefName(/*AnyRedecl*/ true))
      return LinkageInfo::none();

    // Everything not covered here has no linkage.
  } else {
    return LinkageInfo::none();
  }

  // If we ended up with non-externally-visible linkage, visibility should
  // always be default.
  if (!isExternallyVisible(LV.getLinkage()))
    return LinkageInfo(LV.getLinkage(), DefaultVisibility, false);

  return LV;
}

LinkageInfo LinkageComputer::getLVForRecordMember(const NamedDecl *D,
                                                  LVComputationKind computation,
                                                  bool IgnoreVarTypeLinkage) {
  if (!(isa<VarDecl>(D) || isa<FieldDecl>(D) || isa<IndirectFieldDecl>(D) ||
        isa<TagDecl>(D)))
    return LinkageInfo::none();

  LinkageInfo LV;

  // If we have an explicit visibility attribute, merge that in.
  if (!hasExplicitVisibilityAlready(computation)) {
    if (std::optional<Visibility> Vis = getExplicitVisibility(D, computation))
      LV.mergeVisibility(*Vis, true);
  }

  LVComputationKind recordComputation = computation;
  if (LV.isVisibilityExplicit())
    recordComputation = withExplicitVisibilityAlready(computation);

  LinkageInfo recordLV =
      getLVForDecl(cast<RecordDecl>(D->getDeclContext()), recordComputation);
  if (!isExternallyVisible(recordLV.getLinkage()))
    return recordLV;

  if (const auto *VD = dyn_cast<VarDecl>(D)) {
    if (!IgnoreVarTypeLinkage) {
      LinkageInfo typeLV = getLVForType(*VD->getType(), computation);
      if (!LV.isVisibilityExplicit() && !recordLV.isVisibilityExplicit())
        LV.mergeVisibility(typeLV);
      LV.mergeExternalVisibility(typeLV);
    }
  }

  LV.mergeMaybeWithVisibility(recordLV, /*considerRecordVisibility=*/true);
  return LV;
}

void NamedDecl::anchor() {}

bool NamedDecl::isLinkageValid() const {
  if (!hasCachedLinkage())
    return true;

  Linkage L = LinkageComputer{}
                  .computeLVForDecl(this, LVComputationKind::forLinkageOnly())
                  .getLinkage();
  return L == getCachedLinkage();
}

ReservedIdentifierStatus
NamedDecl::isReserved(const LangOptions &LangOpts) const {
  const IdentifierInfo *II = getIdentifier();

  if (!II)
    return ReservedIdentifierStatus::NotReserved;

  ReservedIdentifierStatus Status = II->isReserved(LangOpts);
  if (isReservedAtGlobalScope(Status) && !isReservedInAllContexts(Status)) {
    // This name is only reserved at global scope. Check if this declaration
    // conflicts with a global scope declaration.
    if (isa<ParmVarDecl>(this))
      return ReservedIdentifierStatus::NotReserved;

    // Names reserved at global scope are also reserved for extern "C"
    // variables/functions.
    const DeclContext *DC = getDeclContext()->getRedeclContext();
    if (DC->isTranslationUnit())
      return Status;
    if (auto *VD = dyn_cast<VarDecl>(this))
      if (VD->isExternC())
        return ReservedIdentifierStatus::StartsWithUnderscoreAndIsExternC;
    if (auto *FD = dyn_cast<FunctionDecl>(this))
      if (FD->isExternC())
        return ReservedIdentifierStatus::StartsWithUnderscoreAndIsExternC;
    return ReservedIdentifierStatus::NotReserved;
  }

  return Status;
}

Linkage NamedDecl::getLinkageInternal() const {
  // We don't care about visibility here, so ask for the cheapest
  // possible visibility analysis.
  return LinkageComputer{}
      .getLVForDecl(this, LVComputationKind::forLinkageOnly())
      .getLinkage();
}

Linkage NamedDecl::getFormalLinkage() const {
  return neverc::getFormalLinkage(getLinkageInternal());
}

LinkageInfo NamedDecl::getLinkageAndVisibility() const {
  return LinkageComputer{}.getDeclLinkageAndVisibility(this);
}

namespace {
std::optional<Visibility>
getExplicitVisibilityAux(const NamedDecl *ND,
                         NamedDecl::ExplicitVisibilityKind kind,
                         bool IsMostRecent) {
  assert(!IsMostRecent || ND == ND->getMostRecentDecl());

  // Check the declaration itself first.
  if (std::optional<Visibility> V = getVisibilityOf(ND, kind))
    return V;

  if (!IsMostRecent) {
    const NamedDecl *MostRecent = ND->getMostRecentDecl();
    if (MostRecent != ND)
      return getExplicitVisibilityAux(MostRecent, kind, true);
  }

  if (isa<VarDecl>(ND) || isa<FunctionDecl>(ND))
    return std::nullopt;

  return std::nullopt;
}
} // namespace

std::optional<Visibility>
NamedDecl::getExplicitVisibility(ExplicitVisibilityKind kind) const {
  return getExplicitVisibilityAux(this, kind, false);
}

LinkageInfo LinkageComputer::getLVForLocalDecl(const NamedDecl *D,
                                               LVComputationKind computation) {
  if (const auto *Function = dyn_cast<FunctionDecl>(D)) {
    // This is a "void f();" which got merged with a file static.
    if (Function->getCanonicalDecl()->getStorageClass() == SC_Static)
      return LinkageInfo::internal();

    LinkageInfo LV;
    if (!hasExplicitVisibilityAlready(computation)) {
      if (std::optional<Visibility> Vis =
              getExplicitVisibility(Function, computation))
        LV.mergeVisibility(*Vis, true);
    }

    // Note that Sema::MergeCompatibleFunctionDecls already takes care of
    // merging storage classes and visibility attributes, so we don't have to
    // look at previous decls in here.

    return LV;
  }

  if (const auto *Var = dyn_cast<VarDecl>(D)) {
    if (Var->hasExternalStorage()) {
      LinkageInfo LV;
      if (Var->getStorageClass() == SC_PrivateExtern)
        LV.mergeVisibility(HiddenVisibility, true);
      else if (!hasExplicitVisibilityAlready(computation)) {
        if (std::optional<Visibility> Vis =
                getExplicitVisibility(Var, computation))
          LV.mergeVisibility(*Vis, true);
      }

      if (const VarDecl *Prev = Var->getPreviousDecl()) {
        LinkageInfo PrevLV = getLVForDecl(Prev, computation);
        if (PrevLV.getLinkage() != Linkage::Invalid)
          LV.setLinkage(PrevLV.getLinkage());
        LV.mergeVisibility(PrevLV);
      }

      return LV;
    }

    if (!Var->isStaticLocal())
      return LinkageInfo::none();
  }

  return LinkageInfo::none();
}

LinkageInfo LinkageComputer::computeLVForDecl(const NamedDecl *D,
                                              LVComputationKind computation,
                                              bool IgnoreVarTypeLinkage) {
  // Internal_linkage attribute overrides other considerations.
  if (D->hasAttr<InternalLinkageAttr>())
    return LinkageInfo::internal();

  switch (D->getKind()) {
  default:
    break;

  // Only certain decl kinds can carry linkage; typedefs, using-decls, etc.
  // do not.
  case Decl::ImplicitParam:
  case Decl::Label:
  case Decl::ParmVar:
    return LinkageInfo::none();

  case Decl::EnumConstant:
    return LinkageInfo::visible_none();

  case Decl::Typedef:
    // A typedef declaration has linkage if it gives a type a name for
    // linkage purposes.
    if (!cast<TypedefNameDecl>(D)->getAnonDeclWithTypedefName(
            /*AnyRedecl*/ true))
      return LinkageInfo::none();
    break;

  case Decl::Record:
    break;
  }

  // Handle linkage for file-scope names.
  if (D->getDeclContext()->getRedeclContext()->isFileContext())
    return getLVForFileScopeDecl(D, computation, IgnoreVarTypeLinkage);

  // Struct/union members: linkage follows record scope rules.
  if (D->getDeclContext()->isRecord())
    return getLVForRecordMember(D, computation, IgnoreVarTypeLinkage);

  // Block scope: extern / prior-linkage matching rules.
  if (D->getDeclContext()->isFunctionOrMethod())
    return getLVForLocalDecl(D, computation);

  return LinkageInfo::none();
}

LinkageInfo LinkageComputer::getLVForDecl(const NamedDecl *D,
                                          LVComputationKind computation) {
  // Internal_linkage attribute overrides other considerations.
  if (D->hasAttr<InternalLinkageAttr>())
    return LinkageInfo::internal();

  if (computation.IgnoreAllVisibility && D->hasCachedLinkage())
    return LinkageInfo(D->getCachedLinkage(), DefaultVisibility, false);

  if (std::optional<LinkageInfo> LI = lookup(D, computation))
    return *LI;

  LinkageInfo LV = computeLVForDecl(D, computation);
  if (D->hasCachedLinkage())
    assert(D->getCachedLinkage() == LV.getLinkage());

  D->setCachedLinkage(LV.getLinkage());
  cache(D, computation, LV);
  return LV;
}

LinkageInfo LinkageComputer::getDeclLinkageAndVisibility(const NamedDecl *D) {
  NamedDecl::ExplicitVisibilityKind EK = usesTypeVisibility(D)
                                             ? NamedDecl::VisibilityForType
                                             : NamedDecl::VisibilityForValue;
  LVComputationKind CK(EK);
  return getLVForDecl(D, CK);
}

// ===----------------------------------------------------------------------===
// NamedDecl
// ===----------------------------------------------------------------------===

void NamedDecl::printName(llvm::raw_ostream &OS,
                          const PrintingPolicy &Policy) const {
  Name.print(OS, Policy);
}

void NamedDecl::printName(llvm::raw_ostream &OS) const {
  printName(OS, getTreeContext().getPrintingPolicy());
}

std::string NamedDecl::getQualifiedNameAsString() const {
  std::string QualName;
  llvm::raw_string_ostream OS(QualName);
  printQualifiedName(OS, getTreeContext().getPrintingPolicy());
  return QualName;
}

void NamedDecl::printQualifiedName(llvm::raw_ostream &OS) const {
  printQualifiedName(OS, getTreeContext().getPrintingPolicy());
}

void NamedDecl::printQualifiedName(llvm::raw_ostream &OS,
                                   const PrintingPolicy &P) const {
  if (getDeclContext()->isFunctionOrMethod()) {
    // We do not print '(anonymous)' for function parameters without name.
    printName(OS, P);
    return;
  }
  printScopePrefix(OS, P);
  if (getDeclName())
    OS << *this;
  else {
    // Give the printName override a chance to pick a different name before we
    // fall back to "(anonymous)".
    llvm::SmallString<64> NameBuffer;
    llvm::raw_svector_ostream NameOS(NameBuffer);
    printName(NameOS, P);
    if (NameBuffer.empty())
      OS << "(anonymous)";
    else
      OS << NameBuffer;
  }
}

void NamedDecl::printScopePrefix(llvm::raw_ostream &OS) const {
  printScopePrefix(OS, getTreeContext().getPrintingPolicy());
}

void NamedDecl::printScopePrefix(llvm::raw_ostream &OS,
                                 const PrintingPolicy &P) const {
  const DeclContext *Ctx = getDeclContext();

  if (Ctx->isFunctionOrMethod())
    return;

  using ContextsTy = llvm::SmallVector<const DeclContext *, 8>;
  ContextsTy Contexts;

  // Collect named contexts.
  for (; Ctx; Ctx = Ctx->getParent()) {
    // Skip non-named contexts (e.g. linkage specification blocks).
    const NamedDecl *ND = dyn_cast<NamedDecl>(Ctx);
    if (!ND)
      continue;

    Contexts.push_back(Ctx);
  }

  for (const DeclContext *DC : llvm::reverse(Contexts)) {
    if (const auto *RD = dyn_cast<RecordDecl>(DC)) {
      if (!RD->getIdentifier())
        OS << "(anonymous " << RD->getKindName() << ')';
      else
        OS << *RD;
    } else if (const auto *FD = dyn_cast<FunctionDecl>(DC)) {
      const FunctionProtoType *FT = nullptr;
      if (FD->hasWrittenPrototype())
        FT = dyn_cast<FunctionProtoType>(FD->getType()->castAs<FunctionType>());

      OS << *FD << '(';
      if (FT) {
        unsigned NumParams = FD->getNumParams();
        for (unsigned i = 0; i < NumParams; ++i) {
          if (i)
            OS << ", ";
          OS << FD->getParamDecl(i)->getType().stream(P);
        }

        if (FT->isVariadic()) {
          if (NumParams > 0)
            OS << ", ";
          OS << "...";
        }
      }
      OS << ')';
    } else if (isa<EnumDecl>(DC)) {
      continue;
    } else {
      OS << *cast<NamedDecl>(DC);
    }
    OS << "::";
  }
}

void NamedDecl::getNameForDiagnostic(llvm::raw_ostream &OS,
                                     const PrintingPolicy &Policy,
                                     bool Qualified) const {
  if (Qualified)
    printQualifiedName(OS, Policy);
  else
    printName(OS, Policy);
}

namespace {
template <typename T> bool isRedeclarableImpl(Redeclarable<T> *) {
  return true;
}
bool isRedeclarableImpl(...) { return false; }
bool isRedeclarable(Decl::Kind K) {
  switch (K) {
#define DECL(Type, Base)                                                       \
  case Decl::Type:                                                             \
    return isRedeclarableImpl((Type##Decl *)nullptr);
#define ABSTRACT_DECL(DECL)
#include "neverc/Tree/DeclNodes.td.h"
  }
  llvm_unreachable("unknown decl kind");
}
} // namespace

bool NamedDecl::declarationReplaces(const NamedDecl *OldD,
                                    bool IsKnownNewer) const {
  assert(getDeclName() == OldD->getDeclName() && "Declaration name mismatch");

  // A kind mismatch implies that the declaration is not replaced.
  if (OldD->getKind() != getKind())
    return false;

  // For parameters, pick the newer one. This is either an error or
  // permitted as an extension.
  if (isa<ParmVarDecl>(this))
    return true;

  // Inline namespaces can give us two declarations with the same
  // name and kind in the same scope but different contexts; we should
  // keep both declarations in this case.
  if (!this->getDeclContext()->getRedeclContext()->Equals(
          OldD->getDeclContext()->getRedeclContext()))
    return false;

  if (isRedeclarable(getKind())) {
    if (getCanonicalDecl() != OldD->getCanonicalDecl())
      return false;

    if (IsKnownNewer)
      return true;

    // Check whether this is actually newer than OldD. We want to keep the
    // newer declaration. This loop will usually only iterate once, because
    // OldD is usually the previous declaration.
    for (const auto *D : redecls()) {
      if (D == OldD)
        break;

      // If we reach the canonical declaration, then OldD is not actually older
      // than this one.
      //
      if (D->isCanonicalDecl())
        return false;
    }

    // It's a newer declaration of the same kind of declaration in the same
    // scope: we want this decl instead of the existing one.
    return true;
  }

  // In all other cases, we need to keep both declarations in case they have
  // different visibility. Any attempt to use the name will result in an
  // ambiguity if more than one is visible.
  return false;
}

bool NamedDecl::hasLinkage() const {
  switch (getFormalLinkage()) {
  case Linkage::Invalid:
    llvm_unreachable("Linkage hasn't been computed!");
  case Linkage::None:
    return false;
  case Linkage::Internal:
    return true;
  case Linkage::UniqueExternal:
  case Linkage::VisibleNone:
    llvm_unreachable("Non-formal linkage is not allowed here!");
  case Linkage::External:
    return true;
  }
  llvm_unreachable("Unhandled Linkage enum");
}

// ===----------------------------------------------------------------------===
// DeclaratorDecl
// ===----------------------------------------------------------------------===

SourceLocation DeclaratorDecl::getTypeSpecStartLoc() const {
  TypeSourceInfo *TSI = getTypeSourceInfo();
  if (TSI)
    return TSI->getTypeLoc().getBeginLoc();
  return SourceLocation();
}

SourceLocation DeclaratorDecl::getTypeSpecEndLoc() const {
  TypeSourceInfo *TSI = getTypeSourceInfo();
  if (TSI)
    return TSI->getTypeLoc().getEndLoc();
  return SourceLocation();
}

SourceLocation DeclaratorDecl::getOuterLocStart() const {
  return getInnerLocStart();
}

// Helper function: returns true if QT is or contains a type
// having a postfix component.
namespace {
bool typeIsPostfix(QualType QT) {
  while (true) {
    const Type *T = QT.getTypePtr();
    switch (T->getTypeClass()) {
    default:
      return false;
    case Type::Pointer:
      QT = cast<PointerType>(T)->getPointeeType();
      break;
    case Type::Paren:
    case Type::ConstantArray:
    case Type::IncompleteArray:
    case Type::VariableArray:
    case Type::FunctionProto:
    case Type::FunctionNoProto:
      return true;
    }
  }
}
} // namespace

SourceRange DeclaratorDecl::getSourceRange() const {
  SourceLocation RangeEnd = getLocation();
  if (TypeSourceInfo *TInfo = getTypeSourceInfo()) {
    // If the declaration has no name or the type extends past the name take the
    // end location of the type.
    if (!getDeclName() || typeIsPostfix(TInfo->getType()))
      RangeEnd = TInfo->getTypeLoc().getSourceRange().getEnd();
  }
  return SourceRange(getOuterLocStart(), RangeEnd);
}

const char *VarDecl::getStorageClassSpecifierString(StorageClass SC) {
  switch (SC) {
  case SC_None:
    break;
  case SC_Auto:
    return tok::getKeywordSpelling(tok::kw_auto);
  case SC_Extern:
    return tok::getKeywordSpelling(tok::kw_extern);
  case SC_PrivateExtern:
    return tok::getKeywordSpelling(tok::kw___private_extern__);
  case SC_Register:
    return tok::getKeywordSpelling(tok::kw_register);
  case SC_Static:
    return tok::getKeywordSpelling(tok::kw_static);
  }

  llvm_unreachable("Invalid storage class");
}

// ===----------------------------------------------------------------------===
// VarDecl
// ===----------------------------------------------------------------------===

VarDecl::VarDecl(Kind DK, TreeContext &C, DeclContext *DC,
                 SourceLocation StartLoc, SourceLocation IdLoc,
                 const IdentifierInfo *Id, QualType T, TypeSourceInfo *TInfo,
                 StorageClass SC)
    : DeclaratorDecl(DK, DC, IdLoc, Id, T, TInfo, StartLoc),
      redeclarable_base(C) {
  static_assert(sizeof(VarDeclBitfields) <= sizeof(unsigned),
                "VarDeclBitfields too large!");
  static_assert(sizeof(ParmVarDeclBitfields) <= sizeof(unsigned),
                "ParmVarDeclBitfields too large!");
  static_assert(sizeof(NonParmVarDeclBitfields) <= sizeof(unsigned),
                "NonParmVarDeclBitfields too large!");
  AllBits = 0;
  VarDeclBits.SClass = SC;
  // Everything else is implicitly initialized to false.
}

VarDecl *VarDecl::Create(TreeContext &C, DeclContext *DC, SourceLocation StartL,
                         SourceLocation IdL, const IdentifierInfo *Id,
                         QualType T, TypeSourceInfo *TInfo, StorageClass S) {
  return new (C, DC) VarDecl(Var, C, DC, StartL, IdL, Id, T, TInfo, S);
}

void VarDecl::setStorageClass(StorageClass SC) { VarDeclBits.SClass = SC; }

VarDecl::TLSKind VarDecl::getTLSKind() const {
  switch (VarDeclBits.TSCSpec) {
  case TSCS_unspecified:
    if (!hasAttr<ThreadAttr>())
      return TLS_None;
    return getTreeContext().getLangOpts().isCompatibleWithMSVC(
               LangOptions::MSVC2015)
               ? TLS_Dynamic
               : TLS_Static;
  case TSCS___thread: // Fall through.
  case TSCS__Thread_local:
    return TLS_Static;
  case TSCS_thread_local:
    return TLS_Dynamic;
  }
  llvm_unreachable("Unknown thread storage class specifier!");
}

SourceRange VarDecl::getSourceRange() const {
  if (const Expr *Init = getInit()) {
    SourceLocation InitEnd = Init->getEndLoc();
    // If Init is implicit, ignore its source range and fallback on
    // DeclaratorDecl::getSourceRange() to handle postfix elements.
    if (InitEnd.isValid() && InitEnd != getLocation())
      return SourceRange(getOuterLocStart(), InitEnd);
  }
  return DeclaratorDecl::getSourceRange();
}

namespace {
template <typename T> LanguageLinkage getDeclLanguageLinkage(const T &D) {
  // Extern-linkage functions and variables have a language linkage.
  if (!D.hasExternalFormalLinkage())
    return NoLanguageLinkage;

  return CLanguageLinkage;
}
} // namespace

LanguageLinkage VarDecl::getLanguageLinkage() const {
  return getDeclLanguageLinkage(*this);
}

bool VarDecl::isExternC() const {
  if (getDeclContext()->isRecord())
    return false;
  return getLanguageLinkage() == CLanguageLinkage;
}

VarDecl *VarDecl::getCanonicalDecl() { return getFirstDecl(); }

VarDecl::DefinitionKind
VarDecl::isThisDeclarationADefinition(TreeContext &C) const {
  if (isThisDeclarationADemotedDefinition())
    return DeclarationOnly;

  // C99 6.7p5:
  //   A definition of an identifier is a declaration for that identifier that
  //   [...] causes storage to be reserved for that object.
  // Note: that applies for all non-file-scope objects.
  // C99 6.9.2p1:
  //   If the declaration of an identifier for an object has file scope and an
  //   initializer, the declaration is an external definition for the identifier
  if (hasInit())
    return Definition;

  if (hasDefiningAttr())
    return Definition;

  if (const auto *SAA = getAttr<SelectAnyAttr>())
    if (!SAA->isInherited())
      return Definition;

  if (hasExternalStorage())
    return DeclarationOnly;

  // Linkage-specification blocks affect whether this counts as a definition.
  // C99 6.9.2p2:
  //   A declaration of an object that has file scope without an initializer,
  //   and without a storage class specifier or the scs 'static', constitutes
  //   a tentative definition.
  if (isFileVarDecl())
    return TentativeDefinition;

  // What's left is (in C, block-scope) declarations without initializers or
  // external storage. These are definitions.
  return Definition;
}

VarDecl *VarDecl::getActingDefinition() {
  DefinitionKind Kind = isThisDeclarationADefinition();
  if (Kind != TentativeDefinition)
    return nullptr;

  VarDecl *LastTentative = nullptr;

  // Loop through the declaration chain, starting with the most recent.
  for (VarDecl *Decl = getMostRecentDecl(); Decl;
       Decl = Decl->getPreviousDecl()) {
    Kind = Decl->isThisDeclarationADefinition();
    if (Kind == Definition)
      return nullptr;
    // Record the first (most recent) TentativeDefinition that is encountered.
    if (Kind == TentativeDefinition && !LastTentative)
      LastTentative = Decl;
  }

  return LastTentative;
}

VarDecl *VarDecl::getDefinition(TreeContext &C) {
  VarDecl *First = getFirstDecl();
  for (auto *I : First->redecls()) {
    if (I->isThisDeclarationADefinition(C) == Definition)
      return I;
  }
  return nullptr;
}

VarDecl::DefinitionKind VarDecl::hasDefinition(TreeContext &C) const {
  DefinitionKind Kind = DeclarationOnly;

  const VarDecl *First = getFirstDecl();
  for (auto *I : First->redecls()) {
    Kind = std::max(Kind, I->isThisDeclarationADefinition(C));
    if (Kind == Definition)
      break;
  }

  return Kind;
}

const Expr *VarDecl::getAnyInitializer(const VarDecl *&D) const {
  for (auto *I : redecls()) {
    if (auto Expr = I->getInit()) {
      D = I;
      return Expr;
    }
  }
  return nullptr;
}

bool VarDecl::hasInit() const { return !Init.isNull(); }

Expr *VarDecl::getInit() {
  if (!hasInit())
    return nullptr;

  if (auto *S = Init.dyn_cast<Stmt *>())
    return cast<Expr>(S);

  auto *Eval = getEvaluatedStmt();
  return cast<Expr>(Eval->Value.get(nullptr));
}

Stmt **VarDecl::getInitAddress() {
  if (auto *ES = Init.dyn_cast<EvaluatedStmt *>())
    return ES->Value.getAddressOfPointer(nullptr);

  return Init.getAddrOfPtr1();
}

VarDecl *VarDecl::getInitializingDeclaration() {
  VarDecl *Def = nullptr;
  for (auto *I : redecls()) {
    if (I->hasInit())
      return I;

    if (I->isThisDeclarationADefinition())
      Def = I;
  }
  return Def;
}

void VarDecl::setInit(Expr *I) {
  if (auto *Eval = Init.dyn_cast<EvaluatedStmt *>()) {
    Eval->~EvaluatedStmt();
    getTreeContext().Deallocate(Eval);
  }

  Init = I;
}

EvaluatedStmt *VarDecl::ensureEvaluatedStmt() const {
  auto *Eval = Init.dyn_cast<EvaluatedStmt *>();
  if (!Eval) {
    // Note: EvaluatedStmt contains an APValue, which usually holds
    // resources not allocated from the TreeContext.  We need to do some
    // work to avoid leaking those, but we do so in VarDecl::evaluateValue
    // where we can detect whether there's anything to clean up or not.
    Eval = new (getTreeContext()) EvaluatedStmt;
    Eval->Value = Init.get<Stmt *>();
    Init = Eval;
  }
  return Eval;
}

EvaluatedStmt *VarDecl::getEvaluatedStmt() const {
  return Init.dyn_cast<EvaluatedStmt *>();
}

APValue *VarDecl::evaluateValue() const {
  llvm::SmallVector<PartialDiagnosticAt, 8> Notes;
  return evaluateValueImpl(Notes, hasConstantInitialization());
}

APValue *
VarDecl::evaluateValueImpl(llvm::SmallVectorImpl<PartialDiagnosticAt> &Notes,
                           bool IsConstantInitialization) const {
  EvaluatedStmt *Eval = ensureEvaluatedStmt();

  const auto *Init = getInit();
  assert(!Init->isValueDependent());

  // We only produce notes indicating why an initializer is non-constant the
  // first time it is evaluated.
  if (Eval->WasEvaluated)
    return Eval->Evaluated.isAbsent() ? nullptr : &Eval->Evaluated;

  if (Eval->IsEvaluating) {
    return nullptr;
  }

  Eval->IsEvaluating = true;

  TreeContext &Ctx = getTreeContext();
  bool Result = Init->EvaluateAsInitializer(Eval->Evaluated, Ctx, this, Notes,
                                            IsConstantInitialization);

  // Ensure the computed APValue is cleaned up later if evaluation succeeded,
  // or that it's empty (so that there's nothing to clean up) if evaluation
  // failed.
  if (!Result)
    Eval->Evaluated = APValue();
  else if (Eval->Evaluated.needsCleanup())
    Ctx.addDestruction(&Eval->Evaluated);

  Eval->IsEvaluating = false;
  Eval->WasEvaluated = true;

  return Result ? &Eval->Evaluated : nullptr;
}

APValue *VarDecl::getEvaluatedValue() const {
  if (EvaluatedStmt *Eval = getEvaluatedStmt())
    if (Eval->WasEvaluated)
      return &Eval->Evaluated;

  return nullptr;
}

bool VarDecl::hasICEInitializer(const TreeContext &Context) const {
  const Expr *Init = getInit();
  assert(Init && "no initializer");

  EvaluatedStmt *Eval = ensureEvaluatedStmt();
  if (!Eval->CheckedForICEInit) {
    Eval->CheckedForICEInit = true;
    Eval->HasICEInit = Init->isIntegerConstantExpr(Context);
  }
  return Eval->HasICEInit;
}

bool VarDecl::hasConstantInitialization() const {
  if (hasGlobalStorage())
    return true;

  if (EvaluatedStmt *Eval = getEvaluatedStmt())
    return Eval->HasConstantInitialization;

  return false;
}

bool VarDecl::checkForConstantInitialization(
    llvm::SmallVectorImpl<PartialDiagnosticAt> &Notes) const {
  EvaluatedStmt *Eval = ensureEvaluatedStmt();
  // If we ask for the value before we know whether we have a constant
  // initializer, we can compute the wrong value (for example, due to
  // std::is_constant_evaluated()).
  assert(!Eval->WasEvaluated &&
         "already evaluated var value before checking for constant init");

  assert(!getInit()->isValueDependent());

  // Evaluate the initializer to check whether it's a constant expression.
  Eval->HasConstantInitialization =
      evaluateValueImpl(Notes, true) && Notes.empty();

  // If evaluation as a constant initializer failed, allow re-evaluation as a
  // non-constant initializer if we later find we want the value.
  if (!Eval->HasConstantInitialization)
    Eval->WasEvaluated = false;

  return Eval->HasConstantInitialization;
}

template <typename DeclT> static DeclT *getDefinitionOrSelf(DeclT *D) {
  assert(D);
  if (auto *Def = D->getDefinition())
    return Def;
  return D;
}

bool VarDecl::hasDependentAlignment() const {
  QualType T = getType();
  return T->isUndeducedType() ||
         llvm::any_of(specific_attrs<AlignedAttr>(), [](const AlignedAttr *AA) {
           return AA->isAlignmentDependent();
         });
}

bool VarDecl::isKnownToBeDefined() const { return hasDefinition(); }

bool VarDecl::isNoDestroy(const TreeContext &Ctx) const {
  return hasGlobalStorage() && hasAttr<NoDestroyAttr>();
}

QualType::DestructionKind
VarDecl::needsDestruction(const TreeContext &Ctx) const {
  if (isNoDestroy(Ctx))
    return QualType::DK_none;

  return getType().isDestructedType();
}

bool VarDecl::hasFlexibleArrayInit(const TreeContext &Ctx) const {
  assert(hasInit() && "Expect initializer to check for flexible array init");
  auto *Ty = getType()->getAs<RecordType>();
  if (!Ty || !Ty->getDecl()->hasFlexibleArrayMember())
    return false;
  auto *List = dyn_cast<InitListExpr>(getInit()->IgnoreParens());
  if (!List)
    return false;
  const Expr *FlexibleInit = List->getInit(List->getNumInits() - 1);
  auto InitTy = Ctx.getAsConstantArrayType(FlexibleInit->getType());
  if (!InitTy)
    return false;
  return InitTy->getSize() != 0;
}

CharUnits VarDecl::getFlexibleArrayInitChars(const TreeContext &Ctx) const {
  assert(hasInit() && "Expect initializer to check for flexible array init");
  auto *Ty = getType()->getAs<RecordType>();
  if (!Ty || !Ty->getDecl()->hasFlexibleArrayMember())
    return CharUnits::Zero();
  auto *List = dyn_cast<InitListExpr>(getInit()->IgnoreParens());
  if (!List)
    return CharUnits::Zero();
  const Expr *FlexibleInit = List->getInit(List->getNumInits() - 1);
  auto InitTy = Ctx.getAsConstantArrayType(FlexibleInit->getType());
  if (!InitTy)
    return CharUnits::Zero();
  CharUnits FlexibleArraySize = Ctx.getTypeSizeInChars(InitTy);
  const StructRecordLayout &RL = Ctx.getStructRecordLayout(Ty->getDecl());
  CharUnits FlexibleArrayOffset =
      Ctx.toCharUnitsFromBits(RL.getFieldOffset(RL.getFieldCount() - 1));
  if (FlexibleArrayOffset + FlexibleArraySize < RL.getSize())
    return CharUnits::Zero();
  return FlexibleArrayOffset + FlexibleArraySize - RL.getSize();
}

ParmVarDecl *ParmVarDecl::Create(TreeContext &C, DeclContext *DC,
                                 SourceLocation StartLoc, SourceLocation IdLoc,
                                 IdentifierInfo *Id, QualType T,
                                 TypeSourceInfo *TInfo, StorageClass S,
                                 Expr *DefArg) {
  return new (C, DC)
      ParmVarDecl(ParmVar, C, DC, StartLoc, IdLoc, Id, T, TInfo, S, DefArg);
}

QualType ParmVarDecl::getOriginalType() const {
  TypeSourceInfo *TSI = getTypeSourceInfo();
  QualType T = TSI ? TSI->getType() : getType();
  if (const auto *DT = dyn_cast<DecayedType>(T))
    return DT->getOriginalType();
  return T;
}

SourceRange ParmVarDecl::getSourceRange() const {
  {
    SourceRange ArgRange = getDefaultArgRange();
    if (ArgRange.isValid())
      return SourceRange(getOuterLocStart(), ArgRange.getEnd());
  }

  return DeclaratorDecl::getSourceRange();
}

Expr *ParmVarDecl::getDefaultArg() {
  Expr *Arg = getInit();
  if (auto *E = dyn_cast_if_present<FullExpr>(Arg))
    return E->getSubExpr();

  return Arg;
}

void ParmVarDecl::setDefaultArg(Expr *defarg) {
  ParmVarDeclBits.DefaultArgKind = DAK_Normal;
  Init = defarg;
}

SourceRange ParmVarDecl::getDefaultArgRange() const {
  if (ParmVarDeclBits.DefaultArgKind == DAK_Normal) {
    if (const Expr *E = getInit())
      return E->getSourceRange();
  }
  return SourceRange();
}

bool ParmVarDecl::hasDefaultArg() const { return !Init.isNull(); }

void ParmVarDecl::setParameterIndexLarge(unsigned parameterIndex) {
  getTreeContext().setParameterIndex(this, parameterIndex);
  ParmVarDeclBits.ParameterIndex = ParameterIndexSentinel;
}

unsigned ParmVarDecl::getParameterIndexLarge() const {
  return getTreeContext().getParameterIndex(this);
}

// ===----------------------------------------------------------------------===
// FunctionDecl
// ===----------------------------------------------------------------------===

FunctionDecl::FunctionDecl(Kind DK, TreeContext &C, DeclContext *DC,
                           SourceLocation StartLoc,
                           const DeclarationNameInfo &NameInfo, QualType T,
                           TypeSourceInfo *TInfo, StorageClass S,
                           bool UsesFPIntrin, bool isInlineSpecified,
                           ConstexprSpecKind ConstexprKind)
    : DeclaratorDecl(DK, DC, NameInfo.getLoc(), NameInfo.getName(), T, TInfo,
                     StartLoc),
      DeclContext(DK), redeclarable_base(C), Body(),
      EndRangeLoc(NameInfo.getEndLoc()) {
  assert(T.isNull() || T->isFunctionType());
  FunctionDeclBits.SClass = S;
  FunctionDeclBits.IsInline = isInlineSpecified;
  FunctionDeclBits.IsInlineSpecified = isInlineSpecified;
  FunctionDeclBits.HasInheritedPrototype = false;
  FunctionDeclBits.HasWrittenPrototype = true;
  FunctionDeclBits.HasImplicitReturnZero = false;
  FunctionDeclBits.ConstexprKind = static_cast<uint64_t>(ConstexprKind);
  FunctionDeclBits.UsesSEHTry = false;
  FunctionDeclBits.UsesFPIntrin = UsesFPIntrin;
  FunctionDeclBits.WillHaveBody = false;
  FunctionDeclBits.IsMultiVersion = false;
}

void FunctionDecl::getNameForDiagnostic(llvm::raw_ostream &OS,
                                        const PrintingPolicy &Policy,
                                        bool Qualified) const {
  NamedDecl::getNameForDiagnostic(OS, Policy, Qualified);
}

bool FunctionDecl::isVariadic() const {
  if (const auto *FT = getType()->getAs<FunctionProtoType>())
    return FT->isVariadic();
  return false;
}

bool FunctionDecl::hasBody(const FunctionDecl *&Definition) const {
  for (const auto *I : redecls()) {
    if (I->doesThisDeclarationHaveABody()) {
      Definition = I;
      return true;
    }
  }

  return false;
}

bool FunctionDecl::isDefined(const FunctionDecl *&Definition) const {
  for (const FunctionDecl *FD : redecls()) {
    if (FD->isThisDeclarationADefinition()) {
      Definition = FD;
      return true;
    }
  }

  return false;
}

Stmt *FunctionDecl::getBody(const FunctionDecl *&Definition) const {
  if (!hasBody(Definition))
    return nullptr;

  if (Definition->Body)
    return Definition->Body.get(nullptr);

  return nullptr;
}

void FunctionDecl::setBody(Stmt *B) {
  Body = B;
  if (B)
    EndRangeLoc = B->getEndLoc();
}

namespace {
template <std::size_t Len>
bool isNamed(const NamedDecl *ND, const char (&Str)[Len]) {
  const IdentifierInfo *II = ND->getIdentifier();
  return II && II->isStr(Str);
}
} // namespace

bool FunctionDecl::isMain() const {
  const TranslationUnitDecl *tunit =
      dyn_cast<TranslationUnitDecl>(getDeclContext()->getRedeclContext());
  return tunit && !tunit->getTreeContext().getLangOpts().Freestanding &&
         isNamed(this, "main");
}

bool FunctionDecl::isMSVCRTEntryPoint() const {
  const TranslationUnitDecl *TUnit =
      dyn_cast<TranslationUnitDecl>(getDeclContext()->getRedeclContext());
  if (!TUnit)
    return false;

  // Even though we aren't really targeting MSVCRT if we are freestanding,
  // semantic analysis for these functions remains the same.

  // MSVCRT entry points only exist on MSVCRT targets.
  if (!TUnit->getTreeContext().getTargetInfo().getTriple().isOSMSVCRT())
    return false;

  // Nameless functions cannot be entry points.
  if (!getIdentifier())
    return false;

  return llvm::StringSwitch<bool>(getName())
      .Cases("main",     // an ANSI console app
             "wmain",    // a Unicode console App
             "WinMain",  // an ANSI GUI app
             "wWinMain", // a Unicode GUI app
             "DllMain",  // a DLL
             true)
      .Default(false);
}

bool FunctionDecl::isInlineBuiltinDeclaration() const {
  if (!getBuiltinID())
    return false;

  const FunctionDecl *Definition;
  if (!hasBody(Definition))
    return false;

  if (!Definition->isInlineSpecified() ||
      !Definition->hasAttr<AlwaysInlineAttr>())
    return false;

  TreeContext &Context = getTreeContext();
  switch (Context.GetGVALinkageForFunction(Definition)) {
  case GVA_Internal:
  case GVA_DiscardableODR:
  case GVA_StrongODR:
    return false;
  case GVA_AvailableExternally:
  case GVA_StrongExternal:
    return true;
  }
  llvm_unreachable("Unknown GVALinkage");
}

LanguageLinkage FunctionDecl::getLanguageLinkage() const {
  return getDeclLanguageLinkage(*this);
}

bool FunctionDecl::isExternC() const {
  if (getDeclContext()->isRecord())
    return false;
  return getLanguageLinkage() == CLanguageLinkage;
}

bool FunctionDecl::isGlobal() const {
  if (getCanonicalDecl()->getStorageClass() == SC_Static)
    return false;

  return true;
}

bool FunctionDecl::isNoReturn() const {
  if (hasAttr<NoReturnAttr>() || hasAttr<StandardNoReturnAttr>() ||
      hasAttr<C11NoReturnAttr>())
    return true;

  if (auto *FnTy = getType()->getAs<FunctionType>())
    return FnTy->getNoReturnAttr();

  return false;
}

MultiVersionKind FunctionDecl::getMultiVersionKind() const {
  if (hasAttr<TargetAttr>())
    return MultiVersionKind::Target;
  if (hasAttr<TargetVersionAttr>())
    return MultiVersionKind::TargetVersion;
  if (hasAttr<CPUDispatchAttr>())
    return MultiVersionKind::CPUDispatch;
  if (hasAttr<CPUSpecificAttr>())
    return MultiVersionKind::CPUSpecific;
  if (hasAttr<TargetClonesAttr>())
    return MultiVersionKind::TargetClones;
  return MultiVersionKind::None;
}

bool FunctionDecl::isCPUDispatchMultiVersion() const {
  return isMultiVersion() && hasAttr<CPUDispatchAttr>();
}

bool FunctionDecl::isCPUSpecificMultiVersion() const {
  return isMultiVersion() && hasAttr<CPUSpecificAttr>();
}

bool FunctionDecl::isTargetMultiVersion() const {
  return isMultiVersion() &&
         (hasAttr<TargetAttr>() || hasAttr<TargetVersionAttr>());
}

bool FunctionDecl::isTargetClonesMultiVersion() const {
  return isMultiVersion() && hasAttr<TargetClonesAttr>();
}

void FunctionDecl::setPreviousDeclaration(FunctionDecl *PrevDecl) {
  redeclarable_base::setPreviousDecl(PrevDecl);

  if (PrevDecl && PrevDecl->isInlined())
    setImplicitlyInline(true);
}

FunctionDecl *FunctionDecl::getCanonicalDecl() { return getFirstDecl(); }

unsigned FunctionDecl::getBuiltinID(bool ConsiderWrapperFunctions) const {
  unsigned BuiltinID = 0;

  if (const auto *ABAA = getAttr<ArmBuiltinAliasAttr>()) {
    BuiltinID = ABAA->getBuiltinName()->getBuiltinID();
  } else if (const auto *BAA = getAttr<BuiltinAliasAttr>()) {
    BuiltinID = BAA->getBuiltinName()->getBuiltinID();
  } else if (const auto *A = getAttr<BuiltinAttr>()) {
    BuiltinID = A->getID();
  }

  if (!BuiltinID)
    return 0;

  // If the function is marked "overloadable", it has a different mangled name
  // and is not the C library function.
  if (!ConsiderWrapperFunctions && hasAttr<OverloadableAttr>() &&
      (!hasAttr<ArmBuiltinAliasAttr>() && !hasAttr<BuiltinAliasAttr>()))
    return 0;

  const TreeContext &Context = getTreeContext();
  if (!Context.BuiltinInfo.isPredefinedLibFunction(BuiltinID))
    return BuiltinID;

  // This function has the name of a known C library
  // function. Determine whether it actually refers to the C library
  // function or whether it just has the same name.

  // If this is a static function, it's not a builtin.
  if (!ConsiderWrapperFunctions && getStorageClass() == SC_Static)
    return 0;

  return BuiltinID;
}

unsigned FunctionDecl::getNumParams() const {
  const auto *FPT = getType()->getAs<FunctionProtoType>();
  return FPT ? FPT->getNumParams() : 0;
}

void FunctionDecl::setParams(TreeContext &C,
                             llvm::ArrayRef<ParmVarDecl *> NewParamInfo) {
  assert(!ParamInfo && "Already has param info!");
  assert(NewParamInfo.size() == getNumParams() && "Parameter count mismatch!");

  // Zero params -> null pointer.
  if (!NewParamInfo.empty()) {
    ParamInfo = new (C) ParmVarDecl *[NewParamInfo.size()];
    std::copy(NewParamInfo.begin(), NewParamInfo.end(), ParamInfo);
  }
}

unsigned FunctionDecl::getMinRequiredArguments() const {
  return getNumParams();
}

bool FunctionDecl::isMSExternInline() const {
  assert(isInlined() && "expected to get called on an inlined function!");

  if (!hasAttr<DLLExportAttr>())
    return false;

  for (const FunctionDecl *FD = getMostRecentDecl(); FD;
       FD = FD->getPreviousDecl())
    if (!FD->isImplicit() && FD->getStorageClass() == SC_Extern)
      return true;

  return false;
}

namespace {
bool redeclForcesDefMSVC(const FunctionDecl *Redecl) {
  if (Redecl->getStorageClass() != SC_Extern)
    return false;

  for (const FunctionDecl *FD = Redecl->getPreviousDecl(); FD;
       FD = FD->getPreviousDecl())
    if (!FD->isImplicit() && FD->getStorageClass() == SC_Extern)
      return false;

  return true;
}

bool redeclForcesDefC99(const FunctionDecl *Redecl) {
  // Only consider file-scope declarations in this test.
  if (!Redecl->getLexicalDeclContext()->isTranslationUnit())
    return false;

  // Only consider explicit declarations; the presence of a builtin for a
  // libcall shouldn't affect whether a definition is externally visible.
  if (Redecl->isImplicit())
    return false;

  if (!Redecl->isInlineSpecified() || Redecl->getStorageClass() == SC_Extern)
    return true; // Not an inline definition

  return false;
}
} // namespace

bool FunctionDecl::doesDeclarationForceExternallyVisibleDefinition() const {
  assert(!doesThisDeclarationHaveABody() &&
         "Must have a declaration without a body.");

  const TreeContext &Context = getTreeContext();

  if (Context.getLangOpts().MSVCCompat) {
    const FunctionDecl *Definition;
    if (hasBody(Definition) && Definition->isInlined() &&
        redeclForcesDefMSVC(this))
      return true;
  }

  if (Context.getLangOpts().GNUInline || hasAttr<GNUInlineAttr>()) {
    // With GNU inlining, a declaration with 'inline' but not 'extern', forces
    // an externally visible definition.
    //
    if (!isInlineSpecified() || getStorageClass() == SC_Extern)
      return false;

    const FunctionDecl *Prev = this;
    bool FoundBody = false;
    while ((Prev = Prev->getPreviousDecl())) {
      FoundBody |= Prev->doesThisDeclarationHaveABody();

      if (Prev->doesThisDeclarationHaveABody()) {
        // If it's not the case that both 'inline' and 'extern' are
        // specified on the definition, then it is always externally visible.
        if (!Prev->isInlineSpecified() || Prev->getStorageClass() != SC_Extern)
          return false;
      } else if (Prev->isInlineSpecified() &&
                 Prev->getStorageClass() != SC_Extern) {
        return false;
      }
    }
    return FoundBody;
  }

  // C99 6.7.4p6:
  //   [...] If all of the file scope declarations for a function in a
  //   translation unit include the inline function specifier without extern,
  //   then the definition in that translation unit is an inline definition.
  if (isInlineSpecified() && getStorageClass() != SC_Extern)
    return false;
  const FunctionDecl *Prev = this;
  bool FoundBody = false;
  while ((Prev = Prev->getPreviousDecl())) {
    FoundBody |= Prev->doesThisDeclarationHaveABody();
    if (redeclForcesDefC99(Prev))
      return false;
  }
  return FoundBody;
}

FunctionTypeLoc FunctionDecl::getFunctionTypeLoc() const {
  const TypeSourceInfo *TSI = getTypeSourceInfo();
  return TSI ? TSI->getTypeLoc().IgnoreParens().getAs<FunctionTypeLoc>()
             : FunctionTypeLoc();
}

SourceRange FunctionDecl::getReturnTypeSourceRange() const {
  FunctionTypeLoc FTL = getFunctionTypeLoc();
  if (!FTL)
    return SourceRange();

  // Skip self-referential return types.
  const SourceManager &SM = getTreeContext().getSourceManager();
  SourceRange RTRange = FTL.getReturnLoc().getSourceRange();
  SourceLocation Boundary = getNameInfo().getBeginLoc();
  if (RTRange.isInvalid() || Boundary.isInvalid() ||
      !SM.isBeforeInTranslationUnit(RTRange.getEnd(), Boundary))
    return SourceRange();

  return RTRange;
}

SourceRange FunctionDecl::getParametersSourceRange() const {
  unsigned NP = getNumParams();
  SourceLocation EllipsisLoc = getEllipsisLoc();

  if (NP == 0 && EllipsisLoc.isInvalid())
    return SourceRange();

  SourceLocation Begin =
      NP > 0 ? ParamInfo[0]->getSourceRange().getBegin() : EllipsisLoc;
  SourceLocation End = EllipsisLoc.isValid()
                           ? EllipsisLoc
                           : ParamInfo[NP - 1]->getSourceRange().getEnd();

  return SourceRange(Begin, End);
}

bool FunctionDecl::isInlineDefinitionExternallyVisible() const {
  assert((doesThisDeclarationHaveABody() || willHaveBody() ||
          hasAttr<AliasAttr>()) &&
         "Must be a function definition");
  assert(isInlined() && "Function must be inline");
  TreeContext &Context = getTreeContext();

  if (Context.getLangOpts().GNUInline || hasAttr<GNUInlineAttr>()) {
    // Note: If you change the logic here, please change
    // doesDeclarationForceExternallyVisibleDefinition as well.
    //
    // If it's not the case that both 'inline' and 'extern' are
    // specified on the definition, then this inline definition is
    if (!(isInlineSpecified() && getStorageClass() == SC_Extern))
      return true;

    // If any declaration is 'inline' but not 'extern', then this definition
    // is externally visible.
    for (auto *Redecl : redecls()) {
      if (Redecl->isInlineSpecified() && Redecl->getStorageClass() != SC_Extern)
        return true;
    }

    return false;
  }

  // C99 inline visibility (non-GNU branch).

  // C99 6.7.4p6:
  //   [...] If all of the file scope declarations for a function in a
  //   translation unit include the inline function specifier without extern,
  //   then the definition in that translation unit is an inline definition.
  for (auto *Redecl : redecls()) {
    if (redeclForcesDefC99(Redecl))
      return true;
  }

  // C99 6.7.4p6:
  //   An inline definition does not provide an external definition for the
  //   function, and does not forbid an external definition in another
  //   translation unit.
  return false;
}

SourceRange FunctionDecl::getSourceRange() const {
  return SourceRange(getOuterLocStart(), EndRangeLoc);
}

unsigned FunctionDecl::getMemoryFunctionKind() const {
  IdentifierInfo *FnInfo = getIdentifier();

  if (!FnInfo)
    return 0;

  // Builtin handling.
  switch (getBuiltinID()) {
  case Builtin::BI__builtin_memset:
  case Builtin::BI__builtin___memset_chk:
  case Builtin::BImemset:
    return Builtin::BImemset;

  case Builtin::BI__builtin_memcpy:
  case Builtin::BI__builtin___memcpy_chk:
  case Builtin::BImemcpy:
    return Builtin::BImemcpy;

  case Builtin::BI__builtin_mempcpy:
  case Builtin::BI__builtin___mempcpy_chk:
  case Builtin::BImempcpy:
    return Builtin::BImempcpy;

  case Builtin::BI__builtin_memmove:
  case Builtin::BI__builtin___memmove_chk:
  case Builtin::BImemmove:
    return Builtin::BImemmove;

  case Builtin::BIstrlcpy:
  case Builtin::BI__builtin___strlcpy_chk:
    return Builtin::BIstrlcpy;

  case Builtin::BIstrlcat:
  case Builtin::BI__builtin___strlcat_chk:
    return Builtin::BIstrlcat;

  case Builtin::BI__builtin_memcmp:
  case Builtin::BImemcmp:
    return Builtin::BImemcmp;

  case Builtin::BI__builtin_bcmp:
  case Builtin::BIbcmp:
    return Builtin::BIbcmp;

  case Builtin::BI__builtin_strncpy:
  case Builtin::BI__builtin___strncpy_chk:
  case Builtin::BIstrncpy:
    return Builtin::BIstrncpy;

  case Builtin::BI__builtin_strncmp:
  case Builtin::BIstrncmp:
    return Builtin::BIstrncmp;

  case Builtin::BI__builtin_strncasecmp:
  case Builtin::BIstrncasecmp:
    return Builtin::BIstrncasecmp;

  case Builtin::BI__builtin_strncat:
  case Builtin::BI__builtin___strncat_chk:
  case Builtin::BIstrncat:
    return Builtin::BIstrncat;

  case Builtin::BI__builtin_strndup:
  case Builtin::BIstrndup:
    return Builtin::BIstrndup;

  case Builtin::BI__builtin_strlen:
  case Builtin::BIstrlen:
    return Builtin::BIstrlen;

  case Builtin::BI__builtin_bzero:
  case Builtin::BIbzero:
    return Builtin::BIbzero;

  case Builtin::BI__builtin_bcopy:
  case Builtin::BIbcopy:
    return Builtin::BIbcopy;

  case Builtin::BIfree:
    return Builtin::BIfree;

  default:
    if (isExternC()) {
      if (FnInfo->isStr("memset"))
        return Builtin::BImemset;
      if (FnInfo->isStr("memcpy"))
        return Builtin::BImemcpy;
      if (FnInfo->isStr("mempcpy"))
        return Builtin::BImempcpy;
      if (FnInfo->isStr("memmove"))
        return Builtin::BImemmove;
      if (FnInfo->isStr("memcmp"))
        return Builtin::BImemcmp;
      if (FnInfo->isStr("bcmp"))
        return Builtin::BIbcmp;
      if (FnInfo->isStr("strncpy"))
        return Builtin::BIstrncpy;
      if (FnInfo->isStr("strncmp"))
        return Builtin::BIstrncmp;
      if (FnInfo->isStr("strncasecmp"))
        return Builtin::BIstrncasecmp;
      if (FnInfo->isStr("strncat"))
        return Builtin::BIstrncat;
      if (FnInfo->isStr("strndup"))
        return Builtin::BIstrndup;
      if (FnInfo->isStr("strlen"))
        return Builtin::BIstrlen;
      if (FnInfo->isStr("bzero"))
        return Builtin::BIbzero;
      if (FnInfo->isStr("bcopy"))
        return Builtin::BIbcopy;
    }
    break;
  }
  return 0;
}

FieldDecl *FieldDecl::Create(const TreeContext &C, DeclContext *DC,
                             SourceLocation StartLoc, SourceLocation IdLoc,
                             IdentifierInfo *Id, QualType T,
                             TypeSourceInfo *TInfo, Expr *BW) {
  return new (C, DC)
      FieldDecl(Decl::Field, DC, StartLoc, IdLoc, Id, T, TInfo, BW);
}

bool FieldDecl::isAnonymousStructOrUnion() const {
  if (!isImplicit() || getDeclName())
    return false;

  if (const auto *Record = getType()->getAs<RecordType>())
    return Record->getDecl()->isAnonymousStructOrUnion();

  return false;
}

unsigned FieldDecl::getBitWidthValue(const TreeContext &Ctx) const {
  assert(isBitField() && "not a bitfield");
  return getBitWidth()->EvaluateKnownConstInt(Ctx).getZExtValue();
}

bool FieldDecl::isZeroLengthBitField(const TreeContext &Ctx) const {
  return isUnnamedBitfield() && getBitWidthValue(Ctx) == 0;
}

bool FieldDecl::isZeroSize(const TreeContext &Ctx) const {
  // C-only: zero-length GNU/extension bit-fields are the only zero-size fields.
  return isZeroLengthBitField(Ctx);
}

unsigned FieldDecl::getFieldIndex() const {
  const FieldDecl *Canonical = getCanonicalDecl();
  if (Canonical != this)
    return Canonical->getFieldIndex();

  if (CachedFieldIndex)
    return CachedFieldIndex - 1;

  unsigned Index = 0;
  const RecordDecl *RD = getParent()->getDefinition();
  assert(RD && "requested index for field of struct with no definition");

  for (auto *Field : RD->fields()) {
    Field->getCanonicalDecl()->CachedFieldIndex = Index + 1;
    assert(Field->getCanonicalDecl()->CachedFieldIndex == Index + 1 &&
           "overflow in field numbering");
    ++Index;
  }

  assert(CachedFieldIndex && "failed to find field in parent");
  return CachedFieldIndex - 1;
}

SourceRange FieldDecl::getSourceRange() const {
  if (const Expr *BW = getBitWidth())
    return SourceRange(getInnerLocStart(), BW->getEndLoc());
  return DeclaratorDecl::getSourceRange();
}

void FieldDecl::printName(llvm::raw_ostream &OS,
                          const PrintingPolicy &Policy) const {
  // Print unnamed members using name of their type.
  if (isAnonymousStructOrUnion()) {
    this->getType().print(OS, Policy);
    return;
  }
  // Otherwise, do the normal printing.
  DeclaratorDecl::printName(OS, Policy);
}

TagDecl::TagDecl(Kind DK, TagKind TK, const TreeContext &C, DeclContext *DC,
                 SourceLocation L, IdentifierInfo *Id, TagDecl *PrevDecl,
                 SourceLocation StartL)
    : TypeDecl(DK, DC, L, Id, StartL), DeclContext(DK), redeclarable_base(C),
      TypedefNameDeclOrQualifier((TypedefNameDecl *)nullptr) {
  assert((DK != Enum || TK == TagTypeKind::Enum) &&
         "EnumDecl not matched with TagTypeKind::Enum");
  setPreviousDecl(PrevDecl);
  setTagKind(TK);
  setCompleteDefinition(false);
  setBeingDefined(false);
  setEmbeddedInDeclarator(false);
  setFreeStanding(false);
  setCompleteDefinitionRequired(false);
  TagDeclBits.IsThisDeclarationADemotedDefinition = false;
}

SourceLocation TagDecl::getOuterLocStart() const { return getInnerLocStart(); }

SourceRange TagDecl::getSourceRange() const {
  SourceLocation RBraceLoc = BraceRange.getEnd();
  SourceLocation E = RBraceLoc.isValid() ? RBraceLoc : getLocation();
  return SourceRange(getOuterLocStart(), E);
}

TagDecl *TagDecl::getCanonicalDecl() { return getFirstDecl(); }

void TagDecl::setTypedefNameForAnonDecl(TypedefNameDecl *TDD) {
  TypedefNameDeclOrQualifier = TDD;
  if (const Type *T = getTypeForDecl()) {
    (void)T;
    assert(T->isLinkageValid());
  }
  assert(isLinkageValid());
}

void TagDecl::startDefinition() { setBeingDefined(true); }

void TagDecl::completeDefinition() {
  setCompleteDefinition(true);
  setBeingDefined(false);

  if (TreeMutationListener *L = getTreeMutationListener())
    L->CompletedTagDefinition(this);
}

TagDecl *TagDecl::getDefinition() const {
  if (isCompleteDefinition())
    return const_cast<TagDecl *>(this);

  // If it's possible for us to have an out-of-date definition, check now.

  for (auto *R : redecls())
    if (R->isCompleteDefinition())
      return R;

  return nullptr;
}

void TagDecl::printName(llvm::raw_ostream &OS,
                        const PrintingPolicy &Policy) const {
  DeclarationName Name = getDeclName();
  // If the name is supposed to have an identifier but does not have one, then
  // the tag is anonymous and we should print it differently.
  if (!Name.getAsIdentifierInfo()) {
    // If the caller wanted to print a qualified name, they've already printed
    // the scope. And if the caller doesn't want that, the scope information
    // is already printed as part of the type.
    PrintingPolicy Copy(Policy);
    Copy.SuppressScope = true;
    getTreeContext().getTagDeclType(this).print(OS, Copy);
    return;
  }
  // Otherwise, do the normal printing.
  Name.print(OS, Policy);
}

EnumDecl::EnumDecl(TreeContext &C, DeclContext *DC, SourceLocation StartLoc,
                   SourceLocation IdLoc, IdentifierInfo *Id, EnumDecl *PrevDecl,
                   bool Fixed)
    : TagDecl(Enum, TagTypeKind::Enum, C, DC, IdLoc, Id, PrevDecl, StartLoc) {
  IntegerType = nullptr;
  setNumPositiveBits(0);
  setNumNegativeBits(0);
  setFixed(Fixed);
}

void EnumDecl::anchor() {}

EnumDecl *EnumDecl::Create(TreeContext &C, DeclContext *DC,
                           SourceLocation StartLoc, SourceLocation IdLoc,
                           IdentifierInfo *Id, EnumDecl *PrevDecl,
                           bool IsFixed) {
  auto *Enum =
      new (C, DC) EnumDecl(C, DC, StartLoc, IdLoc, Id, PrevDecl, IsFixed);
  C.getTypeDeclType(Enum, PrevDecl);
  return Enum;
}

SourceRange EnumDecl::getIntegerTypeRange() const {
  if (const TypeSourceInfo *TI = getIntegerTypeSourceInfo())
    return TI->getTypeLoc().getSourceRange();
  return SourceRange();
}

void EnumDecl::completeDefinition(QualType NewType, QualType NewPromotionType,
                                  unsigned NumPositiveBits,
                                  unsigned NumNegativeBits) {
  assert(!isCompleteDefinition() && "Cannot redefine enums!");
  if (!IntegerType)
    IntegerType = NewType.getTypePtr();
  PromotionType = NewPromotionType;
  setNumPositiveBits(NumPositiveBits);
  setNumNegativeBits(NumNegativeBits);
  TagDecl::completeDefinition();
}

bool EnumDecl::isClosed() const {
  if (const auto *A = getAttr<EnumExtensibilityAttr>())
    return A->getExtensibility() == EnumExtensibilityAttr::Closed;
  return true;
}

bool EnumDecl::isClosedFlag() const {
  return isClosed() && hasAttr<FlagEnumAttr>();
}

bool EnumDecl::isClosedNonFlag() const {
  return isClosed() && !hasAttr<FlagEnumAttr>();
}

SourceRange EnumDecl::getSourceRange() const {
  auto Res = TagDecl::getSourceRange();
  // Set end-point to enum-base, e.g. enum foo : ^bar
  if (auto *TSI = getIntegerTypeSourceInfo()) {
    // TagDecl doesn't know about the enum base.
    if (!getBraceRange().getEnd().isValid())
      Res.setEnd(TSI->getTypeLoc().getEndLoc());
  }
  return Res;
}

void EnumDecl::getValueRange(llvm::APInt &Max, llvm::APInt &Min) const {
  unsigned Bitwidth = getTreeContext().getIntWidth(getIntegerType());
  unsigned NumNegativeBits = getNumNegativeBits();
  unsigned NumPositiveBits = getNumPositiveBits();

  if (NumNegativeBits) {
    unsigned NumBits = std::max(NumNegativeBits, NumPositiveBits + 1);
    Max = llvm::APInt(Bitwidth, 1) << (NumBits - 1);
    Min = -Max;
  } else {
    Max = llvm::APInt(Bitwidth, 1) << NumPositiveBits;
    Min = llvm::APInt::getZero(Bitwidth);
  }
}

RecordDecl::RecordDecl(Kind DK, TagKind TK, const TreeContext &C,
                       DeclContext *DC, SourceLocation StartLoc,
                       SourceLocation IdLoc, IdentifierInfo *Id,
                       RecordDecl *PrevDecl)
    : TagDecl(DK, TK, C, DC, IdLoc, Id, PrevDecl, StartLoc) {
  assert(classof(static_cast<Decl *>(this)) && "Invalid Kind!");
  setHasFlexibleArrayMember(false);
  setAnonymousStructOrUnion(false);
  setHasVolatileMember(false);
  setNonTrivialToPrimitiveDefaultInitialize(false);
  setNonTrivialToPrimitiveCopy(false);
  setNonTrivialToPrimitiveDestroy(false);
  setHasNonTrivialToPrimitiveDefaultInitializeCUnion(false);
  setHasNonTrivialToPrimitiveDestructCUnion(false);
  setHasNonTrivialToPrimitiveCopyCUnion(false);
  setIsRandomized(false);
}

RecordDecl *RecordDecl::Create(const TreeContext &C, TagKind TK,
                               DeclContext *DC, SourceLocation StartLoc,
                               SourceLocation IdLoc, IdentifierInfo *Id,
                               RecordDecl *PrevDecl) {
  RecordDecl *R =
      new (C, DC) RecordDecl(Record, TK, C, DC, StartLoc, IdLoc, Id, PrevDecl);
  C.getTypeDeclType(R, PrevDecl);
  return R;
}

bool RecordDecl::isOrContainsUnion() const {
  if (isUnion())
    return true;

  if (const RecordDecl *Def = getDefinition()) {
    for (const FieldDecl *FD : Def->fields()) {
      const RecordType *RT = FD->getType()->getAs<RecordType>();
      if (RT && RT->getDecl()->isOrContainsUnion())
        return true;
    }
  }

  return false;
}

RecordDecl::field_iterator RecordDecl::field_begin() const {
  if (RecordDecl *D = getDefinition(); D && D != this)
    return D->field_begin();
  return field_iterator(decl_iterator(FirstDecl));
}

void RecordDecl::completeDefinition() {
  assert(!isCompleteDefinition() && "Cannot redefine record!");
  TagDecl::completeDefinition();
}

bool RecordDecl::isMsStruct(const TreeContext &C) const {
  return hasAttr<MSStructAttr>() || C.getLangOpts().MSBitfields == 1;
}

void RecordDecl::reorderDecls(const llvm::SmallVectorImpl<Decl *> &Decls) {
  std::tie(FirstDecl, LastDecl) = DeclContext::FormDeclChain(Decls, false);
  LastDecl->NextInContext = nullptr;
  setIsRandomized(true);
}

const FieldDecl *RecordDecl::findFirstNamedDataMember() const {
  for (const auto *I : fields()) {
    if (I->getIdentifier())
      return I;

    if (const auto *RT = I->getType()->getAs<RecordType>())
      if (const FieldDecl *NamedDataMember =
              RT->getDecl()->findFirstNamedDataMember())
        return NamedDataMember;
  }

  // We didn't find a named data member.
  return nullptr;
}

// Other Decl Allocation/Deallocation Method Implementations

void TranslationUnitDecl::anchor() {}

TranslationUnitDecl *TranslationUnitDecl::Create(TreeContext &C) {
  return new (C, (DeclContext *)nullptr) TranslationUnitDecl(C);
}

void PragmaCommentDecl::anchor() {}

PragmaCommentDecl *PragmaCommentDecl::Create(const TreeContext &C,
                                             TranslationUnitDecl *DC,
                                             SourceLocation CommentLoc,
                                             PragmaMSCommentKind CommentKind,
                                             llvm::StringRef Arg) {
  PragmaCommentDecl *PCD =
      new (C, DC, additionalSizeToAlloc<char>(Arg.size() + 1))
          PragmaCommentDecl(DC, CommentLoc, CommentKind);
  memcpy(PCD->getTrailingObjects<char>(), Arg.data(), Arg.size());
  PCD->getTrailingObjects<char>()[Arg.size()] = '\0';
  return PCD;
}

void PragmaDetectMismatchDecl::anchor() {}

PragmaDetectMismatchDecl *
PragmaDetectMismatchDecl::Create(const TreeContext &C, TranslationUnitDecl *DC,
                                 SourceLocation Loc, llvm::StringRef Name,
                                 llvm::StringRef Value) {
  size_t ValueStart = Name.size() + 1;
  PragmaDetectMismatchDecl *PDMD =
      new (C, DC, additionalSizeToAlloc<char>(ValueStart + Value.size() + 1))
          PragmaDetectMismatchDecl(DC, Loc, ValueStart);
  memcpy(PDMD->getTrailingObjects<char>(), Name.data(), Name.size());
  PDMD->getTrailingObjects<char>()[Name.size()] = '\0';
  memcpy(PDMD->getTrailingObjects<char>() + ValueStart, Value.data(),
         Value.size());
  PDMD->getTrailingObjects<char>()[ValueStart + Value.size()] = '\0';
  return PDMD;
}

void ExternCContextDecl::anchor() {}

ExternCContextDecl *ExternCContextDecl::Create(const TreeContext &C,
                                               TranslationUnitDecl *DC) {
  return new (C, DC) ExternCContextDecl(DC);
}

void LabelDecl::anchor() {}

LabelDecl *LabelDecl::Create(TreeContext &C, DeclContext *DC,
                             SourceLocation IdentL, IdentifierInfo *II) {
  return new (C, DC) LabelDecl(DC, IdentL, II, nullptr, IdentL);
}

LabelDecl *LabelDecl::Create(TreeContext &C, DeclContext *DC,
                             SourceLocation IdentL, IdentifierInfo *II,
                             SourceLocation GnuLabelL) {
  assert(GnuLabelL != IdentL && "Use this only for GNU local labels");
  return new (C, DC) LabelDecl(DC, IdentL, II, nullptr, GnuLabelL);
}

void LabelDecl::setMSAsmLabel(llvm::StringRef Name) {
  char *Buffer = new (getTreeContext(), 1) char[Name.size() + 1];
  memcpy(Buffer, Name.data(), Name.size());
  Buffer[Name.size()] = '\0';
  MSAsmName = Buffer;
}

void ValueDecl::anchor() {}

bool ValueDecl::isWeak() const {
  auto *MostRecent = getMostRecentDecl();
  return MostRecent->hasAttr<WeakAttr>() ||
         MostRecent->hasAttr<WeakRefAttr>() || isWeakImported();
}

void ImplicitParamDecl::anchor() {}

ImplicitParamDecl *ImplicitParamDecl::Create(TreeContext &C, DeclContext *DC,
                                             SourceLocation IdLoc,
                                             IdentifierInfo *Id,
                                             QualType Type) {
  return new (C, DC) ImplicitParamDecl(C, DC, IdLoc, Id, Type);
}

ImplicitParamDecl *ImplicitParamDecl::Create(TreeContext &C, QualType Type) {
  return new (C, nullptr) ImplicitParamDecl(C, Type);
}

FunctionDecl *
FunctionDecl::Create(TreeContext &C, DeclContext *DC, SourceLocation StartLoc,
                     const DeclarationNameInfo &NameInfo, QualType T,
                     TypeSourceInfo *TInfo, StorageClass SC, bool UsesFPIntrin,
                     bool isInlineSpecified, bool hasWrittenPrototype,
                     ConstexprSpecKind ConstexprKind) {
  FunctionDecl *New = new (C, DC)
      FunctionDecl(Function, C, DC, StartLoc, NameInfo, T, TInfo, SC,
                   UsesFPIntrin, isInlineSpecified, ConstexprKind);
  New->setHasWrittenPrototype(hasWrittenPrototype);
  return New;
}

EnumConstantDecl *EnumConstantDecl::Create(TreeContext &C, EnumDecl *CD,
                                           SourceLocation L, IdentifierInfo *Id,
                                           QualType T, Expr *E,
                                           const llvm::APSInt &V) {
  return new (C, CD) EnumConstantDecl(CD, L, Id, T, E, V);
}

void IndirectFieldDecl::anchor() {}

IndirectFieldDecl::IndirectFieldDecl(TreeContext &C, DeclContext *DC,
                                     SourceLocation L, DeclarationName N,
                                     QualType T,
                                     llvm::MutableArrayRef<NamedDecl *> CH)
    : ValueDecl(IndirectField, DC, L, N, T), Chaining(CH.data()),
      ChainingSize(CH.size()) {
  (void)C;
}

IndirectFieldDecl *
IndirectFieldDecl::Create(TreeContext &C, DeclContext *DC, SourceLocation L,
                          IdentifierInfo *Id, QualType T,
                          llvm::MutableArrayRef<NamedDecl *> CH) {
  return new (C, DC) IndirectFieldDecl(C, DC, L, Id, T, CH);
}

SourceRange EnumConstantDecl::getSourceRange() const {
  SourceLocation End = getLocation();
  if (Init)
    End = Init->getEndLoc();
  return SourceRange(getLocation(), End);
}

void TypeDecl::anchor() {}

TypedefDecl *TypedefDecl::Create(TreeContext &C, DeclContext *DC,
                                 SourceLocation StartLoc, SourceLocation IdLoc,
                                 IdentifierInfo *Id, TypeSourceInfo *TInfo) {
  return new (C, DC) TypedefDecl(C, DC, StartLoc, IdLoc, Id, TInfo);
}

void TypedefNameDecl::anchor() {}

TagDecl *TypedefNameDecl::getAnonDeclWithTypedefName(bool AnyRedecl) const {
  if (auto *TT = getTypeSourceInfo()->getType()->getAs<TagType>()) {
    auto *OwningTypedef = TT->getDecl()->getTypedefNameForAnonDecl();
    auto *ThisTypedef = this;
    if (AnyRedecl && OwningTypedef) {
      OwningTypedef = OwningTypedef->getCanonicalDecl();
      ThisTypedef = ThisTypedef->getCanonicalDecl();
    }
    if (OwningTypedef == ThisTypedef)
      return TT->getDecl();
  }

  return nullptr;
}

bool TypedefNameDecl::isTransparentTagSlow() const {
  auto determineIsTransparent = [&]() {
    if (auto *TT = getUnderlyingType()->getAs<TagType>()) {
      if (auto *TD = TT->getDecl()) {
        if (TD->getName() != getName())
          return false;
        SourceLocation TTLoc = getLocation();
        SourceLocation TDLoc = TD->getLocation();
        if (!TTLoc.isMacroID() || !TDLoc.isMacroID())
          return false;
        SourceManager &SM = getTreeContext().getSourceManager();
        return SM.getSpellingLoc(TTLoc) == SM.getSpellingLoc(TDLoc);
      }
    }
    return false;
  };

  bool isTransparent = determineIsTransparent();
  MaybeModedTInfo.setInt((isTransparent << 1) | 1);
  return isTransparent;
}

SourceRange TypedefDecl::getSourceRange() const {
  SourceLocation RangeEnd = getLocation();
  if (TypeSourceInfo *TInfo = getTypeSourceInfo()) {
    if (typeIsPostfix(TInfo->getType()))
      RangeEnd = TInfo->getTypeLoc().getSourceRange().getEnd();
  }
  return SourceRange(getBeginLoc(), RangeEnd);
}

void FileScopeAsmDecl::anchor() {}

FileScopeAsmDecl *FileScopeAsmDecl::Create(TreeContext &C, DeclContext *DC,
                                           StringLiteral *Str,
                                           SourceLocation AsmLoc,
                                           SourceLocation RParenLoc) {
  return new (C, DC) FileScopeAsmDecl(DC, Str, AsmLoc, RParenLoc);
}

void EmptyDecl::anchor() {}

EmptyDecl *EmptyDecl::Create(TreeContext &C, DeclContext *DC,
                             SourceLocation L) {
  return new (C, DC) EmptyDecl(DC, L);
}

bool neverc::analyze_os_log::computeOSLogBufferLayout(
    TreeContext &Ctx, const CallExpr *E, OSLogBufferLayout &Layout) {
  return false;
}

LLVM_DUMP_METHOD void Decl::dump() const { dump(llvm::errs()); }

LLVM_DUMP_METHOD void Decl::dump(llvm::raw_ostream &OS) const {
  OS << "<dump unavailable>\n";
}

LLVM_DUMP_METHOD void DeclContext::dumpLookups(llvm::raw_ostream &OS,
                                               bool) const {
  OS << "<dump unavailable>\n";
}

LLVM_DUMP_METHOD void Stmt::dump() const {
  llvm::errs() << "<dump unavailable>\n";
}

LLVM_DUMP_METHOD void Stmt::dump(llvm::raw_ostream &OS,
                                 const TreeContext &) const {
  OS << "<dump unavailable>\n";
}

LLVM_DUMP_METHOD void Type::dump() const {
  llvm::errs() << "<dump unavailable>\n";
}

LLVM_DUMP_METHOD void Type::dump(llvm::raw_ostream &OS,
                                 const TreeContext &) const {
  OS << "<dump unavailable>\n";
}

LLVM_DUMP_METHOD void QualType::dump() const {
  llvm::errs() << "<dump unavailable>\n";
}

LLVM_DUMP_METHOD void QualType::dump(llvm::raw_ostream &OS,
                                     const TreeContext &) const {
  OS << "<dump unavailable>\n";
}
