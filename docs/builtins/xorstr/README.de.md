**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Eingebautes Laufzeitsystem](../README.de.md)

# Kompilierzeit-Zeichenkettenverschlüsselung (`xorstr`)

## Überblick

NeverC bietet eine zweistufige Kompilierzeit-Zeichenkettenverschlüsselung für C-Code, die für sicherheitskritische Szenarien entwickelt wurde — API-Namen, Registrierungspfade und Debug-Nachrichten sind im kompilierten Binary nicht im Klartext sichtbar.

- **Stufe 1 — Explizites Makro**: `NC_XORSTR("string")` / `NEVERC_XORSTR("string")` für präzise Kontrolle pro Zeichenkette
- **Stufe 2 — Automatischer IR-Pass**: `-fencrypt-call-strings` verschlüsselt automatisch alle Zeichenkettenargumente in Funktionsaufrufen

Beide Stufen verwenden Stack-allozierte Puffer (keine Heap-Allokation), individuelle Schlüsselströme und volatile Bereinigung. An der nativen Maschinencode-Grenze werden explizite `NC_XORSTR`-Decoderaufrufe neu verschlüsselt und direkt an ihren jeweiligen Aufrufstellen expandiert; im finalen Objekt bleibt keine gemeinsam genutzte Decoderfunktion erhalten.

---

## Schnellstart

```c
#include <neverc/xorstr/xorstr.h>
FARPROC addr = GetProcAddress(hModule, NC_XORSTR("NtQuerySystemInformation"));
```

```bash
neverc -fencrypt-call-strings main.c -o main
```

---

## Schutzablauf

1. **Sema** verschlüsselt jedes Literal mit einem eigenen Schlüssel. Seed `0` bezieht frische Betriebssystem-Entropie; `-fstring-encrypt-key=` wählt deterministische 64-Bit-Ausgabe.
2. **Zwischen-IR / LTO-Eingabe** behält einen opaken, nicht spezialisierbaren Decoderaufruf, damit Optimierungen keinen Klartext zurückfalten.
3. **Finale Maschinencode-Grenze** entschlüsselt und verschlüsselt den Compiler-Ciphertext neu, wählt pro Aufrufstelle eine Form des Loops, expandiert ihn dort und entfernt Decoder, Hilfsgraph, ABI-Anker, Routenzustand und semantische Namen.
4. **Bereinigung** wird vor Optimierungs-/Provider-Übergaben und nochmals im finalen Tail eingefügt; die Wiederholung ist idempotent und repariert die Platzierung nach CFG-Änderungen.

### Decoder-Vielfalt

Die Zustandsfolge, Konstanten, der Ciphertext und äquivalente Byte-Ausdrücke variieren mit Seed und Aufrufstelle. Eine mögliche Form ist `a + b − 2 × (a & b)`. Volatile Zustands-/Ciphertext-Ladevorgänge hemmen Konstantenfaltung; `nooutline` verhindert, dass der Machine Outliner nach der IR-Finalisierung wieder einen gemeinsamen Decoder erzeugt.

Damit gibt es für IDA keine stabile Einzelroutine, die einmal erkannt oder emuliert werden kann. Zur Laufzeit benötigter Klartext kann durch dynamische Instrumentierung grundsätzlich weiterhin beobachtet werden.

---

## Automatische Verschlüsselung und Bereinigung

`-fencrypt-call-strings` läuft vor IPO, nach der normalen Optimierung und erneut nach jeder späten normalen oder Plugin-IR-Phase. LTO setzt denselben verpflichtenden Abschluss nach Provider- und Pre-Codegen-Hooks ein.

Direkte und indirekte `CallBase`-Argumente aus privaten, compiler-eigenen `unnamed_addr`-Literalen werden verarbeitet; GEPs, Casts, `freeze`, `select`, PHIs und promotbare lokale Pointer-Slots bleiben semantisch erhalten. Intrinsics, Inline-Assembly, extern sichtbare bzw. benutzerdefinierte Arrays und zu große Literale werden übersprungen. Ein geschütztes `musttail`-Argument führt bewusst zu einem Kompilierungsfehler.

`XorStrCleanupPass` löscht den vollständigen Stack-Puffer vor jedem erreichbaren `ret`, `resume`, zum Aufrufer abwickelnden `cleanupret` und nicht abgefangenen `catchswitch`-Unwind per volatile `memset`. Nicht vollständig verfolgbare oder unsichere Speicherformen werden abgelehnt statt nur teilweise gelöscht.

---

## Compiler-Flags-Referenz

| Flag | Beschreibung |
|------|-------------|
| `-fencrypt-call-strings` | Automatische Zeichenkettenverschlüsselung aktivieren |
| `-fno-encrypt-call-strings` | Automatische Verschlüsselung deaktivieren |
| `-fencrypt-call-strings-max-len=N` | Maximale Bytelänge (Standard: 1024) |
| `-fstring-encrypt-key=0xHEX` | Vollständigen 64-Bit-Seed überschreiben; `0` verwendet frische Entropie |

## Ausgabegrenzen und Reproduzierbarkeit

- `-fno-lto` finalisiert bei der nativen Codegenerierung des Frontends.
- Auto-LTO und Full-LTO behalten den opaken Decoder im Pre-Link-Bitcode und verschlüsseln/expandieren ihn erst nach Whole-Program- und Plugin-IR-Optimierung neu.
- Provider-Ersatzpipelines und späte Plugin-Pässe werden immer von Verschlüsselung, Bereinigung und Finalisierung gefolgt.
- Mit Standard-Seed unterscheiden sich unabhängige native Builds; betroffene Whole-Link- und Partition-Caches werden umgangen.
- Ein Seed ungleich null ist absichtlich deterministisch und cachefähig: gleiche Eingabe plus gleicher vollständiger 64-Bit-Seed ergibt denselben geschützten Code.
- `-emit-llvm` und Pre-Link-Bitcode sind Zwischenartefakte und behalten absichtlich die opake Decoder-ABI. Die Garantie „kein gemeinsamer Decoder“ gilt für erfolgreich erzeugten finalen Maschinencode.
