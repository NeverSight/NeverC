/*===-- PluginSema.h - NeverC semantic analysis plugin C ABI ------- C ---===*/

#ifndef NEVERC_PLUGIN_PLUGINSEMA_H
#define NEVERC_PLUGIN_PLUGINSEMA_H

#include "neverc/Plugin/PluginAST.h"
#include "neverc/Plugin/PluginCore.h"
#include "neverc/Plugin/PluginPhaseSchema.h" /* IWYU pragma: export */

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_SEMA_API_MAJOR UINT16_C(1)
#define NEVERC_SEMA_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_SEMA_HIGH UINT64_C(0x4e435053454d0001)
#define NEVERC_INTERFACE_SEMA_LOW UINT64_C(0x0000000000000001)

typedef uint64_t NevercSemaScopeFlags;
typedef uint32_t NevercSemaLookupKind;
typedef uint32_t NevercSemaLookupResultKind;
typedef uint32_t NevercSemaVectorKind;
typedef uint32_t NevercSemaConversionKind;
typedef uint32_t NevercSemaConversionContext;
typedef uint32_t NevercSemaConstantKind;
typedef uint64_t NevercSemaBuiltinFlags;
typedef uint32_t NevercSemaDiagnosticLevel;
typedef uint32_t NevercSemaExtensionDisposition;

#define NEVERC_SEMA_SCOPE_FILE UINT64_C(1)
#define NEVERC_SEMA_SCOPE_FUNCTION UINT64_C(2)
#define NEVERC_SEMA_SCOPE_RECORD UINT64_C(4)
#define NEVERC_SEMA_SCOPE_BLOCK UINT64_C(8)

#define NEVERC_SEMA_LOOKUP_ORDINARY UINT32_C(1)
#define NEVERC_SEMA_LOOKUP_TAG UINT32_C(2)
#define NEVERC_SEMA_LOOKUP_MEMBER UINT32_C(3)

#define NEVERC_SEMA_LOOKUP_NOT_FOUND UINT32_C(1)
#define NEVERC_SEMA_LOOKUP_FOUND UINT32_C(2)
#define NEVERC_SEMA_LOOKUP_AMBIGUOUS UINT32_C(3)

#define NEVERC_SEMA_VECTOR_GENERIC UINT32_C(1)
#define NEVERC_SEMA_VECTOR_EXTENDED UINT32_C(2)
#define NEVERC_SEMA_VECTOR_SCALABLE UINT32_C(3)

#define NEVERC_SEMA_CONVERSION_COMPATIBLE UINT32_C(1)
#define NEVERC_SEMA_CONVERSION_POINTER_TO_INTEGER UINT32_C(2)
#define NEVERC_SEMA_CONVERSION_INTEGER_TO_POINTER UINT32_C(3)
#define NEVERC_SEMA_CONVERSION_FUNCTION_VOID_POINTER UINT32_C(4)
#define NEVERC_SEMA_CONVERSION_INCOMPATIBLE_POINTER UINT32_C(5)
#define NEVERC_SEMA_CONVERSION_INCOMPATIBLE_FUNCTION_POINTER UINT32_C(6)
#define NEVERC_SEMA_CONVERSION_STRICT_FUNCTION_POINTER UINT32_C(7)
#define NEVERC_SEMA_CONVERSION_POINTER_SIGN UINT32_C(8)
#define NEVERC_SEMA_CONVERSION_DISCARDS_QUALIFIERS UINT32_C(9)
#define NEVERC_SEMA_CONVERSION_INVALID_QUALIFIERS UINT32_C(10)
#define NEVERC_SEMA_CONVERSION_ADDRESS_SPACE_MISMATCH UINT32_C(11)
#define NEVERC_SEMA_CONVERSION_NESTED_QUALIFIERS UINT32_C(12)
#define NEVERC_SEMA_CONVERSION_VECTOR UINT32_C(13)
#define NEVERC_SEMA_CONVERSION_INCOMPATIBLE UINT32_C(14)

#define NEVERC_SEMA_CONVERSION_ASSIGNMENT UINT32_C(1)
#define NEVERC_SEMA_CONVERSION_ARGUMENT UINT32_C(2)
#define NEVERC_SEMA_CONVERSION_RETURN UINT32_C(3)
#define NEVERC_SEMA_CONVERSION_INITIALIZATION UINT32_C(4)
#define NEVERC_SEMA_CONVERSION_EXPLICIT_CAST UINT32_C(5)

