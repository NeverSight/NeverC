**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android カーネル Syscall フック

`openat` のシステムコールテーブル置換。デフォルト：テーブルエントリ交換。`-DNVK_SYSCALL_INLINE_HOOK` 付き：ハンドラ関数プロローグをパッチ。`nvk_syscall_replace`/`nvk_syscall_restore` と arm64 システムコール番号定義を実演。

## ビルド

```bash
cd examples/android-kernel-syscall-hook
neverc make
```

他のカーネルバージョンには `KERNEL` を `515`、`601`、`606`、`612` に変更してください。

## デプロイと実行

```bash
neverc make run
```

または手動で：

```bash
adb push nvk_syscall_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_syscall_hook.ko'
adb shell su -c 'dmesg | grep nvk_syscall_hook'
```

## アンロード

```bash
neverc make rmmod
```

または手動で：

```bash
adb shell su -c 'rmmod nvk_syscall_hook'
```
