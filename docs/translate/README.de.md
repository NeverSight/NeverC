**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Dokumentation](../README.de.md)

# C++ nach NeverC übersetzen

Das experimentelle `neverc translate` erzeugt prüfbaren `.nc`-Quelltext mit `cpp-core-v1`, `cpp-project-v1` und `cpp-math-v1`.

**Derzeit ist nur die Übersetzung von C++-Quelltext implementiert.** Unterstützung für E Language (易语言, `.e`), Python, Go, Rust, TypeScript und JavaScript ist geplant; entsprechende Übersetzer sind noch nicht verfügbar.

## Einrichtung und skalare Übersetzung

Erstellen oder installieren Sie das festgelegte Clang-20.1.8-Hilfsprogramm gemäß der [Frontend-Anleitung](../../utils/translate-frontends/cpp/README.md). Legen Sie es neben NeverC ab, setzen Sie `NEVERC_CPP_FRONTEND` oder verwenden Sie `--frontend PATH`. Zum Übersetzen wird es benötigt, zum Kompilieren bereits erzeugter Skalar- oder Projektausgaben nicht.

```sh
neverc translate --from cpp input.cpp -o output.nc
neverc output.nc -c -o output.o
```

Das Profil `cpp-core-v1` akzeptiert eine eigenständige C++17-Datei ohne Includes. Unterstützt werden `int`, `unsigned int`, `bool`, `void`, triviale Aggregattypen, freie Funktionen, Namensräume, Überladungen und der dokumentierte Kontrollfluss. Alle projekteigenen Deklarationen werden geprüft, auch unbenutzter Code.

## Projekte mit mehreren Dateien

Wählen Sie Übersetzungseinheiten ausdrücklich aus einer Kompilierungsdatenbank aus und geben Sie das Projektstammverzeichnis an. Das Hilfsprogramm analysiert jede Einheit einzeln; die Zusammenführung prüft Definitionen, Bindung, gemeinsame Typen und die Einhaltung der Ein-Definitions-Regel (ODR) anhand konservativer Prüfungen.

```sh
neverc translate --from cpp --profile cpp-project-v1 \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/a.cpp src/b.cpp --out-dir generated
```

Die Projektausgabe besteht aus `translated.nc` und `translated.h`. Nur Projekt-Header innerhalb dieses Stammverzeichnisses werden zugelassen. Bei mehreren Konfigurationen wählen Sie den nullbasierten Datenbankindex mit `--compdb-entry src/a.cpp=4`. Gespeicherte Compilerbefehle werden als Daten gelesen und niemals ausgeführt.

## Begrenzte Mathematik mit double

`cpp-math-v1` ergänzt Projekte um `double`, dokumentierte Konvertierungen/Vergleiche und exakt `std::fabs(double)` sowie `std::floor(double)`. Erforderlich sind Clang 20.1.8 / libc++ 200100 / macOS SDK 15.5, eine SDK-Beschreibungsdatei und ein explizites macOS-15.0-Ziel (arm64 oder x86_64). Allgemeine Gleitkommaarithmetik bleibt ausgeschlossen. Gleitkomma-Traps müssen maskiert und das Nullsetzen subnormaler Zahlen deaktiviert sein; alle vier Standard-Rundungsmodi sind getestet.

```sh
neverc translate --from cpp --profile cpp-math-v1 \
  --target arm64-apple-macosx15.0.0 --cpp-sdk /path/to/neverc-cpp-sdk.json \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/math.cpp --out-dir generated-math
```

Die mathematische Übersetzung prüft außerdem installierte NeverC-Mathematikheader, die Identität eingebetteter Implementierungen und einen echten Linkvorgang. Generierte Mathematikmodule verwenden die NeverC-Laufzeit ohne C++-Hilfsprogramm oder SDK. Mit `-fno-builtin-std` schlägt die Übersetzung nur dann vor dem Schreiben der Ausgabedateien fehl, wenn der endgültige Code Zuordnungen für `fabs`/`floor` benötigt. Mathematikcode ohne diese Zuordnungen kann weiterhin übersetzt werden.

## Validierung und Ausgabe

Mit `--check` anstelle einer Ausgabeoption durchlaufen Sie dieselbe Analyse, Erzeugung sowie Syntax- und Objektvalidierung, ohne erzeugte Dateien zu behalten. `--report PATH` schreibt strukturierte Diagnosen. Die Übersetzung führt das Quellprogramm niemals aus.

Ausgaben und Begleitdateien werden nicht überschrieben. `--out-dir` verlangt ein neues Verzeichnis unter einem vorhandenen Elternverzeichnis; `-o` einen neuen `.nc`-Pfad. Das Manifest enthält Zielanforderungen, Ein- und Ausgabehashes sowie die Kompilierungsanleitung; die Quellzuordnung verbindet erzeugte Zeilen mit ursprünglichen Positionen.

Die Ausführung wurde auf nativem macOS arm64 und auf macOS x86_64 unter Rosetta geprüft. Damit ist keine Unterstützung für die native Ausführung auf Intel-Macs, Linux, Windows oder andere Ziele nachgewiesen. Der angegebene Umfang umfasst weder vollständiges C++/STL noch Zeiger, Referenzen, Arrays, Ausnahmen, Templates, Zeichenketten oder `std::vector`. Siehe [Unterstützungsmatrix](../../utils/translate-frontends/docs/support-matrix.md), [Protokoll und Wiederherstellungsregeln](../../utils/translate-frontends/docs/protocol.md) und [Beispielprojekt](../../tests/neverc/Inputs/translate/cpp/project/).
