**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Documentation index](../README.md) · [← NeverC project](../../README.md)

# NeverC Examples

Complete, buildable examples demonstrating NeverC's cross-platform compilation capabilities. All examples cross-compile from any host OS — no target build environment required.

---

## Available Examples

### Server Backends

| Example | Description | Key Features |
|---------|-------------|-------------|
| [Authoritative Game Server](../../examples/network-authoritative-server/README.md) | Cross-platform game backend | Fixed 60 Hz tick, TCP sessions, UDP/QUIC input, replay protection |
| [Anti-Cheat Collector](../../examples/network-anticheat-collector/README.md) | Hardened telemetry ingestion | mTLS, streaming NRPC, HMAC telemetry, bounded audit pipeline |

### Windows

| Example | Description | Key Features |
|---------|-------------|-------------|
| [Windows Kernel Driver](../../examples/windows-driver/README.md) | Minimal WDM kernel driver | Cross-compile `.sys` for **x64** (default) and **ARM64**, auto-LTO, built-in linker, `DbgPrint` device I/O |
| [Windows Driver + CET](../../examples/windows-driver-cet/README.md) | Kernel driver with Intel CET Shadow Stack | CET-compatible kernel code (**x64 only**), `/guard:ehcont`, Shadow Stack enforcement |
| [Windows Driver + Float](../../examples/windows-driver-float/README.md) | Kernel driver with floating-point/SIMD | Safe FP in kernel mode on **x64** and **ARM64**, `KeSaveExtendedProcessorState` / `KeRestoreExtendedProcessorState` |
| [Windows Ring3 EXE](../../examples/windows-exe/README.md) | User-mode console app | GetSystemInfo, process enum, VirtualAlloc/VirtualQuery |
| [Windows Ring3 DLL](../../examples/windows-dll/README.md) | User-mode DLL | ReadProcessMemory, VirtualAllocEx, module enum, XOR helper |

### Linux

| Example | Description | Key Features |
|---------|-------------|-------------|
| [Linux Hello World](../../examples/linux-hello/README.md) | Minimal C program | Cross-compile ELF from macOS/Windows, printf, string ops |
| [Linux POSIX](../../examples/linux-posix/README.md) | POSIX system programming | pthreads, mmap, pipe, signal handling |
| [Linux Static](../../examples/linux-static/README.md) | Fully-static binary | `-static` linking, zero runtime dependencies, math functions |
| [Linux Network](../../examples/linux-network/README.md) | TCP socket demo | Client/server, socket API, loopback communication |
| [Linux Math + zlib](../../examples/linux-math/README.md) | Math + compression | Trigonometry, special functions, zlib compress/decompress, CRC32 |

### macOS

| Example | Description | Key Features |
|---------|-------------|-------------|
| [macOS Application](../../examples/macos-app/README.md) | Native Mach-O executable | sysctl, uname, Mach host_info/task_info, process introspection |
| [macOS Dynamic Library](../../examples/macos-dylib/README.md) | Native `.dylib` library | Mach vm_read/vm_write, vm_alloc/vm_dealloc, task_info, XOR helper |

### Android

| Example | Description | Key Features |
|---------|-------------|-------------|
| [Android ELF](../../examples/android-elf/README.md) | Native ARM64 binary for rooted devices | Cross-compile to Android, dlopen/liblog, /proc info, root check |
| [Android Shared Library](../../examples/android-so/README.md) | Native ARM64 `.so` library | Shared library, mmap RWX, XOR encryption, dlopen liblog |

### Android Kernel (.ko)

No kernel source tree required — NeverC compiles against the bundled minimal runtime. Single source targets GKI 5.10–6.12.

| Example | Description | Key Features |
|---------|-------------|-------------|
| [Kernel Hello](../../examples/android-kernel-hello/README.md) | Minimal `.ko` module | kallsyms bootstrap via kprobe, simplest insmod validation |
| [Kernel Driver Template](../../examples/android-kernel-driver/README.md) | Dynamic symbol resolution template | `kallsyms_lookup_name`, GKI-stable ABI, 5.10–6.12 |
| [Kernel Inline Interpose](../../examples/android-kernel-inline-interpose/README.md) | Inline interpose on `do_faccessat` | BTI/PAC-safe patch, context interpose mode, PC-relative relocation |
| [Kernel Syscall Interpose](../../examples/android-kernel-syscall-interpose/README.md) | Syscall table / inline / context interpose | `sys_call_table` swap, inline interpose, context interpose modes |
| [Kernel Lowvis](../../examples/android-kernel-lowvis/README.md) | Module visibility management | List/sysfs/proc visibility, credential wrappers, SELinux enforcement state |
| [Kernel Full SDK](../../examples/android-kernel-full/README.md) | Complete SDK integration | Netlink IPC, interposes, credential wrappers, module visibility, SELinux policy control, VMA, file I/O |
| [Kernel Chardev](../../examples/android-kernel-chardev/README.md) | Character device + ioctl | `misc_register`, ioctl dispatch, `/proc` seq_file |
| [Kernel Netlink](../../examples/android-kernel-netlink/README.md) | Bidirectional netlink IPC | PING/VERSION/ECHO commands, `nvk_nl_open`/`nvk_nl_reply` |
| [Kernel Probe](../../examples/android-kernel-probe/README.md) | Probe an arbitrary instruction | `neverc_krt_probe_register`, full register context, priority chaining, skip/redirect |
| [Kernel Multi-File](../../examples/android-kernel-multifile/README.md) | Multi-file kernel module | One `NEVERC_KRT_BOOTSTRAP()`, `weak_odr` shared state, split init/interpose/helper files |

---

## Quick Start

Every example follows the same pattern:

```bash
cd examples/<example-name>
neverc make
```

Override the compiler path if needed:

```bash
neverc make NEVERC=/path/to/neverc
```

Windows driver examples select architecture with `ARCH` (x64 by default). The
CET example is x64-only — CET is an x86 feature:

```bash
neverc make ARCH=x64        # Build for x64 (default)
neverc make ARCH=arm64      # Build for ARM64
neverc make all-arch        # Build every architecture the example supports
neverc make TESTSIGN=1      # Attach an Authenticode test signature
```

Linux examples support architecture selection:

```bash
neverc make TARGET=aarch64-linux-gnu   # Build for ARM64
neverc make TARGET=x86_64-linux-gnu    # Build for x86_64 (default)
```

macOS examples support architecture selection:

```bash
neverc make TARGET=arm64-apple-macos     # Build for Apple Silicon (default)
neverc make TARGET=x86_64-apple-macos    # Build for Intel
```

Android examples target ARM64 by default:

```bash
cd examples/android-elf
neverc make            # Build
neverc make run        # Build + push to device + run via adb
```

---

## Cross-Platform Highlights

- **Single toolchain**: NeverC handles preprocessing, compilation, optimization (auto-LTO), and linking in one invocation
- **Bundled SDK**: Windows SDK/WDK, Linux sysroot (Ubuntu 22.04), macOS sysroot (macOS 14), and Android sysroot (NDK r26c, API 21+) are bundled in `runtime/` — zero external dependencies
- **Host-agnostic**: Build from macOS (arm64/x86_64), Linux (x86_64/aarch64), or Windows with identical commands
- **Multi-target**: Cross-compile to Windows PE (`.sys`/`.exe`/`.dll`), Linux ELF, macOS Mach-O (`.dylib`), and Android ELF from any host
- **Debug support**: Pass `-g` for DWARF debug info; inspect with `llvm-dwarfdump`
