**Языки**: [English](ast-sema.md) | [简体中文](ast-sema.zh-CN.md) | [繁體中文](ast-sema.zh-TW.md) | [日本語](ast-sema.ja.md) | [한국어](ast-sema.ko.md) | [Français](ast-sema.fr.md) | [Deutsch](ast-sema.de.md) | [Español](ast-sema.es.md) | [Italiano](ast-sema.it.md) | [Русский](ast-sema.ru.md) | [العربية](ast-sema.ar.md)

[← ABI плагинов NeverC](README.ru.md)

# API AST и семантики плагинов NeverC

Фронтенд покрывают три таблицы. `NevercParserAPI` позволяет плагину взять на себя
кусок синтаксического разбора — новую форму объявления, новый оператор — управляя
курсором токенов с контрольными точками. `NevercASTAPI` читает дерево и
транзакционно его изменяет. `NevercSemaAPI` занимается поиском имён, построением
типов, классификацией преобразований и вычислением констант.

AST открывается через **схему**, а не через C-зеркало иерархии классов Clang.
Узлы — непрозрачные дескрипторы; вы запрашиваете свойство по устойчивому
идентификатору и получаете тегированное `NevercASTValue`. Именно эта косвенность
делает поверхность устойчивой между версиями LLVM.

## Интерфейсы

```c
#include "neverc/Plugin/PluginAST.h"
#include "neverc/Plugin/PluginSema.h"
```

| Интерфейс | Таблица | Макросы версии |
|---|---|---|
| `NEVERC_INTERFACE_AST_{HIGH,LOW}` | `NevercASTAPI` | `NEVERC_AST_API_MAJOR` (1) / `_MINOR` (1) |
| `NEVERC_INTERFACE_PARSER_{HIGH,LOW}` | `NevercParserAPI` | `NEVERC_PARSER_API_MAJOR` / `_MINOR` |
| `NEVERC_INTERFACE_SEMA_{HIGH,LOW}` | `NevercSemaAPI` | `NEVERC_SEMA_API_MAJOR` / `_MINOR` |

`Schema/PluginASTSchema.inc` поставляет идентификаторы видов узлов, свойств и
дочерних слотов; его capability major обязан равняться `NEVERC_AST_API_MAJOR`.

## Фазы

Семь синтаксических фаз и семь семантических, все со свойствами
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE`:

| Синтаксис | Семантика |
|---|---|
| `neverc.syntax.parse` | `neverc.sema.analyze` |
| `neverc.syntax.extension.declaration` | `neverc.sema.extension.declaration` |
| `neverc.syntax.extension.statement` | `neverc.sema.extension.statement` |
| `neverc.syntax.extension.expression` | `neverc.sema.extension.expression` |
| `neverc.syntax.extension.type_name` | `neverc.sema.extension.type` |
| `neverc.syntax.extension.attribute` | `neverc.sema.extension.lookup` |
| `neverc.syntax.extension.keyword` | `neverc.sema.extension.conversion` |

`neverc.syntax.parse` потребляет поток токенов и производит AST-единицу;
`neverc.sema.analyze` потребляет эту единицу и производит семантическую единицу.
Фазы `extension.*` — крючки для языковых расширений: хост спрашивает, не хочет ли
какой-нибудь плагин обработать эту конструкцию, прежде чем откатиться к
встроенному поведению.

## Модель схемы

Каждый узел — это `NevercASTNodeHandle`, с типизированными псевдонимами
(`NevercDeclHandle`, `NevercStmtHandle`, `NevercExprHandle`,
`NevercTypeHandle`, `NevercAttrHandle`, `NevercDeclContextHandle`,
`NevercTypeLocHandle`). Структурная навигация единообразна:

```c
NevercASTNodeInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info), NEVERC_AST_API_MAJOR,
                                     NEVERC_AST_API_MINOR, 0};
AST->GetNodeInfo(AST->Context, Task, Node, &Info);
/* Info.Kind, .Domain, .Parent, .DeclContext, .SourceRange */

