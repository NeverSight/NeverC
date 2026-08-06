**언어**: [English](python.md) | [简体中文](python.zh-CN.md) | [繁體中文](python.zh-TW.md) | [日本語](python.ja.md) | [한국어](python.ko.md) | [Français](python.fr.md) | [Deutsch](python.de.md) | [Español](python.es.md) | [Italiano](python.it.md) | [Русский](python.ru.md) | [العربية](python.ar.md)

[← NeverC 플러그인 ABI](README.ko.md)

# Python 플러그인

NeverC는 네이티브 플러그인과 동일한 `-fplugin=` 옵션으로 Python 소스 파일을
불러올 수 있습니다. 일반 source build는 Python plugin과 install 시 runtime
bundling을 기본으로 활성화합니다.

```sh
cmake -S llvm -B build -C neverc/cmake/caches/NeverC.cmake \
  -DCMAKE_INSTALL_PREFIX="$PWD/neverc-install"
cmake --build build --target install
```

새 build의 기본값은 `NEVERC_ENABLE_PYTHON_PLUGINS=ON`과
`NEVERC_BUNDLE_PYTHON_RUNTIME=ON`입니다. configure에는 CPython 3.10 이상의
embedding header와 shared library가 필요하며, install은 선택된 interpreter의 정확한
version을 인접한 `python/` 디렉터리에 자동 복사합니다. build tree executable은 build
시 Python을 사용할 수 있지만 install된 compiler는 실행 시 외부 Python,
`PYTHONHOME`, `PYTHONPATH`가 필요하지 않습니다. 공식 NeverC archive는 CPython
3.12.10을 선택하여 포함합니다.

Linux install bundler에는 `PATH`의 `patchelf`가 필요합니다.
`CMAKE_CROSSCOMPILING`이면 host interpreter를 target architecture package에 넣지
않도록 자동 bundling을 거부합니다. 이 경우 bundling을 끄고 target runtime을 명시적으로
package해야 합니다. Python 없는 compiler는 `-DNEVERC_ENABLE_PYTHON_PLUGINS=OFF`와
`-DNEVERC_BUNDLE_PYTHON_RUNTIME=OFF`를 함께 지정해 build할 수 있습니다.

`python3 -m pip install ./pluginsdk/python`으로 작성 패키지를 설치하거나,
해당 디렉터리를 `PYTHONPATH`에 추가하거나, `neverc-pluginsdk` component를
빌드·설치하십시오. NeverC는 `<neverc 디렉터리>/../pluginsdk/python`에 staging된
SDK도 자동으로 찾습니다.

## 최소 플러그인

```python
from neverc_plugin import Plugin


@Plugin(id="com.example.minimal", name="Minimal Python Plugin", version="1.0.0")
class MinimalPlugin:
    def on_process_begin(self, ctx):
        ctx.state = {"sessions": 0}
```

파일 시스템 경로로 로드합니다.

```sh
neverc -fplugin=/absolute/path/to/minimal.py -fsyntax-only input.c
```

decorator는 canonical plugin ID, 비어 있지 않은 표시 이름, 엄격한 semantic
version을 받습니다. 한 script는 하나의 plugin class만 선언합니다. 서로 다른
script는 독립 module이며 native plugin과 함께 사용할 수 있습니다.

## 수명 주기

모든 hook은 선택 사항입니다.

- `on_process_begin(ctx)`와 `on_destroy(ctx)`는 compiler process를 감쌉니다.
- `register(ctx)`는 phase graph가 freeze되기 전에 option과 observer를 등록합니다.
- `on_session_begin(ctx)`와 `on_session_end(ctx)`는 invocation을 감쌉니다.
- `on_task_begin(ctx)`와 `on_task_end(ctx)`는 compiler 작업 단위를 감쌉니다.

begin hook은 Python 값을 반환하거나 `ctx.state`에 대입할 수 있으며, 대응하는
end hook에서 그 값을 읽을 수 있습니다. 그 밖의 hook과 observer callback은
`None`을 반환해야 합니다. v1 Python plugin은 session-serial이며 재진입하지 않습니다.

## 옵션과 observer

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

`neverc_plugin.phases`에는 정규 phase schema에서 생성한 130개 built-in phase
constant가 모두 들어 있습니다. Observer frame은 phase/route data, opaque
input/output handle, 파싱된 plugin option 값, diagnostic, cancellation check,
`driver.RAW_ARGUMENTS`의 raw argument를 제공합니다. native context와 frame
handle은 lifetime checked이므로 callback 후 보관한 object를 사용하면
`RuntimeError`가 발생합니다.

option kind는 `flag`, `joined`, `separate`, `multi_arg`, value type은
`bool`, `int`, `uint`, `string`, `enum`, `path`, multiplicity는 `single`,
`last_wins`, `append`입니다. enum option에는 `enum_values={name: integer}`
mapping을 전달합니다. `argument_count`는 `multi_arg`에만 적용됩니다.

## 오류, 보안 및 현재 범위

잡히지 않은 Python exception은 `NEVERC_STATUS_PLUGIN_EXCEPTION`으로 변환됩니다.
활성 session/task callback에서는 NeverC가 formatted traceback을 structured
plugin diagnostic으로 출력하며, import 및 activation 실패는 loader error에
traceback을 포함합니다. embedded interpreter는 process 전체에서 공유되고
NeverC는 의도적으로 finalize하지 않지만, plugin별 object는 unload 때 해제됩니다.

Python plugin은 신뢰되는 compiler extension입니다. process 내부에서 실행되고
임의의 module을 import할 수 있으며 NeverC와 같은 filesystem/process 권한을
가집니다. sandbox는 없습니다.

v1은 option registration 외에는 의도적으로 read-only입니다. interceptor,
provider, artifact mutation, domain별 IR/MIR/Link object model, subinterpreter,
manifest, module/factory entry point는 노출하지 않습니다. 이런 기능에는 lifetime을
강제할 수 있는 transaction 및 continuation wrapper가 필요하며, 필요할 때는
native C ABI를 계속 사용할 수 있습니다.
