//===- SemaDeclDeclarator.cpp - Standalone declarations & anonymous structs -----===//

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
// Standalone declarations & anonymous structs
// ===----------------------------------------------------------------------===

namespace neverc {

StorageClass storageClassSpecToVarDeclStorageClass(const DeclSpec &DS) {
  DeclSpec::SCS StorageClassSpec = DS.getStorageClassSpec();
  assert(StorageClassSpec != DeclSpec::SCS_typedef &&
         "Parser allowed 'typedef' as storage class VarDecl.");
  switch (StorageClassSpec) {
  case DeclSpec::SCS_unspecified:
    return SC_None;
  case DeclSpec::SCS_extern:
    if (DS.isExternInLinkageSpec())
      return SC_None;
    return SC_Extern;
  case DeclSpec::SCS_static:
    return SC_Static;
  case DeclSpec::SCS_auto:
    return SC_Auto;
  case DeclSpec::SCS_register:
    return SC_Register;
  case DeclSpec::SCS_private_extern:
    return SC_PrivateExtern;
    // Illegal SCSs map to None: error reporting is up to the caller.
  case DeclSpec::SCS_typedef:
    return SC_None;
  }
  llvm_unreachable("unknown storage class specifier");
}

} // namespace neverc


void Sema::setTagNameForLinkagePurposes(TagDecl *TagFromDeclSpec,
                                        TypedefNameDecl *NewTD) {
  if (TagFromDeclSpec->isInvalidDecl())
    return;

  // Do nothing if the tag already has a name for linkage purposes.
  if (TagFromDeclSpec->hasNameForLinkage())
    return;

  // A well-formed anonymous tag must always be a TUK_Definition.
  assert(TagFromDeclSpec->isThisDeclarationADefinition());

  // The type must match the tag exactly;  no qualifiers allowed.
  if (!Context.hasSameType(NewTD->getUnderlyingType(),
                           Context.getTagDeclType(TagFromDeclSpec)))
    return;

  TagFromDeclSpec->setTypedefNameForAnonDecl(NewTD);
}

