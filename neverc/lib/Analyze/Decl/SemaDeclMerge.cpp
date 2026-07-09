//===- SemaDeclMerge.cpp - Builtin creation & declaration merging ---------===//

#include "SemaDeclUtils.h"
#include "Type/TypeLocBuilder.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Builtin/BuiltinString.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/IncludeResolver.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/SourceScanner.h"
#include "neverc/Tree/Core/CharUnits.h"
#include "neverc/Tree/Core/TreeConsumer.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <optional>
#include <unordered_map>

using namespace neverc;
using namespace sema;

// ===----------------------------------------------------------------------===
// Builtin creation & declaration merging
// ===----------------------------------------------------------------------===

FunctionDecl *Sema::CreateBuiltin(IdentifierInfo *II, QualType Type,
                                  unsigned ID, SourceLocation Loc) {
  DeclContext *Parent = Context.getTranslationUnitDecl();

  FunctionDecl *New = FunctionDecl::Create(Context, Parent, Loc, Loc, II, Type,
                                           /*TInfo=*/nullptr, SC_Extern,
                                           getCurFPFeatures().isFPConstrained(),
                                           false, Type->isFunctionProtoType());
  New->setImplicit();
  New->addAttr(BuiltinAttr::CreateImplicit(Context, ID));

  if (const FunctionProtoType *FT = dyn_cast<FunctionProtoType>(Type)) {
    llvm::SmallVector<ParmVarDecl *, 16> Params;
    for (unsigned i = 0, e = FT->getNumParams(); i != e; ++i) {
      ParmVarDecl *parm = ParmVarDecl::Create(
          Context, New, SourceLocation(), SourceLocation(), nullptr,
          FT->getParamType(i), /*TInfo=*/nullptr, SC_None, nullptr);
      parm->setScopeInfo(0, i);
      Params.push_back(parm);
    }
    New->setParams(Params);
  }

  AddKnownFunctionAttributes(New);
  return New;
}

NamedDecl *Sema::LazilyCreateBuiltin(IdentifierInfo *II, unsigned ID, Scope *S,
                                     bool ForRedeclaration,
                                     SourceLocation Loc) {
  TreeContext::GetBuiltinTypeError Error;
  QualType R = Context.GetBuiltinType(ID, Error);
  if (Error) {
    if (!ForRedeclaration)
      return nullptr;

    // If we have a builtin without an associated type we should not emit a
    // warning when we were not able to find a type for it.
    if (Error == TreeContext::GE_Missing_type ||
        Context.BuiltinInfo.allowTypeMismatch(ID))
      return nullptr;

    // If we could not find a type for setjmp it is because the jmp_buf type was
    // not defined prior to the setjmp declaration.
    if (Error == TreeContext::GE_Missing_setjmp) {
      Diag(Loc, diag::warn_implicit_decl_no_jmp_buf)
          << Context.BuiltinInfo.getName(ID);
      return nullptr;
    }

    // Generally, we emit a warning that the declaration requires the
    // appropriate header.
    Diag(Loc, diag::warn_implicit_decl_requires_sysheader)
        << getHeaderName(Context.BuiltinInfo, ID, Error)
        << Context.BuiltinInfo.getName(ID);
    return nullptr;
  }
  // NeverC silently accepts predefined libc functions referenced without
  // the matching `#include` so first-touch programs and out-of-order
  // intra-TU calls compile cleanly.  The linker resolves the implicit
  // declaration to the real libc symbol of the same name.
  if (R.isNull())
    return nullptr;

  FunctionDecl *New = CreateBuiltin(II, R, ID, Loc);
  RegisterLocallyScopedExternCDecl(New, S);

  // TUScope is the translation-unit scope to insert this function into.
  // We need to teach PushOnScopeChains to
  // relate Scopes to DeclContexts, and probably eliminate CurContext
  // entirely, but we're not there yet.
  DeclContext *SavedContext = CurContext;
  CurContext = New->getDeclContext();
  PushOnScopeChains(New, TUScope);
  CurContext = SavedContext;
  return New;
}

