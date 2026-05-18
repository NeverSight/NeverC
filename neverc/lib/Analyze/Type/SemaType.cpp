#include "Type/TypeLocBuilder.h"
#include "neverc/Analyze/DelayedDiagnostic.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Tree/Core/TreeConsumer.h"
#include "neverc/Tree/Type/TypeLocVisitor.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <bitset>
#include <optional>

using namespace neverc;

// ===----------------------------------------------------------------------===
// Type attribute helpers
// ===----------------------------------------------------------------------===

namespace {

enum TypeDiagSelector { TDS_Function };

void diagnoseBadTypeAttribute(Sema &S, const ParsedAttr &attr, QualType type) {
  TypeDiagSelector WhichType = TDS_Function;

  SourceLocation loc = attr.getLoc();
  llvm::StringRef name = attr.getAttrName()->getName();

  S.Diag(loc, attr.isRegularKeywordAttribute()
                  ? diag::err_type_attribute_wrong_type
                  : diag::warn_type_attribute_wrong_type)
      << name << WhichType << type;
}

// Calling convention attributes.
#define CALLING_CONV_ATTRS_CASELIST                                            \
  case ParsedAttr::AT_CDecl:                                                   \
  case ParsedAttr::AT_FastCall:                                                \
  case ParsedAttr::AT_StdCall:                                                 \
  case ParsedAttr::AT_RegCall:                                                 \
  case ParsedAttr::AT_VectorCall:                                              \
  case ParsedAttr::AT_AArch64VectorPcs:                                        \
  case ParsedAttr::AT_AArch64SVEPcs:                                           \
  case ParsedAttr::AT_MSABI:                                                   \
  case ParsedAttr::AT_SysVABI:                                                 \
  case ParsedAttr::AT_PreserveMost:                                            \
  case ParsedAttr::AT_PreserveAll

// Function type attributes.
#define FUNCTION_TYPE_ATTRS_CASELIST                                           \
  case ParsedAttr::AT_NoReturn:                                                \
  case ParsedAttr::AT_Regparm:                                                 \
  case ParsedAttr::AT_ArmStreaming:                                            \
  case ParsedAttr::AT_ArmStreamingCompatible:                                  \
  case ParsedAttr::AT_ArmSharedZA:                                             \
  case ParsedAttr::AT_ArmPreservesZA:                                          \
  case ParsedAttr::AT_AnyX86NoCallerSavedRegisters:                            \
  case ParsedAttr::AT_AnyX86NoCfCheck:                                         \
    CALLING_CONV_ATTRS_CASELIST

// Microsoft-specific type qualifiers.
#define MS_TYPE_ATTRS_CASELIST                                                 \
  case ParsedAttr::AT_Ptr32:                                                   \
  case ParsedAttr::AT_Ptr64:                                                   \
  case ParsedAttr::AT_SPtr:                                                    \
  case ParsedAttr::AT_UPtr

// Nullability qualifiers.
#define NULLABILITY_TYPE_ATTRS_CASELIST                                        \
  case ParsedAttr::AT_TypeNonNull:                                             \
  case ParsedAttr::AT_TypeNullable:                                            \
  case ParsedAttr::AT_TypeNullUnspecified

namespace {
class TypeProcessingState {
  Sema &sema;

  Declarator &declarator;

  unsigned chunkIndex;

  llvm::SmallVector<ParsedAttr *, 2> savedAttrs;

  llvm::SmallVector<ParsedAttr *, 2> ignoredTypeAttrs;

  // The two-phase mechanism by which we construct Types and fill
  // their TypeLocs makes it hard to correctly assign these. We keep the
  // attributes in creation order as an attempt to make them line up
  // properly.
  using TypeAttrPair = std::pair<const AttributedType *, const Attr *>;
  llvm::SmallVector<TypeAttrPair, 8> AttrsForTypes;
  bool AttrsForTypesSorted = true;

  llvm::DenseMap<const MacroQualifiedType *, SourceLocation> LocsForMacros;

  bool parsedNoDeref;

public:
  TypeProcessingState(Sema &sema, Declarator &declarator)
      : sema(sema), declarator(declarator),
        chunkIndex(declarator.getNumTypeObjects()), parsedNoDeref(false) {}

  Sema &getSema() const { return sema; }

  Declarator &getDeclarator() const { return declarator; }

  bool isProcessingDeclSpec() const {
    return chunkIndex == declarator.getNumTypeObjects();
  }

  unsigned getCurrentChunkIndex() const { return chunkIndex; }

  void setCurrentChunkIndex(unsigned idx) {
    assert(idx <= declarator.getNumTypeObjects());
    chunkIndex = idx;
  }

  ParsedAttributesView &getCurrentAttributes() const {
    if (isProcessingDeclSpec())
      return getMutableDeclSpec().getAttributes();
    return declarator.getTypeObject(chunkIndex).getAttrs();
  }

  void saveDeclSpecAttrs() {
    // Don't try to save them multiple times.
    if (!savedAttrs.empty())
      return;

    DeclSpec &spec = getMutableDeclSpec();
    llvm::append_range(savedAttrs,
                       llvm::make_pointer_range(spec.getAttributes()));
  }

  void addIgnoredTypeAttr(ParsedAttr &attr) {
    ignoredTypeAttrs.push_back(&attr);
  }

  void diagnoseIgnoredTypeAttrs(QualType type) const {
    for (auto *Attr : ignoredTypeAttrs)
      diagnoseBadTypeAttribute(getSema(), *Attr, type);
  }

  QualType getAttributedType(Attr *A, QualType ModifiedType,
                             QualType EquivType) {
    QualType T =
        sema.Context.getAttributedType(A->getKind(), ModifiedType, EquivType);
    AttrsForTypes.push_back({cast<AttributedType>(T.getTypePtr()), A});
    AttrsForTypesSorted = false;
    return T;
  }

  QualType getBTFTagAttributedType(const BTFTypeTagAttr *BTFAttr,
                                   QualType WrappedType) {
    return sema.Context.getBTFTagAttributedType(BTFAttr, WrappedType);
  }

  QualType ReplaceAutoType(QualType TypeWithAuto, QualType Replacement) {
    QualType T = sema.ReplaceAutoType(TypeWithAuto, Replacement);
    if (auto *AttrTy = TypeWithAuto->getAs<AttributedType>()) {
      // Attributed type still should be an attributed type after replacement.
      auto *NewAttrTy = cast<AttributedType>(T.getTypePtr());
      for (TypeAttrPair &A : AttrsForTypes) {
        if (A.first == AttrTy)
          A.first = NewAttrTy;
      }
      AttrsForTypesSorted = false;
    }
    return T;
  }

  const Attr *takeAttrForAttributedType(const AttributedType *AT) {
    if (!AttrsForTypesSorted) {
      llvm::stable_sort(AttrsForTypes, llvm::less_first());
      AttrsForTypesSorted = true;
    }

    for (auto It = std::partition_point(
             AttrsForTypes.begin(), AttrsForTypes.end(),
             [=](const TypeAttrPair &A) { return A.first < AT; });
         It != AttrsForTypes.end() && It->first == AT; ++It) {
      if (It->second) {
        const Attr *Result = It->second;
        It->second = nullptr;
        return Result;
      }
    }

    llvm_unreachable("no Attr* for AttributedType*");
  }

  SourceLocation
  getExpansionLocForMacroQualifiedType(const MacroQualifiedType *MQT) const {
    auto FoundLoc = LocsForMacros.find(MQT);
    assert(FoundLoc != LocsForMacros.end() &&
           "Unable to find macro expansion location for MacroQualifedType");
    return FoundLoc->second;
  }

  void setExpansionLocForMacroQualifiedType(const MacroQualifiedType *MQT,
                                            SourceLocation Loc) {
    LocsForMacros[MQT] = Loc;
  }

  void setParsedNoDeref(bool parsed) { parsedNoDeref = parsed; }

  bool didParseNoDeref() const { return parsedNoDeref; }

