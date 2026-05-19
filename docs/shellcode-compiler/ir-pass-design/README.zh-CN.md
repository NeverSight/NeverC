**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 编译器](../README.zh-CN.md)

# IR Pass 设计 — 原则、流水线与前后对比

> 本文档解释 shellcode 编译流水线中每个 pass 的 **设计理由**。实现细节在 `.cpp` 源文件的英文注释中。

---

## 0. 核心理念

shellcode 的目标一句话概括：**消除 `.o` 中一切会变成重定位的东西，只留下可以直接 `mmap(RWX)` + `memcpy` + `blr` 的纯指令流。**

我们不想把这个约束泄漏给用户 — 用户应该写普通 C，流水线在内部处理产生重定位的 IR 构造。

这导出了以下 pass 分工：

| Pass | 职责 | 运行时机 |
|------|------|---------|
| ZeroRelocPass (Prep) | 统一链接属性 / 强制 always_inline / 拒绝硬阻塞 | PipelineStart |
| IndirectBrPass | 计算跳转 `indirectbr` → `switch` | PipelineStart |
| MemIntrinPass | `@llvm.mem*` + 显式 mem*/str*/abs 调用 → 内部字节循环辅助 | PipelineStart |
| StringRuntimePass | 内置 `string` 类型运行时 → 栈分配 arena 变体 | PipelineStart |
| CompilerRtPass | `__udivti3` 家族 + IR 级 i128 div/rem → 内部 128 位长除法 | PipelineStart |
| SyscallStubPass | libc extern → 目标 OS 内核陷入包装，通过 `TargetDesc` 表驱动 | PipelineStart |
| WinPEBImportPass | Win32 extern → PEB 模块遍历 + PE 导出解析器 + 加密地址缓存 | PipelineStart |
| KernelImportPass | Ring-0 extern → 解析器支持的间接调用 + 加密地址缓存（仅内核模式） | PipelineStart |
| Data2TextPass (Phase 1) | 常量 GV → 立即数 / 栈块存储 | PipelineStart |
| *(LLVM 标准优化)* | AlwaysInliner / SROA / InstCombine / SLP / ... | O-level |
| Data2TextPass (Phase 2) | 拆分 SROA 残留 `store <N x i8> <const>`，消费晚期常量 | OptimizerLast |
| ZeroRelocPass (Stackify) | 可变全局 → 入口 alloca + 最终验证 | OptimizerLast |
| AllBlrPass (可选) | 直接调用 → 间接调用 | OptimizerLast |
| MIRPrepPass | MIR 兜底：剥离 CFI/EH/XRay/SEH/等伪指令 | Before addPreEmitPass |
| ShellcodeExtractor | 扫描 `.o` 进行最终审计 + 输出 flat `.bin` | 后处理 |

**为什么 pass 要运行两次？** 因为 AlwaysInliner / SROA / 向量化器前后的 IR 形态变化显著，每个阶段都必须将 IR 清理到"无产生重定位的使用"状态。

---

## 1. ZeroRelocPass

### 1.1 Prep 阶段 — 链接属性统一

**目标**：使模块只包含一个用户可见符号（入口），其余全部 `internal` + `alwaysinline`。这消除了跨函数直接调用，防止 AArch64 上的 `ARM64_RELOC_BRANCH26` 残留。

**动作**：
- 所有非入口函数 → `internal` 链接 + `alwaysinline` 属性
- 拒绝硬阻塞：`__attribute__((constructor))`、`external_weak`、`extern_weak`
- `_Thread_local` → 静默降级为 static（shellcode 在宿主的调用线程上运行）
- 清除 `@llvm.used` / `@llvm.compiler.used`（防止 GV 被保活）

### 1.2 Stackify 阶段 — 全局变量消除

**目标**：将所有剩余可变全局变量移到入口函数的栈帧中。

**动作**：
- 对每个可变 GV：在入口创建 `alloca`，从 GV 初始化器初始化，替换所有使用
- 删除前调用 `GV->removeDeadConstantUsers()`（处理孤儿 ConstantExpr GEP）
- 最终验证：拒绝任何剩余的用户 GV，带可操作诊断

### 1.3 `placeEntryFirst`

所有改写后，将入口函数移到 `Module::getFunctionList()` 前端，确保在输出 `.bin` 中位于偏移 0。处理递归函数和无法内联的 `noinline` 辅助函数。

---

## 2. IndirectBrPass

**问题**：GCC 计算跳转（`goto *labels[op]`）产生引用 `BlockAddress` 表的 `indirectbr` 指令，后端将其放在数据段中带 `ARM64_RELOC_UNSIGNED` 重定位。

