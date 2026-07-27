**Sprachen**: [English](prep.md) | [简体中文](prep.zh-CN.md) | [繁體中文](prep.zh-TW.md) | [日本語](prep.ja.md) | [한국어](prep.ko.md) | [Français](prep.fr.md) | [Deutsch](prep.de.md) | [Español](prep.es.md) | [Italiano](prep.it.md) | [Русский](prep.ru.md) | [العربية](prep.ar.md)

[← NeverC-Plugin-ABI](README.de.md)

# NeverC Plugin-API für den Präprozessor

[`PluginPrep.h`] legt den Präprozessor auf zwei Weisen offen. Ein **Abonnement**
von 39 Ereignisarten liefert eine reine Lesespur von allem, was der Präprozessor
tut: Dateieintritt, Makrodefinition und -expansion, Auswertung von Bedingungen,
Pragmas. Sechs **Phasen** gehen weiter und lassen Sie das Ergebnis umschreiben:
ein `#include` umleiten, die Expansionstoken eines Makros ersetzen, ein Pragma
selbst behandeln oder `__has_feature` anders beantworten.

## Schnittstelle

```c
#include "neverc/Plugin/PluginPrep.h"

Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_PREP_HIGH, NEVERC_INTERFACE_PREP_LOW},
    NEVERC_PREP_API_MAJOR, NEVERC_PREP_API_MINOR, &Table, &Minor, &TableSize);
```

Die 230 Tokenarten (`NEVERC_TOKEN_KIND_COUNT`) und die Präprozessor-
Schlüsselwortarten stammen aus [`Schema/PluginPrepSchema.inc`], das der Header
einbindet und dessen Capability-Major gleich `NEVERC_PREP_API_MAJOR` sein muss —
eine Abweichung ist ein Übersetzungsfehler, keine Überraschung zur Laufzeit.
Jede Art trägt außerdem eine Kategorie: `NEVERC_TOKEN_CATEGORY_SPECIAL`,
`COMMENT`, `IDENTIFIER`, `LITERAL`, `PUNCTUATOR`, `KEYWORD` oder `ANNOTATION`.

## Die sechs Präprozessorphasen

| Phase | Richtlinie | Eingabe → Ausgabe |
|---|---|---|
| `neverc.prep.token` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE | ein Token → Tokenliste |
| `neverc.prep.build_token_stream` | ebenso | Bereich → Tokenstrom |
| `neverc.prep.include.intercept` | ebenso | Include-Anfrage → Include-Entscheidung |
| `neverc.prep.macro.intercept` | ebenso | Makrooperation → Aktion + Token |
| `neverc.prep.pragma.intercept` | ebenso | Pragma → Aktion + Token |
| `neverc.prep.feature_query.intercept` | ebenso | `__has_*`-Anfrage → Wert |

Fünf der sechs besitzen auf `NevercPrepAPI` ein Paar `Get<Kind>PhaseInput` und
`Create<Kind>PhaseOutput`; die `Create`-Hälfte nimmt die
`NevercPhaseContinuation` des Interzeptors entgegen, sodass eine Ausgabe nur von
innerhalb der Phase erzeugt werden kann, der sie gehört.
`neverc.prep.build_token_stream` ist die Ausnahme: sie hat
`GetTokenStreamPhaseInput` und veröffentlicht über `TokenStreamBuilderCommit`
am Phasen-`Frame`, nicht über ein `Create*PhaseOutput` mit Continuation.

## Token lesen

```c
typedef struct NevercTokenInfo {
  NevercABITableHeader Header;
  NevercTokenKind Kind;
  NevercTokenFlags Flags;
  NevercTokenOriginKind Origin;
  uint32_t Reserved;
  NevercStringView Spelling;
  NevercSourceLocation Location;
  NevercSourceRange Range;
  NevercIdentifierHandle Identifier;
  NevercMacroDefinitionHandle MacroDefinition;
} NevercTokenInfo;
```

`Origin` ist `NEVERC_TOKEN_ORIGIN_FILE`, `MACRO_REPLACEMENT`, `MACRO_ARGUMENT`
oder `SYNTHESIZED` — so unterscheidet man ein vom Benutzer getipptes Token von
einem, das ein Makro erzeugt hat.

Die Flags sind die Buchführung des Präprozessors selbst und werden wichtig,
sobald Sie Token synthetisieren:

| Flag | Bedeutung |
|---|---|
| `NEVERC_TOKEN_FLAG_START_OF_LINE` | Erstes Token seiner Zeile |
| `_LEADING_SPACE` | Davor steht Leerraum |
| `_DISABLE_EXPANSION` | Dieses Token nicht makroexpandieren |
| `_NEEDS_CLEANING` | Die Schreibweise enthält maskierte Zeilenumbrüche oder Trigraphen |
| `_LEADING_EMPTY_MACRO` | Unmittelbar davor expandierte ein leeres Makro |
| `_HAS_UCN` | Enthält einen universellen Zeichennamen |
| `_IGNORED_COMMA`, `_COMMA_AFTER_ELIDED` | Buchführung zur variadischen Kommaauslassung |
| `_STRINGIFIED_IN_MACRO` | Von `#` erzeugt |
| `_REINJECTED` | Zurück in den Tokenstrom eingespeist |

`NEVERC_TOKEN_FLAG_ALL` ist die Maske aller definierten Bits. Stapellesevorgänge
laufen über `GetTokenInfoBatch`; ein ganzer Strom wird entweder als leichte
Sicht auf `NevercTokenView`-Datensätze über `GetTokenStreamView` gelesen oder
Handle für Handle mit `GetTokenStreamToken`. Ein Strom fasst höchstens
`NEVERC_PREP_TOKEN_STREAM_MAX_TOKENS` (16.777.216) Token.

## Bezeichner und Makros

```c
NevercIdentifierHandle Identifier;
Prep->GetOrCreateIdentifier(Prep->Context, Task, SV("MY_MACRO"), &Identifier);

NevercMacroDefinitionHandle Definition;
Prep->GetMacroDefinitionForIdentifier(Prep->Context, Task, Identifier,
                                      &Definition);

NevercMacroDefinitionInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info), NEVERC_PREP_API_MAJOR,
                                     NEVERC_PREP_API_MINOR, 0};
Prep->GetMacroDefinitionInfo(Prep->Context, Task, Definition, &Info);
```

`NevercMacroDefinitionInfo` meldet den Namen, die definierende Direktive, die
Positionen von Definition, Ende und Aufhebung, die Anzahl der Parameter und
Ersatztoken sowie Flags: `NEVERC_MACRO_FUNCTION_LIKE`, `VARIADIC`,
`C99_VARIADIC`, `GNU_VARIADIC`, `HAS_VA_OPT`, `BUILTIN` und `COMMA_PASTING`.
Einzelne Parameter und Ersatztoken kommen von `GetMacroParameter` und
`GetMacroReplacementToken`.

`NevercIdentifierInfo` ergänzt Tokenart, Präprozessor-Schlüsselwortart,
Builtin-ID und Flags wie `NEVERC_IDENTIFIER_KEYWORD`, `_HAS_MACRO`,
`_POISONED` und `_RESERVED`.

An einer Expansionsstelle meldet `GetMacroArgumentInfo` die Argumentanzahl und
ob variadische Argumente ausgelassen wurden, und
`GetMacroArgumentTokenStream` liefert die Token jedes Arguments.

## Ereignisabonnement

Ein einziger Rückruf empfängt alle abonnierten Ereignisse. Die Maske wird aus
den Ereignisarten gebaut, die Sie interessieren:

```c
static NevercStatus NEVERC_CALL
on_event(NevercTaskHandle Task, const NevercPrepEvent *Event, void *UserData) {
  switch (Event->Kind) {
  case NEVERC_PREP_EVENT_INCLUSION_DIRECTIVE:
    /* Event->Payload.Include.Filename, .IsAngled, .File, .FilenameRange */
    break;
  case NEVERC_PREP_EVENT_MACRO_EXPANDS:
    /* Event->Payload.Macro.NameToken, .Definition, .Arguments, .Range */
    break;
  case NEVERC_PREP_EVENT_IFDEF:
    /* Event->Payload.Condition.Value ist NOT_EVALUATED, FALSE oder TRUE */
    break;
  default:
    break;
  }
  return neverc_status_ok();
}

NevercPrepObserverDescriptor Observer = {0};
Observer.Header = (NevercABITableHeader){sizeof(Observer),
                                         NEVERC_PREP_API_MAJOR,
                                         NEVERC_PREP_API_MINOR, 0};
Observer.Events = NEVERC_PREP_EVENT_MASK(NEVERC_PREP_EVENT_INCLUSION_DIRECTIVE) |
                  NEVERC_PREP_EVENT_MASK(NEVERC_PREP_EVENT_MACRO_EXPANDS) |
                  NEVERC_PREP_EVENT_MASK(NEVERC_PREP_EVENT_IFDEF);
Observer.Callback = on_event;
Observer.UserData = State;
Prep->RegisterEventObserver(Prep->Context, Task, &Observer);
```

`NEVERC_PREP_EVENT_MASK_ALL` abonniert alles. Die 39 Arten, gruppiert nach dem
Union-Glied der Nutzlast, das sie verwenden:

| Nutzlast | Ereignisse |
|---|---|
| `File` | `FILE_CHANGED`, `LEXED_FILE_CHANGED`, `FILE_SKIPPED`, `END_OF_MAIN_FILE` |
| `Include` | `INCLUSION_DIRECTIVE`, `FILE_NOT_FOUND`, `HAS_INCLUDE` |
| `Text` | `IDENT`, `PRAGMA_DIRECTIVE`, `PRAGMA_COMMENT`, `PRAGMA_MARK`, `PRAGMA_DETECT_MISMATCH`, `PRAGMA_DEBUG`, `PRAGMA_MESSAGE`, `PRAGMA_DIAGNOSTIC_PUSH`, `PRAGMA_DIAGNOSTIC_POP`, `PRAGMA_DIAGNOSTIC`, `PRAGMA_WARNING`, `PRAGMA_WARNING_PUSH`, `PRAGMA_WARNING_POP`, `PRAGMA_EXEC_CHARSET_PUSH`, `PRAGMA_EXEC_CHARSET_POP`, `PRAGMA_ASSUME_NONNULL_BEGIN`, `PRAGMA_ASSUME_NONNULL_END` |
| `Macro` | `MACRO_EXPANDS`, `MACRO_DEFINED`, `MACRO_UNDEFINED`, `DEFINED` |
| `Condition` | `IF`, `ELIF`, `IFDEF`, `ELIFDEF`, `ELIFDEF_SKIPPED`, `IFNDEF`, `ELIFNDEF`, `ELIFNDEF_SKIPPED`, `ELSE`, `ENDIF`, `SOURCE_RANGE_SKIPPED` |

`NevercPrepFileEvent.Reason` unterscheidet `NEVERC_PREP_FILE_ENTER`, `EXIT`,
`SYSTEM_HEADER_PRAGMA` und `RENAME`. Ereignisse sind schreibgeschützt: der
Datensatz und jede Sicht darin sind für die Dauer des Rückrufs geliehen, während
in einem Ereignis veröffentlichte Handles in den umgebenden Aufgabenbereich
hochgestuft werden.

## Ein Include umleiten

```c
NevercPrepIncludePhaseInput In = {0};
In.Header = (NevercABITableHeader){sizeof(In), NEVERC_PREP_API_MAJOR,
                                   NEVERC_PREP_API_MINOR, 0};
Prep->GetIncludePhaseInput(Prep->Context, Frame, Frame->Input, &In);

NevercPrepIncludePhaseOutput Out = {0};
Out.Header = In.Header;
if (view_equals(In.Filename, "legacy.h")) {
  Out.Action    = NEVERC_PREP_INCLUDE_REDIRECT;
  Out.Filename  = SV("modern.h");
  Out.IsAngled  = NEVERC_FALSE;
} else {
  Out.Action = NEVERC_PREP_INCLUDE_CONTINUE;
}

NevercArtifactHandle Output;
Prep->CreateIncludePhaseOutput(Prep->Context, Frame, Continuation, &Out,
                               &Output);
```

Die Aktionen sind `NEVERC_PREP_INCLUDE_CONTINUE`, `_SKIP` und `_REDIRECT`. Die
Eingabe meldet zusätzlich `IsImport` und `IsIncludeNext`, sodass sich `#import`
und `#include_next` unterscheiden lassen.

## Eine Makroexpansion ersetzen

Die Eingabe der Makrophase trägt die gerade ausgeführte Operation —
`NEVERC_PREP_MACRO_DEFINE`, `_UNDEFINE`, `_EXPAND` oder `_EXPAND_BUILTIN` —
zusammen mit dem Namenstoken, der Definition, den Argumenten und den
Ersatztoken, die der Präprozessor verwenden wollte.

```c
NevercPrepMacroPhaseOutput Out = {0};
Out.Header     = /* … */;
Out.Action     = NEVERC_PREP_MACRO_REPLACE_TOKENS;
Out.Tokens     = MyTokens;      /* const NevercTokenHandle * */
Out.TokenCount = MyTokenCount;
Prep->CreateMacroPhaseOutput(Prep->Context, Frame, Continuation, &Out, &Output);
```

`NEVERC_PREP_MACRO_CONTINUE` behält das eingebaute Verhalten bei, und
`_SUPPRESS` expandiert zu nichts.

## Token bauen

Synthetisierte Token stammen aus einem Erbauer, der die Kombination aus Art,
Schreibweise und Bezeichner vor dem Festschreiben prüft:

```c
NevercTokenBuilderHandle Builder;
Prep->CreateTokenBuilder(Prep->Context, Task, &Builder);
Prep->TokenBuilderSetLiteral(Prep->Context, Task, Builder,
                             NEVERC_TOKEN_NUMERIC_CONSTANT, SV("42"));
Prep->TokenBuilderSetLocation(Prep->Context, Task, Builder, Location);
Prep->TokenBuilderSetFlags(Prep->Context, Task, Builder,
                           NEVERC_TOKEN_FLAG_LEADING_SPACE);

NevercTokenHandle Token;
Prep->TokenBuilderCommit(Prep->Context, Task, Builder, &Token);
Prep->DestroyTokenBuilder(Prep->Context, Task, Builder);
```

Verwenden Sie `TokenBuilderSetKind` für Satzzeichen und Schlüsselwörter und
`TokenBuilderSetIdentifier` für Bezeichner. Die Tokenart-Konstanten stammen aus
[`PluginPrepSchema.inc`].

Für einen ganzen Strom — die Phase `neverc.prep.build_token_stream` — sammeln
Sie in einem Stromerbauer und schreiben einmal fest:

```c
NevercTokenStreamBuilderHandle Stream;
Prep->CreateTokenStreamBuilder(Prep->Context, Task, &Stream);
Prep->TokenStreamBuilderAppend(Prep->Context, Task, Stream, Tokens, Count);
Prep->TokenStreamBuilderCommit(Prep->Context, Frame, Stream, &Output);
Prep->DestroyTokenStreamBuilder(Prep->Context, Task, Stream);
```

Die Phaseneingabe `NevercPrepTokenStreamPhaseInput` liefert Anfangs- und
Endposition sowie ein `MaximumTokenCount`, das die Ausgabe einhalten muss.

## Pragmas und Feature-Abfragen

Die Eingabe einer Pragmaphase meldet den Einleiter (`NEVERC_PREP_PRAGMA_HASH`,
`_OPERATOR` für `_Pragma` oder `_MS` für `__pragma`), Namensraum und Namen sowie
die Argumenttoken. Die Ausgabeaktion ist `NEVERC_PREP_PRAGMA_CONTINUE`,
`_HANDLED` oder `_REPLACE_TOKENS`.

Eine Feature-Abfrage deckt `__has_feature`, `__has_extension`, `__has_builtin`,
`__has_include` und `__has_include_next` über `NEVERC_PREP_QUERY_HAS_FEATURE`
und Verwandte ab. Die Eingabe trägt den Namen und den vom Compiler berechneten
`BuiltinValue`; die Ausgabe fährt entweder fort oder ersetzt:

```c
NevercPrepFeatureQueryPhaseOutput Out = {0};
Out.Header = /* … */;
Out.Action = NEVERC_PREP_QUERY_REPLACE;
Out.Value  = NEVERC_TRUE;
Prep->CreateFeatureQueryPhaseOutput(Prep->Context, Frame, Continuation, &Out,
                                    &Output);
```

## Regeln

- Ereignisdatensätze, Zeichenkettensichten und Ganzzahlfelder sind für die Dauer
  des Rückrufs geliehen. In einem Ereignis veröffentlichte Handles leben, bis
  die Aufgabe endet.
- Jeder Erbauer braucht seinen passenden `Destroy*`-Aufruf, auch auf dem
  Fehlerpfad.
- Ein Aufruf von `Create<Kind>PhaseOutput` verlangt die Continuation der Phase,
  zu der er gehört; die Continuation einer anderen Phase zu verwenden liefert
  `NEVERC_STATUS_WRONG_SCOPE`. `TokenStreamBuilderCommit` nimmt den `Frame` der
  Phase `build_token_stream` statt einer Continuation.
- Abonnieren Sie nur Ereignisse, die Sie auch behandeln. Die Maske ist die
  Drossel — ein Plugin, das `NEVERC_PREP_EVENT_MASK_ALL` nimmt und in C filtert,
  bezahlt jeden einzelnen Rückruf.
- Präprozessor-Rückrufe laufen auf dem Aufgaben-Thread, während der Präprozessor
  mitten in der Arbeit ist. Treten Sie aus einem Rückruf nicht erneut in den
  Präprozessor ein.
- Geben Sie `NEVERC_STATUS_INVALID_ARGUMENT` zurück, wenn ein erforderlicher
  Zeiger fehlt, und lassen Sie niemals eine Ausnahme die Grenze überschreiten.

Die normativen Deklarationen stehen in [`PluginPrep.h`] und
[`Schema/PluginPrepSchema.inc`], das Schema der Tokenarten in
[`Schema/PrepSchema.json`], die sechs Präprozessor-Phasen und ihre
Richtlinien in [`Schema/PhaseSchema.json`].

<!-- reference links -->
[`PluginPrep.h`]: ../../neverc/include/neverc/Plugin/PluginPrep.h
[`PluginPrepSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginPrepSchema.inc
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginPrepSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginPrepSchema.inc
[`Schema/PrepSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PrepSchema.json
