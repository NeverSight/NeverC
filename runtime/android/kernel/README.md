# `runtime/android/kernel` — Android GKI kernel-module runtime

Minimal, self-contained runtime for building **standard `.ko` Android kernel
modules** with `neverc -fandroid-kernel-driver-mode`, from any host
(macOS / Linux / Windows), **without** a full kernel source tree or the kernel
build system.

This is deliberately separate from the user-space Android runtime
(`runtime/android/arm64`, the bionic NDK sysroot). Nothing here is used for
app-layer builds.

## Why this can be "minimal + cross-version"

A `.ko` is a relocatable ELF (`ET_REL`) plus kernel-module metadata. Two pieces
are bound to a specific kernel build and cannot be unified in one insmod-loadable
binary across major versions:

1. **`vermagic`** — the `.modinfo` string the loader compares to the running
   kernel.
2. **`struct module` layout** — the byte offsets of `name` / `init` / `exit`
   inside `.gnu.linkonce.this_module`, read using the *kernel's* own layout.

Everything else (the kernel API the driver calls) is resolved **dynamically at
runtime** via `kallsyms_lookup_name()` (bootstrapped with a kprobe). So the
driver source stays version independent and the `.ko` imports almost no symbols
(typically only `register_kprobe` / `unregister_kprobe`, which are stable GKI
exports). Building without MODVERSIONS (no `__versions` section) makes the
loader skip CRC checks, so those few imports load on any matching-vermagic
kernel.

The only per-kernel data therefore lives in `include/nvkmod_version.h`, selected
with `-DNVK_KERNEL=510|515|601|606|612` (default `510` = android12-5.10).

## Layout

```
runtime/android/kernel/
  include/                     # NeverC kernel SDK (18 headers)
    nvkmod.h                   #   module macros, kprobe/kallsyms bootstrap
    nvkmod_version.h           #   per-kernel vermagic + struct module offsets (5.10–6.12) + SDK version
    nvk.h                      #   all-in-one include (initializes all subsystems)
    nvk_hook.h                 #   arm64 inline-hook engine v2 (simple + context + FP-SIMD + kCFI + ftrace fallback)
    nvk_mem.h                  #   safe memory read/write, pattern scan, write-protection bypass
    nvk_syscall.h              #   sys_call_table operations + arm64 syscall number table
    nvk_process.h              #   process enumeration, PID lookup, task walking
    nvk_cred.h                 #   credential manipulation (root, uid/gid, capabilities)
    nvk_selinux.h              #   SELinux enforcement control + AVC/inode bypass hooks
    nvk_hide.h                 #   module concealment (list + sysfs + /proc/modules + dmesg + PID + mount + maps filter)
    nvk_log.h                  #   leveled logging (silent/error/warn/info/debug/trace) + ratelimit + hexdump
    nvk_thread.h               #   kernel thread management (kthread create/stop/sleep)
    nvk_netlink.h              #   user↔kernel netlink IPC channel + auth
    nvk_file.h                 #   kernel file I/O (filp_open/kernel_read/write)
    nvk_addr.h                 #   virtual↔physical address translation, page table walking
    nvk_compat.h               #   runtime kernel version detection + feature probing (PAC/BTI/MTE/SVE/CFI)
    nvk_anti.h                 #   environment detection + integrity verification + watchdog
    nvk_vma.h                  #   VMA operations, process memory map inspection
  arm64/
    include/                   # minimal kernel headers (110 total)
      linux/*.h                #   99+ headers: types, kernel, printk, list, slab, fs, ...
      asm/*.h                  #   8 headers: barrier, current, page, ptrace, syscall, ...
    lib/                       # optional libclang_rt.builtins-aarch64.a
  tools/
    gen_struct_module_offsets.c # regenerate exact struct module offsets per kernel
    test-all.sh                # full verification: 8 demos × 5 kernels × extra modes = 70 configs
```

`neverc -fandroid-kernel-driver-mode` automatically:

- swaps the bionic sysroot for the kernel include roots above
  (`include/` + `arm64/include/`),
- disables auto-LTO (emits real ELF, not bitcode),
- adds `-D__KERNEL__ -DMODULE -ffreestanding`, direct external-data access
  (the arm64 module loader has no GOT), reserved `x18`, and disables outline
  atomics and CFI checks,
