**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← ドキュメント](../README.ja.md)

# C++ を NeverC に変換する

実験的な `neverc translate` は `cpp-core-v1`、`cpp-project-v1`、`cpp-math-v1` でレビュー可能な `.nc` ソースを生成します。

**現在、入力言語として実装されているのは C++ のみです。** E Language（易言語、`.e`）、Python、Go、Rust、TypeScript、JavaScript への対応は今後の計画であり、これらの変換機能はまだ利用できません。

## 準備とスカラー変換

通常の NeverC と標準リソースをインストールすれば利用できます。C++ フロントエンドと承認済み SDK ヘッダーは内蔵されており、Clang の別途インストールは不要です。ビルドの詳細は[フロントエンドの説明](../../utils/translate-frontends/cpp/README.md)を参照してください。

```sh
neverc translate --from cpp input.cpp -o output.nc
neverc output.nc -c -o output.o
```

`cpp-core-v1` は include のない単一の C++17 ソースを受け付けます。`int`、`unsigned int`、`bool`、`void`、単純な集成体型、非メンバー関数、名前空間、オーバーロード、および文書化された制御フローをサポートします。未使用のコードを含め、入力に含まれるすべての宣言を検査します。

## 複数ファイルのプロジェクト

コンパイルデータベースから翻訳単位を明示的に選び、プロジェクトのルートディレクトリを指定します。内蔵フロントエンドは各単位を個別に解析し、マージ処理は定義の完全性、リンケージ、共有型を検査し、単一定義規則（ODR）への適合を保守的に検証します。

```sh
neverc translate --from cpp --profile cpp-project-v1 \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/a.cpp src/b.cpp --out-dir generated
```

出力は `translated.nc` と `translated.h` です。そのルート内にあるプロジェクトのヘッダーだけを受け付けます。同じファイルに複数の構成がある場合は、`--compdb-entry src/a.cpp=4` でデータベースのゼロ始まりの番号を選択します。保存されたコンパイラコマンドはデータとして解析され、実行されません。

## 限定的な倍精度数学演算

`cpp-math-v1` はプロジェクトに `double`、記載された変換・比較、正確なシグネチャの `std::fabs(double)` と `std::floor(double)` を追加します。内蔵の Clang 20.1.8 / libc++ 200100 / macOS 15.5 ヘッダーセットを使い、macOS 15.0 ターゲット（arm64 または x86_64）の明示が必要です。一般的な浮動小数点算術は未対応です。例外トラップをマスクし、非正規化数のゼロ化を無効にする必要があります。標準の四つの丸めモードを検証済みです。

```sh
neverc translate --from cpp --profile cpp-math-v1 \
  --target arm64-apple-macosx15.0.0 \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/math.cpp --out-dir generated-math
```

数学変換ではインストール済みの NeverC 数学ヘッダー、埋め込み実装の識別情報、実際のリンクも検証します。生成した数学モジュールは NeverC ランタイムを使用します。`-fno-builtin-std` 指定時は、最終出力に `fabs`／`floor` の対応付けが必要な場合だけ、ファイル出力前に失敗します。これらの対応付けが不要なコードは引き続き変換できます。

## 検証と出力

出力オプションの代わりに `--check` を使うと、同じ解析、生成、構文およびオブジェクト検証を実行し、生成ファイルを保持しません。`--report PATH` は構造化された診断を書き込みます。変換中に元のプログラムを実行することはありません。

出力と付随ファイルは上書きされません。`--out-dir` には既存の親ディレクトリ内の新しいディレクトリ、`-o` には新しい `.nc` パスが必要です。マニフェストはターゲット要件、入出力ハッシュ、コンパイル手順を記録し、ソースマップは生成行を元の位置に対応付けます。

実行とインストールの検証記録は、ネイティブの macOS arm64 と Rosetta 上の macOS x86_64 を分けて記載しています。Intel Mac 上でのネイティブ実行、Linux、Windows、その他のターゲットへの対応を示すものではありません。完全な C++／STL、ポインター、参照、配列、例外、テンプレート、文字列、`std::vector` は公開されたサポート範囲外です。[サポート表](../../utils/translate-frontends/docs/support-matrix.md)、[プロトコルと復旧規則](../../utils/translate-frontends/docs/protocol.md)、[プロジェクト例](../../tests/neverc/Inputs/translate/cpp/project/)を参照してください。