#define NEVERC_SEMA_CONSTANT_NONE UINT32_C(1)
#define NEVERC_SEMA_CONSTANT_INDETERMINATE UINT32_C(2)
#define NEVERC_SEMA_CONSTANT_INTEGER UINT32_C(3)
#define NEVERC_SEMA_CONSTANT_FLOAT UINT32_C(4)
#define NEVERC_SEMA_CONSTANT_FIXED_POINT UINT32_C(5)
#define NEVERC_SEMA_CONSTANT_COMPLEX_INTEGER UINT32_C(6)
#define NEVERC_SEMA_CONSTANT_COMPLEX_FLOAT UINT32_C(7)
#define NEVERC_SEMA_CONSTANT_ADDRESS UINT32_C(8)
#define NEVERC_SEMA_CONSTANT_VECTOR UINT32_C(9)
#define NEVERC_SEMA_CONSTANT_ARRAY UINT32_C(10)
#define NEVERC_SEMA_CONSTANT_STRUCT UINT32_C(11)
#define NEVERC_SEMA_CONSTANT_UNION UINT32_C(12)
#define NEVERC_SEMA_CONSTANT_ADDRESS_LABEL_DIFFERENCE UINT32_C(13)

#define NEVERC_SEMA_BUILTIN_TARGET_SPECIFIC UINT64_C(1)
#define NEVERC_SEMA_BUILTIN_PURE UINT64_C(2)
#define NEVERC_SEMA_BUILTIN_CONST UINT64_C(4)
#define NEVERC_SEMA_BUILTIN_NOTHROW UINT64_C(8)
#define NEVERC_SEMA_BUILTIN_NORETURN UINT64_C(16)
#define NEVERC_SEMA_BUILTIN_RETURNS_TWICE UINT64_C(32)
#define NEVERC_SEMA_BUILTIN_UNEVALUATED UINT64_C(64)
#define NEVERC_SEMA_BUILTIN_LIBRARY_FUNCTION UINT64_C(128)
#define NEVERC_SEMA_BUILTIN_CUSTOM_TYPECHECK UINT64_C(256)

#define NEVERC_SEMA_DIAGNOSTIC_NOTE UINT32_C(1)
#define NEVERC_SEMA_DIAGNOSTIC_REMARK UINT32_C(2)
#define NEVERC_SEMA_DIAGNOSTIC_WARNING UINT32_C(3)
#define NEVERC_SEMA_DIAGNOSTIC_ERROR UINT32_C(4)
#define NEVERC_SEMA_DIAGNOSTIC_FATAL UINT32_C(5)

#define NEVERC_SEMA_EXTENSION_UNHANDLED UINT32_C(1)
#define NEVERC_SEMA_EXTENSION_HANDLED UINT32_C(2)

NEVERC_ABI_PACK_BEGIN

typedef NevercHandle NevercSemaScopeHandle;
typedef NevercHandle NevercLookupResultHandle;
typedef NevercHandle NevercConversionSequenceHandle;
typedef NevercHandle NevercConstantValueHandle;
typedef NevercHandle NevercSemanticUnitHandle;
typedef NevercHandle NevercSemaMutationLeaseHandle;

typedef struct NevercSemaScopeInfo {
  NevercABITableHeader Header;
  NevercSemaScopeHandle Parent;
  NevercDeclHandle DeclContext;
  NevercSemaScopeFlags Flags;
  uint64_t DeclarationCount;
} NevercSemaScopeInfo;

typedef struct NevercSemaLookupRequest {
  NevercABITableHeader Header;
  NevercSemaScopeHandle Scope;
  NevercStringView Name;
  NevercSemaLookupKind Kind;
  NevercBool IncludeHidden;
  uint32_t Reserved[2];
} NevercSemaLookupRequest;

typedef struct NevercSemaLookupResultInfo {
  NevercABITableHeader Header;
  NevercSemaLookupResultKind Kind;
  NevercBool Ambiguous;
  uint32_t Reserved;
  uint64_t CandidateCount;
} NevercSemaLookupResultInfo;

typedef struct NevercSemaFunctionTypeDescriptor {
  NevercABITableHeader Header;
  NevercTypeHandle ResultType;
  const NevercTypeHandle *ParameterTypes;
  uint64_t ParameterCount;
  NevercBool Variadic;
  uint32_t Reserved[3];
} NevercSemaFunctionTypeDescriptor;

