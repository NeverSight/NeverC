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

`--target neverc` は日常の stage-1 ビルド（埋め込み runtime は空のプレースホルダ）
で、ほとんどのローカルコンパイル／デバッグに十分です。バイナリ自体に
string / mimalloc / std / NVK runtime を埋め込む場合（または CI 相当の
コンパイラが必要な場合）は、stage-2 の傘ターゲットを実行します：

```bash
cmake --build build-neverc --target neverc-embed-runtime-bitcode
```

二段階ブートストラップの詳細は [Builtins](../builtins/README.ja.md) を参照してください。

### テスト付きビルド

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake -DNEVERC_INCLUDE_TESTS=ON
cmake --build build-neverc --target check-neverc
```

`check-neverc` は `neverc-embed-runtime-bitcode` に依存するため、初回のテスト実行前に
自動でブートストラップとコンパイラの再リンクが行われます。embed ターゲットを
手で実行する必要はありません。

---

## PATH の設定（macOS / Linux）

ビルド後、`neverc` バイナリは `build-neverc/bin/neverc` にあります。ヘルパースクリプトを使って `PATH` に追加すれば、毎回フルパスを入力する必要がなくなります：

```bash
source ./utils/build/neverc-env.sh
```

これで `neverc` を直接実行できます：

```bash
neverc --version
neverc -c hello.c -o hello.o
```

### PATH から削除

ローカルビルドを `PATH` から外す場合は、同じシェルセッションで以下を実行します：

```bash
source ./utils/build/neverc-env.sh --remove   # または -r
```

### 永続化

`source` 行をシェルの rc ファイル（`~/.zshrc`、`~/.bashrc`、または `~/.profile`）に自動追記します：

```bash
source ./utils/build/neverc-env.sh --install
```

取り消し：

```bash
source ./utils/build/neverc-env.sh --uninstall
```

### ローカル開発版 / リリース版の切り替え

release（デフォルト: `~/.neverc`）とソースツリー内のビルドの両方がある場合、`neverc-env.sh` で現在のシェル内のアクティブな `neverc` を切り替えられます。どちらのインストールも上書きされません：

```bash
source ./utils/build/neverc-env.sh              # ローカル開発版（build-neverc/bin）
source ./utils/build/neverc-env.sh --local      # 同上
source ./utils/build/neverc-env.sh --release    # リリース版（~/.neverc/bin）
source ./utils/build/neverc-env.sh --status     # 使用中の neverc を表示
source ./utils/build/neverc-env.sh --remove     # 両方を PATH から削除
```

切り替え後、`NEVERC_ENV` が `local` または `release` に設定されます：

```bash
echo "$NEVERC_ENV"
neverc --version
which neverc
```

release を別の prefix にインストールした場合は、`install.sh` と同じディレクトリを指定します：

```bash
NEVERC_INSTALL_DIR=$HOME/.neverc-v3389.1.2 source ./utils/build/neverc-env.sh --release
```

任意：シェル設定にエイリアスを追加（パスをリポジトリの絶対パスに置き換え）：

```bash
alias neverc-dev='source /path/to/NeverC/utils/build/neverc-env.sh --local'
alias neverc-rel='source /path/to/NeverC/utils/build/neverc-env.sh --release'
```

---

## Windows (CMD)

Windows では `.bat` スクリプトを使用します（管理者権限不要）：

```cmd
utils\build\neverc-env.bat             &REM PATH に追加（現在のセッション）
utils\build\neverc-env.bat --remove    &REM PATH から削除（現在のセッション）
utils\build\neverc-env.bat --global    &REM setx でユーザー PATH に永続化
utils\build\neverc-env.bat --global -r &REM setx でユーザー PATH から削除
```

Unix スクリプトとは異なり、`source` は不要です — `.bat` は現在の `cmd` セッションを直接変更します。`--global` は `setx` を使用してユーザーレベルのレジストリに書き込みます（管理者権限不要）。

---

## macOS プリビルドバイナリ

リリースは Apple Developer ID 証明書で署名され、Apple による公証済みです。アーカイブを展開してそのまま使用できます。

---

## Windows へのクロスコンパイル

NeverC は `runtime/` に各プラットフォーム SDK（Windows SDK/WDK、Linux sysroot、macOS sysroot、Android NDK）を同梱しており、外部 SDK の設定は不要です。

```bash
neverc --target=x86_64-pc-windows-msvc \
  -fbuiltin-string -o hello.exe hello.c -lkernel32
```

Windows dyncode（`-fdyncode`、PEB インポート解決など）については [dyncode コンパイラドキュメント](../dyncode-compiler/README.ja.md)を参照してください。

---

## 動作確認

```bash
neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
neverc -c /tmp/hello.c -o /tmp/hello.o
```
