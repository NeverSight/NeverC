**言語**: [English](target-mc-object.md) | [简体中文](target-mc-object.zh-CN.md) | [繁體中文](target-mc-object.zh-TW.md) | [日本語](target-mc-object.ja.md) | [한국어](target-mc-object.ko.md) | [Français](target-mc-object.fr.md) | [Deutsch](target-mc-object.de.md) | [Español](target-mc-object.es.md) | [Italiano](target-mc-object.it.md) | [Русский](target-mc-object.ru.md) | [العربية](target-mc-object.ar.md)

[← NeverC プラグイン ABI](README.ja.md)

# NeverC プラグイン ターゲット・MC・アセンブリ・オブジェクト API

バックエンドは 4 つのヘッダーと 29 のフェーズです。[`PluginTarget.h`] はターゲット
とコード生成の経路を記述します。[`PluginMC.h`] は機械語を構築し観測します。アセン
ブリの解析と出力も同じヘッダーにあります。[`PluginObject.h`] は再配置可能ファイル
を正規化されたグラフに変換し、また元に戻します。

これらを合わせると、プラグインはターゲットを追加し、低位化ステップの 1 つまたは
全部を差し替え、命令が発行される様子をすべて監視し、アセンブリ方言を定義し、オブ
ジェクトファイルを書き換えられます——しかもすべて、LLVM の `MCInst`、`MCSection`、
`object::ObjectFile` を一切露出しない純粋な C ABI を通してです。

## インターフェース

```c
#include "neverc/Plugin/PluginTarget.h"
#include "neverc/Plugin/PluginMC.h"
#include "neverc/Plugin/PluginObject.h"   /* includes both of the above */
```

| インターフェース | テーブル | スロット | 目的 |
|---|---|--:|---|
| `NEVERC_INTERFACE_TARGET_*` | `NevercTargetAPI` | 2 | `RegisterTarget`、`RegisterCodeGenEdge` |
| `NEVERC_INTERFACE_TARGET_ABI_*` | `NevercTargetABIAPI` | 1 | `RegisterABI` |
| `NEVERC_INTERFACE_CALLING_CONVENTION_*` | `NevercCallingConventionAPI` | 1 | `RegisterCallingConvention` |
| `NEVERC_INTERFACE_MC_*` | `NevercMCAPI` | 53 | `MCUnit` の読み取りと変更、エンコーダー・デコーダー・バックエンドの登録 |
| `NEVERC_INTERFACE_MC_EMISSION_*` | `NevercMCEmissionAPI` | 7 | 発行イベントとレイアウトのスナップショット |
| `NEVERC_INTERFACE_MC_PROVIDER_*` | `NevercMCProviderAPI` | 4 | MIR → MC の置き換え |
| `NEVERC_INTERFACE_ASSEMBLY_PROVIDER_*` | `NevercAssemblyProviderAPI` | 8 | アセンブリパーサーまたはプリンターの置き換え |
| `NEVERC_INTERFACE_OBJECT_*` | `NevercObjectAPI` | 34 | ObjectGraph の読み取りと変更 |
| `NEVERC_INTERFACE_OBJECT_FORMAT_*` | `NevercObjectFormatAPI` | 1 | `RegisterFormat` |
| `NEVERC_INTERFACE_OBJECT_PHASE_*` | `NevercObjectPhaseAPI` | 2 | `GetGraph`、`GetImage` |

## 2 つの互換性ティア

ここから先のすべてを支配するのがこの規則です。

**STABLE**、ハードコードしても安全なもの: ターゲット非依存のディスクリプター、
フェーズ ID、アーティファクト ID、MC と ObjectGraph のコンテナー、出力トランザク
ション、そしてすべてのコールバック契約。

**LOCKSTEP**、確認なしでは危険なもの: ターゲット固有のオペコード、レジスター、オ
ペランド、フィックスアップ、再配置、呼び出し規約のスキーマ。これらの数値は、ある
特定のスキーマ改訂に対してのみ意味を持ちます。

LOCKSTEP の値が現れる場所には必ず、その隣にスキーマダイジェストがあります。値を読
む前に比較してください:

```c
if (!string_equal(Target.SchemaDigest, MY_COMPILED_SCHEMA_DIGEST))
  return fail(NEVERC_STATUS_ABI_MISMATCH);
```

NeverC もプロバイダーを呼ぶ前に不一致のスキーマを拒否するので、このチェックは二重
の備えです——とはいえ、これを飛ばして生のオペコードを読んでしまうプラグインは、黙
って命令を読み違えます。

## フェーズ

29 個、4 つのドメインに分かれています。

### `codegen` — 経路選択（4）

| フェーズ | ポリシー |
|---|---|
| `neverc.codegen.ir_to_mir` | OBSERVABLE、INTERCEPTABLE、REPLACEABLE |
| `neverc.codegen.mir_to_mc` | OBSERVABLE、INTERCEPTABLE、REPLACEABLE |
| `neverc.codegen.coarse_lower` | OBSERVABLE、INTERCEPTABLE、REPLACEABLE |
| `neverc.codegen.product_verify` | OBSERVABLE、**SEALED** |

### `mc` — 機械語（13）

`neverc.mc.encode`、`neverc.mc.decode`、`neverc.mc.layout` は OBSERVABLE、
INTERCEPTABLE、REPLACEABLE です。

`neverc.mc.emission.pre_instruction` は REPLACEABLE でもある唯一の発行イベントで
——命令を差し替えるのはそこです。残り 9 つ（`unit_begin`、`unit_end`、
`section_change`、`post_instruction`、`post_encode`、`fixup`、
`relaxation_round`、`pre_layout`、`post_layout`）は観測専用です。

### `assembly`（4）

`neverc.assembly.parse` と `neverc.assembly.print` は REPLACEABLE です。
`neverc.assembly.final_verify` と `neverc.assembly.commit` は SEALED です。

### `object`（8）

`neverc.object.probe`、`read`、`write`、`pre_write`、`post_layout` は
REPLACEABLE、`neverc.object.post_write` は INTERCEPTABLE のみ、
`neverc.object.final_verify` と `neverc.object.commit` は SEALED です。

## ターゲットの登録

`NevercTargetDescriptor` はこの ABI で最大のディスクリプターです。フロントエンド
とバックエンドが知る必要のあるものをすべて運ぶからです:

```c
typedef struct NevercTargetDescriptor {
  NevercABITableHeader Header;
  NevercTargetID TargetID;
  NevercStringView CanonicalName;
  NevercStringArrayView Aliases;
  NevercStructArrayView TripleMatchers;    /* NevercTargetTripleMatcher[] */
  NevercTargetABIID DefaultABI;
  NevercCallingConventionID DefaultCallingConvention;
  NevercInterfaceID MCSchemaID;
  NevercInterfaceID DefaultObjectFormatID;
  NevercTargetMachineDescriptor Machine;
  NevercStructArrayView Macros;            /* predefined macros           */
  NevercStructArrayView Builtins;          /* target builtins + lowering  */
  NevercStructArrayView Registers;         /* inline-asm register names   */
  NevercStructArrayView Constraints;       /* inline-asm constraints      */
  NevercStringView Clobbers;
  uint64_t Flags;
  NevercTargetValidateCPUFn ValidateCPU;
  NevercTargetCanonicalizeCPUFn CanonicalizeCPU;
  NevercTargetListCPUsFn ListCPUs;
  NevercTargetResolveFeaturesFn ResolveFeatures;
  NevercCreateTargetMachineFn CreateTargetMachine;
  NevercDestroyTargetMachineFn DestroyTargetMachine;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercTargetDescriptor;
```

`TripleMatchers` はターゲットがいつ選ばれるかを決めます。各マッチャーはアーキテク
チャー、ベンダー、OS、環境を指定し、さらに組み込みターゲットとの同点を破るための
`Priority` を持ちます。

`Machine` は `NevercTargetMachineDescriptor` です——データレイアウト、既定および
チューニング用 CPU、機能テーブル、対応 ABI・呼び出し規約・オブジェクト形式、アド
レス空間、再配置モデルとコードモデル（既定値と対応マスクの両方）、例外モデル
（`NONE`、`DWARF`、`SJLJ`、`SEH`、`WASM`）、巻き戻しモデル、エンディアン、
pointer/int/long/long long の幅、スタックアライメント、アトミックとベクターの最大
幅、`va_list` の種類、実行レベル（`USER`、`KERNEL`、`HYPERVISOR`、`FIRMWARE`）、
そして TLS 対応。