  ~TypeProcessingState() {
    if (savedAttrs.empty())
      return;

    getMutableDeclSpec().getAttributes().clearListOnly();
    for (ParsedAttr *AL : savedAttrs)
      getMutableDeclSpec().getAttributes().addAtEnd(AL);
  }

private:
  DeclSpec &getMutableDeclSpec() const {
    return const_cast<DeclSpec &>(declarator.getDeclSpec());
  }
};
} // end anonymous namespace

void moveAttrFromListToList(ParsedAttr &attr, ParsedAttributesView &fromList,
                            ParsedAttributesView &toList) {
  fromList.remove(&attr);
  toList.addAtEnd(&attr);
}

enum TypeAttrLocation { TAL_DeclSpec, TAL_DeclChunk, TAL_DeclName };

void processTypeAttrs(TypeProcessingState &state, QualType &type,
                      TypeAttrLocation TAL, const ParsedAttributesView &attrs);

bool handleFunctionTypeAttr(TypeProcessingState &state, ParsedAttr &attr,
                            QualType &type);

bool handleMSPointerTypeQualifierAttr(TypeProcessingState &state,
                                      ParsedAttr &attr, QualType &type);

DeclaratorChunk *maybeMovePastReturnType(Declarator &declarator, unsigned i,
                                         bool) {
  assert(i <= declarator.getNumTypeObjects());

  DeclaratorChunk *result = nullptr;

  for (; i != 0; --i) {
    DeclaratorChunk &fnChunk = declarator.getTypeObject(i - 1);
    switch (fnChunk.Kind) {
    case DeclaratorChunk::Paren:
      continue;

    case DeclaratorChunk::Pointer:
    case DeclaratorChunk::Array:
      return result;

    case DeclaratorChunk::Function:
      for (--i; i != 0; --i) {
        DeclaratorChunk &ptrChunk = declarator.getTypeObject(i - 1);
        switch (ptrChunk.Kind) {
        case DeclaratorChunk::Paren:
        case DeclaratorChunk::Array:
        case DeclaratorChunk::Function:
          continue;

        case DeclaratorChunk::Pointer:
          result = &ptrChunk;
          goto continue_outer;
        }
        llvm_unreachable("bad declarator chunk kind");
      }

      // If we run out of declarators doing that, we're done.
      return result;
    }
    llvm_unreachable("bad declarator chunk kind");

    // Okay, reconsider from our new point.
  continue_outer:;
  }

  // Ran out of chunks, bail out.
  return result;
}

void distributeFunctionTypeAttr(TypeProcessingState &state, ParsedAttr &attr,
                                QualType type) {
  Declarator &declarator = state.getDeclarator();

  // Try to push the attribute from the return type of a function to
  // the function itself.
  for (unsigned i = state.getCurrentChunkIndex(); i != 0; --i) {
    DeclaratorChunk &chunk = declarator.getTypeObject(i - 1);
    switch (chunk.Kind) {
    case DeclaratorChunk::Function:
      moveAttrFromListToList(attr, state.getCurrentAttributes(),
                             chunk.getAttrs());
      return;

    case DeclaratorChunk::Paren:
    case DeclaratorChunk::Pointer:
    case DeclaratorChunk::Array:
      continue;
    }
  }

  diagnoseBadTypeAttribute(state.getSema(), attr, type);
}

bool distributeFunctionTypeAttrToInnermost(TypeProcessingState &state,
                                           ParsedAttr &attr,
                                           ParsedAttributesView &attrList,
                                           QualType &declSpecType) {
  Declarator &declarator = state.getDeclarator();

  // Put it on the innermost function chunk, if there is one.
  for (unsigned i = 0, e = declarator.getNumTypeObjects(); i != e; ++i) {
    DeclaratorChunk &chunk = declarator.getTypeObject(i);
    if (chunk.Kind != DeclaratorChunk::Function)
      continue;

    moveAttrFromListToList(attr, attrList, chunk.getAttrs());
    return true;
  }

  return handleFunctionTypeAttr(state, attr, declSpecType);
}

void distributeFunctionTypeAttrFromDeclSpec(TypeProcessingState &state,
                                            ParsedAttr &attr,
                                            QualType &declSpecType) {
  state.saveDeclSpecAttrs();

  // Try to distribute to the innermost.
  if (distributeFunctionTypeAttrToInnermost(
          state, attr, state.getCurrentAttributes(), declSpecType))
    return;

  // If that failed, diagnose the bad attribute when the declarator is
  // fully built.
  state.addIgnoredTypeAttr(attr);
}

void distributeFunctionTypeAttrFromDeclarator(TypeProcessingState &state,
                                              ParsedAttr &attr,
                                              QualType &declSpecType) {
  Declarator &declarator = state.getDeclarator();

  // Try to distribute to the innermost.
  if (distributeFunctionTypeAttrToInnermost(
          state, attr, declarator.getAttributes(), declSpecType))
    return;

  // If that failed, diagnose the bad attribute when the declarator is
  // fully built.
  declarator.getAttributes().remove(&attr);
  state.addIgnoredTypeAttr(attr);
}

void distributeTypeAttrsFromDeclarator(TypeProcessingState &state,
                                       QualType &declSpecType) {
  // The called functions in this loop actually remove things from the current
  // list, so iterating over the existing list isn't possible.  Instead, make a
  // non-owning copy and iterate over that.
  ParsedAttributesView AttrsCopy{state.getDeclarator().getAttributes()};
  for (ParsedAttr &attr : AttrsCopy) {
    // Do not distribute [[]] attributes. They have strict rules for what
    // they appertain to.
    if (attr.isStandardAttributeSyntax() || attr.isRegularKeywordAttribute())
      continue;

    switch (attr.getKind()) {
    FUNCTION_TYPE_ATTRS_CASELIST:
      distributeFunctionTypeAttrFromDeclarator(state, attr, declSpecType);
      break;

    MS_TYPE_ATTRS_CASELIST:
      // Microsoft type attributes cannot go after the declarator-id.
      continue;

    NULLABILITY_TYPE_ATTRS_CASELIST:
      // Nullability specifiers cannot go after the declarator-id.
      continue;

    default:
      break;
    }
  }
}

void diagnoseAndRemoveTypeQualifiers(Sema &S, const DeclSpec &DS,
                                     unsigned &TypeQuals, QualType TypeSoFar,
                                     unsigned RemoveTQs, unsigned DiagID) {
  // Warn the user about redundant qualifiers.
  typedef std::pair<DeclSpec::TQ, SourceLocation> QualLoc;
  for (QualLoc Qual : {QualLoc(DeclSpec::TQ_const, DS.getConstSpecLoc()),
                       QualLoc(DeclSpec::TQ_restrict, DS.getRestrictSpecLoc()),
                       QualLoc(DeclSpec::TQ_volatile, DS.getVolatileSpecLoc()),
                       QualLoc(DeclSpec::TQ_atomic, DS.getAtomicSpecLoc())}) {
    if (!(RemoveTQs & Qual.first))
      continue;

    if (TypeQuals & Qual.first)
      S.Diag(Qual.second, DiagID)
          << DeclSpec::getSpecifierName(Qual.first) << TypeSoFar
          << FixItHint::CreateRemoval(Qual.second);

    TypeQuals &= ~Qual.first;
  }
}

QualType convertDeclSpecToType(TypeProcessingState &state) {

  Sema &S = state.getSema();
  Declarator &declarator = state.getDeclarator();
  DeclSpec &DS = declarator.getMutableDeclSpec();
  SourceLocation DeclLoc = declarator.getIdentifierLoc();
  if (DeclLoc.isInvalid())
    DeclLoc = DS.getBeginLoc();

  TreeContext &Context = S.Context;

  QualType Result;
  switch (DS.getTypeSpecType()) {
  case DeclSpec::TST_void:
    Result = Context.VoidTy;
    break;
  case DeclSpec::TST_char:
    if (DS.getTypeSpecSign() == TypeSpecifierSign::Unspecified)
      Result = Context.CharTy;
    else if (DS.getTypeSpecSign() == TypeSpecifierSign::Signed)
      Result = Context.SignedCharTy;
    else {
      assert(DS.getTypeSpecSign() == TypeSpecifierSign::Unsigned &&
             "Unknown TSS value");
      Result = Context.UnsignedCharTy;
    }
    break;
  case DeclSpec::TST_wchar:
    if (DS.getTypeSpecSign() == TypeSpecifierSign::Unspecified)
      Result = Context.WCharTy;
    else if (DS.getTypeSpecSign() == TypeSpecifierSign::Signed) {
      S.Diag(DS.getTypeSpecSignLoc(), diag::ext_wchar_t_sign_spec)
          << DS.getSpecifierName(DS.getTypeSpecType(),
                                 Context.getPrintingPolicy());
      Result = Context.getSignedWCharType();
    } else {
      assert(DS.getTypeSpecSign() == TypeSpecifierSign::Unsigned &&
             "Unknown TSS value");
      S.Diag(DS.getTypeSpecSignLoc(), diag::ext_wchar_t_sign_spec)
          << DS.getSpecifierName(DS.getTypeSpecType(),
                                 Context.getPrintingPolicy());
      Result = Context.getUnsignedWCharType();
    }
    break;
  case DeclSpec::TST_char8:
    assert(DS.getTypeSpecSign() == TypeSpecifierSign::Unspecified &&
           "Unknown TSS value");
    Result = Context.Char8Ty;
    break;
  case DeclSpec::TST_char16:
    assert(DS.getTypeSpecSign() == TypeSpecifierSign::Unspecified &&
           "Unknown TSS value");
    Result = Context.Char16Ty;
    break;
  case DeclSpec::TST_char32:
    assert(DS.getTypeSpecSign() == TypeSpecifierSign::Unspecified &&
           "Unknown TSS value");
    Result = Context.Char32Ty;
    break;
  case DeclSpec::TST_unspecified:
    // Unspecified typespec defaults to int in C90.  However, the C90 grammar
    // [C90 6.5] only allows a decl-spec if there was *some* type-specifier,
    // type-qualifier, or storage-class-specifier.  If not, emit an extwarn.
    // Note that the one exception to this is function definitions, which are
    // allowed to be completely missing a declspec.  This is handled in the
    // parser already though by it pretending to have seen an 'int' in this
    // case.
    if (S.getLangOpts().isImplicitIntRequired()) {
      S.Diag(DeclLoc, diag::warn_missing_type_specifier)
          << DS.getSourceRange()
          << FixItHint::CreateInsertion(DS.getBeginLoc(),
                                        tok::getKeywordSpelling(tok::kw_int));
    } else if (!DS.hasTypeSpecifier()) {
      // C99 6.7.2p2: each declaration and struct member list needs a type
      // specifier.
      if (!S.getLangOpts().isImplicitIntAllowed()) {
        S.Diag(DeclLoc, diag::err_missing_type_specifier)
            << DS.getSourceRange();

        // When this occurs, often something is very broken with the value
        // being declared, poison it as invalid so we don't get chains of
        // errors.
        declarator.setInvalidType(true);
      } else {
        assert(S.getLangOpts().isImplicitIntAllowed() &&
               "implicit int is disabled?");
        S.Diag(DeclLoc, diag::ext_missing_type_specifier)
            << DS.getSourceRange()
            << FixItHint::CreateInsertion(DS.getBeginLoc(),
                                          tok::getKeywordSpelling(tok::kw_int));
      }
    }

    [[fallthrough]];
  case DeclSpec::TST_int: {
    if (DS.getTypeSpecSign() != TypeSpecifierSign::Unsigned) {
      switch (DS.getTypeSpecWidth()) {
      case TypeSpecifierWidth::Unspecified:
        Result = Context.IntTy;
        break;
      case TypeSpecifierWidth::Short:
        Result = Context.ShortTy;
        break;
      case TypeSpecifierWidth::Long:
        Result = Context.LongTy;
        break;
      case TypeSpecifierWidth::LongLong:
        Result = Context.LongLongTy;

        if (!S.getLangOpts().C99)
          S.Diag(DS.getTypeSpecWidthLoc(), diag::ext_c99_longlong);
        break;
      }
    } else {
      switch (DS.getTypeSpecWidth()) {
      case TypeSpecifierWidth::Unspecified:
        Result = Context.UnsignedIntTy;
        break;
      case TypeSpecifierWidth::Short:
        Result = Context.UnsignedShortTy;
        break;
      case TypeSpecifierWidth::Long:
        Result = Context.UnsignedLongTy;
        break;
      case TypeSpecifierWidth::LongLong:
        Result = Context.UnsignedLongLongTy;

        // `long long` requires C99 or later.
        if (!S.getLangOpts().C99)
          S.Diag(DS.getTypeSpecWidthLoc(), diag::ext_c99_longlong);
        break;
      }
    }
    break;
  }
  case DeclSpec::TST_bitint: {
    if (!S.Context.getTargetInfo().hasBitIntType())
      S.Diag(DS.getTypeSpecTypeLoc(), diag::err_type_unsupported)
          << tok::getKeywordSpelling(tok::kw__BitInt);
    Result =
        S.FormBitIntType(DS.getTypeSpecSign() == TypeSpecifierSign::Unsigned,
                         DS.getRepAsExpr(), DS.getBeginLoc());
    if (Result.isNull()) {
      Result = Context.IntTy;
      declarator.setInvalidType(true);
    }
    break;
  }
  case DeclSpec::TST_accum: {
    switch (DS.getTypeSpecWidth()) {
    case TypeSpecifierWidth::Short:
      Result = Context.ShortAccumTy;
      break;
    case TypeSpecifierWidth::Unspecified:
      Result = Context.AccumTy;
      break;
    case TypeSpecifierWidth::Long:
      Result = Context.LongAccumTy;
      break;
    case TypeSpecifierWidth::LongLong:
      llvm_unreachable("Unable to specify long long as _Accum width");
    }

    if (DS.getTypeSpecSign() == TypeSpecifierSign::Unsigned)
      Result = Context.getCorrespondingUnsignedType(Result);

    if (DS.isTypeSpecSat())
      Result = Context.getCorrespondingSaturatedType(Result);

    break;
  }
  case DeclSpec::TST_fract: {
    switch (DS.getTypeSpecWidth()) {
    case TypeSpecifierWidth::Short:
      Result = Context.ShortFractTy;
      break;
    case TypeSpecifierWidth::Unspecified:
      Result = Context.FractTy;
      break;
    case TypeSpecifierWidth::Long:
      Result = Context.LongFractTy;
      break;
    case TypeSpecifierWidth::LongLong:
      llvm_unreachable("Unable to specify long long as _Fract width");
    }

    if (DS.getTypeSpecSign() == TypeSpecifierSign::Unsigned)
      Result = Context.getCorrespondingUnsignedType(Result);

    if (DS.isTypeSpecSat())
      Result = Context.getCorrespondingSaturatedType(Result);

    break;
  }
  case DeclSpec::TST_int128:
    if (!S.Context.getTargetInfo().hasInt128Type())
      S.Diag(DS.getTypeSpecTypeLoc(), diag::err_type_unsupported)
          << tok::getKeywordSpelling(tok::kw___int128);
    if (DS.getTypeSpecSign() == TypeSpecifierSign::Unsigned)
      Result = Context.UnsignedInt128Ty;
    else
      Result = Context.Int128Ty;
    break;
  // NeverC Rust-style fixed-width integer types
  case DeclSpec::TST_i8:
    Result = Context.SignedCharTy;
    break;
  case DeclSpec::TST_i16:
    Result = Context.ShortTy;
    break;
  case DeclSpec::TST_i32:
    Result = Context.IntTy;
    break;
  case DeclSpec::TST_i64:
    Result = Context.LongLongTy;
    break;
  case DeclSpec::TST_i128:
    if (!S.Context.getTargetInfo().hasInt128Type())
      S.Diag(DS.getTypeSpecTypeLoc(), diag::err_type_unsupported) << "i128";
    Result = Context.Int128Ty;
    break;
  case DeclSpec::TST_u8:
    Result = Context.UnsignedCharTy;
    break;
  case DeclSpec::TST_u16:
    Result = Context.UnsignedShortTy;
    break;
  case DeclSpec::TST_u32:
    Result = Context.UnsignedIntTy;
    break;
  case DeclSpec::TST_u64:
    Result = Context.UnsignedLongLongTy;
    break;
  case DeclSpec::TST_u128:
    if (!S.Context.getTargetInfo().hasInt128Type())
      S.Diag(DS.getTypeSpecTypeLoc(), diag::err_type_unsupported) << "u128";
    Result = Context.UnsignedInt128Ty;
    break;
  case DeclSpec::TST_isize:
    Result = Context.getIntPtrType();
    break;
  case DeclSpec::TST_usize:
    Result = Context.getUIntPtrType();
    break;

  case DeclSpec::TST_float16:
    if (!S.Context.getTargetInfo().hasFloat16Type())
      S.Diag(DS.getTypeSpecTypeLoc(), diag::err_type_unsupported)
          << tok::getKeywordSpelling(tok::kw__Float16);
    Result = Context.Float16Ty;
    break;
  case DeclSpec::TST_half:
    Result = Context.HalfTy;
    break;
  case DeclSpec::TST_BFloat16:
    if (!S.Context.getTargetInfo().hasBFloat16Type())
      S.Diag(DS.getTypeSpecTypeLoc(), diag::err_type_unsupported)
          << tok::getKeywordSpelling(tok::kw___bf16);
    Result = Context.BFloat16Ty;
    break;
  case DeclSpec::TST_float:
    Result = Context.FloatTy;
    break;
  case DeclSpec::TST_double:
    if (DS.getTypeSpecWidth() == TypeSpecifierWidth::Long)
      Result = Context.LongDoubleTy;
    else
      Result = Context.DoubleTy;
    break;
  case DeclSpec::TST_float128:
    if (!S.Context.getTargetInfo().hasFloat128Type())
      S.Diag(DS.getTypeSpecTypeLoc(), diag::err_type_unsupported)
          << tok::getKeywordSpelling(tok::kw___float128);
    Result = Context.Float128Ty;
    break;
  case DeclSpec::TST_bool:
    Result = Context.BoolTy; // _Bool or bool
    break;
  case DeclSpec::TST_decimal32:  // _Decimal32
  case DeclSpec::TST_decimal64:  // _Decimal64
  case DeclSpec::TST_decimal128: // _Decimal128
    S.Diag(DS.getTypeSpecTypeLoc(), diag::err_decimal_unsupported);
    Result = Context.IntTy;
    declarator.setInvalidType(true);
    break;
  case DeclSpec::TST_enum:
  case DeclSpec::TST_union:
  case DeclSpec::TST_struct: {
    TagDecl *D = dyn_cast_or_null<TagDecl>(DS.getRepAsDecl());
    if (!D) {
      // Missing tag decl after lookup (parse error recovery).
      Result = Context.IntTy;
      declarator.setInvalidType(true);
      break;
    }

    // If the type is deprecated or unavailable, diagnose it.
    S.CheckDeclUsage(D, DS.getTypeSpecTypeNameLoc());

    assert(DS.getTypeSpecWidth() == TypeSpecifierWidth::Unspecified &&
           DS.getTypeSpecComplex() == 0 &&
           DS.getTypeSpecSign() == TypeSpecifierSign::Unspecified &&
           "No qualifiers on tag names!");

    // TypeQuals handled by caller.
    Result = Context.getTypeDeclType(D);

    // Wrap in ElaboratedType (struct/union/enum keyword).
    ElaboratedTypeKeyword Keyword =
        ElaboratedType::getKeywordForTypeSpec(DS.getTypeSpecType());
    Result = S.getElaboratedType(Keyword, Result,
                                 DS.isTypeSpecOwned() ? D : nullptr);

    break;
  }
  case DeclSpec::TST_typename: {
    assert(DS.getTypeSpecWidth() == TypeSpecifierWidth::Unspecified &&
           DS.getTypeSpecComplex() == 0 &&
           DS.getTypeSpecSign() == TypeSpecifierSign::Unspecified &&
           "Can't handle qualifiers on typedef names yet!");
    Result = S.GetTypeFromParser(DS.getRepAsType());
    if (Result.isNull()) {
      declarator.setInvalidType(true);
    }
    break;
  }
  case DeclSpec::TST_typeof_unqualType:
  case DeclSpec::TST_typeofType:
    Result = S.GetTypeFromParser(DS.getRepAsType());
    assert(!Result.isNull() && "Didn't get a type for typeof?");
    if (const TagType *TT = Result->getAs<TagType>())
      S.CheckDeclUsage(TT->getDecl(), DS.getTypeSpecTypeLoc());
    // TypeQuals handled by caller.
    Result = Context.getTypeOfType(
        Result, DS.getTypeSpecType() == DeclSpec::TST_typeof_unqualType
                    ? TypeOfKind::Unqualified
                    : TypeOfKind::Qualified);
    break;
  case DeclSpec::TST_typeof_unqualExpr:
  case DeclSpec::TST_typeofExpr: {
    Expr *E = DS.getRepAsExpr();
    assert(E && "Didn't get an expression for typeof?");
    // TypeQuals handled by caller.
    Result = S.FormTypeofExprType(E, DS.getTypeSpecType() ==
                                             DeclSpec::TST_typeof_unqualExpr
                                         ? TypeOfKind::Unqualified
                                         : TypeOfKind::Qualified);
    if (Result.isNull()) {
      Result = Context.IntTy;
      declarator.setInvalidType(true);
    }
    break;
  }
  case DeclSpec::TST_auto: {
    auto AutoKW = AutoTypeKeyword::Auto;

    Result = S.Context.getAutoType(QualType(), AutoKW,
                                   /*IsDependent*/ false);
    break;
  }

  case DeclSpec::TST_auto_type:
    Result =
        Context.getAutoType(QualType(), AutoTypeKeyword::GNUAutoType, false);
    break;

  case DeclSpec::TST_atomic:
    Result = S.GetTypeFromParser(DS.getRepAsType());
    assert(!Result.isNull() && "Didn't get a type for _Atomic?");
    Result = S.FormAtomicType(Result, DS.getTypeSpecTypeLoc());
    if (Result.isNull()) {
      Result = Context.IntTy;
      declarator.setInvalidType(true);
    }
    break;

  case DeclSpec::TST_error:
    Result = Context.IntTy;
    declarator.setInvalidType(true);
    break;
  }

  // We want resulting declarations to be marked invalid, but claiming
  // the type is invalid is too strong - e.g. it causes OnTypeName to return
  // a null type.
  if (Result->containsErrors())
    declarator.setInvalidType();

  bool IsFixedPointType = DS.getTypeSpecType() == DeclSpec::TST_accum ||
                          DS.getTypeSpecType() == DeclSpec::TST_fract;

  // Only fixed point types can be saturated
  if (DS.isTypeSpecSat() && !IsFixedPointType)
    S.Diag(DS.getTypeSpecSatLoc(), diag::err_invalid_saturation_spec)
        << DS.getSpecifierName(DS.getTypeSpecType(),
                               Context.getPrintingPolicy());
  if (DS.getTypeSpecComplex() == DeclSpec::TSC_complex) {
    if (S.getLangOpts().Freestanding)
      S.Diag(DS.getTypeSpecComplexLoc(), diag::ext_freestanding_complex);
    Result = Context.getComplexType(Result);
  }

  if (DS.getTypeSpecComplex() == DeclSpec::TSC_imaginary)
    S.Diag(DS.getTypeSpecComplexLoc(), diag::err_imaginary_not_supported);

  // Apply any type attributes from the decl spec.  This may cause the
  // list of type attributes to be temporarily saved while the type
  // attributes are pushed around.
  {
    ParsedAttributesView SlidingAttrs;
    for (ParsedAttr &AL : declarator.getDeclarationAttributes()) {
      if (AL.slidesFromDeclToDeclSpecLegacyBehavior()) {
        SlidingAttrs.addAtEnd(&AL);

        if (AL.isStandardAttributeSyntax() && AL.isNeverCScope() &&
            !(AL.getKind() == ParsedAttr::AT_MatrixType &&
              DS.getStorageClassSpec() != DeclSpec::SCS_typedef)) {
          S.Diag(AL.getLoc(), diag::warn_type_attribute_deprecated_on_decl)
              << AL;
        }
      }
    }
    processTypeAttrs(state, Result, TAL_DeclSpec, SlidingAttrs);
    processTypeAttrs(state, Result, TAL_DeclSpec, DS.getAttributes());
  }

  // Apply const/volatile/restrict qualifiers to T.
  if (unsigned TypeQuals = DS.getTypeQualifiers()) {
    // C99 6.7.3p8: qualifiers on a function type have undefined behavior; strip
    // them with a diagnostic.
    if (Result->isFunctionType()) {
      diagnoseAndRemoveTypeQualifiers(
          S, DS, TypeQuals, Result, DeclSpec::TQ_const | DeclSpec::TQ_volatile,
          diag::warn_typecheck_function_qualifiers_unspecified);
      // No diagnostic for 'restrict' or '_Atomic' applied to a
      // function type; we'll diagnose those later, in FormQualifiedType.
    }

    // C90 6.5.3 constraints: "The same type qualifier shall not appear more
    // than once in the same specifier-list or qualifier-list, either directly
    // or via one or more typedefs."
    if (!S.getLangOpts().C99 && TypeQuals & Result.getCVRQualifiers()) {
      if (TypeQuals & DeclSpec::TQ_const && Result.isConstQualified()) {
        S.Diag(DS.getConstSpecLoc(), diag::ext_duplicate_declspec)
            << tok::getKeywordSpelling(tok::kw_const);
      }

      if (TypeQuals & DeclSpec::TQ_volatile && Result.isVolatileQualified()) {
        S.Diag(DS.getVolatileSpecLoc(), diag::ext_duplicate_declspec)
            << tok::getKeywordSpelling(tok::kw_volatile);
      }

      // C90 doesn't have restrict nor _Atomic, so it doesn't force us to
      // produce a warning in this case.
    }

    QualType Qualified = S.FormQualifiedType(Result, DeclLoc, TypeQuals, &DS);

    // If adding qualifiers fails, just use the unqualified type.
    if (Qualified.isNull())
      declarator.setInvalidType(true);
    else
      Result = Qualified;
  }

  assert(!Result.isNull() && "This function should not return a null type");
  return Result;
}

std::string getPrintableNameForEntity(DeclarationName Entity) {
  if (Entity)
    return Entity.getAsString();

  return "type name";
}

bool isGNUAutoType(QualType T) {
  const auto *AT = dyn_cast<AutoType>(T);
  return AT && AT->isGNUAutoType();
}

} // namespace

// ===----------------------------------------------------------------------===
// Type formation
// ===----------------------------------------------------------------------===

QualType Sema::FormQualifiedType(QualType T, SourceLocation Loc, Qualifiers Qs,
                                 const DeclSpec *DS) {
  if (T.isNull())
    return QualType();

  // Enforce C99 6.7.3p2: "Types other than pointer types derived from
  // object or incomplete types shall not be restrict-qualified."
  if (Qs.hasRestrict()) {
    unsigned DiagID = 0;
    QualType ProblemTy;

    if (T->isAnyPointerType()) {
      QualType EltTy = T->getPointeeType();

      // If we have a pointer or reference, the pointee must have an object
      // incomplete type.
      if (!EltTy->isIncompleteOrObjectType()) {
        DiagID = diag::err_typecheck_invalid_restrict_invalid_pointee;
        ProblemTy = EltTy;
      }
    } else if (!isGNUAutoType(T)) {
      // For an __auto_type variable, we may not have seen the initializer yet
      // and so have no idea whether the underlying type is a pointer type or
      // not.
      DiagID = diag::err_typecheck_invalid_restrict_not_pointer;
      ProblemTy = T;
    }

    if (DiagID) {
      Diag(DS ? DS->getRestrictSpecLoc() : Loc, DiagID) << ProblemTy;
      Qs.removeRestrict();
    }
  }

  return Context.getQualifiedType(T, Qs);
}

QualType Sema::FormQualifiedType(QualType T, SourceLocation Loc, unsigned CVRAU,
                                 const DeclSpec *DS) {
  if (T.isNull())
    return QualType();

  // Convert from DeclSpec::TQ to Qualifiers::TQ by just dropping TQ_atomic and
  // TQ_unaligned;
  unsigned CVR = CVRAU & ~(DeclSpec::TQ_atomic | DeclSpec::TQ_unaligned);

  // C11 6.7.3/5:
  //   If the same qualifier appears more than once in the same
  //   specifier-qualifier-list, either directly or via one or more typedefs,
  //   the behavior is the same as if it appeared only once.
  //
  // It's not specified what happens when the _Atomic qualifier is applied to
  // a type specified with the _Atomic specifier, but we assume that this
  // should be treated as if the _Atomic qualifier appeared multiple times.
  if (CVRAU & DeclSpec::TQ_atomic && !T->isAtomicType()) {
    // C11 6.7.3/5:
    //   If other qualifiers appear along with the _Atomic qualifier in a
    //   specifier-qualifier-list, the resulting type is the so-qualified
    //   atomic type.
    //
    // Don't need to worry about array types here, since _Atomic can't be
    // applied to such types.
    SplitQualType Split = T.getSplitUnqualifiedType();
    T = FormAtomicType(QualType(Split.Ty, 0),
                       DS ? DS->getAtomicSpecLoc() : Loc);
    if (T.isNull())
      return T;
    Split.Quals.addCVRQualifiers(CVR);
    return FormQualifiedType(T, Loc, Split.Quals);
  }

  Qualifiers Q = Qualifiers::fromCVRMask(CVR);
  Q.setUnaligned(CVRAU & DeclSpec::TQ_unaligned);
  return FormQualifiedType(T, Loc, Q, DS);
}

QualType Sema::FormParenType(QualType T) { return Context.getParenType(T); }

std::string getFunctionQualifiersAsString(const FunctionProtoType *FnTy) {
  return FnTy->getMethodQuals().getAsString();
}

bool checkQualifiedFunction(Sema &S, QualType T, SourceLocation Loc) {
  const FunctionProtoType *FPT = T->getAs<FunctionProtoType>();
  if (!FPT || FPT->getMethodQuals().empty())
    return false;

  S.Diag(Loc, diag::err_compound_qualified_function_type)
      << isa<FunctionType>(T.IgnoreParens()) << T
      << getFunctionQualifiersAsString(FPT);
  return true;
}

QualType Sema::FormPointerType(QualType T, SourceLocation Loc,
                               DeclarationName Entity) {
  if (checkQualifiedFunction(*this, T, Loc))
    return QualType();

  return Context.getPointerType(T);
}

QualType Sema::FormBitIntType(bool IsUnsigned, Expr *BitWidth,
                              SourceLocation Loc) {
  llvm::APSInt Bits(32);
  ExprResult ICE = VerifyIntegerConstantExpression(BitWidth, &Bits, AllowFold);

  if (ICE.isInvalid())
    return QualType();

  size_t NumBits = Bits.getZExtValue();
  if (!IsUnsigned && NumBits < 2) {
    Diag(Loc, diag::err_bit_int_bad_size) << 0;
    return QualType();
  }

  if (IsUnsigned && NumBits < 1) {
    Diag(Loc, diag::err_bit_int_bad_size) << 1;
    return QualType();
  }

  const TargetInfo &TI = getTreeContext().getTargetInfo();
  if (NumBits > TI.getMaxBitIntWidth()) {
    Diag(Loc, diag::err_bit_int_max_size)
        << IsUnsigned << static_cast<uint64_t>(TI.getMaxBitIntWidth());
    return QualType();
  }

  return Context.getBitIntType(IsUnsigned, NumBits);
}

ExprResult checkArraySize(Sema &S, Expr *&ArraySize, llvm::APSInt &SizeVal,
                          unsigned VLADiag, bool VLAIsError) {
  // If the size is an ICE, it certainly isn't a VLA. If we're in a GNU mode
  // (like gnu99, but not c99) accept any evaluatable value as an extension.
  class VLADiagnoser : public Sema::VerifyICEDiagnoser {
  public:
    unsigned VLADiag;
    bool VLAIsError;
    bool IsVLA = false;

    VLADiagnoser(unsigned VLADiag, bool VLAIsError)
        : VLADiag(VLADiag), VLAIsError(VLAIsError) {}

    Sema::SemaDiagnosticBuilder diagnoseNotICEType(Sema &S, SourceLocation Loc,
                                                   QualType T) override {
      return S.Diag(Loc, diag::err_array_size_non_int) << T;
    }

    Sema::SemaDiagnosticBuilder diagnoseNotICE(Sema &S,
                                               SourceLocation Loc) override {
      IsVLA = !VLAIsError;
      return S.Diag(Loc, VLADiag);
    }

    Sema::SemaDiagnosticBuilder diagnoseFold(Sema &S,
                                             SourceLocation Loc) override {
      return S.Diag(Loc, diag::ext_vla_folded_to_constant);
    }
  } Diagnoser(VLADiag, VLAIsError);

  ExprResult R =
      S.VerifyIntegerConstantExpression(ArraySize, &SizeVal, Diagnoser);
  if (Diagnoser.IsVLA)
    return ExprResult();
  return R;
}

bool Sema::checkArrayElementAlignment(QualType EltTy, SourceLocation Loc) {
  EltTy = Context.getBaseElementType(EltTy);
  if (EltTy->isIncompleteType() || EltTy->isUndeducedType())
    return true;

  CharUnits Size = Context.getTypeSizeInChars(EltTy);
  CharUnits Alignment = Context.getTypeAlignInChars(EltTy);

  if (Size.isMultipleOf(Alignment))
    return true;

  Diag(Loc, diag::err_array_element_alignment)
      << EltTy << Size.getQuantity() << Alignment.getQuantity();
  return false;
}