uint64_t ChildCount = 0;
AST->GetChildCount(AST->Context, Task, Node, &ChildCount);
for (uint64_t I = 0; I != ChildCount; ++I) {
  NevercASTNodeHandle Child;
  AST->GetChild(AST->Context, Task, Node, I, &Child);
}
```

`Domain` — одно из `NEVERC_AST_SCHEMA_DOMAIN_DECL`, `STMT`, `TYPE`, `TYPE_LOC`
или `ATTR`.

Свойства читаются по идентификатору в тегированное значение:

```c
typedef struct NevercASTValue {
  NevercABITableHeader Header;
  NevercASTValueType Type;
  uint32_t Reserved;
  int64_t SignedValue;
  uint64_t UnsignedValue;
  NevercStringView StringValue;
  NevercSourceRange SourceRangeValue;
  NevercASTNodeHandle NodeValue;
} NevercASTValue;
```

`Type` выбирает, какой член действителен: `NEVERC_AST_VALUE_BOOL`, `I64`,
`U64`, `STRING`, `SOURCE_RANGE`, `NODE`, `DECL`, `STMT`, `EXPR`, `TYPE`,
`TYPE_LOC`, `ATTR`, `IDENTIFIER`, `ENUM`, `VERSION`, `PARAMETER_INDEX` или
`ALIGNMENT_OPERAND`. Схема фиксирует для каждого свойства режим доступа
(`READ_ONLY`, `READ_WRITE`, `BUILD_ONLY`) и кратность (`REQUIRED`, `OPTIONAL`,
`MANY`), поэтому попытка записать свойство только для чтения проваливается на
уровне API, а не портит дерево.

Обходить множество узлов разом дешевле пакетными вызовами: они принимают шаг
вывода, так что писать можно прямо в собственный массив структур:

```c
AST->GetNodeInfoBatch(AST->Context, Task, Nodes, NodeCount,
                      OutInfos, OutInfoCapacity, OutInfoStride);
AST->GetPropertyBatch(AST->Context, Task, Nodes, Properties, QueryCount,
                      OutValues, OutValueCapacity, OutValueStride);
```

## Типизированные аксессоры

Для конструкций, к которым плагины обращаются чаще всего, есть прямые читатели
вместо поиска по свойствам:

| Вызов | Что даёт |
|---|---|
| `GetTranslationUnit` | Корневое объявление |
| `GetFunctionDeclInfo`, `GetFunctionDeclParameter` | Имя, тип, тип возврата, тело, число параметров, вариативность, определение |
| `GetVarDeclInfo` | Имя, тип, инициализатор, определение, глобальное хранение |
| `GetRecordDeclInfo` | Имя, число полей, полнота, объединение, гибкий массив-член |
| `GetDeclAttributeCount`, `GetDeclAttribute`, `GetAttrInfo` | Вид атрибута, написание, неявность, наследование |
| `GetDeclRefExprInfo` | Упомянутое и найденное объявление, тип |
| `GetCallExprInfo`, `GetCallExprArgument` | Вызываемое, прямо вызываемое, тип, аргументы |
| `GetBinaryOperatorInfo` | Левое, правое, тип, написание и вид оператора |
| `GetCompoundStmtInfo` | Число операторов |
| `GetIntegerLiteralInfo`, `GetIntegerLiteralWord` | Разрядность и слова little-endian |
| `GetTypeInfo`, `GetTypeElement` | Полное описание типа |
| `GetBuiltinType` | Встроенный тип по `NevercBuiltinTypeKind` |

Самая богатая из них — `NevercTypeInfo`:

```c
typedef struct NevercTypeInfo {
  NevercABITableHeader Header;
  NevercTypeKind Kind;
  NevercTypeQualifierFlags QualifierFlags;  /* CONST, RESTRICT, VOLATILE, UNALIGNED */
  NevercTypeFlags Flags;                    /* CANONICAL, SUGARED, DEPENDENT,
                                               INCOMPLETE, FUNCTION, VARIADIC,
                                               HAS_KNOWN_LAYOUT, POINTER, ARRAY,
                                               VECTOR, ATOMIC */
  NevercTypeAddressSpaceKind AddressSpaceKind;
  uint32_t TargetAddressSpace;
  uint32_t Reserved;
  uint64_t SizeInBits;
  uint64_t AlignmentInBits;
  uint64_t ElementCount;
  NevercTypeHandle CanonicalType;
  NevercTypeHandle DesugaredType;
  NevercTypeHandle RelatedType;
  NevercStringView Name;
} NevercTypeInfo;
```

Виды встроенных типов идут от `NEVERC_BUILTIN_TYPE_VOID` и `_BOOL` вверх по
целочисленной лестнице до `_LONG_DOUBLE`, а виды бинарных операторов — от
`NEVERC_BINARY_OPERATOR_MUL` до `_COMMA`.

## Построение и изменение

Построение идёт через строитель, изменение — через транзакцию. Они сочетаются:
сначала стройте узел-замену, затем вставляйте его.

```c
NevercASTBuilderHandle Builder;
AST->CreateASTBuilder(AST->Context, Task, NodeKind, &Builder);