namespace {
unsigned mapTypeSpecifierToDiagID(const DeclSpec &DS) {
  DeclSpec::TST T = DS.getTypeSpecType();
  switch (T) {
  case DeclSpec::TST_struct:
    return 0;
  case DeclSpec::TST_union:
    return 1;
  case DeclSpec::TST_enum:
    return 2;
  default:
    llvm_unreachable("unexpected type specifier");
  }
}
} // namespace
Decl *Sema::ParsedFreeStandingDeclSpec(Scope *S, AccessSpecifier AS,
                                       DeclSpec &DS,
                                       const ParsedAttributesView &DeclAttrs,
                                       RecordDecl *&AnonRecord) {
  Decl *TagD = nullptr;
  TagDecl *Tag = nullptr;
  if (DS.getTypeSpecType() == DeclSpec::TST_struct ||
      DS.getTypeSpecType() == DeclSpec::TST_union ||
      DS.getTypeSpecType() == DeclSpec::TST_enum) {
    TagD = DS.getRepAsDecl();

    if (!TagD) // We probably had an error
      return nullptr;

    // Note that the above type specs guarantee that the
    // type rep is a Decl, whereas in many of the others
    // it's a Type.
    if (isa<TagDecl>(TagD))
      Tag = cast<TagDecl>(TagD);
  }

  if (Tag) {
    Tag->setFreeStanding();
    if (Tag->isInvalidDecl())
      return Tag;
  }

  if (unsigned TypeQuals = DS.getTypeQualifiers()) {
    // Enforce C99 6.7.3p2: "Types other than pointer types derived from object
    // or incomplete types shall not be restrict-qualified."
    if (TypeQuals & DeclSpec::TQ_restrict)
      Diag(DS.getRestrictSpecLoc(),
           diag::err_typecheck_invalid_restrict_not_pointer_noarg)
          << DS.getSourceRange();
  }

  if (DS.isInlineSpecified())
    Diag(DS.getInlineSpecLoc(), diag::err_inline_non_function) << false;

  if (DS.hasConstexprSpecifier()) {
    // constexpr applies only to functions or variables, not tags.
    if (Tag)
      Diag(DS.getConstexprSpecLoc(), diag::err_constexpr_tag)
          << mapTypeSpecifierToDiagID(DS)
          << static_cast<int>(DS.getConstexprSpecifier());
    else
      Diag(DS.getConstexprSpecLoc(), diag::err_constexpr_wrong_decl_kind)
          << static_cast<int>(DS.getConstexprSpecifier());
    // Don't emit warnings after this error.
    return TagD;
  }

  if (DS.isNoreturnSpecified())
    Diag(DS.getNoreturnSpecLoc(), diag::err_noreturn_non_function);

  // Track whether this decl-specifier declares anything.
  bool DeclaresAnything = true;

  // Handle anonymous struct definitions.
  if (RecordDecl *Record = dyn_cast_or_null<RecordDecl>(Tag)) {
    if (!Record->getDeclName() && Record->isCompleteDefinition() &&
        DS.getStorageClassSpec() != DeclSpec::SCS_typedef) {
      if (Record->getDeclContext()->isRecord()) {
        // If CurContext is a DeclContext that can contain statements,
        // RecursiveTreeVisitor won't visit the decls that
        // FormAnonymousStructOrUnion() will put into CurContext.
        // Also store them here so that they can be part of the
        // DeclStmt that gets created in this case.
        if (CurContext->isFunctionOrMethod())
          AnonRecord = Record;
        return FormAnonymousStructOrUnion(S, DS, AS, Record,
                                          Context.getPrintingPolicy());
      }

      DeclaresAnything = false;
    }
  }

  // C11 6.7.2.1p2:
  //   A struct-declaration that does not declare an anonymous structure or
  //   anonymous union shall contain a struct-declarator-list.
  //
  // This rule also existed in C89 and C99; the grammar for struct-declaration
  // did not permit a struct-declaration without a struct-declarator-list.
  if (CurContext->isRecord() &&
      DS.getStorageClassSpec() == DeclSpec::SCS_unspecified) {
    // Check for Microsoft C extension: anonymous struct/union member.
    // Handle 2 kinds of anonymous struct/union:
    //   struct STRUCT;
    //   union UNION;
    // and
    //   STRUCT_TYPE;  <- where STRUCT_TYPE is a typedef struct.
    //   UNION_TYPE;   <- where UNION_TYPE is a typedef union.
    if ((Tag && Tag->getDeclName()) ||
        DS.getTypeSpecType() == DeclSpec::TST_typename) {
      RecordDecl *Record = nullptr;
      if (Tag)
        Record = dyn_cast<RecordDecl>(Tag);
      else if (const RecordType *RT =
                   DS.getRepAsType().get()->getAsStructureType())
        Record = RT->getDecl();
      else if (const RecordType *UT = DS.getRepAsType().get()->getAsUnionType())
        Record = UT->getDecl();

      if (Record && getLangOpts().MicrosoftExt) {
        Diag(DS.getBeginLoc(), diag::ext_ms_anonymous_record)
            << Record->isUnion() << DS.getSourceRange();
        return FormMicrosoftCAnonymousStruct(S, DS, Record);
      }

      DeclaresAnything = false;
    }
  }

  // Skip all the checks below if we have a type error.
  if (DS.getTypeSpecType() == DeclSpec::TST_error ||
      (TagD && TagD->isInvalidDecl()))
    return TagD;

#ifndef _WIN32
  if (!DS.isMissingDeclaratorOk()) {
    // Customize diagnostic for a typedef missing a name.
    if (DS.getStorageClassSpec() == DeclSpec::SCS_typedef)
      Diag(DS.getBeginLoc(), diag::ext_typedef_without_a_name)
          << DS.getSourceRange();
    else
      DeclaresAnything = false;
  }
#endif

  // A declaration must introduce something (declarator, tag, or enum);
  // otherwise emit the extension diagnostic below.
  if (!DeclaresAnything) {
    // In C, we allow this as a (popular) extension / bug. Don't bother
    // producing further diagnostics for redundant qualifiers after this.
    Diag(DS.getBeginLoc(), diag::ext_no_declarators) << DS.getSourceRange();
    return TagD;
  }

  // Standalone storage-class or type qualifiers without a declarator list are
  // usually invalid; some cases remain as extensions.
  unsigned DiagID = diag::warn_standalone_specifier;

  // Note that a linkage-specification sets a storage class, but
  // 'extern "C" struct foo;' is actually valid and not theoretically
  // useless.
  if (DeclSpec::SCS SCS = DS.getStorageClassSpec()) {
    if (!DS.isExternInLinkageSpec() && SCS != DeclSpec::SCS_typedef)
      Diag(DS.getStorageClassSpecLoc(), DiagID)
          << DeclSpec::getSpecifierName(SCS);
  }

  if (DeclSpec::TSCS TSCS = DS.getThreadStorageClassSpec())
    Diag(DS.getThreadStorageClassSpecLoc(), DiagID)
        << DeclSpec::getSpecifierName(TSCS);
  if (DS.getTypeQualifiers()) {
    if (DS.getTypeQualifiers() & DeclSpec::TQ_const)
      Diag(DS.getConstSpecLoc(), DiagID)
          << tok::getKeywordSpelling(tok::kw_const);
    if (DS.getTypeQualifiers() & DeclSpec::TQ_volatile)
      Diag(DS.getConstSpecLoc(), DiagID)
          << tok::getKeywordSpelling(tok::kw_volatile);
    // Restrict is covered above.
    if (DS.getTypeQualifiers() & DeclSpec::TQ_atomic)
      Diag(DS.getAtomicSpecLoc(), DiagID)
          << tok::getKeywordSpelling(tok::kw__Atomic);
    if (DS.getTypeQualifiers() & DeclSpec::TQ_unaligned)
      Diag(DS.getUnalignedSpecLoc(), DiagID)
          << tok::getKeywordSpelling(tok::kw___unaligned);
  }

  // Warn about ignored type attributes, for example:
  // __attribute__((aligned)) struct A;
  // Attributes should be placed after tag to apply to type declaration.
  if (!DS.getAttributes().empty() || !DeclAttrs.empty()) {
    DeclSpec::TST TypeSpecType = DS.getTypeSpecType();
    if (TypeSpecType == DeclSpec::TST_struct ||
        TypeSpecType == DeclSpec::TST_union ||
        TypeSpecType == DeclSpec::TST_enum) {

      auto GenAttributeDiagnostic = [this, &DS](const ParsedAttr &AL) {
        unsigned DiagnosticId = diag::warn_declspec_attribute_ignored;
        if (AL.isAlignas())
          DiagnosticId = diag::warn_attribute_ignored;
        else if (AL.isRegularKeywordAttribute())
          DiagnosticId = diag::err_declspec_keyword_has_no_effect;
        else
          DiagnosticId = diag::warn_declspec_attribute_ignored;
        Diag(AL.getLoc(), DiagnosticId) << AL << mapTypeSpecifierToDiagID(DS);
      };

      llvm::for_each(DS.getAttributes(), GenAttributeDiagnostic);
      llvm::for_each(DeclAttrs, GenAttributeDiagnostic);
    }
  }

  return TagD;
}

