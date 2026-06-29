**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android カーネル関数フック

`neverc_krt_hook_register` を使用して `do_faccessat` のエントリポイントをフックします。デモ内容：

- **自動チェーン**：同一ターゲットに複数のハンドラを優先度順に実行
- **オリジナル呼び出しパターン**：ハンドラは `orig` ポインタを受け取り、元の関数を呼び出し可能
- **優先度制御**：値が小さいほど先に実行。負の値で他のフックより先に実行
- **共存**：ターゲットが既に他のモジュールにフックされていても動作

## API

```c
int neverc_krt_hook_register(void *target, void *handler, int priority,
                             void **orig, struct neverc_krt_hook_ref *ref);
int neverc_krt_hook_unregister(struct neverc_krt_hook_ref *ref);
```

ハンドラシグネチャ：

```c
long my_hook(void *orig, void *a0, void *a1, void *a2, void *a3, void *a4, void *a5);
```

## ビルド

```bash
cd examples/android-kernel-inline-hook
neverc make
```

`KERNEL` を `515`、`601`、`606`、`612` に変更して他のカーネルバージョンに対応。

## デプロイと実行

```bash
neverc make run
```

または手動：

```bash
adb push nvk_hook_demo.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_hook_demo.ko'
adb shell su -c 'dmesg | grep neverc_krt_hook_demo'
```

## アンロード

```bash
neverc make rmmod
```
