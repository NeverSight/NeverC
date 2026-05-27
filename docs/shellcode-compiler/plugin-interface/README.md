**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode Compiler](../README.md)

# Shellcode Plugin Interface (Plugin SDK)

NeverC's shellcode pipeline has a **core pipeline + pluggable user layer** dual structure. Obfuscation, anti-disassembly, EDR evasion, staged encoders (XOR / RC4 / self-decrypting), and similar strategy-level features are **intentionally not built-in** — they are designed to be integrated through the plugin interfaces described here.

The main extension points are in [Plugin.h](../../neverc/include/neverc/Shellcode/Pipeline/Plugin.h) and [Pipeline.h](../../neverc/include/neverc/Shellcode/Pipeline/Pipeline.h). The former provides the SDK for the finalize stage (byte-stream post-processing); the latter provides hooks for IR / MIR / byte-level obfuscation. This document focuses on the former.

## 1. Finalize Pipeline

`extractMachO` / `extractELF` / `extractCOFF` call `finalizeShellcodeBytes` before writing the `.bin`, processing the `.text` byte stream in this order:

1. `applyPostExtractObfuscationHook` (`ObfuscationHooks::RunPostExtract`, byte-level **pre-finalize** hook).
2. **Bad-byte rewriter chain**: iterates `getBadByteRewriteStrategies()` strategies in registration order, executing serially. Strategies may change the byte stream length.
3. **Charset encoder**: when `-fshellcode-charset=<name>` is set, calls `getCharsetEncoder(name)` to get `(Encode, Stub, IsCharsetMember)`. Output becomes `bin = Stub(target) || Encode(text, target)`, with all bytes validated against the charset.
4. `auditFinalBadBytes`: cross-checks `Opts.BadBytes` against the final byte stream.
5. **Sizing**: applies `-fshellcode-align=` / `-fshellcode-max-length=` / `-fshellcode-pad=` for alignment, length limit validation, and padding.
6. `applyPostFinalizeObfuscationHook` (`ObfuscationHooks::RunPostFinalize`, byte-level **post-finalize** hook). NeverC performs no further auditing / alignment / length checks; the hook is responsible for ensuring output compliance.

Steps 1–5 hard-abort on failure via non-zero exit code + stderr diagnostics. Step 6 only provides the hook invocation opportunity without checking return values.

## 2. Bad-Byte Rewriter (`BadByteRewriteStrategy`)

Signature (excerpt):

```cpp
struct BadByteRewriteContext {
  llvm::SmallVectorImpl<uint8_t> *Bytes;   // read-write
  llvm::ArrayRef<uint8_t> BadBytes;        // forbidden byte set
  const TargetDesc *Target;                // OS / arch / format
  const ShellcodeOptions *Opts;            // full options snapshot
};

enum class BadByteRewriteResult { NotApplied, Applied, Error };

using BadByteRewriteStrategy =
    std::function<BadByteRewriteResult(BadByteRewriteContext &)>;

size_t registerBadByteRewriteStrategy(BadByteRewriteStrategy);
void   clearBadByteRewriteStrategies();
llvm::ArrayRef<BadByteRewriteStrategy> getBadByteRewriteStrategies();
```

Constraints:

- Strategies must be **idempotent**: the finalize stage calls each strategy serially in registration order, passing the cumulatively modified bytes to the next. `auditFinalBadBytes` determines whether the entire chain succeeded.
- Strategies must be **deterministic**: same input produces same output; finalize does not reorder strategies.
- Strategies **only see the byte stream** — do not assume access to IR / MIR. For MIR-level changes, use `Pipeline.h::ObfuscationHooks::RunBeforePreEmit/RunAfterPreEmit`.

Registration example (link your `libmybadbyterewriter.a` into `nevercShellcode`, then call once during initialization):

```cpp
#include "neverc/Shellcode/Pipeline/Plugin.h"

namespace {
struct Init {
  Init() {
    using namespace neverc::shellcode;
    registerBadByteRewriteStrategy([](BadByteRewriteContext &Ctx) {
      if (Ctx.Target->Arch != ShellcodeArch::X86_64)
        return BadByteRewriteResult::NotApplied;
      // Replace all 0x00 immediates with xor reg,reg equivalents.
      // See byvalver / Capstone-based tooling for reference implementations.
      return BadByteRewriteResult::Applied;
    });
  }
} init;
} // namespace
```

