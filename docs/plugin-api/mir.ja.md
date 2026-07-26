**言語**: [English](mir.md) | [简体中文](mir.zh-CN.md) | [繁體中文](mir.zh-TW.md) | [日本語](mir.ja.md) | [한국어](mir.ko.md) | [Français](mir.fr.md) | [Deutsch](mir.de.md) | [Español](mir.es.md) | [Italiano](mir.it.md) | [Русский](mir.ru.md) | [العربية](mir.ar.md)

[← NeverC プラグイン ABI](README.ja.md)

# NeverC プラグイン MIR API

[`PluginMIR.h`] は Machine IR を公開します。マシン関数、ブロック、命令、オペランド、
仮想レジスタと物理レジスタ、スタックフレーム、定数プール、ジャンプテーブル、
メモリオペランドです。プラグインは 9 つの安定したコード生成フックにパスを取り付け、
あるいは IR から MIR への低下処理をまるごと置き換えられます。

ここでは 2 つのスキーマが出会います。**汎用スキーマ** はターゲット非依存で、常に
利用できます。ターゲット固有のもの —— 実在のオペコード、レジスタ番号、レジスタ
クラス —— には交渉済みの **ターゲットスキーマ** が要り、それを必要とする値はすべて
`RequiresTargetSchema` フラグでそう申告します。

## インターフェイス

```c
#include "neverc/Plugin/PluginMIR.h"
```

| インターフェイス | テーブル | スロット | 目的 |
|---|---|--:|---|
| `NEVERC_INTERFACE_MIR_{HIGH,LOW}` | `NevercMIRAPI` | 89 | マシン関数の読み取りと変更 |
| `NEVERC_INTERFACE_MIR_ANALYSIS_{HIGH,LOW}` | `NevercMIRAnalysisAPI` | 11 | 生存区間、支配関係、ループ、レジスタ圧 |
| `NEVERC_INTERFACE_MIR_PASS_{HIGH,LOW}` | `NevercMIRPassAPI` | 1 | `RegisterPass` |
| `NEVERC_INTERFACE_MIR_PROVIDER_{HIGH,LOW}` | `NevercMIRProviderAPI` | 3 | IR → MIR の低下処理を置き換える |

4 つとも major 1 では `NEVERC_INTERFACE_STABLE` です。返ってきた `TableSize` を、
自分が使う最後のスロットのオフセットと突き合わせて確認し、新しいホストがその先に
付け足したものは無視してください。

## フェーズ

MIR フェーズは 10 個あり、うち 9 個がパスのフックです:

| フェーズ | タイミング |
|---|---|
| `neverc.mir.pass.post_isel` | 命令選択のあと |
| `neverc.mir.pass.post_legalize` | 合法化のあと |
| `neverc.mir.pass.pre_scheduler` | スケジューリングの前 |
| `neverc.mir.pass.post_scheduler` | スケジューリングのあと |
| `neverc.mir.pass.pre_regalloc` | レジスタ割り当ての前 |
| `neverc.mir.pass.post_regalloc` | レジスタ割り当てのあと |
| `neverc.mir.pass.post_prolog_epilog` | プロローグ／エピローグ挿入のあと |
| `neverc.mir.pass.preemit` | 発行の直前 |
| `neverc.mir.pass.final` | 最後のプラグインスロット |
| `neverc.mir.final_verify` | **封印された** ホストの `MachineVerifier` |

9 つのフックはすべて `OBSERVABLE | INTERCEPTABLE` です。どの解析が存在するかは
どこに取り付けるかで決まります。生存区間はレジスタ割り当ての前には存在せず、仮想
レジスタは割り当てのあとには消えています。

`neverc.mir.final_verify` は最後のプラグインスロットのあとに LLVM の
`MachineVerifier` を走らせます。どのプラグインもこれを無効化・置換・スキップでき
ません。

## スキーマ

[`Schema/PluginMIRSchema.inc`] は生成物で、[`PluginMIR.h`] が include します:

```c
#define NEVERC_MIR_SCHEMA_DIGEST          "6b523b20…"
#define NEVERC_MIR_ENTITY_COUNT           UINT32_C(4)
#define NEVERC_MIR_OPERAND_COUNT          UINT32_C(21)
#define NEVERC_MIR_GENERIC_OPCODE_COUNT   UINT32_C(266)
#define NEVERC_MIR_PROPERTY_COUNT         UINT32_C(11)
```

4 つの呼び出しが実行時にスキーマを記述します。いずれも正規名、背後の LLVM の値、
そしてターゲットスキーマが要るかどうかを持つ `NevercMIRSchemaEntry` を返します:

```c
NevercMIRSchemaEntry Entry = {0};
Entry.Header = /* … */;
MIR->GetGenericOpcodeInfo(MIR->Context, Opcode, &Entry);
/* Entry.StableID、.LLVMValue、.RequiresTargetSchema、.CanonicalName */
```

残りは `GetEntityInfo`、`GetOperandKindInfo`、`GetMachinePropertyInfo` です。
`GetSchemaDigest` は実際に使われている対応関係のダイジェストを返します ——
ターゲット固有の値を信じる前に `NEVERC_MIR_SCHEMA_DIGEST` と突き合わせてください。

## MIR の読み取り

走査はカーソル方式ではなく双方向リンク方式です:

```c
NevercMachineBasicBlockHandle Block;
MIR->GetFirstBasicBlock(MIR->Context, Task, Function, &Block);

while (!neverc_handle_is_null(Block)) {
  NevercMachineInstrHandle Instruction;
  MIR->GetFirstInstruction(MIR->Context, Task, Block, &Instruction);

  while (!neverc_handle_is_null(Instruction)) {
    NevercMIRInstructionInfo Info = {0};
    Info.Header = (NevercABITableHeader){sizeof(Info), NEVERC_MIR_API_MAJOR,
                                         NEVERC_MIR_API_MINOR, 0};
    MIR->GetInstructionInfo(MIR->Context, Task, Instruction, &Info);
    /* Info.StableOpcode、.TargetOpcode、.RequiresTargetSchema、
       .IsBranch、.IsCall、.IsReturn、.IsTerminator、.IsBarrier、
       .IsInlineAssembly、.IsDebugInstruction、.IsPseudo、.IsBundle、
       .Flags、.OperandCount、.MemoryOperandCount                    */
    MIR->GetNextInstruction(MIR->Context, Task, Instruction, &Instruction);
  }
  MIR->GetNextBasicBlock(MIR->Context, Task, Block, &Block);
}
```

`CollectBasicBlocks` と `CollectInstructions` は代わりに上限付き配列を埋め、
`GetLastBasicBlock` / `GetPreviousInstruction` は後ろ向きに歩きます。CFG の
問い合わせは `GetSuccessorCount` / `GetSuccessor`（後者は分岐確率を分子／分母の組
として運ぶ `NevercMIRCFGEdge` を返します）、`GetPredecessorCount` /
`GetPredecessor`、`GetLiveInCount` / `GetLiveIn` です。

命令フラグは `FRAME_SETUP` と `FRAME_DESTROY` から fast-math 群を経て
`NO_MERGE`、`UNPREDICTABLE`、`NO_CONVERGENT` に至る 18 ビットです。

## オペランド

21 種すべてのオペランド種別が 1 つのタグ付き共用体で返ります:

```c
NevercMIROperandValue Value = {0};
Value.Header = /* … */;
MIR->GetOperandValue(MIR->Context, Task, Operand, &Value);

switch (Value.Kind) {
case NEVERC_MIR_OPERAND_REGISTER:
  /* Value.Payload.Register.Number、.SubRegister、.Flags、.IsPhysical */
  break;
case NEVERC_MIR_OPERAND_IMMEDIATE:
  /* Value.Payload.Immediate */
  break;
case NEVERC_MIR_OPERAND_MACHINE_BASIC_BLOCK:
  /* Value.Payload.BasicBlock */
  break;
case NEVERC_MIR_OPERAND_GLOBAL_ADDRESS:
  /* Value.Payload.SymbolOffset.Symbol、.Offset */
  break;
}
```

