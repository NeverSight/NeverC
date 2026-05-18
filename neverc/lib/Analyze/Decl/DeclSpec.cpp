#include "neverc/Analyze/DeclSpec.h"
#include "neverc/Analyze/Sema.h"
#include "neverc/Analyze/SemaDiag.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Expr/Expr.h"
#include "neverc/Tree/Type/LocInfoType.h"
#include "neverc/Tree/Type/TypeLoc.h"
using namespace neverc;

// ===----------------------------------------------------------------------===
// Declaration specifiers
// ===----------------------------------------------------------------------===

DeclaratorChunk DeclaratorChunk::getFunction(
    bool hasProto, bool isAmbiguous, SourceLocation LParenLoc,
    ParamInfo *Params, unsigned NumParams, SourceLocation EllipsisLoc,
    SourceLocation RParenLoc, llvm::ArrayRef<NamedDecl *> DeclsInPrototype,
    SourceLocation LocalRangeBegin, SourceLocation LocalRangeEnd,
    Declarator &TheDeclarator, DeclSpec *MethodQualifiers) {
  assert(!(MethodQualifiers &&
           MethodQualifiers->getTypeQualifiers() & DeclSpec::TQ_atomic) &&
         "function cannot have _Atomic qualifier");

  DeclaratorChunk I;
  I.Kind = Function;
  I.Loc = LocalRangeBegin;
  I.EndLoc = LocalRangeEnd;
  new (&I.Fun) FunctionTypeInfo;
  I.Fun.hasPrototype = hasProto;
  I.Fun.isVariadic = EllipsisLoc.isValid();
  I.Fun.isAmbiguous = isAmbiguous;
  I.Fun.LParenLoc = LParenLoc;
  I.Fun.EllipsisLoc = EllipsisLoc;
  I.Fun.RParenLoc = RParenLoc;
  I.Fun.DeleteParams = false;
  I.Fun.NumParams = NumParams;
  I.Fun.Params = nullptr;
  I.Fun.NumDeclsInPrototype = 0;
  I.Fun.DeclsInPrototype = nullptr;
  I.Fun.MethodQualifiers = nullptr;
  I.Fun.QualAttrFactory = nullptr;

  if (MethodQualifiers && (MethodQualifiers->getTypeQualifiers() ||
                           MethodQualifiers->getAttributes().size())) {
    auto &attrs = MethodQualifiers->getAttributes();
    I.Fun.MethodQualifiers = new DeclSpec(attrs.getPool().getFactory());
    MethodQualifiers->forEachCVRUQualifier([&](DeclSpec::TQ TypeQual,
                                               llvm::StringRef PrintName,
                                               SourceLocation SL) {
      I.Fun.MethodQualifiers->SetTypeQual(TypeQual, SL);
    });
    I.Fun.MethodQualifiers->getAttributes().takeAllFrom(attrs);
    I.Fun.MethodQualifiers->getAttributePool().takeAllFrom(attrs.getPool());
  }

  if (NumParams) {
    if (!TheDeclarator.InlineStorageUsed &&
        NumParams <= std::size(TheDeclarator.InlineParams)) {
      I.Fun.Params = TheDeclarator.InlineParams;
      new (I.Fun.Params) ParamInfo[NumParams];
      I.Fun.DeleteParams = false;
      TheDeclarator.InlineStorageUsed = true;
    } else {
      I.Fun.Params = new DeclaratorChunk::ParamInfo[NumParams];
      I.Fun.DeleteParams = true;
    }
    for (unsigned i = 0; i < NumParams; i++)
      I.Fun.Params[i] = std::move(Params[i]);
  }

  if (!DeclsInPrototype.empty()) {
    I.Fun.NumDeclsInPrototype = DeclsInPrototype.size();
    I.Fun.DeclsInPrototype = new NamedDecl *[DeclsInPrototype.size()];
    for (size_t J = 0; J < DeclsInPrototype.size(); ++J)
      I.Fun.DeclsInPrototype[J] = DeclsInPrototype[J];
  }

  return I;
}

