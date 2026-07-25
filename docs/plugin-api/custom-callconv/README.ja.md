**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# カスタム呼び出し規約

NeverC は**データ駆動のカスタム呼び出し規約**をサポートします。コンパイラ本体や TableGen 定義に一切手を入れることなく、アウトオブツリーのプラグインまたはソースレベル属性だけで、任意の関数の引数と戻り値に任意の物理レジスタを割り当てられます。

## 概要

従来の LLVM 呼び出し規約は `.td` / `.inc` ファイルを通じてバックエンドに焼き込まれています。追加や変更のたびにコンパイラのソースを編集し、TableGen を再実行する必要があります。NeverC はこれを二層構成の**実行時データ駆動**モデルで置き換えます。

- **spec** — `gpr:rcx,rdx;ret:rax` のような、人間が手で書ける短い文字列 — をプラグインまたはソースレベル属性が `"neverc-callconv"` 文字列属性として関数に付与します。
- コード生成の前に、ホストがその spec を `"neverc-cc-plan-v1"` 属性へと**マテリアライズ**します。これは特定のターゲットスキーマに束縛された、不変で検証済みの正確な位置テーブルです。バックエンドが消費するのは plan だけです。

spec は書く側のもの、plan はバックエンドが信頼するものです。これにより呼び出し規約は「コンパイル時にバックエンドへハードコード」から「実行時に外部ポリシーが駆動」へ移行しつつ、検証を手放しません。

## Spec の形式

spec はセミコロン区切りの文字列です。各セグメントはキーとカンマ区切りのレジスタ名リストから成ります（大文字小文字を区別せず、空白を許容します）。

```
gpr:rcx,rdx,r8,r9; xmm:xmm0,xmm1; ret:rax; ret_xmm:xmm0
```

| セグメント | 別名 | 意味 |
|---|---|---|
| `args` | | **位置モード**：各トークンはレジスタ名または `stack`/`mem` で、引数のインデックス順に対応します |
| `gpr` | `arg_gpr` | **プールモード**：整数/ポインタ引数レジスタ。順に消費し、尽きるとスタックへ退避します |
| `xmm` | `arg_xmm` | **プールモード**：浮動小数点/ベクトル引数レジスタ |
| `fpr` | | `xmm` のターゲット中立な別名 |
| `ret_gpr` | `ret` | 整数/ポインタ戻り値レジスタ |
| `ret_xmm` | | 浮動小数点/ベクトル戻り値レジスタ |
| `ret_fpr` | | `ret_xmm` のターゲット中立な別名 |
| `csr` | | カスタムの callee-saved レジスタ集合（既定は標準 ABI の集合） |

どのセグメントも省略でき、認識できないセグメントは無視されます。これらのキーは `llvm/include/llvm/CodeGen/NeverCCallConv.h` に一度だけ定義されているため、生成側とパーサが食い違うことはありません。

### 二つの引数モード

**プールモード**（`gpr:` / `xmm:`）：整数引数は `gpr` プールから順にレジスタを取り、浮動小数点とベクトル引数は `xmm` から取ります。プールが尽きると残りの引数はスタックへ退避します。

**位置モード**（`args:`）：*i* 番目の引数は *i* 番目のトークンを使います。各トークンはレジスタ名か、その引数を強制的にスタックへ送る `stack` / `mem` のいずれかです。

```
args:rcx,stack,r8;ret:rax   # 引数0→rcx、引数1→スタック、引数2→r8、戻り値→rax
```

`args` が存在する場合は `gpr` / `xmm` より優先されます。引数の型に対してレジスタクラスが合わないトークン、トークンリストを超えるインデックス、すでに割り当て済みのレジスタは、いずれもビルドを失敗させずスタックスロットへフォールバックします。

### 対応アーキテクチャ

レジスタ名はターゲットごとのテーブルで解決されます。このテーブルが、spec に書けるレジスタ名の唯一の根拠です。

