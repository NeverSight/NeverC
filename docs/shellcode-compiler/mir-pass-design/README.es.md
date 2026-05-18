**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilador de shellcode](../README.es.md)

# Diseño de pasadas MIR — Principios y puntos de hook

> Documento acompañante de [ir-pass-design.md](../ir-pass-design/README.es.md). La capa IR elimina construcciones que visiblemente producen relocalizaciones. La capa MIR sirve como **red de captura** después de la selección de instrucciones y asignación de registros: elimina pseudo-instrucciones/metadatos introducidos por codegen y expone puntos de hook para pasadas de ofuscación de terceros.
>
> Implementación: `neverc/lib/Shellcode/MIR/MIRPrepPass.cpp` + `Pipeline.cpp`.

---

## 0. Por qué se necesita una capa MIR

La capa IR ya eliminó: GVs constantes (Data2Text), mem*/str* (MemIntrin), __int128 (CompilerRt), extern libc (SyscallStub/WinPEB), globales mutables (ZeroReloc), computed-goto (IndirectBr).

Pero el backend LLVM introduce construcciones adicionales durante **IR → MIR lowering**:

1. **Pseudo-instrucciones CFI / EH_LABEL** → `__compact_unwind` / `.eh_frame` / `.pdata`.
2. **Stubs XRay / patchable** → `.xray_instr_map`.
3. **Metadatos sanitizer**: StackMap / PatchPoint / PseudoProbe.

Los hooks MIR también habilitan **ofuscación a nivel de instrucción de terceros** (sustitución, renombramiento de registros).

---

## 1. Integración con LLVM

Registro en `Pipeline.cpp` del callback global de `TargetPassConfig`:

```cpp
ListRegisterTargetPassConfigCallbacks.push_back(
    [](TargetPassConfig &TPC) {
      const ShellcodeOptions &Opts = currentShellcodeOptionsStorage();
      const ObfuscationHooks &H = getShellcodeObfuscationHooks();
      runMIRHook(H.RunBeforePreEmit, TPC, Opts);
      TPC.addExternalPass(createShellcodeMIRPrepPass(Opts));
      runMIRHook(H.RunAfterPreEmit, TPC, Opts);
    });
```

---

## 2. MIRPrepPass integrado

Multiplataforma, responsabilidad única: Escanea cada `MachineBasicBlock` y elimina tres categorías de pseudo-instrucciones. Las instrucciones máquina reales **nunca se tocan**.

### 2.1 Metadatos de sección lateral

| Opcode | Fuente | Impacto si no se elimina |
|--------|--------|--------------------------|
| `CFI_INSTRUCTION` | Frame lowering / `-g` | `.eh_frame` / `.pdata` no vacío |
| `EH_LABEL` | EH / try-catch | LSDA no vacío |
| `STATEPOINT` / `STACKMAP` / `PATCHPOINT` | GC / sandbox | `.llvm_stackmaps` |
| `PATCHABLE_*` | XRay / Kcov | `.xray_instr_map` |
| `FENTRY_CALL` | `-mfentry` | extern `__fentry__` |

### 2.2 Windows SEH (coincidencia de prefijo)

```cpp
if (Name.starts_with("SEH_")) eraseFromParent();
```

### 2.3 Tabla de reescritura de instrucciones (`MIRRewritePatterns.def`)

Dos patrones registrados:
1. **`aarch64-cpi-fp-to-fmov-imm`**: `ADRP + LDR CPI` → `FMOV #imm8`.
2. **`x86-cpi-zero-fp-to-xorps`**: `movss/movsd +0.0` → `xorps xmm, xmm`.

---

## 3. Hooks de ofuscación del usuario

11 puntos de hook: 6 IR + 3 MIR + 2 nivel de bytes.

- `RunBeforePreEmit`: MIR con pseudos CFI/EH.
- `RunAfterPreEmit`: MIR limpio — más cercano a AsmPrinter.
- `RunPostExtract`: Flujo de bytes puro.

---

## 4. Orden de ejecución completo

```
[IR] → RunBeforePrep → ZeroReloc → RunAfterPrep → Passes → Data2Text #1 → RunBeforeInlining
→ (Optimizaciones LLVM) → RunAfterInlining → Data2Text #2 → ZeroReloc(Stackify) → AllBlr
[Codegen] → RunBeforePreEmit → MIRPrepPass → RunAfterPreEmit
[AsmPrinter → .o → Extractor → RunPostExtract → .bin]
```

## 5. Fundamento del diseño

| Problema | ¿IR? | ¿MIR? |
|----------|------|-------|
| Eliminación GV constante | Sí | No necesario |
| Pseudo-instrucciones CFI | No (backend) | Sí |
| Ofuscación nivel instrucción | No | Sí |

## 6. Guía de extensión

- **Agregar eliminación de pseudo**: Un case en `isShellcodeStripPseudo`.
- **Agregar reescritura MIR**: Escribir `tryRewriteXxx` + `MIRRewritePatterns.def` + `MIRRewriteOpcodes.def`.
- **Terceros**: `setShellcodeObfuscationHooks()`.

## 7. Relación con ShellcodeExtractor

| Capa | Momento | Capacidad |
|------|---------|-----------|
| MIR | Antes de AsmPrinter | Insertar/eliminar MachineInstr |
| Extractor | Después de AsmPrinter | Solo modificar bytes o rechazar |

**Principio**: Corregir en MIR primero; solo recurrir al extractor para parches a nivel de bytes.
