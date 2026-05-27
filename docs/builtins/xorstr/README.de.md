**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Eingebautes Laufzeitsystem](../README.de.md)

# Kompilierzeit-Zeichenkettenverschlüsselung (`xorstr`)

## Überblick

NeverC bietet eine zweistufige Kompilierzeit-Zeichenkettenverschlüsselung für C-Code, die für sicherheitskritische Szenarien entwickelt wurde — API-Namen, Registrierungspfade und Debug-Nachrichten sind im kompilierten Binary nicht im Klartext sichtbar.

- **Stufe 1 — Explizites Makro**: `NC_XORSTR("string")` / `NEVERC_XORSTR("string")` für präzise Kontrolle pro Zeichenkette
- **Stufe 2 — Automatischer IR-Pass**: `-fencrypt-call-strings` verschlüsselt automatisch alle Zeichenkettenargumente in Funktionsaufrufen

Beide Stufen verwenden Stack-allozierte Puffer (keine Heap-Allokation), einen Entschlüsselungsalgorithmus ohne XOR-Befehl (Anti-Signatur) und volatile `memset`-Bereinigung vor dem Funktionsrücksprung.

---

## Schnellstart

```c
#include <neverc/xorstr.h>
FARPROC addr = GetProcAddress(hModule, NC_XORSTR("NtQuerySystemInformation"));
```

```bash
neverc -fencrypt-call-strings main.c -o main
```

---

## Anti-Signatur-Entschlüsselung

Die Entschlüsselungsoperation vermeidet den XOR-Befehl vollständig und verwendet das mathematisch äquivalente: `a + b − 2 × (a & b)`.

---

## Compiler-Flags-Referenz

| Flag | Beschreibung |
|------|-------------|
| `-fencrypt-call-strings` | Automatische Zeichenkettenverschlüsselung aktivieren |
| `-fno-encrypt-call-strings` | Automatische Verschlüsselung deaktivieren |
| `-fencrypt-call-strings-max-len=N` | Maximale Bytelänge (Standard: 1024) |
| `-fstring-encrypt-key=0xHEX` | XOR-Schlüssel-Seed überschreiben |