| アーキテクチャ | GPR 名 | SIMD 名 | 幅の選択 |
|---|---|---|---|
| **x86-64** | `rax`、`rbx`、`rcx`、`rdx`、`rsi`、`rdi`、`rbp`、`r8`–`r15` | `xmm0`–`xmm15` | i32 → 32 ビットサブレジスタ、i64/ポインタ → 64 ビット |
| **AArch64** | `x0`–`x28` | `v0`–`v31` | i32→`w`、i64→`x`、f16→`h`、f32→`s`、f64→`d`、f128/ベクトル→`q` |

GPR は常に 64 ビット表記で書き、バックエンドが各値の型に合うサブレジスタへ絞り込みます。AArch64 のベクトルレジスタは `v0`–`v31` と書き、バックエンドが型に応じて `H`/`S`/`D`/`Q` 形式を選びます。

### 制約

- **予約レジスタ**：スタックポインタは両方のテーブルに存在しません（x86-64 の `rsp`、AArch64 の `sp`/`x31`）。AArch64 の `x29`/`x30`（FP/LR）も同様です。spec でこれらを指名しても単に読み飛ばされ、その値は次の有効な位置に置かれます。
- **フレームポインタ**：x86-64 の `rbp` は正当な callee-saved レジスタなので選択**可能**ですが、引数レジスタとして使うのが健全なのは `-fomit-frame-pointer` の下だけです。自己責任でお使いください。
- **Callee-saved**：既定は標準 ABI の集合です。`csr:r12,r13` はカスタム集合を宣言し、呼び出し側は対応する保存レジスタマスクを構築して、どのレジスタが呼び出しをまたいで生き残るかを把握します。x86-64 と AArch64 の両方で利用できます。
- **csr の衝突**：あるレジスタが `csr` と引数/戻り値リストの両方に現れた場合、プラグインが警告します。callee がそれを復元してしまい、値を渡す役割を壊すからです。コンパイル自体は成功します。
- **可変長引数関数**：非対応です。可変長部分を黙って誤って渡すのではなく、両方のバックエンドが明確な診断を出します。
- **間接呼び出し**：関数ポインタ経由の呼び出しはカスタム規約を運べません。カスタム規約の関数のアドレスが取られるとプラグインが警告し、間接呼び出しは標準規約へフォールバックします。
- **末尾呼び出し**：呼び出しのどちらか一方でもカスタム規約を使う場合、両方のバックエンドで無効化されます。
- **plan が扱わない値**：plan が覆っていない引数や戻り値は、ターゲットの標準規約（x86-64 は SysV、AArch64 は AAPCS）へフォールバックします。

## 使い方

### 1. プラグイン駆動（推奨）

リファレンスプラグイン `CustomCallConvPlugin.c` は `pluginsdk/examples/` にあります。`neverc.ir.pass.post_opt` フェーズにモジュールレベルの IR パスを登録します。

**プラグインをビルドする：**

```bash
cd pluginsdk/examples && make CustomCallConvPlugin.dylib   # または .so / .dll
```

**属性モード**（既定）— `custom_attr` のソース注釈が付いた関数だけが対象です。

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib input.c -o output.o
```

**グローバルモード** — 定義済みの全関数に一つの spec を適用します（`cc-all` の明示が必要）。

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib \
       -fplugin-arg=org.neverc.example.custom-callconv:cc-all \
       -fplugin-arg=org.neverc.example.custom-callconv:ccspec="gpr:r10,r11,rsi;ret:rdx" \
       input.c -o output.o
```

**名前の接頭辞で絞り込む：**

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib \
       -fplugin-arg=org.neverc.example.custom-callconv:cc-all \
       -fplugin-arg=org.neverc.example.custom-callconv:ccprefix=secret_ \
       -fplugin-arg=org.neverc.example.custom-callconv:ccspec="gpr:r9,r8;ret:rax" \
       input.c -o output.o
```

**多様化** — 四つの組み込みレイアウトを巡回させ、関数どうしが同じレイアウトを共有しないようにします（リバースエンジニアリング対策）。

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib \
       -fplugin-arg=org.neverc.example.custom-callconv:cc-all \
       -fplugin-arg=org.neverc.example.custom-callconv:ccshuffle \
       input.c -o output.o
```

