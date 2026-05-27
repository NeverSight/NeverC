**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Документация NeverC](../README.ru.md)

# Встроенная система времени выполнения NeverC

NeverC расширяет стандартный C опциональными встроенными средами выполнения, которые встроены непосредственно в двоичный файл компилятора в виде LLVM bitcode. При активации через флаги компилятора соответствующая среда выполнения объединяется с IR пользователя во время компиляции — без внешних заголовочных файлов, библиотек или зависимостей компоновки.

## Доступные встроенные функции

| Встроенная | Флаг | По умолчанию | Описание |
|-----------|------|-------------|----------|
| [**`string`**](string/README.ru.md) | `-fbuiltin-string` | Выкл. | Строковый тип с семантикой значения, методы через точку, автоматическое управление памятью и нативная поддержка UTF-8 |
| [**`mimalloc`**](mimalloc/README.ru.md) | `-fbuiltin-mimalloc` | **Вкл.** | Высокопроизводительный аллокатор памяти, прозрачно заменяющий `malloc`/`free`/`calloc`/`realloc` |
| [**`xorstr`**](xorstr/README.ru.md) | `-fencrypt-call-strings` | Выкл. | Шифрование строк на этапе компиляции, стековая XOR-дешифрация, антисигнатурный алгоритм |

```bash
neverc -fbuiltin-string -fbuiltin-mimalloc main.c -o main
```

---

## Обзор архитектуры

`string` и `mimalloc` используют одну и ту же четырёхуровневую архитектуру:

1. **Языковые опции и флаги драйвера** — `LangOption` определён в `LangOptions.def`
2. **API Foundation** — предоставляет `getEmbeddedBitcode()` и `isSupported()`
3. **Инфраструктура CMake Bootstrap** — двухэтапная генерация bitcode
4. **Проход слияния IR** — слияние bitcode в модуль пользователя на `PipelineStartEP`

Пример регистрации в `LangOptions.def`:

```cpp
LANGOPT(BuiltinString,      1, 0, "inject NeverC builtin string prelude")
LANGOPT(BuiltinMimalloc,    1, 1, "inject mimalloc allocator override")
LANGOPT(EncryptCallStrings, 1, 0, "auto-encrypt string literals in call arguments")
VALUE_LANGOPT(EncryptCallStringsMaxLen, 32, 1024,
              "maximum string length for auto-encryption (0 = no limit)")
```

> **Примечание:** `xorstr` не использует модель встроенного bitcode. Явный макрос [`NC_XORSTR(s)` / `NEVERC_XORSTR(s)`](xorstr/README.ru.md) понижается слоем Sema (обработчик `semaBuiltinNeverCXorstr` в `SemaChecking.cpp`), а опциональное автоматическое шифрование `-fencrypt-call-strings` выполняется IR-трансформацией `EncryptCallStringsPass`, зарегистрированной в позиции **OptimizerLast** (вместе с `XorStrCleanupPass`, который обнуляет открытые буферы стека через `volatile memset`). Подробности см. в [документации xorstr](xorstr/README.ru.md).

---

## Различия в дизайне встроенных функций

| Аспект | `string` | `mimalloc` |
|--------|----------|------------|
| **Стратегия слияния** | По требованию (BFS граф вызовов) | Полный архив (все символы) |
| **Bitcode платформы** | Единый (независимый от архитектуры) | По ОС (Linux / Darwin / Windows) |
| **Обработка символов** | Все интернализированы | Точки входа переопределения сохраняют внешнюю компоновку |
| **Макрос препроцессора** | *(нет)* | `__NEVERC_MIMALLOC__` |
| **Режим шеллкода** | Авто-активация, перезапись арены | Подавлен (HeapArenaPass обрабатывает кучу) |
| **Уровень оптимизации** | `-O0` (компиляция bitcode) | `-O2` (критичный для производительности аллокатор) |
| **DCE** | Пред-слияние + пост-слияние mark-and-sweep | Без DCE (семантика полного архива) |

