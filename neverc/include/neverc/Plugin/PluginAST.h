/*===-- PluginAST.h - NeverC syntax tree plugin C ABI -------------- C ---===*/

#ifndef NEVERC_PLUGIN_PLUGINAST_H
#define NEVERC_PLUGIN_PLUGINAST_H

#include "neverc/Plugin/PluginCore.h"
#include "neverc/Plugin/PluginPhaseSchema.h" /* IWYU pragma: export */
#include "neverc/Plugin/PluginSource.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_AST_API_MAJOR UINT16_C(1)
#define NEVERC_AST_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_AST_HIGH UINT64_C(0x4e43504153540001)
#define NEVERC_INTERFACE_AST_LOW UINT64_C(0x0000000000000001)

typedef uint32_t NevercASTNodeKind;
typedef uint32_t NevercDeclKind;
typedef uint32_t NevercStmtKind;
typedef uint32_t NevercTypeKind;
typedef uint32_t NevercTypeLocKind;
typedef uint32_t NevercAttrKind;
typedef uint32_t NevercASTPropertyID;
typedef uint32_t NevercASTChildSlotID;
typedef uint32_t NevercASTSchemaDomain;
typedef uint32_t NevercASTValueType;
typedef uint32_t NevercASTAccessMode;
typedef uint32_t NevercASTCardinality;
typedef uint32_t NevercASTSourceRangeMode;
typedef uint32_t NevercTypeQualifierFlags;
typedef uint32_t NevercTypeFlags;
typedef uint32_t NevercTypeAddressSpaceKind;
typedef uint32_t NevercBinaryOperatorKind;

#define NEVERC_AST_NODE_KIND_INVALID UINT32_C(0)
#define NEVERC_DECL_KIND_INVALID UINT32_C(0)
#define NEVERC_STMT_KIND_INVALID UINT32_C(0)
#define NEVERC_TYPE_KIND_INVALID UINT32_C(0)
#define NEVERC_TYPE_LOC_KIND_INVALID UINT32_C(0)
#define NEVERC_ATTR_KIND_INVALID UINT32_C(0)
#define NEVERC_AST_PROPERTY_INVALID UINT32_C(0)
#define NEVERC_AST_CHILD_SLOT_INVALID UINT32_C(0)

#define NEVERC_AST_SCHEMA_DOMAIN_DECL UINT32_C(1)
#define NEVERC_AST_SCHEMA_DOMAIN_STMT UINT32_C(2)
#define NEVERC_AST_SCHEMA_DOMAIN_TYPE UINT32_C(3)
#define NEVERC_AST_SCHEMA_DOMAIN_TYPE_LOC UINT32_C(4)
#define NEVERC_AST_SCHEMA_DOMAIN_ATTR UINT32_C(5)

#define NEVERC_AST_VALUE_BOOL UINT32_C(1)
#define NEVERC_AST_VALUE_I64 UINT32_C(2)
#define NEVERC_AST_VALUE_U64 UINT32_C(3)
#define NEVERC_AST_VALUE_STRING UINT32_C(4)
#define NEVERC_AST_VALUE_SOURCE_RANGE UINT32_C(5)
#define NEVERC_AST_VALUE_NODE UINT32_C(6)
#define NEVERC_AST_VALUE_DECL UINT32_C(7)
#define NEVERC_AST_VALUE_STMT UINT32_C(8)
#define NEVERC_AST_VALUE_EXPR UINT32_C(9)
#define NEVERC_AST_VALUE_TYPE UINT32_C(10)
#define NEVERC_AST_VALUE_TYPE_LOC UINT32_C(11)
#define NEVERC_AST_VALUE_ATTR UINT32_C(12)
#define NEVERC_AST_VALUE_IDENTIFIER UINT32_C(13)
#define NEVERC_AST_VALUE_ENUM UINT32_C(14)
#define NEVERC_AST_VALUE_VERSION UINT32_C(15)
#define NEVERC_AST_VALUE_PARAMETER_INDEX UINT32_C(16)
#define NEVERC_AST_VALUE_ALIGNMENT_OPERAND UINT32_C(17)

#define NEVERC_AST_ACCESS_READ_ONLY UINT32_C(1)
#define NEVERC_AST_ACCESS_READ_WRITE UINT32_C(2)
#define NEVERC_AST_ACCESS_BUILD_ONLY UINT32_C(3)

