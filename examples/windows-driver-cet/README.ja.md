**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

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

## コンパイラ vs bin2bin: CET にフレンドリーなのはどちらか？

CET は**ソースレベルのコンパイラ**と **bin2bin ツール**（パッカー、難読化器、
フッカー、dump+rebuild）の間に明確な境界線を引きます。ハードウェアの
Shadow Stack は 3 つのルールを強制し、保護 / 難読化業界全体を再構築します：

> 1. **リターンアドレスを変更しないこと。**
> 2. **コードを自己修正しないこと**（HVCI がコードページに W^X を強制）。
> 3. **ルール 1、2 を尊重する強力な難読化変換を探すこと。**

### コンパイラは「リターンアドレスを暗号化」できるか？

**いいえ。** これはよくある誤解です。Shadow Stack は OS ではなく CPU に
よって強制され、ユーザーモードコードには不可視です。関数エピローグで
通常スタック上のリターンアドレスを XOR 暗号化しても：

```c
void my_func() {
    // ... 関数本体 ...
    // エピローグがリターンアドレスを暗号化しようとする：
    // XOR [rsp], 0xDEADBEEF
    // RET           <- ハードウェアが通常スタック vs シャドウスタックを比較
                     //   一致しなくなる -> #CP 例外 -> BUGCHECK
}
```

シャドウスタックは元のリターンアドレスを保持しています。RET はハードウェア
比較をトリガーし、不一致で `#CP` が発火しカーネルが BUGCHECK します。
コンパイラは**シャドウスタックに到達できません**：

- ユーザーモード：シャドウスタックに書き込める命令はない
- カーネルモード：`WRSSQ` は特権命令で、`ntoskrnl` だけが使用する

### コンパイラができる CET フレンドリーな難読化

| 変換 | なぜ CET 安全か |
|------|--------------|
| **コントロールフロー平坦化** | switch ディスパッチャは直接 CALL/JMP を使用；ケースには必要に応じて ENDBR64 |
| **VM ベースの仮想化** | ハンドラ間は間接 JMP（ENDBR64 付き）で接続、push+ret は使用しない |
| **文字列 / 定数暗号化** | 純粋なデータ変換、制御フローに影響なし |
| **MBA 式** | `x + y → (x ^ y) + 2*(x & y)` —— データのみ |
| **不透明述語** | 直接ジャンプによる条件分岐 |
| **関数クローン / インライン化** | 呼び出しスタックのセマンティクスは変わらない |
| **命令置換** | `MOV → XOR + ADD` —— スタックへの影響なし |

### CET に敵対するパターン（KCET 下で死ぬ）

| パターン | なぜ壊れるか |
|----------|------------|
| **リターンアドレス暗号化** | シャドウスタック不一致 → `#CP` |
| **PUSH addr; RET ディスパッチャ**（古典的な VMProtect / Themida スタイル） | 同上 —— シャドウスタックに `addr` のエントリがない |
| **スタックピボット**（ROP ガジェットチェーン） | シャドウスタックはピボットに追従できない |
| **自己修正コード** | HVCI が実行可能ページへの書き込みをブロック |
| **実行時コード生成** | 同上 —— HVCI W^X 違反 |
| **トランポリンベースのインラインフック** | 関数プロローグの変更が HVCI をトリガー；HVCI を回避してもトランポリンの RET でシャドウスタックが壊れる |

### なぜ bin2bin ツールは構造的に不利か

コンパイラはセマンティック IR から CET 正しいコードを生成します。
bin2bin ツールはコンパイル済みバイトからセマンティクスを**再発見**しなければなりません：

1. **命令境界の曖昧性** —— x86 は可変長。誤ったオフセットに ENDBR64（4 バイト）を追加すると、すべての RIP 相対アドレッシングと再配置が壊れる。
2. **間接ターゲット識別** —— bin2bin は `.data` 内のどのアドレスがジャンプテーブルエントリで、どれが生データかを常に判別できない。過剰マーキング（コード肥大化、新しい ROP ガジェットの起点）するか、マーキング不足（実行時 `#CP`）になる。
3. **自己証明の危険** —— `IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT` の設定は約束です。bin2bin 出力が CET 敵対パターンを含むと、ドライバは非 CET マシンでは正常にロードされるが、KCET ホストでは即座に BSOD する。
4. **CFG の完全性** —— コンパイラは完全な呼び出しグラフを見る；bin2bin は推測しなければならず、正確なターゲットがない間接呼び出しは保守的な ENDBR 配置を強制する。

### 業界の現状

| ツール / クラス | CET 状態 |
|----------------|---------|
| **NeverC / Clang / MSVC（コンパイラ）** | `-fcf-protection` + リンカーフラグでネイティブに CET フレンドリー |
| **OLLVM / Tigress / NeverC パス** | IR レベル変換 → 自然に CET 安全 |
| **Microsoft Detours (4.0+)** | CET 互換に更新済み |
| **VMProtect / Themida（旧版）** | Push+RET ディスパッチャが KCET ホスト上のドライバを殺す |
| **VMProtect / Themida（新版）** | ENDBR 対応ディスパッチャを追加中、サポートはまちまち |
| **Manual map / dump+rebuild ローダ** | すべての ENDBR マーカーを再構築する必要あり —— エラーが起きやすい |

### ゲームセキュリティの観点

アンチチートドライバ（EAC、BattlEye、FACEIT AC、Vanguard）は出荷時に
`--cetcompat` が設定されているため、KCET 有効マシンで正常に動作します。
チートドライバ —— 通常 bin2bin ツールでパッキング、フック、またはトランポリン
注入される —— は CET 準拠を維持するのが困難です。KCET + HVCI は
**「コンパイラフレンドリー、bin2bin 敵対」のハードウェアウォール**を形成し、
うまく設計されたセキュリティソフトウェアにマルウェアスタイルのコードに対する
非対称な優位性を与えます。

これが Microsoft がカーネルソフトウェアに対し KCET を強く推進する深い理由です：
正規のカーネルコードを強化しやすくしつつ、攻撃者の技術を徐々により困難にします。

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
