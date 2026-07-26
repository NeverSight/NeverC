**语言**: [English](ast-sema.md) | [简体中文](ast-sema.zh-CN.md) | [繁體中文](ast-sema.zh-TW.md) | [日本語](ast-sema.ja.md) | [한국어](ast-sema.ko.md) | [Français](ast-sema.fr.md) | [Deutsch](ast-sema.de.md) | [Español](ast-sema.es.md) | [Italiano](ast-sema.it.md) | [Русский](ast-sema.ru.md) | [العربية](ast-sema.ar.md)

[← NeverC 插件 ABI](README.zh-CN.md)

# NeverC 插件 AST 与语义 API

三张表覆盖整个前端。`NevercParserAPI` 让插件接管一部分解析工作——一种新的声明
形式、一种新的语句——方式是驱动一个带检查点的 token 游标。`NevercASTAPI` 读取
语法树并以事务方式修改它。`NevercSemaAPI` 负责名字查找、类型构造、转换分类和常
量求值。

AST 是通过 **schema** 暴露的，而不是 Clang 类层次的 C 版镜像。节点是不透明句
柄；你按稳定 ID 请求某个属性，拿回一个带标签的 `NevercASTValue`。正是这一点让这
套接口能跨 LLVM 版本保持稳定。

## 接口

```c
#include "neverc/Plugin/PluginAST.h"
#include "neverc/Plugin/PluginSema.h"
```

| 接口 | 表 | 版本宏 |
|---|---|---|
| `NEVERC_INTERFACE_AST_{HIGH,LOW}` | `NevercASTAPI` | `NEVERC_AST_API_MAJOR`（1）/ `_MINOR`（1） |
| `NEVERC_INTERFACE_PARSER_{HIGH,LOW}` | `NevercParserAPI` | `NEVERC_PARSER_API_MAJOR` / `_MINOR` |
| `NEVERC_INTERFACE_SEMA_{HIGH,LOW}` | `NevercSemaAPI` | `NEVERC_SEMA_API_MAJOR` / `_MINOR` |

[`Schema/PluginASTSchema.inc`] 提供节点种类、属性和子槽的 ID；它的能力主版本必须等
于 `NEVERC_AST_API_MAJOR`。

## 阶段

七个语法阶段和七个语义阶段，全部是
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE`：

| 语法 | 语义 |
|---|---|
| `neverc.syntax.parse` | `neverc.sema.analyze` |
| `neverc.syntax.extension.declaration` | `neverc.sema.extension.declaration` |
| `neverc.syntax.extension.statement` | `neverc.sema.extension.statement` |
| `neverc.syntax.extension.expression` | `neverc.sema.extension.expression` |
| `neverc.syntax.extension.type_name` | `neverc.sema.extension.type` |
| `neverc.syntax.extension.attribute` | `neverc.sema.extension.lookup` |
| `neverc.syntax.extension.keyword` | `neverc.sema.extension.conversion` |

`neverc.syntax.parse` 消费 token 流并产出一个 AST 单元；`neverc.sema.analyze` 消
费该单元并产出一个语义单元。`extension.*` 那些阶段是语言扩展的钩子：宿主在回退
到内建行为之前，会先问有没有插件想处理这个构造。

## Schema 模型

每个节点都是一个 `NevercASTNodeHandle`，另有带类型的别名
（`NevercDeclHandle`、`NevercStmtHandle`、`NevercExprHandle`、
`NevercTypeHandle`、`NevercAttrHandle`、`NevercDeclContextHandle`、
`NevercTypeLocHandle`）。结构性导航是统一的：

```c
NevercASTNodeInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info), NEVERC_AST_API_MAJOR,
                                     NEVERC_AST_API_MINOR, 0};
AST->GetNodeInfo(AST->Context, Task, Node, &Info);
/* Info.Kind, .Domain, .Parent, .DeclContext, .SourceRange */

