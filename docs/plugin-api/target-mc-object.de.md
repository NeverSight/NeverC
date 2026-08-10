**Sprachen**: [English](target-mc-object.md) | [简体中文](target-mc-object.zh-CN.md) | [繁體中文](target-mc-object.zh-TW.md) | [日本語](target-mc-object.ja.md) | [한국어](target-mc-object.ko.md) | [Français](target-mc-object.fr.md) | [Deutsch](target-mc-object.de.md) | [Español](target-mc-object.es.md) | [Italiano](target-mc-object.it.md) | [Русский](target-mc-object.ru.md) | [العربية](target-mc-object.ar.md)

[← NeverC-Plugin-ABI](README.de.md)

# NeverC-Plugin-API für Target, MC, Assembly und Objekte

Das Back-End besteht aus vier Headern und neunundzwanzig Phasen.
[`PluginTarget.h`] beschreibt ein Target und die Routen durch die
Codeerzeugung. [`PluginMC.h`] baut und beobachtet Maschinencode. Das Parsen und
Ausgeben von Assembly wohnt im selben Header. [`PluginObject.h`] verwandelt eine
relozierbare Datei in einen normalisierten Graphen und wieder zurück.

Zusammen erlauben sie einem Plugin, ein Target hinzuzufügen, einen einzelnen
Lowering-Schritt oder alle zu ersetzen, jede Instruktion beim Emittieren zu
beobachten, einen Assembly-Dialekt zu definieren oder eine Objektdatei
umzuschreiben — über ein reines C-ABI, das niemals ein `MCInst`, `MCSection`
oder `object::ObjectFile` von LLVM offenlegt.

## Schnittstellen

```c
#include "neverc/Plugin/PluginTarget.h"
#include "neverc/Plugin/PluginMC.h"
#include "neverc/Plugin/PluginObject.h"   /* includes both of the above */
```

| Schnittstelle | Tabelle | Slots | Zweck |
|---|---|--:|---|
| `NEVERC_INTERFACE_TARGET_*` | `NevercTargetAPI` | 2 | `RegisterTarget`, `RegisterCodeGenEdge` |
| `NEVERC_INTERFACE_TARGET_ABI_*` | `NevercTargetABIAPI` | 1 | `RegisterABI` |
| `NEVERC_INTERFACE_CALLING_CONVENTION_*` | `NevercCallingConventionAPI` | 1 | `RegisterCallingConvention` |
| `NEVERC_INTERFACE_MC_*` | `NevercMCAPI` | 53 | Ein `MCUnit` lesen und ändern; Encoder, Decoder, Back-Ends registrieren |
| `NEVERC_INTERFACE_MC_EMISSION_*` | `NevercMCEmissionAPI` | 7 | Emissionsereignisse und Layout-Momentaufnahmen |
| `NEVERC_INTERFACE_MC_PROVIDER_*` | `NevercMCProviderAPI` | 4 | MIR → MC ersetzen |
| `NEVERC_INTERFACE_ASSEMBLY_PROVIDER_*` | `NevercAssemblyProviderAPI` | 8 | Assembly-Parser oder -Printer ersetzen |
| `NEVERC_INTERFACE_OBJECT_*` | `NevercObjectAPI` | 34 | Einen ObjectGraph lesen und ändern |
| `NEVERC_INTERFACE_OBJECT_FORMAT_*` | `NevercObjectFormatAPI` | 1 | `RegisterFormat` |
| `NEVERC_INTERFACE_OBJECT_PHASE_*` | `NevercObjectPhaseAPI` | 2 | `GetGraph`, `GetImage` |

## Zwei Kompatibilitätsstufen

Das ist die Regel, die alles Weitere hier bestimmt.

**STABLE**, und sicher fest zu verdrahten: target-unabhängige Deskriptoren,
Phasen-IDs, Artefakt-IDs, die MC- und ObjectGraph-Container,
Ausgabetransaktionen und jeder Callback-Vertrag.

**LOCKSTEP**, und ohne Prüfung unsicher: target-spezifische Schemata für
Opcodes, Register, Operanden, Fixups, Relokationen und Aufrufkonventionen. Ihre
Zahlenwerte sind nur gegenüber genau einer Schema-Revision bedeutsam.

Überall, wo ein LOCKSTEP-Wert auftaucht, steht ein Schema-Digest daneben.
Vergleichen Sie ihn, bevor Sie den Wert lesen:

```c
if (!string_equal(Target.SchemaDigest, MY_COMPILED_SCHEMA_DIGEST))
  return fail(NEVERC_STATUS_ABI_MISMATCH);
```