bool Declarator::isDeclarationOfFunction() const {
  for (unsigned i = 0, i_end = DeclTypeInfo.size(); i < i_end; ++i) {
    switch (DeclTypeInfo[i].Kind) {
    case DeclaratorChunk::Function:
      return true;
    case DeclaratorChunk::Paren:
      continue;
    case DeclaratorChunk::Pointer:
    case DeclaratorChunk::Array:
      return false;
    }
    llvm_unreachable("Invalid type chunk");
  }

  switch (DS.getTypeSpecType()) {
  case TST_atomic:
  case TST_auto:
  case TST_auto_type:
  case TST_bool:
  case TST_char:
  case TST_char8:
  case TST_char16:
  case TST_char32:
  case TST_decimal128:
  case TST_decimal32:
  case TST_decimal64:
  case TST_double:
  case TST_Accum:
  case TST_Fract:
  case TST_Float16:
  case TST_float128:
  case TST_enum:
  case TST_error:
  case TST_float:
  case TST_half:
  case TST_int:
  case TST_int128:
  case TST_bitint:
  case TST_struct:
  case TST_union:
  case TST_unspecified:
  case TST_void:
  case TST_wchar:
  case TST_BFloat16:
  case TST_i8:
  case TST_i16:
  case TST_i32:
  case TST_i64:
  case TST_i128:
  case TST_u8:
  case TST_u16:
  case TST_u32:
  case TST_u64:
  case TST_u128:
  case TST_isize:
  case TST_usize:
    return false;

  case TST_typeof_unqualExpr:
  case TST_typeofExpr:
    if (Expr *E = DS.getRepAsExpr())
      return E->getType()->isFunctionType();
    return false;

  case TST_typename:
  case TST_typeof_unqualType:
  case TST_typeofType: {
    QualType QT = DS.getRepAsType().get();
    if (QT.isNull())
      return false;

    if (const LocInfoType *LIT = dyn_cast<LocInfoType>(QT))
      QT = LIT->getType();

    if (QT.isNull())
      return false;

    return QT->isFunctionType();
  }
  }

  llvm_unreachable("Invalid TypeSpecType!");
}

void DeclSpec::forEachCVRUQualifier(
    llvm::function_ref<void(TQ, llvm::StringRef, SourceLocation)> Handle) {
  if (TypeQualifiers & TQ_const)
    Handle(TQ_const, tok::getKeywordSpelling(tok::kw_const), TQ_constLoc);
  if (TypeQualifiers & TQ_volatile)
    Handle(TQ_volatile, tok::getKeywordSpelling(tok::kw_volatile),
           TQ_volatileLoc);
  if (TypeQualifiers & TQ_restrict)
    Handle(TQ_restrict, tok::getKeywordSpelling(tok::kw_restrict),
           TQ_restrictLoc);
  if (TypeQualifiers & TQ_unaligned)
    Handle(TQ_unaligned, tok::getKeywordSpelling(tok::kw___unaligned),
           TQ_unalignedLoc);
}

void DeclSpec::forEachQualifier(
    llvm::function_ref<void(TQ, llvm::StringRef, SourceLocation)> Handle) {
  forEachCVRUQualifier(Handle);
}

bool DeclSpec::hasTagDefinition() const {
  if (!TypeSpecOwned)
    return false;
  return cast<TagDecl>(getRepAsDecl())->isCompleteDefinition();
}

unsigned DeclSpec::getParsedSpecifiers() const {
  unsigned Res = 0;
  if (StorageClassSpec != SCS_unspecified ||
      ThreadStorageClassSpec != TSCS_unspecified)
    Res |= PQ_StorageClassSpecifier;

  if (TypeQualifiers != TQ_unspecified)
    Res |= PQ_TypeQualifier;

  if (hasTypeSpecifier())
    Res |= PQ_TypeSpecifier;

  if (FS_inline_specified || FS_noreturn_specified || FS_forceinline_specified)
    Res |= PQ_FunctionSpecifier;
  return Res;
}

namespace {
template <class T>
bool badSpecifier(T TNew, T TPrev, const char *&PrevSpec, unsigned &DiagID,
                  bool IsExtension = true) {
  PrevSpec = DeclSpec::getSpecifierName(TPrev);
  if (TNew != TPrev)
    DiagID = diag::err_invalid_decl_spec_combination;
  else
    DiagID = IsExtension ? diag::ext_warn_duplicate_declspec
                         : diag::warn_duplicate_declspec;
  return true;
}
} // namespace

