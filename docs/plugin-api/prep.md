**Languages**: [English](prep.md) | [简体中文](prep.zh-CN.md) | [繁體中文](prep.zh-TW.md) | [日本語](prep.ja.md) | [한국어](prep.ko.md) | [Français](prep.fr.md) | [Deutsch](prep.de.md) | [Español](prep.es.md) | [Italiano](prep.it.md) | [Русский](prep.ru.md) | [العربية](prep.ar.md)

# NeverC Plugin Preprocessor API

`PluginPrep.h` exposes the preprocessor two ways. A **subscription** to 39
event kinds gives you a read-only trace of everything the preprocessor does —
file entry, macro definition and expansion, conditional evaluation, pragmas.
Six **phases** go further and let you rewrite the result: redirect an
`#include`, replace a macro's expansion tokens, handle a pragma yourself, or
answer `__has_feature` differently.

## Interface

```c
#include "neverc/Plugin/PluginPrep.h"

Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_PREP_HIGH, NEVERC_INTERFACE_PREP_LOW},
    NEVERC_PREP_API_MAJOR, NEVERC_PREP_API_MINOR, &Table, &Minor, &TableSize);
```

The 230 token kinds (`NEVERC_TOKEN_KIND_COUNT`) and the preprocessor keyword
kinds come from `Schema/PluginPrepSchema.inc`, which the header includes and
whose capability major must equal `NEVERC_PREP_API_MAJOR` — a mismatch is a
compile error, not a runtime surprise. Each kind also carries a category:
`NEVERC_TOKEN_CATEGORY_SPECIAL`, `COMMENT`, `IDENTIFIER`, `LITERAL`,
`PUNCTUATOR`, `KEYWORD`, or `ANNOTATION`.

## The six preprocessor phases

| Phase | Policy | Input → output |
|---|---|---|
| `neverc.prep.token` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE | one token → token list |
| `neverc.prep.build_token_stream` | same | range → token stream |
| `neverc.prep.include.intercept` | same | include request → include decision |
| `neverc.prep.macro.intercept` | same | macro operation → action + tokens |
| `neverc.prep.pragma.intercept` | same | pragma → action + tokens |
| `neverc.prep.feature_query.intercept` | same | `__has_*` query → value |

Each has a paired `Get<Kind>PhaseInput` and `Create<Kind>PhaseOutput` on
`NevercPrepAPI`, and the `Create` half takes the interceptor's
`NevercPhaseContinuation`, so an output can only be produced from inside the
phase that owns it.

## Reading tokens

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

`Origin` is `NEVERC_TOKEN_ORIGIN_FILE`, `MACRO_REPLACEMENT`,
`MACRO_ARGUMENT`, or `SYNTHESIZED`, which is how you tell a token the user
typed from one a macro produced.

The flags are the preprocessor's own bookkeeping and matter when you
synthesize tokens:

| Flag | Meaning |
|---|---|
| `NEVERC_TOKEN_FLAG_START_OF_LINE` | First token on its line |
| `_LEADING_SPACE` | Whitespace precedes it |
| `_DISABLE_EXPANSION` | Do not macro-expand this token |
| `_NEEDS_CLEANING` | Spelling contains escaped newlines or trigraphs |
| `_LEADING_EMPTY_MACRO` | An empty macro expanded just before it |
| `_HAS_UCN` | Contains a universal character name |
| `_IGNORED_COMMA`, `_COMMA_AFTER_ELIDED` | Variadic comma elision bookkeeping |
| `_STRINGIFIED_IN_MACRO` | Produced by `#` |
| `_REINJECTED` | Fed back into the token stream |

`NEVERC_TOKEN_FLAG_ALL` is the mask of all defined bits. Batch reads use
`GetTokenInfoBatch`; a whole stream is read either as a lightweight view of
`NevercTokenView` records via `GetTokenStreamView` or one handle at a time
with `GetTokenStreamToken`. A stream holds at most
`NEVERC_PREP_TOKEN_STREAM_MAX_TOKENS` (16,777,216) tokens.

## Identifiers and macros

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

`NevercMacroDefinitionInfo` reports the name, the defining directive, the
definition/end/undefinition locations, the parameter and replacement-token
counts, and flags: `NEVERC_MACRO_FUNCTION_LIKE`, `VARIADIC`, `C99_VARIADIC`,
`GNU_VARIADIC`, `HAS_VA_OPT`, `BUILTIN`, and `COMMA_PASTING`. Individual
parameters and replacement tokens come from `GetMacroParameter` and
`GetMacroReplacementToken`.

`NevercIdentifierInfo` adds the token kind, preprocessor keyword kind, builtin
ID, and flags such as `NEVERC_IDENTIFIER_KEYWORD`, `_HAS_MACRO`,
`_POISONED`, and `_RESERVED`.

At an expansion site, `GetMacroArgumentInfo` reports the argument count and
whether varargs were elided, and `GetMacroArgumentTokenStream` yields each
argument's tokens.

## Event subscription

One callback receives all subscribed events. The mask is built from the event
kinds you care about:

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
    /* Event->Payload.Condition.Value is NOT_EVALUATED, FALSE, or TRUE */
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

`NEVERC_PREP_EVENT_MASK_ALL` subscribes to everything. The 39 kinds, grouped
by the payload union member they use:

| Payload | Events |
|---|---|
| `File` | `FILE_CHANGED`, `LEXED_FILE_CHANGED`, `FILE_SKIPPED`, `FILE_NOT_FOUND`, `END_OF_MAIN_FILE` |
| `Include` | `INCLUSION_DIRECTIVE` |
| `Text` | `IDENT`, `PRAGMA_DIRECTIVE`, `PRAGMA_COMMENT`, `PRAGMA_MARK`, `PRAGMA_DETECT_MISMATCH`, `PRAGMA_DEBUG`, `PRAGMA_MESSAGE`, `PRAGMA_DIAGNOSTIC_PUSH`, `PRAGMA_DIAGNOSTIC_POP`, `PRAGMA_DIAGNOSTIC`, `PRAGMA_WARNING`, `PRAGMA_WARNING_PUSH`, `PRAGMA_WARNING_POP`, `PRAGMA_EXEC_CHARSET_PUSH`, `PRAGMA_EXEC_CHARSET_POP`, `PRAGMA_ASSUME_NONNULL_BEGIN`, `PRAGMA_ASSUME_NONNULL_END`, `SOURCE_RANGE_SKIPPED` |
| `Macro` | `MACRO_EXPANDS`, `MACRO_DEFINED`, `MACRO_UNDEFINED`, `DEFINED`, `HAS_INCLUDE` |
| `Condition` | `IF`, `ELIF`, `IFDEF`, `ELIFDEF`, `ELIFDEF_SKIPPED`, `IFNDEF`, `ELIFNDEF`, `ELIFNDEF_SKIPPED`, `ELSE`, `ENDIF` |

`NevercPrepFileEvent.Reason` distinguishes `NEVERC_PREP_FILE_ENTER`, `EXIT`,
`SYSTEM_HEADER_PRAGMA`, and `RENAME`. Events are read-only: the record and
every view inside it are borrowed for the callback, while handles published in
an event are promoted to the enclosing task scope.

## Redirecting an include

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

Actions are `NEVERC_PREP_INCLUDE_CONTINUE`, `_SKIP`, and `_REDIRECT`. The
input also reports `IsImport` and `IsIncludeNext`, so `#import` and
`#include_next` are distinguishable.

## Replacing a macro expansion

The macro phase input carries the operation being performed —
`NEVERC_PREP_MACRO_DEFINE`, `_UNDEFINE`, `_EXPAND`, or `_EXPAND_BUILTIN` —
together with the name token, definition, arguments, and the replacement
tokens the preprocessor was going to use.

```c
NevercPrepMacroPhaseOutput Out = {0};
Out.Header     = /* … */;
Out.Action     = NEVERC_PREP_MACRO_REPLACE_TOKENS;
Out.Tokens     = MyTokens;      /* const NevercTokenHandle * */
Out.TokenCount = MyTokenCount;
Prep->CreateMacroPhaseOutput(Prep->Context, Frame, Continuation, &Out, &Output);
```

`NEVERC_PREP_MACRO_CONTINUE` keeps the built-in behaviour and
`_SUPPRESS` expands to nothing.

## Building tokens

Synthesized tokens come from a builder, which validates the combination of
kind, spelling, and identifier before committing:

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

Use `TokenBuilderSetKind` for punctuators and keywords and
`TokenBuilderSetIdentifier` for identifiers. Token-kind constants come from
`PluginPrepSchema.inc`.

For a whole stream — the `neverc.prep.build_token_stream` phase — accumulate
into a stream builder and commit once:

```c
NevercTokenStreamBuilderHandle Stream;
Prep->CreateTokenStreamBuilder(Prep->Context, Task, &Stream);
Prep->TokenStreamBuilderAppend(Prep->Context, Task, Stream, Tokens, Count);
Prep->TokenStreamBuilderCommit(Prep->Context, Frame, Stream, &Output);
Prep->DestroyTokenStreamBuilder(Prep->Context, Task, Stream);
```

The phase input, `NevercPrepTokenStreamPhaseInput`, gives the start and end
locations and a `MaximumTokenCount` the output must respect.

## Pragmas and feature queries

A pragma phase input reports the introducer (`NEVERC_PREP_PRAGMA_HASH`,
`_OPERATOR` for `_Pragma`, or `_MS` for `__pragma`), the namespace and name,
and the argument tokens. The output action is
`NEVERC_PREP_PRAGMA_CONTINUE`, `_HANDLED`, or `_REPLACE_TOKENS`.

A feature query covers `__has_feature`, `__has_extension`, `__has_builtin`,
`__has_include`, and `__has_include_next` via
`NEVERC_PREP_QUERY_HAS_FEATURE` and friends. The input carries the name and
the `BuiltinValue` the compiler computed; the output either continues or
replaces:

```c
NevercPrepFeatureQueryPhaseOutput Out = {0};
Out.Header = /* … */;
Out.Action = NEVERC_PREP_QUERY_REPLACE;
Out.Value  = NEVERC_TRUE;
Prep->CreateFeatureQueryPhaseOutput(Prep->Context, Frame, Continuation, &Out,
                                    &Output);
```

## Rules

- Event records, string views, and integer arrays are borrowed for the
  callback. Handles published in an event live until the task ends.
- Every builder needs its matching `Destroy*` call, including on the error
  path.
- A `Create<Kind>PhaseOutput` call requires the continuation of the phase it
  belongs to; using another phase's continuation returns
  `NEVERC_STATUS_WRONG_SCOPE`.
- Subscribe only to the events you handle. The mask is the throttle — a
  plugin that takes `NEVERC_PREP_EVENT_MASK_ALL` and filters in C pays for
  every callback.
- Preprocessor callbacks run on the task thread while the preprocessor is
  mid-flight. Do not re-enter the preprocessor from one.
- Return `NEVERC_STATUS_INVALID_ARGUMENT` for a missing required pointer, and
  never let an exception cross the boundary.

See `PluginPrep.h` and `Schema/PluginPrepSchema.inc` for the normative
declarations, and `Schema/PrepSchema.json` for the token-kind schema.
