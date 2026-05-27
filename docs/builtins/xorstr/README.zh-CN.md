**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 内置运行时系统](../README.zh-CN.md)

# 编译期字符串加密 (`xorstr`)

## 概述

NeverC 提供两层编译期字符串加密机制，专为安全场景设计——确保 API 名称、注册表路径、调试信息等敏感字符串在编译后的二进制文件中不以明文出现。

- **第 1 层 — 显式宏**：`NC_XORSTR("string")` / `NEVERC_XORSTR("string")`，逐字符串精确控制
- **第 2 层 — 自动 IR Pass**：`-fencrypt-call-strings`，自动加密函数调用中的所有字符串参数

两层机制均使用栈分配缓冲区（无堆分配），使用无 XOR 指令特征的解密算法（反签名检测），并在函数返回前通过 volatile `memset` 清零栈上明文。

---

## 快速上手

### 第 1 层：显式宏

```c
#include <neverc/xorstr.h>

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
#include <neverc/xorstr.h>

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

1. **Sema 层（编译期）**：`__builtin_neverc_xorstr("hello")` 使用基于编译时间 + 计数器的独立 XOR 密钥加密字符串字节
2. **重写**：builtin 调用被替换为 `__neverc_xorstr_decrypt(encrypted_literal, len, key)`
3. **运行时**：`always_inline` 辅助函数通过 `__builtin_alloca` 分配栈缓冲区，使用反 XOR 签名算法（`a + b - 2*(a & b)`）逐字节解密，返回 `const char*`
4. **清零**：`XorStrCleanupPass` 在每个 `ret` 指令前插入 `volatile memset(buf, 0, size)` 清除栈上明文

### 反签名解密

解密操作完全避免使用 XOR 指令。`a ^ b` 的等价计算为：

```
dec(a, b) = a + b − 2 × (a & b)
```

结合密钥变量的 `volatile` 修饰符，可防止：
- XOR 解密循环的模式匹配
- 优化器的常量折叠（密钥保持不透明）
- YARA/签名检测 XOR 解密例程

---

## 第 2 层：`-fencrypt-call-strings`（自动模式）

### 用法

```bash
neverc -fencrypt-call-strings main.c -o main
```

此 IR Pass 在所有优化完成后（Post pass 阶段）运行，自动加密非 intrinsic 函数调用中的每个字符串字面量参数。

### 选项

| 标志 | 说明 | 默认值 |
|------|------|--------|
| `-fencrypt-call-strings` | 启用自动加密 | 关闭 |
| `-fno-encrypt-call-strings` | 禁用（覆盖 `-fencrypt-call-strings`） | — |
| `-fencrypt-call-strings-max-len=N` | 跳过超过 N 字节的字符串 | 1024 |

### 加密范围

Pass 处理所有 `CallInst` / `InvokeInst` 参数中引用的：
- `ConstantDataArray` 全局变量，元素类型为 `i8`（char）、`i16`（wchar_t/char16_t）或 `i32`（char32_t）

### 跳过条件

| 条件 | 原因 |
|------|------|
| LLVM intrinsic（`llvm.memcpy`、`llvm.dbg.*` 等） | 编译器内部原语，非用户代码 |
| 间接调用 / inline asm | 无法确定被调用函数 |
| EH pad 块（`catchpad`、`cleanuppad`） | 异常处理块不可重构 |
| 超过 `-fencrypt-call-strings-max-len` 的字符串 | 避免大字符串导致栈压力过大 |
| 已标记 `!neverc.xorstr` 元数据的字符串 | 防止重复加密 |

---

## 栈清零（`XorStrCleanupPass`）

解密后，明文驻留在栈上。`XorStrCleanupPass`（FunctionPass）确保其不会在函数返回后残留：

1. 扫描所有带 `!neverc.xorstr` 元数据的 `AllocaInst`
2. 在每个 `ReturnInst` 前插入 `llvm.memset(buf, 0, size, volatile=true)`
3. `volatile` 标志防止优化器将清零操作当作死存储消除

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

两种机制共享相同的编译期 XOR 加密逻辑和反签名解密算法。

---

## 编译器标志参考

| 标志 | 说明 |
|------|------|
| `-fencrypt-call-strings` | 启用函数调用参数的自动字符串加密 |
| `-fno-encrypt-call-strings` | 禁用自动加密 |
| `-fencrypt-call-strings-max-len=N` | 自动加密的最大字节长度（默认：1024，0 = 无限制） |
| `-fstring-encrypt-key=0xHEX` | 覆盖 XOR 密钥种子（与 `.encrypt()` 共享，默认：基于编译时间） |