#define NEVERC_AST_CARDINALITY_REQUIRED UINT32_C(1)
#define NEVERC_AST_CARDINALITY_OPTIONAL UINT32_C(2)
#define NEVERC_AST_CARDINALITY_MANY UINT32_C(3)

#define NEVERC_AST_SOURCE_RANGE_NONE UINT32_C(0)
#define NEVERC_AST_SOURCE_RANGE_NATIVE UINT32_C(1)

#define NEVERC_TYPE_QUALIFIER_CONST (UINT32_C(1) << 0)
#define NEVERC_TYPE_QUALIFIER_RESTRICT (UINT32_C(1) << 1)
#define NEVERC_TYPE_QUALIFIER_VOLATILE (UINT32_C(1) << 2)
#define NEVERC_TYPE_QUALIFIER_UNALIGNED (UINT32_C(1) << 3)

#define NEVERC_TYPE_FLAG_CANONICAL (UINT32_C(1) << 0)
#define NEVERC_TYPE_FLAG_SUGARED (UINT32_C(1) << 1)
#define NEVERC_TYPE_FLAG_DEPENDENT (UINT32_C(1) << 2)
#define NEVERC_TYPE_FLAG_INCOMPLETE (UINT32_C(1) << 3)
#define NEVERC_TYPE_FLAG_FUNCTION (UINT32_C(1) << 4)
#define NEVERC_TYPE_FLAG_VARIADIC (UINT32_C(1) << 5)
#define NEVERC_TYPE_FLAG_HAS_KNOWN_LAYOUT (UINT32_C(1) << 6)
#define NEVERC_TYPE_FLAG_POINTER (UINT32_C(1) << 7)
#define NEVERC_TYPE_FLAG_ARRAY (UINT32_C(1) << 8)
#define NEVERC_TYPE_FLAG_VECTOR (UINT32_C(1) << 9)
#define NEVERC_TYPE_FLAG_ATOMIC (UINT32_C(1) << 10)

#define NEVERC_TYPE_ADDRESS_SPACE_DEFAULT UINT32_C(0)
#define NEVERC_TYPE_ADDRESS_SPACE_PTR32_SIGNED UINT32_C(1)
#define NEVERC_TYPE_ADDRESS_SPACE_PTR32_UNSIGNED UINT32_C(2)
#define NEVERC_TYPE_ADDRESS_SPACE_PTR64 UINT32_C(3)
#define NEVERC_TYPE_ADDRESS_SPACE_TARGET UINT32_C(4)

#define NEVERC_BINARY_OPERATOR_MUL UINT32_C(1)
#define NEVERC_BINARY_OPERATOR_DIV UINT32_C(2)
#define NEVERC_BINARY_OPERATOR_REM UINT32_C(3)
#define NEVERC_BINARY_OPERATOR_ADD UINT32_C(4)
#define NEVERC_BINARY_OPERATOR_SUB UINT32_C(5)
#define NEVERC_BINARY_OPERATOR_SHL UINT32_C(6)
#define NEVERC_BINARY_OPERATOR_SHR UINT32_C(7)
#define NEVERC_BINARY_OPERATOR_LT UINT32_C(8)
#define NEVERC_BINARY_OPERATOR_GT UINT32_C(9)
#define NEVERC_BINARY_OPERATOR_LE UINT32_C(10)
#define NEVERC_BINARY_OPERATOR_GE UINT32_C(11)
#define NEVERC_BINARY_OPERATOR_EQ UINT32_C(12)
#define NEVERC_BINARY_OPERATOR_NE UINT32_C(13)
#define NEVERC_BINARY_OPERATOR_AND UINT32_C(14)
#define NEVERC_BINARY_OPERATOR_XOR UINT32_C(15)
#define NEVERC_BINARY_OPERATOR_OR UINT32_C(16)
#define NEVERC_BINARY_OPERATOR_LOGICAL_AND UINT32_C(17)
#define NEVERC_BINARY_OPERATOR_LOGICAL_OR UINT32_C(18)
#define NEVERC_BINARY_OPERATOR_ASSIGN UINT32_C(19)
#define NEVERC_BINARY_OPERATOR_MUL_ASSIGN UINT32_C(20)
#define NEVERC_BINARY_OPERATOR_DIV_ASSIGN UINT32_C(21)
#define NEVERC_BINARY_OPERATOR_REM_ASSIGN UINT32_C(22)
#define NEVERC_BINARY_OPERATOR_ADD_ASSIGN UINT32_C(23)
#define NEVERC_BINARY_OPERATOR_SUB_ASSIGN UINT32_C(24)
#define NEVERC_BINARY_OPERATOR_SHL_ASSIGN UINT32_C(25)
#define NEVERC_BINARY_OPERATOR_SHR_ASSIGN UINT32_C(26)
#define NEVERC_BINARY_OPERATOR_AND_ASSIGN UINT32_C(27)
#define NEVERC_BINARY_OPERATOR_XOR_ASSIGN UINT32_C(28)
#define NEVERC_BINARY_OPERATOR_OR_ASSIGN UINT32_C(29)
#define NEVERC_BINARY_OPERATOR_COMMA UINT32_C(30)