bool Sema::isIncompatibleTypedef(TypeDecl *Old, TypedefNameDecl *New) {
  QualType OldType;
  if (TypedefNameDecl *OldTypedef = dyn_cast<TypedefNameDecl>(Old))
    OldType = OldTypedef->getUnderlyingType();
  else
    OldType = Context.getTypeDeclType(Old);
  QualType NewType = New->getUnderlyingType();

  if (NewType->isVariablyModifiedType()) {
    // Must not redefine a typedef with a variably-modified type.
    Diag(New->getLocation(), diag::err_redefinition_variably_modified_typedef)
        << NewType;
    if (Old->getLocation().isValid())
      notePreviousDefinition(Old, New->getLocation());
    New->setInvalidDecl();
    return true;
  }

  if (OldType != NewType && !Context.hasSameType(OldType, NewType)) {
    Diag(New->getLocation(), diag::err_redefinition_different_typedef)
        << NewType << OldType;
    if (Old->getLocation().isValid())
      notePreviousDefinition(Old, New->getLocation());
    New->setInvalidDecl();
    return true;
  }
  return false;
}

void Sema::MergeTypedefNameDecl(Scope *S, TypedefNameDecl *New,
                                LookupResult &OldDecls) {
  // If the new decl is known invalid already, don't bother doing any
  // merging checks.
  if (New->isInvalidDecl())
    return;

  TypeDecl *Old = OldDecls.getAsSingle<TypeDecl>();
  if (!Old) {
    Diag(New->getLocation(), diag::err_redefinition_different_kind)
        << New->getDeclName();

    NamedDecl *OldD = OldDecls.getRepresentativeDecl();
    if (OldD->getLocation().isValid())
      notePreviousDefinition(OldD, New->getLocation());

    return New->setInvalidDecl();
  }

  // If the old declaration is invalid, just give up here.
  if (Old->isInvalidDecl())
    return New->setInvalidDecl();

  if (auto *OldTD = dyn_cast<TypedefNameDecl>(Old)) {
    auto *OldTag = OldTD->getAnonDeclWithTypedefName(/*AnyRedecl*/ true);
    auto *NewTag = New->getAnonDeclWithTypedefName();
    // The BuiltinString prelude planted typedefs (e.g. `typedef struct
    // __neverc_string { ... } string;`) into the <built-in> predefines
    // buffer.  When the user source redefines the same typedef name we
    // have to redirect the new typedef onto the pre-scan tag — otherwise
    // a function call whose return type resolved against the prelude
    // tag would look `incompatible` with a variable typed against the
    // user-source tag even though both describe the same record.
    bool BuiltinOldTypedef =
        Context.getSourceManager().isWrittenInBuiltinFile(OldTD->getLocation());
    if (OldTag && NewTag &&
        OldTag->getCanonicalDecl() != NewTag->getCanonicalDecl() &&
        BuiltinOldTypedef) {
      // The old decl is a synthetic pre-decl.  Reuse the old typedef's
      // type so both decls describe the same entity.
      New->setTypeForDecl(OldTD->getTypeForDecl());
      if (OldTD->isModed())
        New->setModedTypeSourceInfo(OldTD->getTypeSourceInfo(),
                                    OldTD->getUnderlyingType());
      else
        New->setTypeSourceInfo(OldTD->getTypeSourceInfo());

      // If this was an enumeration, yank all of its enumerators
      // out of the scope.
      if (isa<EnumDecl>(NewTag)) {
        Scope *EnumScope = getNonFieldDeclScope(S);
        for (auto *D : NewTag->decls()) {
          auto *ED = cast<EnumConstantDecl>(D);
          assert(EnumScope->isDeclScope(ED));
          EnumScope->RemoveDecl(ED);
          IdResolver.RemoveDecl(ED);
          ED->getLexicalDeclContext()->removeDecl(ED);
        }
      }
    } else if (BuiltinOldTypedef) {
      // The pre-scan already supplied a compatible typedef (e.g. a pure
      // typedef without a record body).  Just chain the redeclaration
      // without any compatibility diagnostic.
      New->setPreviousDecl(OldTD);
      return;
    }
  }

  // If the typedef types are not identical, reject them in all languages and
  // with any extensions enabled.
  if (isIncompatibleTypedef(Old, New))
    return;

  // The types match.  Link up the redeclaration chain and merge attributes if
  // the old declaration was a typedef.
  if (TypedefNameDecl *Typedef = dyn_cast<TypedefNameDecl>(Old)) {
    New->setPreviousDecl(Typedef);
    mergeDeclAttributes(New, Old);
  }

  if (getLangOpts().MicrosoftExt)
    return;

  // C11 always permits redefinition of typedefs.
  if (getLangOpts().C11)
    return;

  // If we have a redefinition of a typedef in C, emit a warning.  This warning
  // is normally mapped to an error, but can be controlled with
  // -Wtypedef-redefinition.  If either the original or the redefinition is
  // in a system header, don't emit this for compatibility with GCC.
  if (getDiagnostics().getSuppressSystemWarnings() &&
      // Some standard types are defined implicitly in NeverC.
      (Old->isImplicit() ||
       Context.getSourceManager().isInSystemHeader(Old->getLocation()) ||
       Context.getSourceManager().isInSystemHeader(New->getLocation())))
    return;

  Diag(New->getLocation(), diag::ext_redefinition_of_typedef)
      << New->getDeclName();
  notePreviousDefinition(Old, New->getLocation());
}

