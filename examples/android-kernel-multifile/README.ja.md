**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC サンプル](../../docs/examples/README.ja.md)

# Android カーネル マルチファイルモジュール

マルチファイル NeverC カーネルモジュールのデモ。ポイント：

- **シングルブートストラップ**：`NEVERC_KRT_BOOTSTRAP()` は `module_init` で一度だけ呼び出し
- **共有状態**：コンパイラが全ての `neverc_krt_*` 状態を `weak_odr` リンケージに昇格し、全 `.c` ファイルが同じリゾルバ、キャッシュ、サブシステム状態を共有
- **分割アーキテクチャ**：`main.c`（初期化/終了）、`interposes.c`（フックロジック）、`utils.c`（ヘルパー）

## ビルド

```bash
cd examples/android-kernel-multifile
neverc make
```

`KERNEL` を `515`、`601`、`606`、`612` に変更して他のカーネルバージョンに対応。

## デプロイと実行

```bash
neverc make run
```

または手動：

```bash
adb push nvk_multi.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_multi.ko'
adb shell su -c 'dmesg | grep neverc_krt_multi'
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
adb shell su -c 'insmod /data/local/tests/nvk_multi.ko'
```

ロード直後の新しい行が端末 1 に流れます。Ctrl+C で停止します。

補足：Android によっては `dmesg -w` が使えません。`/proc/kmsg` は root が必要ですが、モジュール読み込みのデバッグには安定しています。

## アンロード

```bash
neverc make rmmod
```
