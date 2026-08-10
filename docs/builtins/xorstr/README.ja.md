**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC ビルトインランタイムシステム](../README.ja.md)

# コンパイル時文字列暗号化 (`xorstr`)

## 概要

NeverC は C コード向けの二層コンパイル時文字列暗号化を提供します。API 名、レジストリパス、デバッグメッセージなどの機密文字列がコンパイル済みバイナリに平文で残らないよう設計されています。

- **レイヤー 1 — 明示的マクロ**: `NC_XORSTR("string")` / `NEVERC_XORSTR("string")` で文字列単位の精密制御
- **レイヤー 2 — 自動 IR パス**: `-fencrypt-call-strings` で関数呼び出しの全文字列引数を自動暗号化

両レイヤーともスタック割り当てバッファ（ヒープ不使用）、インスタンスごとのキーストリーム、volatile クリアを使用します。ネイティブ機械語境界では、明示的な `NC_XORSTR` デコーダ呼び出しを再暗号化して各呼び出し箇所へ直接展開するため、最終オブジェクトに共有デコーダ関数は残りません。

---

## クイックスタート

### レイヤー 1: 明示的マクロ

```c
#include <neverc/xorstr/xorstr.h>

FARPROC addr = GetProcAddress(hModule, NC_XORSTR("NtQuerySystemInformation"));
```

### レイヤー 2: 自動暗号化

```bash
neverc -fencrypt-call-strings main.c -o main
```

---

## レイヤー 1: `NC_XORSTR` / `NEVERC_XORSTR` マクロ

すべての文字列リテラル種別（通常、UTF-8、ワイド、UTF-16、UTF-32）をサポート。非リテラル引数はコンパイルエラーになります。

### 保護フロー

1. **Sema** がリテラルごとに独立した鍵で暗号化します。seed `0` は OS から新しいエントロピーを取得し、`-fstring-encrypt-key=` は決定的な 64 ビット出力を選択します。
2. **中間 IR / LTO 入力**では、不透明で特殊化不能なデコーダ呼び出しを保持し、通常最適化や LTO が平文を IR に戻すことを防ぎます。
3. **最終機械語境界**でコンパイラ側 ciphertext を復号・再暗号化し、呼び出し箇所ごとにループ形状を選んで直接展開します。その後、デコーダ、補助グラフ、ABI アンカー、ルート状態、意味を示す名前を削除します。
4. **クリア処理**は最適化/provider への引き渡し前と最終テールの両方に挿入されます。後段は冪等で、CFG 変更後の配置を修復します。

### デコーダの多様化

状態遷移、定数、ciphertext、等価なバイト演算は seed と呼び出し箇所ごとに変化します。`a + b − 2 × (a & b)` はその一形態です。volatile な状態/ciphertext ロードが定数畳み込みを抑え、`nooutline` が IR finalization 後に Machine Outliner が共有デコーダを再構築することを防ぎます。

これにより、IDA が一度だけ識別・エミュレートできる安定した単独ルーチンはなくなります。ただし、実行中に必要な平文まで動的 instrumentation で観測不能になるという意味ではありません。

---

## レイヤー 2: `-fencrypt-call-strings`

| フラグ | 説明 | デフォルト |
|--------|------|-----------|
| `-fencrypt-call-strings` | 自動暗号化を有効化 | オフ |
| `-fno-encrypt-call-strings` | 無効化 | — |
| `-fencrypt-call-strings-max-len=N` | N バイトを超える文字列をスキップ | 1024 |

この変換は IPO 前、通常最適化後、および通常または plugin が提供する各 late IR フェーズの後に実行されます。LTO でも provider hook と pre-codegen hook の後に同じ必須シールを適用します。

コンパイラ所有の private `unnamed_addr` リテラルに由来する直接・間接 `CallBase` 引数を処理し、GEP、cast、`freeze`、`select`、PHI、昇格可能なローカルポインタスロットの意味を保持します。intrinsic、inline asm、外部可視またはユーザー定義配列、上限を超えるリテラルは対象外です。保護対象リテラルを `musttail` で渡す場合は安全側にコンパイルエラーとします。

## スタッククリア（`XorStrCleanupPass`）

到達可能なすべての `ret`、`resume`、呼び出し元へ unwind する `cleanupret`、未捕捉の `catchswitch` unwind の前で、完全なバッファを volatile `memset` により消去します。安全性を完全に追跡できないストレージは部分的に消去せず拒否します。

---

## `.encrypt()` との比較

| 側面 | `NC_XORSTR()` | `.encrypt()` |
|------|---------------|--------------|
| **利用可能性** | 純 C（ヘッダー経由） | NeverC 構文拡張のみ |
| **メモリ** | スタック（`alloca`） | ヒープ（`NEVERC_STRING_ALLOC`） |
| **戻り値型** | `const char*` | `string`（値型） |
| **用途** | Win32 API、FFI | 汎用文字列操作 |

---

## コンパイラフラグリファレンス

| フラグ | 説明 |
|--------|------|
| `-fencrypt-call-strings` | 関数呼び出し引数の自動文字列暗号化を有効化 |
| `-fno-encrypt-call-strings` | 自動暗号化を無効化 |
| `-fencrypt-call-strings-max-len=N` | 自動暗号化の最大バイト長（デフォルト: 1024） |
| `-fstring-encrypt-key=0xHEX` | 完全な 64 ビット seed を指定。`0` は新しいエントロピーを使用 |

## 出力境界と再現性

- `-fno-lto` は frontend のネイティブコード生成時に finalization を行います。
- Auto-LTO と Full LTO は pre-link bitcode に不透明デコーダを保持し、全プログラム最適化と plugin IR 最適化の後で再暗号化・展開します。
- provider 置換 pipeline と late plugin pass の後には、必ず暗号化、クリア、finalization のテールが続きます。
- 既定 seed では独立したネイティブビルドの出力が異なり、以前の保護コードを再利用し得る whole-link/partition cache は回避されます。
- 非ゼロ seed は意図的に決定的かつ cache 対応です。同じ入力と同じ完全な 64 ビット seed は同じ保護コードを生成します。
- `-emit-llvm` と pre-link bitcode は中間成果物なので、不透明な decoder ABI を意図的に保持します。「共有デコーダなし」の保証は正常に生成された最終機械語に適用されます。
