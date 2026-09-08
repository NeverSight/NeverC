**Langues**: [English](../../dyncode-compiler/kernel-mode-dyncode.md) | [简体中文](../../zh-CN/dyncode-compiler/kernel-mode-dyncode.md) | [繁體中文](../../zh-TW/dyncode-compiler/kernel-mode-dyncode.md) | [日本語](../../ja/dyncode-compiler/kernel-mode-dyncode.md) | [한국어](../../ko/dyncode-compiler/kernel-mode-dyncode.md) | [Français](kernel-mode-dyncode.md) | [Deutsch](../../de/dyncode-compiler/kernel-mode-dyncode.md) | [Español](../../es/dyncode-compiler/kernel-mode-dyncode.md) | [Italiano](../../it/dyncode-compiler/kernel-mode-dyncode.md) | [Русский](../../ru/dyncode-compiler/kernel-mode-dyncode.md) | [العربية](../../ar/dyncode-compiler/kernel-mode-dyncode.md)

[← Compilateur dyncode](README.md)

# Support dyncode mode noyau (Ring-0)

`-fdyncode` couvrait initialement uniquement les charges ring-3. Les charges ring-0 ne peuvent réutiliser l'ABI ring-3 : pas de TEB/PEB, instructions syscall = traps utilisateur→noyau, x86_64 nécessite modèle de code différent et désactivation de la zone rouge.

## 1. `-mdyncode-context={user,kernel}`
- **User** (défaut) : Pipeline PEB/syscall.
- **Kernel** : SyscallStub/WinPEB désactivés, flags noyau injectés, KernelImportPass activé.

## 2–3. Champs TargetDesc et flags driver
`Level`, `KernelImport`, `KernelInjectFlags`. x86_64 : `-mno-red-zone -mcmodel=kernel -mno-sse` ; AArch64 : `-mgeneral-regs-only`.

## 4. KernelImportPass
Réécriture automatique des appels extern non résolus en appels indirects via résolveur. Hash FNV-1a 64-bit. Défense trois couches.

## 5–7. Noyau Android, en-têtes, écriture de code Ring-0
`<neverc/dyncode/kernel.h>` fournit `neverc_kern_resolve_t` et `neverc_kern_hash()`. Charges calcul pur ou basées sur résolveur.

## 8. Feuille de route
Changement contexte noyau, réécriture résolveur, deux types de charges — tout terminé. En-têtes SDK noyau planifiés.
