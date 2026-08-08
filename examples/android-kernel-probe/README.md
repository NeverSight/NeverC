**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Examples](../../docs/examples/README.md)

# Android Kernel Probe

Interposes an arbitrary instruction inside `do_faccessat` (not the entry point) using `neverc_krt_probe_register`. Demonstrates:

- **Arbitrary-address interposing**: probe any instruction, not just function entries
- **Full register context**: read/write all GPRs via `neverc_krt_reg_ctx`
- **Auto-chain**: multiple handlers on the same address, dispatched by priority
- **Control flow**: `NEVERC_KRT_CTX_SKIP` to abort, `NEVERC_KRT_CTX_REDIRECT` to redirect

## API

```c
int neverc_krt_probe_register(void *addr, neverc_krt_ctx_handler_t handler,
                              int priority, struct neverc_krt_probe_ref *ref);
int neverc_krt_probe_unregister(struct neverc_krt_probe_ref *ref);
```

Handler signature:

```c
void my_probe(neverc_krt_reg_ctx *ctx);
```

## Build

```bash
cd examples/android-kernel-probe
neverc make          # debug: -g (default on the first build)
neverc make release  # release: -O2 --strip
neverc make debug    # switch back to debug
```

Select another preset with, for example, `neverc make KERNEL=612 release`.
The Makefile persists both `KERNEL` and `PROFILE`, so later `make push`/`run`
commands use the artifact you selected instead of silently rebuilding another
profile.

Release stripping is integrated into NeverC and is module-safe: it removes
DWARF, `.comment`, and relocation-unneeded private/undefined symbol names, but
keeps the ET_REL symbol/string tables, relocations, imports, global definitions,
`__versions`, `.codetag.alloc_tags`, and other loader ABI data. It is not
strip-all or obfuscation; relocation-required names can remain. If the module
will be signed, strip first and sign the final bytes. Never put stripping in
`clean`, never run `llvm-strip --strip-all` on a `.ko`, and do not blindly
remove `.codetag.alloc_tags` or `__codetag_*` sections.

## Deploy & Run

```bash
neverc make run
```

Or manually:

```bash
adb push nvk_probe.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_probe.ko'
adb shell su -c 'dmesg | grep neverc_krt_probe_demo'
```

## Kernel log (live)

On the device, `cat /proc/kmsg` streams the kernel ring buffer in real time — similar to **DbgView** on Windows. Use it when `insmod` fails with a vague error or you need the exact kernel rejection reason (vermagic, modversions, section size, and so on).

Terminal 1 (leave running):

```bash
adb shell
su
cat /proc/kmsg
```

Terminal 2:

```bash
adb shell su -c 'insmod /data/local/tests/nvk_probe.ko'
```

New lines appear in terminal 1 as the kernel handles the load. Press Ctrl+C to stop.

Note: stock `dmesg -w` is missing on some Android builds; `/proc/kmsg` needs root but follows live kernel output reliably.

## Unload

```bash
neverc make rmmod
```
