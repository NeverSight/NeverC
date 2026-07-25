**Langues**: [English](prep.md) | [简体中文](prep.zh-CN.md) | [繁體中文](prep.zh-TW.md) | [日本語](prep.ja.md) | [한국어](prep.ko.md) | [Français](prep.fr.md) | [Deutsch](prep.de.md) | [Español](prep.es.md) | [Italiano](prep.it.md) | [Русский](prep.ru.md) | [العربية](prep.ar.md)

# API du préprocesseur des plugins NeverC

`PluginPrep.h` expose le préprocesseur de deux manières. Un **abonnement** à 39
genres d'événements vous donne une trace en lecture seule de tout ce que fait le
préprocesseur : entrée dans un fichier, définition et expansion de macro,
évaluation de conditions, pragmas. Six **phases** vont plus loin et vous
laissent réécrire le résultat : rediriger un `#include`, remplacer les jetons
d'expansion d'une macro, traiter un pragma vous-même, ou répondre autrement à
`__has_feature`.

## Interface

```c
#include "neverc/Plugin/PluginPrep.h"

Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_PREP_HIGH, NEVERC_INTERFACE_PREP_LOW},
    NEVERC_PREP_API_MAJOR, NEVERC_PREP_API_MINOR, &Table, &Minor, &TableSize);
```

Les 230 genres de jetons (`NEVERC_TOKEN_KIND_COUNT`) et les genres de mots-clés
du préprocesseur proviennent de `Schema/PluginPrepSchema.inc`, que l'en-tête
inclut et dont le majeur de capacité doit être égal à `NEVERC_PREP_API_MAJOR` —
un désaccord est une erreur de compilation, non une surprise à l'exécution.
Chaque genre porte aussi une catégorie : `NEVERC_TOKEN_CATEGORY_SPECIAL`,
`COMMENT`, `IDENTIFIER`, `LITERAL`, `PUNCTUATOR`, `KEYWORD` ou `ANNOTATION`.

## Les six phases du préprocesseur

| Phase | Politique | Entrée → sortie |
|---|---|---|
| `neverc.prep.token` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE | un jeton → liste de jetons |
| `neverc.prep.build_token_stream` | idem | plage → flux de jetons |
| `neverc.prep.include.intercept` | idem | requête d'inclusion → décision d'inclusion |
| `neverc.prep.macro.intercept` | idem | opération de macro → action + jetons |
| `neverc.prep.pragma.intercept` | idem | pragma → action + jetons |
| `neverc.prep.feature_query.intercept` | idem | requête `__has_*` → valeur |

Chacune possède sur `NevercPrepAPI` un couple `Get<Kind>PhaseInput` et
`Create<Kind>PhaseOutput` ; la moitié `Create` prend la
`NevercPhaseContinuation` de l'intercepteur, de sorte qu'une sortie ne peut être
produite que depuis l'intérieur de la phase qui la possède.

## Lire les jetons

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

`Origin` vaut `NEVERC_TOKEN_ORIGIN_FILE`, `MACRO_REPLACEMENT`,
`MACRO_ARGUMENT` ou `SYNTHESIZED` : c'est ainsi que l'on distingue un jeton
saisi par l'utilisateur d'un jeton produit par une macro.

Les drapeaux constituent la comptabilité interne du préprocesseur et comptent
lorsque vous synthétisez des jetons :

| Drapeau | Signification |
|---|---|
| `NEVERC_TOKEN_FLAG_START_OF_LINE` | Premier jeton de sa ligne |
| `_LEADING_SPACE` | Une espace le précède |
| `_DISABLE_EXPANSION` | Ne pas développer ce jeton par macro |
| `_NEEDS_CLEANING` | L'écriture contient des sauts de ligne échappés ou des trigraphes |
| `_LEADING_EMPTY_MACRO` | Une macro vide s'est développée juste avant |
| `_HAS_UCN` | Contient un nom de caractère universel |
| `_IGNORED_COMMA`, `_COMMA_AFTER_ELIDED` | Comptabilité de l'élision de virgule variadique |
| `_STRINGIFIED_IN_MACRO` | Produit par `#` |
| `_REINJECTED` | Réinjecté dans le flux de jetons |