namespace neverc {
bool hasMatchingAttr(const Decl *D, const Attr *A) {
  const AnnotateAttr *Ann = dyn_cast<AnnotateAttr>(A);
  for (const auto *i : D->attrs())
    if (i->getKind() == A->getKind()) {
      if (Ann) {
        if (Ann->getAnnotation() == cast<AnnotateAttr>(i)->getAnnotation())
          return true;
        continue;
      }
      return true;
    }

  return false;
}
} // namespace neverc

namespace {
bool isAttrTargetDefinition(Decl *D) {
  if (VarDecl *VD = dyn_cast<VarDecl>(D))
    return VD->isThisDeclarationADefinition();
  if (TagDecl *TD = dyn_cast<TagDecl>(D))
    return TD->isCompleteDefinition() || TD->isBeingDefined();
  return true;
}
} // namespace

namespace {
bool mergeAlignedAttrs(Sema &S, NamedDecl *New, Decl *Old) {
  // Look for alignas attributes on Old, and pick out whichever attribute
  // specifies the strictest alignment requirement.
  AlignedAttr *OldAlignasAttr = nullptr;
  AlignedAttr *OldStrictestAlignAttr = nullptr;
  unsigned OldAlign = 0;
  for (auto *I : Old->specific_attrs<AlignedAttr>()) {
    if (I->isAlignmentDependent())
      return false;

    if (I->isAlignas())
      OldAlignasAttr = I;

    unsigned Align = I->getAlignment(S.Context);
    if (Align > OldAlign) {
      OldAlign = Align;
      OldStrictestAlignAttr = I;
    }
  }

  // Look for alignas attributes on New.
  AlignedAttr *NewAlignasAttr = nullptr;
  unsigned NewAlign = 0;
  for (auto *I : New->specific_attrs<AlignedAttr>()) {
    if (I->isAlignmentDependent())
      return false;

    if (I->isAlignas())
      NewAlignasAttr = I;

    unsigned Align = I->getAlignment(S.Context);
    if (Align > NewAlign)
      NewAlign = Align;
  }

  if (OldAlignasAttr && NewAlignasAttr && OldAlign != NewAlign) {
    // Both declarations have 'alignas' attributes. We require them to match
    // (per alignment consistency rules for multiple declarations).

    // If either declaration only contains 'alignas(0)' specifiers, then it
    // specifies the natural alignment for the type.
    if (OldAlign == 0 || NewAlign == 0) {
      QualType Ty;
      if (ValueDecl *VD = dyn_cast<ValueDecl>(New))
        Ty = VD->getType();
      else
        Ty = S.Context.getTagDeclType(cast<TagDecl>(New));

      if (OldAlign == 0)
        OldAlign = S.Context.getTypeAlign(Ty);
      if (NewAlign == 0)
        NewAlign = S.Context.getTypeAlign(Ty);
    }

    if (OldAlign != NewAlign) {
      S.Diag(NewAlignasAttr->getLocation(), diag::err_alignas_mismatch)
          << (unsigned)S.Context.toCharUnitsFromBits(OldAlign).getQuantity()
          << (unsigned)S.Context.toCharUnitsFromBits(NewAlign).getQuantity();
      S.Diag(OldAlignasAttr->getLocation(), diag::note_previous_declaration);
    }
  }

  if (OldAlignasAttr && !NewAlignasAttr && isAttrTargetDefinition(New)) {
    // Definition must carry alignas when any declaration did (and vice versa).
    S.Diag(New->getLocation(), diag::err_alignas_missing_on_definition)
        << OldAlignasAttr;
    S.Diag(OldAlignasAttr->getLocation(), diag::note_alignas_on_declaration)
        << OldAlignasAttr;
  }

  bool AnyAdded = false;

  // Ensure we have an attribute representing the strictest alignment.
  if (OldAlign > NewAlign) {
    AlignedAttr *Clone = OldStrictestAlignAttr->clone(S.Context);
    Clone->setInherited(true);
    New->addAttr(Clone);
    AnyAdded = true;
  }

  // Ensure we have an alignas attribute if the old declaration had one.
  if (OldAlignasAttr && !NewAlignasAttr &&
      !(AnyAdded && OldStrictestAlignAttr->isAlignas())) {
    AlignedAttr *Clone = OldAlignasAttr->clone(S.Context);
    Clone->setInherited(true);
    New->addAttr(Clone);
    AnyAdded = true;
  }

  return AnyAdded;
}
} // namespace

