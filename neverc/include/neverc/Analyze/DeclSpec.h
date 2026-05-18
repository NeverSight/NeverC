#ifndef NEVERC_SEMA_DECLSPEC_H
#define NEVERC_SEMA_DECLSPEC_H

#include "neverc/Analyze/Ownership.h"
#include "neverc/Analyze/ParsedAttr.h"
#include "neverc/Foundation/Core/Specifiers.h"
#include "neverc/Scan/Token.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"

namespace neverc {
class LangOptions;
class IdentifierInfo;
class Sema;
class Declarator;

class DeclSpec {
public:
  enum SCS {
    SCS_unspecified = 0,
    SCS_typedef,
    SCS_extern,
    SCS_static,
    SCS_auto,
    SCS_register,
    SCS_private_extern
  };

  // Import thread storage class specifier enumeration and constants.
  // These can be combined with SCS_extern and SCS_static.
  typedef ThreadStorageClassSpecifier TSCS;
  static const TSCS TSCS_unspecified = neverc::TSCS_unspecified;
  static const TSCS TSCS___thread = neverc::TSCS___thread;
  static const TSCS TSCS_thread_local = neverc::TSCS_thread_local;
  static const TSCS TSCS__Thread_local = neverc::TSCS__Thread_local;

  enum TSC { TSC_unspecified, TSC_imaginary, TSC_complex };

  // Import type specifier type enumeration and constants.
  typedef TypeSpecifierType TST;
  static const TST TST_unspecified = neverc::TST_unspecified;
  static const TST TST_void = neverc::TST_void;
  static const TST TST_char = neverc::TST_char;
  static const TST TST_wchar = neverc::TST_wchar;
  static const TST TST_char8 = neverc::TST_char8;
  static const TST TST_char16 = neverc::TST_char16;
  static const TST TST_char32 = neverc::TST_char32;
  static const TST TST_int = neverc::TST_int;
  static const TST TST_int128 = neverc::TST_int128;
  static const TST TST_bitint = neverc::TST_bitint;
  static const TST TST_half = neverc::TST_half;
  static const TST TST_BFloat16 = neverc::TST_BFloat16;
  static const TST TST_float = neverc::TST_float;
  static const TST TST_double = neverc::TST_double;
  static const TST TST_float16 = neverc::TST_Float16;
  static const TST TST_accum = neverc::TST_Accum;
  static const TST TST_fract = neverc::TST_Fract;
  static const TST TST_float128 = neverc::TST_float128;
  static const TST TST_bool = neverc::TST_bool;
  static const TST TST_decimal32 = neverc::TST_decimal32;
  static const TST TST_decimal64 = neverc::TST_decimal64;
  static const TST TST_decimal128 = neverc::TST_decimal128;
  static const TST TST_enum = neverc::TST_enum;
  static const TST TST_union = neverc::TST_union;
  static const TST TST_struct = neverc::TST_struct;
  static const TST TST_typename = neverc::TST_typename;
  static const TST TST_typeofType = neverc::TST_typeofType;
  static const TST TST_typeofExpr = neverc::TST_typeofExpr;
  static const TST TST_typeof_unqualType = neverc::TST_typeof_unqualType;
  static const TST TST_typeof_unqualExpr = neverc::TST_typeof_unqualExpr;
  static const TST TST_auto = neverc::TST_auto;
  static const TST TST_auto_type = neverc::TST_auto_type;
  static const TST TST_atomic = neverc::TST_atomic;
  // NeverC Rust-style fixed-width integer types
  static const TST TST_i8 = neverc::TST_i8;
  static const TST TST_i16 = neverc::TST_i16;
  static const TST TST_i32 = neverc::TST_i32;
  static const TST TST_i64 = neverc::TST_i64;
  static const TST TST_i128 = neverc::TST_i128;
  static const TST TST_u8 = neverc::TST_u8;
  static const TST TST_u16 = neverc::TST_u16;
  static const TST TST_u32 = neverc::TST_u32;
  static const TST TST_u64 = neverc::TST_u64;
  static const TST TST_u128 = neverc::TST_u128;
  static const TST TST_isize = neverc::TST_isize;
  static const TST TST_usize = neverc::TST_usize;
  static const TST TST_error = neverc::TST_error;

  // type-qualifiers
  enum TQ { // NOTE: These flags must be kept in sync with Qualifiers::TQ.
    TQ_unspecified = 0,
    TQ_const = 1,
    TQ_restrict = 2,
    TQ_volatile = 4,
    TQ_unaligned = 8,
    // This has no corresponding Qualifiers::TQ value, because it's not treated
    // as a qualifier in our type system.
    TQ_atomic = 16
  };

  enum ParsedSpecifiers {
    PQ_None = 0,
    PQ_StorageClassSpecifier = 1,
    PQ_TypeSpecifier = 2,
    PQ_TypeQualifier = 4,
    PQ_FunctionSpecifier = 8
  };

private:
  // storage-class-specifier
  /*SCS*/ unsigned StorageClassSpec : 3;
  /*TSCS*/ unsigned ThreadStorageClassSpec : 2;
  unsigned SCS_extern_in_linkage_spec : 1;