uint64_t ChildCount = 0;
AST->GetChildCount(AST->Context, Task, Node, &ChildCount);
for (uint64_t I = 0; I != ChildCount; ++I) {
  NevercASTNodeHandle Child;
  AST->GetChild(AST->Context, Task, Node, I, &Child);
}
```

`Domain` 是 `NEVERC_AST_SCHEMA_DOMAIN_DECL`、`STMT`、`TYPE`、`TYPE_LOC` 或
`ATTR` 之一。

属性按 ID 读入一个带标签的值：

```c
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
```

`Type` 决定哪个成员有效：`NEVERC_AST_VALUE_BOOL`、`I64`、`U64`、`STRING`、
`SOURCE_RANGE`、`NODE`、`DECL`、`STMT`、`EXPR`、`TYPE`、`TYPE_LOC`、`ATTR`、
`IDENTIFIER`、`ENUM`、`VERSION`、`PARAMETER_INDEX` 或 `ALIGNMENT_OPERAND`。
schema 记录了每个属性的访问模式（`READ_ONLY`、`READ_WRITE`、`BUILD_ONLY`）和基
数（`REQUIRED`、`OPTIONAL`、`MANY`），所以写只读属性会在 API 层失败，而不是把树
弄坏。

一次遍历很多节点时，批量调用更划算，它们接受一个输出步长，让你能直接写进自己的
结构体数组：

```c
AST->GetNodeInfoBatch(AST->Context, Task, Nodes, NodeCount,
                      OutInfos, OutInfoCapacity, OutInfoStride);
AST->GetPropertyBatch(AST->Context, Task, Nodes, Properties, QueryCount,
                      OutValues, OutValueCapacity, OutValueStride);