#define WANT_DECL_MERGE_LOGIC
#include "neverc/Analyze/AttrParsedAttrImpl.td.h"
#undef WANT_DECL_MERGE_LOGIC

namespace {
bool mergeDeclAttribute(Sema &S, NamedDecl *D, const InheritableAttr *Attr,
                        Sema::AvailabilityMergeKind AMK) {
  // Diagnose any mutual exclusions between the attribute that we want to add
  // and attributes that already exist on the declaration.
  if (!DiagnoseMutualExclusions(S, D, Attr))
    return false;

  // This function copies an attribute Attr from a previous declaration to the
  // new declaration D if the new declaration doesn't itself have that attribute
  // yet or if that attribute allows duplicates.
  // If you're adding a new attribute that requires logic different from
  // "use explicit attribute on decl if present, else use attribute from
  // previous decl", for example if the attribute needs to be consistent
  // between redeclarations, you need to call a custom merge function here.
  InheritableAttr *NewAttr = nullptr;
  if (const auto *AA = dyn_cast<AvailabilityAttr>(Attr))
    NewAttr = S.mergeAvailabilityAttr(
        D, *AA, AA->getPlatform(), AA->isImplicit(), AA->getIntroduced(),
        AA->getDeprecated(), AA->getObsoleted(), AA->getUnavailable(),
        AA->getMessage(), AA->getStrict(), AA->getReplacement(),
        AA->getPriority());
  else if (const auto *VA = dyn_cast<VisibilityAttr>(Attr))
    NewAttr = S.mergeVisibilityAttr(D, *VA, VA->getVisibility());
  else if (const auto *VA = dyn_cast<TypeVisibilityAttr>(Attr))
    NewAttr = S.mergeTypeVisibilityAttr(D, *VA, VA->getVisibility());
  else if (const auto *ImportA = dyn_cast<DLLImportAttr>(Attr))
    NewAttr = S.mergeDLLImportAttr(D, *ImportA);
  else if (const auto *ExportA = dyn_cast<DLLExportAttr>(Attr))
    NewAttr = S.mergeDLLExportAttr(D, *ExportA);
  else if (const auto *EA = dyn_cast<ErrorAttr>(Attr))
    NewAttr = S.mergeErrorAttr(D, *EA, EA->getUserDiagnostic());
  else if (const auto *FA = dyn_cast<FormatAttr>(Attr))
    NewAttr = S.mergeFormatAttr(D, *FA, FA->getType(), FA->getFormatIdx(),
                                FA->getFirstArg());
  else if (const auto *SA = dyn_cast<SectionAttr>(Attr))
    NewAttr = S.mergeSectionAttr(D, *SA, SA->getName());
  else if (const auto *CSA = dyn_cast<CodeSegAttr>(Attr))
    NewAttr = S.mergeCodeSegAttr(D, *CSA, CSA->getName());
  else if (const auto *AA = dyn_cast<AlwaysInlineAttr>(Attr))
    NewAttr = S.mergeAlwaysInlineAttr(D, *AA,
                                      &S.Context.Idents.get(AA->getSpelling()));
  else if (const auto *MA = dyn_cast<MinSizeAttr>(Attr))
    NewAttr = S.mergeMinSizeAttr(D, *MA);
  else if (const auto *OA = dyn_cast<OptimizeNoneAttr>(Attr))
    NewAttr = S.mergeOptimizeNoneAttr(D, *OA);
  else if (const auto *InternalLinkageA = dyn_cast<InternalLinkageAttr>(Attr))
    NewAttr = S.mergeInternalLinkageAttr(D, *InternalLinkageA);
  else if (isa<AlignedAttr>(Attr))
    // AlignedAttrs are handled separately, because we need to handle all
    // such attributes on a declaration at the same time.
    NewAttr = nullptr;
  else if (const auto *TCBA = dyn_cast<EnforceTCBAttr>(Attr))
    NewAttr = S.mergeEnforceTCBAttr(D, *TCBA);
  else if (const auto *TCBLA = dyn_cast<EnforceTCBLeafAttr>(Attr))
    NewAttr = S.mergeEnforceTCBLeafAttr(D, *TCBLA);
  else if (const auto *BTFA = dyn_cast<BTFDeclTagAttr>(Attr))
    NewAttr = S.mergeBTFDeclTagAttr(D, *BTFA);
  else if (Attr->shouldInheritEvenIfAlreadyPresent() ||
           !hasMatchingAttr(D, Attr))
    NewAttr = cast<InheritableAttr>(Attr->clone(S.Context));

  if (NewAttr) {
    NewAttr->setInherited(true);
    D->addAttr(NewAttr);
    return true;
  }

  return false;
}
} // namespace