  // type-specifier
  /*TypeSpecifierWidth*/ unsigned TypeSpecWidth : 2;
  /*TSC*/ unsigned TypeSpecComplex : 2;
  /*TSS*/ unsigned TypeSpecSign : 2;
  /*TST*/ unsigned TypeSpecType : 7;
  unsigned TypeSpecOwned : 1;
  unsigned TypeSpecSat : 1;
  // type-qualifiers
  unsigned TypeQualifiers : 5; // Bitwise OR of TQ.

  // function-specifier
  unsigned FS_inline_specified : 1;
  unsigned FS_forceinline_specified : 1;
  unsigned FS_noreturn_specified : 1;

  // constexpr-specifier
  unsigned ConstexprSpecifier : 2;

  union {
    UnionParsedType TypeRep;
    Decl *DeclRep;
    Expr *ExprRep;
  };

  // attributes.
  ParsedAttributes Attrs;

  // SourceLocation info.  These are null if the item wasn't specified or if
  // the setting was synthesized.
  SourceRange Range;

  SourceLocation StorageClassSpecLoc, ThreadStorageClassSpecLoc;
  SourceRange TSWRange;
  SourceLocation TSCLoc, TSSLoc, TSTLoc, TSSatLoc;
  SourceLocation TSTNameLoc;
  SourceRange TypeofParensRange;
  SourceLocation TQ_constLoc, TQ_restrictLoc, TQ_volatileLoc, TQ_atomicLoc,
      TQ_unalignedLoc;
  SourceLocation FS_inlineLoc, FS_noreturnLoc;
  SourceLocation FS_forceinlineLoc;
  SourceLocation ConstexprLoc;

  WrittenBuiltinSpecs writtenBS;
  void SaveWrittenBuiltinSpecs();

  static bool isTypeRep(TST T) {
    return T == TST_atomic || T == TST_typename || T == TST_typeofType ||
           T == TST_typeof_unqualType;
  }
  static bool isExprRep(TST T) {
    return T == TST_typeofExpr || T == TST_typeof_unqualExpr || T == TST_bitint;
  }

  DeclSpec(const DeclSpec &) = delete;
  void operator=(const DeclSpec &) = delete;

public:
  static bool isDeclRep(TST T) {
    return (T == TST_enum || T == TST_struct || T == TST_union);
  }

  DeclSpec(AttributeFactory &attrFactory)
      : StorageClassSpec(SCS_unspecified),
        ThreadStorageClassSpec(TSCS_unspecified),
        SCS_extern_in_linkage_spec(false),
        TypeSpecWidth(static_cast<unsigned>(TypeSpecifierWidth::Unspecified)),
        TypeSpecComplex(TSC_unspecified),
        TypeSpecSign(static_cast<unsigned>(TypeSpecifierSign::Unspecified)),
        TypeSpecType(TST_unspecified), TypeSpecOwned(false), TypeSpecSat(false),
        TypeQualifiers(TQ_unspecified), FS_inline_specified(false),
        FS_forceinline_specified(false), FS_noreturn_specified(false),
        ConstexprSpecifier(
            static_cast<unsigned>(ConstexprSpecKind::Unspecified)),
        Attrs(attrFactory), writtenBS() {}

  // storage-class-specifier
  SCS getStorageClassSpec() const { return (SCS)StorageClassSpec; }
  TSCS getThreadStorageClassSpec() const {
    return (TSCS)ThreadStorageClassSpec;
  }
  bool isExternInLinkageSpec() const { return SCS_extern_in_linkage_spec; }
  void setExternInLinkageSpec(bool Value) {
    SCS_extern_in_linkage_spec = Value;
  }

  SourceLocation getStorageClassSpecLoc() const { return StorageClassSpecLoc; }
  SourceLocation getThreadStorageClassSpecLoc() const {
    return ThreadStorageClassSpecLoc;
  }

  void ClearStorageClassSpecs() {
    StorageClassSpec = DeclSpec::SCS_unspecified;
    ThreadStorageClassSpec = DeclSpec::TSCS_unspecified;
    SCS_extern_in_linkage_spec = false;
    StorageClassSpecLoc = SourceLocation();
    ThreadStorageClassSpecLoc = SourceLocation();
  }

  void ClearTypeSpecType() {
    TypeSpecType = DeclSpec::TST_unspecified;
    TypeSpecOwned = false;
    TSTLoc = SourceLocation();
  }

  // type-specifier
  TypeSpecifierWidth getTypeSpecWidth() const {
    return static_cast<TypeSpecifierWidth>(TypeSpecWidth);
  }
  TSC getTypeSpecComplex() const { return (TSC)TypeSpecComplex; }
  TypeSpecifierSign getTypeSpecSign() const {
    return static_cast<TypeSpecifierSign>(TypeSpecSign);
  }
  TST getTypeSpecType() const { return (TST)TypeSpecType; }
  bool isTypeSpecOwned() const { return TypeSpecOwned; }
  bool isTypeRep() const { return isTypeRep((TST)TypeSpecType); }
  bool isTypeSpecSat() const { return TypeSpecSat; }
  ParsedType getRepAsType() const {
    assert(isTypeRep((TST)TypeSpecType) && "DeclSpec does not store a type");
    return TypeRep;
  }
  Decl *getRepAsDecl() const {
    assert(isDeclRep((TST)TypeSpecType) && "DeclSpec does not store a decl");
    return DeclRep;
  }
  Expr *getRepAsExpr() const {
    assert(isExprRep((TST)TypeSpecType) && "DeclSpec does not store an expr");
    return ExprRep;
  }
  SourceRange getSourceRange() const LLVM_READONLY { return Range; }
  SourceLocation getBeginLoc() const LLVM_READONLY { return Range.getBegin(); }
  SourceLocation getEndLoc() const LLVM_READONLY { return Range.getEnd(); }

