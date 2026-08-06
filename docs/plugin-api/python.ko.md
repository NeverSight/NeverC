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
`NEVERC_BUNDLE_PYTHON_RUNTIME=ON`입니다. CMake는 build script에 system Python을
사용할 수 있지만 그 interpreter가 plugin ABI를 선택하지는 않습니다. NeverC는 별도로
SHA-256으로 검증된 고정 CPython 3.12.10 development/runtime distribution을 내려받아
plugin bridge를 link하고 `build/python`에 배치한 뒤, install 시에도 동일한 runtime을
인접한 `python/` 디렉터리에 포함합니다. 따라서 일반 source build와 공식 archive 모두
CPython 3.12.10으로 plugin을 실행하며 외부 Python runtime, `PYTHONHOME`,
`PYTHONPATH`가 필요하지 않습니다.

offline build에서는 `-DNEVERC_MANAGED_PYTHON_ROOT=/path/to/cpython-3.12.10`를 미리 압축 해제한 정확한
CPython 3.12.10 development/runtime tree로 지정할 수 있습니다. NeverC는 이를 검증해
build 디렉터리로 복사하며 원본 디렉터리는 수정하지 않습니다.

Linux install bundler에는 `PATH`의 `patchelf`가 필요합니다. CMake가 ABI probe를
실행하므로 managed Python plugin build는 현재 native build여야 합니다. cross build는
Python feature를 끄거나 target platform의 별도 native packaging stage를 사용해야 합니다.
Python 없는 compiler는 `-DNEVERC_ENABLE_PYTHON_PLUGINS=OFF`와
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
`None`을 반환해야 합니다. 기본값은 session-serial 및 non-reentrant이며,
`@Plugin`으로 native plugin과 같은 모델을 선택할 수 있습니다.

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

## C ABI 전체 접근

수명 주기, 옵션, observer 고수준 도우미는 공개 C 플러그인 ABI 전체를
기반으로 합니다. `neverc_plugin.abi`는 생성된 `ctypes` 정의를 제공하고,
`neverc_plugin.domains`의 각 모듈은 버전과 크기를 검사하며 모든 공식 테이블을
조회합니다. `bind_callbacks`는 정확한 시그니처의 네이티브 trampoline을 통해
Python callback을 연결합니다.

```python
from neverc_plugin import abi
from neverc_plugin.domains import ir
from neverc_plugin.ffi import bind_callbacks, require_ok


def register(self, context):
    scope = context.ffi
    core = ir.CORE.query(scope)
    builder = ir.BUILDER.query(scope)
    passes = ir.PASS.query(scope)
```

## Python OLLVM 예제

SDK의
[`pluginsdk/python/examples/ollvm`](../../pluginsdk/python/examples/ollvm/README.md)
에는 이 공개 binding만 사용해 작성한 결정적 명령어 치환(SUB), 가짜 제어 흐름
(BCF), 제어 흐름 평탄화(FLA) 예제가 포함되어 있습니다.

```sh
neverc -fplugin=/path/to/ollvm_plugin.py \
  --ollvm-sub --ollvm-bcf --ollvm-fla \
  --ollvm-seed 42 --ollvm-probability 80 \
  input.c -o output
```

## 오류, 보안 및 현재 범위

잡히지 않은 Python exception은 `NEVERC_STATUS_PLUGIN_EXCEPTION`으로 변환됩니다.
활성 session/task callback에서는 NeverC가 formatted traceback을 structured
plugin diagnostic으로 출력하며, import 및 activation 실패는 loader error에
traceback을 포함합니다. embedded interpreter는 process 전체에서 공유되고
NeverC는 의도적으로 finalize하지 않지만, plugin별 object는 unload 때 해제됩니다.

Python plugin은 신뢰되는 compiler extension입니다. process 내부에서 실행되고
임의의 module을 import할 수 있으며 NeverC와 같은 filesystem/process 권한을
가집니다. sandbox는 없습니다.

Python binding은 축소된 API가 아닙니다. 생성된 `ctypes` 정의와 native
trampoline이 공식 C ABI의 36개 interface table, 모든 record, function 및 callback
family를 포함하며 mutation, interceptor, provider도 지원합니다. lifetime,
transaction, continuation도 검사합니다. SUB, BCF, FLA를 구현한 완전한 Python
OLLVM 예제는 `pluginsdk/python/examples/ollvm/`에 있습니다.
raw 정의는 `neverc_plugin.abi`, table descriptor는
`neverc_plugin.domains`에 있습니다.