NevercASTValue Value = {0};
Value.Header = (NevercABITableHeader){sizeof(Value), NEVERC_AST_API_MAJOR,
                                      NEVERC_AST_API_MINOR, 0};
Value.Type          = NEVERC_AST_VALUE_U64;
Value.UnsignedValue = 1;
AST->ASTBuilderSetProperty(AST->Context, Task, Builder, PropertyID, &Value);
AST->ASTBuilderSetChild(AST->Context, Task, Builder, SlotID, 0, ChildNode);

NevercASTNodeHandle NewNode;
AST->ASTBuilderCommit(AST->Context, Task, Builder, &NewNode);
AST->DestroyASTBuilder(AST->Context, Task, Builder);
```

Для литералов шире 64 бит `ASTBuilderSetIntegerValue` принимает
`NevercAPIntView` (слова little-endian плюс разрядность), а
`ASTBuilderSetBinaryOperatorKind` задаёт оператор бинарного выражения.

```c
NevercASTMutationHandle Mutation;
AST->BeginASTMutation(AST->Context, Task, &Mutation);
AST->ASTMutationReplaceChild(AST->Context, Task, Mutation, Parent, SlotID,
                             Index, NewNode);
AST->CommitASTMutation(AST->Context, Task, Mutation);   /* или AbortASTMutation */
AST->DestroyASTMutation(AST->Context, Task, Mutation);
```

Фиксация проверяет подготовленное дерево и публикует его атомарно. Неудачная
фиксация оставляет прежнее дерево нетронутым, а отмена делает созданные этим
изменением дескрипторы устаревшими.
[`pluginsdk/examples/ASTRewritePlugin.c`](../../pluginsdk/examples/ASTRewritePlugin.c)
показывает весь цикл, включая перехват синтаксического анализатора.

## События жизненного цикла

Вместо опроса подпишитесь на одиннадцать точек, где фронтенд публикует
объявление:

```c
NevercASTLifecycleObserverDescriptor Observer = {0};
Observer.Header = /* … */;
Observer.Events =
    NEVERC_AST_LIFECYCLE_EVENT_MASK(NEVERC_AST_LIFECYCLE_TOP_LEVEL_DECL) |
    NEVERC_AST_LIFECYCLE_EVENT_MASK(NEVERC_AST_LIFECYCLE_TRANSLATION_UNIT);
Observer.Callback = on_lifecycle;
Observer.UserData = State;
AST->RegisterLifecycleObserver(AST->Context, Task, &Observer);
```

Виды: `TREE_INITIALIZE`, `SEMA_BEGIN`, `TOP_LEVEL_DECL`,
`INLINE_FUNCTION_DEFINITION`, `INTERESTING_DECL`, `TAG_DEFINITION`,
`TAG_REQUIRED_DEFINITION`, `TENTATIVE_DEFINITION`, `EXTERNAL_DECLARATION`,
`TRANSLATION_UNIT` и `SEMA_END`; `NEVERC_AST_LIFECYCLE_EVENT_MASK_ALL` покрывает
все одиннадцать. Событие несёт единицу трансляции, одно объявление и массив
объявлений — всё только для чтения и одолжено на время обратного вызова.

## Расширение синтаксического анализатора

Расширение анализатора получает курсор токенов со встроенным спекулятивным
разбором:

```c
NevercParserExtensionInput In = {0};
In.Header = /* … */;
Parser->GetExtensionInput(Parser->Context, Frame, Frame->Input, &In);

NevercParserCheckpointHandle Checkpoint;
Parser->CursorCheckpoint(Parser->Context, Task, In.Cursor, &Checkpoint);

NevercTokenHandle Token;
Parser->CursorPeek(Parser->Context, Task, In.Cursor, /*Offset=*/0, &Token);
if (!is_my_construct(Token)) {
  Parser->CursorRollback(Parser->Context, Task, In.Cursor, Checkpoint);
  Out.Disposition = NEVERC_PARSER_EXTENSION_UNHANDLED;
} else {
  Parser->CursorConsume(Parser->Context, Task, In.Cursor, &Token);
  /* … построить узел … */
  Parser->CursorCommit(Parser->Context, Task, In.Cursor, Checkpoint);
  Out.Disposition = NEVERC_PARSER_EXTENSION_HANDLED;
  Out.ResultKind  = NEVERC_PARSER_RESULT_DECL;
  Out.Node        = MyNode;
}
Parser->CreateExtensionOutput(Parser->Context, Frame, Continuation, &Out,
                              &Output);
