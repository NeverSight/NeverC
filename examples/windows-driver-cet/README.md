# Windows Kernel Driver with CET Shadow Stack

A minimal WDM kernel driver built with NeverC, with Intel CET (Control-flow
Enforcement Technology) Kernel Shadow Stack enabled. Cross-compiles from
macOS / Linux.

## CET Shadow Stack

CET Shadow Stack is a hardware-backed return address protection mechanism.
The CPU maintains a separate "shadow" stack that mirrors CALL/RET operations;
any ROP-style return address manipulation triggers a #CP exception.

Windows kernel CET (KCET) uses Shadow Stack only — Indirect Branch Tracking
(IBT) is not used; Windows uses Control Flow Guard (CFG) instead.

**Requirements:**

- HVCI enabled on the target machine
- Windows build 21389 or later
- CPU with CET support (Intel Tiger Lake+ / AMD Zen 3+)

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

## Enabling KCET on target machine

```cmd
reg add HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity /v Enabled /t REG_DWORD /d 1 /f
reg add HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\KernelShadowStacks /v Enabled /t REG_DWORD /d 1 /f
```

Reboot required. Verify with `msinfo32.exe` → "Kernel DMA Protection" / "Kernel-mode Hardware-enforced Stack Protection".

## Loading

```cmd
sc create CetDriver type= kernel binPath= C:\path\to\CetDriver.sys
sc start CetDriver
sc stop CetDriver
sc delete CetDriver
```

Enable test signing or use a code signing certificate for production.