  SourceLocation getTypeSpecWidthLoc() const { return TSWRange.getBegin(); }
  SourceRange getTypeSpecWidthRange() const { return TSWRange; }
  SourceLocation getTypeSpecComplexLoc() const { return TSCLoc; }
  SourceLocation getTypeSpecSignLoc() const { return TSSLoc; }
  SourceLocation getTypeSpecTypeLoc() const { return TSTLoc; }
  SourceLocation getTypeSpecSatLoc() const { return TSSatLoc; }

  SourceLocation getTypeSpecTypeNameLoc() const {
    assert(isDeclRep((TST)TypeSpecType) || isTypeRep((TST)TypeSpecType) ||
           isExprRep((TST)TypeSpecType));
    return TSTNameLoc;
  }

  SourceRange getTypeofParensRange() const { return TypeofParensRange; }
  void setTypeArgumentRange(SourceRange range) { TypeofParensRange = range; }

  bool hasAutoTypeSpec() const {
    return (TypeSpecType == TST_auto || TypeSpecType == TST_auto_type);
  }

  bool hasTagDefinition() const;

  static const char *getSpecifierName(DeclSpec::TST T,
                                      const PrintingPolicy &Policy);
  static const char *getSpecifierName(DeclSpec::TQ Q);
  static const char *getSpecifierName(TypeSpecifierSign S);
  static const char *getSpecifierName(DeclSpec::TSC C);
  static const char *getSpecifierName(TypeSpecifierWidth W);
  static const char *getSpecifierName(DeclSpec::SCS S);
  static const char *getSpecifierName(DeclSpec::TSCS S);
  static const char *getSpecifierName(ConstexprSpecKind C);

  // type-qualifiers

  unsigned getTypeQualifiers() const { return TypeQualifiers; }
  SourceLocation getConstSpecLoc() const { return TQ_constLoc; }
  SourceLocation getRestrictSpecLoc() const { return TQ_restrictLoc; }
  SourceLocation getVolatileSpecLoc() const { return TQ_volatileLoc; }
  SourceLocation getAtomicSpecLoc() const { return TQ_atomicLoc; }
  SourceLocation getUnalignedSpecLoc() const { return TQ_unalignedLoc; }
  void ClearTypeQualifiers() {
    TypeQualifiers = 0;
    TQ_constLoc = SourceLocation();
    TQ_restrictLoc = SourceLocation();
    TQ_volatileLoc = SourceLocation();
    TQ_atomicLoc = SourceLocation();
    TQ_unalignedLoc = SourceLocation();
  }

  // function-specifier
  bool isInlineSpecified() const {
    return FS_inline_specified | FS_forceinline_specified;
  }
  SourceLocation getInlineSpecLoc() const {
    return FS_inline_specified ? FS_inlineLoc : FS_forceinlineLoc;
  }

  bool isNoreturnSpecified() const { return FS_noreturn_specified; }
  SourceLocation getNoreturnSpecLoc() const { return FS_noreturnLoc; }

  void ClearFunctionSpecs() {
    FS_inline_specified = false;
    FS_inlineLoc = SourceLocation();
    FS_forceinline_specified = false;
    FS_forceinlineLoc = SourceLocation();
    FS_noreturn_specified = false;
    FS_noreturnLoc = SourceLocation();
  }

  void forEachCVRUQualifier(
      llvm::function_ref<void(TQ, llvm::StringRef, SourceLocation)> Handle);

  void forEachQualifier(
      llvm::function_ref<void(TQ, llvm::StringRef, SourceLocation)> Handle);

  bool hasTypeSpecifier() const {
    return getTypeSpecType() != DeclSpec::TST_unspecified ||
           getTypeSpecWidth() != TypeSpecifierWidth::Unspecified ||
           getTypeSpecComplex() != DeclSpec::TSC_unspecified ||
           getTypeSpecSign() != TypeSpecifierSign::Unspecified;
  }

  unsigned getParsedSpecifiers() const;

  bool isEmpty() const { return getParsedSpecifiers() == DeclSpec::PQ_None; }

  void SetRangeStart(SourceLocation Loc) { Range.setBegin(Loc); }
  void SetRangeEnd(SourceLocation Loc) { Range.setEnd(Loc); }

