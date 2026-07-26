**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilador de dyncode](../README.es.md)

# Diseño de pasadas MIR — Principios y puntos de interpose

> Documento acompañante de [ir-pass-design.md](../ir-pass-design/README.es.md). La capa IR elimina construcciones que visiblemente producen relocalizaciones. La capa MIR sirve como **red de captura** después de la selección de instrucciones y asignación de registros: elimina pseudo-instrucciones/metadatos introducidos por codegen y expone puntos de interpose para pasadas de ofuscación de terceros.
>
> Implementación: `neverc/lib/DynCode/MIR/MIRPrepPass.cpp` + `Pipeline.cpp`.
> Interfaz de interpose: [`neverc/include/neverc/DynCode/Pipeline/Pipeline.h`].

---

## 0. Por qué se necesita una capa MIR

La capa IR ya eliminó: GVs constantes (Data2Text), mem*/str* (MemIntrin), __int128 (CompilerRt), extern libc (SyscallStub/WinPEB), globales mutables (ZeroReloc), computed-goto (IndirectBr).

Pero el backend LLVM introduce construcciones adicionales durante **IR → MIR lowering**:

1. **Pseudo-instrucciones CFI / EH_LABEL** → `__compact_unwind` / `.eh_frame` / `.pdata`.
2. **Stubs XRay / patchable** → `.xray_instr_map`.
3. **Metadatos sanitizer**: StackMap / PatchPoint / PseudoProbe.

Los interposes MIR también habilitan **ofuscación a nivel de instrucción de terceros** (sustitución, renombramiento de registros).

---

## 1. Integración con LLVM

Registro en `Pipeline.cpp` del callback global de `TargetPassConfig`:

```cpp
ListRegisterTargetPassConfigCallbacks.push_back(
    [](TargetPassConfig &TPC) {
      const DynCodeOptions &Opts = currentDynCodeOptionsStorage();
      const ObfuscationInterposes &H = getDynCodeObfuscationInterposes();
      runMIRInterpose(H.RunBeforePreEmit, TPC, Opts);
      TPC.addExternalPass(createDynCodeMIRPrepPass(Opts));
      runMIRInterpose(H.RunAfterPreEmit, TPC, Opts);
    });
```

---

## 2. MIRPrepPass integrado

Multiplataforma, responsabilidad única: Escanea cada `MachineBasicBlock` y elimina tres categorías de pseudo-instrucciones. Las instrucciones máquina reales **nunca se tocan**.

### 2.1 Metadatos de sección lateral

| Opcode | Origen | Si no se elimina |
|--------|--------|------------------|
| `CFI_INSTRUCTION` | frame-lowering de todas las plataformas / `-g` | `.eh_frame` / `__compact_unwind` / `.pdata` no vacío |
| `EH_LABEL` | Puntos EH / try-catch setjmp | Sección lateral LSDA no vacía |
| `GC_LABEL` / `ANNOTATION_LABEL` | Marcadores GC / annotation | MCSymbol con metadatos relativos a sección |
| `STATEPOINT` / `STACKMAP` / `PATCHPOINT` | Stackmap GC / sandbox | Sección lateral `.llvm_stackmaps` |
| `PSEUDO_PROBE` | `-fprofile-sample-use` | Sección lateral `.pseudo_probe` |
| Familia `PATCHABLE_*` | Stubs XRay / Kcov | `.xray_instr_map` / `.xray_fn_idx` |
| `FENTRY_CALL` | Sonda de entrada `-mfentry` | Llamada extern `__fentry__` |
| `LOCAL_ESCAPE` | Frame-escape SEH de Microsoft | Arrastra `_local_unwind2` / `__except_handler3` |
| `JUMP_TABLE_DEBUG_INFO` | Info de depuración de tabla de saltos | Entrada `.debug_rnglists` |

### 2.2 Windows SEH (coincidencia de prefijo)

```cpp
if (Name.starts_with("SEH_")) eraseFromParent();
```

### 2.3 Tabla de reescritura de instrucciones (`MIRRewritePatterns.def`)

Dos patrones registrados:
1. **`aarch64-cpi-fp-to-fmov-imm`**: `ADRP + LDR CPI` → `FMOV #imm8`.
2. **`x86-cpi-zero-fp-to-xorps`**: `movss/movsd +0.0` → `xorps xmm, xmm`.

---

## 3. Interposes de ofuscación del usuario

11 puntos de interpose: 6 IR + 3 MIR + 2 nivel de bytes.

