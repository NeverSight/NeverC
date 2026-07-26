**Sprachen**: [English](dyncode.md) | [简体中文](dyncode.zh-CN.md) | [繁體中文](dyncode.zh-TW.md) | [日本語](dyncode.ja.md) | [한국어](dyncode.ko.md) | [Français](dyncode.fr.md) | [Deutsch](dyncode.de.md) | [Español](dyncode.es.md) | [Italiano](dyncode.it.md) | [Русский](dyncode.ru.md) | [العربية](dyncode.ar.md)

[← NeverC-Plugin-ABI](README.de.md)

# DynCode-Plugins

`-fdyncode` kompiliert eine Übersetzungseinheit zu einem flachen,
positionsunabhängigen Abbild (`.bin`), dessen Code null Relokationen und keine
Datensection hat. Es zielt auf arm64/x86_64 unter macOS, Linux, Android und
Windows, wahlweise auf User- oder Kernel-Ausführungsebene. Plugins beobachten,
unterbrechen oder ersetzen die typisierten Phasen, die C in dieses Abbild
verwandeln, über dasselbe reine C-ABI wie die anderen Domänen: keine
LLVM-C++-Objekte, keine STL-Typen, keine Ausnahmen und keine Host-Zeiger, deren
Lebensdauer nicht von einer API-Tabelle festgelegt ist.

## Schnittstellen

```c
#include "neverc/Plugin/PluginDynCode.h"
```

| Schnittstelle | Tabelle | Slots | Zweck |
|---|---|--:|---|
| `NEVERC_INTERFACE_DYNCODE_{HIGH,LOW}` | `NevercDynCodeAPI` | 16 | Anfrage, Abbild, Bericht sowie die Section-/Symbol-/Relokations-/Extern-Abbildungen lesen |
| `NEVERC_INTERFACE_DYNCODE_REGISTRAR_{HIGH,LOW}` | `NevercDynCodeRegistrarAPI` | 5 | `RegisterTarget`, `RegisterImportProvider`, `RegisterExtractor`, `RegisterCharsetEncoder`, `RegisterBinaryVerifier` |
| `NEVERC_INTERFACE_DYNCODE_PHASE_{HIGH,LOW}` | `NevercDynCodePhaseAPI` | 4 | `GetPhaseInfo`, `GetRequest`, `GetImage`, `GetReport` |

Alle drei sind bei Major 1 `NEVERC_INTERFACE_STABLE`. Innerhalb eines
Phasen-Callbacks ist `NevercDynCodePhaseAPI` der Einstiegspunkt — sie verwandelt
den Frame in die Handles, die die andere Tabelle konsumiert:

```c
NevercDynCodeRequestHandle Request;
Phase->GetRequest(Phase->Context, Frame, Frame->Input, &Request);

NevercDynCodeRequestInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info), NEVERC_DYNCODE_API_MAJOR,
                                     NEVERC_DYNCODE_API_MINOR, 0};
DynCode->GetRequestInfo(DynCode->Context, Task, Request, &Info);
```

Die vier Abbildungsfamilien — Section-Maps, Symbol-Maps, Relokationen und
externe Referenzen — werden alle mit demselben first/next/info-Tripel
durchlaufen, zum Beispiel `GetFirstRelocation`, `GetNextRelocation`,
`GetRelocationInfo`. So liest ein Plugin die Entscheidungen der Extraktion, ohne
das Bericht-JSON zu parsen.

## DynCode ist ein Kompilierprodukt, kein `main()`-Nachbearbeitungsschritt

`-fdyncode` ist eine normale Action bzw. ein normaler Job im Driver-DAG. Der
Compile-Job veröffentlicht einen verifizierten `ObjectGraph` im Speicher; ein
`-dyncode-extract`-Job konsumiert diesen Graphen und schreibt das `-o`-Abbild
des Benutzers. `-###`, die Phasenausgabe und der Job-Graph zeigen den
Extraktionsjob allesamt an, sodass ein Plugin nie ein umgeschriebenes argv
rekonstruieren muss, um den Modus zu erkennen. Die eingefrorene Anfrage wird
task-lokal mit der prozessinternen Codegenerierung geteilt; es gibt kein
`getCurrentDynCodeOptions()`, kein prozessglobales Modus-Flag und keinen Umweg
über temporäre Objektdateien.

