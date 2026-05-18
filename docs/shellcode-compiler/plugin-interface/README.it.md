**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilatore shellcode](../README.it.md)

# Interfaccia plugin Shellcode (Plugin SDK)

La pipeline NeverC ha una struttura **pipeline core + livello utente innestabile**. Offuscamento, anti-disassemblaggio, evasione EDR ecc. sono **intenzionalmente non integrati**.

## 1. Pipeline di finalizzazione
Hook PostExtract → catena riscrittori byte proibiti → codificatore charset → audit byte proibiti → dimensione → hook PostFinalize.

## 2. Riscrittore di byte proibiti
`registerBadByteRewriteStrategy`. Idempotente, deterministico, solo flusso byte.

## 3. Codificatore charset
`registerCharsetEncoder` con `(Name, Encode, Stub, IsCharsetMember)`. Output deve passare charset.

## 4. Dimensione / allineamento / padding
`-fshellcode-max-length=`, `-fshellcode-align=`, `-fshellcode-pad=`.

## 5–6. Mapping hook e matrice PIC
11 hook (6 IR + 3 MIR + 2 byte). Registrazione precedente = copertura PIC più ampia. Raccomandazione: cifratura string → `RunAfterPrep`; CFF → `RunAfterInlining`; sostituzione istruzioni → `RunAfterPreEmit`; cifratura payload → `RunPostFinalize`.