const char *DeclSpec::getSpecifierName(DeclSpec::SCS S) {
  switch (S) {
  case DeclSpec::SCS_unspecified:
    return "unspecified";
  case DeclSpec::SCS_typedef:
    return tok::getKeywordSpelling(tok::kw_typedef);
  case DeclSpec::SCS_extern:
    return tok::getKeywordSpelling(tok::kw_extern);
  case DeclSpec::SCS_static:
    return tok::getKeywordSpelling(tok::kw_static);
  case DeclSpec::SCS_auto:
    return tok::getKeywordSpelling(tok::kw_auto);
  case DeclSpec::SCS_register:
    return tok::getKeywordSpelling(tok::kw_register);
  case DeclSpec::SCS_private_extern:
    return tok::getKeywordSpelling(tok::kw___private_extern__);
  }
  llvm_unreachable("Unknown typespec!");
}

const char *DeclSpec::getSpecifierName(DeclSpec::TSCS S) {
  switch (S) {
  case DeclSpec::TSCS_unspecified:
    return "unspecified";
  case DeclSpec::TSCS___thread:
    return tok::getKeywordSpelling(tok::kw___thread);
  case DeclSpec::TSCS_thread_local:
    return tok::getKeywordSpelling(tok::kw_thread_local);
  case DeclSpec::TSCS__Thread_local:
    return tok::getKeywordSpelling(tok::kw__Thread_local);
  }
  llvm_unreachable("Unknown typespec!");
}

const char *DeclSpec::getSpecifierName(TypeSpecifierWidth W) {
  switch (W) {
  case TypeSpecifierWidth::Unspecified:
    return "unspecified";
  case TypeSpecifierWidth::Short:
    return tok::getKeywordSpelling(tok::kw_short);
  case TypeSpecifierWidth::Long:
    return tok::getKeywordSpelling(tok::kw_long);
  case TypeSpecifierWidth::LongLong:
    return "long long";
  }
  llvm_unreachable("Unknown typespec!");
}

const char *DeclSpec::getSpecifierName(TSC C) {
  switch (C) {
  case TSC_unspecified:
    return "unspecified";
  case TSC_imaginary:
    return tok::getKeywordSpelling(tok::kw__Imaginary);
  case TSC_complex:
    return tok::getKeywordSpelling(tok::kw__Complex);
  }
  llvm_unreachable("Unknown typespec!");
}

const char *DeclSpec::getSpecifierName(TypeSpecifierSign S) {
  switch (S) {
  case TypeSpecifierSign::Unspecified:
    return "unspecified";
  case TypeSpecifierSign::Signed:
    return tok::getKeywordSpelling(tok::kw_signed);
  case TypeSpecifierSign::Unsigned:
    return tok::getKeywordSpelling(tok::kw_unsigned);
  }
  llvm_unreachable("Unknown typespec!");
}

