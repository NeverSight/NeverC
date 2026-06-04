**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Индекс документации](../README.ru.md)

# Дорожная карта NeverC

Этот документ описывает основные запланированные направления развития проекта NeverC помимо существующего компилятора shellcode и встроенных сред выполнения.

---

## 1. Стандартная библиотека (`std`)

NeverC предоставит комплексную стандартную библиотеку по образцу стандартной библиотеки Go — пакеты «с батарейками», покрывающие типичные потребности системного программирования без внешних зависимостей.

### Запланированные пакеты

| Пакет | Описание |
|-------|----------|
| `fmt` | Форматированный ввод/вывод (семейство printf + типобезопасные расширения) |
| `os` | Взаимодействие с ОС: переменные окружения, управление процессами, права доступа к файлам |
| `io` | Интерфейсы Reader/Writer, буферизованный ввод/вывод, утилиты pipe |
| `fs` | Операции с файловой системой: обход, glob, временные файлы, атомарная запись |
| `net` | TCP/UDP-сокеты, DNS-разрешение, HTTP-клиент/сервер |
| `net/http` | HTTP/1.1 и HTTP/2 клиент и сервер |
| `crypto` | Хеширование (SHA-256, SHA-512, BLAKE3), HMAC, AES, ChaCha20, RSA, Ed25519 |
| `encoding` | JSON, Base64, Hex, CSV, бинарный (little/big endian) |
| `sync` | Mutex, RWLock, WaitGroup, Once, атомарные операции |
| `time` | Монотонные/настенные часы, длительность, таймеры, форматирование |
| `bytes` | Манипуляции срезами байтов, буфер |
| `math` | Константы, элементарные функции, генерация случайных чисел |
| `sort` | Обобщённая сортировка и поиск |
| `container` | Связный список, куча, кольцевой буфер |
| `log` | Структурированное логирование с уровнями |
| `flag` | Разбор флагов командной строки |
| `path` | Манипуляции путями (POSIX и Windows) |
| `regexp` | Сопоставление регулярных выражений (синтаксис RE2) |
| `compress` | gzip, zlib, zstd, lz4 |
| `hash` | CRC32, CRC64, FNV, xxHash |
| `unicode` | Таблицы Unicode, свёртка регистра, преобразование UTF-8/UTF-16 |

### Принципы проектирования

- **Чистый C23** — каждый пакет компилируется как стандартный NeverC/C23; без скрытого C++ или платформоспецифического ассемблера
- **Ноль внешних зависимостей** — стандартная библиотека встраивается как LLVM bitcode в компилятор, аналогично существующим built-in `string` и `mimalloc`
- **Кроссплатформенность** — все пакеты работают на macOS, Linux и Windows (x86_64 / AArch64)
- **Совместимость с shellcode** — пакеты, имеющие смысл в freestanding-режиме (например, `crypto`, `encoding`, `bytes`), работают с `-fshellcode`

---

## 2. Obfuscation Plugin Suite (`neverc-obfuscation`)

NeverC will ship a first-party suite of code obfuscation plugins — reference implementations that demonstrate the Plugin API's full capabilities while providing production-grade code protection out of the box.

### Planned Plugins

| Plugin | Hook Point | Description |
|--------|-----------|-------------|
| Junk Code Insertion | `RunAfterFinalMIR` | Insert semantically dead but syntactically valid instruction sequences between real basic blocks |
| Opaque Predicates | `RunBeforePreEmit` | Insert always-true/always-false branches guarded by number-theoretic invariants; adds dead paths that confuse analysis |
| Control Flow Flattening | `RunAfterStackify` | Scatter basic blocks into a switch-dispatched loop; destroys natural CFG structure for decompilers |
| Anti-Tamper | `RunPostFinalize` | Embed self-integrity checks (CRC/hash of code sections) that trigger failure on patching |
| Polymorphic Engine | `RunPostExtract` | Seed-based output variation — each compilation produces functionally equivalent but structurally different code; defeats signature-based detection |
| MBA (Mixed Boolean Arithmetic) | `RunAfterInlining` | Replace arithmetic/boolean expressions with equivalent but opaque MBA forms (e.g., `x + y` → `(x ^ y) + 2 * (x & y)` chains); resists symbolic execution |
| VM (Code Virtualization) | `RunAfterFinalIR` | Convert functions into custom bytecode executed by an embedded interpreter; defeats static disassembly and signature matching |

### Design Principles