Genau eine Übersetzungseinheit wird zu genau einem Abbild abgesenkt. Mehrere
Eingaben, `-c/-S/-E` und nicht unterstützte Triples werden vorab mit stabilen
Diagnosen abgelehnt.

## Kompatibilitätsstufen

Phasen-IDs, Artefakt-IDs, die Container für Anfrage/Bericht/Abbild und die
Callback-Verträge sind STABLE-ABI der ersten Freigabe. Zielspezifische
Relokationsarten und die Section-/Symbol-Schemata der Objektformate sind
LOCKSTEP: Vergleichen Sie Ziel-Schema-ID und Digest, bevor Sie sie konsumieren.
NeverC weist ein nicht passendes Schema ab, bevor ein Provider aufgerufen wird.

## Die eingefrorene Anfrage

Zu Job-Beginn normalisiert der Driver die Kommandozeile zu einem unveränderlichen
`DynCodeRequest` und friert ihn ein. Kindtasks leihen sich den Schnappschuss;
sie verändern ihn nie. Die Anfrage trägt Ziel-Key und Objektformat, die
Ausführungsebene (user/kernel), die Entry-Policy (explizites Symbol,
Standard-Kandidatenliste, Anforderung Entry-at-Zero), die PIC-/Section-Policy,
die Policy für externe Referenzen, die Bad-Byte-Menge bzw. das Profil und das
Rewrite-Flag, die Charset-Provider-ID sowie Maximallänge, Ausrichtung und
Füllbyte.

## Der typisierte Phasengraph