種別は `REGISTER`、`IMMEDIATE`、`C_IMMEDIATE`、`FP_IMMEDIATE`、
`MACHINE_BASIC_BLOCK`、`FRAME_INDEX`、`CONSTANT_POOL_INDEX`、`TARGET_INDEX`、
`JUMP_TABLE_INDEX`、`EXTERNAL_SYMBOL`、`GLOBAL_ADDRESS`、`BLOCK_ADDRESS`、
`REGISTER_MASK`、`REGISTER_LIVE_OUT`、`METADATA`、`MC_SYMBOL`、`CFI_INDEX`、
`INTRINSIC_ID`、`PREDICATE`、`SHUFFLE_MASK`、`DBG_INSTR_REF` です。

レジスタオペランドのフラグは `DEF`、`IMPLICIT`、`KILL`、`DEAD`、`UNDEF`、
`EARLY_CLOBBER`、`RENAMABLE`、`INTERNAL_READ`、`DEBUG`。浮動小数点即値は
`NevercMIRWordView` として届きます —— リトルエンディアンのワードにビット幅と、
`IEEE_HALF` から `PPC_DOUBLE_DOUBLE` までの 7 つの浮動小数点意味論のひとつ ——
ので、ホストの浮動小数点型は一切関与しません。

## レジスタ

仮想レジスタは低レベル型と割り当てで記述されます:

```c
NevercMIRVirtualRegisterDesc Desc = {0};
Desc.Header             = /* … */;
Desc.AssignmentKind     = NEVERC_MIR_REG_ASSIGNMENT_CLASS;
Desc.TargetID           = RegisterClassID;   /* ターゲットスキーマが必要 */
Desc.Type.Kind          = NEVERC_MIR_LLT_SCALAR;
Desc.Type.ScalarSizeInBits = 32;

uint32_t Register = 0;
MIR->CreateVirtualRegister(MIR->Context, Task, Mutation, &Desc, &Register);
```

割り当て種別は `NONE`、`GENERIC`、`CLASS`、`BANK`。低レベル型の種別は
`INVALID`、`SCALAR`、`POINTER`、`VECTOR`、`POINTER_VECTOR` で、スケーラブル
ベクタには `IsScalable` があります。

def-use の問い合わせは `GetRegisterDefCount` / `GetRegisterDef` と
`GetRegisterUseCount` / `GetRegisterUse`。`ReplaceRegister` は 1 回のステージング
操作ですべての出現を書き換えます。関数レベルの live-in は物理レジスタと、そこから
コピーされた仮想レジスタを対にします（`GetFunctionLiveIn`、`AddFunctionLiveIn`、
`RemoveFunctionLiveIn`）。ブロックレベルの live-in はレーンマスクを伴います
（`AddBasicBlockLiveIn`、`RemoveBasicBlockLiveIn`）。

## スタックフレーム

```c
int32_t FrameIndex = 0;
MIR->CreateStackObject(MIR->Context, Task, Mutation, /*Size=*/16,
                       /*Alignment=*/8, /*IsSpillSlot=*/NEVERC_FALSE,
                       /*StackID=*/0, &FrameIndex);
```

`CreateFixedStackObject` は既知のオフセットにオブジェクトを置き（`IsImmutable`
と `IsAliased` 付き）、`CreateVariableSizedStackObject` は動的な確保を扱います。
あとから `SetFrameObjectSize`、`SetFrameObjectAlignment`、
`SetFrameObjectOffset` で調整できます。

`NevercMIRFrameObjectInfo` は `Index`、`Flags`、`Size`、`Offset`、`Alignment`、
`StackID` を報告します。フレームフラグは `FIXED`、`SPILL_SLOT`、
`VARIABLE_SIZED`、`IMMUTABLE`、`ALIASED`、`DEAD`、`PREALLOCATED` です。呼び出され
る側が保存する状態は `GetCalleeSaved` で読み、`SetCalleeSaved` で丸ごと置き換え
ます。

## 定数プール、ジャンプテーブル、メモリオペランド

