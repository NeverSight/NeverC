**Idiomas**: [English](prep.md) | [简体中文](prep.zh-CN.md) | [繁體中文](prep.zh-TW.md) | [日本語](prep.ja.md) | [한국어](prep.ko.md) | [Français](prep.fr.md) | [Deutsch](prep.de.md) | [Español](prep.es.md) | [Italiano](prep.it.md) | [Русский](prep.ru.md) | [العربية](prep.ar.md)

[← ABI de complementos de NeverC](README.es.md)

# API del preprocesador de los plugins de NeverC

[`PluginPrep.h`] expone el preprocesador de dos maneras. Una **suscripción** a 39
géneros de eventos ofrece una traza de solo lectura de todo lo que hace el
preprocesador: entrada en un archivo, definición y expansión de macros,
evaluación de condicionales, pragmas. Seis **fases** van más allá y permiten
reescribir el resultado: redirigir un `#include`, sustituir los tokens de
expansión de una macro, atender un pragma uno mismo o responder de otro modo a
`__has_feature`.

## Interfaz

```c
#include "neverc/Plugin/PluginPrep.h"

Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_PREP_HIGH, NEVERC_INTERFACE_PREP_LOW},
    NEVERC_PREP_API_MAJOR, NEVERC_PREP_API_MINOR, &Table, &Minor, &TableSize);
```

Los 230 géneros de token (`NEVERC_TOKEN_KIND_COUNT`) y los géneros de palabra
clave del preprocesador provienen de [`Schema/PluginPrepSchema.inc`], que la
cabecera incluye y cuyo mayor de capacidad debe ser igual a
`NEVERC_PREP_API_MAJOR`: un desajuste es un error de compilación, no una
sorpresa en tiempo de ejecución. Cada género lleva además una categoría:
`NEVERC_TOKEN_CATEGORY_SPECIAL`, `COMMENT`, `IDENTIFIER`, `LITERAL`,
`PUNCTUATOR`, `KEYWORD` o `ANNOTATION`.

## Las seis fases del preprocesador

| Fase | Política | Entrada → salida |
|---|---|---|
| `neverc.prep.token` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE | un token → lista de tokens |
| `neverc.prep.build_token_stream` | igual | rango → flujo de tokens |
| `neverc.prep.include.intercept` | igual | petición de inclusión → decisión de inclusión |
| `neverc.prep.macro.intercept` | igual | operación de macro → acción + tokens |
| `neverc.prep.pragma.intercept` | igual | pragma → acción + tokens |
| `neverc.prep.feature_query.intercept` | igual | consulta `__has_*` → valor |

Cada una tiene en `NevercPrepAPI` una pareja `Get<Kind>PhaseInput` y
`Create<Kind>PhaseOutput`; la mitad `Create` toma la
`NevercPhaseContinuation` del interceptor, de modo que una salida solo puede
producirse desde dentro de la fase que la posee.

## Leer tokens

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

`Origin` es `NEVERC_TOKEN_ORIGIN_FILE`, `MACRO_REPLACEMENT`, `MACRO_ARGUMENT`
o `SYNTHESIZED`, que es como se distingue un token escrito por el usuario de
otro producido por una macro.

Las banderas son la contabilidad propia del preprocesador y cobran importancia
cuando uno sintetiza tokens:

| Bandera | Significado |
|---|---|
| `NEVERC_TOKEN_FLAG_START_OF_LINE` | Primer token de su línea |
| `_LEADING_SPACE` | Le precede un espacio en blanco |
| `_DISABLE_EXPANSION` | No expandir por macro este token |
| `_NEEDS_CLEANING` | La grafía contiene saltos de línea escapados o trigrafos |
| `_LEADING_EMPTY_MACRO` | Justo antes se expandió una macro vacía |
| `_HAS_UCN` | Contiene un nombre de carácter universal |
| `_IGNORED_COMMA`, `_COMMA_AFTER_ELIDED` | Contabilidad de la elisión de coma variádica |
| `_STRINGIFIED_IN_MACRO` | Producido por `#` |
| `_REINJECTED` | Reinyectado en el flujo de tokens |

`NEVERC_TOKEN_FLAG_ALL` es la máscara de todos los bits definidos. Las lecturas
por lotes usan `GetTokenInfoBatch`; un flujo entero se lee o bien como una vista
ligera de registros `NevercTokenView` mediante `GetTokenStreamView`, o bien un
descriptor cada vez con `GetTokenStreamToken`. Un flujo alberga como mucho
`NEVERC_PREP_TOKEN_STREAM_MAX_TOKENS` (16 777 216) tokens.

