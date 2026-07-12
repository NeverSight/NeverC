**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 内置运行时系统](../README.zh-CN.md)

# 编译期字符串哈希 (`strhash`)

## 概述

NeverC 为纯 C 提供编译期与运行时字符串哈希，适合通过整数哈希相等做快速字符串分发——例如匹配 API 名、物品 ID、命令词——而无需在二进制中保留明文对照表。

- **第 1 层 — 显式编译期宏**：`NC_STRHASH("string")` / `NEVERC_STRHASH("string")` 在 Sema 阶段折叠为整数常量
- **第 2 层 — 运行时 + 可选 IR 折叠**：`neverc_strhash_rt` / `NC_STRHASH_AUTO`，配合 `-fstrhash-fold` 将参数为字符串字面量的运行时调用折叠为常量

两层共用 `-fstrhash-algo` 选定的算法（默认：FNV-1a 64-bit），保证编译期与运行时哈希始终一致。

---

## 快速上手

### 第 1 层：编译期宏

```c
#include <neverc/strhash/strhash.h>

static const uint64_t kApi = NC_STRHASH("NtQuerySystemInformation");

int is_api(const char *name) {
    return neverc_strhash_rt(name, strlen(name)) == kApi;
}
```

### 第 2 层：自动分发 + 折叠

```c
#include <neverc/strhash/strhash.h>

// 变量 → 运行时调用；字面量 + -fstrhash-fold → 常量
uint64_t h = NC_STRHASH_AUTO(name);
```

```bash
neverc -fstrhash-fold -fstrhash-algo=fnv64a main.c -o main
```

---

## 第 1 层：`NC_STRHASH` / `NEVERC_STRHASH` 宏

### 用法

```c
#include <neverc/strhash/strhash.h>

uint64_t h = NC_STRHASH("hello");      // 简写
uint64_t h = NEVERC_STRHASH("hello");  // 全称（别名）

// 可用于静态初始化与 _Static_assert
static const uint64_t kHello = NC_STRHASH("hello");
_Static_assert(NC_STRHASH("a") != NC_STRHASH("b"), "hashes differ");
```

宏支持所有字符串字面量类型：

| 字面量 | 示例 | 支持情况 |
|--------|------|----------|
| 普通字符串 | `NC_STRHASH("hello")` | 支持 |
| UTF-8 | `NC_STRHASH(u8"hello 世界")` | 支持（折叠为 UTF-8） |
| 宽字符 | `NC_STRHASH(L"hello")` | 支持（折叠为 UTF-8） |
| UTF-16 | `NC_STRHASH(u"hello")` | 支持（折叠为 UTF-8） |
| UTF-32 | `NC_STRHASH(U"hello")` | 支持（折叠为 UTF-8） |

非字符串字面量参数会产生编译期错误：

```c
const char *s = get_string();
NC_STRHASH(s);  // error: expression is not a string literal
```

变量请使用 `NC_STRHASH_AUTO(s)` 或 `neverc_strhash_rt(s, len)`。

### 工作原理

1. **Sema 层（编译期）**：`__builtin_neverc_strhash("hello")` 使用 `-fstrhash-algo` 选定的算法计算哈希
2. **重写**：builtin 调用被替换为 `IntegerLiteral`（`unsigned long long`）——不留下任何运行时调用
3. **结果**：纯编译期常量，可用于初始化器、类 `switch` 的 `if` 链以及 `_Static_assert`

---

## 哈希算法

| Algo ID | 标志值 | 函数 | 位宽 | 默认 |
|---------|--------|------|------|------|
| 1 | `fnv32a` | FNV-1a 32-bit（零扩展为 `uint64_t`） | 32 → 64 | |
| 2 | `fnv64a` | FNV-1a 64-bit | 64 | **是** |
| 3 | `xxhash64` | XXHash64（seed `0`） | 64 | |

```bash
neverc -fstrhash-algo=fnv32a main.c
neverc -fstrhash-algo=fnv64a main.c
neverc -fstrhash-algo=xxhash64 main.c
```

选定算法同时通过预处理器宏 `__NEVERC_STRHASH_ALGO__`（`1` / `2` / `3`）暴露，驱动 `strhash_impl.inc` 中的运行时分发。

---

## 运行时哈希：`neverc_strhash_rt`

```c
uint64_t neverc_strhash_rt(const void *data, size_t len);
```

内联辅助函数，调用与选定算法匹配的 NeverC std 哈希实现：

| `__NEVERC_STRHASH_ALGO__` | 运行时被调函数 |
|---------------------------|----------------|
| 1 (`fnv32a`) | `neverc_fnv_sum32a` |
| 2 (`fnv64a`) | `neverc_fnv_sum64a` |
| 3 (`xxhash64`) | `neverc_xxhash64(..., 0)` |

这些符号位于 NeverC std（`std/src/hash/`）。使用运行时哈希时需链接 std 哈希对象（或完整 NeverC std）。

### 典型模式：编译期表 + 运行时查找

```c
static const uint64_t valuable_items[] = {
    NC_STRHASH("苹果"),
    NC_STRHASH("香蕉"),
    NC_STRHASH("葡萄"),
};

int is_valuable(const char *name) {
    uint64_t h = neverc_strhash_rt(name, strlen(name));
    for (size_t i = 0; i < sizeof(valuable_items) / sizeof(valuable_items[0]); i++)
        if (h == valuable_items[i])
            return 1;
    return 0;
}
```

---

## `NC_STRHASH_AUTO` / `NEVERC_STRHASH_AUTO`

```c
#define NC_STRHASH_AUTO(s) neverc_strhash_rt((s), __builtin_strlen(s))
```

同时接受字面量与变量：

