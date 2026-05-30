**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# 浮動小数点演算対応 Windows カーネルドライバー

NeverC で構築した WDM カーネルドライバーで、**カーネルモードでの浮動小数点 / SIMD
の安全な使用方法**を示します。macOS / Linux からのクロスコンパイルに対応。

## ビルド

```bash
cd examples/windows-driver-float
make
```

スタンドアロンの NeverC リリースから：

```bash
make NEVERC=/path/to/neverc
```

出力は `FloatDriver.sys`（auto-LTO 最適化済み）。
デフォルトビルドにはデバッグ用の `-g` が含まれます。リリースビルドでは `-g` を削除してください。

---

## 対処すべき2つの問題

カーネルモードの浮動小数点には2つの独立した問題があります：

### 問題 1 — `_fltused` ABI マーカー（コンパイル/リンク時）

MSVC のコンパイラは、翻訳単位が浮動小数点演算を行うたびに、シンボル
`_fltused` への未定義参照を生成します。ユーザーモードプログラムでは
`libcmt.lib` がこのシンボルを提供するため、リンカーは満足し、いくつかの
FP 固有の CRT 部分が取り込まれます。

カーネルドライバーは `libcmt` にリンクしません（`-nostdlib` と
`-Xlinker --nodefaultlib` を渡しています）。そのため未解決の `_fltused`
はリンクエラーを引き起こします。

**NeverC の解決方法**：`-fms-kernel` 使用時、LLVM の X86 バックエンドは
`_fltused` をローカルに 0 として定義します。生成されたアセンブリで確認できます：

```asm
# ユーザーモードターゲット：
    .globl  _fltused              # 外部参照 —— libcmt が必要
```

```asm
# -fms-kernel ターゲット：
    .globl  _fltused
    .set    _fltused, 0           # ローカル定義！外部シンボル不要
```

そのため、ドライバーに**手動で `int _fltused = 0;` を書く必要は決してありません**。

### 問題 2 — カーネルは FP/SIMD レジスタを保存しない（実行時）

Windows カーネルはコンテキストスイッチ時にデフォルトで x87 / XMM / YMM / ZMM
レジスタを保存/復元**しません**。ドライバーが任意のカーネルコードからこれらの
レジスタに触れると、その CPU で実行中のユーザーモードスレッドの SIMD
状態を静かに破壊してしまいます。

**解決方法**：すべての浮動小数点 / SIMD 領域を
[`KeSaveExtendedProcessorState`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kesaveextendedprocessorstate)
と `KeRestoreExtendedProcessorState` で囲みます：

```c
XSTATE_SAVE save;
NTSTATUS status = KeSaveExtendedProcessorState(XSTATE_MASK_LEGACY, &save);
if (!NT_SUCCESS(status))
    return status;

// ... FP / SIMD コードをここに ...

KeRestoreExtendedProcessorState(&save);
```

### XSTATE マスク

| マスク | カバー範囲 |
|--------|----------|
| `XSTATE_MASK_LEGACY_FLOATING_POINT`（ビット 0） | x87 スタック |
| `XSTATE_MASK_LEGACY_SSE`（ビット 1） | XMM0–15 |
| `XSTATE_MASK_LEGACY` | ビット 0 \| ビット 1（ほとんどの通常の `double` / SSE コードをカバー） |
| `XSTATE_MASK_GSSE` / AVX（ビット 2） | YMM0–15 の上位半分 |
| `XSTATE_MASK_AVX512` | AVX-512 ZMM レジスタ |

コードが使用する最も広いレジスタに合わせて、OR で結合したマスクを渡します。

---

## このドライバーの動作

- `\Device\FloatDriver` にデバイスオブジェクト、`\DosDevices\FloatDriver` に
  シンボリックリンクを作成
- `DriverEntry` で `ComputeAreaSafe()`（FP 状態保存/復元で `ComputeArea()`
  をラップ）を `radius=1.0` と `radius=5.0` で2回呼び出し
- `DbgPrint` で double の生のビットを出力（`DbgPrint` は `%f` 非対応 ——
  `RtlCopyMemory` で 64 ビットパターンを抽出）
- `-fms-kernel` で暗黙的に `_fltused` を定義

## `_fltused` 出力の検証

`-fms-kernel` の有無でコンパイラの出力を比較：

```bash
# ユーザーモード（libcmt が必要）：
neverc --target=x86_64-pc-windows-msvc -S /tmp/foo.c -o - | grep fltused
#  .globl  _fltused

# カーネル（0 としてローカル定義）：
neverc --target=x86_64-pc-windows-msvc -fms-kernel -S /tmp/foo.c -o - | grep fltused
#  .globl  _fltused
#  .set    _fltused, 0
```

## ロード（Windows テストマシン上）

```cmd
sc create FloatDriver type= kernel binPath= C:\path\to\FloatDriver.sys
sc start FloatDriver
sc stop FloatDriver
sc delete FloatDriver
```

テスト署名を有効にするか、本番環境用のコード署名証明書を使用してください。

## 注意事項

- **`DbgPrint` は `%f` をサポートしません** —— カーネルデバッグ出力ルーチンには
  浮動小数点フォーマット機能がありません。double を固定小数点整数に変換するか、
  この例のように生のビットを出力します。
- **IRQL ≥ DISPATCH_LEVEL では浮動小数点を使用しないでください**（絶対必要な場合を除く）。
  `KeSaveExtendedProcessorState` のドキュメントに IRQL 制約が記載されています。
- **パフォーマンス**：状態の保存/復元は無料ではありません。ホットパスでは
  FP 作業を単一の囲み領域にまとめることを検討してください。
