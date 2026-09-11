**Sprachen**: [English](../translate.md) | [简体中文](../zh-CN/translate.md) | [繁體中文](../zh-TW/translate.md) | [日本語](../ja/translate.md) | [한국어](../ko/translate.md) | [Français](../fr/translate.md) | [Deutsch](translate.md) | [Español](../es/translate.md) | [Italiano](../it/translate.md) | [Русский](../ru/translate.md) | [العربية](../ar/translate.md)

[← Dokumentation](README.md)

# C++ nach NeverC übersetzen

Das experimentelle `neverc translate` erzeugt prüfbaren `.nc`-Quelltext mit `cpp-core-v1`, `cpp-core-v2`, `cpp-project-v1` und `cpp-math-v1`.

**Derzeit ist nur die Übersetzung von C++-Quelltext implementiert.** Unterstützung für E Language (易语言, `.e`), Python, Go, Rust, TypeScript und JavaScript ist geplant; entsprechende Übersetzer sind noch nicht verfügbar.

## Einrichtung und skalare Übersetzung

Verwenden Sie eine normale NeverC-Installation mit den Standardressourcen. Das C++-Frontend und die freigegebenen SDK-Header sind integriert; eine separate Clang-Installation ist nicht erforderlich. Einzelheiten enthält die [Frontend-Bauanleitung](../../utils/translate-frontends/cpp/README.md).

```sh
neverc translate --from cpp input.cpp -o output.nc
neverc output.nc -c -o output.o
```

Das Profil `cpp-core-v1` akzeptiert eine eigenständige C++17-Datei ohne Includes. Unterstützt werden `int`, `unsigned int`, `bool`, `void`, triviale Aggregattypen, freie Funktionen, Namensräume, Überladungen und der dokumentierte Kontrollfluss. Alle projekteigenen Deklarationen werden geprüft, auch unbenutzter Code.

Mit `--profile cpp-core-v2` werden zusätzlich geprüfte `typedef`-/`using`-Typaliase, Aufzählungen mit dem zugrunde liegenden 32-Bit-Typ `int` oder `unsigned int` und `static_assert` unterstützt. Die Beschränkung auf eine Quelldatei ohne Includes bleibt bestehen. Siehe den [core-v2-Vertrag](../../utils/translate-frontends/docs/cpp-core-v2.md).

## Projekte mit mehreren Dateien

Wählen Sie Übersetzungseinheiten ausdrücklich aus einer Kompilierungsdatenbank aus und geben Sie das Projektstammverzeichnis an. Das integrierte Frontend analysiert jede Einheit einzeln; die Zusammenführung prüft Definitionen, Bindung, gemeinsame Typen und die Einhaltung der Ein-Definitions-Regel (ODR) anhand konservativer Prüfungen.

```sh
neverc translate --from cpp --profile cpp-project-v1 \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/a.cpp src/b.cpp --out-dir generated
```

Die Projektausgabe besteht aus `translated.nc` und `translated.h`. Nur Projekt-Header innerhalb dieses Stammverzeichnisses werden zugelassen. Bei mehreren Konfigurationen wählen Sie den nullbasierten Datenbankindex mit `--compdb-entry src/a.cpp=4`. Gespeicherte Compilerbefehle werden als Daten gelesen und niemals ausgeführt.

## Begrenzte Mathematik mit double

`cpp-math-v1` ergänzt Projekte um `double`, dokumentierte Konvertierungen/Vergleiche und exakt `std::fabs(double)` sowie `std::floor(double)`. Verwendet werden die integrierten Header von Clang 20.1.8 / libc++ 200100 / macOS 15.5; ein explizites macOS-15.0-Ziel (arm64 oder x86_64) ist erforderlich. Allgemeine Gleitkommaarithmetik bleibt ausgeschlossen. Gleitkomma-Traps müssen maskiert und das Nullsetzen subnormaler Zahlen deaktiviert sein; alle vier Standard-Rundungsmodi sind getestet.

```sh
neverc translate --from cpp --profile cpp-math-v1 \
  --target arm64-apple-macosx15.0.0 \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/math.cpp --out-dir generated-math
```

Die mathematische Übersetzung prüft außerdem installierte NeverC-Mathematikheader, die Identität eingebetteter Implementierungen und einen echten Linkvorgang. Generierte Mathematikmodule verwenden die NeverC-Laufzeit. Mit `-fno-builtin-std` schlägt die Übersetzung nur dann vor dem Schreiben der Ausgabedateien fehl, wenn der endgültige Code Zuordnungen für `fabs`/`floor` benötigt. Mathematikcode ohne diese Zuordnungen kann weiterhin übersetzt werden.

## Validierung und Ausgabe

Mit `--check` anstelle einer Ausgabeoption durchlaufen Sie dieselbe Analyse, Erzeugung sowie Syntax- und Objektvalidierung, ohne erzeugte Dateien zu behalten. `--report PATH` schreibt strukturierte Diagnosen. Die Übersetzung führt das Quellprogramm niemals aus.

Ausgaben und Begleitdateien werden nicht überschrieben. `--out-dir` verlangt ein neues Verzeichnis unter einem vorhandenen Elternverzeichnis; `-o` einen neuen `.nc`-Pfad. Das Manifest enthält Zielanforderungen, Ein- und Ausgabehashes sowie die Kompilierungsanleitung; die Quellzuordnung verbindet erzeugte Zeilen mit ursprünglichen Positionen.

CI-Ergebnisse, Ausführungs- und Installationsprüfungen sowie übersprungene Tests werden nach Plattform dokumentiert. Natives macOS arm64 und macOS x86_64 unter Rosetta bleiben getrennte Prüfungsumgebungen. Der angegebene Umfang umfasst weder vollständiges C++/STL noch Zeiger, Referenzen, Arrays, Ausnahmen, Templates, Zeichenketten oder `std::vector`. Siehe [Unterstützungsmatrix](../../utils/translate-frontends/docs/support-matrix.md), [Protokoll und Wiederherstellungsregeln](../../utils/translate-frontends/docs/protocol.md) und [Beispielprojekt](../../tests/neverc/Inputs/translate/cpp/project).