QualType Sema::FormArrayType(QualType T, ArraySizeModifier ASM, Expr *ArraySize,
                             unsigned Quals, SourceRange Brackets,
                             DeclarationName Entity) {

  SourceLocation Loc = Brackets.getBegin();
  {
    // C99 6.7.5.2p1: If the element type is an incomplete or function type,
    // reject it (e.g. void ary[7], struct foo ary[7], void ary[7]())
    if (RequireCompleteSizedType(Loc, T,
                                 diag::err_array_incomplete_or_sizeless_type))
      return QualType();
  }

  if (T->isSizelessType()) {
    Diag(Loc, diag::err_array_incomplete_or_sizeless_type) << 1 << T;
    return QualType();
  }

  if (T->isFunctionType()) {
    Diag(Loc, diag::err_illegal_decl_array_of_functions)
        << getPrintableNameForEntity(Entity) << T;
    return QualType();
  }

  if (const RecordType *EltTy = T->getAs<RecordType>()) {
    // If the element type is a struct or union that contains a variadic
    // array, accept it as a GNU extension: C99 6.7.2.1p2.
    if (EltTy->getDecl()->hasFlexibleArrayMember())
      Diag(Loc, diag::ext_flexible_array_in_array) << T;
  }

  if (!checkArrayElementAlignment(T, Loc))
    return QualType();

  // Do placeholder conversions on the array size expression.
  if (ArraySize && ArraySize->hasPlaceholderType()) {
    ExprResult Result = CheckPlaceholderExpr(ArraySize);
    if (Result.isInvalid())
      return QualType();
    ArraySize = Result.get();
  }

  // Do lvalue-to-rvalue conversions on the array size expression.
  if (ArraySize && !ArraySize->isPRValue()) {
    ExprResult Result = DefaultLvalueConversion(ArraySize);
    if (Result.isInvalid())
      return QualType();

    ArraySize = Result.get();
  }

  // C99 6.7.5.2p1: array size shall have integer type (including unscoped
  // enum).
  if (ArraySize &&
      !ArraySize->getType()->isIntegralOrUnscopedEnumerationType()) {
    if (ArraySize->containsErrors()) {
      if (!getLangOpts().C99)
        Diag(Loc, diag::ext_vla);
      return QualType();
    }
    Diag(ArraySize->getBeginLoc(), diag::err_array_size_non_int)
        << ArraySize->getType() << ArraySize->getSourceRange();
    return QualType();
  }

  // VLAs always produce at least a -Wvla diagnostic, sometimes an error.
  unsigned VLADiag;
  bool VLAIsError;
  if (getLangOpts().C99) {
    VLADiag = diag::warn_vla_used;
    VLAIsError = false;
  } else {
    VLADiag = diag::ext_vla;
    VLAIsError = false;
  }

  llvm::APSInt ConstVal(Context.getTypeSize(Context.getSizeType()));
  if (!ArraySize) {
    if (ASM == ArraySizeModifier::Star) {
      Diag(Loc, VLADiag);
      if (VLAIsError)
        return QualType();

      T = Context.getVariableArrayType(T, nullptr, ASM, Quals, Brackets);
    } else {
      T = Context.getIncompleteArrayType(T, ASM, Quals);
    }
  } else {
    ExprResult R =
        checkArraySize(*this, ArraySize, ConstVal, VLADiag, VLAIsError);
    if (R.isInvalid())
      return QualType();

    if (!R.isUsable()) {
      // C99: an array with a non-ICE size is a VLA. We accept any expression
      // that we can fold to a non-zero positive value as a non-VLA as an
      // extension.
      T = Context.getVariableArrayType(T, ArraySize, ASM, Quals, Brackets);
    } else if (!T->isIncompleteType() && !T->isConstantSizeType()) {
      // C99: an array with an element type that has a non-constant-size is a
      // VLA.
      Diag(Loc, VLADiag);
      if (VLAIsError)
        return QualType();
      T = Context.getVariableArrayType(T, ArraySize, ASM, Quals, Brackets);
    } else {
      // C99 6.7.5.2p1: constant size must be positive.
      if (ConstVal.isSigned() && ConstVal.isNegative()) {
        if (Entity)
          Diag(ArraySize->getBeginLoc(), diag::err_decl_negative_array_size)
              << getPrintableNameForEntity(Entity)
              << ArraySize->getSourceRange();
        else
          Diag(ArraySize->getBeginLoc(),
               diag::err_typecheck_negative_array_size)
              << ArraySize->getSourceRange();
        return QualType();
      }
      if (ConstVal == 0) {
        // GCC accepts zero sized static arrays.
        Diag(ArraySize->getBeginLoc(), diag::ext_typecheck_zero_array_size)
            << ArraySize->getSourceRange();
      }

      // Is the array too large?
      unsigned ActiveSizeBits =
          (!T->isVariablyModifiedType() && !T->isIncompleteType() &&
           !T->isUndeducedType())
              ? ConstantArrayType::getNumAddressingBits(Context, T, ConstVal)
              : ConstVal.getActiveBits();
      if (ActiveSizeBits > ConstantArrayType::getMaxSizeBits(Context)) {
        Diag(ArraySize->getBeginLoc(), diag::err_array_too_large)
            << toString(ConstVal, 10) << ArraySize->getSourceRange();
        return QualType();
      }

      T = Context.getConstantArrayType(T, ConstVal, ArraySize, ASM, Quals);
    }
  }

  if (T->isVariableArrayType()) {
    if (!Context.getTargetInfo().isVLASupported()) {
      targetDiag(Loc, diag::err_vla_unsupported) << 0;
    } else if (sema::FunctionScopeInfo *FSI = getCurFunction()) {
      // VLAs are supported on this target; record for delayed diagnostics.
      FSI->setHasVLA(Loc);
    }
  }

  // If this is not C99, diagnose array size modifiers on non-VLAs.
  if (!getLangOpts().C99 && !T->isVariableArrayType() &&
      (ASM != ArraySizeModifier::Normal || Quals != 0)) {
    Diag(Loc, diag::ext_c99_array_usage) << llvm::to_underlying(ASM);
  }

  return T;
}

QualType Sema::FormVectorType(QualType CurType, Expr *SizeExpr,
                              SourceLocation AttrLoc) {
  // The base type must be integer (not Boolean or enumeration) or float, and
  // can't already be a vector.
  if (((!CurType->isBuiltinType() || CurType->isBooleanType() ||
        (!CurType->isIntegerType() && !CurType->isRealFloatingType())) &&
       !CurType->isBitIntType()) ||
      CurType->isArrayType()) {
    Diag(AttrLoc, diag::err_attribute_invalid_vector_type) << CurType;
    return QualType();
  }
  // Only support _BitInt elements with byte-sized power of 2 NumBits.
  if (const auto *BIT = CurType->getAs<BitIntType>()) {
    unsigned NumBits = BIT->getNumBits();
    if (!llvm::isPowerOf2_32(NumBits) || NumBits < 8) {
      Diag(AttrLoc, diag::err_attribute_invalid_bitint_vector_type)
          << (NumBits < 8);
      return QualType();
    }
  }

  std::optional<llvm::APSInt> VecSize =
      SizeExpr->getIntegerConstantExpr(Context);
  if (!VecSize) {
    Diag(AttrLoc, diag::err_attribute_argument_type)
        << "vector_size" << AANT_ArgumentIntegerConstant
        << SizeExpr->getSourceRange();
    return QualType();
  }

  // vecSize is specified in bytes - convert to bits.
  if (!VecSize->isIntN(61)) {
    // Bit size will overflow uint64.
    Diag(AttrLoc, diag::err_attribute_size_too_large)
        << SizeExpr->getSourceRange() << "vector";
    return QualType();
  }
  uint64_t VectorSizeBits = VecSize->getZExtValue() * 8;
  unsigned TypeSize = static_cast<unsigned>(Context.getTypeSize(CurType));

  if (VectorSizeBits == 0) {
    Diag(AttrLoc, diag::err_attribute_zero_size)
        << SizeExpr->getSourceRange() << "vector";
    return QualType();
  }

  if (!TypeSize || VectorSizeBits % TypeSize) {
    Diag(AttrLoc, diag::err_attribute_invalid_size)
        << SizeExpr->getSourceRange();
    return QualType();
  }

  if (VectorSizeBits / TypeSize > std::numeric_limits<uint32_t>::max()) {
    Diag(AttrLoc, diag::err_attribute_size_too_large)
        << SizeExpr->getSourceRange() << "vector";
    return QualType();
  }

  return Context.getVectorType(CurType, VectorSizeBits / TypeSize,
                               VectorKind::Generic);
}

QualType Sema::FormExtVectorType(QualType T, Expr *ArraySize,
                                 SourceLocation AttrLoc) {
  // Unlike gcc's vector_size attribute, we do not allow vectors to be defined
  // in conjunction with complex types (pointers, arrays, functions, etc.).
  //
  // We don't support selects on bitvectors and have no well-defined ABI for
  // bitvectors, so vectors of bool aren't allowed. We explicitly allow bool
  // elements in ext_vector_type for C.
  if (!T->isIntegerType() && !T->isRealFloatingType()) {
    Diag(AttrLoc, diag::err_attribute_invalid_vector_type) << T;
    return QualType();
  }

  // Only support _BitInt elements with byte-sized power of 2 NumBits.
  if (T->isBitIntType()) {
    unsigned NumBits = T->castAs<BitIntType>()->getNumBits();
    if (!llvm::isPowerOf2_32(NumBits) || NumBits < 8) {
      Diag(AttrLoc, diag::err_attribute_invalid_bitint_vector_type)
          << (NumBits < 8);
      return QualType();
    }
  }

  std::optional<llvm::APSInt> vecSize =
      ArraySize->getIntegerConstantExpr(Context);
  if (!vecSize) {
    Diag(AttrLoc, diag::err_attribute_argument_type)
        << "ext_vector_type" << AANT_ArgumentIntegerConstant
        << ArraySize->getSourceRange();
    return QualType();
  }

  if (!vecSize->isIntN(32)) {
    Diag(AttrLoc, diag::err_attribute_size_too_large)
        << ArraySize->getSourceRange() << "vector";
    return QualType();
  }
  // Unlike gcc's vector_size attribute, the size is specified as the
  // number of elements, not the number of bytes.
  unsigned vectorSize = static_cast<unsigned>(vecSize->getZExtValue());

  if (vectorSize == 0) {
    Diag(AttrLoc, diag::err_attribute_zero_size)
        << ArraySize->getSourceRange() << "vector";
    return QualType();
  }

  return Context.getExtVectorType(T, vectorSize);
}

bool Sema::CheckFunctionReturnType(QualType T, SourceLocation Loc) {
  if (T->isArrayType() || T->isFunctionType()) {
    Diag(Loc, diag::err_func_returning_array_function)
        << T->isFunctionType() << T;
    return true;
  }

  // Functions cannot return half FP.
  if (T->isHalfType() && !getLangOpts().NativeHalfArgsAndReturns &&
      !Context.getTargetInfo().allowHalfArgsAndReturns()) {
    Diag(Loc, diag::err_parameters_retval_cannot_have_fp16_type)
        << 1 << FixItHint::CreateInsertion(Loc, "*");
    return true;
  }

  return false;
}

QualType Sema::FormFunctionType(QualType T,
                                llvm::MutableArrayRef<QualType> ParamTypes,
                                SourceLocation Loc, DeclarationName Entity,
                                const FunctionProtoType::ExtProtoInfo &EPI) {
  bool Invalid = false;

  Invalid |= CheckFunctionReturnType(T, Loc);

  for (unsigned Idx = 0, Cnt = ParamTypes.size(); Idx < Cnt; ++Idx) {
    QualType ParamType = Context.getAdjustedParameterType(ParamTypes[Idx]);
    if (ParamType->isVoidType()) {
      Diag(Loc, diag::err_param_with_void_type);
      Invalid = true;
    } else if (ParamType->isHalfType() &&
               !getLangOpts().NativeHalfArgsAndReturns &&
               !Context.getTargetInfo().allowHalfArgsAndReturns()) {
      // Disallow half FP arguments.
      Diag(Loc, diag::err_parameters_retval_cannot_have_fp16_type)
          << 0 << FixItHint::CreateInsertion(Loc, "*");
      Invalid = true;
    }

    ParamTypes[Idx] = ParamType;
  }

  if (Invalid)
    return QualType();

  return Context.getFunctionType(T, ParamTypes, EPI);
}

QualType Sema::GetTypeFromParser(ParsedType Ty, TypeSourceInfo **TInfo) {
  QualType QT = Ty.get();
  if (QT.isNull()) {
    if (TInfo)
      *TInfo = nullptr;
    return QualType();
  }

  TypeSourceInfo *DI = nullptr;
  if (const LocInfoType *LIT = dyn_cast<LocInfoType>(QT)) {
    QT = LIT->getType();
    DI = LIT->getTypeSourceInfo();
  }

  if (TInfo)
    *TInfo = DI;
  return QT;
}

void Sema::diagnoseIgnoredQualifiers(unsigned DiagID, unsigned Quals,
                                     SourceLocation FallbackLoc,
                                     SourceLocation ConstQualLoc,
                                     SourceLocation VolatileQualLoc,
                                     SourceLocation RestrictQualLoc,
                                     SourceLocation AtomicQualLoc,
                                     SourceLocation UnalignedQualLoc) {
  if (!Quals)
    return;

  struct Qual {
    const char *Name;
    unsigned Mask;
    SourceLocation Loc;
  } const QualKinds[5] = {{tok::getKeywordSpelling(tok::kw_const),
                           DeclSpec::TQ_const, ConstQualLoc},
                          {tok::getKeywordSpelling(tok::kw_volatile),
                           DeclSpec::TQ_volatile, VolatileQualLoc},
                          {tok::getKeywordSpelling(tok::kw_restrict),
                           DeclSpec::TQ_restrict, RestrictQualLoc},
                          {tok::getKeywordSpelling(tok::kw___unaligned),
                           DeclSpec::TQ_unaligned, UnalignedQualLoc},
                          {tok::getKeywordSpelling(tok::kw__Atomic),
                           DeclSpec::TQ_atomic, AtomicQualLoc}};

  llvm::SmallString<32> QualStr;
  unsigned NumQuals = 0;
  SourceLocation Loc;
  FixItHint FixIts[5];

  for (auto &E : QualKinds) {
    if (Quals & E.Mask) {
      if (!QualStr.empty())
        QualStr += ' ';
      QualStr += E.Name;

      // If we have a location for the qualifier, offer a fixit.
      SourceLocation QualLoc = E.Loc;
      if (QualLoc.isValid()) {
        FixIts[NumQuals] = FixItHint::CreateRemoval(QualLoc);
        if (Loc.isInvalid() ||
            getSourceManager().isBeforeInTranslationUnit(QualLoc, Loc))
          Loc = QualLoc;
      }

      ++NumQuals;
    }
  }

  Diag(Loc.isInvalid() ? FallbackLoc : Loc, DiagID)
      << QualStr << NumQuals << FixIts[0] << FixIts[1] << FixIts[2]
      << FixIts[3];
}

// Diagnose pointless type qualifiers on the return type of a function.
void diagnoseRedundantReturnTypeQualifiers(Sema &S, QualType RetTy,
                                           Declarator &D,
                                           unsigned FunctionChunkIndex) {
  for (unsigned OuterChunkIndex = FunctionChunkIndex + 1,
                End = D.getNumTypeObjects();
       OuterChunkIndex != End; ++OuterChunkIndex) {
    DeclaratorChunk &OuterChunk = D.getTypeObject(OuterChunkIndex);
    switch (OuterChunk.Kind) {
    case DeclaratorChunk::Paren:
      continue;

    case DeclaratorChunk::Pointer: {
      DeclaratorChunk::PointerTypeInfo &PTI = OuterChunk.Ptr;
      S.diagnoseIgnoredQualifiers(diag::warn_qual_return_type, PTI.TypeQuals,
                                  SourceLocation(), PTI.ConstQualLoc,
                                  PTI.VolatileQualLoc, PTI.RestrictQualLoc,
                                  PTI.AtomicQualLoc, PTI.UnalignedQualLoc);
      return;
    }

    case DeclaratorChunk::Function:
    case DeclaratorChunk::Array:
      unsigned AtomicQual = RetTy->isAtomicType() ? DeclSpec::TQ_atomic : 0;
      S.diagnoseIgnoredQualifiers(diag::warn_qual_return_type,
                                  RetTy.getCVRQualifiers() | AtomicQual,
                                  D.getIdentifierLoc());
      return;
    }

    llvm_unreachable("unknown declarator chunk kind");
  }

  // Just parens all the way out to the decl specifiers. Diagnose any qualifiers
  // which are present there.
  const DeclSpec &RDS = D.getDeclSpec();
  S.diagnoseIgnoredQualifiers(diag::warn_qual_return_type,
                              RDS.getTypeQualifiers(), D.getIdentifierLoc(),
                              RDS.getConstSpecLoc(), RDS.getVolatileSpecLoc(),
                              RDS.getRestrictSpecLoc(), RDS.getAtomicSpecLoc(),
                              RDS.getUnalignedSpecLoc());
}

TypeSourceInfo *getTypeSourceInfoForDeclarator(TypeProcessingState &State,
                                               QualType T,
                                               TypeSourceInfo *ReturnTypeInfo);

QualType getDeclSpecTypeForDeclarator(TypeProcessingState &state,
                                      TypeSourceInfo *&ReturnTypeInfo) {
  Sema &SemaRef = state.getSema();
  Declarator &D = state.getDeclarator();
  QualType T;
  ReturnTypeInfo = nullptr;

  // The TagDecl owned by the DeclSpec.
  TagDecl *OwnedTagDecl = nullptr;

  T = convertDeclSpecToType(state);

  if (!D.isInvalidType()) {
    const DeclSpec &DS = D.getDeclSpec();
    if (DS.isTypeSpecOwned()) {
      OwnedTagDecl = cast<TagDecl>(DS.getRepAsDecl());
      OwnedTagDecl->setEmbeddedInDeclarator(true);
    }
  }

  // Declaration attributes are not merged into the type here.
  distributeTypeAttrsFromDeclarator(state, T);

  // Find the deduced type in this type (DeclSpec type only; no trailing
  // return).
  DeducedType *Deduced = T->getContainedDeducedType();

  // `auto` only in contexts where deduction is allowed.
  if (Deduced) {
    const DeclSpec &ADS = D.getDeclSpec();
    AutoType *Auto = dyn_cast<AutoType>(Deduced);
    int Error = -1;

    switch (D.getContext()) {
    case DeclaratorContext::Prototype:
      Error = 0;
      break;
    case DeclaratorContext::Member: {
      if (ADS.getStorageClassSpec() == DeclSpec::SCS_static ||
          D.isFunctionDeclarator())
        break;
      switch (cast<TagDecl>(SemaRef.CurContext)->getTagKind()) {
      case TagTypeKind::Enum:
        llvm_unreachable("unhandled tag kind");
      case TagTypeKind::Struct:
        Error = 1;
        break;
      case TagTypeKind::Union:
        Error = 2;
        break;
      }
      break;
    }
    case DeclaratorContext::TypeName:
    case DeclaratorContext::Association:
      Error = 5;
      break;
    case DeclaratorContext::File:
    case DeclaratorContext::Block:
    case DeclaratorContext::ForInit:
      break;
    case DeclaratorContext::KNRTypeList:
      Error = 6;
      break;
    }

    if (ADS.getStorageClassSpec() == DeclSpec::SCS_typedef)
      Error = 3;

    if (D.isFunctionDeclarator())
      Error = 4;

    SourceRange AutoRange = ADS.getTypeSpecTypeLoc();

    if (Error != -1) {
      unsigned Kind = 0;
      if (Auto && Auto->isGNUAutoType())
        Kind = 1;

      SemaRef.Diag(AutoRange.getBegin(), diag::err_auto_not_allowed)
          << Kind << Error << AutoRange;
      T = SemaRef.Context.IntTy;
      D.setInvalidType(true);
    }
  }

  assert(!T.isNull() && "This function should not return a null type");
  return T;
}

CallingConv getCCForDeclaratorChunk(
    Sema &S, Declarator &D, const ParsedAttributesView &AttrList,
    const DeclaratorChunk::FunctionTypeInfo &FTI, unsigned ChunkIndex) {
  assert(D.getTypeObject(ChunkIndex).Kind == DeclaratorChunk::Function);
  for (const ParsedAttr &AL : AttrList) {
    switch (AL.getKind()) {
    CALLING_CONV_ATTRS_CASELIST: {
      // Ignore attributes that don't validate or can't apply to the
      // function type.  We'll diagnose the failure to apply them in
      // handleFunctionTypeAttr.
      CallingConv CC;
      if (!S.ValidateCallingConvAttr(AL, CC) &&
          (!FTI.isVariadic || supportsVariadicCall(CC))) {
        return CC;
      }
      break;
    }

    default:
      break;
    }
  }

  CallingConv CC = S.Context.getDefaultCallingConvention(FTI.isVariadic);

  return CC;
}