- **Pure Plugin API** — every obfuscation ships as a `.dll` / `.so` / `.dylib` plugin; no compiler fork required
- **Composable** — plugins stack: apply MBA first, then flatten, then virtualize — each pass is independent
- **Configurable** — per-function annotations (`__attribute__((obfuscate("vm")))`) to selectively protect hot paths without whole-program overhead
- **Auditable** — each plugin logs its transformations for security review; before/after IR diff output available via `-fshellcode-dump-ir`
- **Shellcode-compatible** — all plugins work in `-fshellcode` mode; generated code remains position-independent

---

## 3. Библиотека UI-компонентов (`neverc-ui`)

NeverC предоставит кроссплатформенную библиотеку UI-компонентов в духе Qt — но с HTML/JS/CSS фронтенд-рендерером, изначально подходящим для проектирования интерфейсов с помощью ИИ.

### Цели

- **Компонентная архитектура** — окна, кнопки, текстовые поля, списки, деревья, таблицы, меню, диалоги, вкладки и контейнеры компоновки как первоклассные типы C
- **HTML/JS/CSS-рендерер** — UI отрисовывается через встроенный легковесный браузерный движок; разработчики пишут логику на C, визуальный слой использует стандартные веб-технологии
- **Визуальный дизайнер с drag-and-drop** — GUI-конструктор, генерирующий C-код, совместимый с NeverC; быстрое прототипирование без ручного написания кода компоновки
- **ИИ-нативный рабочий процесс** — LLM могут генерировать бизнес-логику на C и HTML/CSS-компоновку за один проход
- **Нативный внешний вид** — адаптивные темы для платформ (macOS, Windows, Linux) через CSS-переменные и определение системных шрифтов/цветов
- **Лёгкое встраивание** — рендерер поставляется как встроенная среда выполнения (как `string` / `mimalloc`); без Electron-масштабных накладных расходов
- **Система событий** — C-коллбэки для пользовательских взаимодействий (клик, ввод, изменение размера, перетаскивание, клавиатура, пользовательские события)
- **Привязка данных** — декларативная привязка между C-структурами и состоянием UI; изменения распространяются автоматически
- **Пользовательский рендеринг** — доступ к raw canvas/WebGL для игровых UI, визуализации данных или пользовательских виджетов

### Почему HTML/CSS для UI-библиотеки на C?

- Каждая ИИ-модель уже знает HTML/CSS — генерация UI-кода не требует специализированного обучения
- Веб-технологии — наиболее проверенная система компоновки; нет необходимости заново изобретать flexbox, grid или рендеринг текста
- Инструменты исследования безопасности (дашборды, hex-просмотрщики, анализаторы пакетов) выигрывают от богатых стилизованных интерфейсов без изучения проприетарного API виджетов
- Визуальный дизайнер экспортирует HTML-шаблоны, работающие как в приложении NeverC, так и в отдельном браузере

---

## 4. IDE & Language Tooling (`neverc-ide`)

NeverC will provide first-class IDE support for the `.nc` language extension — a VSCode extension for immediate productivity and a standalone NeverC IDE for a fully integrated development experience.

### VSCode Extension

- **Syntax highlighting** — full `.nc` grammar with semantic token support for NeverC-specific types (`string`, `u8`–`u64`, `i8`–`i64`, `f32`, `f64`)
- **IntelliSense** — auto-completion for built-in types, dot-call methods (`.c_str()`, `.len()`, `.starts_with()`), and `#include` paths
- **Diagnostics** — real-time error and warning display from `neverc` compiler output
- **Go to definition** — jump to function, struct, and macro definitions across translation units
- **Hover documentation** — inline docs for built-in functions, compiler intrinsics, and standard library packages
- **Code actions** — quick-fix suggestions for common errors, auto-import for `std` packages
- **Debugging** — integrated LLDB/GDB debug adapter with breakpoint, step, and variable inspection support
- **Shellcode mode** — syntax-aware features for `-fshellcode` pipelines: bad-byte highlighting, shellcode size display, target-specific completions
- **Plugin API integration** — plugin hook point visualization and scaffolding

### Standalone IDE

- **Built on NeverC UI (`neverc-ui`)** — the IDE is itself a showcase of the HTML/JS/CSS component library, dogfooding the UI framework
- **Integrated terminal** — build, run, and debug without leaving the IDE
- **Visual shellcode pipeline** — graphical view of the IR → MIR → extraction pipeline with pass-by-pass output inspection
- **Project templates** — one-click scaffolding for hosted binaries, shellcode, EVM contracts, and Solana programs
- **AI-assisted coding** — built-in LLM integration that understands NeverC semantics, generates `.nc` code, and explains compiler diagnostics
- **Cross-compilation dashboard** — visual target selector with platform matrix and build status

