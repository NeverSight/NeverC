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
adb shell su -c 'dmesg | grep neverc_krt_netlink'
```

## カーネルログ（リアルタイム）

デバイスで `cat /proc/kmsg` を実行すると、カーネル ring buffer をリアルタイムに追跡できます。Windows の **DbgView** に近い使い方です。`insmod` が曖昧なエラーだけを返すときや、vermagic・modversions・section サイズなど本当の拒否理由を確認するときに使います。

端末 1（そのまま実行）：

```bash
adb shell
su
cat /proc/kmsg
```

端末 2：

```bash
adb shell su -c 'insmod /data/local/tests/nvk_netlink.ko'
```

ロード直後の新しい行が端末 1 に流れます。Ctrl+C で停止します。

補足：Android によっては `dmesg -w` が使えません。`/proc/kmsg` は root が必要ですが、モジュール読み込みのデバッグには安定しています。

## アンロード

```bash
neverc make rmmod
```

または手動で：

```bash
adb shell su -c 'rmmod neverc_krt_netlink'
```