namespace {
enum class SimplePointerKind {
  Pointer,
  Array,
};
} // end anonymous namespace

IdentifierInfo *Sema::getNullabilityKeyword(NullabilityKind nullability) {
  switch (nullability) {
  case NullabilityKind::NonNull:
    if (!Ident__Nonnull)
      Ident__Nonnull = PP.getIdentifierInfo("_Nonnull");
    return Ident__Nonnull;

  case NullabilityKind::Nullable:
    if (!Ident__Nullable)
      Ident__Nullable = PP.getIdentifierInfo("_Nullable");
    return Ident__Nullable;

  case NullabilityKind::Unspecified:
    if (!Ident__Null_unspecified)
      Ident__Null_unspecified = PP.getIdentifierInfo("_Null_unspecified");
    return Ident__Null_unspecified;
  }
  llvm_unreachable("Unknown nullability kind.");
}

bool hasNullabilityAttr(const ParsedAttributesView &attrs) {
  for (const ParsedAttr &AL : attrs) {
    if (AL.getKind() == ParsedAttr::AT_TypeNonNull ||
        AL.getKind() == ParsedAttr::AT_TypeNullable ||
        AL.getKind() == ParsedAttr::AT_TypeNullUnspecified)
      return true;
  }

  return false;
}

namespace {
enum class PointerDeclaratorKind {
  // Not a pointer.
  NonPointer,
  // Single-level pointer.
  SingleLevelPointer,
  // Multi-level pointer (of any pointer kind).
  MultiLevelPointer,
};

// These values must be kept in sync with diagnostics.
enum class PointerWrappingDeclaratorKind {
  None = -1,
  Array = 0,
  Reference = 1
};
} // end anonymous namespace

PointerDeclaratorKind
classifyPointerDeclarator(Sema &S, QualType type, Declarator &declarator,
                          PointerWrappingDeclaratorKind &wrappingKind) {
  unsigned numNormalPointers = 0;

  // For any dependent type, we consider it a non-pointer.
  // Look through the declarator chunks to identify pointers.
  for (unsigned i = 0, n = declarator.getNumTypeObjects(); i != n; ++i) {
    DeclaratorChunk &chunk = declarator.getTypeObject(i);
    switch (chunk.Kind) {
    case DeclaratorChunk::Array:
      if (numNormalPointers == 0)
        wrappingKind = PointerWrappingDeclaratorKind::Array;
      break;

    case DeclaratorChunk::Function:
      break;

    case DeclaratorChunk::Paren:
      break;

    case DeclaratorChunk::Pointer:
      ++numNormalPointers;
      if (numNormalPointers > 2)
        return PointerDeclaratorKind::MultiLevelPointer;
      break;
    }
  }

  // Then, dig into the type specifier itself.
  do {
    // Decompose normal pointers.
    if (auto ptrType = type->getAs<PointerType>()) {
      ++numNormalPointers;

      if (numNormalPointers > 2)
        return PointerDeclaratorKind::MultiLevelPointer;

      type = ptrType->getPointeeType();
      continue;
    }

    // If at this point we haven't seen a pointer, we won't see one.
    if (numNormalPointers == 0)
      return PointerDeclaratorKind::NonPointer;

    break;
  } while (true);

  switch (numNormalPointers) {
  case 0:
    return PointerDeclaratorKind::NonPointer;

  case 1:
    return PointerDeclaratorKind::SingleLevelPointer;

  default:
    return PointerDeclaratorKind::MultiLevelPointer;
  }
}

FileID getNullabilityCompletenessCheckFileID(Sema &S, SourceLocation loc) {
  // If we're anywhere in a function context, don't perform completeness checks.
  for (DeclContext *ctx = S.CurContext; ctx; ctx = ctx->getParent()) {
    if (ctx->isFunctionOrMethod())
      return FileID();

    if (ctx->isFileContext())
      break;
  }

  // We only care about the expansion location.
  loc = S.SourceMgr.getExpansionLoc(loc);
  FileID file = S.SourceMgr.getFileID(loc);
  if (file.isInvalid())
    return FileID();

  // Retrieve file information.
  bool invalid = false;
  const SrcMgr::SLocEntry &sloc = S.SourceMgr.getSLocEntry(file, &invalid);
  if (invalid || !sloc.isFile())
    return FileID();

  // We don't want to perform completeness checks on the main file or in
  // system headers.
  const SrcMgr::FileInfo &fileInfo = sloc.getFile();
  if (fileInfo.getIncludeLoc().isInvalid())
    return FileID();
  if (fileInfo.getFileCharacteristic() != SrcMgr::C_User &&
      S.Diags.getSuppressSystemWarnings()) {
    return FileID();
  }

  return file;
}

template <typename DiagBuilderT>
void fixItNullability(Sema &S, DiagBuilderT &Diag, SourceLocation PointerLoc,
                      NullabilityKind Nullability) {
  assert(PointerLoc.isValid());
  if (PointerLoc.isMacroID())
    return;

  SourceLocation FixItLoc = S.getLocForEndOfToken(PointerLoc);
  if (!FixItLoc.isValid() || FixItLoc == PointerLoc)
    return;

  const char *NextChar = S.SourceMgr.getCharacterData(FixItLoc);
  if (!NextChar)
    return;

  llvm::SmallString<32> InsertionTextBuf{" "};
  InsertionTextBuf += getNullabilitySpelling(Nullability);
  InsertionTextBuf += " ";
  llvm::StringRef InsertionText = InsertionTextBuf.str();

  if (isWhitespace(*NextChar)) {
    InsertionText = InsertionText.drop_back();
  } else if (NextChar[-1] == '[') {
    if (NextChar[0] == ']')
      InsertionText = InsertionText.drop_back().drop_front();
    else
      InsertionText = InsertionText.drop_front();
  } else if (!isAsciiIdentifierContinue(NextChar[0], /*allow dollar*/ true) &&
             !isAsciiIdentifierContinue(NextChar[-1], /*allow dollar*/ true)) {
    InsertionText = InsertionText.drop_back().drop_front();
  }

  Diag << FixItHint::CreateInsertion(FixItLoc, InsertionText);
}

void emitNullabilityConsistencyWarning(Sema &S, SimplePointerKind PointerKind,
                                       SourceLocation PointerLoc,
                                       SourceLocation PointerEndLoc) {
  assert(PointerLoc.isValid());

  if (PointerKind == SimplePointerKind::Array) {
    S.Diag(PointerLoc, diag::warn_nullability_missing_array);
  } else {
    S.Diag(PointerLoc, diag::warn_nullability_missing);
  }

  auto FixItLoc = PointerEndLoc.isValid() ? PointerEndLoc : PointerLoc;
  if (FixItLoc.isMacroID())
    return;

  auto addFixIt = [&](NullabilityKind Nullability) {
    auto Diag = S.Diag(FixItLoc, diag::note_nullability_fix_it);
    Diag << static_cast<unsigned>(Nullability);
    Diag << static_cast<unsigned>(PointerKind);
    fixItNullability(S, Diag, FixItLoc, Nullability);
  };
  addFixIt(NullabilityKind::Nullable);
  addFixIt(NullabilityKind::NonNull);
}

void checkNullabilityConsistency(
    Sema &S, SimplePointerKind pointerKind, SourceLocation pointerLoc,
    SourceLocation pointerEndLoc = SourceLocation()) {
  // Determine which file we're performing consistency checking for.
  FileID file = getNullabilityCompletenessCheckFileID(S, pointerLoc);
  if (file.isInvalid())
    return;

  // If we haven't seen any type nullability in this file, we won't warn now
  // about anything.
  FileNullability &fileNullability = S.NullabilityMap[file];
  if (!fileNullability.SawTypeNullability) {
    // If this is the first pointer declarator in the file, and the appropriate
    // warning is on, record it in case we need to diagnose it retroactively.
    diag::kind diagKind;
    if (pointerKind == SimplePointerKind::Array)
      diagKind = diag::warn_nullability_missing_array;
    else
      diagKind = diag::warn_nullability_missing;

    if (fileNullability.PointerLoc.isInvalid() &&
        !S.Context.getDiagnostics().isIgnored(diagKind, pointerLoc)) {
      fileNullability.PointerLoc = pointerLoc;
      fileNullability.PointerEndLoc = pointerEndLoc;
      fileNullability.PointerKind = static_cast<unsigned>(pointerKind);
    }

    return;
  }

  // Complain about missing nullability.
  emitNullabilityConsistencyWarning(S, pointerKind, pointerLoc, pointerEndLoc);
}

void recordNullabilitySeen(Sema &S, SourceLocation loc) {
  FileID file = getNullabilityCompletenessCheckFileID(S, loc);
  if (file.isInvalid())
    return;

  FileNullability &fileNullability = S.NullabilityMap[file];
  if (fileNullability.SawTypeNullability)
    return;
  fileNullability.SawTypeNullability = true;

  // If we haven't seen any type nullability before, now we have. Retroactively
  // diagnose the first unannotated pointer, if there was one.
  if (fileNullability.PointerLoc.isInvalid())
    return;

  auto kind = static_cast<SimplePointerKind>(fileNullability.PointerKind);
  emitNullabilityConsistencyWarning(S, kind, fileNullability.PointerLoc,
                                    fileNullability.PointerEndLoc);
}

bool hasOuterPointerLikeChunk(const Declarator &D, unsigned endIndex) {
  unsigned i = endIndex;
  while (i != 0) {
    // Walk outwards along the declarator chunks.
    --i;
    const DeclaratorChunk &DC = D.getTypeObject(i);
    switch (DC.Kind) {
    case DeclaratorChunk::Paren:
      break;
    case DeclaratorChunk::Array:
    case DeclaratorChunk::Pointer:
      return true;
    case DeclaratorChunk::Function:
      break;
    }
  }
  return false;
}

bool isNoDerefableChunk(const DeclaratorChunk &Chunk) {
  return (Chunk.Kind == DeclaratorChunk::Pointer ||
          Chunk.Kind == DeclaratorChunk::Array);
}

template <typename AttrT>
AttrT *createSimpleAttr(TreeContext &Ctx, ParsedAttr &AL) {
  AL.setUsedAsTypeAttr();
  return ::new (Ctx) AttrT(Ctx, AL);
}

Attr *createNullabilityAttr(TreeContext &Ctx, ParsedAttr &Attr,
                            NullabilityKind NK) {
  switch (NK) {
  case NullabilityKind::NonNull:
    return createSimpleAttr<TypeNonNullAttr>(Ctx, Attr);

  case NullabilityKind::Nullable:
    return createSimpleAttr<TypeNullableAttr>(Ctx, Attr);

  case NullabilityKind::Unspecified:
    return createSimpleAttr<TypeNullUnspecifiedAttr>(Ctx, Attr);
  }
  llvm_unreachable("unknown NullabilityKind");
}

// Diagnose whether this is a case with the multiple addr spaces.
// Returns true if this is an invalid case.
// ISO/IEC TR 18037 S5.3 (amending C99 6.7.3): "No type shall be qualified
// by qualifiers for two or more different address spaces."
bool diagnoseMultipleAddrSpaceAttributes(Sema &S, LangAS ASOld, LangAS ASNew,
                                         SourceLocation AttrLoc) {
  if (ASOld != LangAS::Default) {
    if (ASOld != ASNew) {
      S.Diag(AttrLoc, diag::err_attribute_address_multiple_qualifiers);
      return true;
    }
    S.Diag(AttrLoc, diag::warn_attribute_address_multiple_identical_qualifiers);
  }
  return false;
}