- lowers any wider-than-64-bit integer div/rem inline (LLVM `ExpandLargeDivRem`,
  via `-expand-div-rem-bits=64`) so `__int128` division never needs the
  compiler-rt helpers the kernel doesn't export — the `.ko` stays fully
  self-contained with **zero** compiler-rt dependency,
- emits the empty `.plt` / `.init.plt` / `.text.ftrace_trampoline` sections the
  arm64 loader requires (`CONFIG_ARM64_MODULE_PLTS`) — no external `module.lds`.

You then pass `-r -nostdlib -o mod.ko mod.c` to relocatably link the module.

## SDK headers

| Header | Purpose |
|--------|---------|
| `nvkmod.h` | Module entry point, kprobe bootstrap, `NVK_BOOTSTRAP()`, `NVK_DEFINE_MODULE()` |
| `nvk_hook.h` | arm64 inline-hook engine v2 — simple/context/FP-SIMD modes + absolute relocation (10 insn types) + BTI/PAC/kCFI-safe + SMP-safe DMB barriers + atomic stop_machine patch + D-cache→I-cache coherent + deep quiescence unhook + trampoline pool (32 pages) + ftrace fallback + hook chain + pause/resume + 6.12 execmem support |
| `nvk_mem.h` | `nvk_mem_read/write`, `nvk_mem_read_user`, `nvk_mem_scan`, `nvk_mem_scan_mask`, `nvk_mem_write_protected` |
| `nvk_syscall.h` | `nvk_syscall_replace/restore`, `nvk_syscall_get`, arm64 syscall number definitions |
| `nvk_process.h` | `nvk_current_pid`, `nvk_find_task_by_name`, `nvk_for_each_task`, task comm/pid resolution |
| `nvk_cred.h` | `nvk_cred_set_root`, `nvk_cred_set_uid`, `nvk_cred_set_caps_full`, `nvk_cred_get_ids` |
| `nvk_selinux.h` | `nvk_selinux_set_permissive/enforcing`, `nvk_selinux_bypass_install/remove` (AVC + inode hook) |
| `nvk_hide.h` | `nvk_mod_hide/show`, `nvk_mod_full_hide` (list + sysfs + /proc/modules + /proc/vmallocinfo + dmesg + PID + mount + maps filter) |
| `nvk_log.h` | `nvk_log_err/warn/info/dbg/trace`, `nvk_log_once`, `nvk_log_ratelimit`, `nvk_log_hexdump` |
| `nvk_thread.h` | `nvk_thread_run`, `nvk_thread_stop`, `nvk_thread_sleep_ms`, `nvk_thread_stop_all` |
| `nvk_netlink.h` | `nvk_nl_open/close/send/reply` — bidirectional netlink IPC with dispatch callback |
| `nvk_addr.h` | `nvk_virt_to_phys`, `nvk_translate_user`, `nvk_walk_pgtable`, VA bits / page size detection |
| `nvk_compat.h` | `nvk_kernel_version()`, `NVK_KERNEL_GE(maj,min)`, `nvk_has_pac/bti/mte`, versioned symbol lookup helpers |
| `nvk_file.h` | `nvk_file_open/read/write/close`, `nvk_file_exists`, `nvk_file_read_all/write_all` |
| `nvk_anti.h` | Environment detection (emulator, debugger, root, su binary, Magisk/KSU/APatch, SELinux permissive, hook/kprobe tampering), integrity verification, watchdog — all detection paths xorstr-encrypted |
| `nvk_vma.h` | VMA operations (find_vma, walk, read/write remote), process memory map inspection |

All symbol lookups go through `NVK_LOOKUP()` which auto-encrypts strings via xorstr.

## Examples

| Example | Description |
|---------|-------------|
| `android-kernel-hello` | Zero-import minimal module (load test) |
| `android-kernel-driver` | Dynamic kallsyms template |
| `android-kernel-chardev` | misc device + ioctl + /proc status page |
| `android-kernel-inline-hook` | Inline hook on `do_faccessat` (simple + context modes) |
| `android-kernel-syscall-hook` | sys_call_table replacement + inline hook (dual mode) |
| `android-kernel-stealth` | Module concealment (list / sysfs / proc + SELinux + root) |
| `android-kernel-netlink` | User↔kernel netlink IPC channel (ping/version/echo) |
| `android-kernel-full` | Full SDK demo — initializes all subsystems, exercises hook/cred/hide/netlink |

