**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilador de shellcode](../README.es.md)

# Hoja de ruta

Este documento rastrea funcionalidades planificadas, en progreso o diferidas por diseño.

## Estado actual

El pipeline de shellcode de NeverC cubre:

- Pipeline completo de LLVM IR con 11+ pasadas dedicadas
- Extractores COFF / ELF / Mach-O
- Resolución de importaciones Win32 PEB-walk (hash ROR-13, 6 buckets de DLL)
- Reducción directa de syscalls (Darwin `svc #0x80`, Linux `svc #0` / `syscall`)
- Soporte de modo kernel (Windows, Linux)
- Auditoría de bytes prohibidos con perfiles configurables
- SDK de plugins para reescritores de bytes prohibidos y codificadores de conjunto de caracteres
- Restricciones de tamaño / alineación / relleno (`-fshellcode-max-length=`, `-fshellcode-align=`, `-fshellcode-pad=`)
- 11 hooks de ofuscación en las capas IR, MIR y flujo de bytes

## Completado (2026-04)

1. **Restricciones de tamaño / alineación / relleno** — Integrado. `-fshellcode-max-length=`, `-fshellcode-align=`, `-fshellcode-pad=` se ejecutan al final de `finalizeShellcodeBytes`. El driver rechaza configuraciones contradictorias (ej. byte de relleno en el conjunto de bytes prohibidos, o relleno sin align/max-length).

2. **Interfaz de reescritor de bytes prohibidos** — Esqueleto integrado, sin estrategias incorporadas. `Plugin.h::registerBadByteRewriteStrategy` expone el SDK. `-fshellcode-bad-byte-rewrite` / `-fno-...` controla si la cadena de finalización invoca reescritores. Desactivar retrocede al modo solo auditoría. Las bibliotecas downstream registran estrategias basadas en Capstone o personalizadas.

3. **Interfaz de codificador de conjunto de caracteres** — Esqueleto integrado, sin conjuntos incorporados. `Plugin.h::registerCharsetEncoder` expone una tupla `(name, Encode, Stub, IsCharsetMember)`. Cuando se establece `-fshellcode-charset=<name>`, la fase de finalización reemplaza `.text` con `Stub(target) || Encode(text, target)` y valida todos los bytes de salida contra el conjunto de caracteres. Los codificadores imprimibles / alfanuméricos / personalizados son registrados por bibliotecas downstream.

## Planificado — Capa de plugins (vía hooks)

Estas capacidades **no están integradas intencionalmente**. Pertenecen a la capa de estrategia/ofuscación y están diseñadas para ser proporcionadas por plugins de terceros a través de las interfaces de hooks y plugins.

| Funcionalidad | Punto de hook | Notas |
|---------------|-------------|-------|
| Anti-desensamblaje | `RunBeforePreEmit` / `RunAfterPreEmit` / `RunAfterFinalMIR` | Interferencia de prefijo de instrucción, reordenamiento de saltos, inserción de basura |
| Polimorfismo | `RunAfterFinalMIR` / `RunPostExtract` | Variación de salida basada en semilla por compilación |
| Codificador por etapas (XOR / RC4 / autodescifrado) | `RunPostExtract` / `RunPostFinalize` | Generación de stub en tiempo de compilación + cifrado de payload |
| Syscalls indirectos (Halos / Tartarus / Recycled Gate) | Plugin de nivel IR o `RunPostExtract` | Escaneo de gadgets ntdll en tiempo de ejecución |
| Máscara de sleep / suplantación de pila de llamadas | Plugin de pasada IR | Patrones Ekko / FOLIAGE / Cronos |
| Parcheo ETW / AMSI | Plugin de pasada IR | Secuencias de parche en tiempo de ejecución |
| Module stomping / unhooking | Plugin de pasada IR | Patrones de manipulación de memoria |

## Resumen de hooks de plugins

11 hooks en tres capas:

**Capa IR (6 hooks, reciben `ModulePassManager &`)**:
- `RunBeforePrep` — Antes de cualquier pasada de shellcode
- `RunAfterPrep` — Después de la unificación de linkage
- `RunBeforeInlining` — Última oportunidad antes de AlwaysInliner
- `RunAfterInlining` — IR completamente aplanado en una función
- `RunAfterStackify` — Forma final de IR antes de codegen
- `RunAfterFinalIR` — Después de `AllBlrPass`, el último hook de IR absoluto

**Capa MIR (3 hooks, reciben `TargetPassConfig &`)**:
- `RunBeforePreEmit` — Registros asignados, pseudos CFI/EH aún presentes
- `RunAfterPreEmit` — Después de la limpieza de `MIRPrepPass`, más cercano a los bytes finales
- `RunAfterFinalMIR` — Después de LLVM `addPreEmitPass2()`, justo antes de AsmPrinter

**Capa de flujo de bytes (2 hooks, reciben `SmallVectorImpl<uint8_t> &`)**:
- `RunPostExtract` — Pre-finalización, aún procesado por reescritor/codificador/auditoría/dimensionamiento
- `RunPostFinalize` — Post-finalización, último momento antes de escribir en disco; NeverC no realiza más auditorías

## Pipeline de finalización

Cada extractor llama a `finalizeShellcodeBytes` antes de escribir el `.bin`:

```
applyPostExtractObfuscationHook       (ObfuscationHooks::RunPostExtract)
        |
runBadByteRewriters                   (Plugin.h::registerBadByteRewriteStrategy)
        |
runCharsetEncoder                     (Plugin.h::registerCharsetEncoder)
        |
auditFinalBadBytes                    (auditoría dura integrada)
        |
applyShellcodeSizing                  (-fshellcode-align/-max-length/-pad)
```

Uso y ejemplos de código en [plugin-interface.md](../plugin-interface/README.es.md).

## No planificado

- **Frontend multi-lenguaje** — NeverC solo acepta su propio frontend C23. El pipeline IR está desacoplado del frontend, pero aceptar bitcode externo (ej. de `rustc` o `zig`) no es un objetivo del proyecto.
