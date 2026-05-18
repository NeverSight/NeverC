**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilateur shellcode](../README.fr.md)

# Support shellcode mode noyau (Ring-0)

`-fshellcode` couvrait initialement uniquement les charges ring-3. Les charges ring-0 ne peuvent réutiliser l'ABI ring-3 : pas de TEB/PEB, instructions syscall = traps utilisateur→noyau, x86_64 nécessite modèle de code différent et désactivation de la zone rouge.

## 1. `-mshellcode-context={user,kernel}`
- **User** (défaut) : Pipeline PEB/syscall.
- **Kernel** : SyscallStub/WinPEB désactivés, flags noyau injectés, KernelImportPass activé.

## 2–3. Champs TargetDesc et flags driver
`Level`, `KernelImport`, `KernelInjectFlags`. x86_64 : `-mno-red-zone -mcmodel=kernel -mno-sse` ; AArch64 : `-mgeneral-regs-only`.

## 4. KernelImportPass
Réécriture automatique des appels extern non résolus en appels indirects via résolveur. Hash FNV-1a 64-bit. Défense trois couches.

## 5–7. Noyau Android, en-têtes, écriture de code Ring-0
`<neverc/kernel.h>` fournit `neverc_kern_resolve_t` et `neverc_kern_hash()`. Charges calcul pur ou basées sur résolveur.

## 8. Feuille de route
Changement contexte noyau, réécriture résolveur, deux types de charges — tout terminé. En-têtes SDK noyau planifiés.