NeverC weist ein nicht passendes Schema ebenfalls zurück, bevor es einen
Provider aufruft — die Prüfung ist also doppelt abgesichert. Ein Plugin, das
sie überspringt und trotzdem einen rohen Opcode liest, wird Instruktionen
jedoch stillschweigend falsch deuten.

## Die Phasen

Neunundzwanzig, in vier Domänen.

### `codegen` — Routing (4)

| Phase | Policy |
|---|---|
| `neverc.codegen.ir_to_mir` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.codegen.mir_to_mc` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.codegen.coarse_lower` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.codegen.product_verify` | OBSERVABLE, **SEALED** |

### `mc` — Maschinencode (13)

`neverc.mc.encode`, `neverc.mc.decode` und `neverc.mc.layout` sind
OBSERVABLE, INTERCEPTABLE, REPLACEABLE.

`neverc.mc.emission.pre_instruction` ist das eine Emissionsereignis, das
zusätzlich REPLACEABLE ist — dort ersetzt man eine Instruktion. Die anderen
neun (`unit_begin`, `unit_end`, `section_change`, `post_instruction`,
`post_encode`, `fixup`, `relaxation_round`, `pre_layout`, `post_layout`)
dienen nur der Beobachtung.

### `assembly` (4)

`neverc.assembly.parse` und `neverc.assembly.print` sind REPLACEABLE.
`neverc.assembly.final_verify` und `neverc.assembly.commit` sind SEALED.

### `object` (8)

`neverc.object.probe`, `read`, `write`, `pre_write` und `post_layout` sind
REPLACEABLE; `neverc.object.post_write` ist nur INTERCEPTABLE;
`neverc.object.final_verify` und `neverc.object.commit` sind SEALED.

## Ein Target registrieren

`NevercTargetDescriptor` ist der größte Deskriptor im ABI, weil er alles
transportiert, was Front-End und Back-End wissen müssen:

```c
typedef struct NevercTargetDescriptor {
  NevercABITableHeader Header;
  NevercTargetID TargetID;
  NevercStringView CanonicalName;
  NevercStringArrayView Aliases;
  NevercStructArrayView TripleMatchers;    /* NevercTargetTripleMatcher[] */
  NevercTargetABIID DefaultABI;
  NevercCallingConventionID DefaultCallingConvention;
  NevercInterfaceID MCSchemaID;
  NevercInterfaceID DefaultObjectFormatID;
  NevercTargetMachineDescriptor Machine;
  NevercStructArrayView Macros;            /* predefined macros           */
  NevercStructArrayView Builtins;          /* target builtins + lowering  */
  NevercStructArrayView Registers;         /* inline-asm register names   */
  NevercStructArrayView Constraints;       /* inline-asm constraints      */
  NevercStringView Clobbers;
  uint64_t Flags;
  NevercTargetValidateCPUFn ValidateCPU;
  NevercTargetCanonicalizeCPUFn CanonicalizeCPU;
  NevercTargetListCPUsFn ListCPUs;
  NevercTargetResolveFeaturesFn ResolveFeatures;
  NevercCreateTargetMachineFn CreateTargetMachine;
  NevercDestroyTargetMachineFn DestroyTargetMachine;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercTargetDescriptor;
```

`TripleMatchers` entscheidet, wann das Target ausgewählt wird: Jeder Matcher
nennt Architektur, Hersteller, Betriebssystem und Umgebung sowie eine
`Priority`, die Gleichstände gegenüber den eingebauten Targets auflöst.

`Machine` ist ein `NevercTargetMachineDescriptor` — Datenlayout, Standard- und
Tuning-CPUs, die Feature-Tabelle, unterstützte ABIs, Aufrufkonventionen und
Objektformate, Adressräume, Relokations- und Codemodelle (sowohl als Standard
als auch als Unterstützungsmaske), Ausnahmemodell (`NONE`, `DWARF`, `SJLJ`,
`SEH`, `WASM`), Unwind-Modell, Endianness, die Breite von
pointer/int/long/long long, Stack-Ausrichtung, maximale Atom- und
Vektorbreiten, `va_list`-Art, Ausführungsebenen (`USER`, `KERNEL`,
`HYPERVISOR`, `FIRMWARE`) sowie TLS-Unterstützung.

Target-Builtins tragen ihren eigenen Lowering-Callback, der einen lebendigen
IR-Builder erhält:

```c
static NevercStatus NEVERC_CALL
lower_builtin(void *UserData,
              const NevercTargetBuiltinLoweringInvocation *In,
              NevercIRValueHandle *OutResult) {
  /* In->Core, In->Builder, In->Mutation, In->IRBuilder,
     In->ResultType, In->Arguments, In->ArgumentCount */
  return In->Builder->BuildCall(/* … */);
}
```

## ABI und Aufrufkonventionen

Ein ABI klassifiziert Funktionssignaturen:

```c
static NevercStatus NEVERC_CALL
classify(void *UserData, const NevercABIFunctionQuery *Query,
         NevercABIArgumentClassification *ReturnValue,
         NevercABIArgumentClassificationArray *Arguments) {
  ReturnValue->Kind = NEVERC_ABI_ARGUMENT_DIRECT;
  for (uint64_t I = 0; I != Arguments->Count; ++I) {
    NevercABIArgumentClassification *A = &Arguments->Data[I];
    A->Kind  = NEVERC_ABI_ARGUMENT_INDIRECT;
    A->Flags = NEVERC_ABI_ARGUMENT_BYVAL;
  }
  return neverc_status_ok();
}
```

Argumentarten sind `DIRECT`, `EXTEND`, `INDIRECT`, `IGNORE`, `EXPAND`,
`INDIRECT_ALIASED` und `COERCE_AND_EXPAND`; Flags sind `BYVAL`, `REALIGN`,
`INREG`, `SRET_AFTER_THIS`, `CAN_BE_FLATTENED`, `SIGN_EXTEND` und
`PADDING_INREG`. Die Umwandlung ist `NONE`, `INTEGER`, `FLOAT` oder
`POINTER`, und `COERCE_AND_EXPAND` liefert ein Array von
`NevercABICoercionElement`.

Eine Aufrufkonvention geht eine Ebene tiefer und weist die tatsächlichen Orte
zu:

```c
static NevercStatus NEVERC_CALL
plan(void *UserData, const NevercCallingConventionQuery *Query,
     NevercCallingConventionPlan *Plan) {
  /* Query->TargetID, ->CallingConventionID, ->SchemaDigest, ->Function */
  /* Fill Plan->ReturnLocations and Plan->ArgumentLocations with
     NevercCallingConventionLocation records: REGISTER or STACK,
     ValueIndex, PieceOffset, Size, Alignment, RegisterNumber,
     StackOffset, and INDIRECT / BYVAL flags.                       */
  Plan->CalleeSavedRegisters = MySavedRegisters;
  Plan->StackAlignment       = 16;
  return neverc_status_ok();
}
```

`Query->SchemaDigest` ist ein LOCKSTEP-Wert — `RegisterNumber` bedeutet nur
etwas gegenüber dem Schema, das er benennt. Das vollständig ausgearbeitete
Beispiel finden Sie unter
[Eigene Aufrufkonventionen](custom-callconv/README.de.md#materialisierte-pläne) und in
[`pluginsdk/examples/CustomCallConvPlugin.c`].

## Routen der Codeerzeugung

Eine Route wird aus dem kanonischen `NevercTargetKey` ausgewählt: Target-ID,
Triple-Teile, CPU, Tuning-CPU, Features, ABI, Aufrufkonvention, Objektformat,
Relokationsmodell, Codemodell, Ausführungsebene, Zeigerbreite, Endianness und
Schema-Digest. Registrieren Sie die Kanten, die Sie bedienen können:

```c
NevercCodeGenEdgeDescriptor Edge = {0};
Edge.Header          = /* … */;
Edge.EdgeID          = MyEdgeID;
Edge.CanonicalName   = SV("com.example.mir-to-mc");
Edge.TargetID        = MyTargetID;
Edge.InputKind       = NEVERC_CODEGEN_PRODUCT_MIR;
Edge.OutputKind      = NEVERC_CODEGEN_PRODUCT_MC;
Edge.CompatibilityKey = SV("…");
Edge.ProviderID      = SV("com.example.backend");
Target->RegisterCodeGenEdge(Target->Context, RegistrarContext, &Edge);
```

Produktarten sind `IR`, `MIR`, `MC`, `ASSEMBLY`, `OBJECT_GRAPH`,
`OBJECT_IMAGE` und `CUSTOM`. Die feingranulare Route lautet
`IR → MIR → MC → ObjectGraph → ObjectImage`.

`NEVERC_CODEGEN_EDGE_COARSE` zu setzen und `CoarseLower` bereitzustellen,
ersetzt die gesamte Spanne `IR → ObjectImage` in einem Schritt:

```c
static NevercStatus NEVERC_CALL
coarse_lower(void *UserData, NevercTaskHandle Task,
             const NevercCodeGenRequest *Request,
             NevercCodeGenProductCandidate *OutCandidate) {
  /* Request->Target, ->Input, ->InputKind, ->OutputKind,
     ->OptimizationLevel, ->HasFinalIRProof                */
  OutCandidate->Kind      = NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE;
  OutCandidate->Artifact  = MyImage;
  OutCandidate->ProductID = MyProductID;
  return neverc_status_ok();
}
```

Auch eine grobe Route durchläuft `neverc.codegen.product_verify` und das
transaktionale Festschreiben der Ausgabe. `VerifyProduct` wird mit genau den
Pflichten aufgerufen, deren Erfüllung der Host von Ihnen erwartet —
`VERIFY_FINAL_IR`, `VERIFY_TARGET_KEY`, `VERIFY_PRODUCT_KIND`,
`VERIFY_PRODUCT_ID`, `VERIFY_STRUCTURE` —, sodass ein Provider kein Gate
heimlich durch eine Abkürzung umgehen kann.

## MC bauen

Ein `MCUnit` enthält Sections, Symbole, Ausdrücke, Fragmente, Instruktionen,
Operanden und Fixups. Gelesen wird per first/next-Iteration:

```c
NevercMCUnitInfo Unit = {0};
Unit.Header = /* … */;
MC->GetUnitInfo(MC->Context, Task, UnitHandle, &Unit);

NevercMCSectionHandle Section;
MC->GetFirstSection(MC->Context, Task, UnitHandle, &Section);
while (!neverc_handle_is_null(Section)) {
  NevercMCFragmentHandle Fragment;
  MC->GetFirstFragment(MC->Context, Task, Section, &Fragment);
  /* … */
  MC->GetNextSection(MC->Context, Task, Section, &Section);
}
```

Mutation ist transaktional, wie überall sonst:

```c
NevercMCMutationHandle Mutation;
MC->BeginMutation(MC->Context, Task, Unit, &Mutation);
MC->CreateSection(MC->Context, Task, Mutation, &SectionDescriptor, &Section);
MC->CreateSymbol(MC->Context, Task, Mutation, &SymbolDescriptor, &Symbol);
MC->AppendInstruction(MC->Context, Task, Mutation, Section, &Instruction);
Status = MC->CommitMutation(MC->Context, Task, Mutation);
if (Status.Code != NEVERC_STATUS_OK)
  MC->AbandonMutation(MC->Context, Task, Mutation);
```

Handles sind task-gebunden und generationsgeprüft, sodass ein Handle aus einer
abgebrochenen Mutation zurückgewiesen statt wiederverwendet wird.

Section-Flags sind `ALLOCATED`, `EXECUTABLE`, `WRITABLE`, `MERGEABLE` und
`DEBUG`. Symbolbindungen sind `LOCAL`, `GLOBAL` und `WEAK`; Typen sind `NONE`,
`FUNCTION`, `OBJECT`, `SECTION` und `TLS`; Definitionen sind `UNDEFINED`,
`SECTION`, `ABSOLUTE` und `COMMON`. Ausdrücke unterstützen die unären
Operatoren `PLUS`, `MINUS`, `NOT` sowie die binären `ADD`, `SUBTRACT`,
`MULTIPLY`, `DIVIDE`, `AND`, `OR`, `XOR`, `SHIFT_LEFT`, `SHIFT_RIGHT`.
Übergeben Sie `NEVERC_MC_AUTOMATIC_OFFSET`, wo der Host etwas für Sie
platzieren soll.

`RegisterSchema` veröffentlicht ein Target-MC-Schema, und `GetSchemaToken` /
`GetSchemaTokenInfo` übersetzen zwischen Name und LOCKSTEP-Token.

## Emission beobachten

Der Emissionsstrom meldet zehn Ereignisarten der Reihe nach — eine je
`neverc.mc.emission.*`-Phase. Die ABI reserviert außerdem
`NEVERC_MC_EMISSION_PRE_OBJECT_WRITE`; das Objekt schreiben selbst ist die
eigene Phase `neverc.object.pre_write`. Abonnieren Sie als
Beobachter und lesen Sie das Ereignis:

```c
NevercMCEmissionEventInfo Event = {0};
Event.Header = /* … */;
Emission->GetEvent(Emission->Context, Frame, Frame->Input, &Event);
/* Event.Kind, Event.Flags */
```

`Flags` sagt, welche Teile des Ereignisses gefüllt sind: `HAS_SECTION`,
`HAS_INSTRUCTION`, `HAS_ENCODING`, `HAS_FIXUP`, `HAS_LAYOUT` und
`CAN_REPLACE_INSTRUCTION`. Prüfen Sie das Flag, bevor Sie das zugehörige Feld
lesen — ein Ereignis, das noch keine Kodierung hat, bekommt keine, nur weil Sie
danach fragen.

`GetLayoutSection`, `GetLayoutFragment`, `GetLayoutSymbol` und
`GetLayoutFixup` liefern Adressen und Größen, sobald `HAS_LAYOUT` gesetzt ist.

Bei `pre_instruction` und nur dann, wenn `CAN_REPLACE_INSTRUCTION` gesetzt
ist, können Sie ersetzen:

```c
const NevercMCAPI *MC;
NevercMCUnitHandle Unit;
NevercMCInstHandle Instruction;
Emission->BeginInstructionReplacement(Emission->Context, Frame, Continuation,
                                       &MC, &Unit, &Instruction);
/* mutate Instruction through MC->BeginMutation / … / CommitMutation */
Emission->PublishInstructionReplacement(Emission->Context, Frame, Continuation,
                                         &OutResult->Output);
```

[`pluginsdk/examples/MCObserverPlugin.c`] ist die rein lesende Variante davon.

## Encoder, Decoder und Layout

Drei Registrierungen erweitern das Maschinencode-Back-End, alle über Target
und Schema-Digest verschlüsselt:

```c
MC->RegisterEncoder(MC->Context, RegistrarContext, &EncoderDescriptor);
MC->RegisterDecoder(MC->Context, RegistrarContext, &DecoderDescriptor);
MC->RegisterAsmBackend(MC->Context, RegistrarContext, &BackendDescriptor);
```

Ein Encoder schreibt durch eine Senke, statt einen Puffer zurückzugeben — so
bleibt die Eigentümerschaft auf der Host-Seite:

```c
Sink->WriteBytes(Sink->Context, Bytes);
Sink->AddFixup(Sink->Context, &Fixup);
```

Ein Decoder meldet eines von `NEVERC_MC_DECODE_SUCCESS`, `_SOFT_FAIL`,
`_UNKNOWN` oder `_FAIL`. Fixup-Arten beschreiben sich selbst über
`NevercMCFixupKindInfo` mit den Flags `PC_RELATIVE`, `SIGNED`, `RELAXABLE` und
`TARGET`.

Das Asm-Back-End besitzt die Relaxation. Das Layout gibt einen Beweis-Digest
aus, und **jede Mutation nach dem Layout entwertet diesen Beweis** und erzwingt
ein erneutes Layout, bevor das Objekt geschrieben werden kann — dasselbe
Muster der Generationsprüfung, das auch der Link-Graph verwendet.

## Assembly

Ein Parser-Provider verbraucht Quellbytes und veröffentlicht ein `MCUnit`:

```c
NevercAssemblyParseInputInfo In = {0};
In.Header = /* … */;
Asm->GetParseInput(Asm->Context, Frame, Frame->Input, &In);

NevercAssemblyTokenInfo Token = {0};
Asm->PeekSourceToken(Asm->Context, Frame, In.Source.Cursor, &Token);
Asm->AdvanceSourceToken(Asm->Context, Frame, In.Source.Cursor);

const NevercMCAPI *MC;
NevercMCUnitHandle Unit;
Asm->GetParseMCBuilder(Asm->Context, Frame, &MC, &Unit);
/* … build into Unit … */
Asm->PublishParsedMCUnit(Asm->Context, Frame, &Output);
```

Quellen sind entweder `NEVERC_ASSEMBLY_SOURCE_BUFFER` oder
`NEVERC_ASSEMBLY_SOURCE_RENDERED_TOKENS`. Vorverarbeitete Assembly (`.S`)
durchläuft zuerst den normalen Frontend-Präprozessor und kommt als gerenderte
Token an; einfache Assembly (`.s`) gelangt direkt als Puffer in den Parser.

Ein Printer geht den umgekehrten Weg — `GetPrintInput`, dann
`WritePrintOutput` in die bereitgestellte Ausgabetransaktion, dann
`PublishAssemblyOutput`. Anderswohin zu schreiben wird nicht unterstützt: Die
Parse-/Print-Verifikation und das Commit-Gate des Hosts laufen, bevor Bytes
sichtbar werden, sodass ein fehlgeschlagenes Ausgeben keine halbe Datei
zurücklässt.

## Objektgraphen

`NevercObjectAPI` normalisiert eine relozierbare Datei zu Sections, Symbolen,
Relokationen und COMDATs. Eingebaute Adapter decken ELF, COFF und Mach-O ab;
`RegisterFormat` fügt ein weiteres hinzu.

```c
NevercObjectGraphInfo Info = {0};
Info.Header = /* … */;
Object->GetGraphInfo(Object->Context, Task, Graph, &Info);
/* Info.Target, .ObjectSchemaDigest, .Generation, .SectionCount,
   .SymbolCount, .RelocationCount, .ComdatCount, .HasLayoutProof */

NevercObjectSymbolHandle Symbol;
Object->GetFirstSymbol(Object->Context, Task, Graph, &Symbol);
while (!neverc_handle_is_null(Symbol)) {
  NevercObjectSymbolInfo SymInfo = {0};
  SymInfo.Header = /* … */;
  Object->GetSymbolInfo(Object->Context, Task, Symbol, &SymInfo);
  Object->GetNextSymbol(Object->Context, Task, Symbol, &Symbol);
}
```

Die Mutation folgt für alle vier Entitätsarten dem Muster
create/replace/move/erase und wird innerhalb von `BeginMutation` …
`CommitMutation` / `AbandonMutation` vorgemerkt.

Section-Flags sind `ALLOCATED`, `EXECUTABLE`, `WRITABLE`, `MERGEABLE`,
`STRINGS`, `TLS`, `DEBUG`, `UNWIND`, `DISCARDABLE` und `RETAIN`. Ziele von
Relokationen sind `SYMBOL`, `SECTION`, `ABSOLUTE` oder `FORMAT_EXTENSION`.

Jeder Deskriptor hat ein Tripel aus `ExtensionOwner` / `ExtensionVersion` /
`Extension`. So bewahrt ein Format Daten auf, für die der normalisierte Graph
kein Feld hat — die Bytes reisen mit der Entität mit und kommen beim Schreiben
zurück, statt beim Hin und Her verloren zu gehen.

Der eingebaute ELF-Adapter hält exakte native Fakten in markierten Extensions
fest: `NCSE v2` enthält Sektionsindex, Adresse, Typ, Flags, Dateioffset und
Eintragsgröße; `NCSY v2` enthält `st_info`, das vollständige `st_other`,
`st_size` und einen expliziten Zustand für leere bzw. nicht leere native Namen;
`NCRL v1` enthält den nativen Relokationstyp und seinen offiziellen Namen. Ein
gewöhnliches leeres ELF-Symbol bleibt deshalb leer und wird nie in einen
künstlichen Namen `$symbol.N` umgeschrieben; ein tatsächlich so benanntes
`$symbol.N` bleibt ein normaler nicht leerer Name. Unverändertes natives
Image-Passthrough kann anonyme Symbole exakt erhalten. Ein graphmaßgeblicher
eingebauter Schreibvorgang lehnt sie vor dem Öffnen der Ausgabe-Senke ab, weil
die portable MC-Schreibweise denselben anonymen Symboltabelleneintrag nicht
rekonstruieren kann. Kanonische Android-Release-Audits verlangen die exakten
aktuellen markierten Payloads und spielen daraus die stabile Graphprojektion
nach.

### Ein Format registrieren

```c
NevercObjectFormatDescriptor Format = {0};
Format.Header           = /* … */;
Format.FormatID         = MyFormatID;
Format.CanonicalName    = SV("com.example.myfmt");
Format.SupportedTargets = MyTargets;
Format.DefaultExtension = SV(".mof");
Format.Flags            = NEVERC_OBJECT_FORMAT_CAN_PROBE |
                          NEVERC_OBJECT_FORMAT_CAN_READ  |
                          NEVERC_OBJECT_FORMAT_CAN_WRITE;
Format.Probe            = probe;
Format.Reader           = read;
Format.Writer           = write;
ObjectFormat->RegisterFormat(ObjectFormat->Context, RegistrarContext,
                             &Format);
```

`Probe` meldet eine `Confidence` von 0 bis
`NEVERC_OBJECT_PROBE_MAX_CONFIDENCE` (1000), die erkannte
`NevercObjectArtifactKind` (`RELOCATABLE`, `ARCHIVE`, `EXECUTABLE_IMAGE`,
`SHARED_IMAGE`, `UNIVERSAL_BINARY`) und ein `ConsumedMinimum` — wie viele
Bytes es zur Gewissheit brauchte, begrenzt auf
`NEVERC_OBJECT_PROBE_MAX_CONSUMED_MINIMUM` (65536). Die höchste Zuversicht
gewinnt.

`Reader` bekommt einen Graphen und eine offene Mutation und füllt beide.
`Writer` bekommt den Graphen, seinen Layout-Beweis und den begrenzten
Binär-Builder.

### Object-Format-1.1-Writer-Richtlinien

`NevercObjectFormatDescriptor.Header.Minor` beschreibt die Fähigkeit des
Providers; es ist kein hostweiter Modusschalter. Ein 1.0-Descriptor bleibt für
Probe, Read und gewöhnliche Default-Writes vollständig kompatibel. Sein Writer
erhält `NevercObjectWriteRequest.Header.Minor == 0` und `Header.Flags == 0`.
Minor 1 darf nur angekündigt werden, wenn der Writer die 1.1-Request-Flags
versteht; ein gewöhnlicher Write führt weiterhin Flags null und behält das
Ausgabeverhalten vor 1.1 bei.

Object Format 1.1 definiert diese Bits in
`NevercObjectWriteRequest.Header.Flags`:

- `NEVERC_OBJECT_WRITE_CANONICAL_ELF_TABLES` verlangt getrennte kanonische
  `.strtab`- und `.shstrtab`-Sektionen sowie die Neuzuordnung aller abhängigen
  Indizes. Dies ist eine Kanonisierung der ELF-Tabellen und kein
  relokierbarer Link: Die Sektionsreihenfolge, COMDAT-Gruppen,
  Linker-Metadaten, doppelte Symbole und Alias-Symbole, Relokationseinträge
  sowie sämtliche Nutzdaten außerhalb der Namenstabellen bleiben unverändert. Zusätzliche
  gültige, formatspezifische `SHT_STRTAB`-Sektionen bleiben erhalten; neu
  aufgebaut werden nur die vom ausgewählten `SHT_SYMTAB` verwendete
  Stringtabelle und die durch `e_shstrndx` bezeichnete Tabelle. Mit
  `DROP_DEBUG_INFO` werden ausschließlich Debug-Sektionen und Metadaten
  entfernt, die auf die entfernten Indizes dieser Sektionen verweisen.
- `NEVERC_OBJECT_WRITE_ANDROID_KERNEL_RELEASE` macht zusätzlich das endgültig
  serialisierte ELF maßgeblich: vom Writer erzeugte Mapping-Symbole werden
  entfernt und Release-Namen aus den tatsächlichen serialisierten
  Sektionskoordinaten erneut erzeugt.
- `NEVERC_OBJECT_WRITE_DROP_DEBUG_INFO` fordert das Entfernen von
  Debug-Sektionen als Teil einer dieser ELF-Richtlinien an.

`NEVERC_OBJECT_WRITE_REQUEST_KNOWN_FLAGS` ist die vollständige Maske bekannter
Bits. Zulässig sind ausschließlich `0`, `CANONICAL_ELF_TABLES`,
`CANONICAL_ELF_TABLES | DROP_DEBUG_INFO`,
`CANONICAL_ELF_TABLES | ANDROID_KERNEL_RELEASE` und alle drei Bits zusammen.
Das Release- oder Debug-Bit ohne das Canonical-Bit ist ungültig.

Der Host lehnt unbekannte oder ungültige Kombinationen und jede spezielle
Anforderung an einen Minor-0-Provider ab, bevor er den Ausgabe-Sink öffnet. Ein
1.1-Writer muss empfangene unbekannte oder ungültige Flags ebenfalls ablehnen,
statt sie zu ignorieren. Nach
dem Writer und allen `object.post_write`-Interceptors prüfen die semantische
Hostvalidierung und das versiegelte `object.final_verify` die serialisierten
Bytes erneut und sind maßgeblich. Diese Flags sind kein allgemeines Versprechen
für jedes Drittanbieterformat. Minor 1 bedeutet, dass der Writer das
Flags-Protokoll versteht: Er kann eine anwendbare ELF-Richtlinie implementieren
oder ausdrücklich `NEVERC_STATUS_CAPABILITY_UNAVAILABLE` zurückgeben, wenn sie
nicht anwendbar oder nicht unterstützt ist; die Anforderung darf nie
stillschweigend ignoriert werden.

### Die Schreib-Pipeline

1. sondieren und Bytes in einen ObjectGraph einlesen;
2. die Graph-Interceptors `object.pre_write` ausführen;
3. Layout erstellen, dann `object.post_layout` ausführen (nach jeder Mutation
   neu layouten);
4. ein begrenztes Kandidaten-Image schreiben;
5. die Binär-Interceptors `object.post_write` ausführen;
6. das versiegelte `object.final_verify` und das atomare `object.commit`
   ausführen.

Der Image-Zustand wandert `CANDIDATE` → `VERIFIED` → `COMMITTED` oder
`ABORTED` / `FAILED_PARTIAL`.

Beobachter erhalten rein lesende Brücken; eine aus einem Beobachter versuchte
Mutation wird mit `NEVERC_STATUS_POLICY_VIOLATION` abgelehnt. Writer und
Post-Write-Interceptors bekommen nur den begrenzten Builder
`NevercMutableBinaryAPI` — `Reserve`, `Write`, `WriteAt`, `Tell`, `ReadAt`,
`Insert`, `Append`, `Resize`. Ein Überlauf, ein fehlgeschlagener Callback oder
eine gescheiterte Verifikation bricht das Vormerken ab, sodass ein Fehlschlag
nie eine halbe Datei auf der Platte hinterlässt.

[`pluginsdk/examples/ObjectRewritePlugin.c`] ist eine vollständige
transaktionale Umschreibung.

## Regeln

- Vergleichen Sie den Schema-Digest, bevor Sie irgendeinen LOCKSTEP-Wert für
  Opcode, Register, Operand, Fixup, Relokation oder Aufrufkonvention
  verwenden.
- Halten Sie veränderlichen Zustand im vom Host bereitgestellten Process-,
  Session- und Task-Zustand.
- Cachen Sie keine Task-Handles oder geliehenen Views, nachdem ein Callback
  zurückgekehrt ist.
- Rufen Sie die Fortsetzung eines Interceptors höchstens einmal auf, und zwar
  auf dem Callback-Thread.
- Jedes `BeginMutation` erreicht genau ein Commit oder ein Abandon.
- Layouten Sie neu, nachdem Sie ein bereits layoutetes MCUnit oder einen
  ObjectGraph verändert haben; der alte Layout-Beweis ist veraltet und der Host
  wird ihn zurückweisen.
- Prüfen Sie `NevercMCEmissionEventInfo.Flags`, bevor Sie ein Ereignisfeld
  lesen, und ersetzen Sie eine Instruktion nur, wenn
  `CAN_REPLACE_INSTRUCTION` gesetzt ist.
- Schreiben Sie Ausgaben ausschließlich über die bereitgestellte Transaktion
  oder Byte-Senke.
- Geben Sie im Fehlerfall den ursprünglichen `NevercStatus` zurück und
  veröffentlichen Sie nichts Teilweises.
- Deklarieren Sie die engsten zutreffenden Modelle für Nebenläufigkeit und
  Reentranz.
- `codegen.product_verify`, `assembly.final_verify`, `assembly.commit`,
  `object.final_verify` und `object.commit` sind versiegelt. Nur beobachten.

Die normativen Deklarationen finden Sie in [`PluginTarget.h`], [`PluginMC.h`],
[`PluginObject.h`] und [`Schema/PhaseSchema.json`]; die Entitäts-, Operanden-,
Fixup- und Abschnittsarten, die sie verwenden, stammen aus
[`Schema/MCSchema.json`] und [`Schema/ObjectSchema.json`], aus denen
[`Schema/PluginMCSchema.inc`] und [`Schema/PluginObjectSchema.inc`] erzeugt
werden. [`coverage.json`] ordnet jeder dieser stabilen Phasen ihre positiven,
negativen, Ersetzungs-, Nur-Lese-Beobachter- und Sealed-Gate-Tests zu.

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PluginMC.h`]: ../../neverc/include/neverc/Plugin/PluginMC.h
[`PluginObject.h`]: ../../neverc/include/neverc/Plugin/PluginObject.h
[`pluginsdk/examples/CustomCallConvPlugin.c`]: ../../pluginsdk/examples/CustomCallConvPlugin.c
[`pluginsdk/examples/MCObserverPlugin.c`]: ../../pluginsdk/examples/MCObserverPlugin.c
[`pluginsdk/examples/ObjectRewritePlugin.c`]: ../../pluginsdk/examples/ObjectRewritePlugin.c
[`PluginTarget.h`]: ../../neverc/include/neverc/Plugin/PluginTarget.h
[`Schema/MCSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/MCSchema.json
[`Schema/ObjectSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/ObjectSchema.json
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginMCSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginMCSchema.inc
[`Schema/PluginObjectSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginObjectSchema.inc
