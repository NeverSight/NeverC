**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← ドキュメント索引](../README.ja.md) · [← NeverC プロジェクト](../../docs/i18n/README.ja.md)

# NeverC サンプル

NeverC のクロスプラットフォームコンパイル機能を示すビルド可能なサンプル集。すべて macOS / Linux からクロスコンパイル可能 — Windows ビルド環境不要。

---

## サンプル一覧

| サンプル | 説明 | 主要機能 |
|---------|------|---------|
| [Windows カーネルドライバ](../../examples/windows-driver/README.ja.md) | 最小 WDM カーネルドライバ | macOS/Linux から `.sys` をクロスコンパイル、自動 LTO、内蔵リンカ |
| [Windows ドライバ + CET](../../examples/windows-driver-cet/README.ja.md) | Intel CET シャドウスタック付きカーネルドライバ | CET 対応カーネルコード、`/guard:ehcont` |
| [Windows ドライバ + 浮動小数点](../../examples/windows-driver-float/README.ja.md) | 浮動小数点/SIMD 付きカーネルドライバ | カーネルモード安全浮動小数点 |
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

---

## クイックスタート

```bash
cd examples/<サンプル名>
neverc make
```

コンパイラパス指定：`neverc make NEVERC=/path/to/neverc`

すべてのサンプルは **neverc** をコンパイラとして使用し、内蔵リンカ経由で Windows PE バイナリ（`.sys`）を生成します。