TypeSourceInfo *getFullTypeForDeclarator(TypeProcessingState &state,
                                         QualType declSpecType,
                                         TypeSourceInfo *TInfo) {
  // The TypeSourceInfo that this function returns will not be a null type.
  // If there is an error, this function will fill in a dummy type as fallback.
  QualType T = declSpecType;
  Declarator &D = state.getDeclarator();
  Sema &S = state.getSema();
  TreeContext &Context = S.Context;
  const LangOptions &LangOpts = S.getLangOpts();

  // The name we're declaring, if any.
  DeclarationName Name;
  if (D.getIdentifier())
    Name = D.getIdentifier();

  // Does this declaration declare a typedef-name?
  bool IsTypedefName =
      D.getDeclSpec().getStorageClassSpec() == DeclSpec::SCS_typedef;

  // Determine whether we should infer _Nonnull on pointer types.
  std::optional<NullabilityKind> inferNullability;
  bool inferNullabilityCS = false;
  bool inferNullabilityInnerOnly = false;
  bool inferNullabilityInnerOnlyComplete = false;

  // Are we in an assume-nonnull region?
  bool inAssumeNonNullRegion = false;
  SourceLocation assumeNonNullLoc = S.PP.getPragmaAssumeNonNullLoc();
  if (assumeNonNullLoc.isValid()) {
    inAssumeNonNullRegion = true;
    recordNullabilitySeen(S, assumeNonNullLoc);
  }

  // Whether to complain about missing nullability specifiers or not.
  enum {
    /// Never complain.
    CAMN_No,
    /// Complain on the inner pointers (but not the outermost
    /// pointer).
    CAMN_InnerPointers,
    /// Complain about any pointers that don't have nullability
    /// specified or inferred.
    CAMN_Yes
  } complainAboutMissingNullability = CAMN_No;
  unsigned NumPointersRemaining = 0;
  auto complainAboutInferringWithinChunk = PointerWrappingDeclaratorKind::None;

  if (IsTypedefName) {
    // For typedefs, we do not infer any nullability (the default),
    // and we only complain about missing nullability specifiers on
    // inner pointers.
    complainAboutMissingNullability = CAMN_InnerPointers;

    if (T->canHaveNullability(/*ResultIfUnknown*/ false) &&
        !T->getNullability()) {
      // Note that we allow but don't require nullability on dependent types.
      ++NumPointersRemaining;
    }

    for (unsigned i = 0, n = D.getNumTypeObjects(); i != n; ++i) {
      DeclaratorChunk &chunk = D.getTypeObject(i);
      switch (chunk.Kind) {
      case DeclaratorChunk::Array:
      case DeclaratorChunk::Function:
        break;

      case DeclaratorChunk::Paren:
        continue;

      case DeclaratorChunk::Pointer:
        ++NumPointersRemaining;
        continue;
      }
    }
  } else {
    switch (state.getDeclarator().getContext()) {
    case DeclaratorContext::Prototype:
    case DeclaratorContext::Member:
      [[fallthrough]];

    case DeclaratorContext::File:
    case DeclaratorContext::KNRTypeList: {
      complainAboutMissingNullability = CAMN_Yes;

      // Nullability inference depends on the type and declarator.
      auto wrappingKind = PointerWrappingDeclaratorKind::None;
      switch (classifyPointerDeclarator(S, T, D, wrappingKind)) {
      case PointerDeclaratorKind::NonPointer:
      case PointerDeclaratorKind::MultiLevelPointer:
        // Cannot infer nullability.
        break;

      case PointerDeclaratorKind::SingleLevelPointer:
        // Infer _Nonnull if we are in an assumes-nonnull region.
        if (inAssumeNonNullRegion) {
          complainAboutInferringWithinChunk = wrappingKind;
          inferNullability = NullabilityKind::NonNull;
          inferNullabilityCS = false;
        }
        break;
      }
      break;
    }

    case DeclaratorContext::Block:
    case DeclaratorContext::ForInit:
    case DeclaratorContext::TypeName:
    case DeclaratorContext::Association:
      // Don't infer in these contexts.
      break;
    }
  }

  // Local function that returns true if its argument looks like a va_list.
  auto isVaList = [&S](QualType T) -> bool {
    auto *typedefTy = T->getAs<TypedefType>();
    if (!typedefTy)
      return false;
    TypedefDecl *vaListTypedef = S.Context.getBuiltinVaListDecl();
    do {
      if (typedefTy->getDecl() == vaListTypedef)
        return true;
      if (auto *name = typedefTy->getDecl()->getIdentifier())
        if (name->isStr("va_list"))
          return true;
      typedefTy = typedefTy->desugar()->getAs<TypedefType>();
    } while (typedefTy);
    return false;
  };

  // Local function that checks the nullability for a given pointer declarator.
  // Returns true if _Nonnull was inferred.
  auto inferPointerNullability =
      [&](SimplePointerKind pointerKind, SourceLocation pointerLoc,
          SourceLocation pointerEndLoc, ParsedAttributesView &attrs,
          AttributePool &Pool) -> ParsedAttr * {
    // We've seen a pointer.
    if (NumPointersRemaining > 0)
      --NumPointersRemaining;

    // If a nullability attribute is present, there's nothing to do.
    if (hasNullabilityAttr(attrs))
      return nullptr;

    // If we're supposed to infer nullability, do so now.
    if (inferNullability && !inferNullabilityInnerOnlyComplete) {
      ParsedAttr::Form form =
          inferNullabilityCS
              ? ParsedAttr::Form::ContextSensitiveKeyword()
              : ParsedAttr::Form::Keyword(false /*IsAlignAs*/,
                                          false /*IsRegularKeywordAttribute*/);
      ParsedAttr *nullabilityAttr = Pool.create(
          S.getNullabilityKeyword(*inferNullability), SourceRange(pointerLoc),
          nullptr, SourceLocation(), nullptr, 0, form);

      attrs.addAtEnd(nullabilityAttr);

      if (pointerLoc.isValid() && complainAboutInferringWithinChunk !=
                                      PointerWrappingDeclaratorKind::None) {
        auto Diag =
            S.Diag(pointerLoc, diag::warn_nullability_inferred_on_nested_type);
        Diag << static_cast<int>(complainAboutInferringWithinChunk);
        fixItNullability(S, Diag, pointerLoc, NullabilityKind::NonNull);
      }

      if (inferNullabilityInnerOnly)
        inferNullabilityInnerOnlyComplete = true;
      return nullabilityAttr;
    }

    // If we're supposed to complain about missing nullability, do so
    // now if it's truly missing.
    switch (complainAboutMissingNullability) {
    case CAMN_No:
      break;

    case CAMN_InnerPointers:
      if (NumPointersRemaining == 0)
        break;
      [[fallthrough]];

    case CAMN_Yes:
      checkNullabilityConsistency(S, pointerKind, pointerLoc, pointerEndLoc);
    }
    return nullptr;
  };

  // If the type itself could have nullability but does not, infer pointer
  // nullability and perform consistency checking.
  if (T->canHaveNullability(/*ResultIfUnknown*/ false) &&
      !T->getNullability()) {
    if (isVaList(T)) {
      // Record that we've seen a pointer, but do nothing else.
      if (NumPointersRemaining > 0)
        --NumPointersRemaining;
    } else {
      SimplePointerKind pointerKind = SimplePointerKind::Pointer;
      const DeclSpec &FDS = D.getDeclSpec();
      if (auto *attr = inferPointerNullability(
              pointerKind, FDS.getTypeSpecTypeLoc(), FDS.getEndLoc(),
              D.getMutableDeclSpec().getAttributes(),
              D.getMutableDeclSpec().getAttributePool())) {
        T = state.getAttributedType(
            createNullabilityAttr(Context, *attr, *inferNullability), T, T);
      }
    }
  }

  if (complainAboutMissingNullability == CAMN_Yes && T->isArrayType() &&
      !T->getNullability() && !isVaList(T) && D.isPrototypeContext() &&
      !hasOuterPointerLikeChunk(D, D.getNumTypeObjects())) {
    checkNullabilityConsistency(S, SimplePointerKind::Array,
                                D.getDeclSpec().getTypeSpecTypeLoc());
  }

  bool ExpectNoDerefChunk =
      state.getCurrentAttributes().hasAttribute(ParsedAttr::AT_NoDeref);

  // Walk the DeclTypeInfo, building the recursive type as we go.
  // DeclTypeInfos are ordered from the identifier out, which is
  // opposite of what we want :).

  // Track if the produced type matches the structure of the declarator.
  // This is used later to decide if we can fill `TypeLoc` from
  // `DeclaratorChunk`s. E.g. it must be false if NeverC recovers from
  // an error by replacing the type with `int`.
  bool AreDeclaratorChunksValid = true;
  for (unsigned i = 0, e = D.getNumTypeObjects(); i != e; ++i) {
    unsigned chunkIndex = e - i - 1;
    state.setCurrentChunkIndex(chunkIndex);
    DeclaratorChunk &DeclType = D.getTypeObject(chunkIndex);
    switch (DeclType.Kind) {
    case DeclaratorChunk::Paren:
      T = S.FormParenType(T);
      break;
    case DeclaratorChunk::Pointer:
      inferPointerNullability(SimplePointerKind::Pointer, DeclType.Loc,
                              DeclType.EndLoc, DeclType.getAttrs(),
                              state.getDeclarator().getAttributePool());

      T = S.FormPointerType(T, DeclType.Loc, Name);
      if (DeclType.Ptr.TypeQuals)
        T = S.FormQualifiedType(T, DeclType.Loc, DeclType.Ptr.TypeQuals);
      break;
    case DeclaratorChunk::Array: {
      DeclaratorChunk::ArrayTypeInfo &ATI = DeclType.Arr;
      Expr *ArraySize = static_cast<Expr *>(ATI.NumElts);
      ArraySizeModifier ASM;

      if (ATI.isStar)
        ASM = ArraySizeModifier::Star;
      else if (ATI.hasStatic)
        ASM = ArraySizeModifier::Static;
      else
        ASM = ArraySizeModifier::Normal;
      if (ASM == ArraySizeModifier::Star && !D.isPrototypeContext()) {
        // This check isn't quite right: it allows star in prototypes
        // for function definitions, and disallows some edge cases detailed
        // in http://gcc.gnu.org/ml/gcc-patches/2009-02/msg00133.html
        S.Diag(DeclType.Loc, diag::err_array_star_outside_prototype);
        ASM = ArraySizeModifier::Normal;
        D.setInvalidType(true);
      }

      // C99 6.7.5.2p1: The optional type qualifiers and the keyword static
      // shall appear only in a declaration of a function parameter with an
      // array type, ...
      if (ASM == ArraySizeModifier::Static || ATI.TypeQuals) {
        if (!(D.isPrototypeContext() ||
              D.getContext() == DeclaratorContext::KNRTypeList)) {
          S.Diag(DeclType.Loc, diag::err_array_static_outside_prototype)
              << (ASM == ArraySizeModifier::Static ? "'static'"
                                                   : "type qualifier");
          // Remove the 'static' and the type qualifiers.
          if (ASM == ArraySizeModifier::Static)
            ASM = ArraySizeModifier::Normal;
          ATI.TypeQuals = 0;
          D.setInvalidType(true);
        }

        // C99 6.7.5.2p1: ... and then only in the outermost array type
        // derivation.
        if (hasOuterPointerLikeChunk(D, chunkIndex)) {
          S.Diag(DeclType.Loc, diag::err_array_static_not_outermost)
              << (ASM == ArraySizeModifier::Static ? "'static'"
                                                   : "type qualifier");
          if (ASM == ArraySizeModifier::Static)
            ASM = ArraySizeModifier::Normal;
          ATI.TypeQuals = 0;
          D.setInvalidType(true);
        }
      }

      // Array parameters can be marked nullable as well, although it's not
      // necessary if they're marked 'static'.
      if (complainAboutMissingNullability == CAMN_Yes &&
          !hasNullabilityAttr(DeclType.getAttrs()) &&
          ASM != ArraySizeModifier::Static && D.isPrototypeContext() &&
          !hasOuterPointerLikeChunk(D, chunkIndex)) {
        checkNullabilityConsistency(S, SimplePointerKind::Array, DeclType.Loc);
      }

      T = S.FormArrayType(T, ASM, ArraySize, ATI.TypeQuals,
                          SourceRange(DeclType.Loc, DeclType.EndLoc), Name);
      break;
    }
    case DeclaratorChunk::Function: {
      // If the function declarator has a prototype (i.e. it is not () and
      // does not have a K&R-style identifier list), then the arguments are part
      // of the type, otherwise the argument list is ().
      DeclaratorChunk::FunctionTypeInfo &FTI = DeclType.Fun;
      const DeclSpec &FnDS = D.getDeclSpec();

      // C99 6.7.5.3p1: The return type may not be a function or array type.
      if (!D.isInvalidType() && (T->isArrayType() || T->isFunctionType())) {
        S.Diag(DeclType.Loc, diag::err_func_returning_array_function)
            << T->isFunctionType() << T;
        T = Context.IntTy;
        D.setInvalidType(true);
        AreDeclaratorChunksValid = false;
      }

      if (T->isHalfType()) {
        if (!S.getLangOpts().NativeHalfArgsAndReturns &&
            !S.Context.getTargetInfo().allowHalfArgsAndReturns()) {
          S.Diag(D.getIdentifierLoc(),
                 diag::err_parameters_retval_cannot_have_fp16_type)
              << 1;
          D.setInvalidType(true);
        }
      }
      // Diagnose redundant cv / atomic on return types (except `void` def).
      if ((T.getCVRQualifiers() || T->isAtomicType())) {
        if (T->isVoidType() && D.getFunctionDefinitionKind() ==
                                   FunctionDefinitionKind::Definition) {
          S.Diag(DeclType.Loc, diag::err_func_returning_qualified_void) << T;
        } else
          diagnoseRedundantReturnTypeQualifiers(S, T, D, chunkIndex);
      }

      // If we see "T var();" or "T var(T());" at block scope, it is probably
      // an attempt to initialize a variable, not a function declaration.

      FunctionType::ExtInfo EI(
          getCCForDeclaratorChunk(S, D, DeclType.getAttrs(), FTI, chunkIndex));

      if (!FTI.NumParams && !FTI.isVariadic &&
          !LangOpts.requiresStrictPrototypes()) {
        // Simple void foo(), where the incoming T is the result type.
        T = Context.getFunctionNoProtoType(T, EI);
      } else {
        // We allow a zero-parameter variadic function in C if the
        // function is marked with the "overloadable" attribute. Scan
        // for this attribute now. We also allow it in C23 per WG14 N2975.
        if (!FTI.NumParams && FTI.isVariadic) {
          if (LangOpts.C23)
            S.Diag(FTI.getEllipsisLoc(),
                   diag::warn_c17_compat_ellipsis_only_parameter);
          else if (!D.getDeclarationAttributes().hasAttribute(
                       ParsedAttr::AT_Overloadable) &&
                   !D.getAttributes().hasAttribute(
                       ParsedAttr::AT_Overloadable) &&
                   !FnDS.getAttributes().hasAttribute(
                       ParsedAttr::AT_Overloadable))
            S.Diag(FTI.getEllipsisLoc(), diag::err_ellipsis_first_param);
        }

        if (FTI.NumParams && FTI.Params[0].Param == nullptr) {
          // C99 6.7.5.3p3: Reject int(x,y,z) when it's not a function
          // definition.
          S.Diag(FTI.Params[0].IdentLoc,
                 diag::err_ident_list_in_fn_declaration);
          D.setInvalidType(true);
          // Recover by creating a K&R-style function type, if possible.
          T = !LangOpts.requiresStrictPrototypes()
                  ? Context.getFunctionNoProtoType(T, EI)
                  : Context.IntTy;
          AreDeclaratorChunksValid = false;
          break;
        }

        FunctionProtoType::ExtProtoInfo EPI;
        EPI.ExtInfo = EI;
        EPI.Variadic = FTI.isVariadic;
        EPI.EllipsisLoc = FTI.getEllipsisLoc();

        // Otherwise, we have a function with a parameter list that is
        // potentially variadic.
        llvm::SmallVector<QualType, 16> ParamTys;
        ParamTys.reserve(FTI.NumParams);

        llvm::SmallVector<FunctionProtoType::ExtParameterInfo, 16>
            ExtParameterInfos(FTI.NumParams);
        bool HasAnyInterestingExtParameterInfos = false;

        for (unsigned i = 0, e = FTI.NumParams; i != e; ++i) {
          ParmVarDecl *Param = cast<ParmVarDecl>(FTI.Params[i].Param);
          QualType ParamTy = Param->getType();
          assert(!ParamTy.isNull() && "Couldn't parse type?");

          // Look for 'void'.  void is allowed only as a single parameter to a
          // function with no other parameters (C99 6.7.5.3p10).  We record
          // int(void) as a FunctionProtoType with an empty parameter list.
          if (ParamTy->isVoidType()) {
            // If this is something like 'float(int, void)', reject it.  'void'
            // is an incomplete type (C99 6.2.5p19) and function decls cannot
            // have parameters of incomplete type.
            if (FTI.NumParams != 1 || FTI.isVariadic) {
              S.Diag(FTI.Params[i].IdentLoc, diag::err_void_only_param);
              ParamTy = Context.IntTy;
              Param->setType(ParamTy);
            } else if (FTI.Params[i].Ident) {
              // Reject, but continue to parse 'int(void abc)'.
              S.Diag(FTI.Params[i].IdentLoc, diag::err_param_with_void_type);
              ParamTy = Context.IntTy;
              Param->setType(ParamTy);
            } else {
              // Reject, but continue to parse 'float(const void)'.
              if (ParamTy.hasQualifiers())
                S.Diag(DeclType.Loc, diag::err_void_param_qualified);

              // Do not add 'void' to the list.
              break;
            }
          } else if (ParamTy->isHalfType()) {
            if (!S.getLangOpts().NativeHalfArgsAndReturns &&
                !S.Context.getTargetInfo().allowHalfArgsAndReturns()) {
              S.Diag(Param->getLocation(),
                     diag::err_parameters_retval_cannot_have_fp16_type)
                  << 0;
              D.setInvalidType();
            }
          } else if (!FTI.hasPrototype) {
            if (Context.isPromotableIntegerType(ParamTy)) {
              ParamTy = Context.getPromotedIntegerType(ParamTy);
              Param->setKNRPromoted(true);
            } else if (const BuiltinType *BTy = ParamTy->getAs<BuiltinType>()) {
              if (BTy->getKind() == BuiltinType::Float) {
                ParamTy = Context.DoubleTy;
                Param->setKNRPromoted(true);
              }
            }
          }

          if (Param->hasAttr<PassObjectSizeAttr>()) {
            ExtParameterInfos[i] = ExtParameterInfos[i].withHasPassObjectSize();
            HasAnyInterestingExtParameterInfos = true;
          }

          if (Param->hasAttr<NoEscapeAttr>()) {
            ExtParameterInfos[i] = ExtParameterInfos[i].withIsNoEscape(true);
            HasAnyInterestingExtParameterInfos = true;
          }

          ParamTys.push_back(ParamTy);
        }

        if (HasAnyInterestingExtParameterInfos)
          EPI.ExtParameterInfos = ExtParameterInfos.data();

        T = Context.getFunctionType(T, ParamTys, EPI);
      }
      break;
    }
    }

    if (T.isNull()) {
      D.setInvalidType(true);
      T = Context.IntTy;
      AreDeclaratorChunksValid = false;
    }

    // See if there are any attributes on this declarator chunk.
    processTypeAttrs(state, T, TAL_DeclChunk, DeclType.getAttrs());

    if (DeclType.Kind != DeclaratorChunk::Paren) {
      if (ExpectNoDerefChunk && !isNoDerefableChunk(DeclType))
        S.Diag(DeclType.Loc, diag::warn_noderef_on_non_pointer_or_array);

      ExpectNoDerefChunk = state.didParseNoDeref();
    }
  }

  if (ExpectNoDerefChunk)
    S.Diag(state.getDeclarator().getBeginLoc(),
           diag::warn_noderef_on_non_pointer_or_array);

  // GNU warning -Wstrict-prototypes
  //   Warn if a function declaration or definition is without a prototype.
  //   This warning is issued for all kinds of unprototyped function
  //   declarations (i.e. function type typedef, function pointer etc.)
  //   C99 6.7.5.3p14:
  //   The empty list in a function declarator that is not part of a definition
  //   of that function specifies that no information about the number or types
  //   of the parameters is supplied.
  // See OnFinishFunctionBody() and MergeFunctionDecl() for handling of
  // function declarations whose behavior changes in C23.
  if (!LangOpts.requiresStrictPrototypes()) {
    for (const DeclaratorChunk &DeclType : D.type_objects()) {
      switch (DeclType.Kind) {
      case DeclaratorChunk::Function: {
        const DeclaratorChunk::FunctionTypeInfo &FTI = DeclType.Fun;
        if (!FTI.hasPrototype && FTI.NumParams == 0 && !FTI.isVariadic &&
            FTI.getLParenLoc().isValid())
          S.Diag(DeclType.Loc, diag::warn_strict_prototypes)
              << FixItHint::CreateInsertion(
                     FTI.getRParenLoc(), tok::getKeywordSpelling(tok::kw_void));
        break;
      }
      default:
        break;
      }
    }
  }

  assert(!T.isNull() && "T must not be null after this point");

  // Apply any undistributed attributes from the declaration or declarator.
  ParsedAttributesView NonSlidingAttrs;
  for (ParsedAttr &AL : D.getDeclarationAttributes()) {
    if (!AL.slidesFromDeclToDeclSpecLegacyBehavior()) {
      NonSlidingAttrs.addAtEnd(&AL);
    }
  }
  processTypeAttrs(state, T, TAL_DeclName, NonSlidingAttrs);
  processTypeAttrs(state, T, TAL_DeclName, D.getAttributes());

  // Diagnose any ignored type attributes.
  state.diagnoseIgnoredTypeAttrs(T);

  // `constexpr` object declarations are also `const`.
  if (D.getDeclSpec().getConstexprSpecifier() == ConstexprSpecKind::Constexpr &&
      T->isObjectType())
    T.addConst();

  assert(!T.isNull() && "T must not be null at the end of this function");
  if (!AreDeclaratorChunksValid)
    return Context.getTrivialTypeSourceInfo(T);
  return getTypeSourceInfoForDeclarator(state, T, TInfo);
}

TypeSourceInfo *Sema::ResolveDeclaratorType(Declarator &D, Scope *S) {
  // Determine the type of the declarator. Not all forms of declarator
  // have a type.

  TypeProcessingState state(*this, D);

  TypeSourceInfo *ReturnTypeInfo = nullptr;
  QualType T = getDeclSpecTypeForDeclarator(state, ReturnTypeInfo);

  return getFullTypeForDeclarator(state, T, ReturnTypeInfo);
}

TypeSourceInfo *Sema::ResolveDeclaratorTypeCast(Declarator &D,
                                                QualType FromTy) {
  TypeProcessingState state(*this, D);

  TypeSourceInfo *ReturnTypeInfo = nullptr;
  QualType declSpecTy = getDeclSpecTypeForDeclarator(state, ReturnTypeInfo);

  return getFullTypeForDeclarator(state, declSpecTy, ReturnTypeInfo);
}

void fillAttributedTypeLoc(AttributedTypeLoc TL, TypeProcessingState &State) {
  TL.setAttr(State.takeAttrForAttributedType(TL.getTypePtr()));
}

void fillMatrixTypeLoc(MatrixTypeLoc MTL, const ParsedAttributesView &Attrs) {
  for (const ParsedAttr &AL : Attrs) {
    if (AL.getKind() == ParsedAttr::AT_MatrixType) {
      MTL.setAttrNameLoc(AL.getLoc());
      MTL.setAttrRowOperand(AL.getArgAsExpr(0));
      MTL.setAttrColumnOperand(AL.getArgAsExpr(1));
      MTL.setAttrOperandParensRange(SourceRange());
      return;
    }
  }

  llvm_unreachable("no matrix_type attribute found at the expected location!");
}

namespace {
class TypeSpecLocFiller : public TypeLocVisitor<TypeSpecLocFiller> {
  TreeContext &Context;
  TypeProcessingState &State;
  const DeclSpec &DS;

public:
  TypeSpecLocFiller(Sema &, TreeContext &Context, TypeProcessingState &State,
                    const DeclSpec &DS)
      : Context(Context), State(State), DS(DS) {}

  void VisitAttributedTypeLoc(AttributedTypeLoc TL) {
    Visit(TL.getModifiedLoc());
    fillAttributedTypeLoc(TL, State);
  }
  void VisitBTFTagAttributedTypeLoc(BTFTagAttributedTypeLoc TL) {
    Visit(TL.getWrappedLoc());
  }
  void VisitMacroQualifiedTypeLoc(MacroQualifiedTypeLoc TL) {
    Visit(TL.getInnerLoc());
    TL.setExpansionLoc(
        State.getExpansionLocForMacroQualifiedType(TL.getTypePtr()));
  }
  void VisitQualifiedTypeLoc(QualifiedTypeLoc TL) {
    Visit(TL.getUnqualifiedLoc());
  }
  // Allow to fill pointee's type locations, e.g.,
  //   int __attr * __attr * __attr *p;
  void VisitPointerTypeLoc(PointerTypeLoc TL) { Visit(TL.getNextTypeLoc()); }
  void VisitTypedefTypeLoc(TypedefTypeLoc TL) {
    TL.setNameLoc(DS.getTypeSpecTypeLoc());
  }
  void VisitTypeOfExprTypeLoc(TypeOfExprTypeLoc TL) {
    assert(DS.getTypeSpecType() == DeclSpec::TST_typeofExpr ||
           DS.getTypeSpecType() == DeclSpec::TST_typeof_unqualExpr);
    TL.setTypeofLoc(DS.getTypeSpecTypeLoc());
    TL.setParensRange(DS.getTypeofParensRange());
  }
  void VisitTypeOfTypeLoc(TypeOfTypeLoc TL) {
    assert(DS.getTypeSpecType() == DeclSpec::TST_typeofType ||
           DS.getTypeSpecType() == DeclSpec::TST_typeof_unqualType);
    TL.setTypeofLoc(DS.getTypeSpecTypeLoc());
    TL.setParensRange(DS.getTypeofParensRange());
    assert(DS.getRepAsType());
    TypeSourceInfo *TInfo = nullptr;
    Sema::GetTypeFromParser(DS.getRepAsType(), &TInfo);
    TL.setUnmodifiedTInfo(TInfo);
  }
  void VisitBuiltinTypeLoc(BuiltinTypeLoc TL) {
    // By default, use the source location of the type specifier.
    TL.setBuiltinLoc(DS.getTypeSpecTypeLoc());
    if (TL.needsExtraLocalData()) {
      TL.getWrittenBuiltinSpecs() = DS.getWrittenBuiltinSpecs();
      // Try to have a meaningful source location.
      if (TL.getWrittenSignSpec() != TypeSpecifierSign::Unspecified)
        TL.expandBuiltinRange(DS.getTypeSpecSignLoc());
      if (TL.getWrittenWidthSpec() != TypeSpecifierWidth::Unspecified)
        TL.expandBuiltinRange(DS.getTypeSpecWidthRange());
    }
  }
  void VisitElaboratedTypeLoc(ElaboratedTypeLoc TL) {
    if (DS.getTypeSpecType() == TST_typename) {
      TypeSourceInfo *TInfo = nullptr;
      Sema::GetTypeFromParser(DS.getRepAsType(), &TInfo);
      if (TInfo)
        if (auto ETL = TInfo->getTypeLoc().getAs<ElaboratedTypeLoc>()) {
          TL.copy(ETL);
          return;
        }
    }
    const ElaboratedType *T = TL.getTypePtr();
    TL.setElaboratedKeywordLoc(T->getKeyword() != ElaboratedTypeKeyword::None
                                   ? DS.getTypeSpecTypeLoc()
                                   : SourceLocation());
    Visit(TL.getNextTypeLoc().getUnqualifiedLoc());
  }
  void VisitAutoTypeLoc(AutoTypeLoc TL) {
    assert(DS.getTypeSpecType() == TST_auto ||
           DS.getTypeSpecType() == TST_auto_type ||
           DS.getTypeSpecType() == TST_unspecified);
    TL.setNameLoc(DS.getTypeSpecTypeLoc());
  }
  void VisitTagTypeLoc(TagTypeLoc TL) {
    TL.setNameLoc(DS.getTypeSpecTypeNameLoc());
  }
  void VisitAtomicTypeLoc(AtomicTypeLoc TL) {
    // An AtomicTypeLoc can come from either an _Atomic(...) type specifier
    // or an _Atomic qualifier.
    if (DS.getTypeSpecType() == DeclSpec::TST_atomic) {
      TL.setKWLoc(DS.getTypeSpecTypeLoc());
      TL.setParensRange(DS.getTypeofParensRange());

      TypeSourceInfo *TInfo = nullptr;
      Sema::GetTypeFromParser(DS.getRepAsType(), &TInfo);
      assert(TInfo);
      TL.getValueLoc().initializeFullCopy(TInfo->getTypeLoc());
    } else {
      TL.setKWLoc(DS.getAtomicSpecLoc());
      // No parens, to indicate this was spelled as an _Atomic qualifier.
      TL.setParensRange(SourceRange());
      Visit(TL.getValueLoc());
    }
  }

  void VisitExtIntTypeLoc(BitIntTypeLoc TL) {
    TL.setNameLoc(DS.getTypeSpecTypeLoc());
  }

