**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Проект NeverC](i18n/README.ru.md)

> **Tip:** Use the language bar above; links on this page point to the same locale (dyncode README and breadcrumbs).

# Документация NeverC

Описания дизайна, справочник API и руководства по каждой подсистеме NeverC.

---

## Компилятор dyncode

Конвейер компиляции dyncode — основной исследовательский фокус NeverC. Архитектура, опции CLI, матрица платформ и примеры:

**[Компилятор dyncode →](dyncode-compiler/README.ru.md)**

| Документ | Описание |
|----------|----------|
| [README](dyncode-compiler/README.ru.md) | Обзор, быстрый старт, поддерживаемые цели |
| [Pipeline & PIC](dyncode-compiler/pipeline-and-pic/README.ru.md) | Дизайн IR → объект → извлечение |
| [IR Pass Design](dyncode-compiler/ir-pass-design/README.ru.md) | Обоснование каждого IR-прохода |
| [MIR Pass Design](dyncode-compiler/mir-pass-design/README.ru.md) | MIR-проходы бэкенда |
| [Kernel-Mode DynCode](dyncode-compiler/kernel-mode-dyncode/README.ru.md) | Компиляция Ring-0 |
| [Cross-Platform Architecture](dyncode-compiler/cross-platform-architecture/README.ru.md) | `TargetDesc` и экстракторы |
| [Platform Extension Guide](dyncode-compiler/platform-extension-guide/README.ru.md) | Добавление платформы |
| [ARM64 Assembly Tutorial](dyncode-compiler/arm64-assembly-tutorial/README.ru.md) | Инструкции ARM64 с точки зрения dyncode |
| [Roadmap](dyncode-compiler/roadmap/README.ru.md) | Запланированная работа |
| [Progress](dyncode-compiler/progress/README.ru.md) | Статус реализации |

---

## Расширение файла `.nc`

NeverC распознаёт `.nc` как своё нативное расширение исходных файлов. С `.nc` все расширения языка NeverC (`-fneverc-types`, `-fbuiltin-string`) включаются автоматически — дополнительные флаги не требуются.

**[Расширение `.nc` →](nc-extension/README.ru.md)**

---

## Встроенные среды выполнения

NeverC расширяет стандартный C встроенными средами выполнения в виде LLVM bitcode. Каждая управляется флагом `-fbuiltin-<name>`. Файлы `.nc` автоматически включают `string`.

**[Система встроенных сред →](builtins/README.ru.md)**

| Встроенная | Флаг | Описание |
|-----------|------|----------|
| [Встроенная строка](builtins/string/README.ru.md) | `-fbuiltin-string` | Тип `string` с семантикой значения, методы через точку, автоуправление памятью, нативный UTF-8 |
| [Встроенный mimalloc](builtins/mimalloc/README.ru.md) | `-fbuiltin-mimalloc` | Прозрачная замена аллокатора `mimalloc` высокой производительности `malloc`/`free`/`calloc`/`realloc` |
| [Шифрование строк (xorstr)](builtins/xorstr/README.ru.md) | `-fencrypt-call-strings` | Шифрование каждого экземпляра, обязательное позднее запечатывание, разворачивание в точке вызова и volatile-очистка стека |
| [Хеширование строк (strhash)](builtins/strhash/README.ru.md) | `-fstrhash-algo` / `-fstrhash-fold` | Хеширование строк на этапе компиляции, тот же алгоритм во время выполнения, опциональный IR-fold |

---

## API плагинов

NeverC открывает всю свою цепочку инструментов через чистый C ABI. Плагин — это разделяемый модуль (`.dll` / `.so` / `.dylib`), который присоединяется к любой из 130 именованных фаз компиляции — от разбора командной строки до итогового скомпонованного образа — как наблюдатель, как перехватчик или как замещающий провайдер. SDK состоит только из заголовков: ни заголовков LLVM, ни компоновки с компилятором.

**[API плагинов →](plugin-api/README.ru.md)**