const char *DeclSpec::getSpecifierName(DeclSpec::TST T,
                                       const PrintingPolicy &Policy) {
  switch (T) {
  case DeclSpec::TST_unspecified:
    return "unspecified";
  case DeclSpec::TST_void:
    return tok::getKeywordSpelling(tok::kw_void);
  case DeclSpec::TST_char:
    return tok::getKeywordSpelling(tok::kw_char);
  case DeclSpec::TST_wchar:
    return Policy.MSWChar ? "__wchar_t"
                          : tok::getKeywordSpelling(tok::kw_wchar_t);
  case DeclSpec::TST_char8:
    return tok::getKeywordSpelling(tok::kw_char8_t);
  case DeclSpec::TST_char16:
    return "char16_t";
  case DeclSpec::TST_char32:
    return "char32_t";
  case DeclSpec::TST_int:
    return tok::getKeywordSpelling(tok::kw_int);
  case DeclSpec::TST_int128:
    return tok::getKeywordSpelling(tok::kw___int128);
  case DeclSpec::TST_bitint:
    return tok::getKeywordSpelling(tok::kw__BitInt);
  case DeclSpec::TST_half:
    return "__fp16";
  case DeclSpec::TST_float:
    return tok::getKeywordSpelling(tok::kw_float);
  case DeclSpec::TST_double:
    return tok::getKeywordSpelling(tok::kw_double);
  case DeclSpec::TST_accum:
    return tok::getKeywordSpelling(tok::kw__Accum);
  case DeclSpec::TST_fract:
    return tok::getKeywordSpelling(tok::kw__Fract);
  case DeclSpec::TST_float16:
    return tok::getKeywordSpelling(tok::kw__Float16);
  case DeclSpec::TST_float128:
    return tok::getKeywordSpelling(tok::kw___float128);
  case DeclSpec::TST_bool:
    return Policy.Bool ? tok::getKeywordSpelling(tok::kw_bool)
                       : tok::getKeywordSpelling(tok::kw__Bool);
  case DeclSpec::TST_decimal32:
    return tok::getKeywordSpelling(tok::kw__Decimal32);
  case DeclSpec::TST_decimal64:
    return tok::getKeywordSpelling(tok::kw__Decimal64);
  case DeclSpec::TST_decimal128:
    return tok::getKeywordSpelling(tok::kw__Decimal128);
  case DeclSpec::TST_enum:
    return tok::getKeywordSpelling(tok::kw_enum);
  case DeclSpec::TST_union:
    return tok::getKeywordSpelling(tok::kw_union);
  case DeclSpec::TST_struct:
    return tok::getKeywordSpelling(tok::kw_struct);
  case DeclSpec::TST_typename:
    return "type-name";
  case DeclSpec::TST_typeofType:
  case DeclSpec::TST_typeofExpr:
    return tok::getKeywordSpelling(tok::kw_typeof);
  case DeclSpec::TST_typeof_unqualType:
  case DeclSpec::TST_typeof_unqualExpr:
    return tok::getKeywordSpelling(tok::kw_typeof_unqual);
  case DeclSpec::TST_auto:
    return tok::getKeywordSpelling(tok::kw_auto);
  case DeclSpec::TST_auto_type:
    return tok::getKeywordSpelling(tok::kw___auto_type);
  case DeclSpec::TST_atomic:
    return tok::getKeywordSpelling(tok::kw__Atomic);
  case DeclSpec::TST_BFloat16:
    return tok::getKeywordSpelling(tok::kw___bf16);
  case DeclSpec::TST_i8:
    return tok::getKeywordSpelling(tok::kw_i8);
  case DeclSpec::TST_i16:
    return tok::getKeywordSpelling(tok::kw_i16);
  case DeclSpec::TST_i32:
    return tok::getKeywordSpelling(tok::kw_i32);
  case DeclSpec::TST_i64:
    return tok::getKeywordSpelling(tok::kw_i64);
  case DeclSpec::TST_i128:
    return tok::getKeywordSpelling(tok::kw_i128);
  case DeclSpec::TST_u8:
    return tok::getKeywordSpelling(tok::kw_u8);
  case DeclSpec::TST_u16:
    return tok::getKeywordSpelling(tok::kw_u16);
  case DeclSpec::TST_u32:
    return tok::getKeywordSpelling(tok::kw_u32);
  case DeclSpec::TST_u64:
    return tok::getKeywordSpelling(tok::kw_u64);
  case DeclSpec::TST_u128:
    return tok::getKeywordSpelling(tok::kw_u128);
  case DeclSpec::TST_isize:
    return tok::getKeywordSpelling(tok::kw_isize);
  case DeclSpec::TST_usize:
    return tok::getKeywordSpelling(tok::kw_usize);
  case DeclSpec::TST_error:
    return "(error)";
  }
  llvm_unreachable("Unknown typespec!");
}

const char *DeclSpec::getSpecifierName(ConstexprSpecKind C) {
  switch (C) {
  case ConstexprSpecKind::Unspecified:
    return "unspecified";
  case ConstexprSpecKind::Constexpr:
    return tok::getKeywordSpelling(tok::kw_constexpr);
  }
  llvm_unreachable("Unknown ConstexprSpecKind");
}

const char *DeclSpec::getSpecifierName(TQ T) {
  switch (T) {
  case DeclSpec::TQ_unspecified:
    return "unspecified";
  case DeclSpec::TQ_const:
    return tok::getKeywordSpelling(tok::kw_const);
  case DeclSpec::TQ_restrict:
    return tok::getKeywordSpelling(tok::kw_restrict);
  case DeclSpec::TQ_volatile:
    return tok::getKeywordSpelling(tok::kw_volatile);
  case DeclSpec::TQ_atomic:
    return tok::getKeywordSpelling(tok::kw__Atomic);
  case DeclSpec::TQ_unaligned:
    return tok::getKeywordSpelling(tok::kw___unaligned);
  }
  llvm_unreachable("Unknown typespec!");
}

