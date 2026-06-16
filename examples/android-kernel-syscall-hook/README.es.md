**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Hook de syscall kernel Android

Reemplazo de tabla syscall en `openat`. Por defecto: intercambio de entrada. Con `-DNVK_SYSCALL_INLINE_HOOK`: parchea el prólogo del handler. Demuestra `nvk_syscall_replace`/`nvk_syscall_restore` y números syscall arm64.

## Compilación

```bash
cd examples/android-kernel-syscall-hook
neverc make
```

Cambie `KERNEL` a `515`, `601`, `606` o `612` para otras versiones.

## Despliegue y ejecución

```bash
neverc make run
```

O manualmente:

```bash
adb push nvk_syscall_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_syscall_hook.ko'
adb shell su -c 'dmesg | grep nvk_syscall_hook'
```

## Descarga

```bash
neverc make rmmod
```

O manualmente:

```bash
adb shell su -c 'rmmod nvk_syscall_hook'
```