`NEVERC_TOKEN_FLAG_ALL` est le masque de tous les bits définis. Les lectures par
lot passent par `GetTokenInfoBatch` ; un flux entier se lit soit comme une vue
légère d'enregistrements `NevercTokenView` via `GetTokenStreamView`, soit un
descripteur à la fois avec `GetTokenStreamToken`. Un flux contient au plus
`NEVERC_PREP_TOKEN_STREAM_MAX_TOKENS` (16 777 216) jetons.

## Identifiants et macros

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

`NevercMacroDefinitionInfo` rapporte le nom, la directive de définition, les
emplacements de définition, de fin et d'annulation, le nombre de paramètres et
de jetons de remplacement, ainsi que des drapeaux :
`NEVERC_MACRO_FUNCTION_LIKE`, `VARIADIC`, `C99_VARIADIC`, `GNU_VARIADIC`,
`HAS_VA_OPT`, `BUILTIN` et `COMMA_PASTING`. Les paramètres et jetons de
remplacement individuels viennent de `GetMacroParameter` et
`GetMacroReplacementToken`.

`NevercIdentifierInfo` y ajoute le genre de jeton, le genre de mot-clé du
préprocesseur, l'identifiant de fonction intrinsèque et des drapeaux tels que
`NEVERC_IDENTIFIER_KEYWORD`, `_HAS_MACRO`, `_POISONED` et `_RESERVED`.

Sur un site d'expansion, `GetMacroArgumentInfo` indique le nombre d'arguments et
si les arguments variadiques ont été élidés, et
`GetMacroArgumentTokenStream` fournit les jetons de chaque argument.

## Abonnement aux événements

Un seul rappel reçoit tous les événements souscrits. Le masque se construit à
partir des genres qui vous intéressent :

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
    /* Event->Payload.Condition.Value vaut NOT_EVALUATED, FALSE ou TRUE */
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

`NEVERC_PREP_EVENT_MASK_ALL` abonne à tout. Les 39 genres, regroupés selon le
membre d'union de charge utile qu'ils emploient :

| Charge utile | Événements |
|---|---|
| `File` | `FILE_CHANGED`, `LEXED_FILE_CHANGED`, `FILE_SKIPPED`, `FILE_NOT_FOUND`, `END_OF_MAIN_FILE` |
| `Include` | `INCLUSION_DIRECTIVE` |
| `Text` | `IDENT`, `PRAGMA_DIRECTIVE`, `PRAGMA_COMMENT`, `PRAGMA_MARK`, `PRAGMA_DETECT_MISMATCH`, `PRAGMA_DEBUG`, `PRAGMA_MESSAGE`, `PRAGMA_DIAGNOSTIC_PUSH`, `PRAGMA_DIAGNOSTIC_POP`, `PRAGMA_DIAGNOSTIC`, `PRAGMA_WARNING`, `PRAGMA_WARNING_PUSH`, `PRAGMA_WARNING_POP`, `PRAGMA_EXEC_CHARSET_PUSH`, `PRAGMA_EXEC_CHARSET_POP`, `PRAGMA_ASSUME_NONNULL_BEGIN`, `PRAGMA_ASSUME_NONNULL_END`, `SOURCE_RANGE_SKIPPED` |
| `Macro` | `MACRO_EXPANDS`, `MACRO_DEFINED`, `MACRO_UNDEFINED`, `DEFINED`, `HAS_INCLUDE` |
| `Condition` | `IF`, `ELIF`, `IFDEF`, `ELIFDEF`, `ELIFDEF_SKIPPED`, `IFNDEF`, `ELIFNDEF`, `ELIFNDEF_SKIPPED`, `ELSE`, `ENDIF` |

`NevercPrepFileEvent.Reason` distingue `NEVERC_PREP_FILE_ENTER`, `EXIT`,
`SYSTEM_HEADER_PRAGMA` et `RENAME`. Les événements sont en lecture seule :
l'enregistrement et chaque vue qu'il contient sont empruntés le temps du rappel,
tandis que les descripteurs publiés dans un événement sont promus à la portée de
la tâche englobante.

## Rediriger une inclusion

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

Les actions sont `NEVERC_PREP_INCLUDE_CONTINUE`, `_SKIP` et `_REDIRECT`.
L'entrée signale aussi `IsImport` et `IsIncludeNext`, si bien que `#import` et
`#include_next` restent distinguables.

## Remplacer une expansion de macro

L'entrée de la phase macro porte l'opération en cours —
`NEVERC_PREP_MACRO_DEFINE`, `_UNDEFINE`, `_EXPAND` ou `_EXPAND_BUILTIN` — avec
le jeton du nom, la définition, les arguments et les jetons de remplacement que
le préprocesseur allait utiliser.

