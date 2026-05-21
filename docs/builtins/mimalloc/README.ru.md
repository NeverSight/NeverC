**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Встроенная система времени выполнения NeverC](../README.ru.md)

# Встроенный аллокатор mimalloc

## Обзор

NeverC может встраивать [mimalloc](https://github.com/microsoft/mimalloc) — высокопроизводительный аллокатор памяти от Microsoft — непосредственно в скомпилированные двоичные файлы через слияние LLVM bitcode. При активации `malloc`, `free`, `calloc` и `realloc` прозрачно заменяются реализациями mimalloc во время компиляции.

**Активация:**

```bash
neverc -fbuiltin-mimalloc main.c -o main
```

---

## Использование

```bash
neverc -fbuiltin-mimalloc hello.c -o hello                     # базовое
neverc -fbuiltin-string -fbuiltin-mimalloc main.c -o main      # в сочетании со string
neverc -fno-builtin-mimalloc main.c -o main                    # отключить
```

```c
#ifdef __NEVERC_MIMALLOC__
    printf("Используется аллокатор mimalloc\n");
#endif
```

---

## Поддержка платформ

| Платформа | Triple | Статус |
|-----------|--------|--------|
| Linux x86_64 | `x86_64-unknown-linux-gnu` | Поддерживается |
| Linux AArch64 | `aarch64-unknown-linux-gnu` | Поддерживается |
| Android | `aarch64-linux-android` | Поддерживается |
| macOS x86_64 | `x86_64-apple-macosx` | Поддерживается |
| macOS AArch64 | `arm64-apple-macosx` | Поддерживается |
| iOS | `arm64-apple-ios` | Поддерживается |
| Windows x86_64 (MSVC) | `x86_64-pc-windows-msvc` | Поддерживается |
| Windows AArch64 (MSVC) | `aarch64-pc-windows-msvc` | Поддерживается |

---

## Автоматическое подавление

| Флаг / Режим | Причина |
|-------------|---------|
| `-fno-builtin` | Нет сценария переопределения CRT |
| `-mkernel` | Нет кучи пользовательского пространства в ядре |
| `-fshellcode-mode` | Нет кучи в шеллкоде |
| `-ffreestanding` | Нет libc для переопределения |

---

## Процесс Bootstrap

```bash
ninja neverc                         # Этап 1: Пустые placeholder'ы bitcode
ninja neverc-bootstrap-mimalloc-bc   # Этап 2: Компиляция bitcode по ОС
ninja neverc                         # Этап 3: Встраивание реального bitcode
```

---

## Архитектура

mimalloc встраивается как LLVM bitcode в двоичный файл компилятора. При компиляции пользовательского кода Module Pass объединяет bitcode с IR до pipeline оптимизации. Компилируется отдельно для каждой ОС (Linux `mmap`, macOS `vm_allocate`, Windows `VirtualAlloc`), выбирается через target triple. Семантика **полного архива** — все функции компонуются.

---

## Структура файлов

```
neverc/
├── include/neverc/Foundation/Builtin/BuiltinMimalloc.h
├── lib/Foundation/Builtin/
│   ├── BuiltinMimalloc.cpp / gen_mimalloc_source.py / bin2c.py
├── lib/Emit/Backend/
│   ├── MimallocRuntimeLinker.{h,cpp} / BackendUtil.cpp
├── lib/Invoke/ToolChains/NeverC.cpp
└── lib/Compiler/Preprocessor/InitPreprocessor.cpp
```

---

## Справочник флагов компилятора

| Флаг | Описание |
|------|----------|
| `-fbuiltin-mimalloc` | Включить инъекцию переопределения mimalloc (включено по умолчанию для hosted-сборок) |
| `-fno-builtin-mimalloc` | Явно отключить инъекцию mimalloc |

| Макрос | Значение | Когда определён |
|--------|----------|----------------|
| `__NEVERC_MIMALLOC__` | `1` | Когда `-fbuiltin-mimalloc` активен |
