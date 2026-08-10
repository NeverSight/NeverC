**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 文档](../README.zh-CN.md)

# NeverC 内置运行时系统

NeverC 通过可选的内置运行时扩展标准 C，这些运行时以 LLVM bitcode 形式直接嵌入编译器二进制文件中。通过编译器标志启用后，相应的运行时会在编译时合并到用户的 IR 中——无需外部头文件、库或链接时依赖。

## 可用内置功能

| 内置功能 | 标志 | 默认 | 描述 |
|---------|------|------|------|
| [**`string`**](string/README.zh-CN.md) | `-fbuiltin-string` | 关闭 | 值语义字符串类型，支持点调用方法、自动内存管理和原生 UTF-8 |
| [**`mimalloc`**](mimalloc/README.zh-CN.md) | `-fbuiltin-mimalloc` | **开启** | 高性能内存分配器，透明替换 `malloc`/`free`/`calloc`/`realloc` |
| [**`xorstr`**](xorstr/README.zh-CN.md) | `-fencrypt-call-strings` | 关闭 | 逐实例编译期加密、强制 late 封口、逐调用点最终展开与 volatile 栈清零 |
| [**`strhash`**](strhash/README.zh-CN.md) | `-fstrhash-algo` / `-fstrhash-fold` | 关闭 | 编译期字符串哈希，运行时算法一致，可选 IR 常量折叠 |

`string` 内置需要显式启用；`mimalloc` 对所有 hosted 构建默认开启（内核、dyncode 和 freestanding 模式下自动抑制）。可以组合使用：

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
LANGOPT(BuiltinString,      1, 0, "inject NeverC builtin string prelude")
LANGOPT(BuiltinMimalloc,    1, 1, "inject mimalloc allocator override")
LANGOPT(EncryptCallStrings, 1, 0, "auto-encrypt string literals in call arguments")
VALUE_LANGOPT(EncryptCallStringsMaxLen, 32, 1024,
              "maximum string length for auto-encryption (0 = no limit)")
```

驱动器标志（`-fbuiltin-<name>` / `-fno-builtin-<name>`、`-fencrypt-call-strings` / `-fno-encrypt-call-strings`、`-fencrypt-call-strings-max-len=N`）在 `Options.td.h` 中声明，并附带 `LANG_OPTION_WITH_MARSHALLING` 条目。驱动器通过 `addNeverCFeatureFlags()` 将其传递给前端。

### 第二层：Foundation API

每个内置功能在 `neverc/Foundation/Builtin/` 中有一对头文件和实现文件：

| 内置功能 | 头文件 | 实现 |
|---------|--------|------|
| `string` | `BuiltinString.h` | `BuiltinString.cpp` |
| `mimalloc` | `BuiltinMimalloc.h` | `BuiltinMimalloc.cpp` |
| `xorstr` | `lib/Headers/neverc/xorstr/xorstr.h` *(用户头文件)* | `lib/Transforms/XorStr/EncryptCallStringsPass.cpp` |
| `strhash` | `lib/Headers/neverc/strhash/strhash.h` *(用户头文件)* | `lib/Transforms/StrHash/StrHashFoldPass.cpp` |

API 提供 `getEmbeddedBitcode()` 用于获取预编译的 LLVM bitcode，以及 `isSupported()` 用于检查平台可用性。

> **说明：** `xorstr` 不走嵌入式 bitcode 模型。显式宏 [`NC_XORSTR(s)` / `NEVERC_XORSTR(s)`](xorstr/README.zh-CN.md) 由 `SemaCheckingBuiltinNeverC.cpp` 中的 `semaBuiltinNeverCXorstr` 降级。`EncryptCallStringsPass` 与 `XorStrCleanupPass` 会在 IPO 前及每个普通或插件 late IR 阶段后封口自动字面量并清理明文栈槽；`FinalizeXorStrPass` 仅在真正的原生机器码边界重新加密和逐调用点展开显式解码器，随后删除共享辅助图。完整设计与可复现性约定见 [xorstr 文档](xorstr/README.zh-CN.md)。

> **说明：** `strhash` 同样不走嵌入式 bitcode 模型。[`NC_STRHASH(s)` / `NEVERC_STRHASH(s)`](strhash/README.zh-CN.md) 在 Sema 中折叠为整数常量（`semaBuiltinNeverCStrHash`）；运行时哈希经 `neverc_strhash_rt` 调用 NeverC std（`neverc_fnv_sum*` / `neverc_xxhash64`）。可选 `-fstrhash-fold` 启用 `StrHashFoldPass`，将字面量参数的运行时哈希调用折叠为常量。完整设计见 [strhash 文档](strhash/README.zh-CN.md)。

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

`xorstr` 会在 IPO 前执行前置封口、普通优化后执行幂等封口，并在每个普通或插件 late IR 回调后执行强制尾部封口。显式解码器在中间 LTO bitcode 中保持不透明，仅在原生边界 Finalize；可选 `strhash` 折叠仍位于优化后：

```cpp
if (LangOpts.StrHashFold) {
    MPM.addPass(neverc::strhash::StrHashFoldPass());
}
if (LangOpts.EncryptCallStrings) {
    MPM.addPass(neverc::xorstr::EncryptCallStringsPass(
                    LangOpts.EncryptCallStringsMaxLen,
                    LangOpts.StringEncryptKey));
    MPM.addPass(createModuleToFunctionPassAdaptor(
                    neverc::xorstr::XorStrCleanupPass()));
}
if (!CodeGenOpts.PrepareForLTO && actionRequiresCodeGen(Action))
    MPM.addPass(neverc::xorstr::FinalizeXorStrPass(
                    LangOpts.StringEncryptKey));
