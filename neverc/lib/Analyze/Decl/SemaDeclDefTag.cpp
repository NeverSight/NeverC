//===- SemaDeclDefTag.cpp - Tag declaration semantics ---===//

#include "SemaDeclUtils.h"
#include "Type/TypeLocBuilder.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Attr/AttributeCommonInfo.h"
#include "neverc/Foundation/Builtin/BuiltinString.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
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
// Tag (struct/union/enum) handling
// ===----------------------------------------------------------------------===

DeclResult Sema::OnTag(Scope *S, unsigned TagSpec, TagUseKind TUK,
                       SourceLocation KWLoc, IdentifierInfo *Name,
                       SourceLocation NameLoc,
                       const ParsedAttributesView &Attrs, AccessSpecifier AS,
                       bool &OwnedDecl, TypeResult UnderlyingType,
                       OffsetOfKind OOK, SkipBodyInfo *SkipBody) {
  // If this is not a definition, it must have a name.
  assert((Name != nullptr || TUK == TUK_Definition) &&
         "Nameless record must be a definition!");

  OwnedDecl = false;
  TagTypeKind Kind = TypeWithKeyword::getTagTypeKindForTypeSpec(TagSpec);

  bool Invalid = false;

  // Figure out the underlying type if this a enum declaration. We need to do
  // this early, because it's needed to detect if this is an incompatible
  // redeclaration.
  llvm::PointerUnion<const Type *, TypeSourceInfo *> EnumUnderlying;
  bool IsFixed = !UnderlyingType.isUnset();

  if (Kind == TagTypeKind::Enum) {
    if (UnderlyingType.isInvalid()) {
      // No underlying type explicitly specified, or we failed to parse the
      // type, default to int.
      EnumUnderlying = Context.IntTy.getTypePtr();
    } else if (UnderlyingType.get()) {
      // Fixed underlying type: must be an integral type (cv ignored).
      TypeSourceInfo *TI = nullptr;
      GetTypeFromParser(UnderlyingType.get(), &TI);
      EnumUnderlying = TI;

      if (CheckEnumUnderlyingType(TI))
        // Recover by falling back to int.
        EnumUnderlying = Context.IntTy.getTypePtr();

    } else if (Context.getTargetInfo().getTriple().isWindowsMSVCEnvironment()) {
      // For MSVC ABI compatibility, unfixed enums must use an underlying type
      // of 'int'. However, if this is an unfixed forward declaration, don't set
      // the underlying type unless the user enables -fms-compatibility. This
      // makes unfixed forward declared enums incomplete and is more conforming.
      if (TUK == TUK_Definition || getLangOpts().MSVCCompat)
        EnumUnderlying = Context.IntTy.getTypePtr();
    }
  }

  DeclContext *SearchDC = CurContext;

  RedeclarationKind Redecl = forRedeclarationInCurContext();
  if (TUK == TUK_Reference)
    Redecl = NotForRedeclaration;

  auto createTagFromNewDecl = [&]() -> TagDecl * {
    // If there is an identifier, use the location of the identifier as the
    // location of the decl, otherwise use the location of the struct/union
    // keyword.
    SourceLocation Loc = NameLoc.isValid() ? NameLoc : KWLoc;
    TagDecl *New = nullptr;

    if (Kind == TagTypeKind::Enum) {
      New = EnumDecl::Create(Context, SearchDC, KWLoc, Loc, Name, nullptr,
                             IsFixed);
      // If this is an undefined enum, bail.
      if (TUK != TUK_Definition && !Invalid)
        return nullptr;
      if (EnumUnderlying) {
        EnumDecl *ED = cast<EnumDecl>(New);
        if (TypeSourceInfo *TI = EnumUnderlying.dyn_cast<TypeSourceInfo *>())
          ED->setIntegerTypeSourceInfo(TI);
        else
          ED->setIntegerType(QualType(EnumUnderlying.get<const Type *>(), 0));
        QualType EnumTy = ED->getIntegerType();
        ED->setPromotionType(Context.isPromotableIntegerType(EnumTy)
                                 ? Context.getPromotedIntegerType(EnumTy)
                                 : EnumTy);
      }
    } else { // struct/union
      if (getLangOpts().CPlusPlus)
        New = CXXRecordDecl::Create(Context, Kind, SearchDC, KWLoc, Loc, Name,
                                    nullptr);
      else
        New = RecordDecl::Create(Context, Kind, SearchDC, KWLoc, Loc, Name,
                                 nullptr);
    }

    if (RecordDecl *RD = dyn_cast<RecordDecl>(New)) {
      // Add alignment attributes if necessary; these attributes are checked
      // when the TreeContext lays out the structure.
      //
      // It is important for implementing the correct semantics that this
      // happen here (in OnTag). The #pragma pack stack is
      // maintained as a result of parser callbacks which can occur at
      // many points during the parsing of a struct declaration (because
      // the #pragma tokens are effectively skipped over during the
      // parsing of the struct).
      if (TUK == TUK_Definition && (!SkipBody || !SkipBody->ShouldSkip)) {
        AddAlignmentAttributesForRecord(RD);
        AddMsStructLayoutForRecord(RD);
      }
    }
    New->setLexicalDeclContext(CurContext);
    return New;
  };

  LookupResult Previous(*this, Name, NameLoc, ResolveTag, Redecl);
  if (Name) {
    // If this is a named struct, check to see if there was a previous forward
    // declaration or definition.
    // We're looking into outer scopes here, even when we
    // shouldn't be. Doing so can result in ambiguities that we
    // shouldn't be diagnosing.
    ResolveName(Previous, S);

    // When declaring or defining a tag, ignore ambiguities introduced
    // by types using'ed into this scope.
    if (Previous.isAmbiguous() &&
        (TUK == TUK_Definition || TUK == TUK_Declaration)) {
      LookupResult::Filter F = Previous.makeFilter();
      while (F.hasNext()) {
        NamedDecl *ND = F.next();
        if (!ND->getDeclContext()->getRedeclContext()->Equals(
                SearchDC->getRedeclContext()))
          F.erase();
      }
      F.done();
    }

    // Note:  there used to be some attempt at recovery here.
    if (Previous.isAmbiguous())
      return true;

    if (TUK != TUK_Reference) {
      while (isa<RecordDecl, EnumDecl>(SearchDC))
        SearchDC = SearchDC->getParent();
    }
  }

  if (Name && Previous.empty() && TUK == TUK_Reference) {
    if (Invalid)
      goto CreateNewDecl;

    // C99 6.7.2.3p8: find the scope where we'll be declaring the tag.
    SearchDC = getTagInjectionContext(SearchDC);
    S = getTagInjectionScope(S, getLangOpts());

    Previous.setRedeclarationKind(forRedeclarationInCurContext());
    ResolveName(Previous, S);
  }

  // If we have a known previous declaration to use, then use it.
  if (Previous.empty() && SkipBody && SkipBody->Previous)
    Previous.addDecl(SkipBody->Previous);

  if (!Previous.empty()) {
    NamedDecl *PrevDecl = Previous.getFoundDecl();
    NamedDecl *DirectPrevDecl = Previous.getRepresentativeDecl();

    if (TagDecl *PrevTagDecl = dyn_cast<TagDecl>(PrevDecl)) {
      // If this is a use of a previous tag, or if the tag is already declared
      // in the same scope (so that the definition/declaration completes or
      // rementions the tag), reuse the decl.
      if (TUK == TUK_Reference || isDeclInScope(DirectPrevDecl, SearchDC, S)) {
        // Make sure that this wasn't declared as an enum and now used as a
        // struct or something similar.
        if (!isAcceptableTagRedeclaration(PrevTagDecl, Kind)) {
          bool SafeToContinue =
              (PrevTagDecl->getTagKind() != TagTypeKind::Enum &&
               Kind != TagTypeKind::Enum);
          if (SafeToContinue)
            Diag(KWLoc, diag::err_use_with_wrong_tag)
                << Name
                << FixItHint::CreateReplacement(SourceRange(KWLoc),
                                                PrevTagDecl->getKindName());
          else
            Diag(KWLoc, diag::err_use_with_wrong_tag) << Name;
          Diag(PrevTagDecl->getLocation(), diag::note_previous_use);

          if (SafeToContinue)
            Kind = PrevTagDecl->getTagKind();
          else {
            // Recover by making this an anonymous redefinition.
            Name = nullptr;
            Previous.clear();
            Invalid = true;
          }
        }

        if (Kind == TagTypeKind::Enum &&
            PrevTagDecl->getTagKind() == TagTypeKind::Enum) {
          const EnumDecl *PrevEnum = cast<EnumDecl>(PrevTagDecl);
          if (TUK == TUK_Reference)
            return PrevTagDecl;

          QualType EnumUnderlyingTy;
          if (TypeSourceInfo *TI = EnumUnderlying.dyn_cast<TypeSourceInfo *>())
            EnumUnderlyingTy = TI->getType().getUnqualifiedType();
          else if (const Type *T = EnumUnderlying.dyn_cast<const Type *>())
            EnumUnderlyingTy = QualType(T, 0);

          // All conflicts with previous declarations are recovered by
          // returning the previous declaration, unless this is a definition,
          // in which case we want the caller to bail out.
          if (CheckEnumRedeclaration(NameLoc.isValid() ? NameLoc : KWLoc,
                                     /*IsScoped=*/false, EnumUnderlyingTy,
                                     IsFixed, PrevEnum))
            return TUK == TUK_Declaration ? PrevTagDecl : nullptr;
        }

        if (!Invalid) {
          // If this is a use, just return the declaration we found, unless
          // we have attributes.
          if (TUK == TUK_Reference) {
            if (!Attrs.empty()) {
            } else {
              return PrevTagDecl;
            }
          }

          // Diagnose attempts to redefine a tag.
          if (TUK == TUK_Definition) {
            if (NamedDecl *Def = PrevTagDecl->getDefinition()) {
              // Allow ODR-like semantics for C, i.e., do not keep more than
              // one definition around (merge them). Ensure the decl passes the
              // structural compatibility check in C11 6.2.7/1 (or 6.1.2.6/1
              // in C89).
              // The BuiltinString prelude planted a tag (e.g.
              // `struct __neverc_string`) into <built-in>; the user's
              // real source can redefine the same tag and we silently
              // absorb the duplicate.
              bool BuiltinPriorDef =
                  Context.getSourceManager().isWrittenInBuiltinFile(
                      Def->getLocation());
              if (SkipBody && BuiltinPriorDef) {
                // The old decl is a synthetic pre-decl; merge visibility
                // after parsing.
                SkipBody->CheckSameAsPrevious = true;
                SkipBody->New = createTagFromNewDecl();
                SkipBody->Previous = Def;
                return Def;
              } else {
                // A redeclaration in function prototype scope in C isn't
                // visible elsewhere, so merely issue a warning.
                if (S->containedInPrototypeScope())
                  Diag(NameLoc, diag::warn_redefinition_in_param_list) << Name;
                else
                  Diag(NameLoc, diag::err_redefinition) << Name;
                notePreviousDefinition(Def,
                                       NameLoc.isValid() ? NameLoc : KWLoc);
                // If this is a redefinition, recover by making this
                // struct be anonymous, which will make any later
                // references get the previous definition.
                Name = nullptr;
                Previous.clear();
                Invalid = true;
              }
            } else {
              // If the type is currently being defined, complain
              // about a nested redefinition.
              auto *TD = Context.getTagDeclType(PrevTagDecl)->getAsTagDecl();
              if (TD->isBeingDefined()) {
                Diag(NameLoc, diag::err_nested_redefinition) << Name;
                Diag(PrevTagDecl->getLocation(),
                     diag::note_previous_definition);
                Name = nullptr;
                Previous.clear();
                Invalid = true;
              }
            }

            // Okay, this is definition of a previously declared or referenced
            // tag. We're going to create a new Decl for it.
          }

          // Okay, we're going to make a redeclaration.  If this is some kind
          // of reference, make sure we build the redeclaration in the same DC
          // as the original, and ignore the current access specifier.
          if (TUK == TUK_Reference) {
            SearchDC = PrevTagDecl->getDeclContext();
            AS = AS_none;
          }
        }
        // If we get here we have (another) forward declaration or we
        // have a definition.  Just create a new decl.

      } else {
        // If we get here, this is a definition of a new tag type in a nested
        // scope, e.g. "struct foo; void bar() { struct foo; }", just create a
        // new decl/type.  We set PrevDecl to NULL so that the entities
        // have distinct types.
        Previous.clear();
      }
      // If we get here, we're going to create a new Decl. If PrevDecl
      // is non-NULL, it's a definition of the tag declared by
      // PrevDecl. If it's NULL, we have a new definition.

      // PrevDecl is not a tag (e.g. typedef) but matched tag lookup.
    } else {
      // Use a better diagnostic if an elaborated-type-specifier
      // found the wrong kind of type on the first
      // (non-redeclaration) lookup.
      if (TUK == TUK_Reference && !Previous.isForRedeclaration() &&
          (!dyn_cast<TypedefNameDecl>(PrevDecl) ||
           Kind != TagTypeKind::Struct)) {
#ifndef _WIN32
        NonTagKind NTK = getNonTagTypeDeclKind(PrevDecl, Kind);
        Diag(NameLoc, diag::err_tag_reference_non_tag)
            << PrevDecl << NTK << llvm::to_underlying(Kind);
        Diag(PrevDecl->getLocation(), diag::note_declared_at);
        Invalid = true;
#endif
        // Otherwise, only diagnose if the declaration is in scope.
      } else if (!isDeclInScope(DirectPrevDecl, SearchDC, S)) {
        // do nothing

        // Diagnose implicit declarations introduced by elaborated types.
      } else if (TUK == TUK_Reference) {
#ifndef _WIN32
        NonTagKind NTK = getNonTagTypeDeclKind(PrevDecl, Kind);
        Diag(NameLoc, diag::err_tag_reference_conflict) << NTK;
        Diag(PrevDecl->getLocation(), diag::note_previous_decl) << PrevDecl;
        Invalid = true;
#endif
        // Otherwise it's a declaration.  Call out a particularly common
        // case here.
      } else if (TypedefNameDecl *TND = dyn_cast<TypedefNameDecl>(PrevDecl)) {
        Diag(NameLoc, diag::err_tag_definition_of_typedef)
            << Name << TND->getUnderlyingType();
        Diag(PrevDecl->getLocation(), diag::note_previous_decl) << PrevDecl;
        Invalid = true;

        // Otherwise, diagnose.
      } else {
        // The tag name clashes with something else in the target scope,
        // issue an error and recover by making this tag be anonymous.
        Diag(NameLoc, diag::err_redefinition_different_kind) << Name;
        notePreviousDefinition(PrevDecl, NameLoc);
        Name = nullptr;
        Invalid = true;
      }

      // The existing declaration isn't relevant to us; we're in a
      // new scope, so clear out the previous declaration.
      Previous.clear();
    }
  }

CreateNewDecl:

  TagDecl *PrevDecl = nullptr;
  if (Previous.isSingleResult())
    PrevDecl = cast<TagDecl>(Previous.getFoundDecl());

  // If there is an identifier, use the location of the identifier as the
  // location of the decl, otherwise use the location of the struct/union
  // keyword.
  SourceLocation Loc = NameLoc.isValid() ? NameLoc : KWLoc;

  // Otherwise, create a new declaration. If there is a previous
  // declaration of the same entity, the two will be linked via
  // PrevDecl.
  TagDecl *New;

  if (Kind == TagTypeKind::Enum) {
    // Tag decls should be chained to any simultaneous vardecls, e.g.:
    // enum X { A, B, C } D;    D should chain to X.
    New = EnumDecl::Create(Context, SearchDC, KWLoc, Loc, Name,
                           cast_or_null<EnumDecl>(PrevDecl), IsFixed);

    // If this is an undefined enum, warn.
#ifndef _WIN32
    if (TUK != TUK_Definition && !Invalid) {
      TagDecl *Def;
      if (IsFixed && cast<EnumDecl>(New)->isFixed()) {
        // Fixed opaque enum: no extra forward-ref diagnostic.
      } else if (PrevDecl &&
                 (Def = cast<EnumDecl>(PrevDecl)->getDefinition())) {
        Diag(Loc, diag::ext_forward_ref_enum_def) << New;
        Diag(Def->getLocation(), diag::note_previous_definition);
      } else {
        unsigned DiagID = diag::ext_forward_ref_enum;
        if (getLangOpts().MSVCCompat)
          DiagID = diag::ext_ms_forward_ref_enum;
        Diag(Loc, DiagID);
      }
    }
#endif

    if (EnumUnderlying) {
      EnumDecl *ED = cast<EnumDecl>(New);
      if (TypeSourceInfo *TI = EnumUnderlying.dyn_cast<TypeSourceInfo *>())
        ED->setIntegerTypeSourceInfo(TI);
      else
        ED->setIntegerType(QualType(EnumUnderlying.get<const Type *>(), 0));
      QualType EnumTy = ED->getIntegerType();
      ED->setPromotionType(Context.isPromotableIntegerType(EnumTy)
                               ? Context.getPromotedIntegerType(EnumTy)
                               : EnumTy);
      assert(ED->isComplete() && "enum with type should be complete");
    }
  } else {
    // struct/union/class

    // Tag decls should be chained to any simultaneous vardecls, e.g.:
    // struct X { int A; } D;    D should chain to X.
    New = RecordDecl::Create(Context, Kind, SearchDC, KWLoc, Loc, Name,
                             cast_or_null<RecordDecl>(PrevDecl));
  }

  if (OOK != OOK_Outside && TUK == TUK_Definition)
    Diag(New->getLocation(), diag::ext_type_defined_in_offsetof)
        << (OOK == OOK_Macro) << New->getSourceRange();

  if (RecordDecl *RD = dyn_cast<RecordDecl>(New)) {
    // Add alignment attributes if necessary; these attributes are checked when
    // the TreeContext lays out the structure.
    //
    // It is important for implementing the correct semantics that this
    // happen here (in OnTag). The #pragma pack stack is
    // maintained as a result of parser callbacks which can occur at
    // many points during the parsing of a struct declaration (because
    // the #pragma tokens are effectively skipped over during the
    // parsing of the struct).
    if (TUK == TUK_Definition && (!SkipBody || !SkipBody->ShouldSkip)) {
      AddAlignmentAttributesForRecord(RD);
      AddMsStructLayoutForRecord(RD);
    }
  }

  // If we're declaring or defining a tag in function prototype scope in C,
  // note that this type can only be used within the function and add it to
  // the list of decls to inject into the function definition scope.
  if ((Name || Kind == TagTypeKind::Enum) &&
      getNonFieldDeclScope(S)->isFunctionPrototypeScope()) {
    if (!PrevDecl) {
      Diag(Loc, diag::warn_decl_in_param_list) << Context.getTagDeclType(New);
    }
  }

  if (Invalid)
    New->setInvalidDecl();

  // Lexical context follows the active scope.
  New->setLexicalDeclContext(CurContext);

  if (!Invalid && SearchDC->isRecord())
    SetMemberAccessSpecifier(New, PrevDecl, AS);

  if (TUK == TUK_Definition && (!SkipBody || !SkipBody->ShouldSkip))
    New->startDefinition();

  ProcessDeclAttributeList(S, New, Attrs);
  AddPragmaAttributes(S, New);

  if (Name) {
    S = getNonFieldDeclScope(S);
    PushOnScopeChains(New, S, true);
  } else {
    CurContext->addDecl(New);
  }

  // If this is the C FILE type, notify the AST context.
  if (IdentifierInfo *II = New->getIdentifier())
    if (!New->isInvalidDecl() &&
        New->getDeclContext()->getRedeclContext()->isTranslationUnit() &&
        II->isStr("FILE"))
      Context.setFILEDecl(New);

  if (PrevDecl)
    mergeDeclAttributes(New, PrevDecl);

  AddPushedVisibilityAttribute(New);

  OwnedDecl = true;
  if (SkipBody && SkipBody->ShouldSkip) {
    return SkipBody->Previous;
  } else {
    return New;
  }
}