DynCode ist ein fester Graph aus 34 Phasen. Dreißig gewöhnliche Übergänge sind
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE`; vier sind
`OBSERVABLE | SEALED_HOST_GATE`. Die versiegelten Gates sind die finale
IR-Verifikation, die finale MIR-Verifikation, die Abbildverifikation und der
Commit. Ein Plugin kann jede Phase beobachten, einen ersetzbaren Übergang mit
einem Interceptor umschließen oder dessen Provider ganz ersetzen; es kann
niemals ein versiegeltes Gate ersetzen, überspringen oder umgehen, und es kann
eine deaktivierte Transformation nicht als ausgelassenen Callback ausdrücken —
eine deaktivierte Transformation führt einen expliziten No-op-Provider aus,
dessen äquivalente Ausgabe der Host-Verifier weiterhin beweist.

Die Phasen in ihrer Reihenfolge:

1. Einfrieren der Anfrage;
2. die IR-Transformationen — Prepare, Absenken indirekter Sprünge, Absenken der
   Memory-Intrinsics (vor und nach Heap), Absenken der String-Laufzeit,
   Heap-Arena, drei `compiler_rt`-Positionen (pre/post/final), Absenken von
   Syscall-/PEB-/Kernel-Imports, zwei `data_to_text`-Positionen (pre/post),
   Inline-Optimierung, String-Finalize, Stackify, All-`blr` und die versiegelte
   finale IR-Verifikation;
3. die MIR-Prepare-Transformation und die versiegelte finale MIR-Verifikation;
4. Objektimport — den verifizierten `ObjectGraph` an den Task binden;
5. Extraktion — Plan, Layout, Relocate und Bau des Kandidatenabbilds;
6. die begrenzten Binärphasen — Post-Extract, Bad-Byte-Rewrite,
   Charset-Encode, Größe/Ausrichtung/Padding und Pre-Verify;
7. die versiegelte Abbildverifikation;
8. der versiegelte Commit.

Die normative Quelle für IDs, Policies, Stabilitätsstufen und Gates ist
[`Schema/PhaseSchema.json`]; der ausführbare Abdeckungsvertrag ist
[`coverage.json`].

## Eingebaute Transformationen sind ebenfalls Provider

Jeder eingebaute IR-/MIR-Pass ist als typisierter Provider verpackt; das
LLVM-Pass-Objekt wird nie über das C-ABI offengelegt. Eine Phase zu ersetzen
bedeutet, dass der eingebaute Provider nicht läuft — der bestandene Test beweist
das Verhalten oder den Trace, nicht bloß eine geglückte Registrierung. Die
Phasen `mem_intrin`, `compiler_rt` und `data_to_text` treten an mehr als einer
Position auf; jede Position ist eine eigene Phasen-ID mit eigenem Beweis, sodass
ein erneuter Durchlauf idempotent ist und nie auf verborgenem Pass-Zustand
beruht.

## ObjectGraph ist die einzige gewöhnliche Objekteingabe

Die Extraktion konsumiert genau einen verifizierten `ObjectGraph`, den die
Codegenerierungsroute des Ziels erzeugt hat. `dyncode.object.import` bindet
diesen Graphen und prüft Ziel-Key und Provenienz; es liest nie Bytes erneut von
der Festplatte und führt kein zweites Objekt-Parsing durch. Ein
benutzerdefiniertes Objektformat gelangt in DynCode, sobald es sich in einen
`ObjectGraph` einlesen lässt und passende Relokations- und Target-Provider hat.
Mehrere Objekte und LTO-Graph-Sets werden beim Einfrieren mit einem stabilen
`CAPABILITY_UNAVAILABLE` abgewiesen.

## Externe Referenzen und Import-Absenkung

Die Allowed-External-Menge der Anfrage bedeutet nur „ein Provider darf das
behandeln"; sie erlaubt niemals, dass eine unaufgelöste Relokation ins flache
Abbild überlebt. Jede externe Referenz muss als eines von diesen enden: in
IR/MIR eliminiert, auf ein Symbol im Abbild aufgelöst, in einen deklarierten und
vom Verifier akzeptierten Runtime-Resolver-Vertrag überführt, oder harter
Fehler. Syscall-Stub, PEB-Import und Kernel-Import sind die drei eingebauten
`ImportProvider`; jeder deklariert seinen Target-/Level-/Symbol-Matcher und den
ABI-Vertrag, den er erzeugt. Ein Plugin darf einen `ImportProvider` hinzufügen,
muss aber Ersatz-Provenienz, Änderung der Entry-ABI, Resolver-Parameter und
verbleibende Referenzen zurückliefern.

## Abbild, Bericht und begrenzte Byte-Änderungen

Die Extraktion erzeugt ein `DynCodeImage` und einen `DynCodeReport`. Das Abbild
ist ein begrenzter Byte-Builder plus Entry-Offset/-Symbol, die Ausgabe-Maps für
Quell-Sections und Quellsymbole, die Relokations-Dispositionen und die
externen/Runtime-Vertragsdatensätze. Jede Byte-Änderung läuft über die geprüfte
read/write/insert/append/resize-API des Builders; es gibt kein `uint8_t **`.
Eine Änderung erhöht die Abbild-Generation und invalidiert jeden
Relokations-/PIC-/Entry-Beweis, der den geänderten Bereich überschneidet.

Der Bericht ist ein unveränderliches, deterministisches Auditprodukt:
Digests von Anfrage/Route/Eingabe/Ausgabe, das Provider-Journal je Phase,
ausgewählte und verworfene Sections samt Begründung, die Entry-Wahl,
gepatchte/verworfene/Runtime-vertragliche Relokationen, verbleibende Externe,
Größe/Ausrichtung/Padding, der Bad-Byte-Scan und die Verifier-Checkliste.
`-fdyncode-report=<path>` schreibt sein kanonisches JSON; die ausführlichen
Diagnosen werden aus demselben Bericht gerendert statt aus einer zweiten
Zählung.

Die Bad-Byte-Rewrite-Kette läuft in eingefrorener topologischer Reihenfolge, und
jeder Schritt liefert einen Änderungsdatensatz zurück. Der Charset-Encoder wird
über eine exakte, stabile ID ausgewählt und liefert einen Decoder-Stub, die
kodierte Nutzlast, eine Entry-Aktualisierung und einen Target-Beweis; eine
unbekannte oder mehrdeutige ID ist ein harter Fehler. Das Rewrite abzuschalten
wählt einen expliziten No-op-Schritt — das abschließende Audit läuft trotzdem.

## Finaler Verifier und Zeitpunkt nach dem Finalisieren

Alle schreibbaren Phasen enden vor dem versiegelten finalen Verifier. Der
Verifier prüft, dass keine unbehandelte externe Relokation oder Referenz
zurückbleibt, dass keine verbotene Data-/TLS-/Unwind-/Debug-/Metadaten-Section
vorhanden ist, dass der Entry existiert, korrekt ausgerichtet und (sofern
gefordert) auf Offset null liegt, dass jede Relokationsstelle im Bereich liegt
und einen passenden PIC-Beweis für die aktuellen Abbild-Bytes hat, dass sich
Section- und Symbol-Maps nicht überlappen, dass die Regeln für
Länge/Ausrichtung/Padding gelten, und dass die finalen Bytes — inklusive
Decoder, Header und Padding — kein verbotenes Byte enthalten. Jeder Fehlschlag
liefert eine strukturierte Diagnose und verwirft das gesamte Ausgabebündel.

Nach dem Audit gibt es keinen schreibbaren Hook mehr. Berührt eine
Byte-Transformation einen ausführbaren Bereich, muss die eingefrorene Route eine
passende Binary-Verifier-Fähigkeit bereitstellen, die der Host aufruft, um den
PIC-Beweis über das finale, unveränderliche Abbild neu auszustellen.

## Driver-Optionen

`-fdyncode` aktiviert den Modus. `-fdyncode-entry=` wählt das Entry-Symbol.
`-fdyncode-bad-bytes=` / `-fdyncode-bad-byte-profile=` legen die verbotenen
Bytes fest, `-fdyncode-bad-byte-rewrite` (standardmäßig an) wählt die
Rewrite-Kette, und `-fdyncode-charset=` wählt einen registrierten Encoder.
`-fdyncode-max-length=`, `-fdyncode-align=` und `-fdyncode-pad=` begrenzen die
Endgröße. `-fdyncode-keep-obj=` zweigt das relozierbare Zwischenobjekt ab, und
`-fdyncode-report=` schreibt den Auditbericht. `-mdyncode-context=user|kernel`
wählt die Ausführungsebene.

## Nebenläufigkeits- und Fehlerregeln

- Halten Sie veränderlichen Zustand in den vom Host bereitgestellten
  Process-/Session-/Task-Scopes; nutzen Sie nie ein Current-Plugin- oder
  Current-Options-Singleton.
- Cachen Sie keine Task-Handles oder geliehenen Sichten über die Rückkehr eines
  Callbacks hinaus.
- Rufen Sie eine Interceptor-Continuation höchstens einmal auf, auf dem
  Callback-Thread.
- Geben Sie den ursprünglichen `NevercStatus` zurück; ein deklariertes
  `REPLACE`, das fehlschlägt, fällt nicht stillschweigend auf den eingebauten
  Provider zurück.
- Deklarieren Sie die engsten wahrheitsgemäßen Nebenläufigkeits- und
  Reentrancy-Modelle.

Die normativen Deklarationen stehen in [`PluginDynCode.h`], einen reinen
Lese-Tracer für Phasen finden Sie in
[`pluginsdk/examples/DynCodeTracePlugin.c`] und einen Charset-Encoder in
[`pluginsdk/examples/DynCodeEncoderPlugin.c`].

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PluginDynCode.h`]: ../../neverc/include/neverc/Plugin/PluginDynCode.h
[`pluginsdk/examples/DynCodeEncoderPlugin.c`]: ../../pluginsdk/examples/DynCodeEncoderPlugin.c
[`pluginsdk/examples/DynCodeTracePlugin.c`]: ../../pluginsdk/examples/DynCodeTracePlugin.c
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
