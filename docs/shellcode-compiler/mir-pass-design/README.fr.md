**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilateur shellcode](../README.fr.md)

# Conception des passes MIR — Principes et points de hook

> Document compagnon de [ir-pass-design.md](../ir-pass-design/README.fr.md). La couche IR élimine les constructions qui produisent visiblement des relocalisations. La couche MIR sert de **filet de sécurité** après la sélection d'instructions et l'allocation de registres : elle supprime les pseudo-instructions/métadonnées introduites par le codegen et expose des points de hook pour les passes d'obfuscation tierces.
>
> Implémentation : `neverc/lib/Shellcode/MIR/MIRPrepPass.cpp` + `Pipeline.cpp`.

---

## 0. Pourquoi une couche MIR est nécessaire

La couche IR a déjà éliminé : GVs constants (Data2Text), mem*/str* (MemIntrin), __int128 (CompilerRt), extern libc (SyscallStub/WinPEB), globales mutables (ZeroReloc), computed-goto (IndirectBr).

Mais le backend LLVM introduit des constructions supplémentaires pendant le **IR → MIR lowering** :

1. **Pseudo-instructions CFI / EH_LABEL** → `__compact_unwind` / `.eh_frame` / `.pdata`.
2. **Stubs XRay / patchable**.
3. **Métadonnées sanitizer** : StackMap / PatchPoint / PseudoProbe.

Les hooks MIR permettent aussi l'**obfuscation au niveau instruction par des tiers** (substitution, renommage de registres).

---

## 1. Intégration avec LLVM

Enregistrement dans `Pipeline.cpp` :

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

## 2. MIRPrepPass intégré

Multiplateforme, responsabilité unique : scanne chaque `MachineBasicBlock` et supprime trois catégories de pseudo-instructions. Les vraies instructions machine ne sont **jamais touchées**.

### 2.1 Métadonnées de sections latérales

| Opcode | Source | Impact si non supprimé |
|--------|--------|----------------------|
| `CFI_INSTRUCTION` | Frame lowering / `-g` | `.eh_frame` / `.pdata` non vide |
| `EH_LABEL` | EH / try-catch | LSDA non vide |
| `STATEPOINT` / `STACKMAP` / `PATCHPOINT` | GC / sandbox | `.llvm_stackmaps` |
| `PATCHABLE_*` | XRay / Kcov | `.xray_instr_map` |
| `FENTRY_CALL` | `-mfentry` | extern `__fentry__` |

### 2.2 Windows SEH (correspondance de préfixe)

```cpp
if (Name.starts_with("SEH_")) eraseFromParent();
```

### 2.3 Table de réécriture d'instructions (`MIRRewritePatterns.def`)

Deux motifs enregistrés :
1. **`aarch64-cpi-fp-to-fmov-imm`** : `ADRP + LDR CPI` → `FMOV #imm8`.
2. **`x86-cpi-zero-fp-to-xorps`** : `movss/movsd +0.0` → `xorps xmm, xmm`.

---

## 3. Hooks d'obfuscation utilisateur

11 points de hook : 6 IR + 3 MIR + 2 niveau octets.

- `RunBeforePreEmit` : MIR avec pseudos CFI/EH.
- `RunAfterPreEmit` : MIR nettoyé — le plus proche d'AsmPrinter.
- `RunPostExtract` : Flux d'octets pur.

---

## 4. Ordre d'exécution complet

```
[IR] → Prep → Passes → Data2Text → Inlining → Stackify → AllBlr
[Codegen] → RunBeforePreEmit → MIRPrepPass → RunAfterPreEmit
[AsmPrinter → .o → Extracteur → RunPostExtract → .bin]
```

## 5. Fondement de conception

| Problème | IR ? | MIR ? |
|----------|------|-------|
| Élimination GV constant | Oui | Pas nécessaire |
| Pseudo-instructions CFI | Non (backend) | Oui |
| Obfuscation niveau instruction | Non | Oui |

## 6. Guide d'extension

- **Ajout suppression pseudo** : Un case dans `isShellcodeStripPseudo`.
- **Ajout réécriture MIR** : Écrire `tryRewriteXxx` + fichiers `.def`.
- **Tiers** : `setShellcodeObfuscationHooks()`.

## 7. Relation avec ShellcodeExtractor

| Couche | Moment | Capacité |
|--------|--------|----------|
| MIR | Avant AsmPrinter | Insérer/supprimer MachineInstr |
| Extracteur | Après AsmPrinter | Modifier octets ou rejeter uniquement |

**Principe** : Corriger d'abord en MIR ; ne recourir à l'extracteur que pour les patches au niveau octets.
