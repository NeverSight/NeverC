**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 内置运行时系统](../README.zh-CN.md)

# 编译期字符串加密 (`xorstr`)

## 概述

NeverC 提供两层编译期字符串加密机制，专为安全场景设计——确保 API 名称、注册表路径、调试信息等敏感字符串在编译后的二进制文件中不以明文出现。

- **第 1 层 — 显式宏**：`NC_XORSTR("string")` / `NEVERC_XORSTR("string")`，逐字符串精确控制
- **第 2 层 — 自动 IR Pass**：`-fencrypt-call-strings`，自动加密函数调用中的所有字符串参数

两层机制均使用栈分配缓冲区（无堆分配）、逐实例密钥流以及函数返回前的 volatile 清零。到达真正的机器码边界时，显式 `NC_XORSTR` 的解码调用会被重新加密并展开到各自调用点；最终目标文件中不保留共享解码函数。

---

## 快速上手

### 第 1 层：显式宏

```c
#include <neverc/xorstr/xorstr.h>

// 字符串在编译期加密，运行时在栈上解密
FARPROC addr = GetProcAddress(hModule, NC_XORSTR("NtQuerySystemInformation"));
```

### 第 2 层：自动加密

```bash
neverc -fencrypt-call-strings main.c -o main
```

所有函数调用中的字符串字面量参数都会被自动加密——无需修改源代码。

---

## 第 1 层：`NC_XORSTR` / `NEVERC_XORSTR` 宏

### 用法

```c
#include <neverc/xorstr/xorstr.h>

const char *api = NC_XORSTR("GetProcAddress");     // 简写
const char *api = NEVERC_XORSTR("GetProcAddress");  // 全称（别名）
```

宏支持所有字符串字面量类型：

| 字面量 | 示例 | 支持情况 |
|--------|------|----------|
| 普通字符串 | `NC_XORSTR("hello")` | 支持 |
| UTF-8 | `NC_XORSTR(u8"hello 世界")` | 支持（折叠为 UTF-8） |
| 宽字符 | `NC_XORSTR(L"hello")` | 支持（折叠为 UTF-8） |
| UTF-16 | `NC_XORSTR(u"hello")` | 支持（折叠为 UTF-8） |
| UTF-32 | `NC_XORSTR(U"hello")` | 支持（折叠为 UTF-8） |

非字符串字面量参数会产生编译期错误：

```c
const char *s = get_string();
NC_XORSTR(s);  // error: expression is not a string literal
```

### 工作原理

1. **Sema（编译期）**：`__builtin_neverc_xorstr("hello")` 使用逐实例密钥加密字节。种子为 `0` 时编译器获取新鲜的操作系统随机熵；`-fstring-encrypt-key=` 可指定确定性的完整 64 位种子。
2. **中间 IR / LTO 输入**：builtin 被改写成不透明、不可特化的解码调用，使普通优化和 LTO 不会把明文重新折叠进 IR。
3. **最终机器码边界**：Finalizer 在编译器侧解开旧密文并重新加密，为每个调用点选择不同循环形态，直接展开解码逻辑；随后删除解码器、辅助函数图、ABI anchor、route state 和语义名称。
4. **清零**：在优化/provider 之前先插入 volatile 清零，最终尾部再幂等执行一次，以修复 CFG 变化后的插入位置。

### 解码器多样化

最终 Pass 可用多种等价形式表达逐字节合并，其中一种为：

```
dec(a, b) = a + b − 2 × (a & b)
```

状态调度、常量、密文以及表达式选择会随 seed 和调用点变化；volatile 状态/密文读取抑制常量折叠，`nooutline` 阻止 MachineOutliner 在 IR Finalizer 之后重新抽取共享解码器。这消除了供 IDA 直接识别或统一模拟的稳定独立函数，但不声称运行中的程序所需明文无法通过动态插桩取得。

---

## 第 2 层：`-fencrypt-call-strings`（自动模式）

### 用法

```bash
neverc -fencrypt-call-strings main.c -o main
```

该变换会在 IPO 前、普通优化后以及所有普通或插件 late IR 阶段之后执行。LTO 也会在 provider 和 pre-codegen hook 之后执行强制封口：前置执行保护尚未丢失的字面量来源，末尾执行捕获后续新引入的字面量。

### 选项

