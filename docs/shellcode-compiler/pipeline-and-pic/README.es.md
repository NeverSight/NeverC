**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilador de shellcode](../README.es.md)

# Pipeline de Shellcode, MIR y Estrategia PIC (Notas de Diseño)

Este documento describe los compromisos de diseño en el modo shellcode de NeverC a través de la cadena **IR → optimización LLVM → backend MIR → archivo objeto → extracción/parcheo**, y su relación con la política de **PIC predeterminado a nivel de compilador**. Los detalles de implementación son autoritativos en el código fuente y los comentarios en inglés.

## 1. Por qué forzar PIC por defecto (incluyendo compilación no-shellcode)

El extractor de shellcode asume que las referencias a símbolos externos en el fragmento ejecutable caen en relocalizaciones **relativas a PC** o resolubles intra-`.text`, no en direcciones absolutas codificadas o pools de constantes que requieren un cargador para llenar `.data`.

NeverC retorna **true** en `Generic_GCC::isPICDefaultForced()`, `MachO::isPICDefaultForced()` y `MSVCToolChain::isPICDefaultForced()`, distinguiéndose del comportamiento de Clang upstream "PIC predeterminado opcional": **toda compilación multiplataforma siempre usa PIC como único modelo**. Esto significa:

- La compilación C normal y la compilación `-fshellcode` comparten los mismos hábitos de relocalización, reduciendo la carga cognitiva "funciona normalmente, falla bajo shellcode".
- Los backends de Linux / Android / macOS / Windows comparten las mismas suposiciones bajo descriptores dirigidos por tablas (`TargetDesc` + `Options.td.h`), evitando codificación dura `if (linux)` en el driver.

Esta política no distingue si `-fshellcode` está habilitado o si el contexto es user/kernel. Incluso si el usuario pasa `-fno-pic` / `-static` / `-mkernel` / `-mdynamic-no-pic`, `ParsePICArgs()` mantiene `Reloc::PIC_`, usando las mismas suposiciones relativas a PC para compilación normal, shellcode modo usuario y shellcode modo kernel.

## 2. División de trabajo IR y MIR en dos fases

### 2.1 Capa IR (`registerShellcodePasses`)

Responsable de comprimir la semántica "C normal" en una forma de **entrada única, sin sección de datos independiente, sin globales problemáticos**: `ZeroRelocPass`, `IndirectBrPass`, `MemIntrinPass`, `StringRuntimePass`, `HeapArenaPass`, `CompilerRtPass`, `SyscallStubPass`, `WinPEBImportPass`, `KernelImportPass` (solo kernel), `Data2TextPass`, etc.

**Principio**: Los problemas solucionables en IR con enfoques estructurados se corrigen primero en IR (pools de constantes, BlockAddress, caída de `memcpy` a libc, caída de `__int128 /` a `__udivti3`, etc.), simplificando el flujo de bytes visto por el backend y el extractor. Para escenarios con alta carga cognitiva del usuario que pueden internalizarse de forma segura, el driver inyecta reglas proactivamente (ej. `long double` de AArch64 Linux / Android / Windows se degrada a binary64 en modo shellcode). Solo las construcciones que no pueden soportarse sin un runtime activan diagnósticos MIR/extractor.

### 2.2 Capa MIR (`registerShellcodeMachinePasses`)

Registra callbacks en el `TargetPassConfig` legacy de LLVM **después de la asignación de registros, antes de `addPreEmitPass`**, en este orden:

1. Usuario/biblioteca de ofuscación: `RunBeforePreEmit` (pseudos CFI / EH aún presentes; útil para transformaciones dependientes de metadatos).
2. **`ShellcodeMIRPrepPass`**: Elimina pseudos que generarían secciones laterales `.eh_frame` / `.pdata` / `.xray_*`, haciendo que el flujo de instrucciones antes de AsmPrinter sea lo más cercano a "código puro" posible.
3. Usuario/biblioteca de ofuscación: `RunAfterPreEmit` (adecuado para sustitución de instrucciones, renombramiento de registros y ofuscación similar de "forma final de código máquina").

**Principio**: Si las secuencias de instrucciones nativas aún tienen problemas, corregir en MIR (especialmente alrededor de `ShellcodeMIRPrepPass`); **la extracción y el parcheo son la última red de seguridad**, evitando duplicar la misma lógica en las capas COFF/ELF/Mach-O.

Los nombres de opcode MIR no se dispersan en el flujo de control del pass; `ShellcodeMIRPrepPass` usa la tabla `(pattern, role, opcode)` de `Tables/MIRRewriteOpcodes.def` vía `TargetInstrInfo::getName()`. Al agregar sustituciones de instrucciones amigables con shellcode, se prefiere agregar entradas de tabla y reescrituras MIR pequeñas; solo recurrir a cambios de selección de instrucciones del backend `.td` cuando sea necesario, con el fallback de formato de objeto a nivel de extractor como último recurso.

