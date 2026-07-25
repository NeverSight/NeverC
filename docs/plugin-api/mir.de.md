**Sprachen**: [English](mir.md) | [简体中文](mir.zh-CN.md) | [繁體中文](mir.zh-TW.md) | [日本語](mir.ja.md) | [한국어](mir.ko.md) | [Français](mir.fr.md) | [Deutsch](mir.de.md) | [Español](mir.es.md) | [Italiano](mir.it.md) | [Русский](mir.ru.md) | [العربية](mir.ar.md)

# MIR-API für NeverC-Plugins

Die erste öffentliche Plugin-ABI stellt Machine IR über `PluginMIR.h` bereit. Die
API verwendet stabile C-Bezeichner und opake Handles; Plugins hängen weder von
LLVM-Klassenlayouts noch von Enum-Nummern oder der C++-ABI ab.

## Aushandlung

Fragen Sie `NEVERC_INTERFACE_MIR` für `NevercMIRAPI` und
`NEVERC_INTERFACE_MIR_PASS` für `NevercMIRPassAPI` ab. Prüfen Sie die
zurückgegebene Tabellengröße, bevor Sie einen Funktionszeiger verwenden, und
ignorieren Sie Felder, die ein neuerer Host angehängt hat.

Der Schema-Digest identifiziert die genaue Abbildung zwischen stabilen Werten und
Host. `GetEntityInfo`, `GetOperandKindInfo`, `GetGenericOpcodeInfo` und
`GetMachinePropertyInfo` liefern kanonische Namen und geben an, ob eine Operation
ein Ziel-Schema benötigt.

## Stabiles Modell

Opake Handles repräsentieren:

- Maschinenfunktionen und Basisblöcke;
- Maschinenbefehle und Operanden;
- Mutationstransaktionen;
- Analyseergebnisse;
- Konstantenpool-Einträge, Rahmenobjekte, Sprungtabellen, Speicheroperanden und
  Zielverweise.

Handles gehören zu genau einer Codegenerierungs-Task. Gelöschte Entitäten,
zurückgerollte Entitäten und durch eine Mutation invalidierte Analyseergebnisse
werden ungültig.

Das generische Schema deckt zielunabhängige Opcodes, Operandenarten,
Maschineneigenschaften, Low-Level-Typen, Befehlsflags, Registerzuweisungen,
Rahmenobjekte, Konstanten, Sprungtabellen, Speicherzeigerformen und atomare
Ordnungen ab. Zielspezifische Opcodes erfordern ein ausdrücklich ausgehandeltes
Ziel-Schema.

## MIR lesen

`NevercMIRAPI` unterstützt:

- Eigenschaften von Maschinenfunktionen und das Durchlaufen von Blöcken;
- die Aufzählung von Vorgängern, Nachfolgern, Live-ins, Befehlen und Operanden;
- Abfragen von Befehls-Opcodes und -Flags;
- alle öffentlichen Formen von Maschinenoperanden;
- Informationen zu virtuellen und physischen Registern;
- Zustand von Rahmen, Konstantenpool, Sprungtabellen und Speicheroperanden.

Verwenden Sie Zähl-/Abfragepaare und begrenzte Ausgabepuffer. Zurückgegebene
Sichten sind, sofern nicht anders dokumentiert, nur für den aktuellen Callback
geliehen.

## Transaktionale Mutation

MIR-Änderungen erfolgen unter einer Mutations-Lease:

1. `BeginMutation` für eine Maschinenfunktion.
2. Blöcke und Befehle erzeugen, verschieben oder löschen.
3. Operanden und CFG-Kanten anhängen oder aktualisieren.
4. Änderungen an Maschineneigenschaften mit dem erforderlichen Nachweis anwenden.
5. `CommitMutation` oder `AbortMutation`.

Der Commit führt eine strukturelle Vorprüfung und die Machine-IR-Verifikation
durch. Ungültige Operanden, ein ungültiger CFG, unzulässige Verwendung generischer
Opcodes oder Eigenschaftsbehauptungen werden atomar zurückgerollt. Ein Abbruch
stellt Blockreihenfolge, Befehle, Operanden, CFG-Kanten und Maschineneigenschaften
wieder her.

Eigenschaftsänderungen verwenden `NevercMIRPropertyProof`. Ein Nachweis muss
entweder eine Eigenschaft invalidieren, deren Annahmen nicht mehr gelten, oder vor
ihrer Feststellung eine strukturelle Prüfung anfordern.

## Passes und Phasen

`NevercMIRPassDescriptor.Level` unterstützt die Adapter MachineModule,
MachineFunction und MachineBasicBlock. Die stabilen Hooks sind:

- nach der Befehlsauswahl;
- nach der Legalisierung;
- vor und nach dem Scheduler;
- vor und nach der Registerzuteilung;
- nach Prolog/Epilog;
- pre-emit;
- der finale Plugin-Slot.

Funktions-Passes können in parallelen Codegenerierungs-Partitionen laufen. Passes
auf Modulebene werden an serialisierten Pipeline-Barrieren ausgeführt. Die
Concurrency- und Reentrancy-Erklärungen des Plugins gelten weiterhin.

Jede Codegenerierungs-Pipeline endet nach dem finalen Plugin-Slot mit einem
host-eigenen `MachineVerifier`. Er ist ein versiegeltes Gate und kann von einem
Plugin nicht deaktiviert werden.

## Analysen

Die Analysetabelle stellt lebendige Variablen, Lebensintervalle, Slot-Indizes,
Dominatorbaum, Schleifeninformationen und Registerdruck bereit. Die Verfügbarkeit
hängt vom gewählten Hook ab, da manche LLVM-Analysen vor oder nach ihrer nativen
Pipeline-Stufe nicht existieren.

Deklarieren Sie benötigte und erhaltene Analysen im Pass-Deskriptor. Eine
committete Mutation invalidiert betroffene Ergebnis-Handles. Nach einer Mutation
„alles erhalten“ zu behaupten, wird abgelehnt.

## Minimales Beispiel

`pluginsdk/examples/MachinePass.c` registriert einen schreibgeschützten
Maschinenfunktions-Pass am stabilen Pre-Emit-Hook.

```sh
cmake --build build-neverc --target neverc-plugin-example-machine-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/MachinePass.so \
  -O2 -fno-lto -c input.c -o input.o
```

Verwenden Sie das von CMake erzeugte plattformspezifische Modulsuffix.

## Sicherheitsanforderungen

- Behalten Sie Task-Handles, MIR-Handles oder geliehene Sichten nicht über einen
  Callback hinaus.
- Erfinden Sie keine Handle-Werte oder LLVM-Opcode-Nummern.
- Mutieren Sie nicht außerhalb einer Lease.
- Initialisieren Sie Tabellen-Header und reservierten Speicher.
- Geben Sie Status über die C-Grenze zurück; lassen Sie niemals eine
  C++-Ausnahme hinüber.

Normative Deklarationen und Abdeckungsnachweise finden Sie in `PluginMIR.h`,
`MIRSchema.json`, `PluginPhaseSchema.h` und `coverage.json`.
