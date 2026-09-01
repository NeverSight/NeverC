**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Dokumentationsindex](../README.de.md) · [← NeverC-Projekt](../i18n/README.de.md)

# VBS-Enklaven-DLLs unter Windows

NeverC kann Microsoft-kompatible VBS-Enklaven-DLLs für 64-Bit-Windows-Ziele linken. Der unterstützte Linker-Vertrag lautet:

```text
/DLL /INCREMENTAL:NO /INTEGRITYCHECK /ENCLAVE /GUARD:MIXED
```

Übergeben Sie Microsoft-Linker-Optionen mit `-Xmslink` oder `-Wl,` über den Windows-Treiber:

```powershell
neverc.exe --target=x86_64-pc-windows-msvc -fno-lto -shared -nostdlib `
  enclave.obj guarded.obj legacy.obj `
  -lvertdll -lbcrypt -llibcmt -llibvcruntime -lucrt `
  -Xmslink /INCREMENTAL:NO `
  -Xmslink /NODEFAULTLIB `
  -Xmslink /ENCLAVE `
  -Xmslink /INTEGRITYCHECK `
  -Xmslink /GUARD:MIXED `
  -Xmslink /DYNAMICBASE `
  -Xmslink /MACHINE:X64 `
  -o game-security-enclave.dll
```

Dieses Beispiel wählt die Enklavenvarianten der MSVC-CRT- und UCRT-Bibliotheken ausdrücklich mit `-l` aus. Jede explizite Angabe für `-vctoolsdir` oder `-winsysroot` behält ihre übliche Priorität. Ohne diese Überschreibungen löst jeder `/ENCLAVE`-Link unter macOS, Linux oder Windows die Windows-Bibliotheken ausschließlich aus der mitgelieferten NeverC-Ziel-Runtime auf; der Treiber erkennt ein auf dem Host installiertes Visual-Studio-Toolset oder Windows SDK weder automatisch noch greift er darauf zurück.

## Hostübergreifende Builds mit der mitgelieferten Runtime

Kompilierung und COFF-Linken sind hostunabhängig. Nach der Installation der Ziel-Runtime kann derselbe Befehl unter macOS, Linux oder Windows ausgeführt werden:

```text
neverc runtime install windows-x64
neverc runtime install windows-arm64
```

Das Zielpaket enthält die Windows-Header, Enklaven-CRT, Enklaven-UCRT, `vertdll.lib`, `bcrypt.lib` und die weiteren erforderlichen Windows-Importbibliotheken. Bei Auflösung über die mitgelieferte Runtime wechselt NeverC nur dann von den gewöhnlichen mitgelieferten CRT/UCRT-Verzeichnissen zu den Enklaven-CRT/UCRT-Verzeichnissen, wenn ein explizites `/ENCLAVE` mit einem globalen `/NODEFAULTLIB` kombiniert wird. In diesem Modus prüft der Treiber vor dem Linken, dass die mitgelieferten Dateien `libcmt.lib`, `libvcruntime.lib`, `ucrt.lib`, `vertdll.lib` und `bcrypt.lib` sämtlich vorhanden sind. Die Bibliotheken werden weiterhin ausdrücklich mit `-l...` ausgewählt. `/ENCLAVE` allein aktiviert weder die Enklaven-CRT/UCRT-Verzeichnisse noch wählt es deren Bibliotheken aus; die gewöhnlichen Suchpfade der mitgelieferten Runtime bleiben aktiv.

Die hostübergreifende Linkstufe erzeugt die unsignierte, unverarbeitete Enklaven-DLL. VEIID-Verarbeitung, SignTool-Signierung und das tatsächliche Laden mit `CreateEnclave`/`LoadEnclaveImage` bleiben Windows vorbehalten. Verschieben Sie eine unter macOS oder Linux gelinkte DLL daher für die letzten drei Schritte auf eine Windows-Paketierungs- oder Testmaschine. Informationen zur Installation und Erkennung der Runtime finden Sie unter [Ziel-Runtimes](../runtime/README.de.md).

## Erforderliche Image-Eingaben

Ein Enklaven-Link muss beide folgenden Image-Datendefinitionen bereitstellen:

- `__enclave_config` mit den `IMAGE_ENCLAVE_CONFIG`-Daten des Images.
- `_load_config_used` mit einer load-config-Struktur, die groß genug ist, um `EnclaveConfigurationPointer` zu enthalten.

NeverC hält `__enclave_config` beim Dead-Stripping am Leben, extrahiert es bei Bedarf aus einem Archiv und prüft, ob der endgültig relokierte load-config-Zeiger der virtuellen Adresse dieses Konfigurationsobjekts entspricht. Eine fehlende, absolute, verworfene, abgeschnittene oder falsch relokierte Definition ist ein Linkfehler.

