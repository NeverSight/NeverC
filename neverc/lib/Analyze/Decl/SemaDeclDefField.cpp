//===- SemaDeclDefField.cpp - Field declaration semantics ---===//

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
// Field declarations
// ===----------------------------------------------------------------------===

Decl *Sema::OnField(Scope *S, Decl *TagD, SourceLocation DeclStart,
                    Declarator &D, Expr *BitfieldWidth) {
  AccessSpecifier AS = AS_public;
  if (getLangOpts().CPlusPlus)
    AS = getDefaultCXXAccessSpecifierFor(dyn_cast_or_null<TagDecl>(TagD));
  FieldDecl *Res = OnField(S, cast_if_present<RecordDecl>(TagD), DeclStart, D,
                           BitfieldWidth, AS);
  return Res;
}

FieldDecl *Sema::OnField(Scope *S, RecordDecl *Record, SourceLocation DeclStart,
                         Declarator &D, Expr *BitWidth, AccessSpecifier AS) {
  IdentifierInfo *II = D.getIdentifier();
  SourceLocation Loc = DeclStart;
  if (II)
    Loc = D.getIdentifierLoc();

  TypeSourceInfo *TInfo = ResolveDeclaratorType(D, S);
  QualType T = TInfo->getType();
  const DeclSpec &FDS = D.getDeclSpec();

  if (FDS.isNoreturnSpecified())
    Diag(FDS.getNoreturnSpecLoc(), diag::err_noreturn_non_function);

  if (FDS.isInlineSpecified())
    Diag(FDS.getInlineSpecLoc(), diag::err_inline_non_function) << false;
  if (DeclSpec::TSCS TSCS = FDS.getThreadStorageClassSpec())
    Diag(FDS.getThreadStorageClassSpecLoc(), diag::err_invalid_thread)
        << DeclSpec::getSpecifierName(TSCS);

  NamedDecl *PrevDecl = nullptr;
  LookupResult Previous(*this, II, Loc, ResolveMember, ForVisibleRedeclaration);
  ResolveName(Previous, S);
  switch (Previous.getResultKind()) {
  case LookupResult::Found:
    PrevDecl = Previous.getAsSingle<NamedDecl>();
    break;

  case LookupResult::FoundOverloaded:
    PrevDecl = Previous.getRepresentativeDecl();
    break;

  case LookupResult::NotFound:
  case LookupResult::Ambiguous:
    break;
  }
  Previous.suppressDiagnostics();

  if (PrevDecl && !isDeclInScope(PrevDecl, Record, S))
    PrevDecl = nullptr;

  SourceLocation TSSL = D.getBeginLoc();
  FieldDecl *NewFD = CheckFieldDecl(II, T, TInfo, Record, Loc, BitWidth, TSSL,
                                    AS, PrevDecl, &D);

  if (NewFD->isInvalidDecl())
    Record->setInvalidDecl();

  if (NewFD->isInvalidDecl() && PrevDecl) {
    // Don't introduce NewFD into scope; there's already something
    // with the same name in the same scope.
  } else if (II) {
    PushOnScopeChains(NewFD, S);
  } else
    Record->addDecl(NewFD);

  return NewFD;
}