**方案**：模式匹配 `indirectbr` 分发（包括 `-O0` 下的多分发点 phi 汇聚），重写为基于索引的 `switch`。配合 `-fno-jump-tables`，switch 变为比较分支阶梯，零重定位零数据段。

---

## 3. SyscallStubPass

**问题**：extern libc 调用（`write`、`read`、`exit`、`mmap` ...）产生无法解析的重定位。

**方案**：对每个匹配 syscall 表的 extern，生成 `always_inline` 包装器发出平台的内联汇编陷入：
- Darwin arm64：`svc #0x80`（x16 = nr）
- Darwin x86_64：`syscall`，带 BSD class mask `0x2000000`
- Linux arm64：`svc #0`（x8 = nr）
- Linux x86_64：`syscall`（rax = nr）

所有模板和寄存器约束来自 `TargetDesc` — pass 零架构特定分支。

**POSIX 兼容层**（arm64 Linux/Android）：没有直接 syscall 号的经典调用（如 `open`）自动转换为 `*at` 等价（如 `open(path, flags)` → `openat(AT_FDCWD, path, flags, 0)`）。

**K&R 自动修复**：检测到 0 形参隐式声明时，pass 从 50+ 函数表中替换为规范 POSIX 签名。

---

## 4. WinPEBImportPass

**问题**：Windows 没有稳定的 syscall ABI；Win32 API 必须通过 PEB walk 解析。

**方案**：对每个匹配 `WinImportTables` 白名单（约 190 API，跨 6 个 DLL）的 extern：
1. 生成 `always_inline` 包装器，包含 fast/slow 路径和加密地址缓存
2. **Fast path**（缓存命中）：`atomic_load(cache) → 解密 → 间接调用`（约 10 条指令）
3. **Slow path**（缓存未命中）：完整 PEB → Ldr → InMemoryOrderModuleList 遍历，ROR-13 哈希匹配 → `加密 → cmpxchg` → 调用
4. 每个平台只有一条内联汇编指令：`movq %gs:0x60, $0`（x86_64）/ `ldr $0, [x18, #0x60]`（arm64）
5. 所有列表遍历、PE 头解析和哈希比较都是纯 LLVM IR
6. 已解析地址用 `PEB_BASE ^ 编译时随机种子` 异或加密后缓存（防内存扫描）
7. 缓存槽为 per-(DLL, API) 全局变量，放置在 `.text` section；通过 `cmpxchg` 保证线程安全（lock-free）

**可插拔加密**：三个函数（`__sc_derive_key`、`__sc_ptr_encrypt`、`__sc_ptr_decrypt`）可由用户代码覆盖，替换默认 XOR 方案为自定义加密（AES、RC4 等）

**Windows POSIX 兼容**：`WinPEBImportPass` 通过 `Win32PosixCompat.def` 桥接 13 个 POSIX 函数组，使同一 C 源码无需 `#ifdef` 即可跨所有 8 个 triple 编译。

---

## 5. MemIntrinPass

**问题**：`memcpy`、`memset`、`strlen`、`strcpy` 等 — 显式调用和隐式 `@llvm.mem*` 内建函数 — 产生无法解析的 extern 调用。

**方案**：为每个函数生成 `internal alwaysinline` 字节循环辅助：
- `__sc_memcpy`：前向字节复制循环
- `__sc_memset`：常量字节存储循环
- `__sc_memmove`：运行时方向检查，前向/后向路径
- `__sc_memcmp`：循环至首个不匹配，返回 `(int)a[i] - (int)b[i]`
- `__sc_strlen`、`__sc_strcpy`、`__sc_strcmp`、`__sc_strchr`、`__sc_strrchr` 等
- `__sc_abs` / `__sc_labs` / `__sc_llabs`：`select (x slt 0) (sub 0 x) x`

每调用点 ABI 协调处理 Windows LLP64 `size_t == i32` 与 POSIX `i64` 差异。

---

## 6. CompilerRtPass

**问题**：`__int128` 除法/取模产生对 `__udivti3` / `__divti3` / `__umodti3` / `__modti3` / `__udivmodti4` 的调用。

**方案**：生成 `internal alwaysinline` 移位减法长除法辅助。仅使用常量 i128 移位和 i64 变量移位，避免 `__ashlti3` / `__lshrti3` 递归。

---

## 7. Data2TextPass

**问题**：常量数据（字符串字面量、const 数组、浮点常量）在 `.data` / `.rodata` / `.cstring` / `__literal*` 段中产生重定位。

### Phase 1（PipelineStart）

