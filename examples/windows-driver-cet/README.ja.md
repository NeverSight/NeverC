# CET シャドウスタック対応 Windows カーネルドライバー

NeverC で構築した最小限の WDM カーネルドライバーで、Intel CET（制御フロー強制技術）
カーネルシャドウスタックを有効にしています。macOS / Linux からのクロスコンパイルに対応。

## ビルド

```bash
cd examples/windows-driver-cet
make
```

スタンドアロンの NeverC リリースから：

```bash
make NEVERC=/path/to/neverc
```

出力は `CetDriver.sys`（auto-LTO 最適化済み）。
デフォルトビルドにはデバッグ用の `-g` が含まれます。**リリースビルドでは `-g` を削除**
してデバッグシンボルを除去し、バイナリサイズを削減してください。

## CET 専用フラグ

| フラグ | レイヤー | 目的 |
|--------|---------|------|
| `-fcf-protection=return` | コンパイラ | シャドウスタック互換コードを生成 |
| `-Xlinker --cetcompat` | リンカー | PE に `IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT` を設定 |

## 手動ビルド（Make なし）

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fcf-protection=return \
  -fms-kernel -fms-extensions -fms-compatibility \
  -D_AMD64_ -DNTDDI_VERSION=0x06010000 -D_WIN32_WINNT=0x0601 \
  -Wall -nostdlib -shared \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -Xlinker --cetcompat \
  -lntoskrnl -lhal \
  -o CetDriver.sys driver.c
```

## 機能

- `\Device\CetDriver` にデバイスオブジェクトを作成
- `\DosDevices\CetDriver` にシンボリックリンクを作成
- 間接呼び出し（`ComputeFn` 関数ポインタ）で CET 互換性を検証——シャドウスタックがこれらの呼び出しのリターンアドレスを保護
- `DbgPrint` 経由でロード/アンロードメッセージを出力

---

## CET 技術詳細

CET には**2つの独立した保護メカニズム**があります：

### 1. シャドウスタック — 後方エッジ保護（RET）

ハードウェアが CALL/RET をミラーする第2のスタック（シャドウスタック）を維持します。
**関数エントリに特別な命令は不要**——完全に透過的です：

```
┌─ CALL target ─────────────────────────────────┐
│                                                │
│  通常スタック:   PUSH return_addr  (RSP)        │
│  シャドウスタック: PUSH return_addr  (SSP, HW)   │
│                                                │
└────────────────────────────────────────────────┘

┌─ RET ─────────────────────────────────────────┐
│                                                │
│  通常スタック:   POP return_addr_A  (RSP)       │
│  シャドウスタック: POP return_addr_B  (SSP, HW)  │
│                                                │
│  比較: return_addr_A == return_addr_B ?          │
│    ✓ 一致   → 正常復帰                          │
│    ✗ 不一致 → #CP 例外 (BUGCHECK)               │
│                                                │
└────────────────────────────────────────────────┘
```

シャドウスタック管理命令（OS がコンテキストスイッチに使用、関数ヘッダには配置しない）：

```asm
RDSSPQ  rax         ; 現在のシャドウスタックポインタを読み取り
INCSSPQ rax         ; SSP を進める（エントリを破棄）
SAVEPREVSSP         ; 前のシャドウスタックトークンを保存
RSTORSSP [addr]     ; 保存済みシャドウスタックに復元
WRSS  [addr], rax   ; スーパーバイザシャドウスタックに書き込み
WRUSS [addr], rax   ; ユーザシャドウスタックに書き込み（ring 0 のみ）
SETSSBSY            ; 現在のシャドウスタックをビジーに設定
CLRSSBSY [addr]     ; ビジーフラグをクリア
```

### 2. 間接分岐追跡（IBT） — 前方エッジ保護（間接 CALL/JMP）

有効な間接呼び出し/ジャンプターゲットごとに `ENDBR64` 命令（`F3 0F 1E FA`、4バイト）
が必要です。CET 非対応 CPU では `ENDBR64` は NOP として動作します。

```
┌─ 間接 CALL/JMP ──────────────────────────────┐
│                                               │
│  CPU 内部 TRACKER = WAIT_FOR_ENDBR に設定     │
│  ターゲットアドレスにジャンプ...                 │
│                                               │
│  ターゲットの最初の命令が ENDBR64 ?              │
│    ✓ はい → TRACKER クリア、正常実行             │
│    ✗ いいえ → #CP 例外                         │
│                                               │
│  直接 CALL/JMP は TRACKER を設定しない          │
│                                               │
└───────────────────────────────────────────────┘
```

### Windows カーネルの選択

| 保護 | メカニズム | Windows カーネルで使用？ |
|------|-----------|------------------------|
| 後方エッジ（RET） | CET シャドウスタック | **はい** (KCET) |
| 前方エッジ（間接 CALL/JMP） | CET IBT (ENDBR) | **いいえ** — 代わりに CFG を使用 |

そのためデフォルトは `-fcf-protection=return`：シャドウスタックのみ、ENDBR64 は生成しません。
ENDBR64 が必要な場合は `-fcf-protection=full` に変更してください（Windows では無害な NOP ですが、Linux 移植時の互換性を提供します）。

### アセンブリ比較：`-fcf-protection` の各モード

ソースコード：

```c
unsigned long rotate13(unsigned long val) {
    return (val << 13) | (val >> 19);
}
```

#### `-fcf-protection=none`（CET なし）

```asm
rotate13:
    mov  eax, ecx
    rol  eax, 13
    ret
```

#### `-fcf-protection=return`（シャドウスタックのみ——本サンプルはこのモードを使用）

```asm
rotate13:
    mov  eax, ecx      ; "none" と完全に同一！
    rol  eax, 13        ; シャドウスタックは完全に透過的——
    ret                 ; ハードウェアが CALL/RET 時に自動的に操作
```

コード生成は `none` と**完全に同一**です。唯一の違いはリンカーフラグ `--cetcompat` が
PE デバッグディレクトリに `IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT` ビットを設定し、
このバイナリがシャドウスタック安全であることを Windows に伝えることです。

#### `-fcf-protection=full`（シャドウスタック + IBT）

```asm
rotate13:
    endbr64             ; ← IBT マーカー (F3 0F 1E FA)
    mov  eax, ecx       ;    非 CET CPU では NOP
    rol  eax, 13        ;    Windows では未使用（CFG が前方エッジを処理）
    ret
```

`ENDBR64` がすべての関数エントリに出現します。Windows では関数あたり 4 バイトの無駄ですが、問題は発生しません。

---

## ターゲットマシンでの KCET 有効化

```cmd
reg add HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity /v Enabled /t REG_DWORD /d 1 /f
reg add HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\KernelShadowStacks /v Enabled /t REG_DWORD /d 1 /f
```

再起動が必要です。`msinfo32.exe` → 「カーネルモードのハードウェアによるスタック保護」で確認してください。

**要件：**

- ターゲットマシンで HVCI が有効
- Windows ビルド 21389 以降
- CET 対応 CPU（Intel Tiger Lake+ / AMD Zen 3+）

## ロード

```cmd
sc create CetDriver type= kernel binPath= C:\path\to\CetDriver.sys
sc start CetDriver
sc stop CetDriver
sc delete CetDriver
```

テスト署名を有効にするか、本番環境用のコード署名証明書を使用してください。