bool DeclSpec::SetStorageClassSpec(Sema &S, SCS SC, SourceLocation Loc,
                                   const char *&PrevSpec, unsigned &DiagID,
                                   const PrintingPolicy &Policy) {
  if (StorageClassSpec != SCS_unspecified) {
    // Conflicting storage-class specifiers (e.g. two different SCS tokens).
    bool isInvalid = true;

    // Changing storage class is allowed only if the previous one
    // was the 'extern' that is part of a linkage specification and
    // the new storage class is 'typedef'.
    if (isInvalid && !(SCS_extern_in_linkage_spec &&
                       StorageClassSpec == SCS_extern && SC == SCS_typedef))
      return badSpecifier(SC, (SCS)StorageClassSpec, PrevSpec, DiagID);
  }
  StorageClassSpec = SC;
  StorageClassSpecLoc = Loc;
  assert((unsigned)SC == StorageClassSpec && "SCS constants overflow bitfield");
  return false;
}

bool DeclSpec::SetStorageClassSpecThread(TSCS TSC, SourceLocation Loc,
                                         const char *&PrevSpec,
                                         unsigned &DiagID) {
  if (ThreadStorageClassSpec != TSCS_unspecified)
    return badSpecifier(TSC, (TSCS)ThreadStorageClassSpec, PrevSpec, DiagID);

  ThreadStorageClassSpec = TSC;
  ThreadStorageClassSpecLoc = Loc;
  return false;
}

bool DeclSpec::SetTypeSpecWidth(TypeSpecifierWidth W, SourceLocation Loc,
                                const char *&PrevSpec, unsigned &DiagID,
                                const PrintingPolicy &Policy) {
  // Overwrite TSWRange.Begin only if TypeSpecWidth was unspecified, so that
  // for 'long long' we will keep the source location of the first 'long'.
  if (getTypeSpecWidth() == TypeSpecifierWidth::Unspecified)
    TSWRange.setBegin(Loc);
  // Allow turning long -> long long.
  else if (W != TypeSpecifierWidth::LongLong ||
           getTypeSpecWidth() != TypeSpecifierWidth::Long)
    return badSpecifier(W, getTypeSpecWidth(), PrevSpec, DiagID);
  TypeSpecWidth = static_cast<unsigned>(W);
  // Remember location of the last 'long'
  TSWRange.setEnd(Loc);
  return false;
}

bool DeclSpec::SetTypeSpecComplex(TSC C, SourceLocation Loc,
                                  const char *&PrevSpec, unsigned &DiagID) {
  if (TypeSpecComplex != TSC_unspecified)
    return badSpecifier(C, (TSC)TypeSpecComplex, PrevSpec, DiagID);
  TypeSpecComplex = C;
  TSCLoc = Loc;
  return false;
}

bool DeclSpec::SetTypeSpecSign(TypeSpecifierSign S, SourceLocation Loc,
                               const char *&PrevSpec, unsigned &DiagID) {
  if (getTypeSpecSign() != TypeSpecifierSign::Unspecified)
    return badSpecifier(S, getTypeSpecSign(), PrevSpec, DiagID);
  TypeSpecSign = static_cast<unsigned>(S);
  TSSLoc = Loc;
  return false;
}

bool DeclSpec::SetTypeSpecType(TST T, SourceLocation Loc, const char *&PrevSpec,
                               unsigned &DiagID, ParsedType Rep,
                               const PrintingPolicy &Policy) {
  return SetTypeSpecType(T, Loc, Loc, PrevSpec, DiagID, Rep, Policy);
}

bool DeclSpec::SetTypeSpecType(TST T, SourceLocation TagKwLoc,
                               SourceLocation TagNameLoc, const char *&PrevSpec,
                               unsigned &DiagID, ParsedType Rep,
                               const PrintingPolicy &Policy) {
  assert(isTypeRep(T) && "T does not store a type");
  assert(Rep && "no type provided!");
  if (TypeSpecType == TST_error)
    return false;
  if (TypeSpecType != TST_unspecified) {
    PrevSpec = DeclSpec::getSpecifierName((TST)TypeSpecType, Policy);
    DiagID = diag::err_invalid_decl_spec_combination;
    return true;
  }
  TypeSpecType = T;
  TypeRep = Rep;
  TSTLoc = TagKwLoc;
  TSTNameLoc = TagNameLoc;
  TypeSpecOwned = false;
  return false;
}

bool DeclSpec::SetTypeSpecType(TST T, SourceLocation Loc, const char *&PrevSpec,
                               unsigned &DiagID, Expr *Rep,
                               const PrintingPolicy &Policy) {
  assert(isExprRep(T) && "T does not store an expr");
  assert(Rep && "no expression provided!");
  if (TypeSpecType == TST_error)
    return false;
  if (TypeSpecType != TST_unspecified) {
    PrevSpec = DeclSpec::getSpecifierName((TST)TypeSpecType, Policy);
    DiagID = diag::err_invalid_decl_spec_combination;
    return true;
  }
  TypeSpecType = T;
  ExprRep = Rep;
  TSTLoc = Loc;
  TSTNameLoc = Loc;
  TypeSpecOwned = false;
  return false;
}

