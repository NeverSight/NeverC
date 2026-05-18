**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode Compiler](../README.md)

# MIR Pass Design — Principles and Hook Points

> Companion document to [ir-pass-design.md](../ir-pass-design/README.md). The IR layer eliminates constructs that are visibly relocation-producing at the IR level. The MIR layer serves as a **catch-all** after instruction selection and register allocation, stripping codegen-introduced pseudo/metadata instructions and exposing hook points for third-party obfuscation passes to perform final instruction-level transformations.
>
> Implementation: `neverc/lib/Shellcode/MIR/MIRPrepPass.cpp` + `Pipeline.cpp`.
> Hook interface: `neverc/include/neverc/Shellcode/Pipeline/Pipeline.h`.

---

## 0. Why a MIR Layer is Needed

The IR layer has already eliminated:
- Constant GVs → stack-ified / immediates (Data2TextPass)
- `memcpy` / `memset` / `str*` / `abs*` → inline byte loops (MemIntrinPass)
- `__int128` compiler-rt helpers → inline always_inline (CompilerRtPass)
- extern libc syscalls → inline svc / syscall (SyscallStubPass)
- Win32 externs → PEB walk + export hash (WinPEBImportPass)
- Mutable globals → entry stack frame (ZeroRelocPass)
- Computed goto → switch (IndirectBrPass)
- Optional: direct call → indirect call (AllBlrPass)

But the LLVM backend introduces additional constructs during **IR → MIR lowering** that shellcode cannot accommodate:

1. **CFI / EH_LABEL pseudo-instructions**: generated when `-g` or default unwind info is enabled, producing `__compact_unwind` (Mach-O) / `.eh_frame` (ELF) / `.pdata + .xdata` (COFF).
2. **XRay / patchable function stubs**: `-fxray-instrument` or `-fpatchable-function-entry` insert `PATCHABLE_FUNCTION_ENTER` and similar.
3. **Sanitizer metadata**: StackMap / PatchPoint / StateMap / PseudoProbe.
4. **Backend MC-level fixups**: e.g., Windows arm64 GOT references invisible at IR level.

Additionally, MIR hooks serve a critical purpose: **enabling third-party instruction-level obfuscation** (instruction substitution, register renaming) that IR cannot express (IR only has virtual registers and abstract instructions).

---

## 1. Integration with LLVM (Native Hooks)

LLVM's `TargetPassConfig` has a global callback list. `addMachinePasses()` invokes each callback after `addPass(&PatchableFunctionID)` and before `addPreEmitPass()`. We added a public wrapper `addExternalPass(Pass *P)` to solve access-control issues with the protected `addPass()`.

Registration in `Pipeline.cpp`:

```cpp
ListRegisterTargetPassConfigCallbacks.push_back(
    [](TargetPassConfig &TPC) {
      const ShellcodeOptions &Opts = currentShellcodeOptionsStorage();
      const ObfuscationHooks &H = getShellcodeObfuscationHooks();
      runMIRHook(H.RunBeforePreEmit, TPC, Opts);
      TPC.addExternalPass(createShellcodeMIRPrepPass(Opts));
      runMIRHook(H.RunAfterPreEmit, TPC, Opts);
    });
```

The callback does not capture `Opts`. It reads the current `ShellcodeOptions` snapshot at runtime, preventing stale configuration when the same process compiles both shellcode and regular C.

---

## 2. Built-in MIRPrepPass

Cross-platform, single responsibility: scan each `MachineBasicBlock` and delete three categories of pseudo-instructions. Real machine instructions (`MOV` / `BL` / `ADRP` / `SYSCALL` / ...) are **never touched**.

### 2.1 Side-Section Metadata (via `TargetOpcode::*`, cross-platform)

