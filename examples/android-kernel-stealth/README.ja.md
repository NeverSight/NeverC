**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android カーネル ステルス

モジュール隠蔽デモ。コンパイルフラグ：なし=基本リスト非表示、`-DNVK_STEALTH_HIDE`=完全非表示（リスト+sysfs+proc）、`-DNVK_STEALTH_FULL_HIDE`=拡張（dmesg+PID+マウント+maps）、`-DNVK_STEALTH_ROOT`=root権限付与、`-DNVK_STEALTH_SELINUX`=permissive設定。

## ビルド

```bash
cd examples/android-kernel-stealth
neverc make
```

他のカーネルバージョンには `KERNEL` を `515`、`601`、`606`、`612` に変更してください。

## デプロイと実行

```bash
neverc make run
```

または手動で：

```bash
adb push nvk_stealth.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_stealth.ko'
adb shell su -c 'dmesg | grep nvk_stealth'
```

## アンロード

```bash
neverc make rmmod
```

または手動で：

```bash
adb shell su -c 'rmmod nvk_stealth'
```
