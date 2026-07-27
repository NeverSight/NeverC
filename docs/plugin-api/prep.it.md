**Lingue**: [English](prep.md) | [简体中文](prep.zh-CN.md) | [繁體中文](prep.zh-TW.md) | [日本語](prep.ja.md) | [한국어](prep.ko.md) | [Français](prep.fr.md) | [Deutsch](prep.de.md) | [Español](prep.es.md) | [Italiano](prep.it.md) | [Русский](prep.ru.md) | [العربية](prep.ar.md)

[← ABI dei plugin NeverC](README.it.md)

# API del preprocessore dei plugin NeverC

[`PluginPrep.h`] espone il preprocessore in due modi. Una **sottoscrizione** a 39
generi di evento fornisce una traccia in sola lettura di tutto ciò che il
preprocessore fa: ingresso in un file, definizione ed espansione di macro,
valutazione di condizionali, pragma. Sei **fasi** vanno oltre e lasciano
riscrivere il risultato: reindirizzare un `#include`, sostituire i token di
espansione di una macro, gestire un pragma da soli, o rispondere diversamente a
`__has_feature`.

## Interfaccia

```c
#include "neverc/Plugin/PluginPrep.h"

Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_PREP_HIGH, NEVERC_INTERFACE_PREP_LOW},
    NEVERC_PREP_API_MAJOR, NEVERC_PREP_API_MINOR, &Table, &Minor, &TableSize);
```

I 230 generi di token (`NEVERC_TOKEN_KIND_COUNT`) e i generi di parola chiave del
preprocessore provengono da [`Schema/PluginPrepSchema.inc`], che l'header include e
il cui major di capacità deve essere uguale a `NEVERC_PREP_API_MAJOR`: una
discrepanza è un errore di compilazione, non una sorpresa a runtime. Ogni genere
porta anche una categoria: `NEVERC_TOKEN_CATEGORY_SPECIAL`, `COMMENT`,
`IDENTIFIER`, `LITERAL`, `PUNCTUATOR`, `KEYWORD` o `ANNOTATION`.

## Le sei fasi del preprocessore

| Fase | Politica | Ingresso → uscita |
|---|---|---|
| `neverc.prep.token` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE | un token → lista di token |
| `neverc.prep.build_token_stream` | come sopra | intervallo → flusso di token |
| `neverc.prep.include.intercept` | come sopra | richiesta di inclusione → decisione di inclusione |
| `neverc.prep.macro.intercept` | come sopra | operazione di macro → azione + token |
| `neverc.prep.pragma.intercept` | come sopra | pragma → azione + token |
| `neverc.prep.feature_query.intercept` | come sopra | interrogazione `__has_*` → valore |

Cinque delle sei espongono su `NevercPrepAPI` una coppia `Get<Kind>PhaseInput` e
`Create<Kind>PhaseOutput`; la metà `Create` prende la
`NevercPhaseContinuation` dell'intercettore, così un'uscita può essere prodotta
solo dall'interno della fase che la possiede. `neverc.prep.build_token_stream` è
l'eccezione: ha `GetTokenStreamPhaseInput` e pubblica tramite
`TokenStreamBuilderCommit` sul `Frame` della fase, non un `Create*PhaseOutput`
che prende una continuation.

## Leggere i token

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

`Origin` vale `NEVERC_TOKEN_ORIGIN_FILE`, `MACRO_REPLACEMENT`,
`MACRO_ARGUMENT` o `SYNTHESIZED`: è così che si distingue un token digitato
dall'utente da uno prodotto da una macro.

I flag sono la contabilità interna del preprocessore e contano quando si
sintetizzano token:

| Flag | Significato |
|---|---|
| `NEVERC_TOKEN_FLAG_START_OF_LINE` | Primo token della sua riga |
| `_LEADING_SPACE` | È preceduto da spazio bianco |
| `_DISABLE_EXPANSION` | Non espandere per macro questo token |
| `_NEEDS_CLEANING` | La grafia contiene ritorni a capo con escape o trigrafi |
| `_LEADING_EMPTY_MACRO` | Subito prima si è espansa una macro vuota |
| `_HAS_UCN` | Contiene un nome di carattere universale |
| `_IGNORED_COMMA`, `_COMMA_AFTER_ELIDED` | Contabilità dell'elisione della virgola variadica |
| `_STRINGIFIED_IN_MACRO` | Prodotto da `#` |
| `_REINJECTED` | Reimmesso nel flusso di token |

