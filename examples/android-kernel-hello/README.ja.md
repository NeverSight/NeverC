**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC サンプル](../../docs/examples/README.ja.md)

# Android カーネル Hello

最小の NeverC Android カーネルモジュール（.ko）。kprobe 経由で `kallsyms_lookup_name` をブートストラップし、ロードメッセージを出力して正常終了します。コンパイル→リンク→insmod のエンドツーエンド検証。

## ビルド

```bash
cd examples/android-kernel-hello
neverc make
```

他のカーネルバージョンには `KERNEL` を `515`、`601`、`606`、`612` に変更してください。

## デプロイと実行

```bash
neverc make run
```

または手動で：

```bash
adb push nvk_hello.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_hello.ko'
adb shell su -c 'dmesg | grep nvk_hello'
```

## アンロード

```bash
neverc make rmmod
```

または手動で：

```bash
adb shell su -c 'rmmod nvk_hello'
```