Tres tipos de firma:

```cpp
using ObfuscationInterpose = std::function<void(
    llvm::ModulePassManager &, const DynCodeOptions &)>;
using MachineObfuscationInterpose = std::function<void(
    llvm::TargetPassConfig &, const DynCodeOptions &)>;
using BinaryObfuscationInterpose = std::function<void(
    llvm::SmallVectorImpl<uint8_t> &, const DynCodeOptions &)>;
```

- `RunBeforePreEmit`: MIR con pseudos CFI/EH.
- `RunAfterPreEmit`: MIR limpio — más cercano a AsmPrinter.
- `RunPostExtract`: Flujo de bytes puro.

```cpp
__attribute__((constructor))
static void myMirObfInit() {
  auto H = neverc::dyncode::getDynCodeObfuscationInterposes();
  H.RunAfterPreEmit = [](llvm::TargetPassConfig &TPC,
                         const neverc::dyncode::DynCodeOptions &Opts) {
    TPC.addExternalPass(new MyInstructionSubstitutionPass(Opts.MirObfuscateSpec));
  };
  // Register via Plugin API: NEVERC_INTERPOSE_SC_BEFORE_PREEMIT / AFTER_PREEMIT / AFTER_FINAL_MIR
}
```

---

## 4. Orden de ejecución completo

```
[IR] → RunBeforePrep → ZeroReloc → RunAfterPrep → Passes → Data2Text #1 → RunBeforeInlining
→ (Optimizaciones LLVM) → RunAfterInlining → Data2Text #2 → ZeroReloc(Stackify) → AllBlr
[Codegen] → RunBeforePreEmit → MIRPrepPass → RunAfterPreEmit
[AsmPrinter → .o → Extractor → RunPostExtract → .bin]
```

## 5. Fundamento del diseño

| Problema | ¿Capa IR? | ¿Capa MIR? |
|----------|-----------|------------|
| Eliminación de GV constante | Sí (Data2Text) | No necesario |
| Eliminación de libc extern | Sí (SyscallStub / WinPEB) | No necesario |
| Stack-ificación de globales mutables | Sí (ZeroReloc) | No necesario |
| Computed goto | Sí (IndirectBr) | No necesario |
| Pseudo-instrucciones CFI | No (generadas por backend) | Sí (escanear y borrar) |
| Stubs XRay | No (generadas por backend) | Sí (escanear y borrar) |
| Ofuscación a nivel de instrucción | No (IR carece de registros físicos) | Sí (tiene registros reales/MI) |
| Renombrado de registros | No | Sí |
| Expansión de constantes peephole | Parcial | Sí (más limpio) |

## 6. Guía de extensión

- **Agregar eliminación de pseudo**: Un case en `isDynCodeStripPseudo`.
- **Agregar reescritura MIR**: Escribir `tryRewriteXxx` + `MIRRewritePatterns.def` + `MIRRewriteOpcodes.def`.
- **Terceros**: [API de Plugins](../../plugin-api/README.es.md) (interposes `NEVERC_INTERPOSE_SC_*`).

## 7. Relación con DynCodeExtractor

| Capa | Momento | Capacidad |
|------|---------|-----------|
| MIR | Antes de AsmPrinter | Insertar/eliminar MachineInstr |
| Extractor | Después de AsmPrinter | Solo modificar bytes o rechazar |

**Principio**: Corregir en MIR primero; solo recurrir al extractor para parches a nivel de bytes.


## 8. Corrección activa vs paso de diagnóstico

1. **Corrección activa**: modifica directamente los MachineInstrs (elimina pseudos, reescribe CPI→FMOV). Bajo costo, independiente del objetivo.
2. **Paso de diagnóstico**: detecta problemas, reporta errores a nivel MIR, deja que el extractor rechace a nivel de byte. Se usa para construcciones donde la reescritura MIR requeriría código específico del objetivo extenso (p. ej., reemplazar `adrp+ldr CPI` con secuencias `mov/movk`).
3. **Reserva del extractor**: falla dura ante cualquier reloc externo restante o secciones de datos no vacías.

Este principio mantiene la capa MIR casi inmune a las actualizaciones de ISA del backend. El único mantenimiento es: "¿hay un nuevo pseudo en TargetOpcode? Si dyncode no lo necesita, agrega un case."

[`neverc/include/neverc/DynCode/Pipeline/Pipeline.h`]: ../../../neverc/include/neverc/DynCode/Pipeline/Pipeline.h