`NEVERC_TOKEN_FLAG_ALL` è la maschera di tutti i bit definiti. Le letture a lotti
usano `GetTokenInfoBatch`; un intero flusso si legge o come vista leggera di
record `NevercTokenView` tramite `GetTokenStreamView`, oppure un handle alla
volta con `GetTokenStreamToken`. Un flusso contiene al massimo
`NEVERC_PREP_TOKEN_STREAM_MAX_TOKENS` (16.777.216) token.

## Identificatori e macro

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

`NevercMacroDefinitionInfo` riporta il nome, la direttiva che la definisce, le
posizioni di definizione, fine e annullamento, il numero di parametri e di token
di sostituzione, e i flag: `NEVERC_MACRO_FUNCTION_LIKE`, `VARIADIC`,
`C99_VARIADIC`, `GNU_VARIADIC`, `HAS_VA_OPT`, `BUILTIN` e `COMMA_PASTING`. I
singoli parametri e token di sostituzione arrivano da `GetMacroParameter` e
`GetMacroReplacementToken`.

`NevercIdentifierInfo` aggiunge il genere di token, il genere di parola chiave
del preprocessore, l'ID di intrinseca e flag come
`NEVERC_IDENTIFIER_KEYWORD`, `_HAS_MACRO`, `_POISONED` e `_RESERVED`.

Nel punto di espansione, `GetMacroArgumentInfo` riporta il numero di argomenti e
se i variadici siano stati elisi, mentre `GetMacroArgumentTokenStream` produce i
token di ciascun argomento.

## Sottoscrizione agli eventi

Una sola callback riceve tutti gli eventi sottoscritti. La maschera si costruisce
dai generi che vi interessano:

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
    /* Event->Payload.Condition.Value è NOT_EVALUATED, FALSE o TRUE */
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

`NEVERC_PREP_EVENT_MASK_ALL` sottoscrive tutto. I 39 generi, raggruppati per il
membro dell'unione di payload che usano:

| Payload | Eventi |
|---|---|
| `File` | `FILE_CHANGED`, `LEXED_FILE_CHANGED`, `FILE_SKIPPED`, `END_OF_MAIN_FILE` |
| `Include` | `INCLUSION_DIRECTIVE`, `FILE_NOT_FOUND`, `HAS_INCLUDE` |
| `Text` | `IDENT`, `PRAGMA_DIRECTIVE`, `PRAGMA_COMMENT`, `PRAGMA_MARK`, `PRAGMA_DETECT_MISMATCH`, `PRAGMA_DEBUG`, `PRAGMA_MESSAGE`, `PRAGMA_DIAGNOSTIC_PUSH`, `PRAGMA_DIAGNOSTIC_POP`, `PRAGMA_DIAGNOSTIC`, `PRAGMA_WARNING`, `PRAGMA_WARNING_PUSH`, `PRAGMA_WARNING_POP`, `PRAGMA_EXEC_CHARSET_PUSH`, `PRAGMA_EXEC_CHARSET_POP`, `PRAGMA_ASSUME_NONNULL_BEGIN`, `PRAGMA_ASSUME_NONNULL_END` |
| `Macro` | `MACRO_EXPANDS`, `MACRO_DEFINED`, `MACRO_UNDEFINED`, `DEFINED` |
| `Condition` | `IF`, `ELIF`, `IFDEF`, `ELIFDEF`, `ELIFDEF_SKIPPED`, `IFNDEF`, `ELIFNDEF`, `ELIFNDEF_SKIPPED`, `ELSE`, `ENDIF`, `SOURCE_RANGE_SKIPPED` |

`NevercPrepFileEvent.Reason` distingue `NEVERC_PREP_FILE_ENTER`, `EXIT`,
`SYSTEM_HEADER_PRAGMA` e `RENAME`. Gli eventi sono in sola lettura: il record e
ogni vista al suo interno sono prestati per la durata della callback, mentre gli
handle pubblicati in un evento vengono promossi all'ambito del task
circostante.

## Reindirizzare un'inclusione

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

Le azioni sono `NEVERC_PREP_INCLUDE_CONTINUE`, `_SKIP` e `_REDIRECT`. L'ingresso
riporta anche `IsImport` e `IsIncludeNext`, così `#import` e `#include_next`
restano distinguibili.

## Sostituire un'espansione di macro

L'ingresso della fase macro porta l'operazione in corso —
`NEVERC_PREP_MACRO_DEFINE`, `_UNDEFINE`, `_EXPAND` o `_EXPAND_BUILTIN` —
insieme al token del nome, alla definizione, agli argomenti e ai token di
sostituzione che il preprocessore stava per usare.