| 参数 | 无 `-fstrhash-fold` | 有 `-fstrhash-fold` |
|------|---------------------|---------------------|
| 字符串字面量 | 运行时调用 | 由 `StrHashFoldPass` 折叠为整数常量 |
| 变量 / 指针 | 运行时调用 | 运行时调用 |

```c
uint64_t auto_literal(void) {
    return NC_STRHASH_AUTO("hello");  // 配合 -fstrhash-fold 可折叠
}

int match_item(const char *name) {
    return NC_STRHASH_AUTO(name) == NC_STRHASH("苹果");
}
```

---

## 第 2 层：`-fstrhash-fold`（IR 常量折叠）

### 用法

```bash
neverc -fstrhash-fold main.c -o main
```

当 `LangOpts.StrHashFold` 开启时，`StrHashFoldPass` 在**后置 Pass** 阶段运行（优化流水线之后）。它扫描对 `neverc_fnv_sum32a`、`neverc_fnv_sum64a`、`neverc_xxhash64` 的调用：若数据参数为常量字符串全局、长度（以及 XXHash 的 seed）为常量，则将调用替换为整数常量。

### 选项

| 标志 | 说明 | 默认 |
|------|------|------|
| `-fstrhash-fold` | 启用对常量参数运行时哈希调用的 IR 折叠 | 关闭 |
| `-fno-strhash-fold` | 禁用折叠 | — |
| `-fstrhash-algo=<algo>` | 为 Sema builtin 与 fold pass 选择算法 | `fnv64a` |

### 折叠条件

| 条件 | 要求 |
|------|------|
| 被调为 `neverc_fnv_sum32a` / `neverc_fnv_sum64a` / `neverc_xxhash64` | 是 |
| 数据参数为常量字符串全局 | 是 |
| 长度参数为常量整数且 ≤ 字符串长度 | 是 |
| 对 XXHash64：seed 为常量 `0` | 是 |

非常量数据或长度参数保留为运行时调用。

---

## 自定义运行时哈希

在包含头文件前定义 `NC_STRHASH_HASH_FN`，可仅覆盖**运行时**路径：

```c
#define NC_STRHASH_HASH_FN(data, len) my_hash(data, len)
#include <neverc/strhash/strhash.h>

// neverc_strhash_rt / NC_STRHASH_AUTO 使用 my_hash
// NC_STRHASH() 仍使用 builtin / -fstrhash-algo
```

---

## 架构

```
┌─── 第 1 层：NC_STRHASH（显式） ───────────────────────────────┐
│                                                                 │
│  NC_STRHASH("GetPid")                                           │
│       │                                                         │
│       ▼ Sema: computeStrHash(bytes, algo)                       │
│       │                                                         │
│  IntegerLiteral (uint64_t) — 无运行时调用                       │
└─────────────────────────────────────────────────────────────────┘

┌─── 运行时 + AUTO ───────────────────────────────────────────────┐
│                                                                 │
│  neverc_strhash_rt(ptr, len) / NC_STRHASH_AUTO(s)               │
│       │                                                         │
│       ▼ always_inline 经 __NEVERC_STRHASH_ALGO__ 分发           │
│       │                                                         │
│  neverc_fnv_sum32a / neverc_fnv_sum64a / neverc_xxhash64        │
└─────────────────────────────────────────────────────────────────┘

┌─── 第 2 层：-fstrhash-fold（可选） ─────────────────────────────┐
│                                                                 │
│  call @neverc_fnv_sum64a(ptr @.str, i64 5)                      │
│       │                                                         │
│       ▼ StrHashFoldPass                                         │
│       │                                                         │
│  i64 <常量哈希>                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## 文件结构

```
neverc/
├── lib/Headers/neverc/strhash/
│   ├── strhash.h                    # NC_STRHASH / NC_STRHASH_AUTO 宏
│   └── strhash_impl.inc             # neverc_strhash_rt 内联分发
│
├── include/neverc/
│   ├── Foundation/
│   │   ├── Builtin/Builtins.def     # __builtin_neverc_strhash 注册
│   │   └── LangOpts/LangOptions.def # StrHashAlgo / StrHashFold 选项
│   ├── Invoke/Options.td.h          # CLI 标志 + marshalling
│   └── Transforms/StrHash/
│       ├── StrHashCompute.h         # 共享 FNV / XXHash 计算
│       └── StrHashFoldPass.h        # IR 折叠 Pass 头文件
│
├── lib/Analyze/Checking/
│   └── SemaCheckingBuiltinNeverC.cpp # semaBuiltinNeverCStrHash
│
├── lib/Transforms/StrHash/
│   ├── StrHashFoldPass.cpp          # 常量参数哈希调用折叠
│   └── CMakeLists.txt
│
├── lib/Emit/Backend/
│   └── BackendUtil.cpp              # Fold Pass 注册
│
├── lib/Invoke/ToolChains/
│   └── NeverC.cpp                   # Driver 标志转发
│
└── std/src/hash/                    # 运行时哈希实现
    ├── fnv/fnv.c                    # neverc_fnv_sum32a / sum64a
    └── xxhash/xxhash.c              # neverc_xxhash64
```

---

## 编译器标志参考

| 标志 | 说明 |
|------|------|
| `-fstrhash-algo=fnv32a` | Sema + fold + 运行时分发使用 FNV-1a 32-bit |
| `-fstrhash-algo=fnv64a` | 使用 FNV-1a 64-bit（默认） |
| `-fstrhash-algo=xxhash64` | 使用 XXHash64（seed 0） |
| `-fstrhash-fold` | 将常量字符串参数的运行时哈希调用折叠为整数 |
| `-fno-strhash-fold` | 禁用 IR 折叠 |
