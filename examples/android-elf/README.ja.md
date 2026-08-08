**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC サンプル](../../docs/examples/README.ja.md)

# Android ELF サンプル

NeverC を使って Android 向けにクロスコンパイルした ARM64 ネイティブ ELF バイナリです。root 化された Android デバイス上で `adb shell` から直接実行できます。macOS、Windows、Linux からビルド可能——Android NDK や CMake は不要です。

NeverC は `runtime/android/` に Android sysroot（NDK r26c, API 21+）を内蔵しているため、前処理、コンパイル、最適化（自動 LTO）、リンクを一度の呼び出しで完了します。

## ビルド

リポジトリから：

```bash
cd examples/android-elf
neverc make          # debug: -g（初回ビルドの既定）
neverc make release  # release: -O2 --strip
neverc make debug    # debug に戻す
```

Makefile は `PROFILE` を保持するため、以降の `neverc make` でも同じ
debug/release 選択が使われます。release は NeverC 組み込みの `--strip`
で、不要な静的シンボル名とデバッグメタデータを削除しつつ、ローダー/
動的 ABI に必要な名前は残します。詳細は
[リリースビルド](../../docs/release-builds/README.ja.md)。

スタンドアロンの NeverC リリースから：

```bash
neverc make NEVERC=/path/to/neverc
```

## 手動ビルド（Make を使わない場合）

```bash
neverc --target=aarch64-linux-android -Wall -fPIE -lm -ldl -llog -o android-elf main.c
```

## デプロイと実行

adb 経由でデバイスにプッシュして実行：

```bash
neverc make run
```

または手動で：

```bash
adb push android-elf /data/local/tests/
adb shell chmod 755 /data/local/tests/android-elf
adb shell /data/local/tests/android-elf
```

## 機能

- デバイス情報（`uname`）とカーネルバージョンを表示
- root/権限ステータスを確認（`uid`/`euid`、`su` パス）
- `liblog.so` を動的ロードして `__android_log_print` を呼び出し
- `/proc/self/maps` を読み取りメモリレイアウトを表示
- Android 上での `dlopen`/`dlsym`、`readlink`、`fopen` のデモ
