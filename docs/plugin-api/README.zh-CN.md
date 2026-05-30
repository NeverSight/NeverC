**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# NeverC Out-of-Tree 插件 API

NeverC 提供一套**纯 C ABI** 用于 out-of-tree pass 插件。插件是一个共享库（`.dll` / `.so` / `.dylib`），在编译流水线的指定位置注册自定义 pass。插件只需编译依赖**一个头文件**（`NevercPluginAPI.h`），**零** LLVM 或 CRT 依赖 — 所有功能通过宿主提供的 vtable 路由。

## 1. 快速开始

### 最小插件

```c
#include "NevercPluginAPI.h"

static int myPass(NevercModuleRef M, const NevercHostAPI *API, void *UD) {
    (void)UD;
    unsigned Count = 0;
    NEVERC_FOR_EACH_DEFINED_FUNCTION(API, M, F) {
        (void)F;
        Count++;
    }
    API->DiagNoteF("[my-plugin] %u defined functions", Count);
    return 0;
}

static void registerPasses(const NevercHostAPI *API, void *Reg) {
    API->RegisterModulePass(Reg, NEVERC_HOOK_PRE_OPT, myPass, NULL, "my-pass");
}

NEVERC_EXPORT NevercPluginInfo nevercGetPluginInfo(void) {
    NevercPluginInfo Info;
    Info.APIVersion     = NEVERC_PLUGIN_API_VERSION;
    Info.PluginName     = "my-plugin";
    Info.PluginVersion  = "1.0.0";
    Info.RegisterPasses = registerPasses;
    Info.Destroy        = NULL;
    return Info;
}
```

### 构建

```bash
# 单命令构建（任意 C 编译器）：
cc -shared -o MyPlugin.dll MyPlugin.c -I/path/to/pluginsdk/include

# 或使用 Make（使用 SDK 附带的 Makefile）：
make -C /path/to/pluginsdk/examples
```

### 运行

```bash
neverc -fplugin-pass=./MyPlugin.dll input.c -o output.obj
```

## 2. 架构

```
┌──────────────────────────────────────────────────────────┐
│                    neverc（宿主）                          │
│                                                          │
│  ┌─────────────────────────────────────────────────────┐ │
│  │              NevercHostAPI vtable                    │ │
│  │  ModuleGetFirstFunction, BuilderCreate, DiagNoteF,  │ │
│  │  ArenaCreate, StrMapCreate, Sort, ...  (200+ 函数)  │ │
│  └──────────────────────┬──────────────────────────────┘ │
│                         │ 传递给插件                      │
│                         ▼                                │
│  ┌─────────────────────────────────────────────────────┐ │
│  │            插件（.dll / .so / .dylib）                │ │
│  │                                                     │ │
│  │  nevercGetPluginInfo() → NevercPluginInfo            │ │
│  │    ├─ RegisterPasses(API, Registrar)                 │ │
│  │    │    └─ API->RegisterModulePass(...)              │ │
│  │    │    └─ API->RegisterMachinePass(...)             │ │
│  │    │    └─ API->RegisterBinaryPass(...)              │ │
│  │    │    └─ API->RegisterLinkerPass(...)              │ │
│  │    └─ Destroy()（可选清理）                            │ │
│  └─────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────┘
```

**核心特性：**

- **单头文件 SDK**：编译插件只需 `NevercPluginAPI.h`。
- **零依赖**：不需要 LLVM 头文件，不链接 CRT。所有操作通过 vtable 完成。
- **纯 C ABI**：插件可用 C、C++、Zig、Rust（FFI）或任何能生成 C 链接共享库的语言编写。
- **版本安全**：使用 `NEVERC_API_FN(api, Field)` 在调用前检查可选 vtable 条目。旧头文件编译的插件仍与新版宿主兼容。

## 3. 插件入口点

每个插件必须导出一个函数：

```c
NEVERC_EXPORT NevercPluginInfo nevercGetPluginInfo(void);
```

返回的 `NevercPluginInfo` 结构体包含：

| 字段 | 类型 | 说明 |
|------|------|------|
| `APIVersion` | `uint32_t` | 必须为 `NEVERC_PLUGIN_API_VERSION` |
| `PluginName` | `const char *` | 可读名称 |
| `PluginVersion` | `const char *` | 语义化版本字符串 |
| `RegisterPasses` | 函数指针 | 调用一次以注册所有 pass |
| `Destroy` | 函数指针 | 可选清理，可为 `NULL` |