> Nota: `ShellcodeMIRPrepPass` solo se registra cuando `-fshellcode` está habilitado. Los programas normales no deben eliminar globalmente CFI/EH, ya que esto rompería el manejo normal de excepciones y la información de depuración.

Tanto los callbacks globales de IR como de MIR usan un patrón de **registrar una vez, leer la instantánea actual de `ShellcodeOptions` en tiempo de ejecución**. Esto soporta procesos de compilador embebido de larga duración: cuando el mismo proceso primero compila shellcode y luego C normal, la compilación C normal no hereda los passes IR/MIR anteriores; al compilar múltiples TUs de shellcode consecutivamente, los registros duplicados de callbacks globales no apilan el mismo conjunto de passes múltiples veces.

## 3. Diferencias de plataforma dirigidas por tablas

- **Triple → comportamiento**: Centralizado en `describeTriple()` de `TargetDesc.cpp` y campos `TargetDesc` (nombre de sección, ABI de syscall, plantilla de ensamblaje en línea, flags de inyección del driver, etc.). Al agregar un nuevo OS/Arch, se prefiere **agregar entradas de tabla** en lugar de escribir ramas largas en extractores o passes.
- **Opciones CLI**: Definidas en `neverc/include/neverc/Invoke/Options.td.h`; consumidas por `DriverIntegration.cpp` usando enums `OPT_*`, evitando magia de strings.

## 4. Toolchain Windows MSVC y diseño del SDK

Al compilar cruzado para objetivos Windows, NeverC soporta dos fuentes de SDK **sin rutas absolutas codificadas**:

1. **SDK integrado** (predeterminado): NeverC incluye un Windows SDK y WDK completo en `runtime/`. Los encabezados están en `runtime/windows/shared/`, las bibliotecas por arquitectura en `runtime/windows/{x64,arm64}/`. Diseño tras compilación:

   ```
   build-neverc/bin/neverc
   build-neverc/runtime/windows/shared/msvc/  (encabezados)
   build-neverc/runtime/windows/x64/msvc/     (bibliotecas x64)
   build-neverc/runtime/windows/arm64/msvc/   (bibliotecas arm64)
   ```

2. **Sysroot explícito estilo VS** (opcional): Si tiene un árbol de directorios `VC/Tools/MSVC/<version>/...` + `Windows Kits/10/...`, apuntar con `-vctoolsdir=<path>` o `-winsysroot=<path>`. Esta ruta tiene prioridad sobre el SDK integrado.

Ambas fuentes funcionan sin registro ni variables de entorno VS proporcionadas por el SO, habilitando la compilación cruzada de shellcode Windows desde macOS / Linux.

## 5. Puntos de ofuscación y extensión

- **Hooks IR**: 6 puntos de enganche IR (`NEVERC_HOOK_SC_BEFORE_PREP` a `NEVERC_HOOK_SC_AFTER_FINAL_IR`) vía la [API de Plugins](../../plugin-api/README.es.md). `NEVERC_HOOK_SC_AFTER_FINAL_IR` es el verdadero último punto de inyección IR — los passes registrados aquí no tienen passes subsecuentes que modifiquen su salida. 11 puntos de hook en total (6 IR + 3 MIR + 2 flujo de bytes).
- **Ofuscación MIR**: `RunBeforePreEmit` / `RunAfterPreEmit` son hooks MIR de granularidad media; `RunAfterFinalMIR` es el **verdadero último** hook MIR (extensión fork agrega `RegisterTargetPassConfigPostPreEmitCallbackFn` invocado después de `addPreEmitPass2()`). `-fshellcode-mir-obfuscate=` especifica MIR spec por separado; por defecto usa IR spec si no se establece.
- **Hooks de flujo de bytes**: `RunPostExtract` es el hook **pre**-finalize; `RunPostFinalize` es el hook **post**-finalize (último momento antes de escritura a disco, NeverC no audita más).
- **Tamaño / alineación / relleno**: `-fshellcode-max-length=`, `-fshellcode-align=`, `-fshellcode-pad=` se ejecutan al final de finalize; el driver rechaza configuraciones contradictorias.
- **Elección de diseño**: Ofuscación, polimorfismo, codificadores por etapas, syscalls indirectos y características similares de capa de estrategia **no están integrados intencionalmente**, solo disponibles como plugins opcionales.

## 6. Dimensión modo kernel (Ring-0)

El modo shellcode introduce `-mshellcode-context=user|kernel` como la segunda dimensión del pipeline, superpuesta al triple:

- **Modo usuario**: Pipeline PEB walk / syscall stub.
- **Modo kernel**:
  - `SyscallStubPass` / `WinPEBImportPass` retornan temprano a nivel de pass.
  - `TargetDesc::KernelInjectFlags` agrega flags de backend apropiados para OS/arch (Unix x86_64: `-mno-red-zone -mcmodel=kernel`, Windows: `/kernel`, AArch64: `-mgeneral-regs-only`).
  - `KernelImportPass` reescribe llamadas directas extern no resueltas a llamadas indirectas respaldadas por resolver, inyectando parámetros de prefijo implícitos `(resolver, cookie)` cuando es necesario.
  - `<neverc/kernel.h>` expone `neverc_kern_resolve_t`, `neverc_kern_hash()` y firmas relacionadas del lado kernel; los shims de modo usuario (`<windows.h>`, `<unistd.h>`, etc.) rechazan en modo kernel vía `#error`.

Ver [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.es.md) para detalles.

## 7. Capa de compatibilidad Windows POSIX

### 7.1 Problema

El código C multiplataforma comúnmente usa `write(fd, buf, n)`, `read(fd, buf, n)`, `exit(code)`, etc. En Unix, `SyscallStubPass` los reemplaza con syscalls en línea. En Windows, estos nombres POSIX no tienen API Win32 correspondiente, causando errores de "relocalización no resuelta".

### 7.2 Objetivo de diseño

**Cero conciencia del usuario**: El mismo código fuente C compila en los 8 triples objetivo sin `#ifdef _WIN32` ni llamadas manuales a API Win32.

### 7.3 Implementación

`WinPEBImportPass` implementa procesamiento en tres fases:

1. **Fase 1 — Escaneo POSIX**: Escanea declaraciones extern no coincidentes contra una tabla de compatibilidad POSIX.
2. **Fase 2 — Generación de wrapper puente**: `Win32PosixCompat.def` despacha nombres POSIX a constructores de wrappers que generan wrappers `always_inline` (ej. `write` → `GetStdHandle` + `WriteFile`, `mmap` → `VirtualAlloc` con mapeo de prot, `exit` → `ExitProcess`, etc.). 13 grupos de funciones POSIX cubiertos.
3. **Fase 3 — Resolución PEB**: Las APIs Win32 referenciadas por wrappers se resuelven a través del resolver PEB walk normal.

### 7.4 Extensibilidad

Agregar nuevas funciones de compatibilidad POSIX: adiciones solo-alias cambian solo `Win32PosixCompat.def`; nueva semántica requiere un pequeño constructor IR + una entrada de tabla. Operaciones con estado como `open→CreateFileA` (necesitan tablas de tiempo de vida fd/handle) intencionalmente no se emulan.

## 8. Auto-corrección de declaración implícita K&R

Cuando los usuarios llaman funciones POSIX sin `#include`, C89 genera declaraciones implícitas K&R con 0 parámetros formales. `SyscallStubPass` mantiene una tabla `getCanonicalSyscallType()` con tipos de función LLVM IR canónicos para 50+ funciones POSIX comunes. Al detectar una declaración K&R de 0 parámetros, se sustituye automáticamente la firma canónica.

## 9. Resumen

| Tema | Enfoque |
|------|---------|
| PIC predeterminado | Todas las toolchains `isPICDefaultForced()==true`, alineado con suposiciones shellcode |
| Corregir primero en IR | Constantes, saltos indirectos, intrínsecos de memoria eliminados en IR cuando es posible |
| Red de seguridad MIR | `ShellcodeMIRPrepPass` + hooks pre/post, luego extracción/parcheo de objeto como último recurso |
| Minimizar codificación dura | `TargetDesc` + `Options.td.h` dirigido por tablas |
| Dos dimensiones user/kernel | `-fshellcode` × `-mshellcode-context={user,kernel}`; cada (OS, arch, level) es una fila en `describeTriple()` |
| Compatibilidad Windows POSIX | `WinPEBImportPass` puentea 13 grupos de funciones POSIX (write→WriteFile, mmap→VirtualAlloc, etc.) |
| Auto-corrección K&R | `SyscallStubPass` recurre a firmas POSIX canónicas en declaraciones de 0 parámetros |

## 10. Constantes multiplataforma de headers shim

Los headers shim (`sys/mman.h`, `fcntl.h`, etc.) exponen constantes que deben coincidir con el ABI del kernel objetivo, ya que los stubs de syscall de shellcode pasan estos valores directamente al kernel sin traducción de libc.

Diferencias clave:

| Constante | Darwin | Linux/Android |
|-----------|--------|---------------|
| `AT_FDCWD` | `-2` | `-100` |
| `MAP_ANONYMOUS` | `0x1000` | `0x20` |
| `O_CREAT` | `0x0200` | `0x0040` |
| `O_TRUNC` | `0x0400` | `0x0200` |
| `O_CLOEXEC` | `0x1000000` | `0x80000` |

Implementación: guardas `#if defined(__APPLE__)` en headers shim. La tabla de compatibilidad POSIX de `SyscallTables.cpp` usa valores Linux (`AT_FDCWD = -100`), activa solo en rutas `SyscallABI::LinuxSvc0` / `LinuxSyscall`. Los objetivos Windows no usan estos headers POSIX; el puente POSIX→Win32 lo manejan los wrappers de compatibilidad de `WinPEBImportPass`.