`/GUARD:MIXED` aktiviert die CFG-Ausgabe für eine Mischung aus geschützten und älteren Objektdateien. Es erzeugt fünf Byte große GFID- und GIAT-Einträge: eine vier Byte große RVA gefolgt von einem Byte Metadaten; bei aktuellen gewöhnlichen Zielen sind diese Metadaten null. Die `GuardFlags` enthalten die Bits für CFG und die Eintragsgröße. Ältere Objekte liefern durch konservatives Scannen von Relokationen Ziele mit übernommener Adresse, wobei Unwind-Metadaten ausgeschlossen werden.
Wenn `/GUARD:MIXED` mit `/GUARD:EHCONT` kombiniert wird, verwendet die EH-Fortsetzungszieltabelle ebenfalls Fünf-Byte-Einträge: eine vier Byte große RVA gefolgt von einem Null-Metadatenbyte.

Eine ausdrückliche Anforderung für inkrementelles Linken ist mit `/ENCLAVE` inkompatibel und wird abgewiesen. Die letzte wirksame `/INCREMENTAL`-Option wird verwendet, einschließlich Optionen aus Objektdatei-Direktiven.

`/ENCLAVE` wählt nicht implizit DLL-Ausgabe, CFG, Integritätsprüfung, Enklaven-CRT-Bibliotheken, VEIID-Verarbeitung oder Signierung aus. Halten Sie diese Entscheidungen in der Build-Pipeline explizit. Im Modus mit mitgelieferter Runtime werden die oben beschriebenen Enklaven-CRT/UCRT-Suchpfade und die Prüfung der fünf Bibliotheken nur mit explizitem globalem `/NODEFAULTLIB` aktiviert; ohne diese Option bleiben die gewöhnlichen Pfade der mitgelieferten Windows-Runtime aktiv. Explizite Überschreibungen der Benutzer-Toolchain behalten ihre übliche Priorität.

## Build- und Bereitstellungsablauf

1. Kompilieren Sie sicherheitskritische Quellen mit aktiviertem CFG, beispielsweise mit `-fms-guard=cf`. Ältere Objekte dürfen uninstrumentiert bleiben, wenn der abschließende Link `/GUARD:MIXED` verwendet.
2. Definieren Sie die Enklavenkonfiguration und den Einstiegspunkt und linken Sie dann gegen Enklaven-CRT/UCRT sowie die erforderlichen Vertdll- und BCrypt-Importbibliotheken.
3. Prüfen Sie das unsignierte PE-Image und verifizieren Sie dessen load-config-Verzeichnis, CFG-Tabellen, Enklavenkonfigurationszeiger und Basisrelokationen.
4. Führen Sie unter Windows das VEIID-Werkzeug des Windows SDK auf dem fertigen Image aus.
5. Signieren Sie unter Windows das von VEIID verarbeitete Image mit SignTool. Die Signierung muss die letzte Dateiänderung sein.
6. Prüfen Sie im Windows-Host `IsEnclaveTypeSupported(ENCLAVE_TYPE_VBS)`, reservieren Sie die Enklave mit `CreateEnclave`, laden Sie die DLL mit `LoadEnclaveImage` und rufen Sie `InitializeEnclave` auf.

Für Anti-Cheat-Systeme eignet sich die Enklave für eine kleine Prüf- oder Schlüsselverwaltungskomponente, deren Code und privater Zustand eine stärkere Grenze zum gewöhnlichen Spielprozess benötigen. Halten Sie die Enklavenschnittstelle schmal und validieren Sie alle vom Host gelieferten Daten: Der Host kontrolliert weiterhin Eingaben, Scheduling, Speicher und Verfügbarkeit. Eine VBS-Enklave ergänzt serverseitige Autorität, Telemetrie, Treiberabwehr und gewöhnliche Prozesshärtung; sie ersetzt diese nicht.

## Validierung

Der Workflow `VBS enclave differential CI` läuft unter Windows. Sein statisches Gate:

- baut den NeverC-Linker und gezielte COFF-Tests;
- erzeugt gleichwertige, von Microsoft und NeverC gelinkte Enklaven-DLLs;
- vergleicht öffentliche PE/load-config/CFG-Semantik;
- führt Mutationstests gegen den PE-Prüfer aus;
- bereitet VEIID-verarbeitete Images für eine differentielle Laufzeitprüfung vor.

Die Laufzeitprüfung führt zuerst das Microsoft-Image aus. Fehlen dem gehosteten Runner VBS oder eine nutzbare Signierungsumgebung, wird das Ergebnis ausdrücklich als umgebungsbedingtes Überspringen ausgewiesen. Sobald das Microsoft-Referenzimage erfolgreich geladen wurde, ist das Scheitern eines der NeverC-Kandidaten ein harter Testfehler. Ein konfigurierter selbstgehosteter VBS-Runner kann den Laufzeiterfolg verbindlich machen.

Der Linker unterstützt x86-64- und ARM64-COFF-Enklavenimages. Er validiert den veröffentlichten Konfigurationszeiger und leitet dann aus der endgültigen Menge gewöhnlicher DLL-Importe eine zusammenhängende Folge von 80 Byte großen `IMAGE_ENCLAVE_IMPORT`-Einträgen ab. Die Einträge enthalten anfangs nur den Importnamen und sonst nullte Identitätsfelder, damit VEIID sie binden kann; der Linker schreibt Anzahl, Liste und Eintragsgröße zurück. Aktive verzögert geladene Importe werden abgewiesen. Für die versionierten Felder in `IMAGE_ENCLAVE_CONFIG` erzwingt der Linker keine zusätzliche Richtlinie.