FieldDecl *Sema::CheckFieldDecl(DeclarationName Name, QualType T,
                                TypeSourceInfo *TInfo, RecordDecl *Record,
                                SourceLocation Loc, Expr *BitWidth,
                                SourceLocation TSSL, AccessSpecifier AS,
                                NamedDecl *PrevDecl, Declarator *D) {
  IdentifierInfo *II = Name.getAsIdentifierInfo();
  bool InvalidDecl = false;
  if (D)
    InvalidDecl = D->isInvalidType();

  // If we receive a broken type, recover by assuming 'int' and
  // marking this declaration as invalid.
  if (T.isNull() || T->containsErrors()) {
    InvalidDecl = true;
    T = Context.IntTy;
  }

  QualType EltTy = Context.getBaseElementType(T);
  if (!EltTy->containsErrors()) {
    if (RequireCompleteSizedType(Loc, EltTy,
                                 diag::err_field_incomplete_or_sizeless)) {
      // Fields of incomplete type force their record to be invalid.
      Record->setInvalidDecl();
      InvalidDecl = true;
    } else {
      NamedDecl *Def;
      EltTy->isIncompleteType(&Def);
      if (Def && Def->isInvalidDecl()) {
        Record->setInvalidDecl();
        InvalidDecl = true;
      }
    }
  }

  // TR 18037 does not allow fields to be declared with address space
  if (T.hasAddressSpace()) {
    Diag(Loc, diag::err_field_with_address_space);
    Record->setInvalidDecl();
    InvalidDecl = true;
  }

  // C99 6.7.2.1p8: A member of a structure or union may have any type other
  // than a variably modified type.
  if (!InvalidDecl && T->isVariablyModifiedType()) {
    if (!tryToFixVariablyModifiedVarType(
            TInfo, T, Loc, diag::err_typecheck_field_variable_size))
      InvalidDecl = true;
  }

  if (InvalidDecl)
    BitWidth = nullptr;
  // If this is declared as a bit-field, check the bit-field.
  if (BitWidth) {
    BitWidth =
        VerifyBitField(Loc, II, T, Record->isMsStruct(Context), BitWidth).get();
    if (!BitWidth) {
      InvalidDecl = true;
      BitWidth = nullptr;
    }
  }

  // At most one union variant may have a default initializer.

  FieldDecl *NewFD =
      FieldDecl::Create(Context, Record, TSSL, Loc, II, T, TInfo, BitWidth);
  if (InvalidDecl)
    NewFD->setInvalidDecl();

  if (PrevDecl && !isa<TagDecl>(PrevDecl)) {
    Diag(Loc, diag::err_duplicate_member) << II;
    Diag(PrevDecl->getLocation(), diag::note_previous_declaration);
    NewFD->setInvalidDecl();
  }

  // We need to pass in the attributes given an AST
  // representation, not a parser representation.
  if (D) {
    ApplyDeclAttributes(getCurScope(), NewFD, *D);

    if (NewFD->hasAttrs())
      CheckAlignasUnderalignment(NewFD);
  }

  NewFD->setAccess(AS);
  return NewFD;
}

