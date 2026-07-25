**言語**: [English](ast-sema.md) | [简体中文](ast-sema.zh-CN.md) | [繁體中文](ast-sema.zh-TW.md) | [日本語](ast-sema.ja.md) | [한국어](ast-sema.ko.md) | [Français](ast-sema.fr.md) | [Deutsch](ast-sema.de.md) | [Español](ast-sema.es.md) | [Italiano](ast-sema.it.md) | [Русский](ast-sema.ru.md) | [العربية](ast-sema.ar.md)

[← NeverC プラグイン ABI](README.ja.md)

# NeverC プラグイン AST・意味解析 API

フロントエンドは 3 枚のテーブルでカバーされます。`NevercParserAPI` は、チェック
ポイント付きのトークンカーソルを操作することで、構文解析の一部 —— 新しい宣言形式
や新しい文 —— をプラグインに引き取らせます。`NevercASTAPI` は木を読み、トランザ
クショナルに変更します。`NevercSemaAPI` は名前探索、型の構築、変換の分類、定数
評価を担います。

AST は Clang のクラス階層を C に写したものではなく、**スキーマ** を通じて公開され
ます。ノードは不透明なハンドルで、安定した ID でプロパティを尋ねるとタグ付きの
`NevercASTValue` が返ります。この間接性こそが、この表面を LLVM のバージョンを
またいで安定させています。

## インターフェイス

```c
#include "neverc/Plugin/PluginAST.h"
#include "neverc/Plugin/PluginSema.h"
```

| インターフェイス | テーブル | バージョンマクロ |
|---|---|---|
| `NEVERC_INTERFACE_AST_{HIGH,LOW}` | `NevercASTAPI` | `NEVERC_AST_API_MAJOR`（1）/ `_MINOR`（1） |
| `NEVERC_INTERFACE_PARSER_{HIGH,LOW}` | `NevercParserAPI` | `NEVERC_PARSER_API_MAJOR` / `_MINOR` |
| `NEVERC_INTERFACE_SEMA_{HIGH,LOW}` | `NevercSemaAPI` | `NEVERC_SEMA_API_MAJOR` / `_MINOR` |

[`Schema/PluginASTSchema.inc`] がノード種別、プロパティ、子スロットの ID を供給し
ます。その capability major は `NEVERC_AST_API_MAJOR` と等しくなければなりません。

## フェーズ

7 つの構文フェーズと 7 つの意味フェーズがあり、いずれも
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE` です:

| 構文 | 意味 |
|---|---|
| `neverc.syntax.parse` | `neverc.sema.analyze` |
| `neverc.syntax.extension.declaration` | `neverc.sema.extension.declaration` |
| `neverc.syntax.extension.statement` | `neverc.sema.extension.statement` |
| `neverc.syntax.extension.expression` | `neverc.sema.extension.expression` |
| `neverc.syntax.extension.type_name` | `neverc.sema.extension.type` |
| `neverc.syntax.extension.attribute` | `neverc.sema.extension.lookup` |
| `neverc.syntax.extension.keyword` | `neverc.sema.extension.conversion` |

`neverc.syntax.parse` はトークンストリームを消費して AST ユニットを生成し、
`neverc.sema.analyze` はそのユニットを消費して意味ユニットを生成します。
`extension.*` フェーズは言語拡張のためのフックです。ホストは組み込みの処理へ
落ちる前に、この構文要素を扱いたいプラグインがいるかどうかを尋ねます。

## スキーマモデル

すべてのノードは `NevercASTNodeHandle` であり、型付きの別名
（`NevercDeclHandle`、`NevercStmtHandle`、`NevercExprHandle`、
`NevercTypeHandle`、`NevercAttrHandle`、`NevercDeclContextHandle`、
`NevercTypeLocHandle`）が用意されています。構造の巡回は一様です:

```c
NevercASTNodeInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info), NEVERC_AST_API_MAJOR,
                                     NEVERC_AST_API_MINOR, 0};
AST->GetNodeInfo(AST->Context, Task, Node, &Info);
/* Info.Kind、.Domain、.Parent、.DeclContext、.SourceRange */