  bool SetStorageClassSpec(Sema &S, SCS SC, SourceLocation Loc,
                           const char *&PrevSpec, unsigned &DiagID,
                           const PrintingPolicy &Policy);
  bool SetStorageClassSpecThread(TSCS TSC, SourceLocation Loc,
                                 const char *&PrevSpec, unsigned &DiagID);
  bool SetTypeSpecWidth(TypeSpecifierWidth W, SourceLocation Loc,
                        const char *&PrevSpec, unsigned &DiagID,
                        const PrintingPolicy &Policy);
  bool SetTypeSpecComplex(TSC C, SourceLocation Loc, const char *&PrevSpec,
                          unsigned &DiagID);
  bool SetTypeSpecSign(TypeSpecifierSign S, SourceLocation Loc,
                       const char *&PrevSpec, unsigned &DiagID);
  bool SetTypeSpecType(TST T, SourceLocation Loc, const char *&PrevSpec,
                       unsigned &DiagID, const PrintingPolicy &Policy);
  bool SetTypeSpecType(TST T, SourceLocation Loc, const char *&PrevSpec,
                       unsigned &DiagID, ParsedType Rep,
                       const PrintingPolicy &Policy);
  bool SetTypeSpecType(TST T, SourceLocation Loc, const char *&PrevSpec,
                       unsigned &DiagID, TypeResult Rep,
                       const PrintingPolicy &Policy) {
    if (Rep.isInvalid())
      return SetTypeSpecError();
    return SetTypeSpecType(T, Loc, PrevSpec, DiagID, Rep.get(), Policy);
  }
  bool SetTypeSpecType(TST T, SourceLocation Loc, const char *&PrevSpec,
                       unsigned &DiagID, Decl *Rep, bool Owned,
                       const PrintingPolicy &Policy);
  bool SetTypeSpecType(TST T, SourceLocation TagKwLoc,
                       SourceLocation TagNameLoc, const char *&PrevSpec,
                       unsigned &DiagID, ParsedType Rep,
                       const PrintingPolicy &Policy);
  bool SetTypeSpecType(TST T, SourceLocation TagKwLoc,
                       SourceLocation TagNameLoc, const char *&PrevSpec,
                       unsigned &DiagID, Decl *Rep, bool Owned,
                       const PrintingPolicy &Policy);
  bool SetTypeSpecType(TST T, SourceLocation Loc, const char *&PrevSpec,
                       unsigned &DiagID, Expr *Rep,
                       const PrintingPolicy &policy);
  bool SetBitIntType(SourceLocation KWLoc, Expr *BitWidth,
                     const char *&PrevSpec, unsigned &DiagID,
                     const PrintingPolicy &Policy);
  bool SetTypeSpecSat(SourceLocation Loc, const char *&PrevSpec,
                      unsigned &DiagID);
  bool SetTypeSpecError();
  void UpdateDeclRep(Decl *Rep) {
    assert(isDeclRep((TST)TypeSpecType));
    DeclRep = Rep;
  }
  void UpdateTypeRep(ParsedType Rep) {
    assert(isTypeRep((TST)TypeSpecType));
    TypeRep = Rep;
  }
  void UpdateExprRep(Expr *Rep) {
    assert(isExprRep((TST)TypeSpecType));
    ExprRep = Rep;
  }

  bool SetTypeQual(TQ T, SourceLocation Loc);

  bool SetTypeQual(TQ T, SourceLocation Loc, const char *&PrevSpec,
                   unsigned &DiagID, const LangOptions &Lang);

  bool setFunctionSpecInline(SourceLocation Loc, const char *&PrevSpec,
                             unsigned &DiagID);
  bool setFunctionSpecForceInline(SourceLocation Loc, const char *&PrevSpec,
                                  unsigned &DiagID);
  bool setFunctionSpecNoreturn(SourceLocation Loc, const char *&PrevSpec,
                               unsigned &DiagID);

  bool SetConstexprSpec(ConstexprSpecKind ConstexprKind, SourceLocation Loc,
                        const char *&PrevSpec, unsigned &DiagID);

  ConstexprSpecKind getConstexprSpecifier() const {
    return ConstexprSpecKind(ConstexprSpecifier);
  }

  SourceLocation getConstexprSpecLoc() const { return ConstexprLoc; }
  bool hasConstexprSpecifier() const {
    return getConstexprSpecifier() != ConstexprSpecKind::Unspecified;
  }

  void ClearConstexprSpec() {
    ConstexprSpecifier = static_cast<unsigned>(ConstexprSpecKind::Unspecified);
    ConstexprLoc = SourceLocation();
  }

  AttributePool &getAttributePool() const { return Attrs.getPool(); }

  void addAttributes(const ParsedAttributesView &AL) {
    Attrs.addAll(AL.begin(), AL.end());
  }

  bool hasAttributes() const { return !Attrs.empty(); }

  ParsedAttributes &getAttributes() { return Attrs; }
  const ParsedAttributes &getAttributes() const { return Attrs; }

  void takeAttributesFrom(ParsedAttributes &attrs) { Attrs.takeAllFrom(attrs); }

  void Finish(Sema &S, const PrintingPolicy &Policy);

  const WrittenBuiltinSpecs &getWrittenBuiltinSpecs() const {
    return writtenBS;
  }

  bool isMissingDeclaratorOk();
};

class UnqualifiedId {
private:
  UnqualifiedId(const UnqualifiedId &Other) = delete;
  const UnqualifiedId &operator=(const UnqualifiedId &) = delete;

