**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 编译器](../README.zh-CN.md)

# Shellcode 插件接口（Plugin SDK）

NeverC 的 shellcode 流水线采用**核心流水线 + 可插拔用户层**双重结构。混淆、反反汇编、EDR 规避、分阶段编码器（XOR / RC4 / 自解密）等策略层功能**有意不内置** — 它们通过本文档描述的插件接口集成。

主要扩展点在 [Plugin.h](../../neverc/include/neverc/Shellcode/Pipeline/Plugin.h) 和 [Pipeline.h](../../neverc/include/neverc/Shellcode/Pipeline/Pipeline.h)。前者提供 finalize 阶段（字节流后处理）的 SDK；后者提供 IR / MIR / 字节级混淆的钩子。本文档聚焦前者。

## 1. Finalize 流水线

`extractMachO` / `extractELF` / `extractCOFF` 在写入 `.bin` 前调用 `finalizeShellcodeBytes`，按此顺序处理 `.text` 字节流：

1. `applyPostExtractObfuscationHook`（`ObfuscationHooks::RunPostExtract`，字节级**前 finalize** 钩子）。
2. **坏字节重写器链**：按注册顺序迭代 `getBadByteRewriteStrategies()` 策略，串行执行。策略可改变字节流长度。
3. **字符集编码器**：设置 `-fshellcode-charset=<name>` 时，调用 `getCharsetEncoder(name)` 获取 `(Encode, Stub, IsCharsetMember)`。输出变为 `bin = Stub(target) || Encode(text, target)`，所有字节验证通过字符集。
4. `auditFinalBadBytes`：将 `Opts.BadBytes` 与最终字节流交叉检查。
5. **大小约束**：应用 `-fshellcode-align=` / `-fshellcode-max-length=` / `-fshellcode-pad=`。
6. `applyPostFinalizeObfuscationHook`（`ObfuscationHooks::RunPostFinalize`，字节级**后 finalize** 钩子）。NeverC 不再审计。

## 2. 坏字节重写器（`BadByteRewriteStrategy`）

签名摘要：

```cpp
struct BadByteRewriteContext {
  llvm::SmallVectorImpl<uint8_t> *Bytes;
  llvm::ArrayRef<uint8_t> BadBytes;
  const TargetDesc *Target;
  const ShellcodeOptions *Opts;
};

enum class BadByteRewriteResult { NotApplied, Applied, Error };
using BadByteRewriteStrategy =
    std::function<BadByteRewriteResult(BadByteRewriteContext &)>;

size_t registerBadByteRewriteStrategy(BadByteRewriteStrategy);
```

约束：
- 策略必须**幂等**：finalize 按注册顺序串行调用每个策略。
- 策略必须**确定性**。
- 策略**只看字节流** — 不假设可访问 IR / MIR。

## 3. 字符集编码器（`CharsetEncoderEntry`）

```cpp
struct CharsetEncoderEntry {
  std::string Name;
  std::function<SmallVector<uint8_t, 256>(ArrayRef<uint8_t>, const TargetDesc &)> Encode;
  std::function<SmallVector<uint8_t, 256>(const TargetDesc &)> Stub;
  std::function<bool(uint8_t)> IsCharsetMember;
};
void registerCharsetEncoder(CharsetEncoderEntry);
```

约束：`Stub(target)` 输出字节必须自身满足 `IsCharsetMember`；`Encode` 输出字节也必须全部满足。

NeverC 不附带内置字符集。CLI 用法：`neverc -fshellcode -fshellcode-charset=printable -target x86_64-pc-windows-msvc payload.c -o payload.bin`

## 4. 大小 / 对齐 / 填充（无需插件）

- `-fshellcode-max-length=<bytes>`：超过则硬失败。
- `-fshellcode-align=<bytes>`：必须是 2 的幂。
- `-fshellcode-pad=<hex>`：填充字节。

## 5. 三层钩子前/后映射

`Pipeline.h::ObfuscationHooks` 是混淆 / 多态 / 分阶段编码器等"用户逻辑层"关切的入口点。

| 层 | 前钩子 | 后钩子 |
|----|--------|--------|
| **IR** | `RunBeforePrep`、`RunAfterPrep`、`RunBeforeInlining` | `RunAfterInlining`、`RunAfterStackify`、`RunAfterFinalIR` |
| **MIR** | `RunBeforePreEmit` | `RunAfterPreEmit`、`RunAfterFinalMIR` |
| **字节流** | `RunPostExtract` | `RunPostFinalize` |

## 6. 注册位置选择 + PIC 覆盖矩阵

**TL;DR**：注册位置是契约。越早注册 = 内置 PIC 修复覆盖越广。越晚注册 = 自由度越大，但你必须自行确保 PIC 干净输出。

### 6.2 PIC 覆盖矩阵

| 钩子 | 之后运行的内置 PIC 修复 | 安全引入 |
|------|------------------------|---------|
| **① RunBeforePrep** | 所有后续 pass | 几乎一切 |
| **② RunAfterPrep** ★ | IndirectBr → MemIntrin → ... → AllBlr | mem* 调用、字符串 GV、`__int128`、POSIX/Win32 extern |
| **④ RunAfterInlining** ★ | Data2Text 2 → ZeroReloc(Stackify) → AllBlr | 可变 GV、栈 alloca、立即数 |
| **⑥ RunAfterFinalIR** | (无) | 必须确保：无 mem*、无 extern、无可变 GV |
| **⑩ RunPostExtract** | 坏字节重写器 → 字符集编码器 → 审计 → 大小 | 任意字节 |
| **⑪ RunPostFinalize** | (无) | 必须自行确保合规 |

### 6.3 按混淆类型推荐注册位置

| 混淆类型 | 推荐钩子 | 理由 |
|----------|---------|------|
| 字符串加密 | `RunAfterPrep` ★ | 解码器引入 mem* → 由 MemIntrin 处理 |
| CFF（控制流平坦化） | `RunAfterInlining` ★ | 单入口函数最大化 |
| 指令替换 | `RunAfterPreEmit` | 中粒度；与 LLVM 后端协作 |
| 全载荷 XOR/RC4 | `RunPostFinalize` | 写入磁盘前最后时刻 |

### 6.5 多混淆库共存

```cpp
auto Existing = neverc::shellcode::getShellcodeObfuscationHooks();
auto Old = std::move(Existing.RunAfterInlining);
Existing.RunAfterInlining = [Old = std::move(Old)](
    llvm::ModulePassManager &MPM,
    const neverc::shellcode::ShellcodeOptions &Opts) {
  if (Old) Old(MPM, Opts);
  MPM.addPass(MyCFFPass());
};
neverc::shellcode::setShellcodeObfuscationHooks(std::move(Existing));
```

`registerBadByteRewriteStrategy` 和 `registerCharsetEncoder` 使用追加语义，不需要此合并模式。