uint64_t ChildCount = 0;
AST->GetChildCount(AST->Context, Task, Node, &ChildCount);
for (uint64_t I = 0; I != ChildCount; ++I) {
  NevercASTNodeHandle Child;
  AST->GetChild(AST->Context, Task, Node, I, &Child);
}
```

`Domain` は `NEVERC_AST_SCHEMA_DOMAIN_DECL`、`STMT`、`TYPE`、`TYPE_LOC`、`ATTR`
のいずれかです。

プロパティは ID を指定してタグ付き値へ読み出します:

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

`Type` がどのメンバが有効かを選びます: `NEVERC_AST_VALUE_BOOL`、`I64`、`U64`、
`STRING`、`SOURCE_RANGE`、`NODE`、`DECL`、`STMT`、`EXPR`、`TYPE`、`TYPE_LOC`、
`ATTR`、`IDENTIFIER`、`ENUM`、`VERSION`、`PARAMETER_INDEX`、
`ALIGNMENT_OPERAND`。スキーマは各プロパティのアクセスモード（`READ_ONLY`、
`READ_WRITE`、`BUILD_ONLY`）と多重度（`REQUIRED`、`OPTIONAL`、`MANY`）を記録して
いるので、読み取り専用プロパティへの書き込みは木を壊す代わりに API の段階で失敗
します。

多数のノードを一度に巡回するならバッチ呼び出しの方が安上がりです。出力ストライド
を取るので、自前の構造体配列へ直接書き込めます:

```c
AST->GetNodeInfoBatch(AST->Context, Task, Nodes, NodeCount,
                      OutInfos, OutInfoCapacity, OutInfoStride);
AST->GetPropertyBatch(AST->Context, Task, Nodes, Properties, QueryCount,
                      OutValues, OutValueCapacity, OutValueStride);
```

## 型付きアクセサ

プラグインが最もよく触る構文要素には、プロパティ検索ではなく直接の読み取り関数が
あります:

| 呼び出し | 得られるもの |
|---|---|
| `GetTranslationUnit` | ルート宣言 |
| `GetFunctionDeclInfo`、`GetFunctionDeclParameter` | 名前、型、戻り値型、本体、引数の個数、可変長、定義 |
| `GetVarDeclInfo` | 名前、型、初期化子、定義、グローバル記憶域 |
| `GetRecordDeclInfo` | 名前、フィールド数、完全性、共用体か、フレキシブル配列メンバ |
| `GetDeclAttributeCount`、`GetDeclAttribute`、`GetAttrInfo` | 属性の種別、綴り、暗黙か、継承か |
| `GetDeclRefExprInfo` | 参照された宣言と発見された宣言、型 |
| `GetCallExprInfo`、`GetCallExprArgument` | 呼び出し先、直接の呼び出し先、型、引数 |
| `GetBinaryOperatorInfo` | 左、右、型、演算子の綴りと種別 |
| `GetCompoundStmtInfo` | 文の個数 |
| `GetIntegerLiteralInfo`、`GetIntegerLiteralWord` | ビット幅とリトルエンディアンのワード |
| `GetTypeInfo`、`GetTypeElement` | 型の完全な記述 |
| `GetBuiltinType` | `NevercBuiltinTypeKind` による組み込み型 |

この中で最も豊かなのが `NevercTypeInfo` です:

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

組み込み型の種別は `NEVERC_BUILTIN_TYPE_VOID` と `_BOOL` から整数の階段を経て
`_LONG_DOUBLE` まで、二項演算子の種別は `NEVERC_BINARY_OPERATOR_MUL` から
`_COMMA` までです。

## 構築と変更

構築にはビルダを、変更にはトランザクションを使います。両者は組み合わさります。
まず置き換えるノードを作り、それから差し込みます。

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

64 ビットを超える幅のリテラルには、`ASTBuilderSetIntegerValue` が
`NevercAPIntView`（リトルエンディアンのワードとビット幅）を取ります。
`ASTBuilderSetBinaryOperatorKind` は二項式の演算子を設定します。

```c
NevercASTMutationHandle Mutation;
AST->BeginASTMutation(AST->Context, Task, &Mutation);
AST->ASTMutationReplaceChild(AST->Context, Task, Mutation, Parent, SlotID,
                             Index, NewNode);