typedef struct NevercSemaConversionSequenceInfo {
  NevercABITableHeader Header;
  NevercTypeHandle SourceType;
  NevercTypeHandle DestinationType;
  NevercSemaConversionKind Kind;
  NevercBool Viable;
  NevercBool RequiresDiagnostic;
  uint32_t Reserved;
} NevercSemaConversionSequenceInfo;

typedef struct NevercSemaConstantValueInfo {
  NevercABITableHeader Header;
  NevercSemaConstantKind Kind;
  NevercBool IsSigned;
  NevercBool HasSideEffects;
  NevercBool IsNullPointer;
  NevercBool IsOnePastTheEnd;
  uint32_t BitWidth;
  uint32_t Reserved;
  uint64_t ElementCount;
  int64_t AddressOffsetBytes;
  NevercASTNodeHandle AddressBase;
  NevercStringView Text;
} NevercSemaConstantValueInfo;

typedef struct NevercSemaBuiltinInfo {
  NevercABITableHeader Header;
  uint32_t BuiltinID;
  uint32_t Reserved;
  NevercSemaBuiltinFlags Flags;
  NevercStringView Name;
  NevercStringView TypeEncoding;
  NevercStringView RequiredFeatures;
  NevercStringView HeaderName;
} NevercSemaBuiltinInfo;

typedef struct NevercSemaDiagnosticDescriptor {
  NevercABITableHeader Header;
  NevercSemaDiagnosticLevel Level;
  uint32_t Reserved;
  NevercSourceLocation Location;
  NevercStringView Message;
} NevercSemaDiagnosticDescriptor;

typedef struct NevercSemaExpressionExtensionInput {
  NevercABITableHeader Header;
  NevercExprHandle Left;
  NevercExprHandle Right;
  NevercSourceLocation OperatorLocation;
} NevercSemaExpressionExtensionInput;

typedef struct NevercSemaExpressionExtensionOutput {
  NevercABITableHeader Header;
  NevercSemaExtensionDisposition Disposition;
  uint32_t Reserved;
  NevercExprHandle Expression;
} NevercSemaExpressionExtensionOutput;

typedef struct NevercSemaStatementExtensionInput {
  NevercABITableHeader Header;
  const NevercStmtHandle *Statements;
  uint64_t StatementCount;
  NevercSourceLocation LeftBrace;
  NevercSourceLocation RightBrace;
} NevercSemaStatementExtensionInput;

typedef struct NevercSemaStatementExtensionOutput {
  NevercABITableHeader Header;
  NevercSemaExtensionDisposition Disposition;
  uint32_t Reserved;
  NevercStmtHandle Statement;
} NevercSemaStatementExtensionOutput;

typedef struct NevercSemaDeclarationExtensionInput {
  NevercABITableHeader Header;
  NevercDeclHandle Declaration;
  NevercSourceLocation NameLocation;
} NevercSemaDeclarationExtensionInput;

typedef struct NevercSemaDeclarationExtensionOutput {
  NevercABITableHeader Header;
  NevercSemaExtensionDisposition Disposition;
  uint32_t Reserved;
  NevercDeclHandle Declaration;
} NevercSemaDeclarationExtensionOutput;

typedef struct NevercSemaTypeExtensionInput {
  NevercABITableHeader Header;
  NevercStringView Name;
  NevercSourceLocation NameLocation;
} NevercSemaTypeExtensionInput;

typedef struct NevercSemaTypeExtensionOutput {
  NevercABITableHeader Header;
  NevercSemaExtensionDisposition Disposition;
  uint32_t Reserved;
  NevercTypeHandle Type;
} NevercSemaTypeExtensionOutput;

typedef struct NevercSemaLookupExtensionInput {
  NevercABITableHeader Header;
  NevercStringView Name;
  NevercSourceLocation NameLocation;
  NevercSemaLookupKind Kind;
  uint32_t Reserved;
} NevercSemaLookupExtensionInput;

typedef struct NevercSemaLookupExtensionOutput {
  NevercABITableHeader Header;
  NevercSemaExtensionDisposition Disposition;
  uint32_t Reserved;
  const NevercDeclHandle *Candidates;
  uint64_t CandidateCount;
} NevercSemaLookupExtensionOutput;

typedef struct NevercSemaConversionExtensionInput {
  NevercABITableHeader Header;
  NevercExprHandle Expression;
  NevercTypeHandle SourceType;
  NevercTypeHandle DestinationType;
  NevercSemaConversionContext Context;
  uint32_t Reserved;
} NevercSemaConversionExtensionInput;

