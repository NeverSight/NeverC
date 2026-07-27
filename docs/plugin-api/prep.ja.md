**言語**: [English](prep.md) | [简体中文](prep.zh-CN.md) | [繁體中文](prep.zh-TW.md) | [日本語](prep.ja.md) | [한국어](prep.ko.md) | [Français](prep.fr.md) | [Deutsch](prep.de.md) | [Español](prep.es.md) | [Italiano](prep.it.md) | [Русский](prep.ru.md) | [العربية](prep.ar.md)

[← NeverC プラグイン ABI](README.ja.md)

# NeverC プラグイン プリプロセッサ API

[`PluginPrep.h`] はプリプロセッサを 2 通りに公開します。39 種類のイベントへの
**購読** は、ファイルへの入場、マクロの定義と展開、条件の評価、pragma —— つまり
プリプロセッサが行うすべての読み取り専用トレースを与えます。6 つの **フェーズ**
はさらに踏み込み、結果を書き換えさせます。`#include` をリダイレクトする、マクロの
展開トークンを差し替える、pragma を自分で処理する、`__has_feature` に別の答えを
返す、といったことです。

## インターフェイス

```c
#include "neverc/Plugin/PluginPrep.h"

Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_PREP_HIGH, NEVERC_INTERFACE_PREP_LOW},
    NEVERC_PREP_API_MAJOR, NEVERC_PREP_API_MINOR, &Table, &Minor, &TableSize);
```

230 のトークン種別（`NEVERC_TOKEN_KIND_COUNT`）とプリプロセッサキーワード種別は
[`Schema/PluginPrepSchema.inc`] に由来します。ヘッダがこれをインクルードしており、
その capability major は `NEVERC_PREP_API_MAJOR` と一致していなければなりません。
食い違いは実行時の不意打ちではなくコンパイルエラーになります。各種別はカテゴリも
持ちます: `NEVERC_TOKEN_CATEGORY_SPECIAL`、`COMMENT`、`IDENTIFIER`、`LITERAL`、
`PUNCTUATOR`、`KEYWORD`、`ANNOTATION`。

## 6 つのプリプロセッサフェーズ

| フェーズ | ポリシー | 入力 → 出力 |
|---|---|---|
| `neverc.prep.token` | OBSERVABLE、INTERCEPTABLE、REPLACEABLE | 1 トークン → トークンリスト |
| `neverc.prep.build_token_stream` | 同上 | 範囲 → トークンストリーム |
| `neverc.prep.include.intercept` | 同上 | include 要求 → include 判断 |
| `neverc.prep.macro.intercept` | 同上 | マクロ操作 → アクション + トークン |
| `neverc.prep.pragma.intercept` | 同上 | pragma → アクション + トークン |
| `neverc.prep.feature_query.intercept` | 同上 | `__has_*` 問い合わせ → 値 |

6 つのうち 5 つは `NevercPrepAPI` 上に `Get<Kind>PhaseInput` と
`Create<Kind>PhaseOutput` の対を持ちます。`Create` 側はインターセプタの
`NevercPhaseContinuation` を取るので、出力はそれを所有するフェーズの内側からしか
作れません。`neverc.prep.build_token_stream` は例外で、
`GetTokenStreamPhaseInput` があり、continuation を取る `Create*PhaseOutput`
ではなく、フェーズの `Frame` 上の `TokenStreamBuilderCommit` で公開します。

## トークンの読み取り

```c
typedef struct NevercTokenInfo {
  NevercABITableHeader Header;
  NevercTokenKind Kind;
  NevercTokenFlags Flags;
  NevercTokenOriginKind Origin;
  uint32_t Reserved;
  NevercStringView Spelling;
  NevercSourceLocation Location;
  NevercSourceRange Range;
  NevercIdentifierHandle Identifier;
  NevercMacroDefinitionHandle MacroDefinition;
} NevercTokenInfo;
```

`Origin` は `NEVERC_TOKEN_ORIGIN_FILE`、`MACRO_REPLACEMENT`、`MACRO_ARGUMENT`、
`SYNTHESIZED` のいずれかで、利用者が打ったトークンとマクロが生んだトークンを
見分ける手段になります。

フラグはプリプロセッサ自身の帳簿であり、トークンを合成するときに効いてきます:

| フラグ | 意味 |
|---|---|
| `NEVERC_TOKEN_FLAG_START_OF_LINE` | その行の最初のトークン |
| `_LEADING_SPACE` | 直前に空白がある |
| `_DISABLE_EXPANSION` | このトークンをマクロ展開しない |
| `_NEEDS_CLEANING` | 綴りにエスケープ改行やトライグラフを含む |
| `_LEADING_EMPTY_MACRO` | 直前で空のマクロが展開された |
| `_HAS_UCN` | ユニバーサル文字名を含む |
| `_IGNORED_COMMA`、`_COMMA_AFTER_ELIDED` | 可変引数のカンマ省略に関する帳簿 |
| `_STRINGIFIED_IN_MACRO` | `#` が生成した |
| `_REINJECTED` | トークンストリームへ戻し入れられた |

`NEVERC_TOKEN_FLAG_ALL` は定義済みビット全体のマスクです。まとめ読みには
`GetTokenInfoBatch` を使います。ストリーム全体は `GetTokenStreamView` で
`NevercTokenView` レコードの軽量なビューとして読むか、`GetTokenStreamToken` で
1 ハンドルずつ読みます。1 本のストリームが保持できるのは最大
`NEVERC_PREP_TOKEN_STREAM_MAX_TOKENS`（16,777,216）トークンです。

## 識別子とマクロ

```c
NevercIdentifierHandle Identifier;
Prep->GetOrCreateIdentifier(Prep->Context, Task, SV("MY_MACRO"), &Identifier);

NevercMacroDefinitionHandle Definition;
Prep->GetMacroDefinitionForIdentifier(Prep->Context, Task, Identifier,
                                      &Definition);

NevercMacroDefinitionInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info), NEVERC_PREP_API_MAJOR,
                                     NEVERC_PREP_API_MINOR, 0};
Prep->GetMacroDefinitionInfo(Prep->Context, Task, Definition, &Info);
```

`NevercMacroDefinitionInfo` は名前、定義したディレクティブ、定義／終了／未定義化
の位置、引数と置換トークンの個数、そしてフラグ
`NEVERC_MACRO_FUNCTION_LIKE`、`VARIADIC`、`C99_VARIADIC`、`GNU_VARIADIC`、
`HAS_VA_OPT`、`BUILTIN`、`COMMA_PASTING` を報告します。個々の引数と置換トークンは
`GetMacroParameter` と `GetMacroReplacementToken` から得られます。

`NevercIdentifierInfo` はさらにトークン種別、プリプロセッサキーワード種別、
builtin ID、そして `NEVERC_IDENTIFIER_KEYWORD`、`_HAS_MACRO`、`_POISONED`、
`_RESERVED` といったフラグを提供します。

展開地点では `GetMacroArgumentInfo` が引数の個数と可変引数が省略されたかを報告し、
`GetMacroArgumentTokenStream` が各引数のトークンを返します。

## イベントの購読

1 つのコールバックが購読したすべてのイベントを受け取ります。マスクは気にかける
イベント種別から組み立てます:

```c
static NevercStatus NEVERC_CALL
on_event(NevercTaskHandle Task, const NevercPrepEvent *Event, void *UserData) {
  switch (Event->Kind) {
  case NEVERC_PREP_EVENT_INCLUSION_DIRECTIVE:
    /* Event->Payload.Include.Filename、.IsAngled、.File、.FilenameRange */
    break;
  case NEVERC_PREP_EVENT_MACRO_EXPANDS:
    /* Event->Payload.Macro.NameToken、.Definition、.Arguments、.Range */
    break;
  case NEVERC_PREP_EVENT_IFDEF:
    /* Event->Payload.Condition.Value は NOT_EVALUATED、FALSE、TRUE */
    break;
  default:
    break;
  }
  return neverc_status_ok();
}

NevercPrepObserverDescriptor Observer = {0};
Observer.Header = (NevercABITableHeader){sizeof(Observer),
                                         NEVERC_PREP_API_MAJOR,
                                         NEVERC_PREP_API_MINOR, 0};
Observer.Events = NEVERC_PREP_EVENT_MASK(NEVERC_PREP_EVENT_INCLUSION_DIRECTIVE) |
                  NEVERC_PREP_EVENT_MASK(NEVERC_PREP_EVENT_MACRO_EXPANDS) |
                  NEVERC_PREP_EVENT_MASK(NEVERC_PREP_EVENT_IFDEF);
Observer.Callback = on_event;
Observer.UserData = State;
Prep->RegisterEventObserver(Prep->Context, Task, &Observer);
```

`NEVERC_PREP_EVENT_MASK_ALL` はすべてを購読します。39 種別を、使用する
ペイロード共用体メンバごとに並べると次のとおりです:

| ペイロード | イベント |
|---|---|
| `File` | `FILE_CHANGED`、`LEXED_FILE_CHANGED`、`FILE_SKIPPED`、`END_OF_MAIN_FILE` |
| `Include` | `INCLUSION_DIRECTIVE`、`FILE_NOT_FOUND`、`HAS_INCLUDE` |
| `Text` | `IDENT`、`PRAGMA_DIRECTIVE`、`PRAGMA_COMMENT`、`PRAGMA_MARK`、`PRAGMA_DETECT_MISMATCH`、`PRAGMA_DEBUG`、`PRAGMA_MESSAGE`、`PRAGMA_DIAGNOSTIC_PUSH`、`PRAGMA_DIAGNOSTIC_POP`、`PRAGMA_DIAGNOSTIC`、`PRAGMA_WARNING`、`PRAGMA_WARNING_PUSH`、`PRAGMA_WARNING_POP`、`PRAGMA_EXEC_CHARSET_PUSH`、`PRAGMA_EXEC_CHARSET_POP`、`PRAGMA_ASSUME_NONNULL_BEGIN`、`PRAGMA_ASSUME_NONNULL_END` |
| `Macro` | `MACRO_EXPANDS`、`MACRO_DEFINED`、`MACRO_UNDEFINED`、`DEFINED` |
| `Condition` | `IF`、`ELIF`、`IFDEF`、`ELIFDEF`、`ELIFDEF_SKIPPED`、`IFNDEF`、`ELIFNDEF`、`ELIFNDEF_SKIPPED`、`ELSE`、`ENDIF`、`SOURCE_RANGE_SKIPPED` |

`NevercPrepFileEvent.Reason` は `NEVERC_PREP_FILE_ENTER`、`EXIT`、
`SYSTEM_HEADER_PRAGMA`、`RENAME` を区別します。イベントは読み取り専用です。
レコードとその中のすべてのビューはコールバックの間だけ借用され、イベントで公開
されたハンドルは外側のタスクスコープへ引き上げられます。

## include のリダイレクト

```c
NevercPrepIncludePhaseInput In = {0};
In.Header = (NevercABITableHeader){sizeof(In), NEVERC_PREP_API_MAJOR,
                                   NEVERC_PREP_API_MINOR, 0};
Prep->GetIncludePhaseInput(Prep->Context, Frame, Frame->Input, &In);

NevercPrepIncludePhaseOutput Out = {0};
Out.Header = In.Header;
if (view_equals(In.Filename, "legacy.h")) {
  Out.Action    = NEVERC_PREP_INCLUDE_REDIRECT;
  Out.Filename  = SV("modern.h");
  Out.IsAngled  = NEVERC_FALSE;
} else {
  Out.Action = NEVERC_PREP_INCLUDE_CONTINUE;
}

NevercArtifactHandle Output;
Prep->CreateIncludePhaseOutput(Prep->Context, Frame, Continuation, &Out,
                               &Output);
```

アクションは `NEVERC_PREP_INCLUDE_CONTINUE`、`_SKIP`、`_REDIRECT` です。入力は
`IsImport` と `IsIncludeNext` も報告するので、`#import` と `#include_next` を
区別できます。

## マクロ展開の差し替え

マクロフェーズの入力は、行われようとしている操作 ——
`NEVERC_PREP_MACRO_DEFINE`、`_UNDEFINE`、`_EXPAND`、`_EXPAND_BUILTIN` —— を、
名前トークン、定義、引数、そしてプリプロセッサが使うはずだった置換トークンと共に
運びます。

```c
NevercPrepMacroPhaseOutput Out = {0};
Out.Header     = /* … */;
Out.Action     = NEVERC_PREP_MACRO_REPLACE_TOKENS;
Out.Tokens     = MyTokens;      /* const NevercTokenHandle * */
Out.TokenCount = MyTokenCount;
Prep->CreateMacroPhaseOutput(Prep->Context, Frame, Continuation, &Out, &Output);
```

`NEVERC_PREP_MACRO_CONTINUE` は組み込みの振る舞いを保ち、`_SUPPRESS` は何もない
ものへ展開します。

## トークンの構築

合成トークンはビルダから生まれます。ビルダはコミット前に種別、綴り、識別子の
組み合わせを検証します:

```c
NevercTokenBuilderHandle Builder;
Prep->CreateTokenBuilder(Prep->Context, Task, &Builder);
Prep->TokenBuilderSetLiteral(Prep->Context, Task, Builder,
                             NEVERC_TOKEN_NUMERIC_CONSTANT, SV("42"));
Prep->TokenBuilderSetLocation(Prep->Context, Task, Builder, Location);
Prep->TokenBuilderSetFlags(Prep->Context, Task, Builder,
                           NEVERC_TOKEN_FLAG_LEADING_SPACE);

NevercTokenHandle Token;
Prep->TokenBuilderCommit(Prep->Context, Task, Builder, &Token);
Prep->DestroyTokenBuilder(Prep->Context, Task, Builder);
```

区切り記号とキーワードには `TokenBuilderSetKind` を、識別子には
`TokenBuilderSetIdentifier` を使います。トークン種別の定数は
[`PluginPrepSchema.inc`] に由来します。

ストリーム全体を扱うとき —— `neverc.prep.build_token_stream` フェーズ —— は、
ストリームビルダに積み上げて一度にコミットします:

```c
NevercTokenStreamBuilderHandle Stream;
Prep->CreateTokenStreamBuilder(Prep->Context, Task, &Stream);
Prep->TokenStreamBuilderAppend(Prep->Context, Task, Stream, Tokens, Count);
Prep->TokenStreamBuilderCommit(Prep->Context, Frame, Stream, &Output);
Prep->DestroyTokenStreamBuilder(Prep->Context, Task, Stream);
```

フェーズ入力 `NevercPrepTokenStreamPhaseInput` は開始位置と終了位置、そして出力が
守るべき `MaximumTokenCount` を与えます。

## pragma と機能問い合わせ

pragma フェーズの入力は、導入子（`NEVERC_PREP_PRAGMA_HASH`、`_Pragma` に対する
`_OPERATOR`、`__pragma` に対する `_MS`）、名前空間と名前、そして引数トークンを
報告します。出力アクションは `NEVERC_PREP_PRAGMA_CONTINUE`、`_HANDLED`、
`_REPLACE_TOKENS` です。

機能問い合わせは `NEVERC_PREP_QUERY_HAS_FEATURE` などを通じて `__has_feature`、
`__has_extension`、`__has_builtin`、`__has_include`、`__has_include_next` を
カバーします。入力は名前とコンパイラが算出した `BuiltinValue` を運び、出力は継続
するか差し替えるかのどちらかです:

```c
NevercPrepFeatureQueryPhaseOutput Out = {0};
Out.Header = /* … */;
Out.Action = NEVERC_PREP_QUERY_REPLACE;
Out.Value  = NEVERC_TRUE;
Prep->CreateFeatureQueryPhaseOutput(Prep->Context, Frame, Continuation, &Out,
                                    &Output);
```

## 規則

- イベントレコード、文字列ビュー、整数配列はコールバックの間だけ借用されます。
  イベントで公開されたハンドルはタスクが終わるまで生きています。
- すべてのビルダには対応する `Destroy*` 呼び出しが必要です。エラー経路でも同じ
  です。
- `Create<Kind>PhaseOutput` の呼び出しには、それが属するフェーズの continuation
  が要ります。別のフェーズの continuation を使うと
  `NEVERC_STATUS_WRONG_SCOPE` が返ります。`TokenStreamBuilderCommit` は
  continuation ではなく `build_token_stream` フェーズの `Frame` を取ります。
- 処理するイベントだけを購読してください。マスクこそが絞り弁です ——
  `NEVERC_PREP_EVENT_MASK_ALL` を受け取って C 側で絞り込むプラグインは、すべての
  コールバック分の代価を払うことになります。
- プリプロセッサのコールバックは、プリプロセッサが処理の途中にある間、タスク
  スレッド上で走ります。そこからプリプロセッサへ再入してはいけません。
- 必須ポインタが欠けていれば `NEVERC_STATUS_INVALID_ARGUMENT` を返し、例外を境界
  の外へ出してはいけません。

規範的な宣言は [`PluginPrep.h`] と [`Schema/PluginPrepSchema.inc`] を、トークン種別の
スキーマは [`Schema/PrepSchema.json`] を、6 つのプリプロセッサフェーズとその
ポリシーは [`Schema/PhaseSchema.json`] を参照してください。

<!-- reference links -->
[`PluginPrep.h`]: ../../neverc/include/neverc/Plugin/PluginPrep.h
[`PluginPrepSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginPrepSchema.inc
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginPrepSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginPrepSchema.inc
[`Schema/PrepSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PrepSchema.json