`-fno-shellcode-bad-byte-rewrite` disables the rewriter chain, falling back to audit-only mode.

## 3. Charset Encoder (`CharsetEncoderEntry`)

Signature (excerpt):

```cpp
struct CharsetEncoderEntry {
  std::string Name;
  std::string Description;
  std::function<llvm::SmallVector<uint8_t, 256>(
      llvm::ArrayRef<uint8_t>, const TargetDesc &)> Encode;
  std::function<llvm::SmallVector<uint8_t, 256>(
      const TargetDesc &)> Stub;
  std::function<bool(uint8_t)> IsCharsetMember;
};

void registerCharsetEncoder(CharsetEncoderEntry);
const CharsetEncoderEntry *getCharsetEncoder(llvm::StringRef Name);
void clearCharsetEncoders();
```

Constraints:

- `Stub(target)` output bytes **must themselves** satisfy `IsCharsetMember`; finalize validates this, and violations are hard errors.
- `Encode` output bytes must all satisfy `IsCharsetMember`.
- Encoders do not need to handle alignment / size constraints — `-fshellcode-align=` / `-fshellcode-max-length=` / `-fshellcode-pad=` are applied later in the finalize chain.

NeverC does not ship any built-in charsets. Typical downstream implementations:

- `printable`: reference Proteus (Internetware 2025) for ARMv8 178-byte stub + 0.25 information redundancy encoding; x86_64 can reference psc / Lycan.
- `alphanumeric`: alphanum-shellcode, alpha2, alpha3 style.
- Custom charset: `emoji-shellcoding` (WOOT 2023, RISC-V) and similar.

After registering a charset named `printable`, the CLI usage is:

```sh
neverc -fshellcode -fshellcode-charset=printable \
       -target x86_64-pc-windows-msvc payload.c -o payload.bin
```

## 4. Size / Alignment / Padding (no plugin required)

These are built-in finalize capabilities that do not require any plugin:

- `-fshellcode-max-length=<bytes>`: hard failure if final byte count (including stub + encoded payload + alignment / padding) exceeds the limit. Accepts decimal or `0x`-prefixed hex.
- `-fshellcode-align=<bytes>`: must be a power of 2; pads byte stream to a multiple of `align` when not 1.
- `-fshellcode-pad=<hex>`: the fill byte; used by both `align` and `max-length`. Only meaningful when at least one is enabled; the driver rejects other combinations. If the pad byte is also in `-fshellcode-bad-bytes` / profile, the driver also rejects (avoids padding that triggers audit failure).

## 5. Three-Layer Hook Pre/Post Mapping

`Pipeline.h::ObfuscationHooks` is the entry point for obfuscation / polymorphism / staged encoder and similar "user logic layer" concerns. Each layer exposes **pre-optimization** and **post-optimization** hooks:

| Layer | Pre hooks | Post hooks | Notes |
|-------|-----------|-----------|-------|
| **IR** | `RunBeforePrep`, `RunAfterPrep`, `RunBeforeInlining` (all in `PipelineStartEPCallback`, before AlwaysInliner / SROA / SLP / Vectorize) | `RunAfterInlining`, `RunAfterStackify`, `RunAfterFinalIR` (all in `OptimizerLastEPCallback`, after all LLVM standard optimizations) | `RunAfterFinalIR` fires after `AllBlrPass` and is the **true last** injectable point in the IR dimension |
| **MIR** | `RunBeforePreEmit` (before `MIRPrepPass`, pre-PreEmit slot) | `RunAfterPreEmit` (after `MIRPrepPass`, mid-granularity); `RunAfterFinalMIR` (**true last**: fork-extension adds `RegisterTargetPassConfigPostPreEmitCallbackFn` global list, invoked at the end of `addMachinePasses` after `addPreEmitPass2()`, just before AsmPrinter) | NeverC extends LLVM's `TargetPassConfig.h` with a post-PreEmit2 callback list specifically for this final slot |
| **Byte-stream** | `RunPostExtract` (finalize step 1; rewriter / encoder / audit / sizing will still modify bytes) | `RunPostFinalize` (finalize step 6; last moment before writing to disk) | After `RunPostFinalize`, NeverC performs no auditing / alignment / length checks. This is the correct place for XOR / RC4 / self-decrypting stubs |