void Sema::OnTagStartDefinition(Scope *S, Decl *TagD) {
  TagDecl *Tag = cast<TagDecl>(TagD);

  // Enter the tag context.
  PushDeclContext(S, Tag);

  // If there's a #pragma GCC visibility in scope, set the visibility of this
  // record.
  AddPushedVisibilityAttribute(Tag);
}

void Sema::OnTagFinishDefinition(Scope *S, Decl *TagD, SourceRange BraceRange) {
  TagDecl *Tag = cast<TagDecl>(TagD);
  Tag->setBraceRange(BraceRange);

  // Make sure we "complete" the definition even it is invalid.
  if (Tag->isBeingDefined()) {
    assert(Tag->isInvalidDecl() && "We should already have completed it");
    if (RecordDecl *RD = dyn_cast<RecordDecl>(Tag))
      RD->completeDefinition();
  }

  // Exit this scope of this tag's definition.
  PopDeclContext();

  // Notify the consumer that we've defined a tag.
  if (!Tag->isInvalidDecl())
    Consumer.ProcessTagDeclDefinition(Tag);
}

// Note that FieldName may be null for anonymous bitfields.
ExprResult Sema::VerifyBitField(SourceLocation FieldLoc,
                                IdentifierInfo *FieldName, QualType FieldTy,
                                bool IsMsStruct, Expr *BitWidth) {
  assert(BitWidth);
  if (BitWidth->containsErrors())
    return ExprError();

  // C99 6.7.2.1p4: bit-field type shall be integral or enumeration.
  if (!FieldTy->isIntegralOrEnumerationType()) {
    if (RequireCompleteSizedType(FieldLoc, FieldTy,
                                 diag::err_field_incomplete_or_sizeless))
      return ExprError();
    if (FieldName)
      return Diag(FieldLoc, diag::err_not_integral_type_bitfield)
             << FieldName << FieldTy << BitWidth->getSourceRange();
    return Diag(FieldLoc, diag::err_not_integral_type_anon_bitfield)
           << FieldTy << BitWidth->getSourceRange();
  }

  llvm::APSInt Value;
  ExprResult ICE = VerifyIntegerConstantExpression(BitWidth, &Value, AllowFold);
  if (ICE.isInvalid())
    return ICE;
  BitWidth = ICE.get();

  // Zero-width bitfield is ok for anonymous field.
  if (Value == 0 && FieldName)
    return Diag(FieldLoc, diag::err_bitfield_has_zero_width)
           << FieldName << BitWidth->getSourceRange();

  if (Value.isSigned() && Value.isNegative()) {
    if (FieldName)
      return Diag(FieldLoc, diag::err_bitfield_has_negative_width)
             << FieldName << toString(Value, 10);
    return Diag(FieldLoc, diag::err_anon_bitfield_has_negative_width)
           << toString(Value, 10);
  }

  // The size of the bit-field must not exceed our maximum permitted object
  // size.
  if (Value.getActiveBits() > ConstantArrayType::getMaxSizeBits(Context)) {
    return Diag(FieldLoc, diag::err_bitfield_too_wide)
           << !FieldName << FieldName << toString(Value, 10);
  }

  {
    uint64_t TypeStorageSize = Context.getTypeSize(FieldTy);
    uint64_t TypeWidth = Context.getIntWidth(FieldTy);
    bool BitfieldIsOverwide = Value.ugt(TypeWidth);

    // Over-wide bitfields are an error in C or when using the MSVC bitfield
    // ABI.
    bool CStdConstraintViolation = BitfieldIsOverwide;
    bool MSBitfieldViolation = Value.ugt(TypeStorageSize) && IsMsStruct;
    if (CStdConstraintViolation || MSBitfieldViolation) {
      unsigned DiagWidth =
          CStdConstraintViolation ? TypeWidth : TypeStorageSize;
      return Diag(FieldLoc, diag::err_bitfield_width_exceeds_type_width)
             << (bool)FieldName << FieldName << toString(Value, 10)
             << !CStdConstraintViolation << DiagWidth;
    }

    // Warn on types where the user might conceivably expect to get all
    // specified bits as value bits: that's all integral types other than
    // 'bool'.
    if (BitfieldIsOverwide && !FieldTy->isBooleanType() && FieldName) {
      Diag(FieldLoc, diag::warn_bitfield_width_exceeds_type_width)
          << FieldName << toString(Value, 10) << (unsigned)TypeWidth;
    }
  }

  return BitWidth;
}

