**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode-Compiler](../README.de.md)

# ARM64 (AArch64) Assembly-Tutorial — Shellcode-Perspektive

> Für Leser, die mit ARM64 nicht vertraut sind, mit Fokus auf vom Shellcode-Compiler generierte Befehle. Jeder Befehl enthält Anmerkungen und Vorher/Nachher-Vergleiche.

## 1. Register-Überblick
Allgemeine Register x0-x30 (64-bit) / w0-w30 (32-bit), sp, xzr/wzr, x29 (fp), x30 (lr). Apple ABI reserviert x16-x18. AAPCS64 Aufrufkonvention.

## 2. Verzweigungen und Aufrufe
- `b` (unbedingt), `bl` (mit Link → BRANCH26 Relocation), `br/blr` (indirekt), `ret`.
- **Shellcode muss `bl` vermeiden**: Linker füllt 26-Bit-Offset. Shellcode hat keinen Linker → `blr` verwenden.

## 3. PC-relative Adressierung
`adr` (±1MB), `adrp+add` (±4GB Seiten). Äquivalent zu x86_64 `lea rax, [rip+sym]`.

## 4. Immediate-Laden
`mov` + `movk` Sequenzen für 64-Bit-Werte. **Data2TextPass-Kern**: Konstantendaten als mov/movk auf Stack.

## 5–7. Speicherzugriff, Arithmetik, Vergleiche
`ldr/str`, `ldp/stp`, `add/sub/and/orr/eor/lsr/lsl`, `cmp` + bedingte Verzweigungen.

## 8. Typische generierte Befehlsfolgen
Reine Berechnung, rekursive Fibonacci, String-Stack-Inlining (Data2TextPass), Syscall (SyscallStubPass direkte svc).

## 9. Zusammenfassung

| Konzept | x86_64 | ARM64 | Shellcode |
|---------|--------|-------|-----------|
| Funktionsaufruf | `call rel32` | `bl imm26` | Extraktor patcht BRANCH26 |
| Adressladen | `lea rax,[rip+sym]` | `adrp+add` | PAGE21/PAGEOFF12 gepatcht |
| 64-Bit-Immediate | `mov rax,imm64` | `mov+movk ×4` | Null Relocations |
| Syscall | `syscall` | `svc #0x80` | Darwin: x16=nr |