typedef struct NevercSemaConversionExtensionOutput {
  NevercABITableHeader Header;
  NevercSemaExtensionDisposition Disposition;
  uint32_t Reserved;
  NevercExprHandle Expression;
} NevercSemaConversionExtensionOutput;

typedef struct NevercSemaAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *GetCurrentScope)(
      void *Context, NevercTaskHandle Task, NevercSemaScopeHandle *OutScope);
  NevercStatus(NEVERC_CALL *GetScopeInfo)(void *Context,
                                          NevercTaskHandle Task,
                                          NevercSemaScopeHandle Scope,
                                          NevercSemaScopeInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetScopeDeclaration)(
      void *Context, NevercTaskHandle Task, NevercSemaScopeHandle Scope,
      uint64_t Index, NevercDeclHandle *OutDeclaration);
  NevercStatus(NEVERC_CALL *LookupName)(
      void *Context, NevercTaskHandle Task,
      const NevercSemaLookupRequest *Request,
      NevercLookupResultHandle *OutResult);
  NevercStatus(NEVERC_CALL *GetLookupResultInfo)(
      void *Context, NevercTaskHandle Task, NevercLookupResultHandle Result,
      NevercSemaLookupResultInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetLookupCandidate)(
      void *Context, NevercTaskHandle Task, NevercLookupResultHandle Result,
      uint64_t Index, NevercDeclHandle *OutDeclaration);
  NevercStatus(NEVERC_CALL *DestroyLookupResult)(
      void *Context, NevercTaskHandle Task, NevercLookupResultHandle Result);
  NevercStatus(NEVERC_CALL *AcquireMutationLease)(
      void *Context, NevercTaskHandle Task,
      NevercSemaMutationLeaseHandle *OutLease);
  NevercStatus(NEVERC_CALL *ReleaseMutationLease)(
      void *Context, NevercTaskHandle Task,
      NevercSemaMutationLeaseHandle Lease);
  NevercStatus(NEVERC_CALL *GetBuiltinType)(
      void *Context, NevercTaskHandle Task, NevercBuiltinTypeKind Kind,
      NevercTypeHandle *OutType);
  NevercStatus(NEVERC_CALL *CreatePointerType)(
      void *Context, NevercTaskHandle Task,
      NevercSemaMutationLeaseHandle Lease, NevercTypeHandle Pointee,
      NevercTypeHandle *OutType);
  NevercStatus(NEVERC_CALL *CreateConstantArrayType)(
      void *Context, NevercTaskHandle Task,
      NevercSemaMutationLeaseHandle Lease, NevercTypeHandle Element,
      uint64_t ElementCount, NevercTypeHandle *OutType);
  NevercStatus(NEVERC_CALL *CreateFunctionType)(
      void *Context, NevercTaskHandle Task,
      NevercSemaMutationLeaseHandle Lease,
      const NevercSemaFunctionTypeDescriptor *Descriptor,
      NevercTypeHandle *OutType);
  NevercStatus(NEVERC_CALL *GetTagType)(
      void *Context, NevercTaskHandle Task, NevercDeclHandle Declaration,
      NevercTypeHandle *OutType);
  NevercStatus(NEVERC_CALL *CreateAtomicType)(
      void *Context, NevercTaskHandle Task,
      NevercSemaMutationLeaseHandle Lease, NevercTypeHandle ValueType,
      NevercTypeHandle *OutType);
  NevercStatus(NEVERC_CALL *CreateVectorType)(
      void *Context, NevercTaskHandle Task,
      NevercSemaMutationLeaseHandle Lease, NevercTypeHandle Element,
      uint32_t ElementCount, NevercSemaVectorKind Kind,
      NevercTypeHandle *OutType);
  NevercStatus(NEVERC_CALL *GetCanonicalType)(
      void *Context, NevercTaskHandle Task, NevercTypeHandle Type,
      NevercTypeHandle *OutType);
  NevercStatus(NEVERC_CALL *AreTypesCompatible)(
      void *Context, NevercTaskHandle Task, NevercTypeHandle Left,
      NevercTypeHandle Right, NevercBool *OutCompatible);
  NevercStatus(NEVERC_CALL *ClassifyImplicitConversion)(
      void *Context, NevercTaskHandle Task, NevercTypeHandle Source,
      NevercTypeHandle Destination, NevercConversionSequenceHandle *OutSequence);
  NevercStatus(NEVERC_CALL *GetConversionSequenceInfo)(
      void *Context, NevercTaskHandle Task,
      NevercConversionSequenceHandle Sequence,
      NevercSemaConversionSequenceInfo *OutInfo);
  NevercStatus(NEVERC_CALL *ApplyImplicitConversion)(
      void *Context, NevercTaskHandle Task,
      NevercSemaMutationLeaseHandle Lease,
      NevercConversionSequenceHandle Sequence, NevercExprHandle Expression,
      NevercSemaConversionContext ConversionContext,
      NevercExprHandle *OutExpression);
  NevercStatus(NEVERC_CALL *CreateExplicitCast)(
      void *Context, NevercTaskHandle Task,
      NevercSemaMutationLeaseHandle Lease, NevercExprHandle Expression,
      NevercTypeHandle Destination, NevercExprHandle *OutExpression);
  NevercStatus(NEVERC_CALL *DestroyConversionSequence)(
      void *Context, NevercTaskHandle Task,
      NevercConversionSequenceHandle Sequence);
  NevercStatus(NEVERC_CALL *EvaluateConstant)(
      void *Context, NevercTaskHandle Task, NevercExprHandle Expression,
      NevercConstantValueHandle *OutValue);
  NevercStatus(NEVERC_CALL *GetConstantValueInfo)(
      void *Context, NevercTaskHandle Task, NevercConstantValueHandle Value,
      NevercSemaConstantValueInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetConstantIntegerWord)(
      void *Context, NevercTaskHandle Task, NevercConstantValueHandle Value,
      uint64_t Index, uint64_t *OutWord);
  NevercStatus(NEVERC_CALL *GetConstantElement)(
      void *Context, NevercTaskHandle Task, NevercConstantValueHandle Value,
      uint64_t Index, NevercConstantValueHandle *OutElement);
  NevercStatus(NEVERC_CALL *DestroyConstantValue)(
      void *Context, NevercTaskHandle Task, NevercConstantValueHandle Value);
  NevercStatus(NEVERC_CALL *HasDeclAttribute)(
      void *Context, NevercTaskHandle Task, NevercDeclHandle Declaration,
      NevercStringView AttributeName, NevercBool *OutPresent);
  NevercStatus(NEVERC_CALL *GetBuiltinInfo)(
      void *Context, NevercTaskHandle Task, NevercStringView Name,
      NevercSemaBuiltinInfo *OutInfo);
  NevercStatus(NEVERC_CALL *EmitDiagnostic)(
      void *Context, NevercTaskHandle Task,
      NevercSemaMutationLeaseHandle Lease,
      const NevercSemaDiagnosticDescriptor *Descriptor);
  NevercStatus(NEVERC_CALL *GetExpressionExtensionInput)(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaExpressionExtensionInput *OutInput);
  NevercStatus(NEVERC_CALL *CreateExpressionExtensionOutput)(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaExpressionExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput);
  NevercStatus(NEVERC_CALL *GetStatementExtensionInput)(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaStatementExtensionInput *OutInput);
  NevercStatus(NEVERC_CALL *CreateStatementExtensionOutput)(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaStatementExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput);
  NevercStatus(NEVERC_CALL *GetDeclarationExtensionInput)(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaDeclarationExtensionInput *OutInput);
  NevercStatus(NEVERC_CALL *CreateDeclarationExtensionOutput)(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaDeclarationExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput);
  NevercStatus(NEVERC_CALL *GetTypeExtensionInput)(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaTypeExtensionInput *OutInput);
  NevercStatus(NEVERC_CALL *CreateTypeExtensionOutput)(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaTypeExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput);
  NevercStatus(NEVERC_CALL *GetLookupExtensionInput)(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaLookupExtensionInput *OutInput);
  NevercStatus(NEVERC_CALL *CreateLookupExtensionOutput)(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaLookupExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput);
  NevercStatus(NEVERC_CALL *GetConversionExtensionInput)(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercSemaConversionExtensionInput *OutInput);
  NevercStatus(NEVERC_CALL *CreateConversionExtensionOutput)(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercPhaseContinuation *Continuation,
      const NevercSemaConversionExtensionOutput *Descriptor,
      NevercArtifactHandle *OutOutput);
} NevercSemaAPI;

NEVERC_ABI_PACK_END

#ifdef __cplusplus
}
#endif

#endif
