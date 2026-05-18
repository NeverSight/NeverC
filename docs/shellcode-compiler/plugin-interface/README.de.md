**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode-Compiler](../README.de.md)

# Shellcode Plugin-Schnittstelle (Plugin SDK)

NeverCs Pipeline hat eine **Kern-Pipeline + steckbare Benutzerschicht**. Obfuskation, Anti-Disassembly, EDR-Umgehung etc. sind **absichtlich nicht eingebaut**.

## 1. Finalize-Pipeline
PostExtract-Hook → Bad-Byte-Rewriter-Kette → Zeichensatz-Encoder → Bad-Byte-Audit → Größe → PostFinalize-Hook.

## 2. Bad-Byte-Rewriter
`registerBadByteRewriteStrategy`. Idempotent, deterministisch, nur Byte-Stream.

## 3. Zeichensatz-Encoder
`registerCharsetEncoder` mit `(Name, Encode, Stub, IsCharsetMember)`. Ausgabe muss Zeichensatz bestehen.

## 4. Größe / Ausrichtung / Padding
`-fshellcode-max-length=`, `-fshellcode-align=`, `-fshellcode-pad=`.

## 5–6. Hook-Mapping und PIC-Matrix
11 Hooks (6 IR + 3 MIR + 2 Byte). Frühere Registrierung = breitere PIC-Abdeckung. Empfehlung: String-Verschlüsselung → `RunAfterPrep`; CFF → `RunAfterInlining`; Befehlssubstitution → `RunAfterPreEmit`; Payload-Verschlüsselung → `RunPostFinalize`.
