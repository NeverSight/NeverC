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
  include/                     # arch-independent NeverC kernel SDK
    nvkmod.h                   #   umbrella: module macros, kprobe/kallsyms bootstrap, NVK_DEFINE_MODULE
    nvkmod_version.h           #   per-kernel vermagic + struct module offsets (5.10/5.15/6.1/6.6/6.12)
  arm64/
    include/                   # NeverC's own minimal kernel headers
      linux/*.h                #   types, kernel, printk, list, slab, kprobes, kallsyms, module, fs, ...
      asm/*.h                  #   arch barriers, etc.
    lib/                       # optional libclang_rt.builtins-aarch64.a (see lib/README.md)
  tools/
    gen_struct_module_offsets.c# regenerate exact struct module offsets per kernel
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

## Writing a driver

The headers are organized like the kernel's own (`#include <linux/...>`), but are
NeverC's own minimal, layout-agnostic definitions: scalars and a few ABI-stable
aggregates are concrete, while version-sensitive structures (`task_struct`,
`mm_struct`, `device`, ...) are intentionally opaque — access their fields by
resolving offsets dynamically. `current` is read from `sp_el0` (stable on arm64).

See `examples/android-kernel-hello` (zero-import load test) and
`examples/android-kernel-driver` (dynamic-kallsyms template).

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
