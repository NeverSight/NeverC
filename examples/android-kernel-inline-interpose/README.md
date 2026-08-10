**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Examples](../../docs/examples/README.md)

# Android Kernel Function Interpose

Interposes `do_faccessat` at its entry point using `neverc_krt_interpose_register`. Demonstrates:

- **Auto-chain**: multiple handlers on the same target, dispatched by priority
- **Call-original pattern**: handler receives `orig` pointer to invoke the original function
- **Priority control**: lower value runs first; use negative to run before other interposes
- **Coexistence**: works even if the target is already interposed by another module

## API

```c
int neverc_krt_interpose_register(void *target, void *handler, int priority,
                             void **orig, struct neverc_krt_interpose_ref *ref);
int neverc_krt_interpose_unregister(struct neverc_krt_interpose_ref *ref);
```

Handler signature:

```c
long my_interpose(void *orig, void *a0, void *a1, void *a2, void *a3, void *a4, void *a5);
```

## Build

```bash
cd examples/android-kernel-inline-interpose
neverc make          # debug: -g (default on the first build)
neverc make release  # release: -O2 --strip
neverc make debug    # switch back to debug
```

Select another kernel preset with, for example,
`neverc make KERNEL=612 release`. `neverc make release` selects
`-O2 --strip`. The Makefile records the selected `KERNEL` and `PROFILE` in
`.nvk-build-flags`, so later `make push`, `make run`, and bare `make` calls keep
using that artifact. Without the stamp, `make` defaults to debug. `make debug`
or an explicit `PROFILE=...` replaces the saved profile; `make clean` removes
the stamp, so the next build defaults to debug.

NeverC writes IDA-inspired, non-reserved release names in five classes:
functions `fn_HEX`, executable no-type labels `code_HEX`, objects `obj_HEX`,
other no-type labels `sym_HEX`, and absolute symbols `abs_HEX`. For ordinary
allocated definitions, `HEX` is a deterministic `analysis EA` derived from the
final `SHF_ALLOC` section layout (`abs_HEX` instead uses the absolute
`st_value`); it is not a hash, encryption, file offset, ELF virtual address, or
runtime kernel address. NeverC stores neither reserved `sub_`/`loc_` forms nor
deliberately empty ordinary names.

For exact-name preservation, IDA's synthetic `extern` view, security boundaries,
and finalization-before-signing order, see the
[release and strip policy](../../docs/release-builds/README.md).

## Deploy & Run

```bash
neverc make run
```

Or manually:

```bash
adb push nvk_interpose_demo.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_interpose_demo.ko'
adb shell su -c 'dmesg | grep neverc_krt_interpose_demo'
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
adb shell su -c 'insmod /data/local/tests/nvk_interpose_demo.ko'
```

New lines appear in terminal 1 as the kernel handles the load. Press Ctrl+C to stop.

Note: stock `dmesg -w` is missing on some Android builds; `/proc/kmsg` needs root but follows live kernel output reliably.

## Unload

```bash
neverc make rmmod
```
