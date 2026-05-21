**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Dokumentation](../README.de.md)

# Die `.nc` Dateierweiterung

## Übersicht

NeverC erkennt `.nc` als seine native Quelldateierweiterung. Wenn der Compiler eine `.nc`-Eingabedatei erkennt, **aktiviert er automatisch** alle NeverC-Spracherweiterungen — keine zusätzlichen Flags erforderlich.

## Automatisch aktivierte Funktionen

| Flag | Wirkung |
|------|---------|
| `-fneverc-types` | Integer-Aliase im Rust-Stil (`u8`, `u16`, `u32`, `u64`, `i8`, `i16`, `i32`, `i64`, `usize`, `isize`) |
| `-fbuiltin-string` | Eingebauter `string`-Werttyp mit automatischer Speicherverwaltung, Dot-Call-Syntax und UTF-8-Unterstützung |

## Verwendung

Benennen Sie Ihre Quelldatei einfach mit der Erweiterung `.nc`:

```bash
# Automatisch — keine zusätzlichen Flags nötig
neverc hello.nc -o hello

# Äquivalent zu:
neverc -fneverc-types -fbuiltin-string hello.c -o hello
```

```c
// hello.nc
#include <stdio.h>

int main(void) {
    string greeting = "Hallo, NeverC!";
    printf("%s (len=%zu)\n", greeting.c_str(), greeting.len);

    u32 x = 42;
    i64 y = -100;

    string msg = greeting + " x=%u, y=%lld".format(x, y);
    printf("%s\n", msg.c_str());
    return 0;
}
```

## Funktionsweise

Die Erkennung erfolgt auf zwei Ebenen der Compiler-Pipeline:

### 1. Driver / Toolchain-Ebene

Der Driver überprüft die Erweiterung jeder Eingabedatei, bevor er den Compiler-Aufruf erstellt. Für `.nc`-Dateien werden `-fneverc-types` und `-fbuiltin-string` bedingungslos in die Befehlszeile eingefügt — der Benutzer muss sie nicht manuell übergeben.

Für `.c`-Dateien bleiben diese Flags optional: Benötigte Flags (`-fneverc-types`, `-fbuiltin-string`) explizit setzen.

### 2. CompilerInvocation-Ebene

Als Sicherheitsnetz überprüft das Frontend auch die Dateierweiterungen der Eingabedateien beim Parsen des Aufrufs. Wenn eine Eingabe die Erweiterung `.nc` hat, werden `LangOpts.NeverCTypes` und `LangOpts.BuiltinString` auf `1` gesetzt, sodass die Funktionen auch dann aktiv sind, wenn die Driver-Ebene umgangen wird (z.B. bei direktem Aufruf von `-cc1`).

## Kompatibilität

- `.nc`-Dateien werden als C-Quellcode behandelt — die Sprache ist immer noch C (standardmäßig C23), keine neue Sprache
- Alle Standard-C-Flags (`-std=c11`, `-O2`, `-g`, `-Wall`, etc.) funktionieren identisch
- `-fshellcode` kombiniert sich natürlich mit `.nc`: Der Shellcode-Modus aktiviert `string` bereits, und `.nc` stellt sicher, dass auch `neverc-types` aktiv ist
- Cross-Compilation (`-target aarch64-linux-gnu`, etc.) funktioniert auf die gleiche Weise
- `.c`-Dateien sind nicht betroffen — sie verhalten sich genau wie zuvor, sofern Sie die Flags nicht explizit übergeben

## Wann `.nc` vs `.c` verwenden

| Szenario | Empfehlung |
|----------|-----------|
| Neues NeverC-Projekt mit `string` und Rust-Stil-Typen | `.nc` verwenden |
| Bestehende C-Codebasis, die mit anderen Compilern kompatibel bleiben soll | `.c` + explizite Flags verwenden |
| Shellcode-Projekt | Beides möglich — `-fshellcode` aktiviert `string` in jedem Fall |
| Gemischte Codebasis | `.nc` für NeverC-spezifische Dateien, `.c` für portablen Code |
