**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 文档](../README.zh-CN.md)

# NeverC 内置运行时系统

NeverC 通过可选的内置运行时扩展标准 C，这些运行时以 LLVM bitcode 形式直接嵌入编译器二进制文件中。通过编译器标志启用后，相应的运行时会在编译时合并到用户的 IR 中——无需外部头文件、库或链接时依赖。

## 可用内置功能

| 内置功能 | 标志 | 默认 | 描述 |
|---------|------|------|------|
| [**`string`**](string/README.zh-CN.md) | `-fbuiltin-string` | 关闭 | 值语义字符串类型，支持点调用方法、自动内存管理和原生 UTF-8 |
| [**`mimalloc`**](mimalloc/README.zh-CN.md) | `-fbuiltin-mimalloc` | **开启** | 高性能内存分配器，透明替换 `malloc`/`free`/`calloc`/`realloc` |

`string` 内置需要显式启用；`mimalloc` 对所有 hosted 构建默认开启（内核、shellcode 和 freestanding 模式下自动抑制）。可以组合使用：

```bash
neverc -fbuiltin-string -fbuiltin-mimalloc main.c -o main
```

---

## 架构总览

所有内置功能共享相同的四层架构：

```
┌─────────────────────────────────────────────────────────────────┐
│                       编译器构建时                                │
│                                                                 │
│  源代码 ──→ neverc -c -emit-llvm ──→ .bc ──→ bin2c.py          │
│                                       │                         │
│                              嵌入编译器二进制                     │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                       用户编译时                                 │
│                                                                 │
│  user.c ──→ 词法/语法/语义/代码生成 ──→ IR 模块                  │
│                                          │                      │
│                      PipelineStartEP: RuntimeLinkerPass          │
│                         │                                       │
│                         ├─ 解析嵌入的 bitcode                    │
│                         ├─ 合并到用户 Module                     │
│                         ├─ 内部化辅助符号                        │
│                         └─ 清理 llvm.used                       │
│                                          │                      │
│                      优化流水线                                  │
│                                          │                      │
│                                       输出 .o                   │
└─────────────────────────────────────────────────────────────────┘
```

### 第一层：语言选项与驱动器标志

每个内置功能由 `LangOptions.def` 中定义的 `LangOption` 控制：

```cpp
LANGOPT(BuiltinString,   1, 0, "inject NeverC builtin string prelude")
LANGOPT(BuiltinMimalloc, 1, 0, "inject mimalloc allocator override")
```

驱动器标志（`-fbuiltin-<name>` / `-fno-builtin-<name>`）在 `Options.td.h` 中声明，并附带 `LANG_OPTION_WITH_MARSHALLING` 条目。驱动器通过 `addNeverCFeatureFlags()` 将其传递给前端。

### 第二层：Foundation API

每个内置功能在 `neverc/Foundation/Builtin/` 中有一对头文件和实现文件：

| 内置功能 | 头文件 | 实现 |
|---------|--------|------|
| `string` | `BuiltinString.h` | `BuiltinString.cpp` |
| `mimalloc` | `BuiltinMimalloc.h` | `BuiltinMimalloc.cpp` |

API 提供 `getEmbeddedBitcode()` 用于获取预编译的 LLVM bitcode，以及 `isSupported()` 用于检查平台可用性。

### 第三层：CMake 引导基础设施

Bitcode 生成遵循两阶段引导流程：

```bash
ninja neverc                         # 阶段 1：空 bitcode 占位符
ninja neverc-bootstrap-string-bc     # 使用 neverc 编译字符串运行时
ninja neverc-bootstrap-mimalloc-bc   # 为所有目标 OS 编译 `mimalloc`
ninja neverc                         # 阶段 2：嵌入真实 bitcode
```

初始构建使用空占位符头文件（`static const unsigned char kXxxBitcode[] = {0};`），确保编译成功。引导目标随后使用新构建的 neverc 将运行时源码编译为 LLVM bitcode，通过 `bin2c.py` 转换为 C 头文件数组，并触发重编译以嵌入真实数据。

### 第四层：IR 合并 Pass（PipelineStartEP）

每个内置功能在 `BackendUtil.cpp` 的 `PipelineStartEP` 注册一个 `ModulePass`：

```cpp
if (LangOpts.BuiltinString) {
    PB.registerPipelineStartEPCallback([](ModulePassManager &MPM, OptimizationLevel) {
        MPM.addPass(StringRuntimeLinkerPass());
    });
}
if (LangOpts.BuiltinMimalloc) {
    PB.registerPipelineStartEPCallback([](ModulePassManager &MPM, OptimizationLevel) {
        MPM.addPass(MimallocRuntimeLinkerPass());
    });
}
```

该 Pass 解析嵌入的 bitcode，通过 `llvm::Linker::linkModules()` 合并到用户模块，内部化辅助符号（仅保留公共 API 的外部链接），并清理 `llvm.used` / `llvm.compiler.used`。