  IdentifierInfo *Identifier;
  SourceLocation StartLocation;
  SourceLocation EndLocation;

public:
  UnqualifiedId() : Identifier(nullptr) {}

  void clear() {
    Identifier = nullptr;
    StartLocation = SourceLocation();
    EndLocation = SourceLocation();
  }

  bool isValid() const { return StartLocation.isValid(); }
  bool isInvalid() const { return !isValid(); }

  void setIdentifier(const IdentifierInfo *Id, SourceLocation IdLoc) {
    Identifier = const_cast<IdentifierInfo *>(Id);
    StartLocation = EndLocation = IdLoc;
  }

  IdentifierInfo *getIdentifierInfo() const { return Identifier; }

  void setEndLoc(SourceLocation Loc) { EndLocation = Loc; }

  SourceRange getSourceRange() const LLVM_READONLY {
    return SourceRange(StartLocation, EndLocation);
  }
  SourceLocation getBeginLoc() const LLVM_READONLY { return StartLocation; }
  SourceLocation getEndLoc() const LLVM_READONLY { return EndLocation; }
};

typedef llvm::SmallVector<Token, 4> CachedTokens;

struct DeclaratorChunk {
  DeclaratorChunk() {};

  enum { Pointer, Array, Function, Paren } Kind;

  SourceLocation Loc;
  SourceLocation EndLoc;

  SourceRange getSourceRange() const {
    if (EndLoc.isInvalid())
      return SourceRange(Loc, Loc);
    return SourceRange(Loc, EndLoc);
  }

  ParsedAttributesView AttrList;

  struct PointerTypeInfo {
    /// The type qualifiers: const/volatile/restrict/unaligned/atomic.
    unsigned TypeQuals : 5;

    /// The location of the const-qualifier, if any.
    SourceLocation ConstQualLoc;

    /// The location of the volatile-qualifier, if any.
    SourceLocation VolatileQualLoc;

    /// The location of the restrict-qualifier, if any.
    SourceLocation RestrictQualLoc;

    /// The location of the _Atomic-qualifier, if any.
    SourceLocation AtomicQualLoc;

    /// The location of the __unaligned-qualifier, if any.
    SourceLocation UnalignedQualLoc;

    void destroy() {}
  };

  struct ArrayTypeInfo {
    /// The type qualifiers for the array:
    /// const/volatile/restrict/__unaligned/_Atomic.
    unsigned TypeQuals : 5;

    /// True if this dimension included the 'static' keyword.
    unsigned hasStatic : 1;

    /// True if this dimension was [*].  In this case, NumElts is null.
    unsigned isStar : 1;

    /// This is the size of the array, or null if [] or [*] was specified.
    /// Since the parser is multi-purpose, and we don't want to impose a root
    /// expression class on all clients, NumElts is untyped.
    Expr *NumElts;

    void destroy() {}
  };

  struct ParamInfo {
    IdentifierInfo *Ident;
    SourceLocation IdentLoc;
    Decl *Param;

    /// DefaultArgTokens - When the parameter's default argument
    /// cannot be parsed immediately, it will be stored here as a
    /// sequence of tokens to be parsed later.
    /// Non-NULL indicates that there is a default argument.
    std::unique_ptr<CachedTokens> DefaultArgTokens;

    ParamInfo() = default;
    ParamInfo(IdentifierInfo *ident, SourceLocation iloc, Decl *param,
              std::unique_ptr<CachedTokens> DefArgTokens = nullptr)
        : Ident(ident), IdentLoc(iloc), Param(param),
          DefaultArgTokens(std::move(DefArgTokens)) {}
  };

  struct TypeAndRange {
    ParsedType Ty;
    SourceRange Range;
  };

  struct FunctionTypeInfo {
    /// hasPrototype - This is true if the function had at least one typed
    /// parameter.  If the function is () or (a,b,c), then it has no prototype,
    /// and is treated as a K&R-style function.
    unsigned hasPrototype : 1;

    /// isVariadic - If this function has a prototype, and if that
    /// proto ends with ',...)', this is true. When true, EllipsisLoc
    /// contains the location of the ellipsis.
    unsigned isVariadic : 1;

    /// Can this declaration be a constructor-style initializer?
    unsigned isAmbiguous : 1;

    /// DeleteParams - If this is true, we need to delete[] Params.
    unsigned DeleteParams : 1;

    /// The location of the left parenthesis in the source.
    SourceLocation LParenLoc;

    /// When isVariadic is true, the location of the ellipsis in the source.
    SourceLocation EllipsisLoc;

    /// The location of the right parenthesis in the source.
    SourceLocation RParenLoc;

    /// NumParams - This is the number of formal parameters specified by the
    /// declarator.
    unsigned NumParams;

    /// The number of declarations in the function prototype (legacy C
    /// prototype-scope rules).
    unsigned NumDeclsInPrototype;

    /// Params - This is a pointer to a new[]'d array of ParamInfo objects that
    /// describe the parameters specified by this function declarator.  null if
    /// there are no parameters specified.
    ParamInfo *Params;

    /// DeclSpec for the function with the qualifier related info.
    DeclSpec *MethodQualifiers;

