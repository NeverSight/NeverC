**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# カスタム呼び出し規約

NeverC は**データ駆動型カスタム呼び出し規約**をサポートしています。外部プラグインやソースコード属性から、任意の関数の引数・戻り値に任意の物理レジスタを割り当てることができます。コンパイラ本体や TableGen 定義の変更は不要です。

## 概要

従来の LLVM 呼び出し規約は `.td` / `.inc` ファイルでバックエンドにハードコードされています。NeverC はこれを**ランタイムデータ駆動**方式に置き換えます：

- **レジスタ割り当てスペック**（プレーンな文字列）が関数の文字列属性として付加されます。
- バックエンドがこのスペックを読み取り、指定された物理レジスタに引数/戻り値を割り当てます。
- スペックは**外部プラグイン**（IR パス）、**ソースコード属性**（`__attribute__` / `__declspec`）、またはその両方から提供できます。

## スペック形式

セミコロン区切りの文字列で、各セグメントはキーとカンマ区切りのレジスタ名で構成されます（大文字小文字不問、空白許容）：

```
gpr:rcx,rdx,r8,r9; xmm:xmm0,xmm1; ret:rax; ret_xmm:xmm0
```

| セグメント | エイリアス | 意味 |
|---|---|---|
| `args` | | **位置指定モード**：各トークンはレジスタ名または `stack`/`mem` |
| `gpr` | `arg_gpr` | **プールモード**：整数/ポインタ引数レジスタ（順に使用、枯渇時はスタックへ） |
| `xmm` | `arg_xmm` | **プールモード**：浮動小数点/ベクトル引数レジスタ |
| `fpr` | `arg_fpr` | AArch64 での `xmm` エイリアス |
| `ret_gpr` | `ret` | 整数/ポインタ戻り値レジスタ |
| `ret_xmm` | | 浮動小数点/ベクトル戻り値レジスタ |
| `ret_fpr` | | AArch64 での `ret_xmm` エイリアス |
| `csr` | | カスタム callee-saved レジスタセット（デフォルト：標準 ABI セット） |

### 2つの引数モード

**プールモード**（`gpr:` / `xmm:`）：整数引数は `gpr` プールから、浮動小数点引数は `xmm` プールから順に取得。プール枯渇後はスタックへスピル。

**位置指定モード**（`args:`）：第 *i* 引数は第 *i* トークンを使用。`stack` / `mem` で強制的にスタック割り当て：

```
args:rcx,stack,r8;ret:rax   # 引数0→rcx, 引数1→スタック, 引数2→r8, 戻り→rax
```

### サポートアーキテクチャ

| アーキテクチャ | GPR 名 | SIMD 名 | ビット幅選択 |
|---|---|---|---|
| **x86-64** | `rax`, `rcx`, `rdx`, `rsi`, `rdi`, `r8`–`r11` | `xmm0`–`xmm15` | i32→32ビットサブレジスタ, i64→64ビット |
| **AArch64** | `x0`–`x28` | `v0`–`v31` | i32→`w`, i64→`x`, f16→`h`, f32→`s`, f64→`d`, f128/vec→`q` |

### 制約

- **Callee-saved**：デフォルトは標準 ABI セット。`csr:r12,r13` でカスタムセットを宣言（x86-64 / AArch64 両対応）。
- **予約レジスタ**：スタックポインタ（`rsp` / `sp`）と AArch64 の `x29`/`x30`（FP/LR）は引数/戻り値レジスタに指定できません（spec に書いても無視）。
- **csr 競合**：あるレジスタが `csr` と引数/戻り値リストの両方に現れると、bridge が警告を出します。
- **可変長引数関数**：非サポート。コンパイラが明確なエラーを出力。
- **間接呼び出し**：関数ポインタ経由の呼び出しはカスタム規約を適用不可。アドレス取得時に警告、間接呼び出しは標準規約にフォールバック。
- **末尾呼び出し**：カスタム規約関数では自動的に無効化。

## 使い方

### 1. プラグイン駆動（推奨）

```bash
cd pluginsdk/examples && make CustomCallConvPlugin.dylib
# 属性モード（デフォルト）
neverc -fplugin-pass=./CustomCallConvPlugin.dylib input.c -o output.o
# グローバルモード
neverc -fplugin-pass=./CustomCallConvPlugin.dylib \
       -fplugin-pass-arg=cc-all=1 \
       -fplugin-pass-arg=ccspec="gpr:r10,r11,rsi;ret:rdx" \
       input.c -o output.o
```

### 2. ソースコード属性

```c
__attribute__((custom_attr("neverc-callconv", "gpr:r10,r11,rsi;ret:rdx")))
int add3(int a, int b, int c) { return a + b + c; }

__declspec(custom_attr("neverc-callconv", "gpr:r10;ret:rdx"))
int msfunc(int a) { return a; }
```

### 3. 併用

ソース属性とプラグイン引数は同時に使用可能。各関数は最大1回処理されます。

## LTO サポート

プラグインは `NEVERC_INTERPOSE_POST_OPT` と `NEVERC_INTERPOSE_LTO_POST_OPT` の両方に登録されます。LTO で翻訳単位が統合された後もカスタム規約を適用できます。

## プラグイン API

```c
API->FunctionSetCustomCallConv(F, "gpr:r10,r11,rsi;ret:rdx");
```

`CallingConv::NeverC_Custom`（CC 1000）を設定し、属性を書き込み、**すべての直接呼び出しサイトを同期**します。`NULL` または `""` を渡すとクリアされます。

## テスト

GoogleTest スイート（22 テスト、すべて PASS）：

```bash
ninja -C build-neverc neverc-tests
build-neverc/bin/neverc-tests --gtest_filter='CustomCallConvTest.*'
```