ターゲット組み込み関数はそれぞれ独自の低位化コールバックを持ち、そこには生きた IR
ビルダーが渡されます:

```c
static NevercStatus NEVERC_CALL
lower_builtin(void *UserData,
              const NevercTargetBuiltinLoweringInvocation *In,
              NevercIRValueHandle *OutResult) {
  /* In->Core, In->Builder, In->Mutation, In->IRBuilder,
     In->ResultType, In->Arguments, In->ArgumentCount */
  return In->Builder->BuildCall(/* … */);
}
```

## ABI と呼び出し規約

ABI は関数シグネチャを分類します:

```c
static NevercStatus NEVERC_CALL
classify(void *UserData, const NevercABIFunctionQuery *Query,
         NevercABIArgumentClassification *ReturnValue,
         NevercABIArgumentClassificationArray *Arguments) {
  ReturnValue->Kind = NEVERC_ABI_ARGUMENT_DIRECT;
  for (uint64_t I = 0; I != Arguments->Count; ++I) {
    NevercABIArgumentClassification *A = &Arguments->Data[I];
    A->Kind  = NEVERC_ABI_ARGUMENT_INDIRECT;
    A->Flags = NEVERC_ABI_ARGUMENT_BYVAL;
  }
  return neverc_status_ok();
}
```

引数の種別は `DIRECT`、`EXTEND`、`INDIRECT`、`IGNORE`、`EXPAND`、
`INDIRECT_ALIASED`、`COERCE_AND_EXPAND`。フラグは `BYVAL`、`REALIGN`、`INREG`、
`SRET_AFTER_THIS`、`CAN_BE_FLATTENED`、`SIGN_EXTEND`、`PADDING_INREG`。強制変換は
`NONE`、`INTEGER`、`FLOAT`、`POINTER` のいずれかで、`COERCE_AND_EXPAND` は
`NevercABICoercionElement` の配列を提供します。

呼び出し規約はもう一段下のレベルで、実際の配置場所を割り当てます:

```c
static NevercStatus NEVERC_CALL
plan(void *UserData, const NevercCallingConventionQuery *Query,
     NevercCallingConventionPlan *Plan) {
  /* Query->TargetID, ->CallingConventionID, ->SchemaDigest, ->Function */
  /* Fill Plan->ReturnLocations and Plan->ArgumentLocations with
     NevercCallingConventionLocation records: REGISTER or STACK,
     ValueIndex, PieceOffset, Size, Alignment, RegisterNumber,
     StackOffset, and INDIRECT / BYVAL flags.                       */
  Plan->CalleeSavedRegisters = MySavedRegisters;
  Plan->StackAlignment       = 16;
  return neverc_status_ok();
}
```

