**言語**: [English](python.md) | [简体中文](python.zh-CN.md) | [繁體中文](python.zh-TW.md) | [日本語](python.ja.md) | [한국어](python.ko.md) | [Français](python.fr.md) | [Deutsch](python.de.md) | [Español](python.es.md) | [Italiano](python.it.md) | [Русский](python.ru.md) | [العربية](python.ar.md)

[← NeverC プラグイン ABI](README.ja.md)

# Python プラグイン

NeverC は、ネイティブプラグインと同じ `-fplugin=` オプションで Python
ソースファイルを読み込めます。Python 対応は任意なので、通常のビルドには
CPython への依存が追加されません。

```sh
cmake -S llvm -B build -DNEVERC_ENABLE_PYTHON_PLUGINS=ON
cmake --build build --target neverc
```

NeverC の公式 compiler archive はこの option を有効にし、隣接する `python/`
directory に relocatable な CPython 3.12 runtime と standard library を同梱する
ため、実行時に Python を別途インストールする必要はありません。独自の source
build では既定値が `OFF` のままで、有効化時は外部 CPython 3.10 以降も使えます。

有効化したビルドには CPython 3.10 以降と埋め込み用開発ファイルが必要です。
`python3 -m pip install ./pluginsdk/python` で authoring package を導入するか、
そのディレクトリを `PYTHONPATH` に追加するか、`neverc-pluginsdk` component
をビルド・インストールしてください。NeverC は
`<neverc のディレクトリ>/../pluginsdk/python` に配置された SDK も検出します。

## 最小プラグイン

```python
from neverc_plugin import Plugin


@Plugin(id="com.example.minimal", name="Minimal Python Plugin", version="1.0.0")
class MinimalPlugin:
    def on_process_begin(self, ctx):
        ctx.state = {"sessions": 0}
```

ファイルシステム上のパスで読み込みます。

```sh
neverc -fplugin=/absolute/path/to/minimal.py -fsyntax-only input.c
```

decorator は canonical な plugin ID、空でない表示名、厳密な semantic version
を受け取ります。1 script が宣言できる plugin class は 1 つです。各 script
は独立した module であり、native plugin と併用できます。

## ライフサイクル

すべての hook は任意です。

- `on_process_begin(ctx)` と `on_destroy(ctx)` が compiler process を囲みます。
- `register(ctx)` は phase graph の freeze 前に option と observer を登録します。
- `on_session_begin(ctx)` と `on_session_end(ctx)` が invocation を囲みます。
- `on_task_begin(ctx)` と `on_task_end(ctx)` が compiler work unit を囲みます。

begin hook は Python value を返すか `ctx.state` に代入でき、対応する end hook
から参照できます。それ以外の hook と observer callback は `None` を返す必要
があります。v1 Python plugin は session-serial かつ non-reentrant です。

## オプションと observer

```python
from neverc_plugin import Plugin
from neverc_plugin.domains import driver


@Plugin(id="com.example.trace", name="Trace", version="1.0.0")
class TracePlugin:
    def register(self, ctx):
        ctx.option(
            "--trace-python",
            kind="flag",
            value_type="bool",
            help="Trace raw driver arguments",
        )
        ctx.observer(
            driver.RAW_ARGUMENTS,
            when=("before", "after"),
            fn=self.observe,
        )

    def observe(self, frame):
        if frame.option_values("--trace-python"):
            frame.check_cancelled()
            frame.emit_remark(f"arguments: {frame.arguments}", code=1001)
```

`neverc_plugin.phases` には、正規の phase schema から生成された 130 個すべての
built-in phase constant があります。Observer frame は phase/route data、opaque
な input/output handle、解析済み plugin option、diagnostic、cancellation check、
そして `driver.RAW_ARGUMENTS` の raw argument を公開します。native context と
frame handle は lifetime checked で、callback 後に保持した object を使うと
`RuntimeError` になります。

option kind は `flag`、`joined`、`separate`、`multi_arg`、value type は
`bool`、`int`、`uint`、`string`、`enum`、`path`、multiplicity は
`single`、`last_wins`、`append` です。enum option には
`enum_values={name: integer}` mapping を渡します。`argument_count` は
`multi_arg` 専用です。

## エラー、セキュリティ、現在の範囲

捕捉されなかった Python exception は `NEVERC_STATUS_PLUGIN_EXCEPTION` になり
ます。active な session/task callback では、NeverC が formatted traceback を
structured plugin diagnostic として出力します。import と activation の失敗は
loader error に traceback を含みます。embedded interpreter は process 全体で
共有され、NeverC は意図的に finalize しませんが、plugin ごとの object は
unload 時に解放されます。

Python plugin は信頼された compiler extension です。同一 process 内で動作し、
任意の module を import でき、NeverC と同じ filesystem/process 権限を持ちます。
sandbox はありません。

v1 は option registration 以外を意図的に read-only としています。
interceptor、provider、artifact mutation、domain 固有の IR/MIR/Link object
model、subinterpreter、manifest、module/factory entry point は公開しません。
これらには lifetime を強制できる transaction/continuation wrapper が必要で、
必要な場合は引き続き native C ABI を利用できます。
