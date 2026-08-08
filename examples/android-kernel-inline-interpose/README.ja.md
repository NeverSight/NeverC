**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC サンプル](../../docs/examples/README.ja.md)

# Android カーネル関数フック

`neverc_krt_interpose_register` を使用して `do_faccessat` のエントリポイントをフックします。デモ内容：

- **自動チェーン**：同一ターゲットに複数のハンドラを優先度順に実行
- **オリジナル呼び出しパターン**：ハンドラは `orig` ポインタを受け取り、元の関数を呼び出し可能
- **優先度制御**：値が小さいほど先に実行。負の値で他のフックより先に実行
- **共存**：ターゲットが既に他のモジュールにフックされていても動作

## API

```c
int neverc_krt_interpose_register(void *target, void *handler, int priority,
                             void **orig, struct neverc_krt_interpose_ref *ref);
int neverc_krt_interpose_unregister(struct neverc_krt_interpose_ref *ref);
```

ハンドラシグネチャ：

```c
long my_interpose(void *orig, void *a0, void *a1, void *a2, void *a3, void *a4, void *a5);
```

## ビルド

```bash
cd examples/android-kernel-inline-interpose
neverc make
```

`KERNEL` を `515`、`601`、`606`、`612` に変更して他のカーネルバージョンに対応。

## デプロイと実行

```bash
neverc make run
```

または手動：

```bash
adb push nvk_interpose_demo.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_interpose_demo.ko'
adb shell su -c 'dmesg | grep neverc_krt_interpose_demo'
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
adb shell su -c 'insmod /data/local/tests/nvk_interpose_demo.ko'
```

ロード直後の新しい行が端末 1 に流れます。Ctrl+C で停止します。

補足：Android によっては `dmesg -w` が使えません。`/proc/kmsg` は root が必要ですが、モジュール読み込みのデバッグには安定しています。

## アンロード

```bash
neverc make rmmod
```