## struct module offsets (important before loading on a device)

`include/nvkmod_version.h` holds the `name`/`init`/`exit` offsets and total size
of `struct module` per kernel. All five presets are **verified** against the
stock GKI `gki_defconfig` (computed from each kernel's own `make modules_prepare`
headers with clang-18):

| preset | release | NAME | INIT | EXIT | sizeof |
|--------|---------|------|------|------|--------|
| `510` (android12-5.10) | 5.10    | 24 | 400 (0x190) | 960 (0x3C0)  | 1024 (0x400) |
| `515` (android13-5.15) | 5.15.206| 24 | 376 (0x178) | 872 (0x368)  | 960 (0x3C0)  |
| `601` (android14-6.1)  | 6.1.172 | 24 | 368 (0x170) | 968 (0x3C8)  | 1024 (0x400) |
| `606` (android15-6.6)  | 6.6.138 | 24 | 392 (0x188) | 1448 (0x5A8) | 1536 (0x600) |
| `612` (android16-6.12) | 6.12.81 | 24 | 392 (0x188) | 1504 (0x5E0) | 1600 (0x640) |

`name` (offset 24) is stable across current GKI builds; `init`/`exit`/sizeof
depend on the target kernel's `CONFIG_*` (CFI_CLANG, MODULE_UNLOAD, TRACEPOINTS,
...). The values above match a stock GKI kernel built with `gki_defconfig`. If an
OEM ships a different config, regenerate them for that kernel before loading:

```
# On a prepared tree (Linux: make ARCH=arm64 LLVM=1 gki_defconfig modules_prepare):
runtime/android/kernel/tools/gen-offsets.sh <path-to-GKI>/common
# prints:  #  define NVK_OFF_INIT ...  etc. -> paste into nvkmod_version.h
```

`tools/gen-offsets.sh` drives `tools/gen_struct_module_offsets.c` (a no-run
"asm-offsets" probe). It works fully on Linux / an already-prepared tree; on
macOS, where the kernel build system can't run, prepare the four newer trees in
a throwaway Linux container instead:

```
# from a host with Docker; computes offsets for all four raw GKI trees at once
docker run --rm -v <repo>/local_docs:/work -v <repo>/runtime/android/kernel/tools:/tools:ro \
  ubuntu:24.04 bash -lc '
    apt-get update -qq && apt-get install -y -qq build-essential clang lld llvm \
      bc bison flex libssl-dev libelf-dev libdw-dev cpio kmod rsync >/dev/null
    for kit in GKI-android13-5.15-kit GKI-android14-6.1-kit GKI-android15-6.6-kit GKI-android16-6.12-kit; do
      KT=/work/$kit/common; O=/build/$kit
      make -C $KT O=$O ARCH=arm64 LLVM=1 -j"$(nproc)" gki_defconfig modules_prepare
      clang --target=aarch64-linux-gnu -fno-lto -nostdlibinc -std=gnu11 -D__KERNEL__ -DNVK_GEN_KSRC=1 \
        -I$KT/arch/arm64/include -I$O/arch/arm64/include/generated -I$KT/include -I$O/include \
        -I$O/include/generated -I$KT/arch/arm64/include/uapi -I$O/arch/arm64/include/generated/uapi \
        -I$KT/include/uapi -I$O/include/generated/uapi \
        -include $KT/include/linux/kconfig.h -include $O/include/generated/autoconf.h \
        -S -o - /tools/gen_struct_module_offsets.c | grep "==NVK=="
    done'
# NOTE: android16-6.12 needs libdw-dev (its gendwarfksyms host tool includes <dwarf.h>).
```

You can also override per build with
`-DNVK_OFF_INIT=… -DNVK_OFF_EXIT=… -DNVK_MODULE_SIZE=…`.

`vermagic` is likewise device-specific; override with `-DNVK_VERMAGIC='"…"'` to
match your target (`cat /proc/version` / `modinfo` of an existing module).
