**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android カーネル Probe

`neverc_krt_probe_register` を使用して `do_faccessat` 内部の任意の命令（エントリポイントではない）をフックします。デモ内容：

- **任意アドレスフック**：関数エントリだけでなく、任意の命令をフック可能
- **完全レジスタコンテキスト**：`neverc_krt_reg_ctx` で全 GPR を読み書き
- **自動チェーン**：同一アドレスに複数のハンドラを優先度順に実行
- **制御フロー**：`NEVERC_KRT_CTX_SKIP` で中止、`NEVERC_KRT_CTX_REDIRECT` でリダイレクト

## API

```c
int neverc_krt_probe_register(void *addr, neverc_krt_ctx_handler_t handler,
                              int priority, struct neverc_krt_probe_ref *ref);
int neverc_krt_probe_unregister(struct neverc_krt_probe_ref *ref);
```

ハンドラシグネチャ：

```c
void my_probe(neverc_krt_reg_ctx *ctx);
```

## ビルド

```bash
cd examples/android-kernel-probe
neverc make
```

`KERNEL` を `515`、`601`、`606`、`612` に変更して他のカーネルバージョンに対応。

## デプロイと実行

```bash
neverc make run
```

または手動：

```bash
adb push nvk_probe.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_probe.ko'
adb shell su -c 'dmesg | grep neverc_krt_probe_demo'
```

## アンロード

```bash
neverc make rmmod
```