void Sema::OnFields(Scope *S, SourceLocation RecLoc, Decl *EnclosingDecl,
                    llvm::ArrayRef<Decl *> Fields, SourceLocation LBrac,
                    SourceLocation RBrac, const ParsedAttributesView &Attrs) {
  assert(EnclosingDecl && "missing record or interface decl");

  RecordDecl *Record = dyn_cast<RecordDecl>(EnclosingDecl);
  assert(Record && "EnclosingDecl must be a RecordDecl");
  if (!Record)
    return;
  // Start counting up the number of named members; make sure to include
  // members of anonymous structs and unions in the total.
  unsigned NumNamedMembers = 0;
  for (const auto *I : Record->decls()) {
    if (const auto *IFD = dyn_cast<IndirectFieldDecl>(I))
      if (IFD->getDeclName())
        ++NumNamedMembers;
  }

  for (llvm::ArrayRef<Decl *>::iterator i = Fields.begin(), end = Fields.end();
       i != end; ++i) {
    FieldDecl *FD = cast<FieldDecl>(*i);

    const Type *FDTy = FD->getType().getTypePtr();

    // If the field is already invalid for some reason, don't emit more
    // diagnostics about it.
    if (FD->isInvalidDecl()) {
      EnclosingDecl->setInvalidDecl();
      continue;
    }

    // C99 6.7.2.1p2:
    //   A structure or union shall not contain a member with
    //   incomplete or function type (hence, a structure shall not
    //   contain an instance of itself, but may contain a pointer to
    //   an instance of itself), except that the last member of a
    //   structure with more than one named member may have incomplete
    //   array type; such a structure (and any union containing,
    //   possibly recursively, a member that is such a structure)
    //   shall not be a member of a structure or an element of an
    //   array.
    bool IsLastField = (i + 1 == Fields.end());
    if (FDTy->isFunctionType()) {
      // Field declared as a function.
      Diag(FD->getLocation(), diag::err_field_declared_as_function)
          << FD->getDeclName();
      FD->setInvalidDecl();
      EnclosingDecl->setInvalidDecl();
      continue;
    } else if (FDTy->isIncompleteArrayType()) {
      // Flexible array member.
      // Microsoft and g++ is more permissive regarding flexible array.
      // It will accept flexible array in union and also
      // as the sole element of a struct.
      unsigned DiagID = 0;
      if (!Record->isUnion() && !IsLastField) {
        Diag(FD->getLocation(), diag::err_flexible_array_not_at_end)
            << FD->getDeclName() << FD->getType()
            << llvm::to_underlying(Record->getTagKind());
        Diag((*(i + 1))->getLocation(), diag::note_next_field_declaration);
        FD->setInvalidDecl();
        EnclosingDecl->setInvalidDecl();
        continue;
      } else if (Record->isUnion())
        DiagID = getLangOpts().MicrosoftExt ? diag::ext_flexible_array_union_ms
                                            : diag::err_flexible_array_union;
      else if (NumNamedMembers < 1)
        DiagID = getLangOpts().MicrosoftExt
                     ? diag::ext_flexible_array_empty_aggregate_ms
                     : diag::err_flexible_array_empty_aggregate;

      if (DiagID)
        Diag(FD->getLocation(), DiagID)
            << FD->getDeclName() << llvm::to_underlying(Record->getTagKind());
      if (!getLangOpts().C99)
        Diag(FD->getLocation(), diag::ext_c99_flexible_array_member)
            << FD->getDeclName() << llvm::to_underlying(Record->getTagKind());

      // If the element type has non-trivial cleanup, we would not
      // implicitly destroy the elements, so disallow it for now.
      QualType BaseElem = Context.getBaseElementType(FD->getType());
      if (BaseElem.isDestructedType()) {
        Diag(FD->getLocation(), diag::err_flexible_array_has_nontrivial_dtor)
            << FD->getDeclName() << FD->getType();
        FD->setInvalidDecl();
        EnclosingDecl->setInvalidDecl();
        continue;
      }
      // Okay, we have a legal flexible array member at the end of the struct.
      Record->setHasFlexibleArrayMember(true);
    } else if (RequireCompleteSizedType(
                   FD->getLocation(), FD->getType(),
                   diag::err_field_incomplete_or_sizeless)) {
      // Incomplete type
      FD->setInvalidDecl();
      EnclosingDecl->setInvalidDecl();
      continue;
    } else if (const RecordType *FDTTy = FDTy->getAs<RecordType>()) {
      if (Record && FDTTy->getDecl()->hasFlexibleArrayMember()) {
        // A type which contains a flexible array member is considered to be a
        // flexible array member.
        Record->setHasFlexibleArrayMember(true);
        if (!Record->isUnion()) {
          // If this is a struct and this is not the last element, reject
          // it.  Note that GCC supports variable sized arrays in the middle of
          // structures.
          if (!IsLastField)
            Diag(FD->getLocation(), diag::ext_variable_sized_type_in_struct)
                << FD->getDeclName() << FD->getType();
          else {
            // We support flexible arrays at the end of structs in
            // other structs as an extension.
            Diag(FD->getLocation(), diag::ext_flexible_array_in_struct)
                << FD->getDeclName();
          }
        }
      }
      if (Record && FDTTy->getDecl()->hasVolatileMember())
        Record->setHasVolatileMember(true);
    }

    if (Record && !shouldIgnoreForRecordTriviality(FD)) {
      QualType FT = FD->getType();
      if (FT.isNonTrivialToPrimitiveDefaultInitialize()) {
        Record->setNonTrivialToPrimitiveDefaultInitialize(true);
        if (FT.hasNonTrivialToPrimitiveDefaultInitializeCUnion() ||
            Record->isUnion())
          Record->setHasNonTrivialToPrimitiveDefaultInitializeCUnion(true);
      }
      QualType::PrimitiveCopyKind PCK = FT.isNonTrivialToPrimitiveCopy();
      if (PCK != QualType::PCK_Trivial &&
          PCK != QualType::PCK_VolatileTrivial) {
        Record->setNonTrivialToPrimitiveCopy(true);
        if (FT.hasNonTrivialToPrimitiveCopyCUnion() || Record->isUnion())
          Record->setHasNonTrivialToPrimitiveCopyCUnion(true);
      }
      if (FT.isDestructedType()) {
        Record->setNonTrivialToPrimitiveDestroy(true);
        if (FT.hasNonTrivialToPrimitiveDestructCUnion() || Record->isUnion())
          Record->setHasNonTrivialToPrimitiveDestructCUnion(true);
      }
    }

    if (Record && FD->getType().isVolatileQualified())
      Record->setHasVolatileMember(true);
    // Keep track of the number of named members.
    if (FD->getIdentifier())
      ++NumNamedMembers;
  }

  // Okay, we successfully defined 'Record'.
  if (Record) {
    bool Completed = false;

    if (!Completed)
      Record->completeDefinition();

    ProcessDeclAttributeList(S, Record, Attrs);

    if (Record->hasAttrs()) {
      CheckAlignasUnderalignment(Record);
    }

    {
      bool IsEmpty = true;
      unsigned NonBitFields = 0;
      for (RecordDecl::field_iterator I = Record->field_begin(),
                                      E = Record->field_end();
           NonBitFields == 0 && I != E; ++I) {
        IsEmpty = false;
        if (!I->isUnnamedBitfield())
          ++NonBitFields;
      }

      if (NonBitFields == 0) {
        Diag(RecLoc, IsEmpty ? diag::ext_empty_struct_union
                             : diag::ext_no_named_members_in_struct_union)
            << Record->isUnion();
      }
    }
  }
}