---

## Блокировки безопасности

| Условие | Эффект | Причина |
|---------|--------|---------|
| `-fno-builtin` | Подавляет mimalloc | Нет сценария переопределения CRT |
| `-mkernel` | Подавляет mimalloc | Нет кучи пользовательского пространства в ядре |
| `-fshellcode-mode` | Подавляет mimalloc | Заменён HeapArenaPass (на основе арены) |
| `-ffreestanding` | Подавляет mimalloc | Нет libc для переопределения |

Встроенная функция `string` имеет собственную логику подавления (перезапись арены в конвейере шеллкода заменяет выделение кучи).

### HeapArenaPass (Выделение кучи в шеллкоде)

При активном `-fshellcode-mode` `mimalloc` подавляется, но вызовы `malloc`/`free`/`calloc`/`realloc` автоматически переписываются `HeapArenaPass` (включён по умолчанию). Проход использует гибридную стратегию:

- **Малые выделения (≤ 64 КБ)**: обслуживаются из арены на стеке, разделяемой со встроенным рантаймом `string` (бамп-аллокатор + повторное использование свободного списка).
- **Большие выделения (> 64 КБ) или переполнение арены**: откат к аллокатору ОС:
  - **Windows**: `malloc`/`free` разрешаются из `msvcrt.dll` через PEB walk (`-mshellcode-win-peb-import`).
  - **Linux / macOS / Android**: `mmap`/`munmap` инлайнятся как нативные системные вызовы (`-mshellcode-syscall`).
  - **Импортный проход не включён**: только арена; переполнение возвращает `NULL`.

Управление через флаги драйвера:

```bash
neverc -fshellcode test.c -o test.bin                     # HeapArenaPass ВКЛ (по умолчанию)
neverc -fshellcode -fno-shellcode-heap-arena test.c       # HeapArenaPass ВЫКЛ (исходное поведение)
```

---

## Макросы препроцессора

```c
#ifdef __NEVERC_MIMALLOC__
// mimalloc активен — malloc/free прозрачно переопределены
#endif
```

---

## Структура файлов

```
neverc/
├── include/neverc/Foundation/Builtin/
│   ├── BuiltinString.h / BuiltinMimalloc.h
│   └── Builtins.def                      # __builtin_neverc_xorstr
├── include/neverc/Transforms/XorStr/
│   └── EncryptCallStringsPass.h / XorStrCleanupPass.h
├── lib/Foundation/Builtin/
│   ├── BuiltinString.cpp / BuiltinMimalloc.cpp
│   └── bin2c.py / gen_string_runtime.py / gen_mimalloc_source.py
├── lib/Headers/neverc/
│   └── xorstr.h / xorstr_impl.inc        # макросы NC_XORSTR / NEVERC_XORSTR
├── lib/Analyze/Checking/SemaChecking.cpp # semaBuiltinNeverCXorstr
├── lib/Transforms/XorStr/
│   └── EncryptCallStringsPass.cpp / XorStrCleanupPass.cpp
├── lib/Emit/Backend/
│   └── BackendUtil.cpp / StringRuntimeLinker.{h,cpp} / MimallocRuntimeLinker.{h,cpp}
├── lib/Invoke/ToolChains/NeverC.cpp
└── lib/Compiler/Preprocessor/InitPreprocessor.cpp
```

---

## Добавление новой встроенной функции

1. Добавить `LANGOPT` в `LangOptions.def`
2. Добавить флаги драйвера в `Options.td.h`
3. Создать API Foundation (`BuiltinFoo.h` + `.cpp`)
4. Создать генератор исходного кода
5. Добавить цели CMake bootstrap
6. Создать IR проход и зарегистрировать в `PipelineStartEP`
7. Определить макрос препроцессора
8. Добавить проверки безопасности
9. Добавить тесты
10. Добавить документацию и переводы i18n