namespace {
const NamedDecl *getDefinition(const Decl *D) {
  if (const TagDecl *TD = dyn_cast<TagDecl>(D))
    return TD->getDefinition();
  if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
    const VarDecl *Def = VD->getDefinition();
    if (Def)
      return Def;
    return VD->getActingDefinition();
  }
  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    const FunctionDecl *Def = nullptr;
    if (FD->isDefined(Def))
      return Def;
  }
  return nullptr;
}
} // namespace

namespace {
bool hasAttribute(const Decl *D, attr::Kind Kind) {
  for (const auto *Attribute : D->attrs())
    if (Attribute->getKind() == Kind)
      return true;
  return false;
}
} // namespace

namespace {
void checkNewAttributesAfterDef(Sema &S, Decl *New, const Decl *Old) {
  if (!New->hasAttrs())
    return;

  const NamedDecl *Def = getDefinition(Old);
  if (!Def || Def == New)
    return;

  AttrVec &NewAttributes = New->getAttrs();
  for (unsigned I = 0, E = NewAttributes.size(); I != E;) {
    const Attr *NewAttribute = NewAttributes[I];

    if (isa<AliasAttr>(NewAttribute) || isa<IFuncAttr>(NewAttribute)) {
      if (FunctionDecl *FD = dyn_cast<FunctionDecl>(New)) {
        Sema::SkipBodyInfo SkipBody;
        S.CheckForFunctionRedefinition(FD, cast<FunctionDecl>(Def), &SkipBody);

        // If we're skipping this definition, drop the "alias" attribute.
        if (SkipBody.ShouldSkip) {
          NewAttributes.erase(NewAttributes.begin() + I);
          --E;
          continue;
        }
      } else {
        VarDecl *VD = cast<VarDecl>(New);
        unsigned Diag = cast<VarDecl>(Def)->isThisDeclarationADefinition() ==
                                VarDecl::TentativeDefinition
                            ? diag::err_alias_after_tentative
                            : diag::err_redefinition;
        S.Diag(VD->getLocation(), Diag) << VD->getDeclName();
        if (Diag == diag::err_redefinition)
          S.notePreviousDefinition(Def, VD->getLocation());
        else
          S.Diag(Def->getLocation(), diag::note_previous_definition);
        VD->setInvalidDecl();
      }
      ++I;
      continue;
    }

    if (const VarDecl *VD = dyn_cast<VarDecl>(Def)) {
      // Tentative definitions are only interesting for the alias check above.
      if (VD->isThisDeclarationADefinition() != VarDecl::Definition) {
        ++I;
        continue;
      }
    }

    if (hasAttribute(Def, NewAttribute->getKind())) {
      ++I;
      continue; // regular attr merging will take care of validating this.
    }

    if (isa<C11NoReturnAttr>(NewAttribute)) {
      // C's _Noreturn is allowed to be added to a function after it is defined.
      ++I;
      continue;
    } else if (const AlignedAttr *AA = dyn_cast<AlignedAttr>(NewAttribute)) {
      if (AA->isAlignas()) {
        // C11 6.7.5/7: if the definition has no alignment specifier, other
        // declarations of that object shall also have none (alignas mismatch).
        S.Diag(Def->getLocation(), diag::err_alignas_missing_on_definition)
            << AA;
        S.Diag(NewAttribute->getLocation(), diag::note_alignas_on_declaration)
            << AA;
        NewAttributes.erase(NewAttributes.begin() + I);
        --E;
        continue;
      }
    } else if (isa<LoaderUninitializedAttr>(NewAttribute)) {
      // Redeclaration with this attribute conflicts with the prior definition.
      {
        S.Diag(NewAttribute->getLocation(),
               diag::err_loader_uninitialized_redeclaration);
        S.Diag(Def->getLocation(), diag::note_previous_definition);
        NewAttributes.erase(NewAttributes.begin() + I);
        --E;
        continue;
      }
    } else if (isa<SelectAnyAttr>(NewAttribute) &&
               cast<VarDecl>(New)->isInline() &&
               !cast<VarDecl>(New)->isInlineSpecified()) {
      // Don't warn about applying selectany to implicitly inline variables.
      // Older compilers and language modes would require the use of selectany
      // to make such variables inline, and it would have no effect if we
      // honored it.
      ++I;
      continue;
    }

    S.Diag(NewAttribute->getLocation(),
           diag::warn_attribute_precede_definition);
    S.Diag(Def->getLocation(), diag::note_previous_definition);
    NewAttributes.erase(NewAttributes.begin() + I);
    --E;
  }
}
} // namespace