```c
NevercPrepMacroPhaseOutput Out = {0};
Out.Header     = /* … */;
Out.Action     = NEVERC_PREP_MACRO_REPLACE_TOKENS;
Out.Tokens     = MyTokens;      /* const NevercTokenHandle * */
Out.TokenCount = MyTokenCount;
Prep->CreateMacroPhaseOutput(Prep->Context, Frame, Continuation, &Out, &Output);
```

`NEVERC_PREP_MACRO_CONTINUE` conserve le comportement natif et `_SUPPRESS` se
développe en rien du tout.

## Construire des jetons

Les jetons synthétisés proviennent d'un constructeur, qui valide la combinaison
de genre, d'écriture et d'identifiant avant de valider :

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

Utilisez `TokenBuilderSetKind` pour les signes de ponctuation et les mots-clés,
et `TokenBuilderSetIdentifier` pour les identifiants. Les constantes de genre de
jeton viennent de `PluginPrepSchema.inc`.

Pour un flux entier — la phase `neverc.prep.build_token_stream` — accumulez dans
un constructeur de flux et validez en une fois :

```c
NevercTokenStreamBuilderHandle Stream;
Prep->CreateTokenStreamBuilder(Prep->Context, Task, &Stream);
Prep->TokenStreamBuilderAppend(Prep->Context, Task, Stream, Tokens, Count);
Prep->TokenStreamBuilderCommit(Prep->Context, Frame, Stream, &Output);
Prep->DestroyTokenStreamBuilder(Prep->Context, Task, Stream);
```

L'entrée de phase, `NevercPrepTokenStreamPhaseInput`, donne les emplacements de
début et de fin ainsi qu'un `MaximumTokenCount` que la sortie doit respecter.

## Pragmas et requêtes de fonctionnalité

L'entrée d'une phase pragma indique l'introducteur (`NEVERC_PREP_PRAGMA_HASH`,
`_OPERATOR` pour `_Pragma`, ou `_MS` pour `__pragma`), l'espace de noms et le
nom, ainsi que les jetons d'arguments. L'action de sortie est
`NEVERC_PREP_PRAGMA_CONTINUE`, `_HANDLED` ou `_REPLACE_TOKENS`.

Une requête de fonctionnalité couvre `__has_feature`, `__has_extension`,
`__has_builtin`, `__has_include` et `__has_include_next` via
`NEVERC_PREP_QUERY_HAS_FEATURE` et ses semblables. L'entrée porte le nom et la
`BuiltinValue` calculée par le compilateur ; la sortie continue ou remplace :

```c
NevercPrepFeatureQueryPhaseOutput Out = {0};
Out.Header = /* … */;
Out.Action = NEVERC_PREP_QUERY_REPLACE;
Out.Value  = NEVERC_TRUE;
Prep->CreateFeatureQueryPhaseOutput(Prep->Context, Frame, Continuation, &Out,
                                    &Output);
```

## Règles

- Les enregistrements d'événement, les vues de chaîne et les tableaux d'entiers
  sont empruntés le temps du rappel. Les descripteurs publiés dans un événement
  vivent jusqu'à la fin de la tâche.
- Chaque constructeur exige son appel `Destroy*` correspondant, y compris sur le
  chemin d'erreur.
- Un appel à `Create<Kind>PhaseOutput` requiert la continuation de la phase à
  laquelle il appartient ; utiliser celle d'une autre phase renvoie
  `NEVERC_STATUS_WRONG_SCOPE`.
- N'abonnez-vous qu'aux événements que vous traitez. Le masque est le
  régulateur : un plugin qui prend `NEVERC_PREP_EVENT_MASK_ALL` puis filtre en C
  paie chaque rappel.
- Les rappels du préprocesseur s'exécutent sur le fil de la tâche alors que le
  préprocesseur est en plein travail. N'y rentrez pas de nouveau depuis un
  rappel.
- Renvoyez `NEVERC_STATUS_INVALID_ARGUMENT` pour un pointeur obligatoire
  manquant, et ne laissez jamais une exception franchir la frontière.

Voir `PluginPrep.h` et `Schema/PluginPrepSchema.inc` pour les déclarations
normatives, et `Schema/PrepSchema.json` pour le schéma des genres de jetons.