Registering an obfuscation IR pass at `RunAfterFinalIR`:

```cpp
neverc::shellcode::ObfuscationHooks H;
H.RunAfterFinalIR = [](llvm::ModulePassManager &MPM,
                       const neverc::shellcode::ShellcodeOptions &Opts) {
  MPM.addPass(MyOpaquePredicatePass());
};
neverc::shellcode::setShellcodeObfuscationHooks(std::move(H));
```

Registering a MIR pass at `RunAfterFinalMIR`:

```cpp
H.RunAfterFinalMIR = [](llvm::TargetPassConfig &TPC,
                        const neverc::shellcode::ShellcodeOptions &Opts) {
  TPC.addExternalPass(new MyInstructionSubstitutionPass());
};
```

Registering a byte-level hook at `RunPostFinalize`:

```cpp
H.RunPostFinalize = [](llvm::SmallVectorImpl<uint8_t> &Bytes,
                       const neverc::shellcode::ShellcodeOptions &Opts) {
  applyMyXorWrapper(Bytes);
};
```

## 6. Registration Position Selection + PIC Coverage Matrix

> **TL;DR**: Registration position is a contract. Earlier registration = broader coverage by built-in PIC fixups. Later registration = more freedom, but you must ensure PIC-clean output yourself.

### 6.1 Full Dispatch Order

Each hook fires exactly once at its designated moment:

```
PipelineStartEP:
  ① RunBeforePrep
  [ZeroReloc(Prep)]
  ② RunAfterPrep
  [IndirectBr → MemIntrin → StringRuntime → CompilerRt → SyscallStub →
   WinPEBImport → KernelImport → Data2Text phase 1]
  ③ RunBeforeInlining

OptimizerLastEP (after AlwaysInliner / SROA / SLP / Vectorize):
  ④ RunAfterInlining
  [AlwaysInliner → Data2Text phase 2 → ZeroReloc(Stackify)]
  ⑤ RunAfterStackify
  [AllBlrPass (if -fshellcode-all-blr)]
  ⑥ RunAfterFinalIR

addMachinePasses (pre-PreEmit slot):
  ⑦ RunBeforePreEmit
  [MIRPrepPass]
  ⑧ RunAfterPreEmit
  [LLVM addPreEmitPass → FuncletLayout → LiveDebugValues →
   MachineOutliner → BBSections / FuncSplitter → CFIFixup →
   StackFrameLayoutAnalysis → addPreEmitPass2]

addMachinePasses (post-PreEmit2 slot, fork-extension):
  ⑨ RunAfterFinalMIR
  [LLVMTargetMachine::addPassesToEmitFile → AsmPrinter → bytes]

extractor finalize:
  ⑩ RunPostExtract
  [BadByteRewriter chain → CharsetEncoder → audit → align/max-length/pad]
  ⑪ RunPostFinalize
  [.bin written to disk]
```

### 6.2 PIC Coverage Matrix

This table answers: "If I register at hook X, which built-in passes will still process constructs I introduce?"

