# Windows Kernel Driver with CET Shadow Stack

A minimal WDM kernel driver built with NeverC, with Intel CET (Control-flow
Enforcement Technology) Kernel Shadow Stack enabled. Cross-compiles from
macOS / Linux.

## Build

```bash
cd examples/windows-driver-cet
make
```

From a standalone NeverC release:

```bash
make NEVERC=/path/to/neverc
```

The output is `CetDriver.sys` (auto-LTO optimized).
Default build includes `-g` for debugging; **release builds should remove `-g`**
to strip debug symbols and reduce binary size.

## CET-specific flags

| Flag | Layer | Purpose |
|------|-------|---------|
| `-fcf-protection=return` | Compiler | Generate Shadow Stack compatible code |
| `-Xlinker --cetcompat` | Linker | Set `IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT` in PE |

## Manual build (without Make)

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fcf-protection=return \
  -fms-kernel -fms-extensions -fms-compatibility \
  -D_AMD64_ -DNTDDI_VERSION=0x06010000 -D_WIN32_WINNT=0x0601 \
  -Wall -nostdlib -shared \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -Xlinker --cetcompat \
  -lntoskrnl -lhal \
  -o CetDriver.sys driver.c
```

## What it does

- Creates a device object at `\Device\CetDriver`
- Creates a symbolic link at `\DosDevices\CetDriver`
- Exercises indirect calls (`ComputeFn` function pointer) to validate CET
  compatibility — Shadow Stack protects the return addresses of these calls
- Prints load/unload messages via `DbgPrint`

---

## CET Technical Details

CET has **two independent protection mechanisms**:

### 1. Shadow Stack — backward-edge protection (RET)

Hardware maintains a second stack (shadow stack) that mirrors CALL/RET. **No
special instructions are needed at function entry** — it is fully transparent:

```
┌─ CALL target ─────────────────────────────────┐
│                                                │
│  Regular stack:  PUSH return_addr  (RSP)       │
│  Shadow stack:   PUSH return_addr  (SSP, HW)   │
│                                                │
└────────────────────────────────────────────────┘

┌─ RET ─────────────────────────────────────────┐
│                                                │
│  Regular stack:  POP return_addr_A  (RSP)      │
│  Shadow stack:   POP return_addr_B  (SSP, HW)  │
│                                                │
│  Compare: return_addr_A == return_addr_B ?      │
│    ✓ match    → normal return                  │
│    ✗ mismatch → #CP exception (BUGCHECK)       │
│                                                │
└────────────────────────────────────────────────┘
```

Shadow Stack management instructions (used by OS for context switching, not
placed at function heads):

```asm
RDSSPQ  rax         ; read current Shadow Stack Pointer
INCSSPQ rax         ; advance SSP (discard entries)
SAVEPREVSSP         ; save previous shadow stack token
RSTORSSP [addr]     ; restore to a saved shadow stack
WRSS  [addr], rax   ; write to supervisor shadow stack
WRUSS [addr], rax   ; write to user shadow stack (ring 0 only)
SETSSBSY            ; mark current shadow stack as busy
CLRSSBSY [addr]     ; clear busy flag
```

### 2. Indirect Branch Tracking (IBT) — forward-edge protection (indirect CALL/JMP)

Requires an `ENDBR64` instruction (`F3 0F 1E FA`, 4 bytes) at every valid
indirect call/jump target. On CPUs without CET support, `ENDBR64` decodes
as a NOP.

```
┌─ Indirect CALL/JMP ──────────────────────────┐
│                                               │
│  CPU sets internal TRACKER = WAIT_FOR_ENDBR   │
│  Jump to target address...                    │
│                                               │
│  First instruction at target is ENDBR64 ?     │
│    ✓ yes → clear TRACKER, execute normally    │
│    ✗ no  → #CP exception                     │
│                                               │
│  Direct CALL/JMP does NOT set TRACKER         │
│                                               │
└───────────────────────────────────────────────┘
```

### Windows kernel's choice

| Protection | Mechanism | Used by Windows kernel? |
|------------|-----------|------------------------|
| Backward-edge (RET) | CET Shadow Stack | **Yes** (KCET) |
| Forward-edge (indirect CALL/JMP) | CET IBT (ENDBR) | **No** — uses CFG instead |

This is why the default is `-fcf-protection=return`: Shadow Stack only, no
ENDBR64 emitted. Use `-fcf-protection=full` if you also want ENDBR64 (harmless
NOPs on Windows, but provides IBT compatibility for Linux portability).

### Assembly comparison: `-fcf-protection` modes

Given this function:

```c
unsigned long rotate13(unsigned long val) {
    return (val << 13) | (val >> 19);
}
```

#### `-fcf-protection=none` (no CET)

```asm
rotate13:
    mov  eax, ecx
    rol  eax, 13
    ret
```

#### `-fcf-protection=return` (Shadow Stack only — this example uses this)

```asm
rotate13:
    mov  eax, ecx      ; identical to "none"!
    rol  eax, 13        ; Shadow Stack is fully transparent —
    ret                 ; hardware operates on CALL/RET automatically
```

Code generation is **identical to `none`**. The only difference is the linker
flag `--cetcompat` which sets the `IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT` bit
in the PE debug directory, telling Windows this binary is Shadow Stack safe.

#### `-fcf-protection=full` (Shadow Stack + IBT)

```asm
rotate13:
    endbr64             ; ← IBT marker (F3 0F 1E FA)
    mov  eax, ecx       ;    NOP on non-CET CPUs
    rol  eax, 13        ;    unused on Windows (CFG handles forward-edge)
    ret