## 4. Pass 类型

### 4.1 Module Pass（IR）

操作 LLVM IR 模块。可读取和修改 IR。

```c
typedef int (*NevercModulePassFn)(NevercModuleRef M,
                                  const NevercHostAPI *API,
                                  void *UserData);
```

返回非零值表示模块被修改。

注册方式：
```c
API->RegisterModulePass(Registrar, hook, callback, userData, "pass-name");
```

### 4.2 Machine Pass（MIR）

操作机器级 IR（指令选择之后）。

```c
typedef int (*NevercMachinePassFn)(NevercMachineFuncRef MF,
                                   const NevercHostAPI *API,
                                   void *UserData);
```

### 4.3 Binary Pass

操作原始字节（shellcode 提取、二进制补丁）。

```c
typedef int (*NevercBinaryPassFn)(uint8_t **Data, uint64_t *Len,
                                  uint64_t *Capacity,
                                  const NevercHostAPI *API,
                                  void *UserData);
```

可通过 `API->BinaryResize()` 调整缓冲区大小。

### 4.4 Linker Pass

在链接时操作，可访问符号和节区。

```c
typedef int (*NevercLinkerPassFn)(const NevercHostAPI *API, void *UserData);
```

## 5. 钩子点

钩子决定 pass **何时**在流水线中运行。

### 常规流程

| 钩子 | 层级 | 说明 |
|------|------|------|
| `NEVERC_HOOK_PRE_OPT` | IR | LLVM 优化 pass 之前 |
| `NEVERC_HOOK_POST_OPT` | IR | LLVM 优化 pass 之后 |
| `NEVERC_HOOK_PIPELINE_START` | IR | 流水线最开始 |
| `NEVERC_HOOK_PIPELINE_LAST` | IR | IR 流水线最后 |
| `NEVERC_HOOK_BEFORE_CODEGEN_PREEMIT` | MIR | pre-emit 机器 pass 之前 |
| `NEVERC_HOOK_AFTER_CODEGEN_FINAL_MIR` | MIR | 所有机器 pass 之后 |

### Shellcode 流程

| 钩子 | 层级 | 说明 |
|------|------|------|
| `NEVERC_HOOK_SC_BEFORE_PREP` | IR | shellcode IR 准备之前 |
| `NEVERC_HOOK_SC_AFTER_PREP` | IR | PIC 准备之后 |
| `NEVERC_HOOK_SC_BEFORE_INLINING` | IR | always-inliner 之前 |
| `NEVERC_HOOK_SC_AFTER_INLINING` | IR | 内联 + stackify 之后 |
| `NEVERC_HOOK_SC_AFTER_STACKIFY` | IR | 栈变换之后 |
| `NEVERC_HOOK_SC_AFTER_FINAL_IR` | IR | 代码生成前的最终 IR |
| `NEVERC_HOOK_SC_BEFORE_PREEMIT` | MIR | MIR 准备之前 |
| `NEVERC_HOOK_SC_AFTER_PREEMIT` | MIR | MIR 准备之后 |
| `NEVERC_HOOK_SC_AFTER_FINAL_MIR` | MIR | 发射前的最终 MIR |
| `NEVERC_HOOK_SC_POST_EXTRACT` | 二进制 | 字节提取之后 |
| `NEVERC_HOOK_SC_POST_FINALIZE` | 二进制 | 所有后处理之后 |

### LTO 流程

| 钩子 | 层级 | 说明 |
|------|------|------|
| `NEVERC_HOOK_LTO_PRE_OPT` | IR | LTO 优化之前 |
| `NEVERC_HOOK_LTO_POST_OPT` | IR | LTO 优化之后 |

### 链接器流程

| 钩子 | 层级 | 说明 |
|------|------|------|
| `NEVERC_HOOK_LINK_PRE_LAYOUT` | 链接器 | 节区布局之前 |
| `NEVERC_HOOK_LINK_POST_LAYOUT` | 链接器 | 节区布局之后 |
| `NEVERC_HOOK_LINK_POST_EMIT` | 链接器 | 二进制发射之后 |

## 6. 不透明句柄类型

所有 IR/MIR 对象通过不透明句柄访问。句柄**仅在接收它们的 pass 回调作用域内有效**。