| Hook | Built-in PIC fixups that run after | Safe to introduce |
|------|-----------------------------------|-------------------|
| **① RunBeforePrep** | All subsequent passes | Nearly everything (except `global ctor` / `external_weak` / `_Thread_local` which ZeroReloc(Prep) rejects) |
| **② RunAfterPrep** ★ | IndirectBr → MemIntrin → ... → AllBlr | mem* calls, string literal GVs, `__int128`, undeclared POSIX/Win32 externs |
| **③ RunBeforeInlining** | AlwaysInliner → Data2Text 2 → ZeroReloc(Stackify) → AllBlr | Mutable GVs, helper functions, immediate constants |
| **④ RunAfterInlining** ★ | Data2Text 2 → ZeroReloc(Stackify) → AllBlr | Mutable GVs, stack allocas, immediates |
| **⑤ RunAfterStackify** | AllBlr (only if `-fshellcode-all-blr`) | Pure IR arithmetic, stack allocas; **no GVs** |
| **⑥ RunAfterFinalIR** | (none) | You must ensure: no mem*, no extern, no mutable GV, no BlockAddress, no const pool refs |
| **⑦ RunBeforePreEmit** | MIRPrepPass → addPreEmitPass / addPreEmitPass2 | MIR rewrites coordinated by subsequent LLVM passes |
| **⑧ RunAfterPreEmit** | addPreEmitPass / addPreEmitPass2 | Instruction substitution cooperates with branch relaxation / NOP padding |
| **⑨ RunAfterFinalMIR** | (none) | Must not break register allocation or introduce unknown relocs |
| **⑩ RunPostExtract** | BadByteRewriter → CharsetEncoder → audit → sizing | Arbitrary bytes; rewriter/encoder/audit will catch issues |
| **⑪ RunPostFinalize** | (none) | You must ensure length / charset / bad-byte compliance |

### 6.3 Recommended Registration by Obfuscation Type

| Obfuscation type | Recommended hook | Rationale |
|-----------------|-----------------|-----------|
| String encryption | `RunAfterPrep` ★ | Decoder introduces mem* → handled by MemIntrin; encrypted constants → handled by Data2Text |
| CFF (control flow flattening) | `RunAfterInlining` ★ | Single entry function maximized; dispatcher state GV handled by ZeroReloc(Stackify) |
| Bogus CF / opaque predicates | `RunAfterInlining` | Same rationale; maximum complexity benefit when function is largest |
| Tamper-check / IR integrity | `RunAfterFinalIR` | Must see final IR; your pass must be PIC-clean |
| Instruction substitution (mov→lea/xor/sub) | `RunAfterPreEmit` | Mid-granularity; cooperates with LLVM backend |
| Register renaming / stack mangling | `RunAfterFinalMIR` | Strictest; sees LLVM's fully optimized MIR |
| Junk bytes / block reorder | `RunPostExtract` | BadByteRewriter / sizing will catch issues |
| Full payload XOR/RC4 + self-decrypt | `RunPostFinalize` | Last moment before disk write; encrypted bytes are not audited |

### 6.4 PIC-Friendly Authoring Rules

If your obfuscation pass registers at `RunAfterStackify` / `RunAfterFinalIR` / `RunAfterFinalMIR` / `RunPostFinalize` (hooks with no subsequent fixups), your output **must**:

- Not introduce new mutable / immutable global variables (use stack allocas or registers)
- Not introduce `BlockAddress` / computed-goto tables (use `switch` + immediate constants)
- Not introduce undeclared extern functions (register at `RunAfterPrep` instead to let MemIntrin / SyscallStub / etc. handle them)
- Not introduce const pool / vector constant literals (backend spills these to `.rodata.cst*`; use `mov imm` sequences)
- Not introduce `_Thread_local` / `external_weak` / `global_ctor` / `global_dtor` / `@llvm.used`
- In MIR hooks: only use PC-relative / RIP-relative / adrp+add addressing

### 6.5 Multiple Obfuscation Libraries Coexisting

`setShellcodeObfuscationHooks` is override-style registration (replaces the entire `ObfuscationHooks` struct). For multiple libraries to coexist:

```cpp
auto Existing = neverc::shellcode::getShellcodeObfuscationHooks();
auto Old = std::move(Existing.RunAfterInlining);
Existing.RunAfterInlining = [Old = std::move(Old)](
    llvm::ModulePassManager &MPM,
    const neverc::shellcode::ShellcodeOptions &Opts) {
  if (Old) Old(MPM, Opts);            // run previous library's pass first
  MPM.addPass(MyCFFPass());            // then run ours
};
neverc::shellcode::setShellcodeObfuscationHooks(std::move(Existing));
```

`registerBadByteRewriteStrategy` and `registerCharsetEncoder` use append semantics and do not require this merge pattern.
