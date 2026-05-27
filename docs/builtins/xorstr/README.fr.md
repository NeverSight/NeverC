**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Système d'exécution intégré NeverC](../README.fr.md)

# Chiffrement de chaînes à la compilation (`xorstr`)

## Vue d'ensemble

NeverC fournit un chiffrement de chaînes à deux niveaux pour le code C, conçu pour les scénarios de sécurité où les chaînes en clair (noms d'API, chemins du registre) ne doivent pas être visibles dans le binaire compilé.

- **Niveau 1 — Macro explicite** : `NC_XORSTR("string")` / `NEVERC_XORSTR("string")` pour un contrôle précis par chaîne
- **Niveau 2 — Passe IR automatique** : `-fencrypt-call-strings` pour chiffrer automatiquement tous les arguments chaîne dans les appels de fonctions

Les deux niveaux utilisent des tampons alloués sur la pile (pas d'allocation sur le tas), un algorithme de déchiffrement sans instruction XOR (anti-signature), et un nettoyage par `memset` volatile avant le retour de fonction.

---

## Démarrage rapide

```c
#include <neverc/xorstr.h>
FARPROC addr = GetProcAddress(hModule, NC_XORSTR("NtQuerySystemInformation"));
```

```bash
neverc -fencrypt-call-strings main.c -o main
```

---

## Déchiffrement anti-signature

L'opération de déchiffrement évite entièrement l'instruction XOR, utilisant l'équivalent mathématique : `a + b − 2 × (a & b)`.

---

## Référence des drapeaux du compilateur

| Drapeau | Description |
|---------|-------------|
| `-fencrypt-call-strings` | Activer le chiffrement automatique des chaînes |
| `-fno-encrypt-call-strings` | Désactiver le chiffrement automatique |
| `-fencrypt-call-strings-max-len=N` | Longueur maximale en octets (défaut : 1024) |
| `-fstring-encrypt-key=0xHEX` | Remplacer la graine de clé XOR |