プラグインが登録するオプションは四つです。`cc-all` と `ccshuffle`（フラグなので `=1` や `=true` は省略可）、`ccspec` と `ccprefix`（文字列値）。`ccspec` を与えない場合、グローバルモードは既定値 `gpr:r10,r11,rsi,rdi;ret:rdx` を使います。

### 2. ソースレベル属性

`custom_attr` 属性を使って C のソース中で直接関数に注釈を付けます。GNU 構文と Microsoft 構文の両方に対応します。

```c
// GNU 構文
__attribute__((custom_attr("neverc-callconv", "gpr:r10,r11,rsi;ret:rdx")))
int add3(int a, int b, int c) { return a + b + c; }

// Microsoft 構文
__declspec(custom_attr("neverc-callconv", "gpr:r10;ret:rdx"))
int msfunc(int a) { return a; }
```

`custom_attr("key", "value")` は清潔な関数文字列属性（`"key"="value"`）を生成し、警告を**出さず**、`llvm.global.annotations` にも**入りません**。これは**汎用**の仕組みで、呼び出し規約に限らず任意のキー/値の組が使えます。IR パスと MIR パスは `F.getFnAttribute("key")` で読み戻します。

### 3. 併用

ソース属性とプラグイン引数は併用できます。`custom_attr` を持つ関数はプラグインの属性モード経路で処理され、`cc-all` が残りを覆います。各関数が処理されるのは最大一度です。

## マテリアライズされた plan

spec はレジスタを指名するだけで、各値の各バイトがどこに置かれるかまでは述べません。最適化パイプラインの後、コード生成の前に、ホストは `materializeCallingConventionPlans` を実行し、すべての `CallingConv::NeverC_Custom` 関数を正確で検証済みの plan に変換します。

- すでに `"neverc-cc-plan-v1"` 属性を持つ関数は**検証されるだけで、再生成されません**。スキーマダイジェスト、ターゲット ID、規約 ID が現在のターゲットと一致していなければなりません。
- `"neverc-callconv"` spec を持つ関数は、そのレジスタ名がターゲットのレジスタテーブルに照らして解決されます。生成された plan が spec を置き換え、spec は IR から取り除かれます。
- どちらも持たないが、そのターゲットがプラグイン ABI 経由で呼び出し規約を登録している関数は、その規約の `PlanCallingConvention` コールバックによって計画されます。

すべての直接呼び出し箇所は callee の plan を継承します。これが翻訳単位をまたいで呼び出し側と呼び出され側のレイアウトを一致させ続ける仕組みです。plan はフラットな文字列です。

```
neverc-cc-plan-v1;schema=<ダイジェスト>;target=<high>:<low>;cc=<high>:<low>;stack=<バイト>;returns=<位置>;arguments=<位置>;callee-saved=<レジスタ番号>
```

各位置は `<r|s>,<値インデックス>,<断片オフセット>,<サイズ>,<アラインメント>,<レジスタ番号>,<スタックオフセット>,<フラグ>` の形式で、複数の位置は `|` で区切ります。組み込み経路のスキーマダイジェストは `llvm-<ターゲットトリプル>` です。プラグインが登録したターゲットは独自のダイジェストを提供します。

レジスタ番号はそれを定義したスキーマの下でしか意味を持たないため、不一致は黙ったミスコンパイルではなくハードエラーになります。

| 状況 | 診断 |
|---|---|
| plan 文字列がパースできない | `malformed NeverC calling convention plan` |
| スキーマダイジェストが異なる | `NeverC calling convention plan belongs to a foreign target schema` |
| ターゲット ID が異なる | `NeverC calling convention plan has a foreign target ID` |
| 規約 ID が異なる | `NeverC calling convention plan has a foreign convention ID` |

これこそが plan をビットコードへ安全に埋め込み、LTO を通過させられる理由です。別のターゲット向けに作られた plan が誤って適用されることはありません。

## プラグイン API

サンプルプラグインが使うのは安定版の IR core テーブルだけで、呼び出し規約専用のエントリポイントは存在しません。関数に規約を適用するのは、三回の呼び出しと呼び出し箇所の同期です。