bool DeclSpec::SetTypeSpecType(TST T, SourceLocation Loc, const char *&PrevSpec,
                               unsigned &DiagID, Decl *Rep, bool Owned,
                               const PrintingPolicy &Policy) {
  return SetTypeSpecType(T, Loc, Loc, PrevSpec, DiagID, Rep, Owned, Policy);
}

bool DeclSpec::SetTypeSpecType(TST T, SourceLocation TagKwLoc,
                               SourceLocation TagNameLoc, const char *&PrevSpec,
                               unsigned &DiagID, Decl *Rep, bool Owned,
                               const PrintingPolicy &Policy) {
  assert(isDeclRep(T) && "T does not store a decl");
  // Unlike the other cases, we don't assert that we actually get a decl.

  if (TypeSpecType == TST_error)
    return false;
  if (TypeSpecType != TST_unspecified) {
    PrevSpec = DeclSpec::getSpecifierName((TST)TypeSpecType, Policy);
    DiagID = diag::err_invalid_decl_spec_combination;
    return true;
  }
  TypeSpecType = T;
  DeclRep = Rep;
  TSTLoc = TagKwLoc;
  TSTNameLoc = TagNameLoc;
  TypeSpecOwned = Owned && Rep != nullptr;
  return false;
}

bool DeclSpec::SetTypeSpecType(TST T, SourceLocation Loc, const char *&PrevSpec,
                               unsigned &DiagID, const PrintingPolicy &Policy) {
  assert(!isDeclRep(T) && !isTypeRep(T) && !isExprRep(T) &&
         "rep required for these type-spec kinds!");
  if (TypeSpecType == TST_error)
    return false;
  if (TypeSpecType != TST_unspecified) {
    PrevSpec = DeclSpec::getSpecifierName((TST)TypeSpecType, Policy);
    DiagID = diag::err_invalid_decl_spec_combination;
    return true;
  }
  TSTLoc = Loc;
  TSTNameLoc = Loc;
  TypeSpecType = T;
  TypeSpecOwned = false;
  return false;
}

bool DeclSpec::SetTypeSpecSat(SourceLocation Loc, const char *&PrevSpec,
                              unsigned &DiagID) {
  // Cannot set twice
  if (TypeSpecSat) {
    DiagID = diag::warn_duplicate_declspec;
    PrevSpec = tok::getKeywordSpelling(tok::kw__Sat);
    return true;
  }
  TypeSpecSat = true;
  TSSatLoc = Loc;
  return false;
}

bool DeclSpec::SetTypeSpecError() {
  TypeSpecType = TST_error;
  TypeSpecOwned = false;
  TSTLoc = SourceLocation();
  TSTNameLoc = SourceLocation();
  return false;
}

bool DeclSpec::SetBitIntType(SourceLocation KWLoc, Expr *BitsExpr,
                             const char *&PrevSpec, unsigned &DiagID,
                             const PrintingPolicy &Policy) {
  assert(BitsExpr && "no expression provided!");
  if (TypeSpecType == TST_error)
    return false;

  if (TypeSpecType != TST_unspecified) {
    PrevSpec = DeclSpec::getSpecifierName((TST)TypeSpecType, Policy);
    DiagID = diag::err_invalid_decl_spec_combination;
    return true;
  }

  TypeSpecType = TST_bitint;
  ExprRep = BitsExpr;
  TSTLoc = KWLoc;
  TSTNameLoc = KWLoc;
  TypeSpecOwned = false;
  return false;
}

bool DeclSpec::SetTypeQual(TQ T, SourceLocation Loc, const char *&PrevSpec,
                           unsigned &DiagID, const LangOptions &Lang) {
  // Duplicates are permitted in C99 onwards, but are not permitted in C89.
  // However, since this is likely not what the user intended, we will
  // always warn.  We do not need to set the qualifier's location since we
  // already have it.
  if (TypeQualifiers & T) {
    bool IsExtension = true;
    if (Lang.C99)
      IsExtension = false;
    return badSpecifier(T, T, PrevSpec, DiagID, IsExtension);
  }

  return SetTypeQual(T, Loc);
}