#include "neverc/Plugin/Schema/PluginASTSchema.inc"

#if NEVERC_AST_SCHEMA_CAPABILITY_MAJOR != NEVERC_AST_API_MAJOR
#error "AST schema capability major must match the AST API major"
#endif

NEVERC_ABI_PACK_BEGIN

typedef NevercHandle NevercTreeContextHandle;
typedef NevercHandle NevercASTNodeHandle;
typedef NevercHandle NevercDeclHandle;
typedef NevercHandle NevercStmtHandle;
typedef NevercHandle NevercExprHandle;
typedef NevercHandle NevercTypeHandle;
typedef NevercHandle NevercAttrHandle;
typedef NevercHandle NevercDeclContextHandle;
typedef NevercHandle NevercTypeLocHandle;
typedef NevercHandle NevercASTUnitHandle;
typedef NevercHandle NevercASTBuilderHandle;
typedef NevercHandle NevercASTMutationHandle;

typedef struct NevercASTValue {
  NevercABITableHeader Header;
  NevercASTValueType Type;
  uint32_t Reserved;
  int64_t SignedValue;
  uint64_t UnsignedValue;
  NevercStringView StringValue;
  NevercSourceRange SourceRangeValue;
  NevercASTNodeHandle NodeValue;
} NevercASTValue;

typedef struct NevercASTNodeInfo {
  NevercABITableHeader Header;
  NevercASTNodeKind Kind;
  NevercASTSchemaDomain Domain;
  NevercASTNodeHandle Parent;
  NevercDeclContextHandle DeclContext;
  NevercSourceRange SourceRange;
} NevercASTNodeInfo;

typedef struct NevercTypeInfo {
  NevercABITableHeader Header;
  NevercTypeKind Kind;
  NevercTypeQualifierFlags QualifierFlags;
  NevercTypeFlags Flags;
  NevercTypeAddressSpaceKind AddressSpaceKind;
  uint32_t TargetAddressSpace;
  uint32_t Reserved;
  uint64_t SizeInBits;
  uint64_t AlignmentInBits;
  uint64_t ElementCount;
  NevercTypeHandle CanonicalType;
  NevercTypeHandle DesugaredType;
  NevercTypeHandle RelatedType;
  NevercStringView Name;
} NevercTypeInfo;

typedef struct NevercFunctionDeclInfo {
  NevercABITableHeader Header;
  NevercStringView Name;
  NevercTypeHandle FunctionType;
  NevercTypeHandle ReturnType;
  NevercStmtHandle Body;
  uint64_t ParameterCount;
  NevercBool IsVariadic;
  NevercBool IsDefinition;
  uint32_t Reserved[2];
} NevercFunctionDeclInfo;

typedef struct NevercAttrInfo {
  NevercABITableHeader Header;
  NevercAttrKind Kind;
  NevercBool IsImplicit;
  NevercBool IsInherited;
  uint32_t Reserved;
  NevercStringView Spelling;
  NevercSourceRange SourceRange;
} NevercAttrInfo;

typedef struct NevercVarDeclInfo {
  NevercABITableHeader Header;
  NevercStringView Name;
  NevercTypeHandle Type;
  NevercExprHandle Initializer;
  NevercBool IsDefinition;
  NevercBool HasGlobalStorage;
  uint32_t Reserved[2];
} NevercVarDeclInfo;

typedef struct NevercRecordDeclInfo {
  NevercABITableHeader Header;
  NevercStringView Name;
  uint64_t FieldCount;
  NevercBool IsComplete;
  NevercBool IsUnion;
  NevercBool HasFlexibleArrayMember;
  uint32_t Reserved;
} NevercRecordDeclInfo;

typedef struct NevercDeclRefExprInfo {
  NevercABITableHeader Header;
  NevercDeclHandle ReferencedDecl;
  NevercDeclHandle FoundDecl;
  NevercTypeHandle Type;
} NevercDeclRefExprInfo;

