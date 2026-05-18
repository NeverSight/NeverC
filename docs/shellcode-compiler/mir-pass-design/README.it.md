**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilatore shellcode](../README.it.md)

# Progettazione dei pass MIR — Principi e punti hook

> Documento compagno di [ir-pass-design.md](../ir-pass-design/README.it.md). Il livello IR elimina costrutti che producono visibilmente rilocazioni. Il livello MIR serve come **rete di sicurezza** dopo selezione istruzioni e allocazione registri: rimuove pseudo-istruzioni/metadati introdotti dal codegen ed espone punti hook per pass di offuscamento terzi.

---

## 0. Perché serve un livello MIR

Il backend LLVM introduce costrutti aggiuntivi durante **IR → MIR lowering**: pseudo CFI/EH_LABEL, stub XRay/patchable, metadati sanitizer, fixup MC. I hook MIR abilitano l'**offuscamento a livello istruzione** (sostituzione, rinomina registri).

## 1. Integrazione con LLVM

Registrazione in `Pipeline.cpp` tramite callback globale `TargetPassConfig`.

## 2. MIRPrepPass integrato

Scansiona ogni `MachineBasicBlock` ed elimina tre categorie di pseudo: metadati sezione laterale (`CFI_INSTRUCTION`, `EH_LABEL`, `STACKMAP`, ecc.), SEH Windows (match prefisso `SEH_`), e riscritture istruzione tabellari (`MIRRewritePatterns.def`).

Due pattern registrati:
1. **`aarch64-cpi-fp-to-fmov-imm`**: `ADRP + LDR CPI` → `FMOV #imm8`.
2. **`x86-cpi-zero-fp-to-xorps`**: `movss/movsd +0.0` → `xorps xmm, xmm`.

## 3. Hook di offuscamento utente

11 punti hook: 6 IR + 3 MIR + 2 livello byte.

- `RunBeforePreEmit`: MIR con pseudo CFI/EH.
- `RunAfterPreEmit`: MIR pulito — più vicino ad AsmPrinter.
- `RunPostExtract`: Flusso byte puro.

## 4. Ordine di esecuzione completo

```
[IR] → Prep → Pass → Data2Text → Inlining → Stackify → AllBlr
[Codegen] → RunBeforePreEmit → MIRPrepPass → RunAfterPreEmit
[AsmPrinter → .o → Estrattore → RunPostExtract → .bin]
```

## 5. Fondamento progettuale

| Problema | IR? | MIR? |
|----------|-----|------|
| Eliminazione GV costante | Sì | Non necessario |
| Pseudo CFI | No (backend) | Sì |
| Offuscamento livello istruzione | No | Sì |

## 6. Guida all'estensione

- **Aggiunta rimozione pseudo**: Un case in `isShellcodeStripPseudo`.
- **Aggiunta riscrittura MIR**: Scrivere `tryRewriteXxx` + file `.def`.
- **Terze parti**: `setShellcodeObfuscationHooks()`.

## 7. Relazione con ShellcodeExtractor

MIR corregge prima (può manipolare istruzioni); l'estrattore è l'ultima risorsa per patch a livello byte.