```

`ENDBR64` appears at every function entry. On Windows this is wasted 4 bytes
per function since IBT is not enforced, but it causes no harm.

---

## Compiler vs bin2bin: who is friendly to CET?

CET draws a sharp line between **source-level compilers** and **bin2bin tools**
(packers, obfuscators, hookers, dump+rebuild). Hardware Shadow Stack enforces
two rules that reshape the whole protection / obfuscation industry:

> 1. **Don't modify return addresses.**
> 2. **Don't self-patch code** (HVCI enforces W^X on code pages).
> 3. **Look for strong obfuscation transforms** that respect 1 & 2.

### Can a compiler "encrypt return addresses"?

**No.** This is a common misconception. Shadow Stack is enforced by the CPU,
not by the OS, and it is invisible to user-mode code. If you XOR-encrypt the
return address on the regular stack in your function epilogue:

```c
void my_func() {
    // ... function body ...
    // epilogue tries to encrypt the return address:
    // XOR [rsp], 0xDEADBEEF
    // RET           <- hardware compares regular stack vs shadow stack
                     //   they no longer match -> #CP exception -> BUGCHECK
}
```

The shadow stack still holds the original return address. RET triggers a
hardware comparison; mismatch fires `#CP` and bugchecks the kernel. The
compiler **cannot** reach the shadow stack:

- User-mode: no instruction can write the shadow stack
- Kernel-mode: `WRSSQ` is privileged, only `ntoskrnl` uses it

### CET-friendly obfuscations the compiler CAN do

| Transform | Why CET-safe |
|-----------|--------------|
| **Control-flow flattening** | Switch dispatcher uses direct CALL/JMP; cases get ENDBR64 if needed |
| **VM-based virtualization** | Handlers connected via indirect JMP (with ENDBR64), not push+ret |
| **String / constant encryption** | Pure data transform, no control-flow impact |
| **MBA expressions** | `x + y → (x ^ y) + 2*(x & y)` — data only |
| **Opaque predicates** | Conditional branches via direct jumps |
| **Function cloning / inlining** | No call-stack semantics change |
| **Instruction substitution** | `MOV → XOR + ADD` — no stack effects |

### CET-hostile patterns (these die under KCET)

| Pattern | Why it breaks |
|---------|---------------|
| **Return-address encryption** | Shadow stack mismatch → `#CP` |
| **PUSH addr; RET dispatcher** (classic VMProtect / Themida style) | Same — shadow stack has no entry for `addr` |
| **Stack pivoting** (ROP gadget chains) | Shadow stack cannot follow the pivot |
| **Self-modifying code** | HVCI blocks writes to executable pages |
| **Runtime code generation** | Same — HVCI W^X violation |
| **Trampoline-based inline hooks** | Modifying function prologue triggers HVCI; even bypassing HVCI, shadow stack breaks on the trampoline RET |

### Why bin2bin tools have a structural disadvantage

A compiler emits CET-correct code from semantic IR. A bin2bin tool must
**rediscover** semantics from compiled bytes:

1. **Instruction-boundary ambiguity** — x86 is variable-length. Adding ENDBR64 (4 bytes) at the wrong offset breaks all RIP-relative addressing and relocations.
2. **Indirect-target identification** — bin2bin can't always tell which addresses in `.data` are jump-table entries vs raw data. Either over-mark (code bloat, new ROP gadget seeds) or under-mark (runtime `#CP`).
3. **Self-attestation hazard** — Setting `IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT` is a promise. If the bin2bin output contains any CET-hostile pattern, the driver will load fine on non-CET machines but BSOD instantly on KCET hosts.
4. **CFG completeness** — Compilers see the entire call graph; bin2bin must infer it, and indirect calls without precise targets force conservative ENDBR placement.

### Industry status

| Tool / class | CET state |
|--------------|-----------|
| **NeverC / Clang / MSVC (compilers)** | Natively CET-friendly via `-fcf-protection` + linker flag |
| **OLLVM / Tigress / NeverC passes** | IR-level transforms → naturally CET-safe |
| **Microsoft Detours (4.0+)** | Updated to be CET-compatible |
| **VMProtect / Themida (older)** | Push+RET dispatcher kills the driver on KCET hosts |
| **VMProtect / Themida (newer)** | Adding ENDBR-aware dispatchers, mixed support |
| **Manual map / dump+rebuild loaders** | Must reconstruct all ENDBR markers — error-prone |

### Game security angle

Anti-cheat drivers (EAC, BattlEye, FACEIT AC, Vanguard) ship with
`--cetcompat` set, so they run cleanly on KCET-enabled machines.
Cheat drivers — typically packed, hooked, or trampoline-injected via
bin2bin tooling — struggle to remain CET-compliant. KCET + HVCI form a
**"compiler-friendly, bin2bin-hostile" hardware wall** that asymmetrically
benefits well-engineered security software over malware-style code.

This is the deeper reason Microsoft pushes KCET so hard for kernel
software: it makes legitimate kernel code easier to harden while making
attacker tradecraft progressively harder.

---

## Enabling KCET on target machine

```cmd
reg add HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity /v Enabled /t REG_DWORD /d 1 /f
reg add HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\KernelShadowStacks /v Enabled /t REG_DWORD /d 1 /f
```

Reboot required. Verify with `msinfo32.exe` → "Kernel-mode Hardware-enforced
Stack Protection".

**Requirements:**

- HVCI enabled on the target machine
- Windows build 21389 or later
- CPU with CET support (Intel Tiger Lake+ / AMD Zen 3+)

## Loading

```cmd
sc create CetDriver type= kernel binPath= C:\path\to\CetDriver.sys
sc start CetDriver
sc stop CetDriver
sc delete CetDriver
```

Enable test signing or use a code signing certificate for production.