void Sema::mergeDeclAttributes(NamedDecl *New, Decl *Old,
                               AvailabilityMergeKind AMK) {
  if (UsedAttr *OldAttr = Old->getMostRecentDecl()->getAttr<UsedAttr>()) {
    UsedAttr *NewAttr = OldAttr->clone(Context);
    NewAttr->setInherited(true);
    New->addAttr(NewAttr);
  }
  if (RetainAttr *OldAttr = Old->getMostRecentDecl()->getAttr<RetainAttr>()) {
    RetainAttr *NewAttr = OldAttr->clone(Context);
    NewAttr->setInherited(true);
    New->addAttr(NewAttr);
  }

  if (!Old->hasAttrs() && !New->hasAttrs())
    return;

  // Attributes declared post-definition are currently ignored.
  checkNewAttributesAfterDef(*this, New, Old);

  if (AsmLabelAttr *NewA = New->getAttr<AsmLabelAttr>()) {
    if (AsmLabelAttr *OldA = Old->getAttr<AsmLabelAttr>()) {
      if (!OldA->isEquivalent(NewA)) {
        // This redeclaration changes __asm__ label.
        Diag(New->getLocation(), diag::err_different_asm_label);
        Diag(OldA->getLocation(), diag::note_previous_declaration);
      }
    } else if (Old->isUsed()) {
      // This redeclaration adds an __asm__ label to a declaration that has
      // already been ODR-used.
      Diag(New->getLocation(), diag::err_late_asm_label_name)
          << isa<FunctionDecl>(Old) << New->getAttr<AsmLabelAttr>()->getRange();
    }
  }

  // This redeclaration adds a section attribute.
  if (New->hasAttr<SectionAttr>() && !Old->hasAttr<SectionAttr>()) {
    if (auto *VD = dyn_cast<VarDecl>(New)) {
      if (VD->isThisDeclarationADefinition() == VarDecl::DeclarationOnly) {
        Diag(New->getLocation(), diag::warn_attribute_section_on_redeclaration);
        Diag(Old->getLocation(), diag::note_previous_declaration);
      }
    }
  }

  if (!Old->hasAttrs())
    return;

  bool foundAny = New->hasAttrs();

  // Ensure that any moving of objects within the allocated map is done before
  // we process them.
  if (!foundAny)
    New->setAttrs(AttrVec());

  for (auto *I : Old->specific_attrs<InheritableAttr>()) {
    // Ignore deprecated/unavailable/availability attributes if requested.
    AvailabilityMergeKind LocalAMK = AMK_None;
    if (isa<DeprecatedAttr>(I) || isa<UnavailableAttr>(I) ||
        isa<AvailabilityAttr>(I)) {
      switch (AMK) {
      case AMK_None:
        continue;

      case AMK_Redeclaration:
        LocalAMK = AMK;
        break;
      }
    }

    // Already handled.
    if (isa<UsedAttr>(I) || isa<RetainAttr>(I))
      continue;

    if (mergeDeclAttribute(*this, New, I, LocalAMK))
      foundAny = true;
  }

  if (mergeAlignedAttrs(*this, New, Old))
    foundAny = true;

  if (!foundAny)
    New->dropAttrs();
}

