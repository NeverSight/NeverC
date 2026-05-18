**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilateur shellcode](../README.fr.md)

# Interface plugin Shellcode (Plugin SDK)

Le pipeline NeverC a une structure **pipeline cœur + couche utilisateur enfichable**. Obfuscation, anti-désassemblage, évasion EDR etc. sont **intentionnellement non intégrés**.

## 1. Pipeline de finalisation
Hook PostExtract → chaîne réécriteurs octets interdits → encodeur charset → audit octets interdits → taille → hook PostFinalize.

## 2. Réécriteur d'octets interdits
`registerBadByteRewriteStrategy`. Idempotent, déterministe, flux d'octets uniquement.

## 3. Encodeur de charset
`registerCharsetEncoder` avec `(Name, Encode, Stub, IsCharsetMember)`. Sortie doit passer le charset.

## 4. Taille / alignement / remplissage
`-fshellcode-max-length=`, `-fshellcode-align=`, `-fshellcode-pad=`.

## 5–6. Mapping des hooks et matrice PIC
11 hooks (6 IR + 3 MIR + 2 octets). Enregistrement plus tôt = couverture PIC plus large. Recommandation : chiffrement string → `RunAfterPrep` ; CFF → `RunAfterInlining` ; substitution instructions → `RunAfterPreEmit` ; chiffrement payload → `RunPostFinalize`.
