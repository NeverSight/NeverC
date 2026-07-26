**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Ejemplos NeverC](../../docs/examples/README.es.md)

# Android Kernel Function Interpose

Interpose de `do_faccessat` en su punto de entrada con `neverc_krt_interpose_register`. Demuestra:

- **Encadenamiento automático**: múltiples handlers en el mismo objetivo, ejecutados por prioridad
- **Patrón de llamada al original**: el handler recibe un puntero `orig` para invocar la función original
- **Control de prioridad**: valor menor = ejecución primero; usar valores negativos para ejecutar antes que otros interposes
- **Coexistencia**: funciona incluso si el objetivo ya está interposeeado por otro módulo

## API

```c
int neverc_krt_interpose_register(void *target, void *handler, int priority,
                             void **orig, struct neverc_krt_interpose_ref *ref);
int neverc_krt_interpose_unregister(struct neverc_krt_interpose_ref *ref);
```

Firma del handler:

```c
long my_interpose(void *orig, void *a0, void *a1, void *a2, void *a3, void *a4, void *a5);
```

## Compilar

```bash
cd examples/android-kernel-inline-interpose
neverc make
```

Cambiar `KERNEL` a `515`, `601`, `606`, `612` o `618` para otras versiones del kernel.

## Despliegue y ejecución

```bash
neverc make run
```

O manualmente:

```bash
adb push nvk_interpose_demo.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_interpose_demo.ko'
adb shell su -c 'dmesg | grep neverc_krt_interpose_demo'
```

## Descargar

```bash
neverc make rmmod
```