| Opcode | Source | If not stripped |
|--------|--------|----------------|
| `CFI_INSTRUCTION` | All platforms' frame-lowering / `-g` | `.eh_frame` / `__compact_unwind` / `.pdata` non-empty |
| `EH_LABEL` | EH / try-catch setjmp points | LSDA side section non-empty |
| `GC_LABEL` / `ANNOTATION_LABEL` | GC / annotation markers | MCSymbol with section-relative metadata |
| `STATEPOINT` / `STACKMAP` / `PATCHPOINT` | GC / sandbox stackmap | `.llvm_stackmaps` side section |
| `PSEUDO_PROBE` | `-fprofile-sample-use` | `.pseudo_probe` side section |
| `PATCHABLE_*` family | XRay / Kcov stubs | `.xray_instr_map` / `.xray_fn_idx` |
| `FENTRY_CALL` | `-mfentry` entry probe | extern `__fentry__` call |
| `LOCAL_ESCAPE` | Microsoft SEH frame-escape | pulls in `_local_unwind2` / `__except_handler3` |
| `JUMP_TABLE_DEBUG_INFO` | Jump table debug info | `.debug_rnglists` entry |

### 2.2 Windows SEH (matched by `TargetInstrInfo::getName()` prefix)

Windows SEH pseudos are target-specific opcodes defined in AArch64/X86 backend TDs (~20 instructions like `SEH_StackAlloc`, `SEH_PushReg`, etc.). To keep the MIR pass **cross-platform without including backend headers**, we use string-prefix matching:

```cpp
StringRef Name = TII->getName(Opcode);
if (Name.starts_with("SEH_"))
  eraseFromParent();
```

### 2.3 Instruction Rewrite Table (`MIRRewritePatterns.def`)

After pseudo stripping, `MIRPrepPass` runs a rewrite pass to replace codegen-selected but shellcode-unfriendly instruction patterns with equivalent shellcode-friendly forms, without modifying LLVM `.td` files.

Two registered patterns:

1. **`aarch64-cpi-fp-to-fmov-imm`**: `ADRP + LDRSui/LDRDui [base, #:lo12:CPI]` → `FMOV Sd/Dd, #imm8` when the IEEE bit pattern falls within FMOV's 256 encodable values.
2. **`x86-cpi-zero-fp-to-xorps`**: `movss/movsd xmm, [rip+CPI]` loading `+0.0` → `FsFLD0SS/FsFLD0SD` (3-byte `xorps xmm, xmm`).

Opcode names are centralized in `Tables/MIRRewriteOpcodes.def`. Adding a new rewrite pattern requires three steps:
1. Write `tryRewriteXxx(MachineFunction &)` using `lookupMIRRewriteOpcode()` + `BuildMI(TII->get(...))`
2. Add opcode roles to `MIRRewriteOpcodes.def`
3. Add pattern entry to `MIRRewritePatterns.def`

---

## 3. User Obfuscation Hooks

`ObfuscationHooks` exposes **11 hook points**: 6 IR-level, 3 MIR-level, 2 byte-level:

Three signature types:
```cpp
using ObfuscationHook = std::function<void(
    llvm::ModulePassManager &, const ShellcodeOptions &)>;
using MachineObfuscationHook = std::function<void(
    llvm::TargetPassConfig &, const ShellcodeOptions &)>;
using BinaryObfuscationHook = std::function<void(
    llvm::SmallVectorImpl<uint8_t> &, const ShellcodeOptions &)>;
```

Key differences:
- `RunBeforePreEmit`: MIR **still has CFI/EH/XRay pseudos** — for prologue/epilogue metadata manipulation.
- `RunAfterPreEmit`: **cleaned MIR** — closest to AsmPrinter form, ideal for instruction substitution / register renaming.
- `RunPostExtract`: **pure byte stream** after extractor patches intra-text relocs — for whole-payload XOR/RC4, junk bytes, custom headers.

```cpp
__attribute__((constructor))
static void myMirObfInit() {
  auto H = neverc::shellcode::getShellcodeObfuscationHooks();
  H.RunAfterPreEmit = [](llvm::TargetPassConfig &TPC,
                         const neverc::shellcode::ShellcodeOptions &Opts) {
    TPC.addExternalPass(new MyInstructionSubstitutionPass(Opts.MirObfuscateSpec));
  };
  neverc::shellcode::setShellcodeObfuscationHooks(std::move(H));
}
```

