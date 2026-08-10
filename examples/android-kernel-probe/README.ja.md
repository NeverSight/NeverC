**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC サンプル](../../docs/examples/README.ja.md)

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
neverc make          # debug: -g（初回ビルドの既定値）
neverc make release  # release: -O2 --strip
neverc make debug    # debug に戻す
```

別のカーネルプリセットは、たとえば
`neverc make KERNEL=612 release` で選択します。`neverc make release` は
`-O2 --strip` を選びます。Makefile は選択した `KERNEL` と `PROFILE` を
`.nvk-build-flags` に記録するため、以後の `make push`、`make run`、
ターゲットなしの `make` は同じ成果物を使います。この状態ファイルがなければ、
`make` の既定値は debug です。`make debug` または明示的な `PROFILE=...` は
保存済みのプロファイルを更新し、`make clean` は状態ファイルを削除して次の
ビルドを debug に戻します。

NeverC は、IDA に着想を得つつ予約接頭辞を使わないリリース名を 5 種類
書き込みます。関数は `fn_HEX`、実行可能な無型ラベルは `code_HEX`、オブジェクトは
`obj_HEX`、その他の無型ラベルは `sym_HEX`、絶対シンボルは `abs_HEX` です。
通常の割り当て済み定義では、`HEX` は最終的な `SHF_ALLOC` セクション配置から
決定的に算出した `analysis EA` です（`abs_HEX` は代わりに絶対 `st_value` を
使います）。これは hash（ハッシュ）、encryption（暗号化）、file offset
（ファイルオフセット）、ELF virtual address（ELF 仮想アドレス）、runtime kernel
address（実行時カーネルアドレス）のいずれでもありません。NeverC は予約済みの
`sub_`/`loc_` 形式も、意図的に空にした通常名も保存しません。

正確に保持する名前、IDA の合成 `extern` 表示、セキュリティ境界、最終処理と署名の
順序については、
[リリースとストリップ方針](../../docs/release-builds/README.ja.md)を参照してください。

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
adb shell su -c 'insmod /data/local/tests/nvk_probe.ko'
```

ロード直後の新しい行が端末 1 に流れます。Ctrl+C で停止します。

補足：Android によっては `dmesg -w` が使えません。`/proc/kmsg` は root が必要ですが、モジュール読み込みのデバッグには安定しています。

## アンロード

```bash
neverc make rmmod
```
