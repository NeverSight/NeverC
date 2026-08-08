**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← ドキュメント索引](../README.ja.md) · [← NeverC プロジェクト](../../docs/i18n/README.ja.md)

# NeverC サンプル

NeverC のクロスプラットフォームコンパイル機能を示すビルド可能なサンプル集。すべて macOS / Linux からクロスコンパイル可能 — Windows ビルド環境不要。

---

## サンプル一覧

### サーバーバックエンド

| サンプル | 説明 | 主要機能 |
|---------|------|---------|
| [権威型ゲームサーバー](../../examples/network-authoritative-server/README.ja.md) | クロスプラットフォームゲームバックエンド | 固定 60 Hz tick、TCP セッション、UDP/QUIC 入力、リプレイ保護 |
| [アンチチートコレクター](../../examples/network-anticheat-collector/README.ja.md) | 強化テレメトリ取り込み | mTLS、ストリーミング NRPC、HMAC テレメトリ、上限付き監査パイプライン |

### Windows

| サンプル | 説明 | 主要機能 |
|---------|------|---------|
| [Windows カーネルドライバ](../../examples/windows-driver/README.ja.md) | 最小 WDM カーネルドライバ | **x64**（デフォルト）と **ARM64** 向けに `.sys` をクロスコンパイル、自動 LTO、内蔵リンカ |
| [Windows ドライバ + CET](../../examples/windows-driver-cet/README.ja.md) | Intel CET シャドウスタック付きカーネルドライバ | CET 対応カーネルコード（**x64 のみ**）、`/guard:ehcont` |
| [Windows ドライバ + 浮動小数点](../../examples/windows-driver-float/README.ja.md) | 浮動小数点/SIMD 付きカーネルドライバ | **x64** と **ARM64** でのカーネルモード安全浮動小数点 |
| [Windows Ring3 EXE](../../examples/windows-exe/README.ja.md) | ユーザーモードコンソールアプリ | GetSystemInfo、プロセス列挙、VirtualAlloc |
| [Windows Ring3 DLL](../../examples/windows-dll/README.ja.md) | ユーザーモード DLL | ReadProcessMemory、VirtualAllocEx、モジュール列挙 |

### Linux

| サンプル | 説明 | 主要機能 |
|---------|------|--------|
| [Linux Hello World](../../examples/linux-hello/README.ja.md) | 最小限の C プログラム | macOS/Windows からのクロスコンパイル |
| [Linux POSIX](../../examples/linux-posix/README.ja.md) | POSIX システムプログラミング | pthreads、mmap、pipe、シグナル |
| [Linux 静的](../../examples/linux-static/README.ja.md) | 完全静的バイナリ | `-static` リンク |
| [Linux ネットワーク](../../examples/linux-network/README.ja.md) | TCP ソケットデモ | クライアント/サーバー |
| [Linux 数学 + zlib](../../examples/linux-math/README.ja.md) | 数学 + 圧縮 | 三角関数、zlib、CRC32 |

### macOS

| サンプル | 説明 | 主要機能 |
|---------|------|---------|
| [macOS アプリケーション](../../examples/macos-app/README.ja.md) | ネイティブ Mach-O 実行ファイル | sysctl、uname、Mach host_info/task_info、プロセス情報 |
| [macOS ダイナミックライブラリ](../../examples/macos-dylib/README.ja.md) | ネイティブ `.dylib` ライブラリ | Mach vm_read/vm_write、vm_alloc/vm_dealloc、task_info、XOR |

### Android

| サンプル | 説明 | 主要機能 |
|---------|------|---------|
| [Android ELF](../../examples/android-elf/README.ja.md) | root 端末向けネイティブ ARM64 バイナリ | Android クロスコンパイル、dlopen/liblog、/proc 情報、root 検出 |
| [Android 共有ライブラリ](../../examples/android-so/README.ja.md) | ネイティブ ARM64 `.so` ライブラリ | 共有ライブラリ、mmap RWX、XOR 暗号化 |

### Android カーネルモジュール (.ko)

カーネルソースツリー不要 — NeverC は組み込みの最小ランタイムに対してコンパイルします。単一ソースで GKI 5.10–6.12 をカバー。

