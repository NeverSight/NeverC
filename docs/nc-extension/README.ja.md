**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC ドキュメント](../README.ja.md)

# `.nc` ファイル拡張子

## 概要

NeverC は `.nc` をネイティブソースファイル拡張子として認識します。コンパイラが `.nc` 入力ファイルを検出すると、すべての NeverC 言語拡張を**自動的に有効化**します — 追加フラグは不要です。

## 自動的に有効化される機能

| フラグ | 効果 |
|--------|------|
| `-fneverc-types` | Rust スタイルの整数エイリアス（`u8`、`u16`、`u32`、`u64`、`i8`、`i16`、`i32`、`i64`、`usize`、`isize`） |
| `-fbuiltin-string` | 組み込み `string` 値型、自動メモリ管理、ドットコール構文、UTF-8 サポート |

## 使用方法

ソースファイルに `.nc` 拡張子を付けるだけです：

```bash
# 自動 — -fbuiltin-string や -fneverc-types は不要
neverc hello.nc -o hello

# 以下と同等：
neverc -fneverc-types -fbuiltin-string hello.c -o hello
```

```c
// hello.nc
#include <stdio.h>

int main(void) {
    string greeting = "こんにちは、NeverC！";
    printf("%s (len=%zu)\n", greeting.c_str(), greeting.len);

    u32 x = 42;
    i64 y = -100;

    string msg = greeting + " x=%u, y=%lld".format(x, y);
    printf("%s\n", msg.c_str());
    return 0;
}
```

## 動作原理

検出はコンパイラパイプラインの 2 つのレイヤーで行われます：

### 1. Driver / Toolchain レイヤー

Driver はコンパイラ呼び出しを構築する前に各入力ファイルの拡張子を検査します。`.nc` ファイルの場合、`-fneverc-types` と `-fbuiltin-string` が無条件にコマンドラインに注入されます — ユーザーが手動で渡す必要はありません。

`.c` ファイルの場合、これらのフラグはオプトインのままです：ユーザーが明示的に `-fneverc-types` や `-fbuiltin-string` を渡す必要があります。

### 2. CompilerInvocation レイヤー

安全策として、フロントエンドも呼び出しの解析時に入力ファイルの拡張子をチェックします。いずれかの入力に `.nc` 拡張子がある場合、`LangOpts.NeverCTypes` と `LangOpts.BuiltinString` が `1` に設定され、Driver レイヤーをバイパスした場合（例：`-cc1` の直接呼び出し）でも機能が確実に有効になります。

## 互換性

- `.nc` ファイルは C ソースとして扱われます — 言語は依然として C（デフォルト C23）であり、新しい言語ではありません
- すべての標準 C フラグ（`-std=c11`、`-O2`、`-g`、`-Wall` など）は同じように動作します
- `-fshellcode` は `.nc` と自然に組み合わせられます：shellcode モードはそれ自体で `string` を有効にし、`.nc` は `neverc-types` もアクティブにします
- クロスコンパイル（`-target aarch64-linux-gnu` など）も同様に動作します
- `.c` ファイルは影響を受けません — フラグを明示的に渡さない限り、以前と全く同じ動作をします

## `.nc` と `.c` の使い分け

| シナリオ | 推奨 |
|----------|------|
| `string` と Rust スタイル型を使用する新しい NeverC プロジェクト | `.nc` を使用 |
| 他のコンパイラとの互換性を維持したい既存の C コードベース | `.c` + 明示的フラグを使用 |
| Shellcode プロジェクト | どちらでも可 — `-fshellcode` は `string` を常に有効化 |
| 混合コードベース | NeverC 固有ファイルには `.nc`、ポータブルコードには `.c` |
