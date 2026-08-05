**Языки**: [English](../../README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

<div align="center">

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="../assets/neverc-logo-dark.svg">
  <img src="../assets/neverc-logo-light.svg" width="72" alt="NeverC">
</picture>

# NeverC

**ИИ-дружественный компилятор C23 для исследований безопасности — на базе LLVM**

Встроенный линкер · Конвейер dyncode · Встроенные среды выполнения (`string` · `mimalloc` · `xorstr` · `strhash`)

[![AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](../../LICENSE)
[![C23](https://img.shields.io/badge/Standard-C23-brightgreen.svg)](#возможности)
![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-informational.svg)
[![Arch](https://img.shields.io/badge/Arch-x86__64%20%7C%20AArch64-orange.svg)](#возможности)

[Документация](../README.ru.md) · [Руководство dyncode](../dyncode-compiler/README.ru.md) · [Встроенные среды выполнения](../builtins/README.ru.md) · [API плагинов](../plugin-api/README.ru.md) · [Дорожная карта](../roadmap/README.ru.md)

</div>

---

> **Примечание:** GitHub всегда показывает на главной репозитория `README.md` (английский), без автоопределения языка браузера. Используйте ссылки языков выше; в [документации](../README.ru.md) и [руководстве dyncode](../dyncode-compiler/README.ru.md) сохраняйте ту же локаль через языковую панель и хлебные крошки.

## Обзор

NeverC компилирует стандартный C в hosted-бинарники, freestanding-исполняемые файлы и позиционно-независимый dyncode — всё из одной цепочки инструментов. Поддерживаются **x86_64** и **AArch64** (только little-endian). В будущих версиях будут добавлены **EVM** (смарт-контракты Ethereum) и **Solana eBPF** (ончейн-программы) как цели компиляции.

## Почему NeverC?

C уже является простейшим системным языком. NeverC делает его ещё проще:

- **Чистый C23, ничего лишнего** — Никаких шаблонов, RAII, перегрузки операторов или скрытого потока управления. Что читаете — то и выполняется.
- **Встроенный `string`** — Строковый тип с семантикой значения, `+`, `==`, `.starts_with()` и автоматическим освобождением — без C++.
- **Никаких исключений** — Обработка ошибок всегда явная. Никакой раскрутки стека, никаких сюрпризов производительности.
- **Единый бинарник** — Компилятор + линкер + среды выполнения в одном исполняемом файле. Ноль внешних зависимостей.
- **Дружественный к LLM** — Минимальная грамматика и детерминированная семантика означают, что ИИ-сгенерированный код NeverC компилируется корректно чаще, чем альтернативы на C++.
- **Настоящая кросс-компиляция** — Собирайте Windows PE, Linux ELF, macOS Mach-O, Android ELF и dyncode из macOS или Linux — без VM, без двойной загрузки, без поиска SDK. SDK платформ встроены в компилятор.
- **Расширяемый без порога вхождения** — Единственный C-заголовок, 130 именованных фаз компиляции — и у вас [плагин компилятора](../plugin-api/README.ru.md), способный вмешаться на любом этапе от оптимизации IR до финального бинарного вывода — без знания LLVM.
- **Исследование безопасности встроено** — Компиляция dyncode, шифрование строк во время компиляции и кроссплатформенная генерация PE нативно встроены в компилятор, а не прикручены постфактум внешними скриптами.

## Возможности

- **[Компилятор dyncode](../dyncode-compiler/README.ru.md)** — многоступенчатый конвейер IR/MIR, кроссплатформенное извлечение, разрешение импортов/syscall, режим ядра, аудит запрещённых байт, архитектура плагинов
- **Интегрированный линкер** — COFF, ELF и Mach-O в одном бинарнике; внешние `ld` и `link.exe` не нужны
- **Кросс-компиляция** — Windows PE, Linux ELF, macOS Mach-O и Android ELF с любого хоста со встроенными SDK платформ
- **[Встроенные среды выполнения](../builtins/README.ru.md)** — встроенные в компилятор LLVM bitcode среды: [`string`](../builtins/string/README.ru.md) (строковый тип с семантикой значения, автоуправление памятью), [`mimalloc`](../builtins/mimalloc/README.ru.md) (прозрачная замена аллокатора высокой производительности, включена по умолчанию вне ядерных и freestanding-целей), [`xorstr`](../builtins/xorstr/README.ru.md) (шифрование строк во время компиляции с дешифровкой против сигнатур) и [`strhash`](../builtins/strhash/README.ru.md) (хеширование строк на этапе компиляции с тем же алгоритмом во время выполнения)
- **[API плагинов](../plugin-api/README.ru.md)** — чистый C ABI для внедеревных плагинов; SDK с одним заголовком, ноль зависимостей LLVM/CRT, охватывающий фазы драйвера, препроцессора, AST, IR, MIR, MC, объектного файла, компоновки, LTO и dyncode
- **[Расширение `.nc`](../nc-extension/README.ru.md)** — используйте `.nc` для автоматического включения всех возможностей NeverC (`string`, целочисленные типы в стиле Rust) без дополнительных флагов
- **Облегчённая сборка LLVM** — только бэкенды x86_64 / AArch64; пути C++/ObjC/OpenMP удалены

## Быстрый пример

```c
#include <stdio.h>

typedef struct { string user; string pass; } creds;

int main(void) {
    string msg = "Hello " + "NeverC!";
    printf("%s\n", msg.c_str());

    // Compile-time encryption — `strings ./bin` cannot find these literals
    creds login = {.user = "admin".encrypt(), .pass = "s3cret".encrypt()};
    string paths[] = {"/api/v1".encrypt(), "/api/v2".encrypt()};

    // Zero-allocation decrypt-and-compare (plaintext never fully in memory)
    if (login.user == "admin".encrypt() && login.pass == "s3cret".encrypt()) {
        for (int i = 0; i < 2; i++)
            if (msg.starts_with(paths[i]))
                printf("route matched: %s\n", paths[i].c_str());
    }
    return 0;
}
```

> **Примечание:** Встроенный тип **`string`** требует **`-fbuiltin-string`** для файлов `.c`. Он автоматически включается для [**файлов `.nc`**](../nc-extension/README.ru.md) и в режиме **`-fdyncode`**.

```bash
# macOS arm64 / x86_64
neverc -fdyncode -target arm64-apple-macos hello.c -o hello.bin
neverc -fdyncode -target x86_64-apple-macos hello.c -o hello.bin

# iOS arm64
neverc -fdyncode -target arm64-apple-ios hello.c -o hello.bin

# Linux x86_64 / arm64
neverc -fdyncode -target x86_64-linux-gnu hello.c -o hello.bin
neverc -fdyncode -target aarch64-linux-gnu hello.c -o hello.bin

# Android arm64 / x86_64
neverc -fdyncode -target aarch64-linux-android hello.c -o hello.bin
neverc -fdyncode -target x86_64-linux-android hello.c -o hello.bin

# Windows x86_64 / arm64
neverc -fdyncode -target x86_64-pc-windows-msvc hello.c -o hello.bin
neverc -fdyncode -target aarch64-pc-windows-msvc hello.c -o hello.bin
```

Подробности: **[индекс документации](../README.ru.md)** — дизайн, матрица платформ, справочник CLI, примеры. Полные собираемые примеры: **[examples](../examples/README.ru.md)**.

## Установка

На **Linux x64/arm64** и **macOS arm64** последний release ставится одной командой:

```bash
curl -fsSL https://raw.githubusercontent.com/NeverSight/NeverC/HEAD/install.sh | sh
```

Установщик скачивает архив release для вашей платформы, проверяет его по `SHA256SUMS`, устанавливает в `~/.neverc` и добавляет `~/.neverc/bin` в начало `PATH` shell.

Чтобы зафиксировать конкретную версию:

```bash
curl -fsSL https://raw.githubusercontent.com/NeverSight/NeverC/v3389.1.2/install.sh | NEVERC_VERSION=v3389.1.2 sh
```

Проверка установки:

```bash
neverc --version
neverc hello.c -o hello -fbuiltin-string
```

Чтобы запустить программу, не сохраняя бинарный файл, используйте
`neverc run -O2 hello.c Alice`. Параметры компилятора указываются перед
непрерывным списком исходников `.c`/`.nc`, а последующие аргументы передаются
программе. Для сложного вызова части можно явно разделить через `--`, например
`neverc run hello.c helper.o -O2 -- Alice`. Временный исполняемый файл наследует
текущий каталог, окружение и стандартные потоки, возвращает код завершения
программы и затем удаляется. Чтобы сохранить артефакт, вызывайте обычный
компилятор.

Пакеты **Windows x64/arm64** — на [GitHub Releases](https://github.com/NeverSight/NeverC/releases) для ручной загрузки. Бинарник macOS arm64 подписан Apple Developer ID и нотаризован.

Необязательные переменные окружения:

| Переменная | Назначение |
|------------|------------|
| `NEVERC_INSTALL_DIR` | Каталог установки (по умолчанию: `~/.neverc`) |
| `NEVERC_VERSION` | Тег release, напр. `v3389.1.2` (по умолчанию: latest) |
| `NEVERC_NO_MODIFY_PATH=1` | Не менять профиль shell |

Sysroot для кросс-компиляции (Windows SDK, Linux sysroot и т. д.) ставятся по требованию, когда компилятор уже в `PATH`:

```bash
neverc runtime install all
neverc runtime install windows-x64
neverc runtime list
```

В release-установке компилятор и уже установленные runtime для кросс-компиляции обновляются синхронно как единая версионированная система:

```bash
neverc update                 # последний полный release
neverc update v3389.1.2       # точная версия, включая откат
```

`neverc upgrade` — псевдоним этой команды. NeverC определяет один конкретный тег release и
переустанавливает только уже имеющиеся runtime, закрепляя все на целевой версии компилятора.
До изменения рабочих файлов все необходимые архивы скачиваются, проверяются по SHA256,
распаковываются и валидируются. Ошибка подготовки или проверки не меняет текущую установку,
а ошибка commit запускает автоматический откат. Если release runtime оказался неисправен,
`neverc update <предыдущая версия>` вместе откатит компилятор и все установленные runtime.

## Сборка из исходников

Требования, команды сборки, кросс-компиляция под Windows, настройка PATH и переключение между release и in-tree сборкой — см. **[Локальная разработка](../local-dev/README.ru.md)**.

## Участие в разработке

NeverC **намеренно поддерживает только C** (C23). C++, Objective-C, CUDA и похожие
языковые фронтенды вне области проекта; pull request с их добавлением будут закрыты.
Если нужна LLVM-цепочка, ориентированная на C++, рассмотрите
[llvm-msvc](https://github.com/backengineering/llvm-msvc).

Крупные изменения языка, ABI или runtime сначала обсудите в issue, и только потом
отправляйте pull request.

Ветка разработки по умолчанию — **`dev`**. Перед началом работы клонируйте репозиторий и переключитесь на `dev`; pull request отправляйте в `dev`.

```bash
git clone https://github.com/NeverSight/NeverC.git
cd NeverC
git checkout dev
```

## Лицензия

[AGPL-3.0](../../LICENSE)

Компоненты LLVM сохраняют лицензию [Apache-2.0 WITH LLVM-exception](../../llvm/LICENSE.TXT).