```

Поле `ExpectedResult` во входе сообщает, что нужно анализатору:
`NEVERC_PARSER_RESULT_DECL`, `STMT`, `EXPR`, `TYPE` или `ATTRIBUTE`.
`CreateParsedAttribute` строит атрибут в форме GNU (`__attribute__`), C23
(`[[…]]`) или `__declspec`.

Поставщик для самой `neverc.syntax.parse` публикует целую AST-единицу:

```c
NevercParserASTUnitDescriptor Unit = {0};
Unit.Header          = /* … */;
Unit.Product         = (NevercInterfaceID){NEVERC_AST_PRODUCT_STANDARD_HIGH,
                                           NEVERC_AST_PRODUCT_STANDARD_LOW};
Unit.TranslationUnit = TU;
Parser->CreateASTUnit(Parser->Context, Frame, &Unit, &Output);
```

`GetASTUnitInfo` сообщает `SemanticState` единицы. Единица, опубликованная как
`NEVERC_AST_UNIT_UNANALYZED`, будет заново проиграна через семантический анализ;
`NEVERC_AST_UNIT_SEMANTICALLY_ANALYZED` утверждает, что поставщик уже выполнил
эту работу.

## Семантические запросы

```c
NevercSemaLookupRequest Request = {0};
Request.Header = /* … */;
Request.Scope  = Scope;
Request.Name   = SV("my_symbol");
Request.Kind   = NEVERC_SEMA_LOOKUP_ORDINARY;   /* или _TAG, _MEMBER */

NevercLookupResultHandle Result;
Sema->LookupName(Sema->Context, Task, &Request, &Result);

NevercSemaLookupResultInfo Info = {0};
Info.Header = /* … */;
Sema->GetLookupResultInfo(Sema->Context, Task, Result, &Info);
/* Info.Kind — NOT_FOUND, FOUND или AMBIGUOUS; далее Info.CandidateCount. */

for (uint64_t I = 0; I != Info.CandidateCount; ++I) {
  NevercDeclHandle Candidate;
  Sema->GetLookupCandidate(Sema->Context, Task, Result, I, &Candidate);
}
Sema->DestroyLookupResult(Sema->Context, Task, Result);
```

`GetCurrentScope`, `GetScopeInfo` и `GetScopeDeclaration` проходят по цепочке
областей видимости; флаги области — `NEVERC_SEMA_SCOPE_FILE`, `FUNCTION`,
`RECORD` и `BLOCK`.

Вычисление константы возвращает дескриптор, чья информация описывает форму
значения:

```c
NevercConstantValueHandle Value;
Sema->EvaluateConstant(Sema->Context, Task, Expression, &Value);

NevercSemaConstantValueInfo Info = {0};
Info.Header = /* … */;
Sema->GetConstantValueInfo(Sema->Context, Task, Value, &Info);
/* Info.Kind: NONE, INDETERMINATE, INTEGER, FLOAT, FIXED_POINT,
   COMPLEX_INTEGER, COMPLEX_FLOAT, ADDRESS, VECTOR, ARRAY, STRUCT, UNION,
   ADDRESS_LABEL_DIFFERENCE. */

uint64_t Word;
Sema->GetConstantIntegerWord(Sema->Context, Task, Value, 0, &Word);
Sema->DestroyConstantValue(Sema->Context, Task, Value);
```

Преобразования классифицируются до применения, так что плагин может рассмотреть
это решение:

```c
NevercConversionSequenceHandle Sequence;
Sema->ClassifyImplicitConversion(Sema->Context, Task, SourceType, DestType,
                                 &Sequence);
NevercSemaConversionSequenceInfo SeqInfo = {0};
SeqInfo.Header = /* … */;
Sema->GetConversionSequenceInfo(Sema->Context, Task, Sequence, &SeqInfo);
/* SeqInfo.Kind охватывает COMPATIBLE, POINTER_TO_INTEGER,
   INTEGER_TO_POINTER, INCOMPATIBLE_POINTER, DISCARDS_QUALIFIERS,
   ADDRESS_SPACE_MISMATCH, VECTOR, INCOMPATIBLE и другие;
   далее идут SeqInfo.Viable и .RequiresDiagnostic. */
