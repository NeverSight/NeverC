**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← ドキュメント索引](../README.ja.md) · [← NeverC プロジェクト](../../README.md)

# `neverc build` / `neverc make`

NeverC は埋め込みの **GNU Make 互換** ドライバを同梱します。`neverc build` と
`neverc make` は同一コマンドで、Makefile を読み、変数／関数を展開してレシピを実行します。
[`examples/`](../examples/README.ja.md) はこの流れ向けです。

これは **`neverc.toml` プロジェクトツールではありません**。通常の Make オプションと
`VAR=value` を渡してください。

## 構文

```text
neverc build [options] [target...]
neverc make  [options] [target...]
```

```bash
cd examples/linux-hello
neverc make
neverc make clean
neverc make NEVERC=/path/to/neverc TARGET=aarch64-linux-gnu
```

オプション一覧は `neverc make --help`。

## オプション

| オプション | 意味 |
|------------|------|
| `-f FILE` | 指定 Makefile を読む |
| `-j [N]` | 並列ジョブ（単独 `-j` は CPU 数） |
| `-C DIR` | Makefile 読み込み前に chdir |
| `-n`, `--dry-run` | 実行せず表示 |
| `-k`, `--keep-going` | エラー後も継続 |
| `-s`, `--silent` | レシピをエコーしない |
| `-B`, `--always-make` | 無条件再構築 |
| `-p` | ルール／変数 DB を表示 |
| `VAR=VALUE` | コマンドライン変数 |
| `-h`, `--help` | 使い方 |

## Makefile の探索順

`-f` 省略時: `GNUmakefile` → `makefile` → `Makefile`。

## 対応する Make 機能（要約）

ルール／パターンルール、`.PHONY`、レシピ接頭辞、代入、条件、`include`／`export`、
`subst`／`patsubst`／`wildcard`／`foreach`／`call`／`eval`／`shell` など。
`MAKE_VERSION` は互換のため `4.3`。意図的なサブセットであり完全な GNU Make ではありません。

## 典型的な Makefile

```make
NEVERC ?= neverc
TARGET  = x86_64-linux-gnu
OUTPUT  = hello
SRCS    = main.c

FLAGS = --target=$(TARGET) -O2

all: $(OUTPUT)

$(OUTPUT): $(SRCS)
	$(NEVERC) $(FLAGS) -o $@ $(SRCS)

clean:
	rm -f $(OUTPUT)

.PHONY: all clean
```

クロスコンパイル例では `ARCH=…` や `TARGET=…` をコマンドラインで渡すことが多く、
詳細は [Examples](../examples/README.ja.md)。

## 関連コマンド

| コマンド | 用途 |
|----------|------|
| `neverc file.c -o out` | Makefile なしの単発コンパイル |
| [`neverc run`](../run/README.ja.md) | ホストで一時コンパイル実行 |
| [`neverc runtime`](../runtime/README.ja.md) | クロス用 sysroot の導入 |
| [リリースと `--strip`](../release-builds/README.ja.md) | 配布用に最終イメージを strip |