bool DeclSpec::SetTypeQual(TQ T, SourceLocation Loc) {
  TypeQualifiers |= T;

  switch (T) {
  case TQ_unspecified:
    break;
  case TQ_const:
    TQ_constLoc = Loc;
    return false;
  case TQ_restrict:
    TQ_restrictLoc = Loc;
    return false;
  case TQ_volatile:
    TQ_volatileLoc = Loc;
    return false;
  case TQ_unaligned:
    TQ_unalignedLoc = Loc;
    return false;
  case TQ_atomic:
    TQ_atomicLoc = Loc;
    return false;
  }

  llvm_unreachable("Unknown type qualifier!");
}

bool DeclSpec::setFunctionSpecInline(SourceLocation Loc, const char *&PrevSpec,
                                     unsigned &DiagID) {
  // 'inline inline' is ok.  However, since this is likely not what the user
  // intended, we will always warn, similar to duplicates of type qualifiers.
  if (FS_inline_specified) {
    DiagID = diag::warn_duplicate_declspec;
    PrevSpec = tok::getKeywordSpelling(tok::kw_inline);
    return true;
  }
  FS_inline_specified = true;
  FS_inlineLoc = Loc;
  return false;
}

bool DeclSpec::setFunctionSpecForceInline(SourceLocation Loc,
                                          const char *&PrevSpec,
                                          unsigned &DiagID) {
  if (FS_forceinline_specified) {
    DiagID = diag::warn_duplicate_declspec;
    PrevSpec = tok::getKeywordSpelling(tok::kw___forceinline);
    return true;
  }
  FS_forceinline_specified = true;
  FS_forceinlineLoc = Loc;
  return false;
}

bool DeclSpec::setFunctionSpecNoreturn(SourceLocation Loc,
                                       const char *&PrevSpec,
                                       unsigned &DiagID) {
  // '_Noreturn _Noreturn' is ok, but warn as this is likely not what the user
  // intended.
  if (FS_noreturn_specified) {
    DiagID = diag::warn_duplicate_declspec;
    PrevSpec = tok::getKeywordSpelling(tok::kw__Noreturn);
    return true;
  }
  FS_noreturn_specified = true;
  FS_noreturnLoc = Loc;
  return false;
}

bool DeclSpec::SetConstexprSpec(ConstexprSpecKind ConstexprKind,
                                SourceLocation Loc, const char *&PrevSpec,
                                unsigned &DiagID) {
  if (getConstexprSpecifier() != ConstexprSpecKind::Unspecified)
    return badSpecifier(ConstexprKind, getConstexprSpecifier(), PrevSpec,
                        DiagID);
  ConstexprSpecifier = static_cast<unsigned>(ConstexprKind);
  ConstexprLoc = Loc;
  return false;
}

void DeclSpec::SaveWrittenBuiltinSpecs() {
  writtenBS.Sign = static_cast<int>(getTypeSpecSign());
  writtenBS.Width = static_cast<int>(getTypeSpecWidth());
  writtenBS.Type = getTypeSpecType();
  // Search the list of attributes for the presence of a mode attribute.
  writtenBS.ModeAttr = getAttributes().hasAttribute(ParsedAttr::AT_Mode);
}

