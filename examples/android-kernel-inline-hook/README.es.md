**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Function Hook

Hook de `do_faccessat` en su punto de entrada con `neverc_krt_hook_register`. Demuestra:

- **Encadenamiento automático**: múltiples handlers en el mismo objetivo, ejecutados por prioridad
- **Patrón de llamada al original**: el handler recibe un puntero `orig` para invocar la función original
- **Control de prioridad**: valor menor = ejecución primero; usar valores negativos para ejecutar antes que otros hooks
- **Coexistencia**: funciona incluso si el objetivo ya está hookeado por otro módulo

## API

```c
int neverc_krt_hook_register(void *target, void *handler, int priority,
                             void **orig, struct neverc_krt_hook_ref *ref);
int neverc_krt_hook_unregister(struct neverc_krt_hook_ref *ref);
```

Firma del handler:

```c
long my_hook(void *orig, void *a0, void *a1, void *a2, void *a3, void *a4, void *a5);
```

## Compilar

```bash
cd examples/android-kernel-inline-hook
neverc make
```

Cambiar `KERNEL` a `515`, `601`, `606`, `612` o `618` para otras versiones del kernel.

## Despliegue y ejecución

```bash
neverc make run
```

O manualmente:

```bash
adb push nvk_hook_demo.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_hook_demo.ko'
adb shell su -c 'dmesg | grep neverc_krt_hook_demo'
```

## Descargar

```bash
neverc make rmmod
```