namespace {
bool checkAnonMemberRedeclaration(Sema &SemaRef, Scope *S, DeclContext *Owner,
                                  DeclarationName Name, SourceLocation NameLoc,
                                  bool IsUnion, StorageClass SC) {
  LookupResult R(SemaRef, Name, NameLoc,
                 Owner->isRecord() ? neverc::ResolveMember
                                   : neverc::ResolveOrdinary,
                 neverc::ForVisibleRedeclaration);
  if (!SemaRef.ResolveName(R, S))
    return false;

  // Pick a representative declaration.
  NamedDecl *PrevDecl = R.getRepresentativeDecl()->getUnderlyingDecl();
  assert(PrevDecl && "Expected a non-null Decl");

  if (!SemaRef.isDeclInScope(PrevDecl, Owner, S))
    return false;

  const llvm::Triple &Triple = SemaRef.Context.getTargetInfo().getTriple();
  if (SemaRef.getLangOpts().MicrosoftExt || SemaRef.getLangOpts().MSVCCompat ||
      Triple.isOSWindows() || Triple.isOSBinFormatCOFF())
    return false;

  SemaRef.Diag(NameLoc, diag::err_anonymous_record_member_redecl)
      << IsUnion << Name;
  SemaRef.Diag(PrevDecl->getLocation(), diag::note_previous_declaration);

  return true;
}
} // namespace