### Why Both VSCode and Standalone?

- VSCode captures the majority of developers who already live in that ecosystem
- The standalone IDE provides a deeper, purpose-built experience for security researchers who want shellcode pipeline visualization and integrated binary analysis
- Both share the same language server backend — improvements benefit both simultaneously

---


## 5. Бэкенд EVM для смарт-контрактов

NeverC будет поддерживать компиляцию исходного кода на C в байткод EVM (Ethereum Virtual Machine) — позволяя разработчикам писать смарт-контракты на C вместо Solidity.

### Цели

- **Новый бэкенд LLVM** — целевая тройка `evm` (например, `neverc --target=evm hello.c -o contract.bin`)
- **Совместимость ABI** — генерация дескрипторов ABI, совместимых с Solidity, для взаимодействия с существующими инструментами Ethereum (Hardhat, Foundry, ethers.js)
- **Раскладка хранилища** — отображение структур C на слоты хранилища EVM с детерминированной раскладкой
- **Встроенные примитивы EVM** — `msg.sender`, `msg.value`, `block.number`, `tx.origin` как встроенные переменные или интринсики
- **Модификаторы payable / view / pure** — атрибуты функций, отображаемые на семантику видимости Solidity
- **Генерация событий** — генерация опкодов `LOG0`–`LOG4` из аннотированных вызовов функций
- **Оптимизация gas** — IR-проходы для минимизации стоимости gas (планирование стека, свёртка констант, удаление мёртвого хранилища)
- **revert / require** — примитивы обработки ошибок с пользовательскими сообщениями

### Почему C для EVM?

- Синтаксис Solidity знаком JavaScript-разработчикам, но чужд системным программистам; C универсален
- Существующий конвейер IR-оптимизации NeverC может генерировать более компактный байткод, чем `solc`, во многих случаях
- Исследователи безопасности уже мыслят на C — писать инструменты аудита и фаззеры на C для C-контрактов естественно
- API плагинов позволяет пользовательские проходы анализа gas и обнаружения уязвимостей на этапе компиляции

---

## 6. Бэкенд Solana eBPF

NeverC будет поддерживать компиляцию исходного кода на C в байткод eBPF Solana — обеспечивая разработку ончейн-программ на C.

### Цели

- **Цель eBPF** — целевая тройка `sbf` (Solana BPF) (например, `neverc --target=sbf-solana hello.c -o program.so`)
- **Привязки к среде выполнения Solana** — встроенные заголовочные файлы для системных вызовов Solana: `sol_invoke_signed`, `sol_log`, `sol_memcpy`, структуры информации об аккаунте
- **Модель аккаунтов** — наложение структур C на данные аккаунтов Solana с автоматической сериализацией/десериализацией
- **CPI (межпрограммный вызов)** — типобезопасные обёртки для вызова других ончейн-программ
- **PDA (программно-производный адрес)** — встроенные функции для деривации и верификации PDA
- **Учёт вычислительного бюджета** — предупреждения компилятора при превышении расчётных вычислительных единиц
- **Совместимость с Anchor** — опциональная генерация IDL для интероперабельности с фронтендами на базе Anchor

### Почему C для Solana?

- Среда выполнения Solana уже исполняет eBPF — C является наиболее естественным исходным языком для BPF-целей
- Существующие C-based BPF тулчейны (clang + solana-bpf) требуют сложной настройки; NeverC объединяет всё в одном бинарнике
- Производительно-критичные программы выигрывают от абстракций C с нулевыми накладными расходами и оптимизационных проходов NeverC
- Опыт компиляции shellcode (позиционно-независимый, минимальный runtime) напрямую переносится на ограничения ончейн-программ

---

## Хронология

Эти функции находятся на стадии исследования и проектирования. Конкретные даты релиза не указаны. Прогресс будет обновляться в этом документе и анонсироваться на странице релизов проекта.

| Функция | Статус |
|---------|--------|
| Стандартная библиотека (`std`) | Исследование / Проектирование |
| Obfuscation Plugin Suite (`neverc-obfuscation`) | Исследование / Проектирование |
| Библиотека UI-компонентов (`neverc-ui`) | Исследование / Проектирование |
| IDE и языковые инструменты (`neverc-ide`) | Исследование / Проектирование |
| Бэкенд EVM для смарт-контрактов | Исследование / Проектирование |
| Бэкенд Solana eBPF | Исследование / Проектирование |
