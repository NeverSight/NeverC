**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC サンプル](../../docs/examples/README.ja.md)

# Android カーネル完全 SDK デモ

完全 SDK 統合 — すべての NVK サブシステムを初期化し、netlink コマンドインターフェースで公開。プロダクションモジュールのリファレンス実装。interpose エンジン、資格情報ラッパー、モジュール可視性、SELinux ポリシー制御、プロセス列挙、VMA 検査、ファイル I/O、環境検出、統計を網羅。

## ビルド

```bash
cd examples/android-kernel-full
neverc make
```

他のカーネルバージョンには `KERNEL` を `515`、`601`、`606`、`612` に変更してください。

## デプロイと実行

```bash
neverc make run
```

または手動で：

```bash
adb push nvk_full.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_full.ko'
adb shell su -c 'dmesg | grep neverc_krt_full'
```

## アンロード

```bash
neverc make rmmod
```

または手動で：

```bash
adb shell su -c 'rmmod neverc_krt_full'
```
