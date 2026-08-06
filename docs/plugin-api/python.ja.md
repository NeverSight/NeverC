**言語**: [English](python.md) | [简体中文](python.zh-CN.md) | [繁體中文](python.zh-TW.md) | [日本語](python.ja.md) | [한국어](python.ko.md) | [Français](python.fr.md) | [Deutsch](python.de.md) | [Español](python.es.md) | [Italiano](python.it.md) | [Русский](python.ru.md) | [العربية](python.ar.md)

[← NeverC プラグイン ABI](README.ja.md)

# Python プラグイン

NeverC はネイティブプラグインと同じ `-fplugin=` オプションで Python
ソースファイルを読み込めます。通常の source build は Python plugin と
install 時の runtime bundling を既定で有効にします。

```sh
cmake -S llvm -B build -C neverc/cmake/caches/NeverC.cmake \
  -DCMAKE_INSTALL_PREFIX="$PWD/neverc-install"
cmake --build build --target install
```

新規 build の既定値は `NEVERC_ENABLE_PYTHON_PLUGINS=ON` と
`NEVERC_BUNDLE_PYTHON_RUNTIME=ON` です。CMake は build script に system Python
を使用できますが、その interpreter は plugin ABI を決めません。NeverC は別途、
SHA-256 で検証した固定 CPython 3.12.10 の development/runtime distribution を
download し、それに plugin bridge を link して `build/python` に配置し、install
時にも同じ runtime を隣接する `python/` directory に収録します。したがって通常の
source build と公式 archive はどちらも CPython 3.12.10 で plugin を実行し、外部
Python runtime、`PYTHONHOME`、`PYTHONPATH` は不要です。

offline build では、`-DNEVERC_MANAGED_PYTHON_ROOT=/path/to/cpython-3.12.10` に展開済みの正確な CPython
3.12.10 development/runtime tree を指定できます。NeverC はその tree を検証して
build directory にコピーし、指定元は変更しません。

Linux の install bundler には `PATH` 上の `patchelf` が必要です。CMake が ABI
probe を実行するため、managed Python plugin build は現在 native build が必要です。
cross build では Python feature を無効にするか、target platform 上に別の native
packaging stage を用意してください。Python を含まない compiler は
`-DNEVERC_ENABLE_PYTHON_PLUGINS=OFF` と
`-DNEVERC_BUNDLE_PYTHON_RUNTIME=OFF` の両方を指定して build できます。

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
