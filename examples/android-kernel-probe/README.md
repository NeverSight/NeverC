**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Probe

Hooks an arbitrary instruction inside `do_faccessat` (not the entry point) using `neverc_krt_probe_register`. Demonstrates:

- **Arbitrary-address hooking**: probe any instruction, not just function entries
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
neverc make
```

Change `KERNEL` to `515`, `601`, `606`, or `612` for other kernel versions.

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

## Unload

```bash
neverc make rmmod
```
