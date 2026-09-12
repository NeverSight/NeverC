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

Mit `--profile cpp-core-v2` werden geprüfte `typedef`-/`using`-Typaliase, Aufzählungen mit unterstützten ganzzahligen Basistypen, `static_assert` sowie Objektzeiger und Lvalue-Referenzen in begrenztem Umfang unterstützt. Referenzparameter und zurückgegebene Referenzen erhalten die Aliasbeziehungen; Nullzeiger und verschachtelte `const`-Qualifikationen werden unterstützt. Die Beschränkung auf eine Quelldatei ohne Includes bleibt bestehen. Siehe den [core-v2-Vertrag](../../utils/translate-frontends/docs/cpp-core-v2.md).

Core v2 ergänzt lokale Arrays fester Länge und Array-Felder, mehrdimensionale Indizierung sowie Zeiger und Referenzen auf Arrays. Bei Teilinitialisierung werden ausgelassene skalare Elemente mit null initialisiert; Record-Elemente folgen ihrer ausgewählten Initialisierung; Reihenfolge und Aliasbeziehungen bleiben erhalten. Array-Länge und Initialisierungsexpansion sind begrenzt. Globale Arrays, Arrays variabler Länge und nichttriviale Elementdestruktoren bleiben ausgeschlossen.

Core v2 unterstützt `switch`/`case`/`default` mit C++17-Initialisierungsanweisungen, Fallthrough und geprüften `[[fallthrough]]`-Annotationen. Der Selektor wird einmal ausgewertet; verschachtelte Switches und Schleifen behalten ihre `break`-/`continue`-Ziele. GNU-Case-Bereiche und andere Anweisungsattribute bleiben ausgeschlossen.

Core v2 unterstützt jetzt vorzeichenbehaftete und vorzeichenlose Ganzzahlen mit 8, 16, 32 und 64 Bit, Zeichentypen und Zeichenliterale sowie konstante `sizeof`-/`alignof`-Abfragen. Integer-Promotions und Überladungsauflösung erfolgen vor der Normalisierung; `long`, `wchar_t` und der Größentyp folgen der Zielplattform. Auch Enum-Basistypen dürfen schmaler oder breiter sein. Laufzeitzeichenketten und STL sind noch in Entwicklung.

Core v2 unterstützt außerdem Objektzeiger-Offsets, Zeigerdifferenzen, Inkrement/Dekrement und zusammengesetzte Zuweisungen, einschließlich Array-Durchläufen und mehrdimensionaler Schrittweiten. Generierte Hilfsfunktionen erhalten die C++17-Regeln für Nullzeiger plus/minus null und die Differenz zweier Nullzeiger. Zeigerordnung, Zeiger/Ganzzahl-Konvertierungen sowie STL-Container und -Algorithmen bleiben ausgenommen.

Core v2 vergleicht Quelltypgrößen und ABI-Ausrichtungen mit dem eigenen Zielmodell von NeverC, einschließlich Strukturgrößen und Feldoffsets. Statische Zusicherungen im erzeugten Code prüfen das Layout bei der Kompilierung erneut; das Manifest hält diese Angaben fest.

Core v2 unterstützt benannte nichtvirtuelle Memberfunktionen der zugelassenen Record-Typen, einschließlich const-Überladungen, lvalue-qualifizierter Methoden, statischer Methoden und `this`. Aufrufe erhalten die Identität des ursprünglichen Objekts und werten das Empfängerobjekt vor den Argumenten aus. Nichtstatische Aufrufe auf temporären Objekten, Vererbung, Templates und allgemeine STL-Unterstützung fehlen noch.

Core v2 unterstützt außerdem gewöhnliche benutzerdefinierte Konstruktoren für Record-Typen mit Standardlayout sowie trivialem Kopieren und Zerstören. Lokale Objekte, Felder und Array-Elemente werden direkt an ihrem endgültigen Speicherort konstruiert; Felder werden in Deklarationsreihenfolge initialisiert. Explizit als default markierte oder delegierende Konstruktoren, benutzerdefinierte Kopier-/Move-Konstruktoren und Destruktoren, Aufräumlogik und Ausnahmen fehlen noch.

Core v2 erzeugt für jeden per Wert übergebenen Record-Parameter ein eigenes Objekt und schreibt Record-Rückgaben direkt in den Zielspeicher des Aufrufers. Dies gilt auch für Konstruktoren und Methoden; erforderliche Kopien und Referenzaliase bleiben erhalten. Das ist die von NeverC gewählte Umsetzung für die unterstützten trivialen Records, keine allgemeine Adressgarantie von C++17. Nichttriviales Kopieren und Verschieben, Destruktion, Bereinigung und vollständige STL-Unterstützung sind noch in Entwicklung.

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

CI-Ergebnisse, Ausführungs- und Installationsprüfungen sowie übersprungene Tests werden nach Plattform dokumentiert. Natives macOS arm64 und macOS x86_64 unter Rosetta bleiben getrennte Prüfungsumgebungen. Der angegebene Umfang umfasst weder vollständiges C++/STL noch Ausnahmen, Templates, Zeichenketten oder `std::vector`. Siehe [Unterstützungsmatrix](../../utils/translate-frontends/docs/support-matrix.md), [Protokoll und Wiederherstellungsregeln](../../utils/translate-frontends/docs/protocol.md) und [Beispielprojekt](../../tests/neverc/Inputs/translate/cpp/project).