```

---

## 内置功能之间的设计差异

| 方面 | `string` | `mimalloc` |
|------|----------|------------|
| **合并策略** | 按需（BFS 调用图，裁剪未使用） | 全量合并（whole-archive，所有符号保留） |
| **平台 bitcode** | 单一（架构中性） | 按 OS 分（Linux / Darwin / Windows） |
| **符号处理** | 全部内部化 | 覆盖入口保持外部链接 |
| **预处理器宏** | *（无）* | `__NEVERC_MIMALLOC__` |
| **DynCode 模式** | 自动启用，arena 重写 | 被抑制（HeapArenaPass 处理堆分配） |
| **优化级别** | `-O0`（bitcode 编译） | `-O2`（性能关键的分配器） |
| **DCE** | 预合并裁剪 + 后合并标记清扫 | 无 DCE（whole-archive 语义） |

---

## 安全互锁

某些编译模式与内置运行时不兼容。驱动器会自动抑制：

| 条件 | 效果 | 原因 |
|------|------|------|
| `-fno-builtin` | 抑制 `mimalloc` | 无 CRT 覆盖场景 |
| `-mkernel` | 抑制 `mimalloc` | 内核无用户空间堆 |
| `-fdyncode-mode` | 抑制 `mimalloc` | 由 HeapArenaPass 替代（基于 arena） |
| `-ffreestanding` | 抑制 `mimalloc` | 无 libc 可覆盖 |

`string` 内置有自己的抑制逻辑（dyncode 流水线中 arena 重写替换堆分配）。

### HeapArenaPass（DynCode 堆分配）

当 `-fdyncode-mode` 启用时，`mimalloc` 被抑制，但 `malloc`/`free`/`calloc`/`realloc` 调用会被 `HeapArenaPass` 自动改写（默认开启）。该 Pass 使用混合策略：

- **小分配（≤ 64 KB）**：从与 `string` 内置运行时共享的栈上 arena 分配（bump allocator + free list 重用）。
- **大分配（> 64 KB）或 arena OOM**：回退到 OS 分配器：
  - **Windows**：`malloc`/`free` 通过 PEB walk 从 `msvcrt.dll` 解析（`-mdyncode-win-peb-import`）。
  - **Linux / macOS / Android**：`mmap`/`munmap` 内联为原生系统调用（`-mdyncode-syscall`）。
  - **未启用导入 Pass**：仅使用 arena；OOM 返回 `NULL`。

通过驱动标志控制：

```bash
neverc -fdyncode test.c -o test.bin                     # HeapArenaPass 开启（默认）
neverc -fdyncode -fno-dyncode-heap-arena test.c       # HeapArenaPass 关闭（原始行为）
```

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
│       ├── Builtins.def                  # __builtin_neverc_xorstr / __builtin_neverc_strhash
│       └── ...
│
├── include/neverc/Invoke/
│   └── Options.td.h                      # 驱动器标志声明
│
├── include/neverc/Transforms/XorStr/     # xorstr IR pass 头文件
│   ├── EncryptCallStringsPass.h
│   └── XorStrCleanupPass.h
│
├── include/neverc/Transforms/StrHash/    # strhash IR pass 头文件
│   ├── StrHashCompute.h
│   └── StrHashFoldPass.h
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
├── lib/Headers/neverc/
│   ├── xorstr/
│   │   ├── xorstr.h                      # NC_XORSTR / NEVERC_XORSTR 宏
│   │   └── xorstr_impl.inc               # 不透明中间解码器
│   └── strhash/
│       ├── strhash.h                     # NC_STRHASH / NC_STRHASH_AUTO 宏
│       └── strhash_impl.inc              # neverc_strhash_rt 内联分发
│
├── lib/Analyze/Checking/
│   ├── SemaChecking.cpp                  # builtin 分发
│   └── SemaCheckingBuiltinNeverC.cpp     # xorstr / strhash Sema 处理
│
├── lib/Transforms/XorStr/                # xorstr IR 变换 Pass
│   ├── EncryptCallStringsPass.cpp        # 自动加密 call 参数中的字符串字面量
│   └── XorStrCleanupPass.cpp             # 清零栈上明文缓冲区
│
├── lib/Transforms/StrHash/               # strhash IR 变换 Pass
│   └── StrHashFoldPass.cpp               # 折叠常量参数的运行时哈希调用
│
├── lib/Emit/Backend/
│   ├── BackendUtil.cpp                   # PipelineStartEP + 后置 Pass 注册
│   ├── StringRuntimeLinker.{h,cpp}       # string IR 合并 Pass
│   └── MimallocRuntimeLinker.{h,cpp}     # mimalloc IR 合并 Pass
│
├── lib/Invoke/ToolChains/
│   └── NeverC.cpp                        # addNeverCFeatureFlags()
│
└── lib/Compiler/Preprocessor/
    └── InitPredefinedMacros.cpp          # __NEVERC_MIMALLOC__ 宏
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
7. **预处理器**：在 `InitPredefinedMacros.cpp` 中定义 `__NEVERC_FOO__`
8. **安全检查**：在 `addNeverCFeatureFlags()` 中添加不兼容模式的抑制逻辑
9. **测试**：添加 GTest 用例 + C 测试源文件
10. **文档**：添加 `docs/builtins/foo/README.md` 及 i18n 翻译
