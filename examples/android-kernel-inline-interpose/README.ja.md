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
neverc make          # debug: -g（初回ビルドの既定値）
neverc make release  # release: -O2 --strip
neverc make debug    # debug に戻す
```

別のカーネルプリセットは、たとえば `neverc make KERNEL=612 release` で選択
します。Makefile は `KERNEL` と `PROFILE` の両方を保存するため、その後の
`make push`/`run` が別プロファイルへ暗黙に戻ることはありません。

release のストリップは NeverC 内蔵で、カーネルモジュール向けに制限されて
います。DWARF、`.comment`、再配置から不要な private/未定義シンボル名を
削除しつつ、ET_REL のシンボル表／文字列表、再配置、import、global 定義、
`__versions`、`.codetag.alloc_tags` などのローダー ABI は保持します。
strip-all や難読化ではないため、再配置に必要な名前は残り得ます。署名する
場合は先にストリップし、最終バイト列へ署名してください。`clean` で
ストリップしたり、`.ko` に `llvm-strip --strip-all` を使ったり、
`.codetag.alloc_tags` / `__codetag_*` を安易に削除してはいけません。

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
