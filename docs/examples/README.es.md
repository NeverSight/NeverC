**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Documentación](../README.es.md) · [← Proyecto NeverC](../../docs/i18n/README.es.md)

# Ejemplos NeverC

Ejemplos compilables que demuestran las capacidades de compilación cruzada de NeverC. Todos compilan desde macOS / Linux — sin necesidad de entorno Windows.

---

## Ejemplos disponibles

| Ejemplo | Descripción | Características clave |
|---------|-------------|----------------------|
| [Controlador de kernel Windows](../../examples/windows-driver/README.es.md) | Controlador WDM mínimo | Compilación cruzada `.sys` desde macOS/Linux, auto-LTO, enlazador integrado |
| [Controlador Windows + CET](../../examples/windows-driver-cet/README.es.md) | Controlador con Intel CET Shadow Stack | Código kernel compatible CET, `/guard:ehcont` |
| [Controlador Windows + punto flotante](../../examples/windows-driver-float/README.es.md) | Controlador con punto flotante/SIMD | Punto flotante seguro en modo kernel |
| [Windows Ring3 EXE](../../examples/windows-exe/README.es.md) | Aplicación consola modo usuario | GetSystemInfo, enumeración procesos, VirtualAlloc |
| [Windows Ring3 DLL](../../examples/windows-dll/README.es.md) | DLL modo usuario | ReadProcessMemory, VirtualAllocEx, enumeración módulos |

### Linux

| Ejemplo | Descripción | Características |
|---------|-------------|----------------|
| [Linux Hello World](../../examples/linux-hello/README.es.md) | Programa C mínimo | Compilación cruzada desde macOS/Windows |
| [Linux POSIX](../../examples/linux-posix/README.es.md) | Programación de sistemas POSIX | pthreads, mmap, pipe, señales |
| [Linux Estático](../../examples/linux-static/README.es.md) | Binario completamente estático | Enlace `-static` |
| [Linux Red](../../examples/linux-network/README.es.md) | Demo socket TCP | Cliente/servidor |
| [Linux Math + zlib](../../examples/linux-math/README.es.md) | Math + compresión | Trigonometría, zlib, CRC32 |

### macOS

| Ejemplo | Descripción | Características |
|---------|-------------|----------------|
| [Aplicación macOS](../../examples/macos-app/README.es.md) | Ejecutable nativo Mach-O | sysctl, uname, Mach host_info/task_info, introspección de procesos |
| [Biblioteca dinámica macOS](../../examples/macos-dylib/README.es.md) | Biblioteca nativa `.dylib` | Mach vm_read/vm_write, vm_alloc/vm_dealloc, task_info, XOR |

### Android

| Ejemplo | Descripción | Características |
|---------|-------------|----------------|
| [Android ELF](../../examples/android-elf/README.es.md) | Binario ARM64 nativo para dispositivos rooteados | Compilación cruzada Android, dlopen/liblog, /proc, detección root |
| [Biblioteca compartida Android](../../examples/android-so/README.es.md) | Biblioteca `.so` nativa ARM64 | Biblioteca compartida, mmap RWX, cifrado XOR |

### Módulos kernel Android (.ko)

No se necesita árbol de fuentes del kernel — NeverC compila contra el runtime mínimo integrado. Fuente única para GKI 5.10–6.12.

| Ejemplo | Descripción | Características |
|---------|-------------|----------------|
| [Kernel Hello](../../examples/android-kernel-hello/README.es.md) | Módulo `.ko` mínimo | Bootstrap kallsyms vía kprobe, validación insmod mínima |
| [Plantilla driver kernel](../../examples/android-kernel-driver/README.es.md) | Plantilla de resolución dinámica de símbolos | `kallsyms_lookup_name`, ABI estable GKI, 5.10–6.12 |
| [Kernel Inline Interpose](../../examples/android-kernel-inline-interpose/README.es.md) | Interpose inline en `do_faccessat` | Parche seguro BTI/PAC, modo context interpose, reubicación PC-relativa |
| [Kernel Syscall Interpose](../../examples/android-kernel-syscall-interpose/README.es.md) | Tabla syscall / inline / context interpose | Reemplazo `sys_call_table`, interpose inline, context interpose |
| [Kernel Lowvis](../../examples/android-kernel-lowvis/README.es.md) | Gestión de visibilidad de módulo | Visibilidad list/sysfs/proc, wrappers de credenciales, estado de aplicación SELinux |
| [Kernel Full SDK](../../examples/android-kernel-full/README.es.md) | Integración SDK completa | Netlink IPC, interposes, wrappers de credenciales, visibilidad de módulo, control de política SELinux, VMA, archivos |
| [Kernel Chardev](../../examples/android-kernel-chardev/README.es.md) | Dispositivo carácter + ioctl | `misc_register`, despacho ioctl, `/proc` seq_file |
| [Kernel Netlink](../../examples/android-kernel-netlink/README.es.md) | IPC netlink bidireccional | Comandos PING/VERSION/ECHO, `nvk_nl_open`/`nvk_nl_reply` |

---

## Inicio rápido

```bash
cd examples/<nombre-ejemplo>
neverc make
```

Especificar ruta del compilador: `neverc make NEVERC=/path/to/neverc`

Todos los ejemplos usan **neverc** y producen binarios Windows PE (`.sys`) mediante el enlazador integrado.