namespace neverc {

bool isRepresentableIntegerValue(TreeContext &Context, llvm::APSInt &Value,
                                 QualType T) {
  assert((T->isIntegralType(Context) || T->isEnumeralType()) &&
         "Integral type required!");
  unsigned BitWidth = Context.getIntWidth(T);

  if (Value.isUnsigned() || Value.isNonNegative()) {
    if (T->isSignedIntegerOrEnumerationType())
      --BitWidth;
    return Value.getActiveBits() <= BitWidth;
  }
  return Value.getSignificantBits() <= BitWidth;
}

QualType getNextLargerIntegralType(TreeContext &Context, QualType T) {
  assert((T->isIntegralType(Context) || T->isEnumeralType()) &&
         "Integral type required!");
  const unsigned NumTypes = 4;
  QualType SignedIntegralTypes[NumTypes] = {Context.ShortTy, Context.IntTy,
                                            Context.LongTy, Context.LongLongTy};
  QualType UnsignedIntegralTypes[NumTypes] = {
      Context.UnsignedShortTy, Context.UnsignedIntTy, Context.UnsignedLongTy,
      Context.UnsignedLongLongTy};

  unsigned BitWidth = Context.getTypeSize(T);
  QualType *Types = T->isSignedIntegerOrEnumerationType()
                        ? SignedIntegralTypes
                        : UnsignedIntegralTypes;
  for (unsigned I = 0; I != NumTypes; ++I)
    if (Context.getTypeSize(Types[I]) > BitWidth)
      return Types[I];

  return QualType();
}

} // namespace neverc