---

## 4. Full Execution Order

```
[IR PassBuilder]
  ├─ RunBeforePrep       (user hook)
  ├─ ZeroRelocPass(Prep)
  ├─ RunAfterPrep        (user hook)
  ├─ IndirectBrPass / MemIntrinPass / CompilerRtPass
  ├─ SyscallStubPass / WinPEBImportPass / KernelImportPass
  ├─ Data2TextPass #1
  ├─ RunBeforeInlining   (user hook)
  │  (LLVM O-level: AlwaysInliner / SROA / SLP)
  ├─ RunAfterInlining    (user hook)
  ├─ Data2TextPass #2 / ZeroReloc(Stackify)
  ├─ RunAfterStackify    (user hook)
  └─ AllBlrPass          (opt)
        │
[Codegen (IR → MIR)]
  ├─ RunBeforePreEmit    (user hook, CFI present)
  ├─ ShellcodeMIRPrepPass  ← this document's focus
  └─ RunAfterPreEmit     (user hook, CFI stripped)
        │
[AsmPrinter → Object File]
        │
[ShellcodeExtractor]  ← byte-level fallback audit
  ├─ RunPostExtract   (user hook, pure bytes)
  └─ flat .bin
```

The MIR layer handles **catch-all cleanup + obfuscation hook points**, not business logic. The "write normal C, no shellcode tricks needed" promise is fulfilled by the 5+ IR passes.

---

## 5. Design Rationale

| Problem | IR layer? | MIR layer? |
|---------|-----------|-----------|
| Constant GV elimination | Yes (Data2Text) | Not needed |
| extern libc elimination | Yes (SyscallStub / WinPEB) | Not needed |
| Mutable global stack-ification | Yes (ZeroReloc) | Not needed |
| Computed goto | Yes (IndirectBr) | Not needed |
| CFI pseudo-instructions | No (backend-generated) | Yes (scan and erase) |
| XRay stubs | No (backend-generated) | Yes (scan and erase) |
| Instruction-level obfuscation | No (IR lacks physical registers) | Yes (has real registers/MI) |
| Register renaming | No | Yes |
| Peephole constant expansion | Partial | Yes (cleaner) |

## 6. Extension Guide

**Adding a built-in pseudo strip**: add one case to `isShellcodeStripPseudo` switch.

**Adding a built-in MIR rewrite**: write `tryRewriteXxx(MachineFunction &)` using `TII->getName()` / `BuildMI(TII->get(...))`. Add pattern to `MIRRewritePatterns.def`, opcodes to `MIRRewriteOpcodes.def`.

**Third-party obfuscation library**: register via `setShellcodeObfuscationHooks()`.

## 7. Relationship with ShellcodeExtractor

| Layer | Timing | Capability |
|-------|--------|-----------|
| MIR | **Before** AsmPrinter | Can insert/delete MachineInstrs |
| Extractor | **After** AsmPrinter | Can only modify bytes or reject |

**Principle**: fix in MIR first (can still manipulate instructions); only fall back to extractor for byte-level patches (e.g., intra-section relocation imm26). This layering ensures users never get a "half-broken `.bin`": either it works or there's a clear actionable error at compile time.

## 8. Active Fix vs Diagnostic Passthrough

1. **Active fix**: directly modify MachineInstrs (strip pseudos, rewrite CPI→FMOV). Low-cost, target-independent.
2. **Diagnostic passthrough**: detect issues, report MIR-level errors, let extractor reject at byte level. Used for constructs where MIR rewriting would require extensive target-specific code (e.g., replacing `adrp+ldr CPI` with `mov/movk` sequences).
3. **Extractor fallback**: hard-fail on any remaining external relocs or non-empty data sections.

This principle keeps the MIR layer nearly immune to backend ISA upgrades. The only maintenance is: "is there a new pseudo in TargetOpcode? If shellcode doesn't need it, add one case."