| 句柄 | 表示 |
|------|------|
| `NevercModuleRef` | LLVM Module |
| `NevercValueRef` | LLVM Value（函数、指令、全局变量） |
| `NevercBasicBlockRef` | LLVM BasicBlock |
| `NevercTypeRef` | LLVM Type |
| `NevercBuilderRef` | IR Builder（`BuilderCreate` 创建，`BuilderDispose` 释放） |
| `NevercContextRef` | LLVM Context |
| `NevercMachineFuncRef` | 机器函数 |
| `NevercMachineBBRef` | 机器基本块 |
| `NevercMachineInstrRef` | 机器指令 |
| `NevercUseRef` | Use-def 链条目 |
| `NevercLinkerSymbolRef` | 链接器符号 |
| `NevercLinkerSectionRef` | 链接器节区 |

## 7. 数据结构

宿主通过 vtable 提供多种高性能数据结构。所有数据结构都是不透明的，必须在 pass 返回前释放。

### Arena（bump-pointer 分配器）

```c
NevercArenaRef A = NEVERC_TRY_ARENA(API);  // 旧宿主返回 NULL
// ... 使用 ArenaAllocArray、ArenaStrDup 等分配
API->ArenaDestroy(A);  // 一次性释放所有 arena 分配
```

最适合收集-然后-处理的工作流。用一个 `ArenaDestroy` 替代 N 个 `Free` 调用。

### StrMap（字符串键哈希表）

```c
NevercStrMapRef Map = NEVERC_STRMAP_NEW(API, 64);
API->StrMapPut(Map, "key", 42);
uint64_t Val;
if (API->StrMapGet(Map, "key", &Val)) { /* 找到 */ }
API->StrMapDestroy(Map);
```

### IntMap（整数键哈希表）

```c
NevercIntMapRef Map = NEVERC_INTMAP_NEW(API, 128);
API->IntMapIncrement(Map, opcode, 1);
API->IntMapDestroy(Map);
```

### StrBuilder（增量字符串构建）

```c
NevercStrBuilderRef SB = API->StrBuilderCreate();
API->StrBuilderAppendF(SB, "count=%u", 42);
NEVERC_STRBUILDER_DIAG(API, SB, DiagNote);  // 发射且不分配
API->StrBuilderDestroy(SB);
```

### ValueSet（值哈希集合）

```c
NevercValueSetRef Set = API->ValueSetCreate();
API->ValueSetInsert(Set, someValue);
if (API->ValueSetContains(Set, someValue)) { /* 在集合中 */ }
API->ValueSetDestroy(Set);
```

## 8. 版本兼容性

vtable 单调增长。使用以下宏检查新增条目：

```c
// 检查字段在 vtable 中是否存在（仅布局检查）。
if (NEVERC_API_HAS(API, SomeNewFunction)) { ... }

// 检查字段是否存在且非 NULL（安全调用）。
if (NEVERC_API_FN(API, SomeNewFunction)) {
    API->SomeNewFunction(...);
}
```

旧头文件编译的插件仍与新版宿主兼容。在旧宿主上调用新 API 的插件必须用 `NEVERC_API_FN` 保护。

## 9. 插件参数

通过 CLI 传递参数：

```bash
neverc -fplugin-pass=./MyPlugin.dll \
       -fplugin-pass-arg=verbose=1 \
       -fplugin-pass-arg=max-fns=100 \
       input.c -o output.obj
```

在插件中读取：

```c
if (NEVERC_API_FN(API, PluginGetArg)) {
    const char *val = API->PluginGetArg("verbose");  // "1" 或 NULL
}

// 类型化访问器（较新宿主）：
int verbose   = API->PluginGetArgBool("verbose", 0);     // 默认 0
int64_t limit = API->PluginGetArgInt64("max-fns", -1);   // 默认 -1
```

## 10. 生命周期规则

| 资源 | 生命周期 | 清理 |
|------|---------|------|
| 不透明句柄（`NevercModuleRef`、`NevercValueRef` 等） | 在 pass 回调内有效 | 无需释放 |
| `ValueGetName`、`ModuleGetTargetTriple` 返回的 `const char*` | 拥有对象存在期间有效 | 无需释放 |
| `NevercBuilderRef` | 由 `BuilderCreate` 创建 | 返回前调用 `BuilderDispose` |
| `StrDup`、`StrFormat`、`ValuePrintToString` 返回的堆字符串 | 调用者拥有 | 调用 `Free` |
| Arena 分配（`ArenaAlloc`、`ArenaStrDup` 等） | 归 Arena 所有 | `ArenaDestroy`（批量释放） |
| `NevercDynArrayRef` / `NevercStrMapRef` / `NevercIntMapRef` / `NevercStrBuilderRef` | 由 `*Create` 创建 | 对应的 `*Destroy` |
| `DiagNoteF` / `DiagWarningF` / `DiagErrorF` 的诊断字符串 | 调用消费 | 无需释放 |