    /// AttributeFactory for the MethodQualifiers.
    AttributeFactory *QualAttrFactory;

    /// Pointer to a new[]'d array of declarations that need to be available
    /// for lookup inside the function body, if one exists (legacy C
    /// prototype-scope rules).
    NamedDecl **DeclsInPrototype;

    /// Reset the parameter list to having zero parameters.
    ///
    /// This is used in various places for error recovery.
    void freeParams() {
      for (unsigned I = 0; I < NumParams; ++I)
        Params[I].DefaultArgTokens.reset();
      if (DeleteParams) {
        delete[] Params;
        DeleteParams = false;
      }
      NumParams = 0;
    }

    void destroy() {
      freeParams();
      delete QualAttrFactory;
      delete MethodQualifiers;
      if (NumDeclsInPrototype != 0)
        delete[] DeclsInPrototype;
    }

    DeclSpec &getOrCreateMethodQualifiers() {
      if (!MethodQualifiers) {
        QualAttrFactory = new AttributeFactory();
        MethodQualifiers = new DeclSpec(*QualAttrFactory);
      }
      return *MethodQualifiers;
    }

    /// isKNRPrototype - Return true if this is a K&R style identifier list,
    /// like "void foo(a,b,c)".  In a function definition, this will be followed
    /// by the parameter type definitions.
    bool isKNRPrototype() const { return !hasPrototype && NumParams != 0; }

    SourceLocation getLParenLoc() const { return LParenLoc; }

    SourceLocation getEllipsisLoc() const { return EllipsisLoc; }

    SourceLocation getRParenLoc() const { return RParenLoc; }

    /// Determine whether this method has qualifiers.
    bool hasMethodTypeQualifiers() const {
      return MethodQualifiers && (MethodQualifiers->getTypeQualifiers() ||
                                  MethodQualifiers->getAttributes().size());
    }

    /// Get the non-parameter decls defined within this function
    /// prototype. Typically these are tag declarations.
    llvm::ArrayRef<NamedDecl *> getDeclsInPrototype() const {
      return llvm::ArrayRef(DeclsInPrototype, NumDeclsInPrototype);
    }
  };

  union {
    PointerTypeInfo Ptr;
    ArrayTypeInfo Arr;
    FunctionTypeInfo Fun;
  };

  void destroy() {
    switch (Kind) {
    case DeclaratorChunk::Function:
      return Fun.destroy();
    case DeclaratorChunk::Pointer:
      return Ptr.destroy();
    case DeclaratorChunk::Array:
      return Arr.destroy();
    case DeclaratorChunk::Paren:
      return;
    }
  }

  const ParsedAttributesView &getAttrs() const { return AttrList; }
  ParsedAttributesView &getAttrs() { return AttrList; }

  static DeclaratorChunk getPointer(unsigned TypeQuals, SourceLocation Loc,
                                    SourceLocation ConstQualLoc,
                                    SourceLocation VolatileQualLoc,
                                    SourceLocation RestrictQualLoc,
                                    SourceLocation AtomicQualLoc,
                                    SourceLocation UnalignedQualLoc) {
    DeclaratorChunk I;
    I.Kind = Pointer;
    I.Loc = Loc;
    new (&I.Ptr) PointerTypeInfo;
    I.Ptr.TypeQuals = TypeQuals;
    I.Ptr.ConstQualLoc = ConstQualLoc;
    I.Ptr.VolatileQualLoc = VolatileQualLoc;
    I.Ptr.RestrictQualLoc = RestrictQualLoc;
    I.Ptr.AtomicQualLoc = AtomicQualLoc;
    I.Ptr.UnalignedQualLoc = UnalignedQualLoc;
    return I;
  }

  static DeclaratorChunk getArray(unsigned TypeQuals, bool isStatic,
                                  bool isStar, Expr *NumElts,
                                  SourceLocation LBLoc, SourceLocation RBLoc) {
    DeclaratorChunk I;
    I.Kind = Array;
    I.Loc = LBLoc;
    I.EndLoc = RBLoc;
    I.Arr.TypeQuals = TypeQuals;
    I.Arr.hasStatic = isStatic;
    I.Arr.isStar = isStar;
    I.Arr.NumElts = NumElts;
    return I;
  }

  static DeclaratorChunk
  getFunction(bool HasProto, bool IsAmbiguous, SourceLocation LParenLoc,
              ParamInfo *Params, unsigned NumParams, SourceLocation EllipsisLoc,
              SourceLocation RParenLoc,
              llvm::ArrayRef<NamedDecl *> DeclsInPrototype,
              SourceLocation LocalRangeBegin, SourceLocation LocalRangeEnd,
              Declarator &TheDeclarator, DeclSpec *MethodQualifiers = nullptr);

  static DeclaratorChunk getParen(SourceLocation LParenLoc,
                                  SourceLocation RParenLoc) {
    DeclaratorChunk I;
    I.Kind = Paren;
    I.Loc = LParenLoc;
    I.EndLoc = RParenLoc;
    return I;
  }