```

`AreTypesCompatible`, `GetCanonicalType`, `GetTagType` и `GetBuiltinInfo`
завершают поверхность только для чтения.

## Аренда на изменение

Всё, что меняет семантическое состояние — создание типа, применение
преобразования, выпуск семантической диагностики, — требует аренды. Именно
аренда делает параллельную семантическую работу безопасной:

```c
NevercSemaMutationLeaseHandle Lease;
Sema->AcquireMutationLease(Sema->Context, Task, &Lease);

NevercTypeHandle Pointer;
Sema->CreatePointerType(Sema->Context, Task, Lease, Pointee, &Pointer);

NevercExprHandle Converted;
Sema->ApplyImplicitConversion(Sema->Context, Task, Lease, Sequence,
                              Expression, NEVERC_SEMA_CONVERSION_ARGUMENT,
                              &Converted);

Sema->ReleaseMutationLease(Sema->Context, Task, Lease);
```

`CreateConstantArrayType`, `CreateFunctionType`, `CreateAtomicType`,
`CreateVectorType`, `CreateExplicitCast` и `EmitDiagnostic` — все принимают
аренду. Контексты преобразования: `NEVERC_SEMA_CONVERSION_ASSIGNMENT`,
`ARGUMENT`, `RETURN`, `INITIALIZATION` и `EXPLICIT_CAST`.

## Фазы семантических расширений

У каждой фазы расширения есть парный вход/выход. Например, крючок выражений:

```c
NevercSemaExpressionExtensionInput In = {0};
In.Header = /* … */;
Sema->GetExpressionExtensionInput(Sema->Context, Frame, Frame->Input, &In);
/* In.Left, In.Right, In.OperatorLocation */

NevercSemaExpressionExtensionOutput Out = {0};
Out.Header      = In.Header;
Out.Disposition = NEVERC_SEMA_EXTENSION_HANDLED;   /* или _UNHANDLED */
Out.Expression  = Result;
Sema->CreateExpressionExtensionOutput(Sema->Context, Frame, Continuation,
                                      &Out, &Output);
```

Та же форма применима к `Statement`, `Declaration`, `Type`, `Lookup` и
`Conversion`. Возврат `NEVERC_SEMA_EXTENSION_UNHANDLED` позволяет отработать
встроенному поведению.

Поставщик для `neverc.sema.analyze` публикует семантическую единицу:

```c
NevercSemanticUnitDescriptor Unit = {0};
Unit.Header           = /* … */;
Unit.Product          = (NevercInterfaceID){NEVERC_SEMANTIC_PRODUCT_STANDARD_HIGH,
                                            NEVERC_SEMANTIC_PRODUCT_STANDARD_LOW};
Unit.TranslationUnit  = TU;
Unit.SemanticComplete = NEVERC_TRUE;
Sema->CreateSemanticUnit(Sema->Context, Frame, &Unit, &Output);
```

`GetSemanticUnitInfo` сообщает `DiagnosticState`
(`NEVERC_SEMANTIC_DIAGNOSTICS_CLEAN` или `_HAS_ERROR`), была ли единица
проиграна заново, и сводку проверяющего.

## Правила

- Дескрипторы AST и типов действуют в пределах задачи. Никогда не храните их
  дольше обратного вызова.
- У каждого строителя, изменения, результата поиска, последовательности
  преобразования и константного значения есть парный `Destroy*`; вызывайте его и
  на пути ошибки.
- Семантическое изменение без аренды возвращает
  `NEVERC_STATUS_INVALID_STATE`.
- Не изменяйте дерево из наблюдателя жизненного цикла — наблюдатели работают
  только на чтение. Используйте перехватчик на соответствующей фазе.
- Идентификаторы свойств и дочерних слотов — константы схемы. Не зашивайте
  числовые литералы; берите имена из `PluginASTSchema.inc`, чтобы пересмотр схемы
  становился ошибкой компиляции.
- Прежде чем доверять `SizeInBits` или `AlignmentInBits`, проверьте
  `NevercTypeInfo.Flags` на `HAS_KNOWN_LAYOUT`.

Нормативные объявления смотрите в `PluginAST.h`, `PluginSema.h` и
`Schema/ASTSchema.json`, а работающий перехват анализатора с атомарной
перезаписью дерева — в `pluginsdk/examples/ASTRewritePlugin.c`.