## 11. 便捷宏

| 宏 | 用途 |
|----|------|
| `NEVERC_FOR_EACH_FUNCTION(api, m, var)` | 遍历所有函数 |
| `NEVERC_FOR_EACH_DEFINED_FUNCTION(api, m, var)` | 遍历已定义（非声明）函数 |
| `NEVERC_FOR_EACH_BB(api, fn, var)` | 遍历基本块 |
| `NEVERC_FOR_EACH_INST(api, bb, var)` | 遍历指令 |
| `NEVERC_FOR_EACH_USE(api, val, var)` | 遍历 use-def 链 |
| `NEVERC_FOR_EACH_MBB(api, mf, var)` | 遍历机器基本块 |
| `NEVERC_FOR_EACH_MI(api, mbb, var)` | 遍历机器指令 |
| `NEVERC_ALLOC_ARRAY(api, type, count)` | 类型化堆分配 |
| `NEVERC_ARENA_ALLOC_ARRAY(api, arena, type, count)` | 类型化 arena 分配 |
| `NEVERC_TRY_ARENA(api)` | 创建 arena（旧宿主返回 `NULL`） |
| `NEVERC_AUTO_COLLECT_*(api, arena, ...)` | arena 优先批量收集 + 堆回退 |
| `NEVERC_FREE_IF_HEAP(api, ptr, arena)` | 仅非 arena 所有时释放 |
| `NEVERC_ARENA_DESTROY(api, arena)` | 非 NULL 时销毁 arena |
| `NEVERC_API_FN(api, field)` | 检查 vtable 条目存在性 + 非 NULL |
| `NEVERC_HOOK_UD(hook)` | 将钩子枚举转换为 `void*` UserData |
| `NEVERC_HOOK_NAME(api, ud)` | 从 UserData 解析钩子名称 |
| `NEVERC_STR_OR(s, def)` | 非 NULL 且非空返回 `s`，否则 `def` |
| `NEVERC_MIN(a, b)` / `NEVERC_MAX(a, b)` | 编译时最小/最大值 |

## 12. 最佳实践

1. **Arena 优先分配**：临时数据使用 `NEVERC_TRY_ARENA` + `NEVERC_ARENA_ALLOC_ARRAY`。一个 `ArenaDestroy` 替代 N 个 `Free`。
2. **版本保护新 API**：始终用 `NEVERC_API_FN` 包裹新版 vtable 调用。
3. **优先使用回调迭代**：`ModuleForEachDefinedFunction` 比 `NEVERC_FOR_EACH_DEFINED_FUNCTION` 更快（一次 vtable 调用 vs N 次）。
4. **分层回退**：通过先尝试批量 API，再回退到回调迭代，最后用逐元素循环，支持多版本宿主。参见 `ExamplePlugin.c` 的规范模式。
5. **无 CRT 依赖**：vtable 提供 `Alloc`、`Free`、`MemSet`、`MemCopy`、`StrDup`、`StrFormat`、`Sort` 等。不要直接调用 `malloc`、`printf` 或 `qsort` — 确保跨 DLL CRT 安全。
6. **干净返回**：pass 回调返回前释放所有 Builder，销毁所有数据结构和 Arena。

## 13. Plugin SDK 内容

随 NeverC 分发的 `pluginsdk/` 目录包含：

```
pluginsdk/
├── include/
│   └── neverc/
│       └── Plugin/
│           └── NevercPluginAPI.h    # 唯一需要的头文件
└── examples/
    ├── Makefile             # 独立构建模板
    ├── ExamplePlugin.c      # 综合演示（IR + MIR + Binary + LTO + Linker）
    ├── CrtShimPlugin.c      # 零 CRT 依赖概念验证
    └── BenchPlugin.c        # HostAPI 吞吐量微基准测试
```

## 14. 相关文档

- [Shellcode 插件接口](../shellcode-compiler/plugin-interface/README.zh-CN.md) — shellcode 流水线的 in-tree C++ 扩展点（与本 out-of-tree C API 分开）。