```c
NevercPrepMacroPhaseOutput Out = {0};
Out.Header     = /* … */;
Out.Action     = NEVERC_PREP_MACRO_REPLACE_TOKENS;
Out.Tokens     = MyTokens;      /* const NevercTokenHandle * */
Out.TokenCount = MyTokenCount;
Prep->CreateMacroPhaseOutput(Prep->Context, Frame, Continuation, &Out, &Output);
```

`NEVERC_PREP_MACRO_CONTINUE` mantiene il comportamento nativo e `_SUPPRESS` si
espande in nulla.

## Costruire token

I token sintetizzati nascono da un builder, che valida la combinazione di
genere, grafia e identificatore prima di confermare:

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

Usate `TokenBuilderSetKind` per i segni di punteggiatura e le parole chiave, e
`TokenBuilderSetIdentifier` per gli identificatori. Le costanti di genere token
provengono da [`PluginPrepSchema.inc`].

Per un flusso intero — la fase `neverc.prep.build_token_stream` — accumulate in
un builder di flusso e confermate una volta sola:

```c
NevercTokenStreamBuilderHandle Stream;
Prep->CreateTokenStreamBuilder(Prep->Context, Task, &Stream);
Prep->TokenStreamBuilderAppend(Prep->Context, Task, Stream, Tokens, Count);
Prep->TokenStreamBuilderCommit(Prep->Context, Frame, Stream, &Output);
Prep->DestroyTokenStreamBuilder(Prep->Context, Task, Stream);
```

L'ingresso di fase, `NevercPrepTokenStreamPhaseInput`, fornisce le posizioni di
inizio e fine e un `MaximumTokenCount` che l'uscita deve rispettare.

## Pragma e interrogazioni di funzionalità

L'ingresso di una fase pragma riporta l'introduttore
(`NEVERC_PREP_PRAGMA_HASH`, `_OPERATOR` per `_Pragma`, o `_MS` per `__pragma`),
lo spazio dei nomi e il nome, e i token degli argomenti. L'azione di uscita è
`NEVERC_PREP_PRAGMA_CONTINUE`, `_HANDLED` o `_REPLACE_TOKENS`.

Un'interrogazione di funzionalità copre `__has_feature`, `__has_extension`,
`__has_builtin`, `__has_include` e `__has_include_next` tramite
`NEVERC_PREP_QUERY_HAS_FEATURE` e affini. L'ingresso porta il nome e il
`BuiltinValue` calcolato dal compilatore; l'uscita continua oppure sostituisce:

```c
NevercPrepFeatureQueryPhaseOutput Out = {0};
Out.Header = /* … */;
Out.Action = NEVERC_PREP_QUERY_REPLACE;
Out.Value  = NEVERC_TRUE;
Prep->CreateFeatureQueryPhaseOutput(Prep->Context, Frame, Continuation, &Out,
                                    &Output);
```

## Regole

- I record di evento, le viste di stringa e gli array di interi sono prestati per
  la durata della callback. Gli handle pubblicati in un evento vivono fino alla
  fine del task.
- Ogni builder richiede la corrispondente chiamata `Destroy*`, anche sul percorso
  di errore.
- Una chiamata a `Create<Kind>PhaseOutput` richiede la continuation della fase a
  cui appartiene; usare quella di un'altra fase restituisce
  `NEVERC_STATUS_WRONG_SCOPE`. `TokenStreamBuilderCommit` prende il `Frame`
  della fase `build_token_stream` invece di una continuation.
- Sottoscrivete solo gli eventi che gestite. La maschera è la valvola: un plugin
  che prende `NEVERC_PREP_EVENT_MASK_ALL` e poi filtra in C paga ogni singola
  callback.
- Le callback del preprocessore girano sul thread del task mentre il
  preprocessore è in pieno lavoro. Non rientrate nel preprocessore da una di
  esse.
- Restituite `NEVERC_STATUS_INVALID_ARGUMENT` per un puntatore obbligatorio
  mancante, e non lasciate mai che un'eccezione attraversi il confine.

Vedere [`PluginPrep.h`] e [`Schema/PluginPrepSchema.inc`] per le dichiarazioni
normative, [`Schema/PrepSchema.json`] per lo schema dei generi di token, e
[`Schema/PhaseSchema.json`] per le sei fasi del preprocessore e le loro policy.

<!-- reference links -->
[`PluginPrep.h`]: ../../neverc/include/neverc/Plugin/PluginPrep.h
[`PluginPrepSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginPrepSchema.inc
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginPrepSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginPrepSchema.inc
[`Schema/PrepSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PrepSchema.json