// ===----------------------------------------------------------------------===
// File-scope asm & pragma directives
// ===----------------------------------------------------------------------===

Decl *Sema::OnFileScopeAsmDecl(Expr *expr, SourceLocation StartLoc,
                               SourceLocation EndLoc) {
  StringLiteral *AsmString = cast<StringLiteral>(expr);

  FileScopeAsmDecl *New = FileScopeAsmDecl::Create(Context, CurContext,
                                                   AsmString, StartLoc, EndLoc);
  CurContext->addDecl(New);
  return New;
}

void Sema::OnPragmaWeakID(IdentifierInfo *Name, SourceLocation PragmaLoc,
                          SourceLocation NameLoc) {
  Decl *PrevDecl = LookupSingleName(TUScope, Name, NameLoc, ResolveOrdinary);

  if (PrevDecl) {
    PrevDecl->addAttr(WeakAttr::CreateImplicit(Context, PragmaLoc));
  } else {
    (void)WeakUndeclaredIdentifiers[Name].insert(WeakInfo(nullptr, NameLoc));
  }
}

void Sema::OnPragmaWeakAlias(IdentifierInfo *Name, IdentifierInfo *AliasName,
                             SourceLocation PragmaLoc, SourceLocation NameLoc,
                             SourceLocation AliasNameLoc) {
  Decl *PrevDecl =
      LookupSingleName(TUScope, AliasName, AliasNameLoc, ResolveOrdinary);
  WeakInfo W = WeakInfo(Name, NameLoc);

  if (PrevDecl && (isa<FunctionDecl>(PrevDecl) || isa<VarDecl>(PrevDecl))) {
    if (!PrevDecl->hasAttr<AliasAttr>())
      if (NamedDecl *ND = dyn_cast<NamedDecl>(PrevDecl))
        DeclApplyPragmaWeak(TUScope, ND, W);
  } else {
    (void)WeakUndeclaredIdentifiers[AliasName].insert(W);
  }
}