  void VisitTypeLoc(TypeLoc TL) {
    TL.initialize(Context, DS.getTypeSpecTypeLoc());
  }
};

class DeclaratorLocFiller : public TypeLocVisitor<DeclaratorLocFiller> {
  TypeProcessingState &State;
  const DeclaratorChunk &Chunk;

public:
  DeclaratorLocFiller(TreeContext &, TypeProcessingState &State,
                      const DeclaratorChunk &Chunk)
      : State(State), Chunk(Chunk) {}

  void VisitQualifiedTypeLoc(QualifiedTypeLoc TL) {
    llvm_unreachable("qualified type locs not expected here!");
  }
  void VisitDecayedTypeLoc(DecayedTypeLoc TL) {
    llvm_unreachable("decayed type locs not expected here!");
  }

  void VisitAttributedTypeLoc(AttributedTypeLoc TL) {
    fillAttributedTypeLoc(TL, State);
  }
  void VisitBTFTagAttributedTypeLoc(BTFTagAttributedTypeLoc TL) {
    // nothing
  }
  void VisitAdjustedTypeLoc(AdjustedTypeLoc TL) {
    // nothing
  }
  void VisitPointerTypeLoc(PointerTypeLoc TL) {
    assert(Chunk.Kind == DeclaratorChunk::Pointer);
    TL.setStarLoc(Chunk.Loc);
  }
  void VisitArrayTypeLoc(ArrayTypeLoc TL) {
    assert(Chunk.Kind == DeclaratorChunk::Array);
    TL.setLBracketLoc(Chunk.Loc);
    TL.setRBracketLoc(Chunk.EndLoc);
    TL.setSizeExpr(static_cast<Expr *>(Chunk.Arr.NumElts));
  }
  void VisitFunctionTypeLoc(FunctionTypeLoc TL) {
    assert(Chunk.Kind == DeclaratorChunk::Function);
    TL.setLocalRangeBegin(Chunk.Loc);
    TL.setLocalRangeEnd(Chunk.EndLoc);

    const DeclaratorChunk::FunctionTypeInfo &FTI = Chunk.Fun;
    TL.setLParenLoc(FTI.getLParenLoc());
    TL.setRParenLoc(FTI.getRParenLoc());
    for (unsigned i = 0, e = TL.getNumParams(), tpi = 0; i != e; ++i) {
      ParmVarDecl *Param = cast<ParmVarDecl>(FTI.Params[i].Param);
      TL.setParam(tpi++, Param);
    }
    TL.setExceptionSpecRange(SourceRange());
  }
  void VisitParenTypeLoc(ParenTypeLoc TL) {
    assert(Chunk.Kind == DeclaratorChunk::Paren);
    TL.setLParenLoc(Chunk.Loc);
    TL.setRParenLoc(Chunk.EndLoc);
  }
  void VisitBitIntTypeLoc(BitIntTypeLoc TL) { TL.setNameLoc(Chunk.Loc); }
  void VisitMacroQualifiedTypeLoc(MacroQualifiedTypeLoc TL) {
    TL.setExpansionLoc(Chunk.Loc);
  }
  void VisitVectorTypeLoc(VectorTypeLoc TL) { TL.setNameLoc(Chunk.Loc); }
  void VisitExtVectorTypeLoc(ExtVectorTypeLoc TL) { TL.setNameLoc(Chunk.Loc); }
  void VisitMatrixTypeLoc(MatrixTypeLoc TL) {
    fillMatrixTypeLoc(TL, Chunk.getAttrs());
  }

  void VisitTypeLoc(TypeLoc TL) {
    llvm_unreachable("unsupported TypeLoc kind in declarator!");
  }
};
} // end anonymous namespace

void fillAtomicQualLoc(AtomicTypeLoc ATL, const DeclaratorChunk &Chunk) {
  SourceLocation Loc;
  switch (Chunk.Kind) {
  case DeclaratorChunk::Function:
  case DeclaratorChunk::Array:
  case DeclaratorChunk::Paren:
  case DeclaratorChunk::Pointer:
    Loc = Chunk.Ptr.AtomicQualLoc;
    break;
  }

  ATL.setKWLoc(Loc);
  ATL.setParensRange(SourceRange());
}

TypeSourceInfo *getTypeSourceInfoForDeclarator(TypeProcessingState &State,
                                               QualType T,
                                               TypeSourceInfo *ReturnTypeInfo) {
  Sema &S = State.getSema();
  Declarator &D = State.getDeclarator();

  TypeSourceInfo *TInfo = S.Context.CreateTypeSourceInfo(T);
  UnqualTypeLoc CurrTL = TInfo->getTypeLoc().getUnqualifiedLoc();

  for (unsigned i = 0, e = D.getNumTypeObjects(); i != e; ++i) {
    // An AtomicTypeLoc might be produced by an atomic qualifier in this
    // declarator chunk.
    if (AtomicTypeLoc ATL = CurrTL.getAs<AtomicTypeLoc>()) {
      fillAtomicQualLoc(ATL, D.getTypeObject(i));
      CurrTL = ATL.getValueLoc().getUnqualifiedLoc();
    }

    bool HasDesugaredTypeLoc = true;
    while (HasDesugaredTypeLoc) {
      switch (CurrTL.getTypeLocClass()) {
      case TypeLoc::MacroQualified: {
        auto TL = CurrTL.castAs<MacroQualifiedTypeLoc>();
        TL.setExpansionLoc(
            State.getExpansionLocForMacroQualifiedType(TL.getTypePtr()));
        CurrTL = TL.getNextTypeLoc().getUnqualifiedLoc();
        break;
      }

      case TypeLoc::Attributed: {
        auto TL = CurrTL.castAs<AttributedTypeLoc>();
        fillAttributedTypeLoc(TL, State);
        CurrTL = TL.getNextTypeLoc().getUnqualifiedLoc();
        break;
      }

      case TypeLoc::Adjusted:
      case TypeLoc::BTFTagAttributed: {
        CurrTL = CurrTL.getNextTypeLoc().getUnqualifiedLoc();
        break;
      }

      default:
        HasDesugaredTypeLoc = false;
        break;
      }
    }

    DeclaratorLocFiller(S.Context, State, D.getTypeObject(i)).Visit(CurrTL);
    CurrTL = CurrTL.getNextTypeLoc().getUnqualifiedLoc();
  }

  // Overlay alternate return-type source info (trailing return, etc.).
  if (ReturnTypeInfo) {
    TypeLoc TL = ReturnTypeInfo->getTypeLoc();
    assert(TL.getFullDataSize() == CurrTL.getFullDataSize());
    memcpy(CurrTL.getOpaqueData(), TL.getOpaqueData(), TL.getFullDataSize());
  } else {
    TypeSpecLocFiller(S, S.Context, State, D.getDeclSpec()).Visit(CurrTL);
  }

  return TInfo;
}

// ===----------------------------------------------------------------------===
// Parsed type creation & declarator resolution
// ===----------------------------------------------------------------------===

ParsedType Sema::CreateParsedType(QualType T, TypeSourceInfo *TInfo) {
  // LocInfoTypes are "transient", only needed for passing to/from Parser
  // and Sema during declaration parsing. Try deallocating/caching them when
  // it's appropriate, instead of allocating them and keeping them around.
  LocInfoType *LocT = (LocInfoType *)BumpAlloc.Allocate(sizeof(LocInfoType),
                                                        alignof(LocInfoType));
  new (LocT) LocInfoType(T, TInfo);
  assert(LocT->getTypeClass() != T->getTypeClass() &&
         "LocInfoType's TypeClass conflicts with an existing Type class");
  return ParsedType::make(QualType(LocT, 0));
}

void LocInfoType::getAsStringInternal(std::string &Str,
                                      const PrintingPolicy &Policy) const {
  llvm_unreachable("LocInfoType leaked into the type system; an opaque TypeTy*"
                   " was used directly instead of getting the QualType through"
                   " GetTypeFromParser");
}

TypeResult Sema::OnTypeName(Scope *S, Declarator &D) {
  // C99 6.7.6: Type names have no identifier.  This is already validated by
  // the parser.
  assert(D.getIdentifier() == nullptr &&
         "Type name should have no identifier!");

  TypeSourceInfo *TInfo = ResolveDeclaratorType(D, S);
  QualType T = TInfo->getType();
  if (D.isInvalidType())
    return true;

  checkUnusedDeclAttributes(D);

  return CreateParsedType(T, TInfo);
}

bool formAddressSpaceIndex(Sema &S, LangAS &ASIdx, const Expr *AddrSpace,
                           SourceLocation AttrLoc) {
  std::optional<llvm::APSInt> OptAddrSpace =
      AddrSpace->getIntegerConstantExpr(S.Context);
  if (!OptAddrSpace) {
    S.Diag(AttrLoc, diag::err_attribute_argument_type)
        << "'address_space'" << AANT_ArgumentIntegerConstant
        << AddrSpace->getSourceRange();
    return false;
  }
  llvm::APSInt &addrSpace = *OptAddrSpace;

  if (addrSpace.isSigned()) {
    if (addrSpace.isNegative()) {
      S.Diag(AttrLoc, diag::err_attribute_address_space_negative)
          << AddrSpace->getSourceRange();
      return false;
    }
    addrSpace.setIsSigned(false);
  }

  llvm::APSInt max(addrSpace.getBitWidth());
  max = Qualifiers::MaxAddressSpace - (unsigned)LangAS::FirstTargetAddressSpace;

  if (addrSpace > max) {
    S.Diag(AttrLoc, diag::err_attribute_address_space_too_high)
        << (unsigned)max.getZExtValue() << AddrSpace->getSourceRange();
    return false;
  }

  ASIdx =
      getLangASFromTargetAS(static_cast<unsigned>(addrSpace.getZExtValue()));
  return true;
}

QualType Sema::FormAddressSpaceAttr(QualType &T, LangAS ASIdx, Expr *AddrSpace,
                                    SourceLocation AttrLoc) {
  if (diagnoseMultipleAddrSpaceAttributes(*this, T.getAddressSpace(), ASIdx,
                                          AttrLoc))
    return QualType();

  return Context.getAddrSpaceQualType(T, ASIdx);
}

QualType Sema::FormAddressSpaceAttr(QualType &T, Expr *AddrSpace,
                                    SourceLocation AttrLoc) {
  LangAS ASIdx;
  if (!formAddressSpaceIndex(*this, ASIdx, AddrSpace, AttrLoc))
    return QualType();
  return FormAddressSpaceAttr(T, ASIdx, AddrSpace, AttrLoc);
}

void applyBTFTypeTagAttribute(QualType &Type, const ParsedAttr &Attr,
                              TypeProcessingState &State) {
  Sema &S = State.getSema();
  if (Attr.getNumArgs() != 1) {
    S.Diag(Attr.getLoc(), diag::err_attribute_wrong_number_arguments)
        << Attr << 1;
    Attr.setInvalid();
    return;
  }

  // Ensure the argument is a string.
  auto *StrLiteral = dyn_cast<StringLiteral>(Attr.getArgAsExpr(0));
  if (!StrLiteral) {
    S.Diag(Attr.getLoc(), diag::err_attribute_argument_type)
        << Attr << AANT_ArgumentString;
    Attr.setInvalid();
    return;
  }

  TreeContext &Ctx = S.Context;
  llvm::StringRef BTFTypeTag = StrLiteral->getString();
  Type = State.getBTFTagAttributedType(
      ::new (Ctx) BTFTypeTagAttr(Ctx, Attr, BTFTypeTag), Type);
}

void applyAddressSpaceTypeAttribute(QualType &Type, const ParsedAttr &Attr,
                                    TypeProcessingState &State) {
  Sema &S = State.getSema();

  // ISO/IEC TR 18037 S5.3 (amending C99 6.7.3): "A function type shall not be
  // qualified by an address-space qualifier."
  if (Type->isFunctionType()) {
    S.Diag(Attr.getLoc(), diag::err_attribute_address_function_type);
    Attr.setInvalid();
    return;
  }

  if (Attr.getKind() == ParsedAttr::AT_AddressSpace) {
    if (Attr.getNumArgs() != 1) {
      S.Diag(Attr.getLoc(), diag::err_attribute_wrong_number_arguments)
          << Attr << 1;
      Attr.setInvalid();
      return;
    }

    Expr *ASArgExpr = static_cast<Expr *>(Attr.getArgAsExpr(0));
    LangAS ASIdx;
    if (!formAddressSpaceIndex(S, ASIdx, ASArgExpr, Attr.getLoc())) {
      Attr.setInvalid();
      return;
    }

    TreeContext &Ctx = S.Context;
    auto *ASAttr =
        ::new (Ctx) AddressSpaceAttr(Ctx, Attr, static_cast<unsigned>(ASIdx));

    QualType EquivType =
        S.FormAddressSpaceAttr(Type, ASIdx, ASArgExpr, Attr.getLoc());
    if (EquivType.isNull()) {
      Attr.setInvalid();
      return;
    }
    QualType T = State.getAttributedType(ASAttr, Type, EquivType);

    if (!T.isNull())
      Type = T;
    else
      Attr.setInvalid();
  }
}

namespace {
struct FunctionTypeUnwrapper {
  enum WrapKind {
    Desugar,
    Attributed,
    Parens,
    Array,
    Pointer,
    MacroQualified,
  };

  QualType Original;
  const FunctionType *Fn;
  llvm::SmallVector<unsigned char /*WrapKind*/, 8> Stack;

  FunctionTypeUnwrapper(Sema &S, QualType T) : Original(T) {
    while (true) {
      const Type *Ty = T.getTypePtr();
      if (isa<FunctionType>(Ty)) {
        Fn = cast<FunctionType>(Ty);
        return;
      } else if (isa<ParenType>(Ty)) {
        T = cast<ParenType>(Ty)->getInnerType();
        Stack.push_back(Parens);
      } else if (isa<ConstantArrayType>(Ty) || isa<VariableArrayType>(Ty) ||
                 isa<IncompleteArrayType>(Ty)) {
        T = cast<ArrayType>(Ty)->getElementType();
        Stack.push_back(Array);
      } else if (isa<PointerType>(Ty)) {
        T = cast<PointerType>(Ty)->getPointeeType();
        Stack.push_back(Pointer);
      } else if (isa<AttributedType>(Ty)) {
        T = cast<AttributedType>(Ty)->getEquivalentType();
        Stack.push_back(Attributed);
      } else if (isa<MacroQualifiedType>(Ty)) {
        T = cast<MacroQualifiedType>(Ty)->getUnderlyingType();
        Stack.push_back(MacroQualified);
      } else {
        const Type *DTy = Ty->getUnqualifiedDesugaredType();
        if (Ty == DTy) {
          Fn = nullptr;
          return;
        }

        T = QualType(DTy, 0);
        Stack.push_back(Desugar);
      }
    }
  }

  bool isFunctionType() const { return (Fn != nullptr); }
  const FunctionType *get() const { return Fn; }

  QualType wrap(Sema &S, const FunctionType *New) {
    // If T wasn't modified from the unwrapped type, do nothing.
    if (New == get())
      return Original;

    Fn = New;
    return wrap(S.Context, Original, 0);
  }

private:
  QualType wrap(TreeContext &C, QualType Old, unsigned I) {
    if (I == Stack.size())
      return C.getQualifiedType(Fn, Old.getQualifiers());

    // Build up the inner type, applying the qualifiers from the old
    // type to the new type.
    SplitQualType SplitOld = Old.split();

    // As a special case, tail-recurse if there are no qualifiers.
    if (SplitOld.Quals.empty())
      return wrap(C, SplitOld.Ty, I);
    return C.getQualifiedType(wrap(C, SplitOld.Ty, I), SplitOld.Quals);
  }

  QualType wrap(TreeContext &C, const Type *Old, unsigned I) {
    if (I == Stack.size())
      return QualType(Fn, 0);

    switch (static_cast<WrapKind>(Stack[I++])) {
    case Desugar:
      // This is the point at which we potentially lose source
      // information.
      return wrap(C, Old->getUnqualifiedDesugaredType(), I);

    case Attributed:
      return wrap(C, cast<AttributedType>(Old)->getEquivalentType(), I);

    case Parens: {
      QualType New = wrap(C, cast<ParenType>(Old)->getInnerType(), I);
      return C.getParenType(New);
    }

    case MacroQualified:
      return wrap(C, cast<MacroQualifiedType>(Old)->getUnderlyingType(), I);

    case Array: {
      if (const auto *CAT = dyn_cast<ConstantArrayType>(Old)) {
        QualType New = wrap(C, CAT->getElementType(), I);
        return C.getConstantArrayType(New, CAT->getSize(), CAT->getSizeExpr(),
                                      CAT->getSizeModifier(),
                                      CAT->getIndexTypeCVRQualifiers());
      }

      if (const auto *VAT = dyn_cast<VariableArrayType>(Old)) {
        QualType New = wrap(C, VAT->getElementType(), I);
        return C.getVariableArrayType(
            New, VAT->getSizeExpr(), VAT->getSizeModifier(),
            VAT->getIndexTypeCVRQualifiers(), VAT->getBracketsRange());
      }

      const auto *IAT = cast<IncompleteArrayType>(Old);
      QualType New = wrap(C, IAT->getElementType(), I);
      return C.getIncompleteArrayType(New, IAT->getSizeModifier(),
                                      IAT->getIndexTypeCVRQualifiers());
    }

    case Pointer: {
      QualType New = wrap(C, cast<PointerType>(Old)->getPointeeType(), I);
      return C.getPointerType(New);
    }
    }

    llvm_unreachable("unknown wrapping kind");
  }
};
} // end anonymous namespace