typedef struct NevercCallExprInfo {
  NevercABITableHeader Header;
  NevercExprHandle Callee;
  NevercDeclHandle DirectCallee;
  NevercTypeHandle Type;
  uint64_t ArgumentCount;
} NevercCallExprInfo;

typedef struct NevercBinaryOperatorInfo {
  NevercABITableHeader Header;
  NevercExprHandle Left;
  NevercExprHandle Right;
  NevercTypeHandle Type;
  NevercStringView Operator;
  NevercBinaryOperatorKind Kind;
  uint32_t Reserved;
} NevercBinaryOperatorInfo;

typedef struct NevercCompoundStmtInfo {
  NevercABITableHeader Header;
  uint64_t StatementCount;
} NevercCompoundStmtInfo;

typedef struct NevercAPIntView {
  NevercABITableHeader Header;
  const uint64_t *Words;
  uint64_t WordCount;
  uint32_t BitWidth;
  uint32_t Reserved;
} NevercAPIntView;

typedef struct NevercIntegerLiteralInfo {
  NevercABITableHeader Header;
  NevercTypeHandle Type;
  uint64_t WordCount;
  uint32_t BitWidth;
  uint32_t Reserved;
} NevercIntegerLiteralInfo;

typedef struct NevercASTAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *GetTranslationUnit)(void *Context,
                                                NevercTaskHandle Task,
                                                NevercDeclHandle *OutDecl);
  NevercStatus(NEVERC_CALL *GetNodeInfo)(void *Context, NevercTaskHandle Task,
                                         NevercASTNodeHandle Node,
                                         NevercASTNodeInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetNodeInfoBatch)(
      void *Context, NevercTaskHandle Task, const NevercASTNodeHandle *Nodes,
      uint64_t NodeCount, NevercASTNodeInfo *OutInfos, uint64_t OutInfoCapacity,
      uint64_t OutInfoStride);
  NevercStatus(NEVERC_CALL *GetProperty)(void *Context, NevercTaskHandle Task,
                                         NevercASTNodeHandle Node,
                                         NevercASTPropertyID Property,
                                         NevercASTValue *OutValue);
  NevercStatus(NEVERC_CALL *GetPropertyBatch)(
      void *Context, NevercTaskHandle Task, const NevercASTNodeHandle *Nodes,
      const NevercASTPropertyID *Properties, uint64_t QueryCount,
      NevercASTValue *OutValues, uint64_t OutValueCapacity,
      uint64_t OutValueStride);
  NevercStatus(NEVERC_CALL *GetChildCount)(void *Context, NevercTaskHandle Task,
                                           NevercASTNodeHandle Node,
                                           uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetChild)(void *Context, NevercTaskHandle Task,
                                      NevercASTNodeHandle Node, uint64_t Index,
                                      NevercASTNodeHandle *OutChild);
  NevercStatus(NEVERC_CALL *GetParent)(void *Context, NevercTaskHandle Task,
                                       NevercASTNodeHandle Node,
                                       NevercASTNodeHandle *OutParent);
  NevercStatus(NEVERC_CALL *GetDeclContext)(
      void *Context, NevercTaskHandle Task, NevercDeclHandle Decl,
      NevercDeclContextHandle *OutContext);
  NevercStatus(NEVERC_CALL *GetTypeInfo)(void *Context, NevercTaskHandle Task,
                                         NevercTypeHandle Type,
                                         NevercTypeInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetTypeElement)(void *Context,
                                            NevercTaskHandle Task,
                                            NevercTypeHandle Type,
                                            uint64_t Index,
                                            NevercTypeHandle *OutElement);
  NevercStatus(NEVERC_CALL *GetFunctionDeclInfo)(
      void *Context, NevercTaskHandle Task, NevercDeclHandle Decl,
      NevercFunctionDeclInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetFunctionDeclParameter)(
      void *Context, NevercTaskHandle Task, NevercDeclHandle Decl,
      uint64_t Index, NevercDeclHandle *OutParameter);
  NevercStatus(NEVERC_CALL *GetDeclAttributeCount)(void *Context,
                                                   NevercTaskHandle Task,
                                                   NevercDeclHandle Decl,
                                                   uint64_t *OutCount);
  NevercStatus(NEVERC_CALL *GetDeclAttribute)(void *Context,
                                              NevercTaskHandle Task,
                                              NevercDeclHandle Decl,
                                              uint64_t Index,
                                              NevercAttrHandle *OutAttribute);
  NevercStatus(NEVERC_CALL *GetAttrInfo)(void *Context, NevercTaskHandle Task,
                                         NevercAttrHandle Attribute,
                                         NevercAttrInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetVarDeclInfo)(void *Context,
                                            NevercTaskHandle Task,
                                            NevercDeclHandle Decl,
                                            NevercVarDeclInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetRecordDeclInfo)(void *Context,
                                               NevercTaskHandle Task,
                                               NevercDeclHandle Decl,
                                               NevercRecordDeclInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetDeclRefExprInfo)(void *Context,
                                                NevercTaskHandle Task,
                                                NevercExprHandle Expression,
                                                NevercDeclRefExprInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetCallExprInfo)(void *Context,
                                             NevercTaskHandle Task,
                                             NevercExprHandle Expression,
                                             NevercCallExprInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetCallExprArgument)(void *Context,
                                                 NevercTaskHandle Task,
                                                 NevercExprHandle Expression,
                                                 uint64_t Index,
                                                 NevercExprHandle *OutArgument);
  NevercStatus(NEVERC_CALL *GetBinaryOperatorInfo)(
      void *Context, NevercTaskHandle Task, NevercExprHandle Expression,
      NevercBinaryOperatorInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetCompoundStmtInfo)(
      void *Context, NevercTaskHandle Task, NevercStmtHandle Statement,
      NevercCompoundStmtInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetIntegerLiteralInfo)(
      void *Context, NevercTaskHandle Task, NevercExprHandle Expression,
      NevercIntegerLiteralInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetIntegerLiteralWord)(void *Context,
                                                   NevercTaskHandle Task,
                                                   NevercExprHandle Expression,
                                                   uint64_t Index,
                                                   uint64_t *OutWord);
  NevercStatus(NEVERC_CALL *CreateASTBuilder)(
      void *Context, NevercTaskHandle Task, NevercASTNodeKind Kind,
      NevercASTBuilderHandle *OutBuilder);
  NevercStatus(NEVERC_CALL *ASTBuilderSetProperty)(
      void *Context, NevercTaskHandle Task, NevercASTBuilderHandle Builder,
      NevercASTPropertyID Property, const NevercASTValue *Value);
  NevercStatus(NEVERC_CALL *ASTBuilderSetIntegerValue)(
      void *Context, NevercTaskHandle Task, NevercASTBuilderHandle Builder,
      const NevercAPIntView *Value);
  NevercStatus(NEVERC_CALL *ASTBuilderSetBinaryOperatorKind)(
      void *Context, NevercTaskHandle Task, NevercASTBuilderHandle Builder,
      NevercBinaryOperatorKind Kind);
  NevercStatus(NEVERC_CALL *ASTBuilderSetChild)(
      void *Context, NevercTaskHandle Task, NevercASTBuilderHandle Builder,
      NevercASTChildSlotID Slot, uint64_t Index, NevercASTNodeHandle Child);
  NevercStatus(NEVERC_CALL *ASTBuilderCommit)(void *Context,
                                              NevercTaskHandle Task,
                                              NevercASTBuilderHandle Builder,
                                              NevercASTNodeHandle *OutNode);
  NevercStatus(NEVERC_CALL *DestroyASTBuilder)(void *Context,
                                               NevercTaskHandle Task,
                                               NevercASTBuilderHandle Builder);
  NevercStatus(NEVERC_CALL *BeginASTMutation)(
      void *Context, NevercTaskHandle Task,
      NevercASTMutationHandle *OutMutation);
  NevercStatus(NEVERC_CALL *ASTMutationReplaceChild)(
      void *Context, NevercTaskHandle Task, NevercASTMutationHandle Mutation,
      NevercASTNodeHandle Parent, NevercASTChildSlotID Slot, uint64_t Index,
      NevercASTNodeHandle Replacement);
  NevercStatus(NEVERC_CALL *CommitASTMutation)(
      void *Context, NevercTaskHandle Task, NevercASTMutationHandle Mutation);
  NevercStatus(NEVERC_CALL *AbortASTMutation)(void *Context,
                                              NevercTaskHandle Task,
                                              NevercASTMutationHandle Mutation);
  NevercStatus(NEVERC_CALL *DestroyASTMutation)(
      void *Context, NevercTaskHandle Task, NevercASTMutationHandle Mutation);
} NevercASTAPI;

NEVERC_ABI_PACK_END

#ifdef __cplusplus
}
#endif

#endif