| 标志 | 说明 | 默认值 |
|------|------|--------|
| `-fencrypt-call-strings` | 启用自动加密 | 关闭 |
| `-fno-encrypt-call-strings` | 禁用（覆盖 `-fencrypt-call-strings`） | — |
| `-fencrypt-call-strings-max-len=N` | 跳过超过 N 字节的字符串 | 1024 |

### 加密范围

Pass 处理直接或间接 `CallBase` 参数中可追溯到编译器私有 `unnamed_addr` 字面量存储的 `i8`、`i16` 或 `i32` 数组。它支持常量/动态 GEP、cast、freeze、select、PHI 以及可提升的局部指针槽，并保持同一源字面量在一次函数调用中的基址、内部偏移和指针同一性。

### 跳过条件

| 条件 | 原因 |
|------|------|
| LLVM intrinsic（`llvm.memcpy`、`llvm.dbg.*` 等） | 编译器内部原语，非用户代码 |
| inline asm | 其操作数和控制流约束不能安全重写 |
| 超过 `-fencrypt-call-strings-max-len` 的字符串 | 避免大字符串导致栈压力过大 |
| 对外可见或用户定义的常量数组 | 其符号、存储和指针身份属于程序 ABI |
| 通过 `musttail` 传递受保护字面量 | 编译失败关闭；栈缓冲区无法安全跨越合法的 `musttail` 转移 |

---

## 栈清零（`XorStrCleanupPass`）

解密后，明文驻留在栈上。`XorStrCleanupPass`（FunctionPass）确保其不会跨越任何正常或异常退出残留：

1. 扫描保护元数据以及仍存在的显式解码调用，定位相关 `AllocaInst`
2. 删除相关 lifetime marker，防止优化器在清零前复用明文栈槽
3. 在每个可达的 `ret`、`resume`、unwind-to-caller `cleanupret` 或未匹配 `catchswitch` 展开前插入 `llvm.memset(buf, 0, complete_size, volatile=true)`；直接 `catchswitch` 展开会经由 cleanup funclet
4. 对非栈或无法完整追踪的解码输出，以及动态、scalable、溢出或不支配退出路径的受保护栈槽明确编译失败，不猜测不安全的清零范围
5. `volatile` 标志防止优化器将清零操作当作死存储消除

---

## 与 `.encrypt()` 字符串方法的对比

| 方面 | `NC_XORSTR()` | `.encrypt()` |
|------|---------------|--------------|
| **可用性** | 纯 C（通过头文件） | 仅 NeverC 语法扩展 |
| **内存** | 栈（`alloca`） | 堆（`NEVERC_STRING_ALLOC`） |
| **返回类型** | `const char*` | `string`（值类型） |
| **生命周期** | 当前函数作用域 | 由 string 运行时管理 |
| **清零** | `ret` 前 `memset` | 由 string 运行时回收 |
| **适用场景** | Win32 API 调用、FFI | 通用字符串操作 |

两种机制共享完整 64 位 seed 控制和默认新鲜随机熵策略，但会根据各自不同的运行时生命周期采用不同表示。

---

## 编译器标志参考

| 标志 | 说明 |
|------|------|
| `-fencrypt-call-strings` | 启用函数调用参数的自动字符串加密 |
| `-fno-encrypt-call-strings` | 禁用自动加密 |
| `-fencrypt-call-strings-max-len=N` | 自动加密的最大字节长度（默认：1024，0 = 无限制） |
| `-fstring-encrypt-key=0xHEX` | 覆盖完整 64 位种子（与 `.encrypt()` 共享）；默认 `0` 使用新鲜随机熵 |

## 输出边界与可复现性

- `-fno-lto` 在前端生成本地机器码时完成 Finalize。
- Auto-LTO 和 Full LTO 的 pre-link bitcode 保留不透明显式解码器，待全程序及插件 IR 优化结束后再重新加密并逐调用点展开。
- provider 替换默认流水线、普通 late plugin pass 等路径之后，都有强制的加密、清零和 Finalize 尾部。
- 默认 seed 下，独立本地构建会产生不同结果；只要缓存回放可能跳过显式 xorstr 的新鲜 rekey，或跳过仅在 LTO 后暴露的字面量自动加密，就会自动绕过整链 LTO 缓存和分区缓存。
- 非零 `-fstring-encrypt-key` 刻意提供确定性：相同输入与相同完整 64 位 seed 产生相同保护代码，并可安全使用缓存。
- `-emit-llvm` 与 pre-link bitcode 属于中间产物，因此刻意保留不透明解码 ABI；“无共享解码器”保证针对成功生成的最终机器码产物。
