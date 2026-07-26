**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilateur dyncode](../README.fr.md)

# Conception des passes MIR — Principes et points de interpose

> Document compagnon de [ir-pass-design.md](../ir-pass-design/README.fr.md). La couche IR élimine les constructions qui produisent visiblement des relocalisations. La couche MIR sert de **filet de sécurité** après la sélection d'instructions et l'allocation de registres : elle supprime les pseudo-instructions/métadonnées introduites par le codegen et expose des points de interpose pour les passes d'obfuscation tierces.
>
> Implémentation : `neverc/lib/DynCode/MIR/MIRPrepPass.cpp` + `Pipeline.cpp`.

---

## 0. Pourquoi une couche MIR est nécessaire

La couche IR a déjà éliminé : GVs constants (Data2Text), mem*/str* (MemIntrin), __int128 (CompilerRt), extern libc (SyscallStub/WinPEB), globales mutables (ZeroReloc), computed-goto (IndirectBr).

Mais le backend LLVM introduit des constructions supplémentaires pendant le **IR → MIR lowering** :

1. **Pseudo-instructions CFI / EH_LABEL** → `__compact_unwind` / `.eh_frame` / `.pdata`.
2. **Stubs XRay / patchable**.
3. **Métadonnées sanitizer** : StackMap / PatchPoint / PseudoProbe.

Les interposes MIR permettent aussi l'**obfuscation au niveau instruction par des tiers** (substitution, renommage de registres).

---

## 1. Intégration avec LLVM

Enregistrement dans `Pipeline.cpp` :

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

## 2. MIRPrepPass intégré

Multiplateforme, responsabilité unique : scanne chaque `MachineBasicBlock` et supprime trois catégories de pseudo-instructions. Les vraies instructions machine ne sont **jamais touchées**.

### 2.1 Métadonnées de sections latérales

| Opcode | Source | Si non supprimé |
|--------|--------|-----------------|
| `CFI_INSTRUCTION` | frame-lowering de toutes les plateformes / `-g` | `.eh_frame` / `__compact_unwind` / `.pdata` non vide |
| `EH_LABEL` | Points EH / try-catch setjmp | Section latérale LSDA non vide |
| `GC_LABEL` / `ANNOTATION_LABEL` | Marqueurs GC / annotation | MCSymbol avec métadonnées relatives à la section |
| `STATEPOINT` / `STACKMAP` / `PATCHPOINT` | Stackmap GC / sandbox | Section latérale `.llvm_stackmaps` |
| `PSEUDO_PROBE` | `-fprofile-sample-use` | Section latérale `.pseudo_probe` |
| Famille `PATCHABLE_*` | Stubs XRay / Kcov | `.xray_instr_map` / `.xray_fn_idx` |
| `FENTRY_CALL` | Sonde d'entrée `-mfentry` | Appel extern `__fentry__` |
| `LOCAL_ESCAPE` | Frame-escape SEH Microsoft | Tire `_local_unwind2` / `__except_handler3` |
| `JUMP_TABLE_DEBUG_INFO` | Info de débogage de table de sauts | Entrée `.debug_rnglists` |

### 2.2 Windows SEH (correspondance de préfixe)

```cpp
if (Name.starts_with("SEH_")) eraseFromParent();
```

### 2.3 Table de réécriture d'instructions (`MIRRewritePatterns.def`)

Deux motifs enregistrés :
1. **`aarch64-cpi-fp-to-fmov-imm`** : `ADRP + LDR CPI` → `FMOV #imm8`.
2. **`x86-cpi-zero-fp-to-xorps`** : `movss/movsd +0.0` → `xorps xmm, xmm`.

---

## 3. Interposes d'obfuscation utilisateur

11 points de interpose : 6 IR + 3 MIR + 2 niveau octets.

Trois types de signature :

```cpp
using ObfuscationInterpose = std::function<void(
    llvm::ModulePassManager &, const DynCodeOptions &)>;
using MachineObfuscationInterpose = std::function<void(
    llvm::TargetPassConfig &, const DynCodeOptions &)>;
using BinaryObfuscationInterpose = std::function<void(
    llvm::SmallVectorImpl<uint8_t> &, const DynCodeOptions &)>;
```

- `RunBeforePreEmit` : MIR avec pseudos CFI/EH.
- `RunAfterPreEmit` : MIR nettoyé — le plus proche d'AsmPrinter.
- `RunPostExtract` : Flux d'octets pur.

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

## 4. Ordre d'exécution complet

```
[IR] → Prep → Passes → Data2Text → Inlining → Stackify → AllBlr
[Codegen] → RunBeforePreEmit → MIRPrepPass → RunAfterPreEmit
[AsmPrinter → .o → Extracteur → RunPostExtract → .bin]
```

## 5. Fondement de conception

| Problème | Couche IR ? | Couche MIR ? |
|----------|-------------|--------------|
| Élimination de GV constante | Oui (Data2Text) | Non nécessaire |
| Élimination de libc extern | Oui (SyscallStub / WinPEB) | Non nécessaire |
| Stack-ification de globales mutables | Oui (ZeroReloc) | Non nécessaire |
| Computed goto | Oui (IndirectBr) | Non nécessaire |
| Pseudo-instructions CFI | Non (générées par backend) | Oui (scanner et effacer) |
| Stubs XRay | Non (générées par backend) | Oui (scanner et effacer) |
| Obfuscation au niveau instruction | Non (IR manque de registres physiques) | Oui (a de vrais registres/MI) |
| Renommage de registres | Non | Oui |
| Expansion de constantes peephole | Partielle | Oui (plus propre) |

## 6. Guide d'extension

- **Ajout suppression pseudo** : Un case dans `isDynCodeStripPseudo`.
- **Ajout réécriture MIR** : Écrire `tryRewriteXxx` + fichiers `.def`.
- **Tiers** : [API Plugin](../../plugin-api/README.fr.md) (interposes `NEVERC_INTERPOSE_SC_*`).

## 7. Relation avec DynCodeExtractor

| Couche | Moment | Capacité |
|--------|--------|----------|
| MIR | Avant AsmPrinter | Insérer/supprimer MachineInstr |
| Extracteur | Après AsmPrinter | Modifier octets ou rejeter uniquement |

**Principe** : Corriger d'abord en MIR ; ne recourir à l'extracteur que pour les patches au niveau octets.


## 8. Correction active vs passthrough de diagnostic

1. **Correction active** : modifie directement les MachineInstrs (supprime les pseudos, réécrit CPI→FMOV). Faible coût, indépendant de la cible.
2. **Passthrough de diagnostic** : détecte les problèmes, signale les erreurs au niveau MIR, laisse l'extracteur rejeter au niveau octet. Utilisé pour les constructions où la réécriture MIR nécessiterait du code spécifique à la cible étendu (p. ex., remplacer `adrp+ldr CPI` par des séquences `mov/movk`).
3. **Repli de l'extracteur** : échec dur sur tout reloc externe restant ou sections de données non vides.

Ce principe garde la couche MIR presque immunisée contre les mises à niveau de l'ISA du backend. La seule maintenance est : « y a-t-il un nouveau pseudo dans TargetOpcode ? Si dyncode n'en a pas besoin, ajoutez un case. »