void DeclSpec::Finish(Sema &S, const PrintingPolicy &Policy) {
  // Before possibly changing their values, save specs as written.
  SaveWrittenBuiltinSpecs();

  if (TypeSpecType == TST_error)
    return;

  bool IsFixedPointType =
      TypeSpecType == TST_accum || TypeSpecType == TST_fract;

  // signed/unsigned are only valid with int/char/wchar_t/_Accum.
  if (getTypeSpecSign() != TypeSpecifierSign::Unspecified) {
    if (TypeSpecType == TST_unspecified)
      TypeSpecType = TST_int; // unsigned -> unsigned int, signed -> signed int.
    else if (TypeSpecType != TST_int && TypeSpecType != TST_int128 &&
             TypeSpecType != TST_char && TypeSpecType != TST_wchar &&
             !IsFixedPointType && TypeSpecType != TST_bitint) {
      S.Diag(TSSLoc, diag::err_invalid_sign_spec)
          << getSpecifierName((TST)TypeSpecType, Policy);
      // signed double -> double.
      TypeSpecSign = static_cast<unsigned>(TypeSpecifierSign::Unspecified);
    }
  }

  // Validate the width of the type.
  switch (getTypeSpecWidth()) {
  case TypeSpecifierWidth::Unspecified:
    break;
  case TypeSpecifierWidth::Short:    // short int
  case TypeSpecifierWidth::LongLong: // long long int
    if (TypeSpecType == TST_unspecified)
      TypeSpecType = TST_int; // short -> short int, long long -> long long int.
    else if (!(TypeSpecType == TST_int ||
               (IsFixedPointType &&
                getTypeSpecWidth() != TypeSpecifierWidth::LongLong))) {
      S.Diag(TSWRange.getBegin(), diag::err_invalid_width_spec)
          << (int)TypeSpecWidth << getSpecifierName((TST)TypeSpecType, Policy);
      TypeSpecType = TST_int;
      TypeSpecSat = false;
      TypeSpecOwned = false;
    }
    break;
  case TypeSpecifierWidth::Long: // long double, long int
    if (TypeSpecType == TST_unspecified)
      TypeSpecType = TST_int; // long -> long int.
    else if (TypeSpecType != TST_int && TypeSpecType != TST_double &&
             !IsFixedPointType) {
      S.Diag(TSWRange.getBegin(), diag::err_invalid_width_spec)
          << (int)TypeSpecWidth << getSpecifierName((TST)TypeSpecType, Policy);
      TypeSpecType = TST_int;
      TypeSpecSat = false;
      TypeSpecOwned = false;
    }
    break;
  }

  if (TypeSpecComplex != TSC_unspecified) {
    if (TypeSpecType == TST_unspecified) {
      llvm::SmallString<8> DoubleKw(" ");
      DoubleKw += tok::getKeywordSpelling(tok::kw_double);
      S.Diag(TSCLoc, diag::ext_plain_complex) << FixItHint::CreateInsertion(
          S.getLocForEndOfToken(getTypeSpecComplexLoc()), DoubleKw);
      TypeSpecType = TST_double; // _Complex -> _Complex double.
    } else if (TypeSpecType == TST_int || TypeSpecType == TST_char ||
               TypeSpecType == TST_bitint) {
      // Note that this intentionally doesn't include _Complex _Bool.
      S.Diag(TSTLoc, diag::ext_integer_complex);
    } else if (TypeSpecType != TST_float && TypeSpecType != TST_double &&
               TypeSpecType != TST_float128 && TypeSpecType != TST_float16) {
      S.Diag(TSCLoc, diag::err_invalid_complex_spec)
          << getSpecifierName((TST)TypeSpecType, Policy);
      TypeSpecComplex = TSC_unspecified;
    }
  }

  // Thread-local specifiers require 'static' or 'extern' storage class
  // (also allowing __private_extern__ as extension).
  if (ThreadStorageClassSpec != TSCS_unspecified) {
    switch (StorageClassSpec) {
    case SCS_unspecified:
    case SCS_extern:
    case SCS_private_extern:
    case SCS_static:
      break;
    default:
      if (S.getSourceManager().isBeforeInTranslationUnit(
              getThreadStorageClassSpecLoc(), getStorageClassSpecLoc()))
        S.Diag(getStorageClassSpecLoc(),
               diag::err_invalid_decl_spec_combination)
            << DeclSpec::getSpecifierName(getThreadStorageClassSpec())
            << SourceRange(getThreadStorageClassSpecLoc());
      else
        S.Diag(getThreadStorageClassSpecLoc(),
               diag::err_invalid_decl_spec_combination)
            << DeclSpec::getSpecifierName(getStorageClassSpec())
            << SourceRange(getStorageClassSpecLoc());
      // Discard the thread storage class specifier to recover.
      ThreadStorageClassSpec = TSCS_unspecified;
      ThreadStorageClassSpecLoc = SourceLocation();
    }
  }

  // Legacy: `auto` was a storage-class specifier; without C23 `auto` as a type
  // specifier, treat standalone `auto` as a deprecated type-specifier
  // extension.
  if (!S.getLangOpts().C23 && TypeSpecType == TST_auto)
    S.Diag(TSTLoc, diag::ext_auto_type_specifier);
  assert(!TypeSpecOwned || isDeclRep((TST)TypeSpecType));

  // Okay, now we can infer the real type.

  // 'data definition has no type or storage class'?
}

bool DeclSpec::isMissingDeclaratorOk() {
  TST tst = getTypeSpecType();
  return isDeclRep(tst) && getRepAsDecl() != nullptr &&
         StorageClassSpec != DeclSpec::SCS_typedef;
}