namespace {

bool handleMSPointerTypeQualifierAttr(TypeProcessingState &State,
                                      ParsedAttr &PAttr, QualType &Type) {
  Sema &S = State.getSema();

  Attr *A;
  switch (PAttr.getKind()) {
  default:
    llvm_unreachable("Unknown attribute kind");
  case ParsedAttr::AT_Ptr32:
    A = createSimpleAttr<Ptr32Attr>(S.Context, PAttr);
    break;
  case ParsedAttr::AT_Ptr64:
    A = createSimpleAttr<Ptr64Attr>(S.Context, PAttr);
    break;
  case ParsedAttr::AT_SPtr:
    A = createSimpleAttr<SPtrAttr>(S.Context, PAttr);
    break;
  case ParsedAttr::AT_UPtr:
    A = createSimpleAttr<UPtrAttr>(S.Context, PAttr);
    break;
  }

  std::bitset<attr::LastAttr> Attrs;
  QualType Desugared = Type;
  for (;;) {
    if (const TypedefType *TT = dyn_cast<TypedefType>(Desugared)) {
      Desugared = TT->desugar();
      continue;
    } else if (const ElaboratedType *ET = dyn_cast<ElaboratedType>(Desugared)) {
      Desugared = ET->desugar();
      continue;
    }
    const AttributedType *AT = dyn_cast<AttributedType>(Desugared);
    if (!AT)
      break;
    Attrs[AT->getAttrKind()] = true;
    Desugared = AT->getModifiedType();
  }

  // You cannot specify duplicate type attributes, so if the attribute has
  // already been applied, flag it.
  attr::Kind NewAttrKind = A->getKind();
  if (Attrs[NewAttrKind]) {
    S.Diag(PAttr.getLoc(), diag::warn_duplicate_attribute_exact) << PAttr;
    return true;
  }
  Attrs[NewAttrKind] = true;

  // You cannot have both __sptr and __uptr on the same type, nor can you
  // have __ptr32 and __ptr64.
  if (Attrs[attr::Ptr32] && Attrs[attr::Ptr64]) {
    S.Diag(PAttr.getLoc(), diag::err_attributes_are_not_compatible)
        << "'__ptr32'"
        << "'__ptr64'" << /*isRegularKeyword=*/0;
    return true;
  } else if (Attrs[attr::SPtr] && Attrs[attr::UPtr]) {
    S.Diag(PAttr.getLoc(), diag::err_attributes_are_not_compatible)
        << "'__sptr'"
        << "'__uptr'" << /*isRegularKeyword=*/0;
    return true;
  }

  // Check the raw (i.e., desugared) Canonical type to see if it
  // is a pointer type.
  if (!isa<PointerType>(Desugared)) {
    // Pointer type qualifiers can only operate on pointer types, but not
    // pointer-to-member types.
    S.Diag(PAttr.getLoc(), diag::err_attribute_pointers_only) << PAttr << 0;
    return true;
  }

  // Add address space to type based on its attributes.
  LangAS ASIdx = LangAS::Default;
  uint64_t PtrWidth =
      S.Context.getTargetInfo().getPointerWidth(LangAS::Default);
  if (PtrWidth == 32) {
    if (Attrs[attr::Ptr64])
      ASIdx = LangAS::ptr64;
    else if (Attrs[attr::UPtr])
      ASIdx = LangAS::ptr32_uptr;
  } else if (PtrWidth == 64 && Attrs[attr::Ptr32]) {
    if (Attrs[attr::UPtr])
      ASIdx = LangAS::ptr32_uptr;
    else
      ASIdx = LangAS::ptr32_sptr;
  }

  QualType Pointee = Type->getPointeeType();
  if (ASIdx != LangAS::Default)
    Pointee = S.Context.getAddrSpaceQualType(
        S.Context.removeAddrSpaceQualType(Pointee), ASIdx);
  Type = State.getAttributedType(A, Type, S.Context.getPointerType(Pointee));
  return false;
}

NullabilityKind mapNullabilityAttrKind(ParsedAttr::Kind kind) {
  switch (kind) {
  case ParsedAttr::AT_TypeNonNull:
    return NullabilityKind::NonNull;

  case ParsedAttr::AT_TypeNullable:
    return NullabilityKind::Nullable;

  case ParsedAttr::AT_TypeNullUnspecified:
    return NullabilityKind::Unspecified;

  default:
    llvm_unreachable("not a nullability attribute kind");
  }
}

bool checkNullabilityTypeSpecifier(TypeProcessingState &state, QualType &type,
                                   ParsedAttr &attr, bool allowOnArrayType) {
  Sema &S = state.getSema();

  NullabilityKind nullability = mapNullabilityAttrKind(attr.getKind());
  SourceLocation nullabilityLoc = attr.getLoc();
  bool isContextSensitive = attr.isContextSensitiveKeywordAttribute();

  recordNullabilitySeen(S, nullabilityLoc);

  QualType desugared = type;
  while (auto attributed = dyn_cast<AttributedType>(desugared.getTypePtr())) {
    if (auto existingNullability = attributed->getImmediateNullability()) {
      // Duplicated nullability.
      if (nullability == *existingNullability) {
        S.Diag(nullabilityLoc, diag::warn_nullability_duplicate)
            << DiagNullabilityKind(nullability, isContextSensitive)
            << FixItHint::CreateRemoval(nullabilityLoc);

        break;
      }

      // Conflicting nullability.
      S.Diag(nullabilityLoc, diag::err_nullability_conflicting)
          << DiagNullabilityKind(nullability, isContextSensitive)
          << DiagNullabilityKind(*existingNullability, false);
      return true;
    }

    desugared = attributed->getModifiedType();
  }

  // If there is already a different nullability specifier, complain.
  // This (unlike the code above) looks through typedefs that might
  // have nullability specifiers on them, which means we cannot
  // provide a useful Fix-It.
  if (auto existingNullability = desugared->getNullability()) {
    if (nullability != *existingNullability) {
      S.Diag(nullabilityLoc, diag::err_nullability_conflicting)
          << DiagNullabilityKind(nullability, isContextSensitive)
          << DiagNullabilityKind(*existingNullability, false);

      // Try to find the typedef with the existing nullability specifier.
      if (auto typedefType = desugared->getAs<TypedefType>()) {
        TypedefNameDecl *typedefDecl = typedefType->getDecl();
        QualType underlyingType = typedefDecl->getUnderlyingType();
        if (auto typedefNullability =
                AttributedType::stripOuterNullability(underlyingType)) {
          if (*typedefNullability == *existingNullability) {
            S.Diag(typedefDecl->getLocation(), diag::note_nullability_here)
                << DiagNullabilityKind(*existingNullability, false);
          }
        }
      }

      return true;
    }
  }

  // If this definitely isn't a pointer type, reject the specifier.
  if (!desugared->canHaveNullability() &&
      !(allowOnArrayType && desugared->isArrayType())) {
    S.Diag(nullabilityLoc, diag::err_nullability_nonpointer)
        << DiagNullabilityKind(nullability, isContextSensitive) << type;
    return true;
  }

  // For the context-sensitive keywords, require that the type be a
  // single-level pointer.
  if (isContextSensitive) {
    // Make sure that the pointee isn't itself a pointer type.
    const Type *pointeeType = nullptr;
    if (desugared->isArrayType())
      pointeeType = desugared->getArrayElementTypeNoTypeQual();
    else if (desugared->isAnyPointerType())
      pointeeType = desugared->getPointeeType().getTypePtr();

    if (pointeeType && pointeeType->isAnyPointerType()) {
      S.Diag(nullabilityLoc, diag::err_nullability_cs_multilevel)
          << DiagNullabilityKind(nullability, true) << type;
      S.Diag(nullabilityLoc, diag::note_nullability_type_specifier)
          << DiagNullabilityKind(nullability, false) << type
          << FixItHint::CreateReplacement(nullabilityLoc,
                                          getNullabilitySpelling(nullability));
      return true;
    }
  }

  // Form the attributed type.
  type = state.getAttributedType(
      createNullabilityAttr(S.Context, attr, nullability), type, type);
  return false;
}

bool distributeNullabilityTypeAttr(TypeProcessingState &state, QualType type,
                                   ParsedAttr &attr) {
  Declarator &declarator = state.getDeclarator();

  auto moveToChunk = [&](DeclaratorChunk &chunk, bool inFunction) -> bool {
    // If there is already a nullability attribute there, don't add
    // one.
    if (hasNullabilityAttr(chunk.getAttrs()))
      return false;

    // Complain about the nullability qualifier being in the wrong
    // place.
    assert(chunk.Kind == DeclaratorChunk::Pointer);
    enum {
      PK_Pointer,
      PK_FunctionPointer,
    } pointerKind = inFunction ? PK_FunctionPointer : PK_Pointer;

    auto diag =
        state.getSema().Diag(attr.getLoc(), diag::warn_nullability_declspec)
        << DiagNullabilityKind(mapNullabilityAttrKind(attr.getKind()),
                               attr.isContextSensitiveKeywordAttribute())
        << type << static_cast<unsigned>(pointerKind);

    diag << FixItHint::CreateRemoval(attr.getLoc())
         << FixItHint::CreateInsertion(
                state.getSema().getPrepEngine().getLocForEndOfToken(chunk.Loc),
                " " + attr.getAttrName()->getName().str() + " ");

    moveAttrFromListToList(attr, state.getCurrentAttributes(),
                           chunk.getAttrs());
    return true;
  };

  // Move it to the outermost pointer declarator.
  for (unsigned i = state.getCurrentChunkIndex(); i != 0; --i) {
    DeclaratorChunk &chunk = declarator.getTypeObject(i - 1);
    switch (chunk.Kind) {
    case DeclaratorChunk::Pointer:
      return moveToChunk(chunk, false);

    case DeclaratorChunk::Paren:
    case DeclaratorChunk::Array:
      continue;

    case DeclaratorChunk::Function:
      if (DeclaratorChunk *dest =
              maybeMovePastReturnType(declarator, i, false)) {
        return moveToChunk(*dest, true);
      }

      return false;
    }
  }

  return false;
}

Attr *getCCTypeAttr(TreeContext &Ctx, ParsedAttr &Attr) {
  assert(!Attr.isInvalid());
  switch (Attr.getKind()) {
  default:
    llvm_unreachable("not a calling convention attribute");
  case ParsedAttr::AT_CDecl:
    return createSimpleAttr<CDeclAttr>(Ctx, Attr);
  case ParsedAttr::AT_FastCall:
    return createSimpleAttr<FastCallAttr>(Ctx, Attr);
  case ParsedAttr::AT_StdCall:
    return createSimpleAttr<StdCallAttr>(Ctx, Attr);
  case ParsedAttr::AT_RegCall:
    return createSimpleAttr<RegCallAttr>(Ctx, Attr);
  case ParsedAttr::AT_VectorCall:
    return createSimpleAttr<VectorCallAttr>(Ctx, Attr);
  case ParsedAttr::AT_AArch64VectorPcs:
    return createSimpleAttr<AArch64VectorPcsAttr>(Ctx, Attr);
  case ParsedAttr::AT_AArch64SVEPcs:
    return createSimpleAttr<AArch64SVEPcsAttr>(Ctx, Attr);
  case ParsedAttr::AT_ArmStreaming:
    return createSimpleAttr<ArmStreamingAttr>(Ctx, Attr);
  case ParsedAttr::AT_MSABI:
    return createSimpleAttr<MSABIAttr>(Ctx, Attr);
  case ParsedAttr::AT_SysVABI:
    return createSimpleAttr<SysVABIAttr>(Ctx, Attr);
  case ParsedAttr::AT_PreserveMost:
    return createSimpleAttr<PreserveMostAttr>(Ctx, Attr);
  case ParsedAttr::AT_PreserveAll:
    return createSimpleAttr<PreserveAllAttr>(Ctx, Attr);
  }
  llvm_unreachable("unexpected attribute kind!");
}

bool checkMutualExclusion(TypeProcessingState &state,
                          const FunctionProtoType::ExtProtoInfo &EPI,
                          ParsedAttr &Attr,
                          AttributeCommonInfo::Kind OtherKind) {
  auto OtherAttr = std::find_if(
      state.getCurrentAttributes().begin(), state.getCurrentAttributes().end(),
      [OtherKind](const ParsedAttr &A) { return A.getKind() == OtherKind; });
  if (OtherAttr == state.getCurrentAttributes().end() || OtherAttr->isInvalid())
    return false;

  Sema &S = state.getSema();
  S.Diag(Attr.getLoc(), diag::err_attributes_are_not_compatible)
      << *OtherAttr << Attr
      << (OtherAttr->isRegularKeywordAttribute() ||
          Attr.isRegularKeywordAttribute());
  S.Diag(OtherAttr->getLoc(), diag::note_conflicting_attribute);
  Attr.setInvalid();
  return true;
}

bool handleFunctionTypeAttr(TypeProcessingState &state, ParsedAttr &attr,
                            QualType &type) {
  Sema &S = state.getSema();

  FunctionTypeUnwrapper unwrapped(S, type);

  if (attr.getKind() == ParsedAttr::AT_NoReturn) {
    if (S.CheckAttrNoArgs(attr))
      return true;

    // Delay if this is not a function type.
    if (!unwrapped.isFunctionType())
      return false;

    // Otherwise we can process right away.
    FunctionType::ExtInfo EI = unwrapped.get()->getExtInfo().withNoReturn(true);
    type = unwrapped.wrap(S, S.Context.adjustFunctionType(unwrapped.get(), EI));
    return true;
  }

  if (attr.getKind() == ParsedAttr::AT_AnyX86NoCallerSavedRegisters) {
    if (S.CheckAttrTarget(attr) || S.CheckAttrNoArgs(attr))
      return true;

    // Delay if this is not a function type.
    if (!unwrapped.isFunctionType())
      return false;

    FunctionType::ExtInfo EI =
        unwrapped.get()->getExtInfo().withNoCallerSavedRegs(true);
    type = unwrapped.wrap(S, S.Context.adjustFunctionType(unwrapped.get(), EI));
    return true;
  }

  if (attr.getKind() == ParsedAttr::AT_AnyX86NoCfCheck) {
    if (!S.getLangOpts().CFProtectionBranch) {
      S.Diag(attr.getLoc(), diag::warn_nocf_check_attribute_ignored);
      attr.setInvalid();
      return true;
    }

    if (S.CheckAttrTarget(attr) || S.CheckAttrNoArgs(attr))
      return true;

    // If this is not a function type, warning will be asserted by subject
    // check.
    if (!unwrapped.isFunctionType())
      return true;

    FunctionType::ExtInfo EI =
        unwrapped.get()->getExtInfo().withNoCfCheck(true);
    type = unwrapped.wrap(S, S.Context.adjustFunctionType(unwrapped.get(), EI));
    return true;
  }

  if (attr.getKind() == ParsedAttr::AT_Regparm) {
    unsigned value;
    if (S.CheckRegparmAttr(attr, value))
      return true;

    // Delay if this is not a function type.
    if (!unwrapped.isFunctionType())
      return false;

    // Diagnose regparm with fastcall.
    const FunctionType *fn = unwrapped.get();
    CallingConv CC = fn->getCallConv();
    if (CC == CC_X86FastCall) {
      S.Diag(attr.getLoc(), diag::err_attributes_are_not_compatible)
          << FunctionType::getNameForCallConv(CC) << "regparm"
          << attr.isRegularKeywordAttribute();
      attr.setInvalid();
      return true;
    }

    FunctionType::ExtInfo EI = unwrapped.get()->getExtInfo().withRegParm(value);
    type = unwrapped.wrap(S, S.Context.adjustFunctionType(unwrapped.get(), EI));
    return true;
  }

  if (attr.getKind() == ParsedAttr::AT_ArmStreaming ||
      attr.getKind() == ParsedAttr::AT_ArmStreamingCompatible ||
      attr.getKind() == ParsedAttr::AT_ArmSharedZA ||
      attr.getKind() == ParsedAttr::AT_ArmPreservesZA) {
    if (S.CheckAttrTarget(attr) || S.CheckAttrNoArgs(attr))
      return true;

    if (!unwrapped.isFunctionType())
      return false;

    const auto *FnTy = unwrapped.get()->getAs<FunctionProtoType>();
    if (!FnTy) {
      // SME ACLE attributes are not supported on K&R-style unprototyped C
      // functions.
      S.Diag(attr.getLoc(), diag::warn_attribute_wrong_decl_type)
          << attr << attr.isRegularKeywordAttribute()
          << ExpectedFunctionWithProtoType;
      attr.setInvalid();
      return false;
    }

    FunctionProtoType::ExtProtoInfo EPI = FnTy->getExtProtoInfo();
    switch (attr.getKind()) {
    case ParsedAttr::AT_ArmStreaming:
      if (checkMutualExclusion(state, EPI, attr,
                               ParsedAttr::AT_ArmStreamingCompatible))
        return true;
      EPI.setArmSMEAttribute(FunctionType::SME_PStateSMEnabledMask);
      break;
    case ParsedAttr::AT_ArmStreamingCompatible:
      if (checkMutualExclusion(state, EPI, attr, ParsedAttr::AT_ArmStreaming))
        return true;
      EPI.setArmSMEAttribute(FunctionType::SME_PStateSMCompatibleMask);
      break;
    case ParsedAttr::AT_ArmSharedZA:
      EPI.setArmSMEAttribute(FunctionType::SME_PStateZASharedMask);
      break;
    case ParsedAttr::AT_ArmPreservesZA:
      EPI.setArmSMEAttribute(FunctionType::SME_PStateZAPreservedMask);
      break;
    default:
      llvm_unreachable("Unsupported attribute");
    }

    QualType newtype = S.Context.getFunctionType(FnTy->getReturnType(),
                                                 FnTy->getParamTypes(), EPI);
    type = unwrapped.wrap(S, newtype->getAs<FunctionType>());
    return true;
  }

  if (attr.getKind() == ParsedAttr::AT_NoThrow) {
    // Delay if this is not a function type.
    if (!unwrapped.isFunctionType())
      return false;

    if (S.CheckAttrNoArgs(attr)) {
      attr.setInvalid();
      return true;
    }

    // Otherwise we can process right away.
    auto *Proto = unwrapped.get()->castAs<FunctionProtoType>();

    // MSVC ignores nothrow if it is in conflict with an explicit exception
    // specification.
    if (Proto->hasExceptionSpec())
      return true;

    type = unwrapped.wrap(
        S, S.Context
               .getFunctionTypeWithExceptionSpec(
                   QualType{Proto, 0},
                   FunctionProtoType::ExceptionSpecInfo{EST_NoThrow})
               ->getAs<FunctionType>());
    return true;
  }

  // Delay if the type didn't work out to a function.
  if (!unwrapped.isFunctionType())
    return false;

  // Otherwise, a calling convention.
  CallingConv CC;
  if (S.ValidateCallingConvAttr(attr, CC))
    return true;

  const FunctionType *fn = unwrapped.get();
  CallingConv CCOld = fn->getCallConv();
  Attr *CCAttr = getCCTypeAttr(S.Context, attr);

  if (CCOld != CC) {
    // Error out on when there's already an attribute on the type
    // and the CCs don't match.
    if (S.getCallingConvAttributedType(type)) {
      S.Diag(attr.getLoc(), diag::err_attributes_are_not_compatible)
          << FunctionType::getNameForCallConv(CC)
          << FunctionType::getNameForCallConv(CCOld)
          << attr.isRegularKeywordAttribute();
      attr.setInvalid();
      return true;
    }
  }

  // Diagnose use of variadic functions with calling conventions that
  // don't support them (e.g. because they're callee-cleanup).
  // We delay warning about this on unprototyped function declarations
  // until after redeclaration checking, just in case we pick up a
  // prototype that way.  And apparently we also "delay" warning about
  // unprototyped function types in general, despite not necessarily having
  // much ability to diagnose it later.
  if (!supportsVariadicCall(CC)) {
    const FunctionProtoType *FnP = dyn_cast<FunctionProtoType>(fn);
    if (FnP && FnP->isVariadic()) {
      // stdcall and fastcall are ignored with a warning for GCC and MS
      // compatibility.
      if (CC == CC_X86StdCall || CC == CC_X86FastCall)
        return S.Diag(attr.getLoc(), diag::warn_cconv_unsupported)
               << FunctionType::getNameForCallConv(CC)
               << (int)Sema::CallingConventionIgnoredReason::VariadicFunction;

      attr.setInvalid();
      return S.Diag(attr.getLoc(), diag::err_cconv_varargs)
             << FunctionType::getNameForCallConv(CC);
    }
  }

  // Also diagnose fastcall with regparm.
  if (CC == CC_X86FastCall && fn->getHasRegParm()) {
    S.Diag(attr.getLoc(), diag::err_attributes_are_not_compatible)
        << "regparm" << FunctionType::getNameForCallConv(CC_X86FastCall)
        << attr.isRegularKeywordAttribute();
    attr.setInvalid();
    return true;
  }

  // Modify the CC from the wrapped function type, wrap it all back, and then
  // wrap the whole thing in an AttributedType as written.  The modified type
  // might have a different CC if we ignored the attribute.
  QualType Equivalent;
  if (CCOld == CC) {
    Equivalent = type;
  } else {
    auto EI = unwrapped.get()->getExtInfo().withCallingConv(CC);
    Equivalent =
        unwrapped.wrap(S, S.Context.adjustFunctionType(unwrapped.get(), EI));
  }
  type = state.getAttributedType(CCAttr, type, Equivalent);
  return true;
}

} // namespace

// ===----------------------------------------------------------------------===
// Calling convention & type attribute dispatch
// ===----------------------------------------------------------------------===

bool Sema::hasExplicitCallingConv(QualType T) {
  const AttributedType *AT;

  // Stop if we'd be stripping off a typedef sugar node to reach the
  // AttributedType.
  while ((AT = T->getAs<AttributedType>()) &&
         AT->getAs<TypedefType>() == T->getAs<TypedefType>()) {
    if (AT->isCallingConv())
      return true;
    T = AT->getModifiedType();
  }
  return false;
}

