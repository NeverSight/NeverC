**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilateur shellcode](../README.fr.md)

# Feuille de route

Ce document suit les fonctionnalités planifiées, en cours ou différées par conception.

## État actuel

Le pipeline shellcode de NeverC couvre :

- Pipeline LLVM IR complet avec 11+ passes dédiées
- Extracteurs COFF / ELF / Mach-O
- Résolution d'importation Win32 PEB-walk (hash ROR-13, 6 buckets DLL)
- Abaissement direct des syscalls (Darwin `svc #0x80`, Linux `svc #0` / `syscall`)
- Support du mode noyau (Windows, Linux)
- Audit des octets interdits avec profils configurables
- SDK de plugins pour réécriteurs d'octets interdits et encodeurs de jeux de caractères
- Contraintes de taille / alignement / remplissage (`-fshellcode-max-length=`, `-fshellcode-align=`, `-fshellcode-pad=`)
- 11 hooks d'obfuscation sur les couches IR, MIR et flux d'octets

## Terminé (2026-04)

1. **Contraintes de taille / alignement / remplissage** — Intégré. `-fshellcode-max-length=`, `-fshellcode-align=`, `-fshellcode-pad=` s'exécutent à la fin de `finalizeShellcodeBytes`. Le driver rejette les configurations contradictoires (ex. octet de remplissage dans l'ensemble des octets interdits, ou remplissage sans align/max-length).

2. **Interface de réécriteur d'octets interdits** — Squelette intégré, aucune stratégie intégrée. `Plugin.h::registerBadByteRewriteStrategy` expose le SDK. `-fshellcode-bad-byte-rewrite` / `-fno-...` contrôle si la chaîne de finalisation invoque les réécriteurs. La désactivation revient au mode audit seul. Les bibliothèques en aval enregistrent des stratégies basées sur Capstone ou personnalisées.

3. **Interface d'encodeur de jeux de caractères** — Squelette intégré, aucun jeu intégré. `Plugin.h::registerCharsetEncoder` expose un tuple `(name, Encode, Stub, IsCharsetMember)`. Lorsque `-fshellcode-charset=<name>` est défini, la phase de finalisation remplace `.text` par `Stub(target) || Encode(text, target)` et valide tous les octets de sortie contre le jeu de caractères. Les encodeurs imprimables / alphanumériques / personnalisés sont enregistrés par les bibliothèques en aval.

## Planifié — Couche plugin (via hooks)

Ces capacités sont **intentionnellement non intégrées**. Elles appartiennent à la couche stratégie/obfuscation et sont conçues pour être fournies par des plugins tiers via les interfaces de hooks et de plugins.

| Fonctionnalité | Point de hook | Notes |
|---------------|-------------|-------|
| Anti-désassemblage | `RunBeforePreEmit` / `RunAfterPreEmit` / `RunAfterFinalMIR` | Interférence de préfixe d'instruction, réordonnancement de sauts, insertion de déchets |
| Polymorphisme | `RunAfterFinalMIR` / `RunPostExtract` | Variation de sortie basée sur une graine par compilation |
| Encodeur par étapes (XOR / RC4 / auto-déchiffrement) | `RunPostExtract` / `RunPostFinalize` | Génération de stub à la compilation + chiffrement de la charge utile |
| Syscalls indirects (Halos / Tartarus / Recycled Gate) | Plugin de niveau IR ou `RunPostExtract` | Scan de gadgets ntdll à l'exécution |
| Masque de sommeil / usurpation de pile d'appels | Plugin de passe IR | Motifs Ekko / FOLIAGE / Cronos |
| Correction ETW / AMSI | Plugin de passe IR | Séquences de correction à l'exécution |
| Module stomping / unhooking | Plugin de passe IR | Motifs de manipulation mémoire |

## Résumé des hooks de plugins

11 hooks en trois couches :

**Couche IR (6 hooks, reçoivent `ModulePassManager &`)** :
- `RunBeforePrep` — Avant toute passe shellcode
- `RunAfterPrep` — Après l'unification du linkage
- `RunBeforeInlining` — Dernière chance avant AlwaysInliner
- `RunAfterInlining` — IR entièrement aplati en une fonction
- `RunAfterStackify` — Forme IR finale avant codegen
- `RunAfterFinalIR` — Après `AllBlrPass`, le tout dernier hook IR

**Couche MIR (3 hooks, reçoivent `TargetPassConfig &`)** :
- `RunBeforePreEmit` — Registres alloués, pseudos CFI/EH encore présents
- `RunAfterPreEmit` — Après le nettoyage de `MIRPrepPass`, le plus proche des octets finaux
- `RunAfterFinalMIR` — Après LLVM `addPreEmitPass2()`, juste avant AsmPrinter

**Couche flux d'octets (2 hooks, reçoivent `SmallVectorImpl<uint8_t> &`)** :
- `RunPostExtract` — Pré-finalisation, encore traité par réécriteur/encodeur/audit/dimensionnement
- `RunPostFinalize` — Post-finalisation, dernier moment avant l'écriture sur disque ; NeverC n'effectue plus aucun audit

## Pipeline de finalisation

Chaque extracteur appelle `finalizeShellcodeBytes` avant d'écrire le `.bin` :

```
applyPostExtractObfuscationHook       (ObfuscationHooks::RunPostExtract)
        |
runBadByteRewriters                   (Plugin.h::registerBadByteRewriteStrategy)
        |
runCharsetEncoder                     (Plugin.h::registerCharsetEncoder)
        |
auditFinalBadBytes                    (audit dur intégré)
        |
applyShellcodeSizing                  (-fshellcode-align/-max-length/-pad)
```

Utilisation et exemples de code dans la [documentation Plugin API](../../plugin-api/README.fr.md).

## Non planifié

- **Frontend multi-langage** — NeverC n'accepte que son propre frontend C23. Le pipeline IR est découplé du frontend, mais l'acceptation de bitcode externe (ex. `rustc` ou `zig`) n'est pas un objectif du projet.