定数プールの項目は値を `NevercMIRWordView` として運ぶので、整数の項目も浮動小数点
の項目も同じ形です:

```c
NevercMIRConstantPoolEntryDesc Desc = {0};
Desc.Header       = /* … */;
Desc.Kind         = NEVERC_MIR_CONSTANT_INTEGER;
Desc.Alignment    = 8;
Desc.Value.Data   = Words;
Desc.Value.Count  = 1;
Desc.Value.BitWidth = 64;

uint32_t Index = 0;
MIR->CreateConstantPoolEntry(MIR->Context, Task, Mutation, &Desc, &Index);
```

ジャンプテーブルは行き先ブロックの配列から、7 つの項目種別
（`BLOCK_ADDRESS`、`GP_REL64_BLOCK_ADDRESS`、`GP_REL32_BLOCK_ADDRESS`、
`LABEL_DIFFERENCE32`、`LABEL_DIFFERENCE64`、`INLINE`、`CUSTOM32`）のひとつを
指定して作ります。

メモリオペランドは最も情報量の多い記述子です。フラグ（`LOAD`、`STORE`、
`VOLATILE`、`NON_TEMPORAL`、`DEREFERENCEABLE`、`INVARIANT`、加えて 3 つの
ターゲットフラグ）、サイズとアラインメント、9 種のうちひとつのポインタ
（`IR_VALUE`、`FIXED_STACK`、`STACK`、`CONSTANT_POOL`、`JUMP_TABLE`、`GOT`、
`UNKNOWN_STACK`、`TARGET_CUSTOM`、`UNKNOWN`）、成功時と失敗時のアトミック順序、
同期スコープ、そして TBAA、alias-scope、no-alias、range の各参照。取り付けには
`AddInstructionMemoryOperand` を使います。

## トランザクショナルな変更

あらゆる変更は、1 つのマシン関数に束縛された mutation の中にステージングされます:

```c
NevercMIRMutationHandle Mutation;
MIR->BeginMutation(MIR->Context, Task, Function, &Mutation);

NevercMIRInstructionOpcode Opcode = {0};
Opcode.StableOpcode = MyGenericOpcode;

NevercMachineInstrHandle New;
MIR->CreateInstruction(MIR->Context, Task, Mutation, Block,
                       /*InsertBefore=*/Terminator, Opcode, &New);

NevercMIROperandValue Op = {0};
Op.Header = /* … */;
Op.Kind   = NEVERC_MIR_OPERAND_IMMEDIATE;
Op.Payload.Immediate = 42;
MIR->AppendOperand(MIR->Context, Task, Mutation, New, &Op, &Operand);

Status = MIR->CommitMutation(MIR->Context, Task, Mutation);
if (Status.Code != NEVERC_STATUS_OK)
  MIR->AbortMutation(MIR->Context, Task, Mutation);
MIR->EndMutation(MIR->Context, Task, Mutation);
```

コミットは構造の事前検査を行い、続いて Machine IR の検証器を走らせます。無効な
オペランド、壊れた CFG、ターゲットスキーマが実オペコードを要求する場所での汎用
オペコードの使用、サポートされないプロパティの主張は、いずれもアトミックに
ロールバックされます。中止はブロック順、命令、オペランド、CFG の辺、マシン
プロパティを元どおりに戻します。

`EndMutation` はハンドルを解放し、コミットや中止とは別物です —— どちらの経路でも
呼んでください。

ステージング可能な操作は `CreateBasicBlock`、`MoveBasicBlock`、
`EraseBasicBlock`、`CreateInstruction`、`MoveInstruction`、`EraseInstruction`、
`AppendOperand`、`SetOperandValue`、`SetInstructionFlags`、`AddCFGEdge`、
`RemoveCFGEdge`、上に挙げたレジスタとフレームの呼び出し、定数プールとジャンプ
テーブルの呼び出し、メモリオペランドの呼び出し、そして
`SetMachinePropertyWithProof` です。

## マシンプロパティには証明が要る

