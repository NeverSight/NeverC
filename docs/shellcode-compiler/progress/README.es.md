**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilador de shellcode](../README.es.md)

# Compilador de shellcode — Seguimiento de progreso

## Etapa 0 — macOS arm64 MVP (entregado)

- [x] Esqueleto de directorio y CMake (biblioteca `nevercShellcode`)
- [x] `ZeroRelocPass`: dos fases (Prep + Stackify), apilamiento automático de globales mutables
- [x] `Data2TextPass`: dos fases (arrays constantes → almacenamiento en pila por fragmentos; división de constantes vectoriales post-SROA; ConstantFP → patrones de bits cargados con volatile)
- [x] `SyscallStubPass`: whitelist basada en tabla para Darwin BSD / Linux arm64 / Linux x86_64 / Android syscalls
- [x] `AllBlrPass`: reescritura agresiva opcional de llamadas indirectas
- [x] `ShellcodeExtractor`: Mach-O `.o` → `.bin` plano con parcheo de reubicaciones intra-sección
- [x] Opciones CLI via `neverc/include/neverc/Invoke/Options.td.h` generado: `-fshellcode`, `-fshellcode-all-blr`, `-mshellcode-syscall`, `-fshellcode-keep-obj=`, `-fshellcode-entry=`
- [x] PIC por defecto en todas las plataformas (`isPICDefault()` retorna `true` universalmente)
- [x] Apilamiento recursivo genérico (tablas de punteros a función, tablas de punteros a cadena, tablas de estructuras anidadas, inicializadores ConstantExpr GEP/BitCast)
- [x] `IndirectBrPass`: GCC computed-goto (`&&label`) → switch, incluyendo compartición de tablas de múltiples sitios de despacho
- [x] Inlining de constantes vectoriales SIMD (`inlineVectorConstants`)
- [x] Degradación automática de `_Thread_local` a static
- [x] Cargador nativo macOS arm64 (MAP_JIT + i-cache flush)

**Tests**: 108/108 aserciones de shellcode pasadas. Tamaños binarios: `add` 8B, `fib` 64B, `hello` 64B, `big_const` 632B.

## Etapa 1 — Linux / Android / Windows multiplataforma (entregado)

- [x] Abstracción `TargetDesc`: diferencias de plataforma basadas en tabla
- [x] Semántica `-mshellcode-syscall` multiplataforma (reemplaza `-mshellcode-libsystem` exclusivo de Darwin)
- [x] Tablas de números de syscall Linux / Android (Darwin BSD 80+, Linux arm64 60+, Linux x86_64 90+)
- [x] `ShellcodeExtractor` refactorizado en `MachOExtractor` / `ELFExtractor` / `COFFExtractor`
- [x] Extractor ELF (arm64: `R_AARCH64_CALL26`/`JUMP26`/`ADR_PREL_PG_HI21`/etc.; x86_64: `R_X86_64_PC32`/`PLT32`)
- [x] Extractor COFF (arm64: `IMAGE_REL_ARM64_BRANCH26`/etc.; x86_64: `IMAGE_REL_AMD64_REL32`/etc.)
- [x] Paso de importación PEB de Windows (`WinPEBImportPass`) con resolver PEB walk real
- [x] Whitelist Win32 API multi-DLL (~190 APIs en kernel32/ntdll/user32/ws2_32/advapi32/shell32)
- [x] `MemIntrinPass`: memcpy/memset/memmove/memcmp/bcmp/bzero/memchr + strlen/strcpy/strcmp/etc. → helpers de bucle de bytes inline
- [x] `CompilerRtPass`: división/módulo `__int128` → helpers de división larga inline
- [x] Soporte frontend Windows `aarch64-pc-windows-msvc`
- [x] `MIRPrepPass`: eliminación de pseudo-instrucciones multiplataforma (CFI/EH/XRay/StackMap/SEH/FENTRY/etc.)
- [x] Hooks de ofuscación MIR + nivel de byte (11 hooks en capas IR/MIR/flujo de bytes)
- [x] Degradación automática de AArch64 no-Darwin `long double` a binary64
- [x] Cabeceras shim de shellcode: `<windows.h>`, `<unistd.h>`, `<fcntl.h>`, `<sys/stat.h>`, `<sys/mman.h>`, `<string.h>`, `<stdlib.h>`
- [x] Capa de compatibilidad POSIX de Windows (13 puentes POSIX→Win32: write→WriteFile, mmap→VirtualAlloc, etc.)
- [x] Corrección automática de declaración implícita K&R (50+ firmas POSIX canónicas)
- [x] Purificación basada en tabla (codificación rígida de ramas de arquitectura → cero)
- [x] `KernelImportPass`: reescritura automática de sitios de llamada respaldada por resolver ring-0
- [x] Diagnósticos basados en tabla de nombres de helpers del kernel (`KernelHelperNames.def`)
- [x] `<neverc/kernel.h>` para convenciones de punto de entrada ring-0
- [x] Imposición de offset cero del punto de entrada (`placeEntryFirst`)
- [x] Pipeline de finalización: SDK de reescritor de bytes prohibidos + SDK de codificador de charset + restricciones de tamaño
- [x] SDK de plugins (`Plugin.h`): `registerBadByteRewriteStrategy` + `registerCharsetEncoder`
- [x] Inyección de `-mno-implicit-float` para x86_64 (previene desbordamiento de pool de constantes SSE del backend)
- [x] Cargadores multiplataforma (macOS/Linux/Windows)

**Tests**: 743+ aserciones de shellcode, todas pasadas en 8 triples. Suite NeverC completa: 1000+ tests pasados.

## Etapa 2 — Codificador imprimible / alfanumérico (planificado)

- [ ] Codificador de shellcode imprimible ARM64 (subconjunto de instrucciones 0x20–0x7e)
- [ ] Codificador alfanumérico x86_64
- [ ] Generación de stub autodescodificante (decoder stub)
- [ ] Estadísticas de tamaño / entropía post-codificación

## Etapa 3 — Polimorfismo / automodificación (planificado)

- [ ] Motor polimórfico: mismo código fuente → secuencias de bytes equivalentes diferentes por compilación
- [ ] Código automodificante: descifrado / descompresión del cuerpo del payload en tiempo de ejecución
- [ ] Anti-detección: evitar patrones de firma de shellcode conocidos

## Extensiones futuras

- [ ] iOS arm64 (firma de código + escenarios de jailbreak JIT)
- [ ] Cortex-M / Thumb
- [ ] RISC-V 64
