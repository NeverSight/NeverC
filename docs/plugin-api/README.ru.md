**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# NeverC Out-of-Tree API плагинов

NeverC предоставляет **чистый C ABI** для out-of-tree плагинов проходов. Плагин — это разделяемая библиотека (`.dll` / `.so` / `.dylib`), регистрирующая пользовательские проходы в заданных точках конвейера компиляции. Для компиляции плагина нужен **один заголовочный файл** (`NevercPluginAPI.h`) с **нулевыми** зависимостями от LLVM или CRT — вся функциональность маршрутизируется через vtable, предоставляемую хостом.

## 1. Быстрый старт

### Минимальный плагин

```c
#include "NevercPluginAPI.h"

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
    API->RegisterModulePass(Reg, NEVERC_HOOK_PRE_OPT, myPass, NULL, "my-pass");
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
cc -shared -o MyPlugin.dll MyPlugin.c -I/path/to/pluginsdk/include
```

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

| Hook | Уровень | Описание |
|------|---------|----------|
| `NEVERC_HOOK_PRE_OPT` | IR | Перед оптимизацией LLVM |
| `NEVERC_HOOK_POST_OPT` | IR | После оптимизации LLVM |
| `NEVERC_HOOK_PIPELINE_START` | IR | Начало конвейера |
| `NEVERC_HOOK_PIPELINE_LAST` | IR | Конец IR-конвейера |
| `NEVERC_HOOK_SC_*` | IR/MIR/Бинарный | Поток shellcode |
| `NEVERC_HOOK_LTO_*` | IR | Поток LTO |
| `NEVERC_HOOK_LINK_*` | Линковщик | Поток линковщика |

## 6. Структуры данных

**Arena** (bump-pointer аллокатор), **StrMap** (хеш-таблица со строковым ключом), **IntMap** (хеш-таблица с целочисленным ключом), **StrBuilder** (инкрементальное построение строк), **ValueSet** (хеш-множество значений).

## 7. Совместимость версий

```c
if (NEVERC_API_FN(API, SomeNewFunction)) {
    API->SomeNewFunction(...);
}
```

## 8. Аргументы плагина

```bash
neverc -fplugin-pass=./MyPlugin.dll \
       -fplugin-pass-arg=verbose=1 \
       input.c -o output.obj
```

## 9. Лучшие практики

1. **Arena в приоритете**: Используйте `NEVERC_TRY_ARENA` для временных данных.
2. **Защита версий**: Всегда оборачивайте новые вызовы vtable в `NEVERC_API_FN`.
3. **Callback-итерация**: `ModuleForEachDefinedFunction` быстрее макросов.
4. **Без зависимости CRT**: Все операции через vtable.
5. **Чистый возврат**: Освободите все ресурсы перед возвратом из прохода.

## 10. Содержимое Plugin SDK

```
pluginsdk/
├── include/
│   └── neverc/
│       └── Plugin/
│           └── NevercPluginAPI.h
└── examples/
    ├── CMakeLists.txt
    ├── ExamplePlugin.c
    ├── CrtShimPlugin.c
    └── BenchPlugin.c
```

## 11. Связанная документация

- [Интерфейс плагинов Shellcode](../shellcode-compiler/plugin-interface/README.ru.md) — Точки расширения C++ in-tree для конвейера shellcode.