```

## 带类型的访问器

对插件最常触碰的那些构造，有直接的读取器，不必走属性查找：

| 调用 | 给出 |
|---|---|
| `GetTranslationUnit` | 根声明 |
| `GetFunctionDeclInfo`、`GetFunctionDeclParameter` | 名字、类型、返回类型、函数体、参数个数、是否可变参数、是否定义 |
| `GetVarDeclInfo` | 名字、类型、初始化器、是否定义、是否全局存储 |
| `GetRecordDeclInfo` | 名字、字段数、是否完整、是否 union、柔性数组成员 |
| `GetDeclAttributeCount`、`GetDeclAttribute`、`GetAttrInfo` | 属性种类、拼写、是否隐式、是否继承 |
| `GetDeclRefExprInfo` | 被引用与被找到的声明、类型 |
| `GetCallExprInfo`、`GetCallExprArgument` | 被调用者、直接被调用者、类型、实参 |
| `GetBinaryOperatorInfo` | 左、右、类型、运算符拼写与种类 |
| `GetCompoundStmtInfo` | 语句数 |
| `GetIntegerLiteralInfo`、`GetIntegerLiteralWord` | 位宽与小端字 |
| `GetTypeInfo`、`GetTypeElement` | 完整的类型描述 |
| `GetBuiltinType` | 按 `NevercBuiltinTypeKind` 取内建类型 |

其中 `NevercTypeInfo` 内容最丰富：

```c
typedef struct NevercTypeInfo {
  NevercABITableHeader Header;
  NevercTypeKind Kind;
  NevercTypeQualifierFlags QualifierFlags;  /* CONST, RESTRICT, VOLATILE, UNALIGNED */
  NevercTypeFlags Flags;                    /* CANONICAL, SUGARED, DEPENDENT,
                                               INCOMPLETE, FUNCTION, VARIADIC,
                                               HAS_KNOWN_LAYOUT, POINTER, ARRAY,
                                               VECTOR, ATOMIC */
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
```

内建类型种类从 `NEVERC_BUILTIN_TYPE_VOID` 和 `_BOOL` 沿着整数阶梯一直到
`_LONG_DOUBLE`，二元运算符种类从 `NEVERC_BINARY_OPERATOR_MUL` 到 `_COMMA`。

## 构造与修改

构造用 builder，修改用事务。两者可以组合：先构造出替换节点，再把它换进去。

```c
NevercASTBuilderHandle Builder;
AST->CreateASTBuilder(AST->Context, Task, NodeKind, &Builder);

NevercASTValue Value = {0};
Value.Header = (NevercABITableHeader){sizeof(Value), NEVERC_AST_API_MAJOR,
                                      NEVERC_AST_API_MINOR, 0};
Value.Type          = NEVERC_AST_VALUE_U64;
Value.UnsignedValue = 1;
AST->ASTBuilderSetProperty(AST->Context, Task, Builder, PropertyID, &Value);
AST->ASTBuilderSetChild(AST->Context, Task, Builder, SlotID, 0, ChildNode);

NevercASTNodeHandle NewNode;
AST->ASTBuilderCommit(AST->Context, Task, Builder, &NewNode);
AST->DestroyASTBuilder(AST->Context, Task, Builder);
```

对于宽于 64 位的字面量，`ASTBuilderSetIntegerValue` 接受一个 `NevercAPIntView`
（小端字加位宽）；`ASTBuilderSetBinaryOperatorKind` 设置二元表达式的运算符。

```c
NevercASTMutationHandle Mutation;
AST->BeginASTMutation(AST->Context, Task, &Mutation);
AST->ASTMutationReplaceChild(AST->Context, Task, Mutation, Parent, SlotID,
                             Index, NewNode);
AST->CommitASTMutation(AST->Context, Task, Mutation);   /* 或 AbortASTMutation */
AST->DestroyASTMutation(AST->Context, Task, Mutation);
```

提交会校验暂存的树并原子发布。提交失败会让之前的树保持完好，而中止会让该
mutation 创建的句柄变成陈旧句柄。 [`pluginsdk/examples/ASTRewritePlugin.c`]
展示了包含解析器拦截在内的完整流程。

## 生命周期事件

不要轮询，订阅前端发布声明的那十一个时点：

```c
NevercASTLifecycleObserverDescriptor Observer = {0};
Observer.Header = /* … */;
Observer.Events =
    NEVERC_AST_LIFECYCLE_EVENT_MASK(NEVERC_AST_LIFECYCLE_TOP_LEVEL_DECL) |
    NEVERC_AST_LIFECYCLE_EVENT_MASK(NEVERC_AST_LIFECYCLE_TRANSLATION_UNIT);
Observer.Callback = on_lifecycle;
Observer.UserData = State;
AST->RegisterLifecycleObserver(AST->Context, Task, &Observer);
```

这些种类是 `TREE_INITIALIZE`、`SEMA_BEGIN`、`TOP_LEVEL_DECL`、
`INLINE_FUNCTION_DEFINITION`、`INTERESTING_DECL`、`TAG_DEFINITION`、
`TAG_REQUIRED_DEFINITION`、`TENTATIVE_DEFINITION`、`EXTERNAL_DECLARATION`、
`TRANSLATION_UNIT` 和 `SEMA_END`；`NEVERC_AST_LIFECYCLE_EVENT_MASK_ALL` 覆盖全
部十一个。事件携带翻译单元、单个声明和一个声明数组——全部只读，且仅在回调期间
借用。

## 解析器扩展

解析器扩展会拿到一个内建了预测解析能力的 token 游标：

```c
NevercParserExtensionInput In = {0};
In.Header = /* … */;
Parser->GetExtensionInput(Parser->Context, Frame, Frame->Input, &In);

NevercParserCheckpointHandle Checkpoint;
Parser->CursorCheckpoint(Parser->Context, Task, In.Cursor, &Checkpoint);

NevercTokenHandle Token;
Parser->CursorPeek(Parser->Context, Task, In.Cursor, /*Offset=*/0, &Token);
if (!is_my_construct(Token)) {
  Parser->CursorRollback(Parser->Context, Task, In.Cursor, Checkpoint);
  Out.Disposition = NEVERC_PARSER_EXTENSION_UNHANDLED;
} else {
  Parser->CursorConsume(Parser->Context, Task, In.Cursor, &Token);
  /* … 构造节点 … */
  Parser->CursorCommit(Parser->Context, Task, In.Cursor, Checkpoint);
  Out.Disposition = NEVERC_PARSER_EXTENSION_HANDLED;
  Out.ResultKind  = NEVERC_PARSER_RESULT_DECL;
  Out.Node        = MyNode;
}
Parser->CreateExtensionOutput(Parser->Context, Frame, Continuation, &Out,
                              &Output);
```

输入上的 `ExpectedResult` 告诉你解析器需要什么：`NEVERC_PARSER_RESULT_DECL`、
`STMT`、`EXPR`、`TYPE` 或 `ATTRIBUTE`。`CreateParsedAttribute` 可按 GNU
（`__attribute__`）、C23（`[[…]]`）或 `__declspec` 形式构造属性。

`neverc.syntax.parse` 本身的 Provider 发布的是整个 AST 单元：

```c
NevercParserASTUnitDescriptor Unit = {0};
Unit.Header          = /* … */;
Unit.Product         = (NevercInterfaceID){NEVERC_AST_PRODUCT_STANDARD_HIGH,
                                           NEVERC_AST_PRODUCT_STANDARD_LOW};
Unit.TranslationUnit = TU;
Parser->CreateASTUnit(Parser->Context, Frame, &Unit, &Output);
```

`GetASTUnitInfo` 报告该单元的 `SemanticState`。以
`NEVERC_AST_UNIT_UNANALYZED` 发布的单元会被重新送进语义分析重放一遍；
`NEVERC_AST_UNIT_SEMANTICALLY_ANALYZED` 则断言 Provider 已经做完了那部分工作。

## 语义查询

```c
NevercSemaLookupRequest Request = {0};
Request.Header = /* … */;
Request.Scope  = Scope;
Request.Name   = SV("my_symbol");
Request.Kind   = NEVERC_SEMA_LOOKUP_ORDINARY;   /* 或 _TAG、_MEMBER */

NevercLookupResultHandle Result;
Sema->LookupName(Sema->Context, Task, &Request, &Result);

NevercSemaLookupResultInfo Info = {0};
Info.Header = /* … */;
Sema->GetLookupResultInfo(Sema->Context, Task, Result, &Info);
/* Info.Kind 为 NOT_FOUND、FOUND 或 AMBIGUOUS；随后是 Info.CandidateCount。 */

for (uint64_t I = 0; I != Info.CandidateCount; ++I) {
  NevercDeclHandle Candidate;
  Sema->GetLookupCandidate(Sema->Context, Task, Result, I, &Candidate);
}
Sema->DestroyLookupResult(Sema->Context, Task, Result);
```

`GetCurrentScope`、`GetScopeInfo` 和 `GetScopeDeclaration` 遍历作用域链；作用域
标志有 `NEVERC_SEMA_SCOPE_FILE`、`FUNCTION`、`RECORD`、`BLOCK`。

常量求值返回一个句柄，其信息描述该值的形态：

```c
NevercConstantValueHandle Value;
Sema->EvaluateConstant(Sema->Context, Task, Expression, &Value);

NevercSemaConstantValueInfo Info = {0};
Info.Header = /* … */;
Sema->GetConstantValueInfo(Sema->Context, Task, Value, &Info);
/* Info.Kind：NONE、INDETERMINATE、INTEGER、FLOAT、FIXED_POINT、
   COMPLEX_INTEGER、COMPLEX_FLOAT、ADDRESS、VECTOR、ARRAY、STRUCT、UNION、
   ADDRESS_LABEL_DIFFERENCE。 */

uint64_t Word;
Sema->GetConstantIntegerWord(Sema->Context, Task, Value, 0, &Word);
Sema->DestroyConstantValue(Sema->Context, Task, Value);
```

转换在被应用之前先被分类，因此插件可以检视这个决定：

```c
NevercConversionSequenceHandle Sequence;
Sema->ClassifyImplicitConversion(Sema->Context, Task, SourceType, DestType,
                                 &Sequence);
NevercSemaConversionSequenceInfo SeqInfo = {0};
SeqInfo.Header = /* … */;
Sema->GetConversionSequenceInfo(Sema->Context, Task, Sequence, &SeqInfo);
/* SeqInfo.Kind 涵盖 COMPATIBLE、POINTER_TO_INTEGER、
   INTEGER_TO_POINTER、INCOMPATIBLE_POINTER、DISCARDS_QUALIFIERS、
   ADDRESS_SPACE_MISMATCH、VECTOR、INCOMPATIBLE 等；
   随后是 SeqInfo.Viable 和 .RequiresDiagnostic。 */
```

`AreTypesCompatible`、`GetCanonicalType`、`GetTagType` 和 `GetBuiltinInfo` 补全
了只读部分。

## 修改租约

任何改变语义状态的操作——创建类型、应用转换、发出语义诊断——都需要一份租约
（lease）。租约正是让并发语义工作得以安全的东西：

```c
NevercSemaMutationLeaseHandle Lease;
Sema->AcquireMutationLease(Sema->Context, Task, &Lease);

NevercTypeHandle Pointer;
Sema->CreatePointerType(Sema->Context, Task, Lease, Pointee, &Pointer);

NevercExprHandle Converted;
Sema->ApplyImplicitConversion(Sema->Context, Task, Lease, Sequence,
                              Expression, NEVERC_SEMA_CONVERSION_ARGUMENT,
                              &Converted);

Sema->ReleaseMutationLease(Sema->Context, Task, Lease);
```

`CreateConstantArrayType`、`CreateFunctionType`、`CreateAtomicType`、
`CreateVectorType`、`CreateExplicitCast` 和 `EmitDiagnostic` 都需要租约。转换上
下文有 `NEVERC_SEMA_CONVERSION_ASSIGNMENT`、`ARGUMENT`、`RETURN`、
`INITIALIZATION`、`EXPLICIT_CAST`。

## 语义扩展阶段

每个扩展阶段都有配对的输入／输出。以表达式钩子为例：

```c
NevercSemaExpressionExtensionInput In = {0};
In.Header = /* … */;
Sema->GetExpressionExtensionInput(Sema->Context, Frame, Frame->Input, &In);
/* In.Left, In.Right, In.OperatorLocation */

NevercSemaExpressionExtensionOutput Out = {0};
Out.Header      = In.Header;
Out.Disposition = NEVERC_SEMA_EXTENSION_HANDLED;   /* 或 _UNHANDLED */
Out.Expression  = Result;
Sema->CreateExpressionExtensionOutput(Sema->Context, Frame, Continuation,
                                      &Out, &Output);
```

`Statement`、`Declaration`、`Type`、`Lookup` 和 `Conversion` 都是同样的形状。返
回 `NEVERC_SEMA_EXTENSION_UNHANDLED` 会让内建行为继续运行。

`neverc.sema.analyze` 的 Provider 发布语义单元：

```c
NevercSemanticUnitDescriptor Unit = {0};
Unit.Header           = /* … */;
Unit.Product          = (NevercInterfaceID){NEVERC_SEMANTIC_PRODUCT_STANDARD_HIGH,
                                            NEVERC_SEMANTIC_PRODUCT_STANDARD_LOW};
Unit.TranslationUnit  = TU;
Unit.SemanticComplete = NEVERC_TRUE;
Sema->CreateSemanticUnit(Sema->Context, Frame, &Unit, &Output);
```

`GetSemanticUnitInfo` 报告 `DiagnosticState`
（`NEVERC_SEMANTIC_DIAGNOSTICS_CLEAN` 或 `_HAS_ERROR`）、该单元是否被重放，以及
一份验证器摘要。

## 规则

- AST 与类型句柄是任务作用域的。绝不要在回调之外保存。
- 每个 builder、mutation、查找结果、转换序列和常量值都有配对的 `Destroy*`；错误
  路径上也要调用。
- 没有租约就做语义修改会返回 `NEVERC_STATUS_INVALID_STATE`。
- 不要在生命周期观察者里修改语法树——观察者是只读的。请在对应阶段上用拦截器。
- 属性 ID 和子槽 ID 是 schema 常量。不要硬编码数字字面量；用
  [`PluginASTSchema.inc`] 里的名字，这样 schema 修订会变成编译错误。
- 在信任 `SizeInBits` 或 `AlignmentInBits` 之前，先检查
  `NevercTypeInfo.Flags` 里的 `HAS_KNOWN_LAYOUT`。

规范性声明见 [`PluginAST.h`]、[`PluginSema.h`] 和 [`Schema/ASTSchema.json`]；阶段策略
见 [`Schema/PhaseSchema.json`]；一个可运行的解析器拦截与原子树改写示例见
[`pluginsdk/examples/ASTRewritePlugin.c`]。

<!-- reference links -->
[`PluginAST.h`]: ../../neverc/include/neverc/Plugin/PluginAST.h
[`PluginASTSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginASTSchema.inc
[`pluginsdk/examples/ASTRewritePlugin.c`]: ../../pluginsdk/examples/ASTRewritePlugin.c
[`PluginSema.h`]: ../../neverc/include/neverc/Plugin/PluginSema.h
[`Schema/ASTSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/ASTSchema.json
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginASTSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginASTSchema.inc