## Identificadores y macros

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

`NevercMacroDefinitionInfo` informa del nombre, la directiva que la define, las
ubicaciones de definición, fin y anulación, el número de parámetros y de tokens
de reemplazo, y banderas: `NEVERC_MACRO_FUNCTION_LIKE`, `VARIADIC`,
`C99_VARIADIC`, `GNU_VARIADIC`, `HAS_VA_OPT`, `BUILTIN` y `COMMA_PASTING`. Los
parámetros y tokens de reemplazo individuales vienen de `GetMacroParameter` y
`GetMacroReplacementToken`.

`NevercIdentifierInfo` añade el género de token, el género de palabra clave del
preprocesador, el identificador de intrínseca y banderas como
`NEVERC_IDENTIFIER_KEYWORD`, `_HAS_MACRO`, `_POISONED` y `_RESERVED`.

En un punto de expansión, `GetMacroArgumentInfo` informa del número de
argumentos y de si se elidieron los variádicos, y
`GetMacroArgumentTokenStream` entrega los tokens de cada argumento.

## Suscripción a eventos

Una sola devolución de llamada recibe todos los eventos suscritos. La máscara se
construye con los géneros que le importan:

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
    /* Event->Payload.Condition.Value es NOT_EVALUATED, FALSE o TRUE */
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

`NEVERC_PREP_EVENT_MASK_ALL` se suscribe a todo. Los 39 géneros, agrupados por
el miembro de unión de carga útil que emplean:

| Carga útil | Eventos |
|---|---|
| `File` | `FILE_CHANGED`, `LEXED_FILE_CHANGED`, `FILE_SKIPPED`, `END_OF_MAIN_FILE` |
| `Include` | `INCLUSION_DIRECTIVE`, `FILE_NOT_FOUND`, `HAS_INCLUDE` |
| `Text` | `IDENT`, `PRAGMA_DIRECTIVE`, `PRAGMA_COMMENT`, `PRAGMA_MARK`, `PRAGMA_DETECT_MISMATCH`, `PRAGMA_DEBUG`, `PRAGMA_MESSAGE`, `PRAGMA_DIAGNOSTIC_PUSH`, `PRAGMA_DIAGNOSTIC_POP`, `PRAGMA_DIAGNOSTIC`, `PRAGMA_WARNING`, `PRAGMA_WARNING_PUSH`, `PRAGMA_WARNING_POP`, `PRAGMA_EXEC_CHARSET_PUSH`, `PRAGMA_EXEC_CHARSET_POP`, `PRAGMA_ASSUME_NONNULL_BEGIN`, `PRAGMA_ASSUME_NONNULL_END` |
| `Macro` | `MACRO_EXPANDS`, `MACRO_DEFINED`, `MACRO_UNDEFINED`, `DEFINED` |
| `Condition` | `IF`, `ELIF`, `IFDEF`, `ELIFDEF`, `ELIFDEF_SKIPPED`, `IFNDEF`, `ELIFNDEF`, `ELIFNDEF_SKIPPED`, `ELSE`, `ENDIF`, `SOURCE_RANGE_SKIPPED` |

`NevercPrepFileEvent.Reason` distingue `NEVERC_PREP_FILE_ENTER`, `EXIT`,
`SYSTEM_HEADER_PRAGMA` y `RENAME`. Los eventos son de solo lectura: el registro
y cada vista que contiene están prestados durante la devolución de llamada,
mientras que los descriptores publicados en un evento se promueven al alcance de
la tarea envolvente.

## Redirigir una inclusión

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

Las acciones son `NEVERC_PREP_INCLUDE_CONTINUE`, `_SKIP` y `_REDIRECT`. La
entrada informa además de `IsImport` e `IsIncludeNext`, de modo que `#import` e
`#include_next` se pueden distinguir.

## Sustituir una expansión de macro

La entrada de la fase de macro lleva la operación en curso —
`NEVERC_PREP_MACRO_DEFINE`, `_UNDEFINE`, `_EXPAND` o `_EXPAND_BUILTIN` — junto
con el token del nombre, la definición, los argumentos y los tokens de
reemplazo que el preprocesador iba a usar.