11 のマシンプロパティ —— `IS_SSA`、`NO_PH_IS`、`TRACKS_LIVENESS`、`NO_V_REGS`、
`FAILED_I_SEL`、`LEGALIZED`、`REG_BANK_SELECTED`、`SELECTED`、
`TIED_OPS_REWRITTEN`、`FAILS_VERIFICATION`、`TRACKS_DEBUG_USER_VALUES` —— は自由
に読めますが、自由に設定はできません:

```c
NevercMIRPropertyProof Proof = {0};
Proof.Header   = /* … */;
Proof.Property = NEVERC_MIR_PROPERTY_IS_SSA;
Proof.Kind     = NEVERC_MIR_PROPERTY_PROOF_INVALIDATION;
Proof.Value    = NEVERC_FALSE;
MIR->SetMachinePropertyWithProof(MIR->Context, Task, Mutation, &Proof);
```

証明は 2 種類です。`INVALIDATION` は、あなたの変更が前提を壊したプロパティを
消します —— 保証を手放すのは安全なので、これは常に受け入れられます。
`STRUCTURAL_CHECK` はプロパティを立てる前にホストに検証を求めるので、`IS_SSA` を
主張するには約束ではなく実際の検査という代価がかかります。

## パス

```c
NevercMIRPassDescriptor Pass = {0};
Pass.Header = (NevercABITableHeader){sizeof(Pass), NEVERC_MIR_PASS_API_MAJOR,
                                     NEVERC_MIR_PASS_API_MINOR, 0};
Pass.PassID        = SV("example.machine-pass");
Pass.Phase         = (NevercInterfaceID){NEVERC_PHASE_MIR_PASS_PREEMIT_HIGH,
                                         NEVERC_PHASE_MIR_PASS_PREEMIT_LOW};
Pass.Level         = NEVERC_MIR_PASS_LEVEL_FUNCTION;
Pass.Deterministic = NEVERC_TRUE;
Pass.Run           = run_machine_function;
PassAPI->RegisterPass(PassAPI->Context, RegistrarContext, &Pass);
```

これは [`pluginsdk/examples/MachinePass.c`] そのままです。レベルは `MODULE`、
`FUNCTION`、`BASIC_BLOCK`。`RequiredAnalyses` と `PreservedAnalyses` は
`NevercMIRBuiltinAnalysis` の配列で、`RequiredTargetSchemaDigest` はそのパスが
想定していないスキーマに対して実行されるのを拒ませます。

呼び出しは `Task`、`Phase`、`PassID`、`Level`、そのレベルで有効な `Function` と
`BasicBlock`、`Core` と `Analyses` の各テーブル、そして現在有効な
`TargetSchemaDigest` を運びます。

保存の申告は `OutPreserved` で行います —— `NEVERC_MIR_PRESERVE_NONE`、`_CFG`、
`_ALL`、加えて `Analyses` の明示リスト。コミット済みの変更のあとに
`PRESERVE_ALL` を主張すると拒否されます。

関数パスは並列のコード生成パーティションで走ることがあり、モジュールレベルのパスは
直列化されたパイプラインの障壁で走ります。プラグインが宣言した並行性と再入の
モデルは、依然としてあなた自身の状態を律します。

## 解析

組み込みは 6 つ: `LIVE_INTERVALS`、`LIVE_VARIABLES`、`SLOT_INDEXES`、
`DOMINATOR_TREE`、`LOOP_INFO`、`REGISTER_PRESSURE`。

```c
NevercMIRAnalysisResultHandle Intervals;
Analyses->QueryBuiltin(Analyses->Context, Task,
                       NEVERC_MIR_ANALYSIS_LIVE_INTERVALS, Function,
                       &Intervals);

uint64_t SegmentCount = 0;
Analyses->GetLiveIntervalSegmentCount(Analyses->Context, Task, Intervals,
                                      Register, &SegmentCount);
for (uint64_t I = 0; I != SegmentCount; ++I) {
  NevercMIRLiveRangeSegment Segment;
  Analyses->GetLiveIntervalSegment(Analyses->Context, Task, Intervals,
                                   Register, I, &Segment);
  /* Segment.Start、Segment.End */
}
```

