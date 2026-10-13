**Languages**: [English](../hosted-dyncode.md) | [简体中文](../zh-CN/hosted-dyncode.md) | [繁體中文](../zh-TW/hosted-dyncode.md) | [日本語](hosted-dyncode.md) | [한국어](../ko/hosted-dyncode.md) | [Français](../fr/hosted-dyncode.md) | [Deutsch](../de/hosted-dyncode.md) | [Español](../es/hosted-dyncode.md) | [Italiano](../it/hosted-dyncode.md) | [Русский](../ru/hosted-dyncode.md) | [العربية](../ar/hosted-dyncode.md)

[← DynCode](dyncode-compiler.md)

# Hosted DynCode と C++ 変換

既存プログラムへコードのみの関数を設置する場合は `-fdyncode-hosted` を使います。`-fdyncode-keep-obj` が必須で、オブジェクト内の CFI/SEH を保持し、フラットなペイロードからは除外します。一つのネイティブコード節とオフセットゼロの入口が展開記録との対応を保持します。

暗黙のシステムコール、PEB、ヒープ arena、文字列ランタイム変換を無効にします。書き込み可能/TLS グローバルと未解決呼び出しは失敗します。データやコールバックのポインタは引数で渡せます。独立 DynCode の動作は変わりません。

コア C++ 変換は `--target` を受け付け、C23、ソースマップ、マニフェストを対象 ABI に結び付けます。`cpp-core-v2` は文書化された C++17 の部分集合を扱い、テンプレートと通常の構築・破棄を含みます。C++ 例外、RTTI、仮想呼び出し、外部例外伝播中の後始末は対象外です。

```sh
neverc translate --from cpp replacement.cpp --profile cpp-core-v2 \
  --target aarch64-unknown-linux-gnu -o replacement.nc -- -std=c++17
neverc --no-default-config --target=aarch64-unknown-linux-gnu \
  -fdyncode -fdyncode-hosted -fdyncode-entry=replacement \
  -fdyncode-keep-obj=replacement.o -fdyncode-report=replacement.audit.json \
  -o replacement.bin replacement.nc
```

監査は `execution_profile="hosted-v1"`、`keep_unwind=true`、`target_triple` を記録します。`input_digest` は保持オブジェクト、`output_digest` は再配置と最終検証後のペイロードのハッシュです。設置前に両方のハッシュとコード・入口配置の一致を検証し、実際の展開記録を登録します。
