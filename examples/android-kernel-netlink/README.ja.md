**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC サンプル](../../docs/examples/README.ja.md)

# Android カーネル Netlink

双方向 Netlink IPC チャネル。ユーザ↔カーネル通信用の netlink ソケットを作成。PING（PONG応答）、VERSION（カーネルバージョン文字列）、ECHO（ペイロードエコー）に対応。`nvk_nl_open`、`nvk_nl_reply`、ディスパッチコールバックパターンを実演。

## ビルド

```bash
cd examples/android-kernel-netlink
neverc make
```

他のカーネルバージョンには `KERNEL` を `515`、`601`、`606`、`612` に変更してください。

## デプロイと実行

```bash
neverc make run
```

または手動で：

```bash
adb push nvk_netlink.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_netlink.ko'
adb shell su -c 'dmesg | grep nvk_netlink'
```

## アンロード

```bash
neverc make rmmod
```

または手動で：

```bash
adb shell su -c 'rmmod nvk_netlink'
```
