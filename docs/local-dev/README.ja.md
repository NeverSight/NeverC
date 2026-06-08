**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← ドキュメント索引](../README.ja.md)

# ローカル開発

NeverC をソースからビルドし、ローカル開発環境をセットアップするガイドです。

---

## 前提条件

- CMake 3.20+
- Ninja
- C++17 ホストコンパイラ（GCC、Clang、または MSVC）

---

## ビルド

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake
cmake --build build-neverc --target neverc
```

`ccache` / `sccache` が検出されると自動的に有効化されます。

### テスト付きビルド

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake -DNEVERC_INCLUDE_TESTS=ON
cmake --build build-neverc --target check-neverc
```

---

## PATH の設定（macOS / Linux）

ビルド後、`neverc` バイナリは `build-neverc/bin/neverc` にあります。ヘルパースクリプトを使って `PATH` に追加すれば、毎回フルパスを入力する必要がなくなります：

```bash
source ./tools/neverc-env.sh
```

これで `neverc` を直接実行できます：

```bash
neverc --version
neverc -c hello.c -o hello.o
```

### PATH から削除

ローカルビルドを `PATH` から外す場合は、同じシェルセッションで以下を実行します：

```bash
source ./tools/neverc-env.sh --remove   # または -r
```

### 永続化

`source` 行をシェルの rc ファイル（`~/.zshrc`、`~/.bashrc`、または `~/.profile`）に自動追記します：

```bash
source ./tools/neverc-env.sh --install
```

取り消し：

```bash
source ./tools/neverc-env.sh --uninstall
```

---

## Windows (CMD)

Windows では `.bat` スクリプトを使用します（管理者権限不要）：

```cmd
tools\neverc-env.bat             &REM PATH に追加（現在のセッション）
tools\neverc-env.bat --remove    &REM PATH から削除（現在のセッション）
tools\neverc-env.bat --global    &REM setx でユーザー PATH に永続化
tools\neverc-env.bat --global -r &REM setx でユーザー PATH から削除
```

Unix スクリプトとは異なり、`source` は不要です — `.bat` は現在の `cmd` セッションを直接変更します。`--global` は `setx` を使用してユーザーレベルのレジストリに書き込みます（管理者権限不要）。

---

## macOS プリビルドバイナリ

リリースは Apple Developer ID 証明書で署名され、Apple による公証済みです。アーカイブを展開してそのまま使用できます。

---

## Windows へのクロスコンパイル

NeverC は `runtime/` に Windows SDK と WDK を同梱しており、外部 SDK の設定は不要です。

```bash
neverc --target=x86_64-pc-windows-msvc \
  -fbuiltin-string -o hello.exe hello.c -lkernel32
```

Windows shellcode（`-fshellcode`、PEB インポート解決など）については [shellcode コンパイラドキュメント](../shellcode-compiler/README.ja.md)を参照してください。

---

## 動作確認

```bash
neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
neverc -c /tmp/hello.c -o /tmp/hello.o
```
