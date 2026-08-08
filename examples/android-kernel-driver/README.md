**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Examples](../../docs/examples/README.md)

# Android Kernel Driver Template

Driver template with dynamic symbol resolution via `kallsyms_lookup_name`. Imports only `register_kprobe`/`unregister_kprobe` (GKI-stable ABI). Single source works across all GKI kernels 5.10–6.12.

## Build

```bash
cd examples/android-kernel-driver
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
adb push nvk_driver.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_driver.ko'
adb shell su -c 'dmesg | grep neverc_krt_driver'
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
adb shell su -c 'insmod /data/local/tests/nvk_driver.ko'
```

New lines appear in terminal 1 as the kernel handles the load. Press Ctrl+C to stop.

Note: stock `dmesg -w` is missing on some Android builds; `/proc/kmsg` needs root but follows live kernel output reliably.

## Unload

```bash
neverc make rmmod
```

Or manually:

```bash
adb shell su -c 'rmmod neverc_krt_driver'
```
