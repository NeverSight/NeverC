**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilador de shellcode](../README.es.md)

# Guía de extensión de plataforma

Este documento explica cómo extender el compilador de shellcode a nuevas plataformas objetivo. Actualmente soportado: **arm64 / x86_64 en macOS / Linux / Android / Windows** (8 triples), cada uno con contextos independientes **User** / **Kernel** (16 variantes en total). Agregar una nueva plataforma típicamente requiere unos cientos de líneas de código.

## Filosofía de diseño: Dirigido por tablas, no por ramas

Todas las pasadas son independientes del objetivo. Las diferencias de plataforma se concentran en **dos lugares**:

1. Entradas de tabla `describeTriple()` de `TargetDesc.cpp`
2. Switches de arquitectura de tres extractores (Mach-O / ELF / COFF)

Agregar nueva plataforma = una fila más en (1), un case más en (2).

## Pasos

### 1. Agregar fila en `TargetDesc`

Agregar la rama OS correspondiente en `describeTriple()`:

```cpp
if (TT.isOSFreeBSD()) {
  D.OS = ShellcodeOS::FreeBSD;
  D.Format = ObjectFormat::ELF;
  D.TextSectionName = ".text";
  if (D.Arch == ShellcodeArch::X86_64) {
    D.Syscall = SyscallABI::FreeBSDSyscall;
    D.AsmTemplate = "syscall";
    D.SyscallNumberReg = "rax";
    D.SyscallRetReg = "rax";
    D.ArgRegs = kX86_64FreeBSDArgRegs;
    D.NumArgRegs = 6;
    D.DriverInjectFlags = kX86_64UnixInjectFlags;
  }
  return D;
}
```

**Campos requeridos** (todos definidos en `TargetDesc.h`):

| Campo | Propósito | Si falta |
|-------|----------|---------|
| `OS` / `Arch` / `Format` | Clave de despacho | `describeTriple` retorna Unknown → driver rechaza temprano |
| `TextSectionName` | Extractor busca sección de entrada | No encuentra `.text` → rechazo |
| `Syscall` | Decisión de reemplazo de SyscallStubPass | `None` → SyscallStubPass no-op |
| `AsmTemplate` / `SyscallNumberReg` / `SyscallRetReg` / `ArgRegs` | Generación de asm inline de SyscallStubPass | Cualquiera vacío → SyscallStubPass no-op |
| `TCBReadAsm` / `TCBReadConstraint` | Asm inline de lectura TEB de WinPEBImportPass | Vacío → PEB walk genera InlineAsm vacío (Windows: requerido) |
| `DriverInjectFlags` | Flags específicos de plataforma en modo shellcode | null → sin inyección |

### 2. Extender `SyscallStub` / `SyscallTables` (si el OS tiene traps de kernel)

- Agregar valor enum a `SyscallABI` en `TargetDesc.h`
- Agregar `kXxxTable` en `SyscallTables.cpp`
- Agregar case en switch de `lookupSyscall`
- `SyscallStubPass` sin cambios — templates/restricciones InlineAsm vienen de `TargetDesc`

### 2.5 Extender lista blanca Win32 API de Windows

Windows no tiene ABI de syscall estable; las llamadas de usuario a `WriteFile` / `CreateThread` / `VirtualAlloc` pasan por `WinPEBImportPass`. La lista blanca es una tabla multi-DLL:

- Definida en `Tables/Win32Apis.def`
- Cada fila: `NEVERC_WIN32_API(ApiName, "dll.dll")`
- El resolver ya soporta DLLs arbitrarias vía `__neverc_win_resolve(dll_hash, api_hash)`

**Agregar nueva API** (ej. `DeviceIoControl`): 1 fila en `Win32Apis.def` + 1 declaración en `lib/Headers/windows.h`.

**Agregar nuevo bucket DLL** (ej. `winhttp.dll`): Solo agregar filas con el nuevo nombre DLL.

### 3. Extender el extractor correspondiente

1. Identificar tipos de relocalización → parchear bytes o rechazar
2. Actualizar lista de nombres de sección de datos prohibidos
3. Actualizar validación de rango de objetivo de relocalización entrada-en-offset-0

Para formato objeto completamente nuevo (ej. módulos WASM):
1. Agregar valor enum `ObjectFormat`
2. Agregar case en switch de despacho de `ShellcodeExtractor.cpp`
3. Escribir `<Format>Extractor.cpp` (siguiendo estructura de `ELFExtractor.cpp`)

### 4. Agregar Loader (solo herramienta de prueba)

Referencia `loader_linux.c` y `loader_windows.c`. Típicamente: `mmap(RWX) → memcpy → icache flush → call`.

### 5. Actualizar pruebas

Agregar línea `cross_compile_check` en `run_cross_target_tests.sh`.

---

## Problemas conocidos multiplataforma

- **Endianness**: NeverC solo soporta little-endian (LE).
- **Diferencias ABI**: Win64 vs System V AMD64 tienen registros de argumentos completamente diferentes. Manejado en la capa frontend de Clang.
- **Números de syscall**: Diferentes por arquitectura en Linux, Android igual a Linux, Darwin tiene números BSD propios, Windows sin números estables (PEB walk).
- **Coherencia de caché**: ARM requiere flush explícito de i-cache; x86 no.
- **SELinux / W^X**: Android restringido por SELinux `execmem`; iOS no-jailbreak rechaza completamente `mmap(RWX)`.

## Roadmap de extensión futura

| Objetivo | Esfuerzo estimado | Dependencias |
|----------|-----------------|--------------|
| **iOS arm64** (jailbreak / `MAP_JIT`) | 1 día | Reutilizar extractor Mach-O |
| **FreeBSD / OpenBSD x86_64** | Medio día | Reutilizar extractor ELF + nueva tabla syscall |
| **RISC-V64 Linux** | 2 días | Necesita RISC-V TargetDesc + nueva variante AllBlr + parcheo reloc RISC-V |

## Interfaz de extensión de pasada de ofuscación

La pipeline shellcode expone 11 hooks vía `Pipeline.h::ObfuscationHooks` para bibliotecas de ofuscación de terceros. MIR patching integrado también es dirigido por tablas: `Tables/MIRRewritePatterns.def` y `Tables/MIRRewriteOpcodes.def`. Preferir entradas de tabla y helpers estrechos sobre ramas específicas de objetivo dispersas en el cuerpo del pass.