| サンプル | 説明 | 主要機能 |
|---------|------|---------|
| [カーネル Hello](../../examples/android-kernel-hello/README.ja.md) | 最小 `.ko` モジュール | kprobe 経由の kallsyms ブートストラップ、最小限の insmod 検証 |
| [カーネルドライバテンプレート](../../examples/android-kernel-driver/README.ja.md) | 動的シンボル解決テンプレート | `kallsyms_lookup_name`、GKI 安定 ABI、5.10–6.12 |
| [カーネルインラインフック](../../examples/android-kernel-inline-interpose/README.ja.md) | `do_faccessat` のインラインフック | BTI/PAC セーフパッチ、コンテキストフックモード、PC 相対リロケーション |
| [カーネル Syscall フック](../../examples/android-kernel-syscall-interpose/README.ja.md) | syscall テーブル / inline / context interpose | `sys_call_table` 置換、インラインフック、コンテキストフック |
| [カーネル低可視性](../../examples/android-kernel-lowvis/README.ja.md) | モジュール可視性管理 | list/sysfs/proc 可視性、資格情報ラッパー、SELinux 強制状態 |
| [カーネル Full SDK](../../examples/android-kernel-full/README.ja.md) | 完全 SDK 統合 | Netlink IPC、interpose、資格情報ラッパー、モジュール可視性、SELinux ポリシー制御、VMA、ファイル I/O |
| [カーネル Chardev](../../examples/android-kernel-chardev/README.ja.md) | キャラクタデバイス + ioctl | `misc_register`、ioctl ディスパッチ、`/proc` seq_file |
| [カーネル Netlink](../../examples/android-kernel-netlink/README.ja.md) | 双方向 netlink IPC | PING/VERSION/ECHO コマンド、`nvk_nl_open`/`nvk_nl_reply` |
| [カーネル Probe](../../examples/android-kernel-probe/README.ja.md) | 任意の命令をプローブ | `neverc_krt_probe_register`、全レジスタコンテキスト、優先度チェーン、スキップ/リダイレクト |
| [カーネル複数ファイルモジュール](../../examples/android-kernel-multifile/README.ja.md) | 複数ファイルのカーネルモジュール | `NEVERC_KRT_BOOTSTRAP()` は一度だけ、`weak_odr` 共有状態、init/interpose/helper の分割 |

---

## クイックスタート

すべての例は同じパターンに従います：

```bash
cd examples/example-name
neverc make
```

Makefile ドライバ本体は [`neverc build` / `make` →](../build/README.ja.md)。

必要に応じてコンパイラパスを上書きします：

```bash
neverc make NEVERC=/path/to/neverc
```

Windows ドライバの例は `ARCH` でアーキテクチャを選びます（デフォルトは x64）。CET の例は
x64 のみです — CET は x86 の機能です：

```bash
neverc make ARCH=x64        # Build for x64 (default)
neverc make ARCH=arm64      # Build for ARM64
neverc make all-arch        # Build every architecture the example supports
neverc make TESTSIGN=1      # Attach an Authenticode test signature
```

Linux の例はアーキテクチャ選択をサポートします：

```bash
neverc make TARGET=aarch64-linux-gnu   # Build for ARM64
neverc make TARGET=x86_64-linux-gnu    # Build for x86_64 (default)
```

macOS の例はアーキテクチャ選択をサポートします：

```bash
neverc make TARGET=arm64-apple-macos     # Build for Apple Silicon (default)
neverc make TARGET=x86_64-apple-macos    # Build for Intel
```

Android の例はデフォルトで ARM64 を対象とします：

```bash
cd examples/android-elf
neverc make            # Build
neverc make run        # Build + push to device + run via adb
```

---

## クロスプラットフォームのハイライト

- **単一ツールチェーン**：NeverC は前処理、コンパイル、最適化（自動 LTO）、リンクを 1 回の呼び出しで処理します
- **バンドル SDK**：Windows SDK/WDK、Linux sysroot（Ubuntu 22.04）、macOS sysroot（macOS 14）、Android sysroot（NDK r26c, API 21+）が `runtime/` にバンドルされています — 外部依存ゼロ
- **ホスト非依存**：macOS（arm64/x86_64）、Linux（x86_64/aarch64）、Windows から同一コマンドでビルド
- **マルチターゲット**：任意のホストから Windows PE（`.sys`/`.exe`/`.dll`）、Linux ELF、macOS Mach-O（`.dylib`）、Android ELF へクロスコンパイル
- **デバッグ対応**：`-g` を渡すと DWARF デバッグ情報を埋め込み、`llvm-dwarfdump` で検査可能
