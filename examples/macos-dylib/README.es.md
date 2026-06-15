**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Ejemplo de biblioteca dinámica macOS

Una biblioteca dinámica nativa macOS `.dylib` compilada de forma cruzada con NeverC. Encapsula interfaces del kernel Mach para introspección de tareas y operaciones de memoria virtual — diseñada para investigación de seguridad. Compilación desde macOS, Windows o Linux — sin Xcode.

## Compilación

Desde el repositorio (objetivo por defecto: `arm64-apple-macos`):

```bash
cd examples/macos-dylib
neverc make
```

Compilar para Intel:

```bash
neverc make TARGET=x86_64-apple-macos
```

Con una versión independiente de NeverC:

```bash
neverc make NEVERC=/path/to/neverc
```

## Compilación manual (sin Make)

```bash
neverc --target=arm64-apple-macos -Wall -dynamiclib -o libneverc.dylib lib.c
```

## Funcionalidades

- Exporta wrapper `nc_task_basic_info` para consultas Mach `task_info`
- Proporciona `nc_vm_read`/`nc_vm_write` para lectura/escritura de memoria virtual Mach
- `nc_vm_alloc`/`nc_vm_dealloc` para asignación y liberación de memoria VM Mach
- Función auxiliar de cifrado XOR y consultas de PID/tarea