  bool isParen() const { return Kind == Paren; }
};

enum class FunctionDefinitionKind {
  Declaration,
  Definition,
};

enum class DeclaratorContext {
  File,        // File scope declaration.
  Prototype,   // Within a function prototype.
  KNRTypeList, // K&R type definition list for formals.
  TypeName,    // Abstract declarator for types.
  Member,      // Struct/Union field.
  Block,       // Declaration within a block in a function.
  ForInit,     // Declaration within first part of a for loop.
  Association  // C11 _Generic selection expression association.
};

class Declarator {

private:
  const DeclSpec &DS;
  UnqualifiedId Name;
  SourceRange Range;

  DeclaratorContext Context;

  llvm::SmallVector<DeclaratorChunk, 8> DeclTypeInfo;

  unsigned InvalidType : 1;

  unsigned GroupingParens : 1;

  unsigned FunctionDefinition : 1;

  unsigned Redeclaration : 1;

  unsigned InlineStorageUsed : 1;

  unsigned HasInitializer : 1;

  ParsedAttributes Attrs;

  const ParsedAttributesView &DeclarationAttrs;

  Expr *AsmLabel;

  DeclaratorChunk::ParamInfo InlineParams[16];

  friend struct DeclaratorChunk;

public:
  Declarator(const DeclSpec &DS, const ParsedAttributesView &DeclarationAttrs,
             DeclaratorContext C)
      : DS(DS), Range(DS.getSourceRange()), Context(C),
        InvalidType(DS.getTypeSpecType() == DeclSpec::TST_error),
        GroupingParens(false), FunctionDefinition(static_cast<unsigned>(
                                   FunctionDefinitionKind::Declaration)),
        Redeclaration(false), InlineStorageUsed(false), HasInitializer(false),
        Attrs(DS.getAttributePool().getFactory()),
        DeclarationAttrs(DeclarationAttrs), AsmLabel(nullptr) {
    assert(llvm::all_of(DeclarationAttrs,
                        [](const ParsedAttr &AL) {
                          return (AL.isStandardAttributeSyntax() ||
                                  AL.isRegularKeywordAttribute());
                        }) &&
           "DeclarationAttrs may only contain [[]] and keyword attributes");
  }

  ~Declarator() { clear(); }
  const DeclSpec &getDeclSpec() const { return DS; }

  DeclSpec &getMutableDeclSpec() { return const_cast<DeclSpec &>(DS); }

  AttributePool &getAttributePool() const { return Attrs.getPool(); }

  UnqualifiedId &getName() { return Name; }

  DeclaratorContext getContext() const { return Context; }

  bool isPrototypeContext() const {
    return Context == DeclaratorContext::Prototype;
  }

  SourceRange getSourceRange() const LLVM_READONLY { return Range; }
  SourceLocation getBeginLoc() const LLVM_READONLY { return Range.getBegin(); }
  SourceLocation getEndLoc() const LLVM_READONLY { return Range.getEnd(); }

  void SetSourceRange(SourceRange R) { Range = R; }
  void SetRangeBegin(SourceLocation Loc) {
    if (!Loc.isInvalid())
      Range.setBegin(Loc);
  }
  void SetRangeEnd(SourceLocation Loc) {
    if (!Loc.isInvalid())
      Range.setEnd(Loc);
  }
  void ExtendWithDeclSpec(const DeclSpec &DS) {
    SourceRange SR = DS.getSourceRange();
    if (Range.getBegin().isInvalid())
      Range.setBegin(SR.getBegin());
    if (!SR.getEnd().isInvalid())
      Range.setEnd(SR.getEnd());
  }

  void clear() {
    Name.clear();
    Range = DS.getSourceRange();

    for (unsigned i = 0, e = DeclTypeInfo.size(); i != e; ++i)
      DeclTypeInfo[i].destroy();
    DeclTypeInfo.clear();
    Attrs.clear();
    AsmLabel = nullptr;
    InlineStorageUsed = false;
    HasInitializer = false;
  }

  bool mayOmitIdentifier() const {
    switch (Context) {
    case DeclaratorContext::File:
    case DeclaratorContext::KNRTypeList:
    case DeclaratorContext::Member:
    case DeclaratorContext::Block:
    case DeclaratorContext::ForInit:
      return false;
    default:
      return true;
    }
  }

  bool mayHaveIdentifier() const {
    switch (Context) {
    case DeclaratorContext::File:
    case DeclaratorContext::KNRTypeList:
    case DeclaratorContext::Member:
    case DeclaratorContext::Block:
    case DeclaratorContext::ForInit:
    case DeclaratorContext::Prototype:
      return true;
    default:
      return false;
    }
  }

  bool isPastIdentifier() const { return Name.isValid(); }

  bool hasName() const { return Name.getIdentifierInfo() != nullptr; }

  IdentifierInfo *getIdentifier() const { return Name.getIdentifierInfo(); }
  SourceLocation getIdentifierLoc() const { return Name.getBeginLoc(); }

  void SetIdentifier(IdentifierInfo *Id, SourceLocation IdLoc) {
    Name.setIdentifier(Id, IdLoc);
  }

