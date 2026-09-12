**言語**: [English](../translate.md) | [简体中文](../zh-CN/translate.md) | [繁體中文](../zh-TW/translate.md) | [日本語](translate.md) | [한국어](../ko/translate.md) | [Français](../fr/translate.md) | [Deutsch](../de/translate.md) | [Español](../es/translate.md) | [Italiano](../it/translate.md) | [Русский](../ru/translate.md) | [العربية](../ar/translate.md)

[← ドキュメント](README.md)

# C++ を NeverC に変換する

実験的な `neverc translate` は `cpp-core-v1`、`cpp-core-v2`、`cpp-project-v1`、`cpp-math-v1` でレビュー可能な `.nc` ソースを生成します。

**現在、入力言語として実装されているのは C++ のみです。** E Language（易言語、`.e`）、Python、Go、Rust、TypeScript、JavaScript への対応は今後の計画であり、これらの変換機能はまだ利用できません。

## 準備とスカラー変換

通常の NeverC と標準リソースをインストールすれば利用できます。C++ フロントエンドと承認済み SDK ヘッダーは内蔵されており、Clang の別途インストールは不要です。ビルドの詳細は[フロントエンドの説明](../../utils/translate-frontends/cpp/README.md)を参照してください。

```sh
neverc translate --from cpp input.cpp -o output.nc
neverc output.nc -c -o output.o
```

`cpp-core-v1` は include のない単一の C++17 ソースを受け付けます。`int`、`unsigned int`、`bool`、`void`、単純な集成体型、非メンバー関数、名前空間、オーバーロード、および文書化された制御フローをサポートします。未使用のコードを含め、入力に含まれるすべての宣言を検査します。

`--profile cpp-core-v2` を選択すると、検査済みの `typedef`／`using` 型エイリアス、対応する整数型を基底型とする列挙型、`static_assert`、限定されたオブジェクトポインターと左辺値参照を追加で変換できます。参照引数と参照戻り値は元のオブジェクトを参照し、多段ポインターの `const` とヌルポインターにも対応します。単一ソースと include 禁止の制限は継続します。[core v2 の対応範囲](../../utils/translate-frontends/docs/cpp-core-v2.md)を参照してください。

core v2 は固定長のローカル配列と配列フィールド、多次元の添字アクセス、配列へのポインターと参照にも対応します。部分初期化では残りの要素をゼロで埋め、要素の初期化順序とエイリアス関係を保持します。配列長と初期化の展開量には上限があります。グローバル配列、可変長配列、非自明な要素の生存期間処理は未対応です。

core v2 は C++17 の初期化文、フォールスルー、検証済みの `[[fallthrough]]` 注釈を含む `switch`／`case`／`default` に対応します。条件値の評価は一度だけ行い、入れ子の switch とループでも `break`／`continue` の対象を保持します。GNU の case 範囲とその他の文属性は未対応です。

core v2 は符号付き・符号なしの 8／16／32／64 ビット整数、文字型と文字リテラル、定数の `sizeof`／`alignof` に対応します。ソースの整数昇格とオーバーロード解決の後にビット幅を正規化し、`long`、`wchar_t`、サイズ型は対象環境に従います。列挙型の基底型にも狭い整数と広い整数を使用できます。実行時文字列と STL は開発中です。

core v2 はソース型のサイズと ABI アラインメントを NeverC 自身のターゲットモデルと照合し、構造体のサイズとフィールドのオフセットも検証します。生成コードのコンパイル時アサーションで配置を再確認し、その情報をマニフェストに記録します。

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

CI の結果、実行・インストールの検証、スキップされたテストはプラットフォーム別に記録しています。ネイティブの macOS arm64 と Rosetta 上の macOS x86_64 は異なる検証環境として区別しています。完全な C++／STL、例外、テンプレート、文字列、`std::vector` は公開されたサポート範囲外です。[サポート表](../../utils/translate-frontends/docs/support-matrix.md)、[プロトコルと復旧規則](../../utils/translate-frontends/docs/protocol.md)、[プロジェクト例](../../tests/neverc/Inputs/translate/cpp/project)を参照してください。