namespace {
bool injectAnonymousStructOrUnionMembers(
    Sema &SemaRef, Scope *S, DeclContext *Owner, RecordDecl *AnonRecord,
    AccessSpecifier AS, StorageClass SC,
    llvm::SmallVectorImpl<NamedDecl *> &Chaining) {
  bool Invalid = false;

  // Look every FieldDecl and IndirectFieldDecl with a name.
  for (auto *D : AnonRecord->decls()) {
    if ((isa<FieldDecl>(D) || isa<IndirectFieldDecl>(D)) &&
        cast<NamedDecl>(D)->getDeclName()) {
      ValueDecl *VD = cast<ValueDecl>(D);
      if (checkAnonMemberRedeclaration(SemaRef, S, Owner, VD->getDeclName(),
                                       VD->getLocation(), AnonRecord->isUnion(),
                                       SC)) {
        Invalid = true;
      } else {
        // Expose members in the enclosing scope (via IndirectFieldDecl when
        // nested).
        unsigned OldChainingSize = Chaining.size();
        if (IndirectFieldDecl *IF = dyn_cast<IndirectFieldDecl>(VD))
          Chaining.append(IF->chain_begin(), IF->chain_end());
        else
          Chaining.push_back(VD);

        assert(Chaining.size() >= 2);
        NamedDecl **NamedChain =
            new (SemaRef.Context) NamedDecl *[Chaining.size()];
        for (unsigned i = 0; i < Chaining.size(); i++)
          NamedChain[i] = Chaining[i];

        IndirectFieldDecl *IndirectField = IndirectFieldDecl::Create(
            SemaRef.Context, Owner, VD->getLocation(), VD->getIdentifier(),
            VD->getType(), {NamedChain, Chaining.size()});

        for (const auto *Attr : VD->attrs())
          IndirectField->addAttr(Attr->clone(SemaRef.Context));

        IndirectField->setAccess(AS);
        IndirectField->setImplicit();
        SemaRef.PushOnScopeChains(IndirectField, S);

        // That includes picking up the appropriate access specifier.
        if (AS != AS_none)
          IndirectField->setAccess(AS);

        Chaining.resize(OldChainingSize);
      }
    }
  }

  return Invalid;
}
} // namespace