namespace neverc {
void mergeParamDeclAttributes(ParmVarDecl *newDecl, const ParmVarDecl *oldDecl,
                              Sema &S) {
  // If any declaration carries carries_dependency, the first must too.
  const CarriesDependencyAttr *CDA = newDecl->getAttr<CarriesDependencyAttr>();
  if (CDA && !oldDecl->hasAttr<CarriesDependencyAttr>()) {
    S.Diag(CDA->getLocation(),
           diag::err_carries_dependency_missing_on_first_decl)
        << 1 /*Param*/;
    const FunctionDecl *FirstFD =
        cast<FunctionDecl>(oldDecl->getDeclContext())->getFirstDecl();
    const ParmVarDecl *FirstVD =
        FirstFD->getParamDecl(oldDecl->getFunctionScopeIndex());
    S.Diag(FirstVD->getLocation(),
           diag::note_carries_dependency_missing_first_decl)
        << 1 /*Param*/;
  }

  if (!oldDecl->hasAttrs())
    return;

  bool foundAny = newDecl->hasAttrs();

  // Ensure that any moving of objects within the allocated map is
  // done before we process them.
  if (!foundAny)
    newDecl->setAttrs(AttrVec());

  for (const auto *I : oldDecl->specific_attrs<InheritableParamAttr>()) {
    if (!hasMatchingAttr(newDecl, I)) {
      InheritableAttr *newAttr =
          cast<InheritableParamAttr>(I->clone(S.Context));
      newAttr->setInherited(true);
      newDecl->addAttr(newAttr);
      foundAny = true;
    }
  }

  if (!foundAny)
    newDecl->dropAttrs();
}
} // namespace neverc

