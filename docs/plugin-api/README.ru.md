**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# NeverC Out-of-Tree API плагинов

NeverC предоставляет **чистый C ABI** для out-of-tree плагинов проходов. Плагин — это разделяемая библиотека (`.dll` / `.so` / `.dylib`), регистрирующая пользовательские проходы в заданных точках конвейера компиляции. Для компиляции плагина нужен **один заголовочный файл** (`NevercPluginAPI.h`) с **нулевыми** зависимостями от LLVM или CRT — вся функциональность маршрутизируется через vtable, предоставляемую хостом.

## 1. Быстрый старт

### Минимальный плагин

```c
#include "neverc/Plugin/NevercPluginAPI.h"

static int myPass(NevercModuleRef M, const NevercHostAPI *API, void *UD) {
    (void)UD;
    unsigned Count = 0;
    NEVERC_FOR_EACH_DEFINED_FUNCTION(API, M, F) {
        (void)F;
        Count++;
    }
    API->DiagNoteF("[my-plugin] %u defined functions", Count);
    return 0;
}

static void registerPasses(const NevercHostAPI *API, void *Reg) {
    API->RegisterModulePass(Reg, NEVERC_INTERPOSE_PRE_OPT, myPass, NULL, "my-pass");
}

NEVERC_EXPORT NevercPluginInfo nevercGetPluginInfo(void) {
    NevercPluginInfo Info;
    Info.APIVersion     = NEVERC_PLUGIN_API_VERSION;
    Info.PluginName     = "my-plugin";
    Info.PluginVersion  = "1.0.0";
    Info.RegisterPasses = registerPasses;
    Info.Destroy        = NULL;
    return Info;
}
```

### Сборка

```bash
# Рекомендуется: сборка с neverc (единый ABI, кроссплатформенность):
neverc --target=x86_64-pc-windows-msvc -shared -o MyPlugin.dll MyPlugin.c \
       -I/path/to/pluginsdk/include

# Или через Make (по умолчанию использует neverc):
neverc make -C /path/to/pluginsdk/examples
```

> **Примечание:** Настоятельно рекомендуется использовать **neverc** для единообразия ABI и кроссплатформенной поддержки.

### Запуск

```bash
neverc -fplugin-pass=./MyPlugin.dll input.c -o output.obj
```

## 2. Архитектура

- **SDK с одним заголовком**: Для компиляции плагина нужен только `NevercPluginAPI.h`.
- **Нулевые зависимости**: Без заголовков LLVM, без линковки CRT. Все операции через vtable.
- **Чистый C ABI**: Плагины можно писать на C, C++, Zig, Rust (FFI) или любом языке, способном создать разделяемую библиотеку с C-линковкой.
- **Безопасность версий**: Используйте `NEVERC_API_FN(api, Field)` для проверки необязательных записей vtable перед вызовом.

## 3. Точка входа

```c
NEVERC_EXPORT NevercPluginInfo nevercGetPluginInfo(void);
```

| Поле | Тип | Описание |
|------|-----|----------|
| `APIVersion` | `uint32_t` | Должно быть `NEVERC_PLUGIN_API_VERSION` |
| `PluginName` | `const char *` | Человекочитаемое имя |
| `PluginVersion` | `const char *` | Строка семантической версии |
| `RegisterPasses` | указатель на функцию | Вызывается один раз для регистрации всех проходов |
| `Destroy` | указатель на функцию | Необязательная очистка, может быть `NULL` |

## 4. Типы проходов

- **Module Pass (IR)**: Работает с модулем LLVM IR.
- **Machine Pass (MIR)**: Работает с машинным IR.
- **Binary Pass**: Работает с сырыми байтами.
- **Linker Pass**: Работает на этапе линковки.

## 5. Точки подключения

| Interpose | Уровень | Описание |
|------|---------|----------|
| `NEVERC_INTERPOSE_PRE_OPT` | IR | Перед оптимизацией LLVM |
| `NEVERC_INTERPOSE_POST_OPT` | IR | После оптимизации LLVM |
| `NEVERC_INTERPOSE_PIPELINE_START` | IR | Начало конвейера |
| `NEVERC_INTERPOSE_PIPELINE_LAST` | IR | Конец IR-конвейера |
| `NEVERC_INTERPOSE_SC_*` | IR/MIR/Бинарный | Поток dyncode |
| `NEVERC_INTERPOSE_LTO_*` | IR | Поток LTO |
| `NEVERC_INTERPOSE_LINK_*` | Линковщик | Поток линковщика |

## 6. Непрозрачные типы дескрипторов

Все объекты IR/MIR доступны через непрозрачные дескрипторы: `NevercModuleRef`, `NevercValueRef`, `NevercBasicBlockRef`, `NevercTypeRef`, `NevercBuilderRef`, `NevercContextRef`, `NevercMetadataRef`, `NevercNamedMDRef`, `NevercComdatRef`, `NevercMachineFuncRef`, `NevercMachineBBRef`, `NevercMachineInstrRef`, `NevercUseRef`, `NevercLinkerSymbolRef`, `NevercLinkerSectionRef`. Дескрипторы действительны **только в области видимости обратного вызова прохода**, который их получил.

## 7. Структуры данных

**Arena** (bump-pointer аллокатор), **StrMap** (хеш-таблица со строковым ключом), **IntMap** (хеш-таблица с целочисленным ключом), **StrBuilder** (инкрементальное построение строк), **ValueSet** (хеш-множество значений).

## 8. Совместимость версий

```c
if (NEVERC_API_FN(API, SomeNewFunction)) {
    API->SomeNewFunction(...);
}
```

## 9. Аргументы плагина

```bash
neverc -fplugin-pass=./MyPlugin.dll \
       -fplugin-pass-arg=verbose=1 \
       input.c -o output.obj
```

## 10. Лучшие практики

1. **Arena в приоритете**: Используйте `NEVERC_TRY_ARENA` для временных данных.
2. **Защита версий (для будущих API)**: Когда vtable получит новые поля после релиза, защищайте вызовы `NEVERC_API_FN` только если плагин должен работать на старых хостах без этих полей. Текущие записи vtable не требуют защиты.
3. **Callback-итерация**: `ModuleForEachDefinedFunction` быстрее макросов.
4. **Без зависимости CRT**: Все операции через vtable.
5. **Чистый возврат**: Освободите все ресурсы перед возвратом из прохода.

## 11. Содержимое Plugin SDK

```
pluginsdk/
├── include/
│   └── neverc/
│       └── Plugin/
│           └── NevercPluginAPI.h
└── examples/
    ├── Makefile
    ├── CrtShimPlugin.c
    ├── BenchPlugin.c
    ├── ExamplePlugin.c
    └── CustomCallConvPlugin.c   # Пользовательские соглашения о вызовах
```

## 12. Пользовательские соглашения о вызовах

`CustomCallConvPlugin.c` демонстрирует самую продвинутую возможность плагина: назначение произвольных физических регистров на уровне IR, бэкенд выполняет спецификацию без изменений `.td`/`.inc`.

**[Пользовательские соглашения о вызовах →](custom-callconv/README.ru.md)**

## 13. Связанная документация