**动作**：
- 标量常量 GV → `ConstantInt`（直接替换）
- 常量数组 → 栈块存储（递归 `writeInto` + `getOrMaterialize` 处理嵌套结构、函数指针表、字符串指针表、自引用结构）
- `ConstantFP` → volatile 加载的整数位模式 + bitcast（防止后端字面量池溢出）
- 函数指针条目 → 直接存储函数指针（后端使用 `adrp+add+str`，段内 PAGE21/PAGEOFF12 由提取器修补）

### Phase 2（OptimizerLast）

**动作**：
- 拆分 SROA/向量化器生成的 `store <N x i8> <const>` 回个体 volatile 块存储（防止 x86_64 `MergeConsecutiveStores` 重聚合到 `.rodata.cst16`）
- 内联向量常量（`inlineVectorConstants`）：扫描非 store 指令中的向量类型常量操作数，通过每函数 i64 volatile 槽 + `insertelement` 链展开

---

## 8. AllBlrPass（可选）

由 `-fshellcode-all-blr` 激活。将剩余模块内直接调用转为通过 volatile 槽 + `blr xN` / `call *rax` 的间接调用，消除所有相对分支重定位。正常使用不需要，因为提取器会修补段内分支。

---

## 9. 混淆钩子

`Pipeline.h::ObfuscationHooks` 为第三方混淆 pass 提供 11 个钩子点。详见 [plugin-interface.md 第 6 节](../plugin-interface/README.zh-CN.md#6-registration-position-selection--pic-coverage-matrix) 的完整 PIC 覆盖矩阵和推荐注册位置。

关键设计原则：
- **注册位置是契约**：越早注册越有更广的内置 PIC 修复覆盖
- **钩子不解释 spec 字符串**：`-fshellcode-obfuscate=<spec>` 透传给 `ShellcodeOptions::ObfuscateSpec`；混淆库定义自有 DSL
- **多库共存**通过 get/modify/set 模式

---

## 10. 两阶段设计理据

为什么 Data2TextPass / ZeroRelocPass 各运行两次？

| 时机 | IR 形态 | 需要清理的内容 |
|------|---------|---------------|
| Phase 1（PipelineStart） | 用户的原始 IR，带 GV、const 数组、extern 调用 | 原始常量 GV、显式 mem* 调用、libc extern |
| *(LLVM 优化运行)* | AlwaysInliner 折叠辅助；SROA 拆分 alloca；SLP/向量化器创建向量常量 | 新向量常量 store、重物化的 FP 常量、合并的连续 store |
| Phase 2（OptimizerLast） | 优化后 IR | SROA 残留向量 store、GlobalOpt/IPSCCP 证明不写的晚期常量 GV、待栈化的可变全局 |

这确保"原始代码"和"优化器引入的代码"都被清理到零重定位状态。

---

## 11. KernelImportPass（仅 ring-0）

当 `-mshellcode-context=kernel` 时激活。自动重写未解析的 extern 直接调用为带加密地址缓存的解析器间接调用：

1. 模块含需要解析的 extern 直接调用时，入口签名前置 `(resolver, cookie)` 隐式参数
2. 每个 API 生成 `always_inline` 包装器，包含 fast/slow 路径：
   - **Fast path**：`atomic_load(cache) → 解密 → 间接调用`
   - **Slow path**：`resolver(FNV1a_hash(name), cookie) → 加密 → cmpxchg → 调用`
3. 保留可变参数（对 `printk` 至关重要）
4. 地址取用的 extern 不被包装；诊断引导用户传递预解析的函数指针
5. 默认加密：异或编译时种子（内核无 PEB）；用户可覆盖 `__sc_derive_key` 以使用 KPCR 等内核特有 key

详见 [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.zh-CN.md)。

---

## 12. StringRuntimePass

处理 NeverC 的内置 `string` 类型方法。shellcode 模式下使用 `string` 值时，此 pass 将堆分配的运行时调用改写为栈分配的 arena 变体，确保零外部依赖。

---

## 13. 错误诊断理念

每个硬错误产出恰好**一条可操作诊断**。模块级 `__neverc_shellcode_hard_error` 元数据哨兵防止级联噪声：一旦 `reportError` 设置标志，后续阶段（ZeroReloc Stackify 验证、提取器审计）静默提前返回。

诊断来源表驱动：
- `Tables/KernelHelperNames.def`：ring-0 辅助函数识别
- `ExtractorCommon::printExternHint`：libc / Win32 / compiler-rt 提示系统
- `MIRPrepPass::hintForExternalSymbol`：MIR 级 extern 审计

用户看到一条清晰的错误加修复方案，永远不会看到三条级联消息中只有一条是根因。
