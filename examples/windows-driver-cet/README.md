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