namespace neverc {
bool equivalentArrayTypes(QualType Old, QualType New, const TreeContext &Ctx) {

  auto NoSizeInfo = [&Ctx](QualType Ty) {
    if (Ty->isIncompleteArrayType() || Ty->isPointerType())
      return true;
    if (const auto *VAT = Ctx.getAsVariableArrayType(Ty))
      return VAT->getSizeModifier() == ArraySizeModifier::Star;
    return false;
  };

  // `type[]` is equivalent to `type *` and `type[*]`.
  if (NoSizeInfo(Old) && NoSizeInfo(New))
    return true;

  // Don't try to compare VLA sizes, unless one of them has the star modifier.
  if (Old->isVariableArrayType() && New->isVariableArrayType()) {
    const auto *OldVAT = Ctx.getAsVariableArrayType(Old);
    const auto *NewVAT = Ctx.getAsVariableArrayType(New);
    if ((OldVAT->getSizeModifier() == ArraySizeModifier::Star) ^
        (NewVAT->getSizeModifier() == ArraySizeModifier::Star))
      return false;
    return true;
  }

  // Only compare size, ignore Size modifiers and CVR.
  if (Old->isConstantArrayType() && New->isConstantArrayType()) {
    return Ctx.getAsConstantArrayType(Old)->getSize() ==
           Ctx.getAsConstantArrayType(New)->getSize();
  }

  return Old == New;
}
} // namespace neverc

namespace neverc {
void mergeParamDeclTypes(ParmVarDecl *NewParam, const ParmVarDecl *OldParam,
                         Sema &S) {
  if (auto Oldnullability = OldParam->getType()->getNullability()) {
    if (auto Newnullability = NewParam->getType()->getNullability()) {
      if (*Oldnullability != *Newnullability) {
        S.Diag(NewParam->getLocation(), diag::warn_mismatched_nullability_attr)
            << DiagNullabilityKind(*Newnullability,
                                   /*IsContextSensitive=*/false)
            << DiagNullabilityKind(*Oldnullability,
                                   /*IsContextSensitive=*/false);
        S.Diag(OldParam->getLocation(), diag::note_previous_declaration);
      }
    } else {
      QualType NewT = NewParam->getType();
      NewT = S.Context.getAttributedType(
          AttributedType::getNullabilityAttrKind(*Oldnullability), NewT, NewT);
      NewParam->setType(NewT);
    }
  }
  const auto *OldParamDT = dyn_cast<DecayedType>(OldParam->getType());
  const auto *NewParamDT = dyn_cast<DecayedType>(NewParam->getType());
  if (OldParamDT && NewParamDT &&
      OldParamDT->getPointeeType() == NewParamDT->getPointeeType()) {
    QualType OldParamOT = OldParamDT->getOriginalType();
    QualType NewParamOT = NewParamDT->getOriginalType();
    if (!equivalentArrayTypes(OldParamOT, NewParamOT, S.getTreeContext())) {
      S.Diag(NewParam->getLocation(), diag::warn_inconsistent_array_form)
          << NewParam << NewParamOT;
      S.Diag(OldParam->getLocation(), diag::note_previous_declaration_as)
          << OldParamOT;
    }
  }
}
} // namespace neverc

namespace neverc {

bool canRedefineFunction(const FunctionDecl *FD, const LangOptions &LangOpts) {
  return ((FD->hasAttr<GNUInlineAttr>() || LangOpts.GNUInline) &&
          FD->isInlineSpecified() && FD->getStorageClass() == SC_Extern);
}

} // namespace neverc

const AttributedType *Sema::getCallingConvAttributedType(QualType T) const {
  const AttributedType *AT = T->getAs<AttributedType>();
  while (AT && !AT->isCallingConv())
    AT = AT->getModifiedType()->getAs<AttributedType>();
  return AT;
}

namespace neverc {
bool hasIdenticalPassObjectSizeAttrs(const FunctionDecl *A,
                                     const FunctionDecl *B) {
  assert(A->getNumParams() == B->getNumParams());

  auto AttrEq = [](const ParmVarDecl *A, const ParmVarDecl *B) {
    const auto *AttrA = A->getAttr<PassObjectSizeAttr>();
    const auto *AttrB = B->getAttr<PassObjectSizeAttr>();
    if (AttrA == AttrB)
      return true;
    return AttrA && AttrB && AttrA->getType() == AttrB->getType() &&
           AttrA->isDynamic() == AttrB->isDynamic();
  };

  return std::equal(A->param_begin(), A->param_end(), B->param_begin(), AttrEq);
}
} // namespace neverc

