**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Ejemplos NeverC](../../docs/examples/README.es.md)

# Android Kernel Syscall Interpose

Interpose de `openat` reemplazando su puntero en `sys_call_table`. Demuestra la intercepción clásica de syscall en kernels ARM64 GKI con `neverc_krt_syscall_replace` / `neverc_krt_syscall_restore`.

## API

```c
int neverc_krt_syscall_replace(int nr, neverc_krt_syscall_fn_t new_fn,
                               neverc_krt_syscall_fn_t *orig);
int neverc_krt_syscall_restore(int nr, neverc_krt_syscall_fn_t orig);
```

## Compilar

```bash
cd examples/android-kernel-syscall-interpose
neverc make          # debug: -g (predeterminado en la primera compilación)
neverc make release  # release: -O2 --strip
neverc make debug    # volver a debug
```

Selecciona otro perfil del kernel, por ejemplo, con
`neverc make KERNEL=612 release`. `neverc make release` selecciona
`-O2 --strip`. El Makefile registra los valores elegidos de `KERNEL` y
`PROFILE` en `.nvk-build-flags`, por lo que `make push`, `make run` y
`make` sin objetivo siguen usando el mismo artefacto. Sin ese archivo de estado,
`make` usa debug por defecto. `make debug` o un `PROFILE=...` explícito sustituye
el perfil guardado; `make clean` elimina el archivo y devuelve la siguiente
compilación a debug.

NeverC escribe cinco clases de nombres de publicación inspirados en IDA pero no
reservados: funciones `fn_HEX`, etiquetas ejecutables sin tipo `code_HEX`,
objetos `obj_HEX`, otras etiquetas sin tipo `sym_HEX` y símbolos absolutos
`abs_HEX`. Para una definición asignada ordinaria, `HEX` es una `analysis EA`
determinista derivada de la disposición final de las secciones `SHF_ALLOC`
(`abs_HEX` usa en cambio el `st_value` absoluto); no es un hash, una encryption
(codificación), un file offset (desplazamiento de archivo), una ELF virtual
address (dirección virtual ELF) ni una runtime kernel address (dirección del
kernel en ejecución). NeverC no almacena las formas reservadas `sub_`/`loc_` ni
nombres ordinarios vaciados deliberadamente.

Para los nombres que deben conservarse exactos, la vista `extern` sintética de
IDA, los límites de seguridad y el orden entre finalización y firma, consulta la
[política de publicación y strip](../../docs/release-builds/README.es.md).

## Despliegue y ejecución

```bash
neverc make run
```

O manualmente:

```bash
adb push nvk_syscall_interpose.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_syscall_interpose.ko'
adb shell su -c 'dmesg | grep neverc_krt_syscall'
```

## Registro del kernel (en vivo)

En el dispositivo, `cat /proc/kmsg` transmite el ring buffer del kernel en tiempo real — similar a **DbgView** en Windows. Úselo cuando `insmod` falle con un error vago o necesite ver el motivo real del rechazo (vermagic, modversions, tamaño de sección, etc.).

Terminal 1 (dejar en ejecución):

```bash
adb shell
su
cat /proc/kmsg
```

Terminal 2:

```bash
adb shell su -c 'insmod /data/local/tests/nvk_syscall_interpose.ko'
```

Las líneas nuevas aparecen en el terminal 1 al cargar. Ctrl+C para detener.

Nota: en algunas builds de Android falta `dmesg -w`; `/proc/kmsg` requiere root pero sigue la salida del kernel en directo de forma fiable.

## Descargar

```bash
neverc make rmmod
```