---

## 内置功能之间的设计差异

| 方面 | `string` | `mimalloc` |
|------|----------|------------|
| **合并策略** | 按需（BFS 调用图，裁剪未使用） | 全量合并（whole-archive，所有符号保留） |
| **平台 bitcode** | 单一（架构中性） | 按 OS 分（Linux / Darwin / Windows） |
| **符号处理** | 全部内部化 | 覆盖入口保持外部链接 |
| **预处理器宏** | *（无）* | `__NEVERC_MIMALLOC__` |
| **Shellcode 模式** | 自动启用，arena 重写 | 被抑制（shellcode 无堆） |
| **优化级别** | `-O0`（bitcode 编译） | `-O2`（性能关键的分配器） |
| **DCE** | 预合并裁剪 + 后合并标记清扫 | 无 DCE（whole-archive 语义） |

---

## 安全互锁

某些编译模式与内置运行时不兼容。驱动器会自动抑制：

| 条件 | 效果 | 原因 |
|------|------|------|
| `-fno-builtin` | 抑制 `mimalloc` | 无 CRT 覆盖场景 |
| `-mkernel` | 抑制 `mimalloc` | 内核无用户空间堆 |
| `-fshellcode-mode` | 抑制 `mimalloc` | shellcode 无堆 |
| `-ffreestanding` | 抑制 `mimalloc` | 无 libc 可覆盖 |

`string` 内置有自己的抑制逻辑（shellcode 流水线中 arena 重写替换堆分配）。

---

## 预处理器宏

当内置功能激活时，会定义相应的预处理器宏：

```c
#ifdef __NEVERC_MIMALLOC__
// `mimalloc` 已激活 — malloc/free 被透明覆盖
#endif
```

用户代码可据此条件编译。

---

## 文件结构

```
neverc/
├── include/neverc/Foundation/
│   ├── LangOpts/LangOptions.def          # LANGOPT 声明
│   └── Builtin/
│       ├── BuiltinString.h               # string API
│       ├── BuiltinMimalloc.h             # mimalloc API
│       └── ...
│
├── include/neverc/Invoke/
│   └── Options.td.h                      # 驱动器标志声明
│
├── lib/Foundation/
│   ├── CMakeLists.txt                    # 所有内置功能的引导目标
│   └── Builtin/
│       ├── BuiltinString.cpp             # string bitcode 嵌入
│       ├── BuiltinMimalloc.cpp           # mimalloc 按 OS bitcode 嵌入
│       ├── bin2c.py                      # .bc → C 头文件转换器（共享）
│       ├── gen_string_runtime.py         # string 源码生成器
│       └── gen_mimalloc_source.py        # mimalloc 源码生成器
│
├── lib/Emit/Backend/
│   ├── BackendUtil.cpp                   # PipelineStartEP 注册
│   ├── StringRuntimeLinker.{h,cpp}       # string IR 合并 Pass
│   └── MimallocRuntimeLinker.{h,cpp}     # mimalloc IR 合并 Pass
│
├── lib/Invoke/ToolChains/
│   └── NeverC.cpp                        # addNeverCFeatureFlags()
│
└── lib/Compiler/Preprocessor/
    └── InitPreprocessor.cpp              # __NEVERC_MIMALLOC__ 宏
```

---

## 添加新的内置功能

要添加新的内置运行时（例如自定义分配器、加密库或平台抽象层）：

1. **LangOption**：在 `LangOptions.def` 中添加 `LANGOPT(BuiltinFoo, 1, 0, "description")`
2. **驱动器标志**：在 `Options.td.h` 中添加 `-fbuiltin-foo` / `-fno-builtin-foo`；在 `NeverC.cpp` 的 `addNeverCFeatureFlags()` 中接入
3. **Foundation API**：创建 `BuiltinFoo.h`（含 `getEmbeddedBitcode()` + `isSupported()`）和 `BuiltinFoo.cpp`
4. **源码生成器**：创建 `gen_foo_source.py` 生成独立 C 编译单元
5. **CMake**：在 `Foundation/CMakeLists.txt` 中添加占位符头文件、引导目标，并将源文件加入 `nevercFoundation`
6. **IR Pass**：在 `Emit/Backend/` 中创建 `FooRuntimeLinkerPass`，在 `BackendUtil.cpp` 的 `PipelineStartEP` 注册
7. **预处理器**：在 `InitPreprocessor.cpp` 中定义 `__NEVERC_FOO__`
8. **安全检查**：在 `addNeverCFeatureFlags()` 中添加不兼容模式的抑制逻辑
9. **测试**：添加 GTest 用例 + C 测试源文件
10. **文档**：添加 `docs/builtins/foo/README.md` 及 i18n 翻译
