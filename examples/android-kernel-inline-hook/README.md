**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Function Hook

Hooks `do_faccessat` at its entry point using `neverc_krt_hook_register`. Demonstrates:

- **Auto-chain**: multiple handlers on the same target, dispatched by priority
- **Call-original pattern**: handler receives `orig` pointer to invoke the original function
- **Priority control**: lower value runs first; use negative to run before other hooks
- **Coexistence**: works even if the target is already hooked by another module

## API

```c
int neverc_krt_hook_register(void *target, void *handler, int priority,
                             void **orig, struct neverc_krt_hook_ref *ref);
int neverc_krt_hook_unregister(struct neverc_krt_hook_ref *ref);
```

Handler signature:

```c
long my_hook(void *orig, void *a0, void *a1, void *a2, void *a3, void *a4, void *a5);
```

## Build

```bash
cd examples/android-kernel-inline-hook
neverc make
```

Change `KERNEL` to `515`, `601`, `606`, or `612` for other kernel versions.

## Deploy & Run

```bash
neverc make run
```

Or manually:

```bash
adb push nvk_hook_demo.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_hook_demo.ko'
adb shell su -c 'dmesg | grep neverc_krt_hook_demo'
```

## Unload

```bash
neverc make rmmod
```