  void AddTypeInfo(const DeclaratorChunk &TI, ParsedAttributes &&attrs,
                   SourceLocation EndLoc) {
    DeclTypeInfo.push_back(TI);
    DeclTypeInfo.back().getAttrs().addAll(attrs.begin(), attrs.end());
    getAttributePool().takeAllFrom(attrs.getPool());

    if (!EndLoc.isInvalid())
      SetRangeEnd(EndLoc);
  }

  void AddTypeInfo(const DeclaratorChunk &TI, SourceLocation EndLoc) {
    DeclTypeInfo.push_back(TI);

    if (!EndLoc.isInvalid())
      SetRangeEnd(EndLoc);
  }

  unsigned getNumTypeObjects() const { return DeclTypeInfo.size(); }

  const DeclaratorChunk &getTypeObject(unsigned i) const {
    assert(i < DeclTypeInfo.size() && "Invalid type chunk");
    return DeclTypeInfo[i];
  }
  DeclaratorChunk &getTypeObject(unsigned i) {
    assert(i < DeclTypeInfo.size() && "Invalid type chunk");
    return DeclTypeInfo[i];
  }

  typedef llvm::SmallVectorImpl<DeclaratorChunk>::const_iterator
      type_object_iterator;
  typedef llvm::iterator_range<type_object_iterator> type_object_range;

  type_object_range type_objects() const {
    return type_object_range(DeclTypeInfo.begin(), DeclTypeInfo.end());
  }

  bool isFunctionDeclarator(unsigned &idx) const {
    for (unsigned i = 0, i_end = DeclTypeInfo.size(); i < i_end; ++i) {
      switch (DeclTypeInfo[i].Kind) {
      case DeclaratorChunk::Function:
        idx = i;
        return true;
      case DeclaratorChunk::Paren:
        continue;
      case DeclaratorChunk::Pointer:
      case DeclaratorChunk::Array:
        return false;
      }
      llvm_unreachable("Invalid type chunk");
    }
    return false;
  }

  bool isFunctionDeclarator() const {
    unsigned index;
    return isFunctionDeclarator(index);
  }

  DeclaratorChunk::FunctionTypeInfo &getFunctionTypeInfo() {
    assert(isFunctionDeclarator() && "Not a function declarator!");
    unsigned index = 0;
    isFunctionDeclarator(index);
    return DeclTypeInfo[index].Fun;
  }

  const DeclaratorChunk::FunctionTypeInfo &getFunctionTypeInfo() const {
    return const_cast<Declarator *>(this)->getFunctionTypeInfo();
  }

  bool isDeclarationOfFunction() const;

  bool isFunctionDeclarationContext() const {
    if (getDeclSpec().getStorageClassSpec() == DeclSpec::SCS_typedef)
      return false;

    switch (Context) {
    case DeclaratorContext::File:
    case DeclaratorContext::Member:
    case DeclaratorContext::Block:
    case DeclaratorContext::ForInit:
      return true;
    default:
      return false;
    }
  }

  bool isFunctionDeclaratorAFunctionDeclaration() const {
    if (!isFunctionDeclarationContext())
      return false;

    for (unsigned I = 0, N = getNumTypeObjects(); I != N; ++I)
      if (getTypeObject(I).Kind != DeclaratorChunk::Paren)
        return false;

    return true;
  }

  void takeAttributes(ParsedAttributes &attrs) {
    Attrs.takeAllFrom(attrs);

    if (attrs.Range.getEnd().isValid())
      SetRangeEnd(attrs.Range.getEnd());
  }

  const ParsedAttributes &getAttributes() const { return Attrs; }
  ParsedAttributes &getAttributes() { return Attrs; }

  const ParsedAttributesView &getDeclarationAttributes() const {
    return DeclarationAttrs;
  }

  void setAsmLabel(Expr *E) { AsmLabel = E; }
  Expr *getAsmLabel() const { return AsmLabel; }

  void setInvalidType(bool Val = true) { InvalidType = Val; }
  bool isInvalidType() const {
    return InvalidType || DS.getTypeSpecType() == DeclSpec::TST_error;
  }

  void setGroupingParens(bool flag) { GroupingParens = flag; }
  bool hasGroupingParens() const { return GroupingParens; }

  void setFunctionDefinitionKind(FunctionDefinitionKind Val) {
    FunctionDefinition = static_cast<unsigned>(Val);
  }

  bool isFunctionDefinition() const {
    return getFunctionDefinitionKind() != FunctionDefinitionKind::Declaration;
  }

  FunctionDefinitionKind getFunctionDefinitionKind() const {
    return (FunctionDefinitionKind)FunctionDefinition;
  }

  void setHasInitializer(bool Val = true) { HasInitializer = Val; }
  bool hasInitializer() const { return HasInitializer; }

  void setRedeclaration(bool Val) { Redeclaration = Val; }
  bool isRedeclaration() const { return Redeclaration; }
};

struct FieldDeclarator {
  Declarator D;
  Expr *BitfieldSize;
  explicit FieldDeclarator(const DeclSpec &DS,
                           const ParsedAttributes &DeclarationAttrs)
      : D(DS, DeclarationAttrs, DeclaratorContext::Member),
        BitfieldSize(nullptr) {}
};

} // end namespace neverc

#endif // NEVERC_SEMA_DECLSPEC_H