ほかに `DominatorTreeDominates`、`GetLoopCount` / `GetLoopHeader` /
`GetLoopForBlock`、`GetSlotIndex`、`IsRegisterLiveInBlock`、
`GetRegisterPressureSetCount` / `GetRegisterPressure` も使えます。

利用できるかどうかはフック次第です。`post_isel` で生存区間を求めると、背後の
LLVM 解析がまだ存在しないため `NEVERC_STATUS_CAPABILITY_UNAVAILABLE` で失敗し
ます。コミットされた変更は、それが影響した結果ハンドルを無効化します。

## IR から MIR への低下処理の置き換え

```c
NevercIRToMIRInputInfo In = {0};
In.Header = /* … */;
Provider->GetIRToMIRInput(Provider->Context, Frame, Frame->Input, &In);
/* In.Module、.IR、.TargetID、.CompatibilityKey、.TargetSchemaDigest、
   .DefinedFunctionCount */

const NevercMIRAPI *MIR;
NevercMachineFunctionHandle MF;
Provider->GetOrCreateMachineFunction(Provider->Context, Frame, IRFunction,
                                     &MIR, &MF);
/* … マシン関数を構築する … */

NevercMIRModuleCoverageDescriptor Coverage = {0};
Coverage.Header              = /* … */;
Coverage.HandlesGlobals      = NEVERC_TRUE;
Coverage.HandlesConstructors = NEVERC_TRUE;
Coverage.HandlesDebugInfo    = NEVERC_FALSE;
Coverage.HandlesUnwind       = NEVERC_FALSE;
Provider->PublishMIRModule(Provider->Context, Frame, &Coverage, &Output);
```

カバレッジ記述子は、部分的なプロバイダが正直でいるための仕組みです。実際に低下
処理したものだけを宣言すれば、残りはホストが自分で扱います。グローバル変数、
コンストラクタ、デバッグ情報、巻き戻し表が黙って落ちることはありません。

## 例

```sh
cmake --build build-neverc --target neverc-plugin-example-machine-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/MachinePass.so \
  -O2 -fno-lto -c input.c -o input.o
```

CMake がお使いのプラットフォーム向けに生成したモジュール拡張子を使ってください。

## 規則

- コールバックが返ったあとにタスクハンドル、MIR ハンドル、借用したビューを保持
  しないこと。ハンドル値や LLVM のオペコード番号をでっち上げないこと。
- `RequiresTargetSchema` フラグが立っている値を使う前に、`GetSchemaDigest` を
  自分がコンパイル時に取り込んだダイジェストと突き合わせること。
- 変更は mutation の内側だけで行うこと。すべての `BeginMutation` は、コミットか
  中止のあとにちょうど 1 つの `EndMutation` に至ります。
- 証明なしにマシンプロパティを主張しないこと。変更が保証を手放したなら
  `STRUCTURAL_CHECK` より `INVALIDATION` を選ぶこと。
- コミット済みの変更のあとに `NEVERC_MIR_PRESERVE_ALL` を主張しないこと。
- 必要な解析が、選んだフックで本当に使えるかを確認すること。
- すべてのテーブルヘッダと予約フィールドを初期化すること。ステータスは C の境界
  越しに返し、C++ の例外を決して越えさせないこと。
- `neverc.mir.final_verify` は封印されています。何があっても走ります。

規範的な宣言、スキーマそのもの、生成される定数、フェーズポリシー、カバレッジの証拠は、
[`PluginMIR.h`]、[`Schema/MIRSchema.json`]、[`Schema/PluginMIRSchema.inc`]、
[`Schema/PhaseSchema.json`]、[`coverage.json`] を参照してください。

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PluginMIR.h`]: ../../neverc/include/neverc/Plugin/PluginMIR.h
[`pluginsdk/examples/MachinePass.c`]: ../../pluginsdk/examples/MachinePass.c
[`Schema/MIRSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/MIRSchema.json
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginMIRSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginMIRSchema.inc
