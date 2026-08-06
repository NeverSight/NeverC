**语言**: [English](python.md) | [简体中文](python.zh-CN.md) | [繁體中文](python.zh-TW.md) | [日本語](python.ja.md) | [한국어](python.ko.md) | [Français](python.fr.md) | [Deutsch](python.de.md) | [Español](python.es.md) | [Italiano](python.it.md) | [Русский](python.ru.md) | [العربية](python.ar.md)

[← NeverC 插件 ABI](README.zh-CN.md)

# Python 插件

NeverC 可以通过原生插件所使用的同一个 `-fplugin=` 选项加载 Python
源文件。普通源码构建现在默认启用 Python 插件，并在安装时捆绑运行时：

```sh
cmake -S llvm -B build -C neverc/cmake/caches/NeverC.cmake \
  -DCMAKE_INSTALL_PREFIX="$PWD/neverc-install"
cmake --build build --target install
```

全新构建默认采用 `NEVERC_ENABLE_PYTHON_PLUGINS=ON` 和
`NEVERC_BUNDLE_PYTHON_RUNTIME=ON`。CMake 可以用系统 Python 执行构建脚本，
但该解释器不会决定插件 ABI。NeverC 会另外下载经过 SHA-256 校验、版本固定为
CPython 3.12.10 的开发/运行时发行包，用它编译并链接插件桥接层，将其暂存到
`build/python`，安装时再把同一套运行时放入相邻的 `python/` 目录。因此普通
源码构建和官方归档都固定用 CPython 3.12.10 运行插件；构建树和安装后的编译器
均不需要外部 Python 运行时、`PYTHONHOME` 或 `PYTHONPATH`。

离线构建时，可用 `-DNEVERC_MANAGED_PYTHON_ROOT=/path/to/cpython-3.12.10` 指向预先解压的、准确版本为
CPython 3.12.10 且包含开发文件的运行时目录。NeverC 会先校验再复制到构建目录，
不会修改你提供的源目录。

Linux 安装阶段要求 `PATH` 中存在 `patchelf`。由于 CMake 会实际运行 ABI 探针，
当前启用托管 Python 插件的构建必须是原生构建；交叉编译时应关闭 Python 功能，
或另设目标平台的原生打包阶段。若有意构建完全不含 Python 的编译器，请同时传入
`-DNEVERC_ENABLE_PYTHON_PLUGINS=OFF` 和
`-DNEVERC_BUNDLE_PYTHON_RUNTIME=OFF`。

可用 `python3 -m pip install ./pluginsdk/python` 安装创作包，也可将该目录加入
`PYTHONPATH`，或构建/安装 `neverc-pluginsdk` 组件。NeverC 还会自动发现
`<neverc 所在目录>/../pluginsdk/python` 中已暂存的 SDK。

## 最小插件

```python
from neverc_plugin import Plugin


@Plugin(id="com.example.minimal", name="Minimal Python Plugin", version="1.0.0")
class MinimalPlugin:
    def on_process_begin(self, ctx):
        ctx.state = {"sessions": 0}
```

通过文件系统路径加载：

```sh
neverc -fplugin=/absolute/path/to/minimal.py -fsyntax-only input.c
```

装饰器接受一个规范插件 ID、非空显示名称和严格语义版本。一个脚本只声明
一个插件类。不同脚本是相互独立的模块，并可与原生插件混合使用。

## 生命周期

所有钩子均可选：

- `on_process_begin(ctx)` 与 `on_destroy(ctx)` 包围编译器进程生命周期。
- `register(ctx)` 在阶段图冻结前注册选项和 observer。
- `on_session_begin(ctx)` 与 `on_session_end(ctx)` 包围一次调用。
- `on_task_begin(ctx)` 与 `on_task_end(ctx)` 包围一个编译工作单元。

begin 钩子可以返回 Python 值或给 `ctx.state` 赋值；配对的 end 钩子可读取
该值。其他钩子和 observer 回调必须返回 `None`。v1 Python 插件采用
session-serial 且不可重入。

## 选项与 observer

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

`neverc_plugin.phases` 包含从规范阶段 schema 生成的全部 130 个内置阶段常量。
Observer frame 提供阶段和路由数据、不透明输入/输出 handle、已解析的插件选项
值、诊断、取消检查，以及 `driver.RAW_ARGUMENTS` 的原始参数。原生 context 和
frame handle 会检查生命周期：回调结束后继续使用保留对象会引发
`RuntimeError`。

选项 kind 包括 `flag`、`joined`、`separate` 和 `multi_arg`；value type
包括 `bool`、`int`、`uint`、`string`、`enum` 和 `path`；multiplicity
包括 `single`、`last_wins` 和 `append`。枚举选项传入
`enum_values={名称: 整数}` 映射。`argument_count` 只适用于 `multi_arg`。

## 错误、安全与当前范围

未捕获的 Python 异常会转换为 `NEVERC_STATUS_PLUGIN_EXCEPTION`。在活动的
session/task 回调中，NeverC 会把格式化 traceback 作为结构化插件诊断输出；
导入和激活失败则会在加载错误中包含 traceback。嵌入解释器在进程范围共享，
NeverC 有意不调用 finalize；每个插件的对象仍会在卸载时释放。

Python 插件是受信任的编译器扩展。它们在进程内运行，可导入任意模块，并拥有
与 NeverC 相同的文件系统和进程权限；这里不存在 sandbox。

v1 除选项注册外有意保持只读。它不开放 interceptor、provider、artifact
修改、各域专用的 IR/MIR/Link 对象模型、subinterpreter、manifest 或
module/factory 入口。这些能力需要可强制执行生命周期的事务与 continuation
包装；需要它们时仍可使用原生 C ABI。