`Query->SchemaDigest` は LOCKSTEP の値です——`RegisterNumber` は、それが指し示すス
キーマに対してのみ意味を持ちます。完全な実例は
[カスタム呼び出し規約](custom-callconv/README.ja.md#マテリアライズされた-plan) と
[`pluginsdk/examples/CustomCallConvPlugin.c`] を参照してください。

## コード生成の経路

経路は正規の `NevercTargetKey` から選ばれます: ターゲット ID、トリプルの各部分、
CPU、チューニング CPU、機能、ABI、呼び出し規約、オブジェクト形式、再配置モデル、
コードモデル、実行レベル、ポインター幅、エンディアン、スキーマダイジェスト。自分
が担える辺を登録します:

```c
NevercCodeGenEdgeDescriptor Edge = {0};
Edge.Header          = /* … */;
Edge.EdgeID          = MyEdgeID;
Edge.CanonicalName   = SV("com.example.mir-to-mc");
Edge.TargetID        = MyTargetID;
Edge.InputKind       = NEVERC_CODEGEN_PRODUCT_MIR;
Edge.OutputKind      = NEVERC_CODEGEN_PRODUCT_MC;
Edge.CompatibilityKey = SV("…");
Edge.ProviderID      = SV("com.example.backend");
Target->RegisterCodeGenEdge(Target->Context, RegistrarContext, &Edge);
```

成果物の種別は `IR`、`MIR`、`MC`、`ASSEMBLY`、`OBJECT_GRAPH`、`OBJECT_IMAGE`、
`CUSTOM` です。細粒度の経路は `IR → MIR → MC → ObjectGraph → ObjectImage` です。

`NEVERC_CODEGEN_EDGE_COARSE` を設定して `CoarseLower` を与えると、
`IR → ObjectImage` の全区間を一手に置き換えます:

```c
static NevercStatus NEVERC_CALL
coarse_lower(void *UserData, NevercTaskHandle Task,
             const NevercCodeGenRequest *Request,
             NevercCodeGenProductCandidate *OutCandidate) {
  /* Request->Target, ->Input, ->InputKind, ->OutputKind,
     ->OptimizationLevel, ->HasFinalIRProof                */
  OutCandidate->Kind      = NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE;
  OutCandidate->Artifact  = MyImage;
  OutCandidate->ProductID = MyProductID;
  return neverc_status_ok();
}
```

粗粒度の経路でも `neverc.codegen.product_verify` とトランザクショナルな出力コミッ
トは通ります。`VerifyProduct` は、ホストがあなたに果たしていることを期待する義務
——`VERIFY_FINAL_IR`、`VERIFY_TARGET_KEY`、`VERIFY_PRODUCT_KIND`、
`VERIFY_PRODUCT_ID`、`VERIFY_STRUCTURE`——とともに呼ばれるので、プロバイダーが近道
を選んでゲートをこっそり飛ばすことはできません。

## MC の構築

`MCUnit` はセクション、シンボル、式、フラグメント、命令、オペランド、フィックスア
ップを保持します。読み取りは first/next の反復です:

```c
NevercMCUnitInfo Unit = {0};
Unit.Header = /* … */;
MC->GetUnitInfo(MC->Context, Task, UnitHandle, &Unit);

NevercMCSectionHandle Section;
MC->GetFirstSection(MC->Context, Task, UnitHandle, &Section);
while (!neverc_handle_is_null(Section)) {
  NevercMCFragmentHandle Fragment;
  MC->GetFirstFragment(MC->Context, Task, Section, &Fragment);
  /* … */
  MC->GetNextSection(MC->Context, Task, Section, &Section);
}
```

変更はトランザクショナルで、他の場所と同じです:

```c
NevercMCMutationHandle Mutation;
MC->BeginMutation(MC->Context, Task, Unit, &Mutation);
MC->CreateSection(MC->Context, Task, Mutation, &SectionDescriptor, &Section);
MC->CreateSymbol(MC->Context, Task, Mutation, &SymbolDescriptor, &Symbol);
MC->AppendInstruction(MC->Context, Task, Mutation, Section, &Instruction);
Status = MC->CommitMutation(MC->Context, Task, Mutation);
if (Status.Code != NEVERC_STATUS_OK)
  MC->AbandonMutation(MC->Context, Task, Mutation);
```

ハンドルはタスクスコープで世代チェックされるため、放棄された変更に由来するハンド
ルは再利用されるのではなく拒否されます。

セクションフラグは `ALLOCATED`、`EXECUTABLE`、`WRITABLE`、`MERGEABLE`、`DEBUG`。
シンボルの束縛は `LOCAL`、`GLOBAL`、`WEAK`、型は `NONE`、`FUNCTION`、`OBJECT`、
`SECTION`、`TLS`、定義は `UNDEFINED`、`SECTION`、`ABSOLUTE`、`COMMON` です。式は
単項の `PLUS`、`MINUS`、`NOT` と、二項の `ADD`、`SUBTRACT`、`MULTIPLY`、
`DIVIDE`、`AND`、`OR`、`XOR`、`SHIFT_LEFT`、`SHIFT_RIGHT` に対応します。配置をホ
ストに任せたいところでは `NEVERC_MC_AUTOMATIC_OFFSET` を渡してください。

`RegisterSchema` はターゲットの MC スキーマを公開し、`GetSchemaToken` /
`GetSchemaTokenInfo` は名前と LOCKSTEP トークンを相互に解決します。

## 発行の観測

発行ストリームは、各 `neverc.mc.emission.*` フェーズに対応する 10 種類の
イベントを順に報告します。ABI はさらに
`NEVERC_MC_EMISSION_PRE_OBJECT_WRITE` を予約していますが、オブジェクト書き込み
自体は別フェーズ `neverc.object.pre_write` です。オブザーバーとして購読し、イ
ベントを読みます:

```c
NevercMCEmissionEventInfo Event = {0};
Event.Header = /* … */;
Emission->GetEvent(Emission->Context, Frame, Frame->Input, &Event);
/* Event.Kind, Event.Flags */
```

`Flags` はイベントのどの部分が埋まっているかを示します: `HAS_SECTION`、
`HAS_INSTRUCTION`、`HAS_ENCODING`、`HAS_FIXUP`、`HAS_LAYOUT`、
`CAN_REPLACE_INSTRUCTION`。対応するフィールドを読む前にフラグを確認してください
——まだエンコーディングを持たないイベントは、尋ねたからといって持つようにはなりま
せん。

`HAS_LAYOUT` が立てば、`GetLayoutSection`、`GetLayoutFragment`、
`GetLayoutSymbol`、`GetLayoutFixup` がアドレスとサイズを返します。

`pre_instruction` で、かつ `CAN_REPLACE_INSTRUCTION` が立っているときに限り、差し
替えができます:

```c
const NevercMCAPI *MC;
NevercMCUnitHandle Unit;
NevercMCInstHandle Instruction;
Emission->BeginInstructionReplacement(Emission->Context, Frame, Continuation,
                                       &MC, &Unit, &Instruction);
/* mutate Instruction through MC->BeginMutation / … / CommitMutation */
Emission->PublishInstructionReplacement(Emission->Context, Frame, Continuation,
                                         &OutResult->Output);
```

[`pluginsdk/examples/MCObserverPlugin.c`] はこの読み取り専用版です。

## エンコーダー、デコーダー、レイアウト

3 つの登録が機械語バックエンドを拡張します。いずれもターゲットとスキーマダイジェ
ストをキーとします:

```c
MC->RegisterEncoder(MC->Context, RegistrarContext, &EncoderDescriptor);
MC->RegisterDecoder(MC->Context, RegistrarContext, &DecoderDescriptor);
MC->RegisterAsmBackend(MC->Context, RegistrarContext, &BackendDescriptor);
```

エンコーダーはバッファーを返すのではなくシンクを通して書き込むので、所有権はホス
ト側に留まります:

```c
Sink->WriteBytes(Sink->Context, Bytes);
Sink->AddFixup(Sink->Context, &Fixup);
```

デコーダーは `NEVERC_MC_DECODE_SUCCESS`、`_SOFT_FAIL`、`_UNKNOWN`、`_FAIL` のいず
れかを報告します。フィックスアップの種別は `NevercMCFixupKindInfo` を通じて
`PC_RELATIVE`、`SIGNED`、`RELAXABLE`、`TARGET` のフラグで自己記述します。

asm バックエンドが緩和（relaxation）を担います。レイアウトは証明ダイジェストを発
行し、**レイアウト後のいかなる変更もその証明を無効化**して、オブジェクトを書き出
す前に再レイアウトを強制します——リンクグラフが使うのと同じ世代チェックの流儀で
す。

## アセンブリ

パーサープロバイダーはソースバイト列を消費して `MCUnit` を公開します:

```c
NevercAssemblyParseInputInfo In = {0};
In.Header = /* … */;
Asm->GetParseInput(Asm->Context, Frame, Frame->Input, &In);

NevercAssemblyTokenInfo Token = {0};
Asm->PeekSourceToken(Asm->Context, Frame, In.Source.Cursor, &Token);
Asm->AdvanceSourceToken(Asm->Context, Frame, In.Source.Cursor);

const NevercMCAPI *MC;
NevercMCUnitHandle Unit;
Asm->GetParseMCBuilder(Asm->Context, Frame, &MC, &Unit);
/* … build into Unit … */
Asm->PublishParsedMCUnit(Asm->Context, Frame, &Output);
```

ソースは `NEVERC_ASSEMBLY_SOURCE_BUFFER` か
`NEVERC_ASSEMBLY_SOURCE_RENDERED_TOKENS` のいずれかです。前処理付きアセンブリ
（`.S`）はまず通常のフロントエンドプリプロセッサーを通り、レンダリング済みトーク
ンとして届きます。素のアセンブリ（`.s`）はバッファーとしてそのままパーサーに入り
ます。

プリンターは逆方向です——`GetPrintInput`、続いて与えられた出力トランザクションへの
`WritePrintOutput`、そして `PublishAssemblyOutput`。それ以外の場所への書き込みは
サポートされません。解析／出力の検証とホストのコミットゲートはバイトが可視になる
前に走るので、出力に失敗しても中途半端なファイルは残りません。

## オブジェクトグラフ

`NevercObjectAPI` は再配置可能ファイルをセクション、シンボル、再配置、COMDAT に正
規化します。組み込みアダプターは ELF、COFF、Mach-O を網羅し、`RegisterFormat` で
さらに追加できます。

```c
NevercObjectGraphInfo Info = {0};
Info.Header = /* … */;
Object->GetGraphInfo(Object->Context, Task, Graph, &Info);
/* Info.Target, .ObjectSchemaDigest, .Generation, .SectionCount,
   .SymbolCount, .RelocationCount, .ComdatCount, .HasLayoutProof */

NevercObjectSymbolHandle Symbol;
Object->GetFirstSymbol(Object->Context, Task, Graph, &Symbol);
while (!neverc_handle_is_null(Symbol)) {
  NevercObjectSymbolInfo SymInfo = {0};
  SymInfo.Header = /* … */;
  Object->GetSymbolInfo(Object->Context, Task, Symbol, &SymInfo);
  Object->GetNextSymbol(Object->Context, Task, Symbol, &Symbol);
}
```

変更は 4 種類のエンティティすべてについて create/replace/move/erase のパターンに
従い、`BeginMutation` … `CommitMutation` / `AbandonMutation` の中でステージされま
す。

セクションフラグは `ALLOCATED`、`EXECUTABLE`、`WRITABLE`、`MERGEABLE`、
`STRINGS`、`TLS`、`DEBUG`、`UNWIND`、`DISCARDABLE`、`RETAIN`。再配置のターゲット
は `SYMBOL`、`SECTION`、`ABSOLUTE`、`FORMAT_EXTENSION` のいずれかです。

どのディスクリプターにも `ExtensionOwner` / `ExtensionVersion` / `Extension` の三
点セットがあります。正規化グラフに対応するフィールドがないデータを形式が保持する
のはこの仕組みによってです——そのバイト列はエンティティとともに運ばれ、書き出し時
に戻ってくるので、往復で失われることがありません。

### 形式の登録

```c
NevercObjectFormatDescriptor Format = {0};
Format.Header           = /* … */;
Format.FormatID         = MyFormatID;
Format.CanonicalName    = SV("com.example.myfmt");
Format.SupportedTargets = MyTargets;
Format.DefaultExtension = SV(".mof");
Format.Flags            = NEVERC_OBJECT_FORMAT_CAN_PROBE |
                          NEVERC_OBJECT_FORMAT_CAN_READ  |
                          NEVERC_OBJECT_FORMAT_CAN_WRITE;
Format.Probe            = probe;
Format.Reader           = read;
Format.Writer           = write;
ObjectFormat->RegisterFormat(ObjectFormat->Context, RegistrarContext,
                             &Format);
```

`Probe` は 0 から `NEVERC_OBJECT_PROBE_MAX_CONFIDENCE`（1000）までの
`Confidence`、認識した `NevercObjectArtifactKind`（`RELOCATABLE`、`ARCHIVE`、
`EXECUTABLE_IMAGE`、`SHARED_IMAGE`、`UNIVERSAL_BINARY`）、そして確信を得るために
必要だったバイト数 `ConsumedMinimum`（上限は
`NEVERC_OBJECT_PROBE_MAX_CONSUMED_MINIMUM`、65536）を報告します。最も確信度の高い
ものが選ばれます。

`Reader` にはグラフと開いた変更が渡され、それを埋めます。`Writer` にはグラフ、そ
のレイアウト証明、そして境界付きバイナリビルダーが渡されます。

### 書き出しパイプライン

1. 探査してバイト列を ObjectGraph に読み込む;
2. `object.pre_write` のグラフインターセプターを実行する;
3. レイアウトし、それから `object.post_layout` を実行する（変更後は再レイアウト）;
4. 境界付きの候補イメージを書く;
5. `object.post_write` のバイナリインターセプターを実行する;
6. 封印された `object.final_verify` とアトミックな `object.commit` を実行する。

イメージの状態は `CANDIDATE` → `VERIFIED` → `COMMITTED`、あるいは `ABORTED` /
`FAILED_PARTIAL` と遷移します。

オブザーバーには読み取り専用のブリッジが渡され、オブザーバーから変更を試みると
`NEVERC_STATUS_POLICY_VIOLATION` で拒否されます。ライターと post-write インターセ
プターに渡るのは境界付きの `NevercMutableBinaryAPI` ビルダーだけです——`Reserve`、
`Write`、`WriteAt`、`Tell`、`ReadAt`、`Insert`、`Append`、`Resize`。オーバーフロ
ー、コールバックの失敗、検証の失敗はステージングを中止させるので、失敗が中途半端
なファイルをディスクに残すことはありません。

[`pluginsdk/examples/ObjectRewritePlugin.c`] は完全なトランザクショナル書き換えの例
です。

## ルール

- LOCKSTEP のオペコード、レジスター、オペランド、フィックスアップ、再配置、呼び出
  し規約の値を使う前に、スキーマダイジェストを比較する。
- 可変状態はホストが提供する process、session、task の状態に置く。
- コールバックから戻った後にタスクハンドルや借用ビューをキャッシュしない。
- インターセプターの継続は、コールバックスレッド上で高々 1 回だけ呼ぶ。
- すべての `BeginMutation` はちょうど 1 回のコミットまたは放棄に到達する。
- レイアウト済みの MCUnit や ObjectGraph を変更したら再レイアウトする。古いレイア
  ウト証明は失効しており、ホストはそれを拒否する。
- イベントのフィールドを読む前に `NevercMCEmissionEventInfo.Flags` を確認し、命令
  の差し替えは `CAN_REPLACE_INSTRUCTION` が立っているときだけ行う。
- 出力は、与えられたトランザクションかバイトシンクを通してのみ書く。
- 失敗時は元の `NevercStatus` を返し、中途半端なものは一切公開しない。
- 真実であるうちで最も狭い並行性モデルと再入モデルを宣言する。
- `codegen.product_verify`、`assembly.final_verify`、`assembly.commit`、
  `object.final_verify`、`object.commit` は封印されている。観測のみ。

規範的な宣言は [`PluginTarget.h`]、[`PluginMC.h`]、[`PluginObject.h`]、
[`Schema/PhaseSchema.json`] を参照してください。それらが使うエンティティ・オペラ
ンド・fixup・セクションの種別は [`Schema/MCSchema.json`] と
[`Schema/ObjectSchema.json`] に由来し、そこから [`Schema/PluginMCSchema.inc`] と
[`Schema/PluginObjectSchema.inc`] が生成されます。これら安定フェーズそれぞれを肯
定・否定・置換・読み取り専用オブザーバー・封印ゲートの各テストへ対応付けたものは
[`coverage.json`] を参照してください。

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PluginMC.h`]: ../../neverc/include/neverc/Plugin/PluginMC.h
[`PluginObject.h`]: ../../neverc/include/neverc/Plugin/PluginObject.h
[`pluginsdk/examples/CustomCallConvPlugin.c`]: ../../pluginsdk/examples/CustomCallConvPlugin.c
[`pluginsdk/examples/MCObserverPlugin.c`]: ../../pluginsdk/examples/MCObserverPlugin.c
[`pluginsdk/examples/ObjectRewritePlugin.c`]: ../../pluginsdk/examples/ObjectRewritePlugin.c
[`PluginTarget.h`]: ../../neverc/include/neverc/Plugin/PluginTarget.h
[`Schema/MCSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/MCSchema.json
[`Schema/ObjectSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/ObjectSchema.json
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginMCSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginMCSchema.inc
[`Schema/PluginObjectSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginObjectSchema.inc
