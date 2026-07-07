**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Probe

Interpose de una instruccion arbitraria dentro de `do_faccessat` (no el punto de entrada) con `neverc_krt_probe_register`. Demuestra:

- **Interpose en direccion arbitraria**: sondear cualquier instruccion, no solo entradas de funciones
- **Contexto completo de registros**: leer/escribir todos los GPR via `neverc_krt_reg_ctx`
- **Encadenamiento automatico**: multiples handlers en la misma direccion, ejecutados por prioridad
- **Control de flujo**: `NEVERC_KRT_CTX_SKIP` para abortar, `NEVERC_KRT_CTX_REDIRECT` para redirigir

## API

```c
int neverc_krt_probe_register(void *addr, neverc_krt_ctx_handler_t handler,
                              int priority, struct neverc_krt_probe_ref *ref);
int neverc_krt_probe_unregister(struct neverc_krt_probe_ref *ref);
```

Firma del handler:

```c
void my_probe(neverc_krt_reg_ctx *ctx);
```

## Compilar

```bash
cd examples/android-kernel-probe
neverc make
```

Cambiar `KERNEL` a `515`, `601`, `606`, `612` o `618` para otras versiones del kernel.

## Despliegue y ejecucion

```bash
neverc make run
```

O manualmente:

```bash
adb push nvk_probe.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_probe.ko'
adb shell su -c 'dmesg | grep neverc_krt_probe_demo'
```

## Descargar

```bash
neverc make rmmod
```