namespace {

void applyVectorSizeAttr(QualType &CurType, const ParsedAttr &Attr, Sema &S) {
  if (Attr.getNumArgs() != 1) {
    S.Diag(Attr.getLoc(), diag::err_attribute_wrong_number_arguments)
        << Attr << 1;
    Attr.setInvalid();
    return;
  }

  Expr *SizeExpr = Attr.getArgAsExpr(0);
  QualType T = S.FormVectorType(CurType, SizeExpr, Attr.getLoc());
  if (!T.isNull())
    CurType = T;
  else
    Attr.setInvalid();
}

void applyExtVectorTypeAttr(QualType &CurType, const ParsedAttr &Attr,
                            Sema &S) {
  // check the attribute arguments.
  if (Attr.getNumArgs() != 1) {
    S.Diag(Attr.getLoc(), diag::err_attribute_wrong_number_arguments)
        << Attr << 1;
    return;
  }

  Expr *SizeExpr = Attr.getArgAsExpr(0);
  QualType T = S.FormExtVectorType(CurType, SizeExpr, Attr.getLoc());
  if (!T.isNull())
    CurType = T;
}

bool isPermittedNeonBaseType(QualType &Ty, VectorKind VecKind, Sema &S) {
  const BuiltinType *BTy = Ty->getAs<BuiltinType>();
  if (!BTy)
    return false;

  llvm::Triple Triple = S.Context.getTargetInfo().getTriple();

  if (VecKind == VectorKind::NeonPoly) {
    return BTy->getKind() == BuiltinType::UChar ||
           BTy->getKind() == BuiltinType::UShort ||
           BTy->getKind() == BuiltinType::ULong ||
           BTy->getKind() == BuiltinType::ULongLong;
  }

  // Non-polynomial vector types: the usual suspects are allowed, as well as
  // float64_t on AArch64.
  if (Triple.isArch64Bit() && BTy->getKind() == BuiltinType::Double)
    return true;

  return BTy->getKind() == BuiltinType::SChar ||
         BTy->getKind() == BuiltinType::UChar ||
         BTy->getKind() == BuiltinType::Short ||
         BTy->getKind() == BuiltinType::UShort ||
         BTy->getKind() == BuiltinType::Int ||
         BTy->getKind() == BuiltinType::UInt ||
         BTy->getKind() == BuiltinType::Long ||
         BTy->getKind() == BuiltinType::ULong ||
         BTy->getKind() == BuiltinType::LongLong ||
         BTy->getKind() == BuiltinType::ULongLong ||
         BTy->getKind() == BuiltinType::Float ||
         BTy->getKind() == BuiltinType::Half ||
         BTy->getKind() == BuiltinType::BFloat16;
}

bool verifyValidIntegerConstantExpr(Sema &S, const ParsedAttr &Attr,
                                    llvm::APSInt &Result) {
  const auto *AttrExpr = Attr.getArgAsExpr(0);
  if (std::optional<llvm::APSInt> Res =
          AttrExpr->getIntegerConstantExpr(S.Context)) {
    Result = *Res;
    return true;
  }
  S.Diag(Attr.getLoc(), diag::err_attribute_argument_type)
      << Attr << AANT_ArgumentIntegerConstant << AttrExpr->getSourceRange();
  Attr.setInvalid();
  return false;
}

void applyNeonVectorTypeAttr(QualType &CurType, const ParsedAttr &Attr, Sema &S,
                             VectorKind VecKind) {
  if (!(S.Context.getTargetInfo().hasFeature("neon") ||
        S.Context.getTargetInfo().hasFeature("sve") ||
        S.Context.getTargetInfo().hasFeature("sme")) &&
      VecKind == VectorKind::Neon) {
    S.Diag(Attr.getLoc(), diag::err_attribute_unsupported)
        << Attr << "'neon', 'sve' or 'sme'";
    Attr.setInvalid();
    return;
  }
  if (!S.Context.getTargetInfo().hasFeature("neon") &&
      VecKind == VectorKind::NeonPoly) {
    S.Diag(Attr.getLoc(), diag::err_attribute_unsupported) << Attr << "'neon'";
    Attr.setInvalid();
    return;
  }

  if (Attr.getNumArgs() != 1) {
    S.Diag(Attr.getLoc(), diag::err_attribute_wrong_number_arguments)
        << Attr << 1;
    Attr.setInvalid();
    return;
  }
  llvm::APSInt numEltsInt(32);
  if (!verifyValidIntegerConstantExpr(S, Attr, numEltsInt))
    return;

  if (!isPermittedNeonBaseType(CurType, VecKind, S)) {
    S.Diag(Attr.getLoc(), diag::err_attribute_invalid_vector_type) << CurType;
    Attr.setInvalid();
    return;
  }

  // The total size of the vector must be 64 or 128 bits.
  unsigned typeSize = static_cast<unsigned>(S.Context.getTypeSize(CurType));
  unsigned numElts = static_cast<unsigned>(numEltsInt.getZExtValue());
  unsigned vecSize = typeSize * numElts;
  if (vecSize != 64 && vecSize != 128) {
    S.Diag(Attr.getLoc(), diag::err_attribute_bad_neon_vector_size) << CurType;
    Attr.setInvalid();
    return;
  }

  CurType = S.Context.getVectorType(CurType, numElts, VecKind);
}

void applyArmSveVectorBitsTypeAttr(QualType &CurType, ParsedAttr &Attr,
                                   Sema &S) {
  // Target must have SVE.
  if (!S.Context.getTargetInfo().hasFeature("sve")) {
    S.Diag(Attr.getLoc(), diag::err_attribute_unsupported) << Attr << "'sve'";
    Attr.setInvalid();
    return;
  }

  // Attribute is unsupported if '-msve-vector-bits=<bits>' isn't specified, or
  // if <bits>+ syntax is used.
  if (!S.getLangOpts().VScaleMin ||
      S.getLangOpts().VScaleMin != S.getLangOpts().VScaleMax) {
    S.Diag(Attr.getLoc(), diag::err_attribute_arm_feature_sve_bits_unsupported)
        << Attr;
    Attr.setInvalid();
    return;
  }
  if (Attr.getNumArgs() != 1) {
    S.Diag(Attr.getLoc(), diag::err_attribute_wrong_number_arguments)
        << Attr << 1;
    Attr.setInvalid();
    return;
  }

  // The vector size must be an integer constant expression.
  llvm::APSInt SveVectorSizeInBits(32);
  if (!verifyValidIntegerConstantExpr(S, Attr, SveVectorSizeInBits))
    return;

  unsigned VecSize = static_cast<unsigned>(SveVectorSizeInBits.getZExtValue());

  // The attribute vector size must match -msve-vector-bits.
  if (VecSize != S.getLangOpts().VScaleMin * 128) {
    S.Diag(Attr.getLoc(), diag::err_attribute_bad_sve_vector_size)
        << VecSize << S.getLangOpts().VScaleMin * 128;
    Attr.setInvalid();
    return;
  }

  // Attribute can only be attached to a single SVE vector or predicate type.
  if (!CurType->isSveVLSBuiltinType()) {
    S.Diag(Attr.getLoc(), diag::err_attribute_invalid_sve_type)
        << Attr << CurType;
    Attr.setInvalid();
    return;
  }

  const auto *BT = CurType->castAs<BuiltinType>();

  QualType EltType = CurType->getSveEltType(S.Context);
  unsigned TypeSize = S.Context.getTypeSize(EltType);
  VectorKind VecKind = VectorKind::SveFixedLengthData;
  if (BT->getKind() == BuiltinType::SveBool) {
    // Predicates are represented as i8.
    VecSize /= S.Context.getCharWidth() * S.Context.getCharWidth();
    VecKind = VectorKind::SveFixedLengthPredicate;
  } else
    VecSize /= TypeSize;
  CurType = S.Context.getVectorType(EltType, VecSize, VecKind);
}

void applyAnnotateTypeAttr(TypeProcessingState &State, QualType &CurType,
                           const ParsedAttr &PA) {
  Sema &S = State.getSema();

  if (PA.getNumArgs() < 1) {
    S.Diag(PA.getLoc(), diag::err_attribute_too_few_arguments) << PA << 1;
    return;
  }

  // Make sure that there is a string literal as the annotation's first
  // argument.
  llvm::StringRef Str;
  if (!S.checkStringLiteralArgumentAttr(PA, 0, Str))
    return;

  llvm::SmallVector<Expr *, 4> Args;
  Args.reserve(PA.getNumArgs() - 1);
  for (unsigned Idx = 1; Idx < PA.getNumArgs(); Idx++) {
    assert(!PA.isArgIdent(Idx));
    Args.push_back(PA.getArgAsExpr(Idx));
  }
  if (!S.ConstantFoldAttrArgs(PA, Args))
    return;
  auto *AnnotateTypeAttr =
      AnnotateTypeAttr::Create(S.Context, Str, Args.data(), Args.size(), PA);
  CurType = State.getAttributedType(AnnotateTypeAttr, CurType, CurType);
}

void processTypeAttrs(TypeProcessingState &state, QualType &type,
                      TypeAttrLocation TAL, const ParsedAttributesView &attrs) {
  if (LLVM_LIKELY(attrs.empty())) {
    state.setParsedNoDeref(false);
    return;
  }
  state.setParsedNoDeref(false);

  // Scan through and apply attributes to this type where it makes sense.  Some
  // attributes (such as __address_space__, __vector_size__, etc) apply to the
  // type, but others can be present in the type specifiers even though they
  // apply to the decl.  Here we apply type attributes and ignore the rest.

  // This loop modifies the list pretty frequently, but we still need to make
  // sure we visit every element once. Copy the attributes list, and iterate
  // over that.
  ParsedAttributesView AttrsCopy{attrs};
  for (ParsedAttr &attr : AttrsCopy) {

    // Skip attributes that were marked to be invalid.
    if (attr.isInvalid())
      continue;

    if (attr.isStandardAttributeSyntax() || attr.isRegularKeywordAttribute()) {
      // [[gnu::...]] attributes are treated as declaration attributes, so may
      // not appertain to a DeclaratorChunk. If we handle them as type
      // attributes, accept them in that position and diagnose the GCC
      // incompatibility.
      if (attr.isGNUScope()) {
        assert(attr.isStandardAttributeSyntax());
        bool IsTypeAttr = attr.isTypeAttr();
        if (TAL == TAL_DeclChunk) {
          state.getSema().Diag(attr.getLoc(), diag::warn_gcc_ignores_type_attr)
              << attr;
          if (!IsTypeAttr)
            continue;
        }
      } else if (TAL != TAL_DeclSpec && TAL != TAL_DeclChunk &&
                 !attr.isTypeAttr()) {
        // `[[...]]` attributes: only attach to the type when they appertain to
        // the type (or are type attributes with legacy placement rules).
        continue;
      }
    }

    // If this is an attribute we can handle, do so now,
    // otherwise, add it to the FnAttrs list for rechaining.
    switch (attr.getKind()) {
    default:
      // A [[]] attribute on a declarator chunk must appertain to a type.
      if ((attr.isStandardAttributeSyntax() ||
           attr.isRegularKeywordAttribute()) &&
          TAL == TAL_DeclChunk) {
        state.getSema().Diag(attr.getLoc(), diag::err_attribute_not_type_attr)
            << attr << attr.isRegularKeywordAttribute();
        attr.setUsedAsTypeAttr();
      }
      break;

    case ParsedAttr::UnknownAttribute:
      if (attr.getAttrName()) {
        state.getSema().Diag(attr.getLoc(),
                             diag::warn_unknown_attribute_ignored)
            << attr << attr.getRange();
        attr.setInvalid();
      }
      break;

    case ParsedAttr::IgnoredAttribute:
      break;

    case ParsedAttr::AT_BTFTypeTag:
      applyBTFTypeTagAttribute(type, attr, state);
      attr.setUsedAsTypeAttr();
      break;

    case ParsedAttr::AT_MayAlias:
      // This attribute needs to actually be handled, but if we ignore
      // it it breaks large amounts of Linux software.
      attr.setUsedAsTypeAttr();
      break;
    case ParsedAttr::AT_AddressSpace:
      applyAddressSpaceTypeAttribute(type, attr, state);
      attr.setUsedAsTypeAttr();
      break;
    case ParsedAttr::AT_VectorSize:
      applyVectorSizeAttr(type, attr, state.getSema());
      attr.setUsedAsTypeAttr();
      break;
    case ParsedAttr::AT_ExtVectorType:
      applyExtVectorTypeAttr(type, attr, state.getSema());
      attr.setUsedAsTypeAttr();
      break;
    case ParsedAttr::AT_NeonVectorType:
      applyNeonVectorTypeAttr(type, attr, state.getSema(), VectorKind::Neon);
      attr.setUsedAsTypeAttr();
      break;
    case ParsedAttr::AT_NeonPolyVectorType:
      applyNeonVectorTypeAttr(type, attr, state.getSema(),
                              VectorKind::NeonPoly);
      attr.setUsedAsTypeAttr();
      break;
    case ParsedAttr::AT_ArmSveVectorBits:
      applyArmSveVectorBitsTypeAttr(type, attr, state.getSema());
      attr.setUsedAsTypeAttr();
      break;
    case ParsedAttr::AT_NoDeref: {
      // `noderef` doesn't work correctly in [[]] syntax yet.
      if (attr.isStandardAttributeSyntax()) {
        state.getSema().Diag(attr.getLoc(), diag::warn_attribute_ignored)
            << attr;
        break;
      }
      TreeContext &Ctx = state.getSema().Context;
      type = state.getAttributedType(createSimpleAttr<NoDerefAttr>(Ctx, attr),
                                     type, type);
      attr.setUsedAsTypeAttr();
      state.setParsedNoDeref(true);
      break;
    }

    case ParsedAttr::AT_MatrixType:
      state.getSema().Diag(attr.getLoc(), diag::err_builtin_matrix_disabled);
      attr.setUsedAsTypeAttr();
      break;

    MS_TYPE_ATTRS_CASELIST:
      if (!handleMSPointerTypeQualifierAttr(state, attr, type))
        attr.setUsedAsTypeAttr();
      break;

    NULLABILITY_TYPE_ATTRS_CASELIST:
      if (type->canHaveNullability() || type->isArrayType() ||
          !distributeNullabilityTypeAttr(state, type, attr)) {
        unsigned endIndex;
        if (TAL == TAL_DeclChunk)
          endIndex = state.getCurrentChunkIndex();
        else
          endIndex = state.getDeclarator().getNumTypeObjects();
        bool allowOnArrayType =
            state.getDeclarator().isPrototypeContext() &&
            !hasOuterPointerLikeChunk(state.getDeclarator(), endIndex);
        if (checkNullabilityTypeSpecifier(state, type, attr,
                                          allowOnArrayType)) {
          attr.setInvalid();
        }

        attr.setUsedAsTypeAttr();
      }
      break;

    case ParsedAttr::AT_NoThrow:
      break;
    FUNCTION_TYPE_ATTRS_CASELIST:
      attr.setUsedAsTypeAttr();

      // Attributes with standard syntax have strict rules for what they
      // appertain to and hence should not use the "distribution" logic below.
      if (attr.isStandardAttributeSyntax() ||
          attr.isRegularKeywordAttribute()) {
        if (!handleFunctionTypeAttr(state, attr, type)) {
          diagnoseBadTypeAttribute(state.getSema(), attr, type);
          attr.setInvalid();
        }
        break;
      }

      // Never process function type attributes as part of the
      // declaration-specifiers.
      if (TAL == TAL_DeclSpec)
        distributeFunctionTypeAttrFromDeclSpec(state, attr, type);

      // Otherwise, handle the possible delays.
      else if (!handleFunctionTypeAttr(state, attr, type))
        distributeFunctionTypeAttr(state, attr, type);
      break;
    case ParsedAttr::AT_AcquireHandle: {
      if (!type->isFunctionType())
        return;

      if (attr.getNumArgs() != 1) {
        state.getSema().Diag(attr.getLoc(),
                             diag::err_attribute_wrong_number_arguments)
            << attr << 1;
        attr.setInvalid();
        return;
      }

      llvm::StringRef HandleType;
      if (!state.getSema().checkStringLiteralArgumentAttr(attr, 0, HandleType))
        return;
      type = state.getAttributedType(
          AcquireHandleAttr::Create(state.getSema().Context, HandleType, attr),
          type, type);
      attr.setUsedAsTypeAttr();
      break;
    }
    case ParsedAttr::AT_AnnotateType: {
      applyAnnotateTypeAttr(state, type, attr);
      attr.setUsedAsTypeAttr();
      break;
    }
    }

    if (isa<AttributedType>(type) && attr.hasMacroIdentifier()) {
      const IdentifierInfo *MacroII = attr.getMacroIdentifier();
      type = state.getSema().Context.getMacroQualifiedType(type, MacroII);
      state.setExpansionLocForMacroQualifiedType(
          cast<MacroQualifiedType>(type.getTypePtr()),
          attr.getMacroExpansionLoc());
    }
  }
}

} // namespace

// ===----------------------------------------------------------------------===
// Complete type requirements
// ===----------------------------------------------------------------------===

bool Sema::RequireCompleteExprType(Expr *E, CompleteTypeKind Kind,
                                   TypeDiagnoser &Diagnoser) {
  return RequireCompleteType(E->getExprLoc(), E->getType(), Kind, Diagnoser);
}

bool Sema::RequireCompleteExprType(Expr *E, unsigned DiagID) {
  BoundTypeDiagnoser<> Diagnoser(DiagID);
  return RequireCompleteExprType(E, CompleteTypeKind::Default, Diagnoser);
}

bool Sema::RequireCompleteType(SourceLocation Loc, QualType T,
                               CompleteTypeKind Kind,
                               TypeDiagnoser &Diagnoser) {
  if (RequireCompleteTypeImpl(Loc, T, Kind, &Diagnoser))
    return true;
  if (const TagType *Tag = T->getAs<TagType>()) {
    if (!Tag->getDecl()->isCompleteDefinitionRequired()) {
      Tag->getDecl()->setCompleteDefinitionRequired();
      Consumer.ProcessTagDeclRequiredDefinition(Tag->getDecl());
    }
  }
  return false;
}

bool Sema::RequireCompleteTypeImpl(SourceLocation Loc, QualType T,
                                   CompleteTypeKind Kind,
                                   TypeDiagnoser *Diagnoser) {
  NamedDecl *Def = nullptr;
  bool AcceptSizeless = (Kind == CompleteTypeKind::AcceptSizeless);
  bool Incomplete = (T->isIncompleteType(&Def) ||
                     (!AcceptSizeless && T->isSizelessBuiltinType()));

  if (!Incomplete)
    return false;

  TagDecl *Tag = dyn_cast_or_null<TagDecl>(Def);

  // Give the external source a chance to provide a definition of the type.
  // This is kept separate from completing the redeclaration chain so that
  // external sources such as LLDB can avoid synthesizing a type definition
  // unless it's actually needed.
  if (Tag) {
    // Avoid diagnosing invalid decls as incomplete.
    if (Def->isInvalidDecl())
      return true;
  }

  if (!Diagnoser)
    return true;

  Diagnoser->diagnose(*this, Loc, T);

  // If the type was a forward declaration of a struct/union
  // type, produce a note.
  if (Tag && !Tag->isInvalidDecl() && !Tag->getLocation().isInvalid())
    Diag(Tag->getLocation(), Tag->isBeingDefined()
                                 ? diag::note_type_being_defined
                                 : diag::note_forward_declaration)
        << Context.getTagDeclType(Tag);

  // If we have external information that we can use to suggest a fix,
  // produce a note.
  return true;
}

bool Sema::RequireCompleteType(SourceLocation Loc, QualType T,
                               CompleteTypeKind Kind, unsigned DiagID) {
  BoundTypeDiagnoser<> Diagnoser(DiagID);
  return RequireCompleteType(Loc, T, Kind, Diagnoser);
}

bool Sema::RequireLiteralType(SourceLocation Loc, QualType T,
                              TypeDiagnoser &Diagnoser) {
  assert(!T->isDependentType() && "type should not be dependent");

  QualType ElemType = Context.getBaseElementType(T);
  if ((isCompleteType(Loc, ElemType) || ElemType->isVoidType()) &&
      T->isLiteralType(Context))
    return false;

  Diagnoser.diagnose(*this, Loc, T);
  return true;
}

bool Sema::RequireLiteralType(SourceLocation Loc, QualType T, unsigned DiagID) {
  BoundTypeDiagnoser<> Diagnoser(DiagID);
  return RequireLiteralType(Loc, T, Diagnoser);
}

QualType Sema::getElaboratedType(ElaboratedTypeKeyword Keyword, QualType T,
                                 TagDecl *OwnedTagDecl) {
  if (T.isNull())
    return T;
  return Context.getElaboratedType(Keyword, T, OwnedTagDecl);
}

QualType Sema::FormTypeofExprType(Expr *E, TypeOfKind Kind) {
  assert(!E->hasPlaceholderType() && "unexpected placeholder");

  if (E->refersToBitField())
    Diag(E->getExprLoc(), diag::err_sizeof_alignof_typeof_bitfield)
        << (Kind == TypeOfKind::Unqualified ? 3 : 2);

  QualType T = E->getType();
  if (const TagType *TT = T->getAs<TagType>())
    CheckDeclUsage(TT->getDecl(), E->getExprLoc());
  return Context.getTypeOfExprType(E, Kind);
}

QualType Sema::FormAtomicType(QualType T, SourceLocation Loc) {
  if (!isGNUAutoType(T)) {
    // It isn't entirely clear whether incomplete atomic types
    // are allowed or not; for simplicity, ban them for the moment.
    if (RequireCompleteType(Loc, T, diag::err_atomic_specifier_bad_type, 0))
      return QualType();

    int DisallowedKind = -1;
    if (T->isArrayType())
      DisallowedKind = 1;
    else if (T->isFunctionType())
      DisallowedKind = 2;
    else if (T->isAtomicType())
      DisallowedKind = 4;
    else if (T.hasQualifiers())
      DisallowedKind = 5;
    else if (T->isSizelessType())
      DisallowedKind = 6;
    else if (T->isBitIntType())
      DisallowedKind = 8;
    else if (getLangOpts().C23 && T->isUndeducedAutoType())
      // _Atomic auto is prohibited in C23
      DisallowedKind = 9;

    if (DisallowedKind != -1) {
      Diag(Loc, diag::err_atomic_specifier_bad_type) << DisallowedKind << T;
      return QualType();
    }
  }

  return Context.getAtomicType(T);
}