| Документ | Описание |
|----------|----------|
| [README](plugin-api/README.ru.md) | Точка входа, фазы, согласование интерфейсов, регистрация, правила ABI |
| [Плагины Python](plugin-api/python.ru.md) | Необязательный встроенный Python, жизненный цикл, опции, read-only observers, диагностика и ограничения |
| [API драйвера](plugin-api/driver.ru.md) | Командная строка, выбор тулчейна, граф действий, граф заданий |
| [API источников и ввода-вывода](plugin-api/source.ru.md) | Провайдеры VFS, позиции в исходниках, буферы, приёмники вывода, зависимости |
| [API препроцессора](plugin-api/prep.ru.md) | Токены, макросы, прагмы, включения, запросы возможностей, 39 видов событий |
| [API AST и семантики](plugin-api/ast-sema.ru.md) | Расширение парсера, изменение AST, поиск имён, типы, константы |
| [API IR](plugin-api/ir.ru.md) | Чтение LLVM IR, транзакционное построение, анализы, проходы, провайдеры |
| [API MIR](plugin-api/mir.ru.md) | Машинные функции, регистры, кадры стека, проходы и анализы MIR |
| [Целевая платформа, MC, ассемблер, объектные файлы](plugin-api/target-mc-object.ru.md) | Регистрация целевых платформ, соглашения о вызовах, кодирование MC, графы объектных файлов |
| [API компоновки и LTO](plugin-api/link-lto.ru.md) | Граф компоновки, разрешение символов, GC/ICF, провайдеры компоновщика и LTO |
| [API DynCode](plugin-api/dyncode.ru.md) | Плоские позиционно-независимые образы, понижение импортов, кодирование набора символов |
| [Пользовательские соглашения о вызовах](plugin-api/custom-callconv/README.ru.md) | Плагины соглашений о вызовах, управляемые данными |

---

## Дорожная карта

Основные запланированные направления проекта NeverC: стандартная библиотека, бэкенд EVM для смарт-контрактов, бэкенд Solana eBPF.

**[Дорожная карта →](roadmap/README.ru.md)**

| Функция | Описание |
|---------|----------|
| Стандартная библиотека (`std`) | Пакеты в стиле Go: `fmt`, `os`, `io`, `net`, `crypto`, `encoding`, `sync` и другие |
| Пакет плагинов обфускации (`neverc-obfuscation`) | VM, MBA, выравнивание потока управления, полиморфный движок, защита от модификации — плагины первой стороны |
| Библиотека UI-компонентов (`neverc-ui`) | Кроссплатформенный UI в духе Qt, HTML/JS/CSS-рендерер, визуальный конструктор drag-and-drop, ИИ-нативный рабочий процесс |
| IDE и языковые инструменты (`neverc-ide`) | Расширение VSCode + автономная IDE для файлов `.nc`, IntelliSense, отладка, визуализация dyncode-конвейера |
| Смарт-контракты EVM | Компиляция C в байткод EVM — смарт-контракты на C вместо Solidity |
| Solana eBPF | Компиляция C в байткод eBPF Solana — разработка ончейн-программ на C |

---

## CLI-инструменты

Пользовательские команды помимо одиночной компиляции.

| Документ | Описание |
|----------|----------|
| [`neverc run`](run/README.ru.md) | Скомпилировать, запустить локально и удалить временный бинарник (как `go run`) |
| [`neverc update`](update/README.ru.md) | Апгрейд или откат release-установки (компилятор + установленные runtime на один тег) |
| [`neverc runtime`](runtime/README.ru.md) | Установка, список, обновление или удаление sysroot кросс-компиляции |
| [`neverc build` / `neverc make`](build/README.ru.md) | GNU Make–совместимый драйвер для Makefile примеров и проектов |
| [Релизные бинарные файлы и `--strip`](release-builds/README.ru.md) | Удалить ненужные во время выполнения символы и исходную отладку из итоговых ELF, Mach-O и PE/COFF |

---

## Локальная разработка

Сборка NeverC из исходного кода и настройка локальной среды разработки, включая конфигурацию PATH.

**[Локальная разработка →](local-dev/README.ru.md)**

---

## Примеры

Собираемые примеры, демонстрирующие кроссплатформенную компиляцию NeverC. Все кросс-компилируются с macOS / Linux.

**[Примеры →](examples/README.ru.md)**