```c
NevercIRAttributeHandle Attribute = {0};
Core->CreateStringAttribute(Core->Context, Task, SV("neverc-callconv"), Spec,
                            &Attribute);
Core->AddFunctionAttribute(Core->Context, Task, Function,
                           NEVERC_IR_ATTRIBUTE_LOCATION_FUNCTION, 0, Attribute);
Core->SetFunctionCallingConvention(Core->Context, Task, Function,
                                   NEVERC_IR_CALLING_CONVENTION_NEVER_C_CUSTOM);
```

`NEVERC_IR_CALLING_CONVENTION_NEVER_C_CUSTOM` は `CallingConv::NeverC_Custom`（LLVM 値 1000）の ABI 安定な名前です。続いてプラグインは `GetValueUseCount` / `GetValueUse` でその関数の使用箇所をたどり、`call`、`invoke`、`callbr` の被呼び出しオペランドである使用箇所ごとに、`SetInstructionProperty` と `NEVERC_IR_PROPERTY_CALLING_CONVENTION` で同じ規約を命令に設定します。それ以外の使用箇所はアドレスが漏れたことを意味し、これが「アドレスが取られた」警告の出どころです。

自前のターゲットを登録するプラグインは、代わりに `NevercCallingConventionDescriptor` に `PlanCallingConvention` コールバックを提供し、spec の層を飛ばして直接 plan を生成することもできます。[ターゲット、MC、アセンブリ、オブジェクト](../target-mc-object.ja.md) を参照してください。

## テスト

GoogleTest スイートは `tests/neverc/CustomCallConvTests.cpp` にあり、26 個のテストを含みます。各テストはサンプルプラグインをビルドし、与えられた spec の下で小さなプログラムをアセンブリへコンパイルし、結果のレジスタまたはスタック配置を検証します。

```bash
ninja -C build-neverc neverc-tests
build-neverc/bin/neverc-tests --gtest_filter='CustomCallConvTest.*'
```

カバレッジ：

| カテゴリ | テスト数 |
|---|---|
| x86-64 プール / 位置 / スタック / 退避 / i64 / sret / byval / フォールバック | 9 |
| AArch64 GPR / FPR / スタック / `csr` / 異なる spec 間の呼び出し | 5 |
| フロントエンド `custom_attr`（GNU / `__declspec` / エンドツーエンド） | 3 |
| plan のマテリアライズとスキーマ拒否 | 3 |
| 堅牢化（`csr`、両ターゲットの可変長引数、間接呼び出し、`rsp`、csr 衝突） | 6 |

## アーキテクチャ

```
ソース属性                     プラグイン IR パス
custom_attr(...)              (neverc.ir.pass.post_opt)
       │                            │
       └─────────────┬──────────────┘
                     ▼
   "neverc-callconv" = spec、CallingConv::NeverC_Custom
   関数とその直接呼び出し箇所に付与
                     │
                     ▼
   ┌──────────────────────────────────────────┐
   │ materializeCallingConventionPlans        │
   │ （最適化の後、コード生成の前）           │
   │                                          │
   │  spec      → 名前を物理レジスタへ解決    │
   │  プラグイン規約 → PlanCallingConvention  │
   │  既存 plan → スキーマ/ターゲットを検証   │
   └──────────────────────────────────────────┘
                     │
                     ▼
   "neverc-cc-plan-v1" = 検証済みの位置テーブル
   spec は除去され、plan は直接呼び出し箇所へ複製
                     │
                     ▼
   ┌──────────────────────────────────────────┐
   │ バックエンド CCAssignFn（ターゲット毎）  │
   │  CC_X86_NeverC     / RetCC_X86_NeverC    │
   │  CC_AArch64_NeverC / RetCC_AArch64_NeverC│
   │                                          │
   │  plan を読み取り → 位置を割り当て        │
   │  扱わない値 → 標準規約                   │
   │  末尾呼び出しは無効                      │
   └──────────────────────────────────────────┘
                     │
                     ▼
   カスタムレジスタ配置を持つ機械語
```

バックエンドの実行器は**一度きりの実装**であり、ポリシーの判断はすべてプラグイン側にあります。新しい規約を追加するのに NeverC を再ビルドする必要は決してありません。
