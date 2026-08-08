**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Documentación](../README.es.md) · [← Proyecto NeverC](../../docs/i18n/README.es.md)

# Ejemplos NeverC

Ejemplos compilables que demuestran las capacidades de compilación cruzada de NeverC. Todos compilan desde macOS / Linux — sin necesidad de entorno Windows.

---

## Ejemplos disponibles

### Backends de servidor

| Ejemplo | Descripción | Características clave |
|---------|-------------|----------------------|
| [Servidor de juego autoritativo](../../examples/network-authoritative-server/README.es.md) | Backend de juego multiplataforma | Tick fijo 60 Hz, sesiones TCP, entrada UDP/QUIC, protección anti-replay |
| [Recolector anti-trampas](../../examples/network-anticheat-collector/README.es.md) | Ingesta de telemetría reforzada | mTLS, NRPC streaming, telemetría HMAC, pipeline de auditoría acotado |

### Windows

| Ejemplo | Descripción | Características clave |
|---------|-------------|----------------------|
| [Controlador de kernel Windows](../../examples/windows-driver/README.es.md) | Controlador WDM mínimo | Compilación cruzada `.sys` para **x64** (predeterminado) y **ARM64**, auto-LTO, enlazador integrado |
| [Controlador Windows + CET](../../examples/windows-driver-cet/README.es.md) | Controlador con Intel CET Shadow Stack | Código kernel compatible CET (**solo x64**), `/guard:ehcont` |
| [Controlador Windows + punto flotante](../../examples/windows-driver-float/README.es.md) | Controlador con punto flotante/SIMD | Punto flotante seguro en modo kernel en **x64** y **ARM64** |
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
| [Kernel Probe](../../examples/android-kernel-probe/README.es.md) | Sondear una instrucción arbitraria | `neverc_krt_probe_register`, contexto completo de registros, encadenado por prioridad, omitir/redirigir |
| [Kernel Multiarchivo](../../examples/android-kernel-multifile/README.es.md) | Módulo de kernel multiarchivo | Un solo `NEVERC_KRT_BOOTSTRAP()`, estado compartido `weak_odr`, división init/interpose/utilidades |

---

## Inicio rápido

Todos los ejemplos siguen el mismo patrón:

```bash
cd examples/example-name
neverc make
```

Sobre el controlador Makefile en sí, véase [`neverc build` / `make` →](../build/README.es.md).

Sobrescribe la ruta del compilador si es necesario:

```bash
neverc make NEVERC=/path/to/neverc
```

Los ejemplos de controladores Windows seleccionan la arquitectura con `ARCH`
(x64 por defecto). El ejemplo CET es solo para x64 — CET es una característica
de x86:

```bash
neverc make ARCH=x64        # Build for x64 (default)
neverc make ARCH=arm64      # Build for ARM64
neverc make all-arch        # Build every architecture the example supports
neverc make TESTSIGN=1      # Attach an Authenticode test signature
```

Los ejemplos de Linux admiten selección de arquitectura:

```bash
neverc make TARGET=aarch64-linux-gnu   # Build for ARM64
neverc make TARGET=x86_64-linux-gnu    # Build for x86_64 (default)
```

Los ejemplos de macOS admiten selección de arquitectura:

```bash
neverc make TARGET=arm64-apple-macos     # Build for Apple Silicon (default)
neverc make TARGET=x86_64-apple-macos    # Build for Intel
```

Los ejemplos de Android apuntan a ARM64 por defecto:

```bash
cd examples/android-elf
neverc make            # Build
neverc make run        # Build + push to device + run via adb
```

---

## Aspectos multiplataforma

- **Cadena de herramientas única**: NeverC maneja preprocesamiento, compilación, optimización (auto-LTO) y enlazado en una sola invocación
- **SDK integrado**: Windows SDK/WDK, sysroot de Linux (Ubuntu 22.04), sysroot de macOS (macOS 14) y sysroot de Android (NDK r26c, API 21+) están integrados en `runtime/` — cero dependencias externas
- **Independiente del host**: Compila desde macOS (arm64/x86_64), Linux (x86_64/aarch64) o Windows con comandos idénticos
- **Multi-objetivo**: Compilación cruzada a Windows PE (`.sys`/`.exe`/`.dll`), Linux ELF, macOS Mach-O (`.dylib`) y Android ELF desde cualquier host
- **Soporte de depuración**: Pasa `-g` para información de depuración DWARF; inspecciona con `llvm-dwarfdump`