AST->CommitASTMutation(AST->Context, Task, Mutation);   /* または AbortASTMutation */
AST->DestroyASTMutation(AST->Context, Task, Mutation);
```

コミットはステージングされた木を検証し、アトミックに公開します。コミットが失敗
すれば以前の木がそのまま残り、中止すればその変更が作ったハンドルは失効します。
[`pluginsdk/examples/ASTRewritePlugin.c`]
がパーサ傍受を含む一連の流れを示しています。

## ライフサイクルイベント

ポーリングする代わりに、フロントエンドが宣言を公開する 11 の地点を購読します:

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

種別は `TREE_INITIALIZE`、`SEMA_BEGIN`、`TOP_LEVEL_DECL`、
`INLINE_FUNCTION_DEFINITION`、`INTERESTING_DECL`、`TAG_DEFINITION`、
`TAG_REQUIRED_DEFINITION`、`TENTATIVE_DEFINITION`、`EXTERNAL_DECLARATION`、
`TRANSLATION_UNIT`、`SEMA_END` で、`NEVERC_AST_LIFECYCLE_EVENT_MASK_ALL` が 11 種
すべてを覆います。イベントは翻訳単位、単一の宣言、宣言の配列を運びます —— どれも
読み取り専用で、コールバックの間だけ借用されます。

## パーサ拡張

パーサ拡張には、投機的構文解析を組み込んだトークンカーソルが渡されます:

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
  /* … ノードを構築する … */
  Parser->CursorCommit(Parser->Context, Task, In.Cursor, Checkpoint);
  Out.Disposition = NEVERC_PARSER_EXTENSION_HANDLED;
  Out.ResultKind  = NEVERC_PARSER_RESULT_DECL;
  Out.Node        = MyNode;
}
Parser->CreateExtensionOutput(Parser->Context, Frame, Continuation, &Out,
                              &Output);
```

入力の `ExpectedResult` が、パーサの求めているものを教えます:
`NEVERC_PARSER_RESULT_DECL`、`STMT`、`EXPR`、`TYPE`、`ATTRIBUTE`。
`CreateParsedAttribute` は GNU（`__attribute__`）、C23（`[[…]]`）、`__declspec`
のいずれかの形式で属性を構築します。

`neverc.syntax.parse` そのもののプロバイダは、AST ユニット全体を公開します:

```c
NevercParserASTUnitDescriptor Unit = {0};
Unit.Header          = /* … */;
Unit.Product         = (NevercInterfaceID){NEVERC_AST_PRODUCT_STANDARD_HIGH,
                                           NEVERC_AST_PRODUCT_STANDARD_LOW};
Unit.TranslationUnit = TU;
Parser->CreateASTUnit(Parser->Context, Frame, &Unit, &Output);
```

`GetASTUnitInfo` はそのユニットの `SemanticState` を報告します。
`NEVERC_AST_UNIT_UNANALYZED` として公開されたユニットは意味解析を通して再生され
ます。`NEVERC_AST_UNIT_SEMANTICALLY_ANALYZED` は、その作業をプロバイダがすでに
済ませたと主張するものです。

## 意味問い合わせ

```c
NevercSemaLookupRequest Request = {0};
Request.Header = /* … */;
Request.Scope  = Scope;
Request.Name   = SV("my_symbol");
Request.Kind   = NEVERC_SEMA_LOOKUP_ORDINARY;   /* または _TAG、_MEMBER */

NevercLookupResultHandle Result;
Sema->LookupName(Sema->Context, Task, &Request, &Result);

NevercSemaLookupResultInfo Info = {0};
Info.Header = /* … */;
Sema->GetLookupResultInfo(Sema->Context, Task, Result, &Info);
/* Info.Kind は NOT_FOUND、FOUND、AMBIGUOUS。続いて Info.CandidateCount。 */

for (uint64_t I = 0; I != Info.CandidateCount; ++I) {
  NevercDeclHandle Candidate;
  Sema->GetLookupCandidate(Sema->Context, Task, Result, I, &Candidate);
}
Sema->DestroyLookupResult(Sema->Context, Task, Result);
```

`GetCurrentScope`、`GetScopeInfo`、`GetScopeDeclaration` がスコープ鎖をたどります。
スコープフラグは `NEVERC_SEMA_SCOPE_FILE`、`FUNCTION`、`RECORD`、`BLOCK` です。

定数評価は、その値の形を情報として持つハンドルを返します:

```c
NevercConstantValueHandle Value;
Sema->EvaluateConstant(Sema->Context, Task, Expression, &Value);

NevercSemaConstantValueInfo Info = {0};
Info.Header = /* … */;
Sema->GetConstantValueInfo(Sema->Context, Task, Value, &Info);
/* Info.Kind: NONE、INDETERMINATE、INTEGER、FLOAT、FIXED_POINT、
   COMPLEX_INTEGER、COMPLEX_FLOAT、ADDRESS、VECTOR、ARRAY、STRUCT、UNION、
   ADDRESS_LABEL_DIFFERENCE。 */

uint64_t Word;
Sema->GetConstantIntegerWord(Sema->Context, Task, Value, 0, &Word);
Sema->DestroyConstantValue(Sema->Context, Task, Value);
```

変換は適用される前に分類されるので、プラグインはその判断を覗けます:

```c
NevercConversionSequenceHandle Sequence;
Sema->ClassifyImplicitConversion(Sema->Context, Task, SourceType, DestType,
                                 &Sequence);
NevercSemaConversionSequenceInfo SeqInfo = {0};
SeqInfo.Header = /* … */;
Sema->GetConversionSequenceInfo(Sema->Context, Task, Sequence, &SeqInfo);
/* SeqInfo.Kind は COMPATIBLE、POINTER_TO_INTEGER、
   INTEGER_TO_POINTER、INCOMPATIBLE_POINTER、DISCARDS_QUALIFIERS、
   ADDRESS_SPACE_MISMATCH、VECTOR、INCOMPATIBLE などにわたる。
   続いて SeqInfo.Viable と .RequiresDiagnostic。 */
```

`AreTypesCompatible`、`GetCanonicalType`、`GetTagType`、`GetBuiltinInfo` が
読み取り専用の面を締めくくります。

## 変更リース

意味状態を変えるもの —— 型の作成、変換の適用、意味診断の発行 —— にはリースが要り
ます。リースこそが並行した意味処理を安全にします:

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
`CreateVectorType`、`CreateExplicitCast`、`EmitDiagnostic` はいずれもリースを取り
ます。変換の文脈は `NEVERC_SEMA_CONVERSION_ASSIGNMENT`、`ARGUMENT`、`RETURN`、
`INITIALIZATION`、`EXPLICIT_CAST` です。

## 意味拡張フェーズ

各拡張フェーズには対応する入力／出力の組があります。たとえば式のフック:

```c
NevercSemaExpressionExtensionInput In = {0};
In.Header = /* … */;
Sema->GetExpressionExtensionInput(Sema->Context, Frame, Frame->Input, &In);
/* In.Left、In.Right、In.OperatorLocation */

NevercSemaExpressionExtensionOutput Out = {0};
Out.Header      = In.Header;
Out.Disposition = NEVERC_SEMA_EXTENSION_HANDLED;   /* または _UNHANDLED */
Out.Expression  = Result;
Sema->CreateExpressionExtensionOutput(Sema->Context, Frame, Continuation,
                                      &Out, &Output);
```

同じ形が `Statement`、`Declaration`、`Type`、`Lookup`、`Conversion` にも当ては
まります。`NEVERC_SEMA_EXTENSION_UNHANDLED` を返せば組み込みの振る舞いが走ります。

`neverc.sema.analyze` のプロバイダは意味ユニットを公開します:

```c
NevercSemanticUnitDescriptor Unit = {0};
Unit.Header           = /* … */;
Unit.Product          = (NevercInterfaceID){NEVERC_SEMANTIC_PRODUCT_STANDARD_HIGH,
                                            NEVERC_SEMANTIC_PRODUCT_STANDARD_LOW};
Unit.TranslationUnit  = TU;
Unit.SemanticComplete = NEVERC_TRUE;
Sema->CreateSemanticUnit(Sema->Context, Frame, &Unit, &Output);
```

`GetSemanticUnitInfo` は `DiagnosticState`
（`NEVERC_SEMANTIC_DIAGNOSTICS_CLEAN` または `_HAS_ERROR`）、そのユニットが再生
されたかどうか、そして検証器の要約を報告します。

## 規則

- AST と型のハンドルはタスクスコープです。コールバックの外へ持ち越さないで
  ください。
- すべてのビルダ、変更、検索結果、変換列、定数値には対応する `Destroy*` があり
  ます。エラー経路でも呼んでください。
- リースなしの意味変更は `NEVERC_STATUS_INVALID_STATE` を返します。
- ライフサイクル観測者から木を変更しないでください —— 観測者は読み取り専用です。
  対応するフェーズのインターセプタを使ってください。
- プロパティ ID と子スロット ID はスキーマ定数です。数値リテラルを直書きせず、
  [`PluginASTSchema.inc`] の名前を使ってください。そうすればスキーマの改訂が
  コンパイルエラーになります。
- `SizeInBits` や `AlignmentInBits` を信じる前に、`NevercTypeInfo.Flags` に
  `HAS_KNOWN_LAYOUT` があるか確認してください。

規範的な宣言は [`PluginAST.h`]、[`PluginSema.h`]、[`Schema/ASTSchema.json`] を、動作する
パーサ傍受とアトミックな木の書き換えは
[`pluginsdk/examples/ASTRewritePlugin.c`] を参照してください。

<!-- reference links -->
[`PluginAST.h`]: ../../neverc/include/neverc/Plugin/PluginAST.h
[`PluginASTSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginASTSchema.inc
[`pluginsdk/examples/ASTRewritePlugin.c`]: ../../pluginsdk/examples/ASTRewritePlugin.c
[`PluginSema.h`]: ../../neverc/include/neverc/Plugin/PluginSema.h
[`Schema/ASTSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/ASTSchema.json
[`Schema/PluginASTSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginASTSchema.inc