Decl *Sema::FormAnonymousStructOrUnion(Scope *S, DeclSpec &DS,
                                       AccessSpecifier AS, RecordDecl *Record,
                                       const PrintingPolicy &Policy) {
  DeclContext *Owner = Record->getDeclContext();

  // Diagnose whether this anonymous struct/union is an extension.
  if (Record->isUnion() && !getLangOpts().C11)
    Diag(Record->getLocation(), diag::ext_anonymous_union);
  else if (!Record->isUnion() && !getLangOpts().C11)
    Diag(Record->getLocation(), diag::ext_c11_anonymous_struct);

  // Anonymous struct/union: placement and extension checks.
  bool Invalid = false;

  if (!Record->isUnion() && !Owner->isRecord()) {
    Diag(Record->getLocation(), diag::err_anonymous_struct_not_member);
    Invalid = true;
  }

  // No declarator-id: build a dummy declarator so the anonymous member has a
  // type and attributes.
  Declarator Dc(DS, ParsedAttributesView::none(), DeclaratorContext::Member);
  StorageClass SC = storageClassSpecToVarDeclStorageClass(DS);
  TypeSourceInfo *TInfo = ResolveDeclaratorType(Dc, S);
  assert(TInfo && "couldn't build declarator info for anonymous struct/union");

  NamedDecl *Anon = nullptr;
  if (RecordDecl *OwningClass = dyn_cast<RecordDecl>(Owner)) {
    Anon = FieldDecl::Create(
        Context, OwningClass, DS.getBeginLoc(), Record->getLocation(),
        /*IdentifierInfo=*/nullptr, Context.getTypeDeclType(Record), TInfo,
        /*BitWidth=*/nullptr);
    Anon->setAccess(AS);
    ApplyDeclAttributes(S, Anon, Dc);

  } else {
    Anon = VarDecl::Create(Context, Owner, DS.getBeginLoc(),
                           Record->getLocation(), /*IdentifierInfo=*/nullptr,
                           Context.getTypeDeclType(Record), TInfo, SC);
    ApplyDeclAttributes(S, Anon, Dc);

    // Default-initialize the implicit variable.
    OnUninitializedDecl(Anon);
  }
  Anon->setImplicit();

  // Mark this as an anonymous struct/union type.
  Record->setAnonymousStructOrUnion(true);

  Owner->addDecl(Anon);

  // Inject members into the owning context and identifier resolver
  // so they're directly accessible by name.
  llvm::SmallVector<NamedDecl *, 2> Chain;
  Chain.push_back(Anon);

  if (injectAnonymousStructOrUnionMembers(*this, S, Owner, Record, AS, SC,
                                          Chain))
    Invalid = true;

  if (Invalid)
    Anon->setInvalidDecl();

  return Anon;
}

Decl *Sema::FormMicrosoftCAnonymousStruct(Scope *S, DeclSpec &DS,
                                          RecordDecl *Record) {
  assert(Record && "expected a record!");

  // Mock up a declarator.
  Declarator Dc(DS, ParsedAttributesView::none(), DeclaratorContext::TypeName);
  TypeSourceInfo *TInfo = ResolveDeclaratorType(Dc, S);
  assert(TInfo && "couldn't build declarator info for anonymous struct");

  auto *ParentDecl = cast<RecordDecl>(CurContext);
  QualType RecTy = Context.getTypeDeclType(Record);

  NamedDecl *Anon =
      FieldDecl::Create(Context, ParentDecl, DS.getBeginLoc(), DS.getBeginLoc(),
                        /*IdentifierInfo=*/nullptr, RecTy, TInfo,
                        /*BitWidth=*/nullptr);
  Anon->setImplicit();

  CurContext->addDecl(Anon);

  // Inject members into the current context and identifier resolver
  // so they're directly accessible by name.
  llvm::SmallVector<NamedDecl *, 2> Chain;
  Chain.push_back(Anon);

  RecordDecl *RecordDef = Record->getDefinition();
  if (RequireCompleteSizedType(Anon->getLocation(), RecTy,
                               diag::err_field_incomplete_or_sizeless) ||
      injectAnonymousStructOrUnionMembers(
          *this, S, CurContext, RecordDef, AS_none,
          storageClassSpecToVarDeclStorageClass(DS), Chain)) {
    Anon->setInvalidDecl();
    ParentDecl->setInvalidDecl();
  }

  return Anon;
}