```c
NevercPrepMacroPhaseOutput Out = {0};
Out.Header     = /* … */;
Out.Action     = NEVERC_PREP_MACRO_REPLACE_TOKENS;
Out.Tokens     = MyTokens;      /* const NevercTokenHandle * */
Out.TokenCount = MyTokenCount;
Prep->CreateMacroPhaseOutput(Prep->Context, Frame, Continuation, &Out, &Output);
```

`NEVERC_PREP_MACRO_CONTINUE` conserva el comportamiento nativo y `_SUPPRESS` se
expande a nada.

## Construir tokens

Los tokens sintetizados salen de un constructor, que valida la combinación de
género, grafía e identificador antes de confirmar:

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

Use `TokenBuilderSetKind` para signos de puntuación y palabras clave, y
`TokenBuilderSetIdentifier` para identificadores. Las constantes de género de
token vienen de [`PluginPrepSchema.inc`].

Para un flujo entero —la fase `neverc.prep.build_token_stream`— acumule en un
constructor de flujos y confirme una sola vez:

```c
NevercTokenStreamBuilderHandle Stream;
Prep->CreateTokenStreamBuilder(Prep->Context, Task, &Stream);
Prep->TokenStreamBuilderAppend(Prep->Context, Task, Stream, Tokens, Count);
Prep->TokenStreamBuilderCommit(Prep->Context, Frame, Stream, &Output);
Prep->DestroyTokenStreamBuilder(Prep->Context, Task, Stream);
```

La entrada de fase, `NevercPrepTokenStreamPhaseInput`, da las ubicaciones de
inicio y fin y un `MaximumTokenCount` que la salida debe respetar.

## Pragmas y consultas de características

La entrada de una fase de pragma informa del introductor
(`NEVERC_PREP_PRAGMA_HASH`, `_OPERATOR` para `_Pragma`, o `_MS` para
`__pragma`), del espacio de nombres y el nombre, y de los tokens de argumento.
La acción de salida es `NEVERC_PREP_PRAGMA_CONTINUE`, `_HANDLED` o
`_REPLACE_TOKENS`.

Una consulta de característica cubre `__has_feature`, `__has_extension`,
`__has_builtin`, `__has_include` y `__has_include_next` mediante
`NEVERC_PREP_QUERY_HAS_FEATURE` y compañía. La entrada lleva el nombre y el
`BuiltinValue` que calculó el compilador; la salida continúa o sustituye:

```c
NevercPrepFeatureQueryPhaseOutput Out = {0};
Out.Header = /* … */;
Out.Action = NEVERC_PREP_QUERY_REPLACE;
Out.Value  = NEVERC_TRUE;
Prep->CreateFeatureQueryPhaseOutput(Prep->Context, Frame, Continuation, &Out,
                                    &Output);
```

## Reglas

- Los registros de evento, las vistas de cadena y los arreglos de enteros están
  prestados durante la devolución de llamada. Los descriptores publicados en un
  evento viven hasta que termina la tarea.
- Cada constructor necesita su llamada `Destroy*` correspondiente, también en la
  ruta de error.
- Una llamada a `Create<Kind>PhaseOutput` requiere la continuación de la fase a
  la que pertenece; usar la de otra fase devuelve
  `NEVERC_STATUS_WRONG_SCOPE`.
- Suscríbase solo a los eventos que atiende. La máscara es el regulador: un
  plugin que toma `NEVERC_PREP_EVENT_MASK_ALL` y filtra en C paga cada
  devolución de llamada.
- Las devoluciones de llamada del preprocesador se ejecutan en el hilo de la
  tarea mientras el preprocesador está en pleno vuelo. No vuelva a entrar en el
  preprocesador desde una de ellas.
- Devuelva `NEVERC_STATUS_INVALID_ARGUMENT` cuando falte un puntero obligatorio,
  y nunca deje que una excepción cruce la frontera.

Consulte [`PluginPrep.h`] y [`Schema/PluginPrepSchema.inc`] para las declaraciones
normativas, [`Schema/PrepSchema.json`] para el esquema de géneros de token, y
[`Schema/PhaseSchema.json`] para las seis fases del preprocesador y sus
políticas.

<!-- reference links -->
[`PluginPrep.h`]: ../../neverc/include/neverc/Plugin/PluginPrep.h
[`PluginPrepSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginPrepSchema.inc
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginPrepSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginPrepSchema.inc
[`Schema/PrepSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PrepSchema.json
