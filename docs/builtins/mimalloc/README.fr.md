**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Système de Runtime Intégré NeverC](../README.fr.md)

# Allocateur mimalloc Intégré

## Vue d'ensemble

NeverC peut intégrer [mimalloc](https://github.com/microsoft/mimalloc) — l'allocateur mémoire haute performance de Microsoft — directement dans les binaires compilés via la fusion de bitcode LLVM. Une fois activé, `malloc`, `free`, `calloc` et `realloc` sont remplacés de manière transparente par les implémentations de mimalloc au moment de la compilation.

**Activation :**

```bash
neverc -fbuiltin-mimalloc main.c -o main
```

---

## Support des Plateformes

| Plateforme | Triple | Statut |
|------------|--------|--------|
| Linux x86_64 | `x86_64-unknown-linux-gnu` | Supporté |
| Linux AArch64 | `aarch64-unknown-linux-gnu` | Supporté |
| Android | `aarch64-linux-android` | Supporté |
| macOS x86_64 | `x86_64-apple-macosx` | Supporté |
| macOS AArch64 | `arm64-apple-macosx` | Supporté |
| iOS | `arm64-apple-ios` | Supporté |
| Windows x86_64 (MSVC) | `x86_64-pc-windows-msvc` | Supporté |
| Windows AArch64 (MSVC) | `aarch64-pc-windows-msvc` | Supporté |

---

## Suppression Automatique

| Drapeau / Mode | Raison |
|----------------|--------|
| `-fno-builtin` | Pas de scénario d'override CRT |
| `-mkernel` | Pas de tas en espace utilisateur dans le noyau |
| `-fshellcode-mode` | Pas de tas en shellcode |
| `-ffreestanding` | Pas de libc à remplacer |

---

## Processus de Bootstrap

```bash
ninja neverc                         # Étape 1 : placeholders bitcode vides
ninja neverc-bootstrap-mimalloc-bc   # Étape 2 : compiler le bitcode par OS
ninja neverc                         # Étape 3 : intégrer le vrai bitcode
```

---

## Référence des Drapeaux du Compilateur

| Drapeau | Description |
|---------|-------------|
| `-fbuiltin-mimalloc` | Activer l'injection de l'override mimalloc (activé par défaut pour les builds hébergés) |
| `-fno-builtin-mimalloc` | Désactiver explicitement l'injection mimalloc |

| Macro | Valeur | Quand définie |
|-------|--------|---------------|
| `__NEVERC_MIMALLOC__` | `1` | Quand `-fbuiltin-mimalloc` est actif |
