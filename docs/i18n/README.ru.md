**Языки**: [English](../../README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

<div align="center">

# NeverC

**Компилятор C23 для исследований безопасности на базе LLVM**

Встроенный линкер · Конвейер shellcode · Встроенные среды выполнения (`string` · `mimalloc`)

[![AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](../../LICENSE)
[![C23](https://img.shields.io/badge/Standard-C23-brightgreen.svg)](#возможности)
[![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-informational.svg)](#кросс-компиляция-под-windows)
[![Arch](https://img.shields.io/badge/Arch-x86__64%20%7C%20AArch64-orange.svg)](#возможности)

[Документация](../README.ru.md) · [Руководство shellcode](../shellcode-compiler/README.ru.md) · [Встроенные среды выполнения](../builtins/README.ru.md)

</div>

---

> **Примечание:** GitHub всегда показывает на главной репозитория `README.md` (английский), без автоопределения языка браузера. Используйте ссылки языков выше; в [документации](../README.ru.md) и [руководстве shellcode](../shellcode-compiler/README.ru.md) сохраняйте ту же локаль через языковую панель и хлебные крошки.

## Обзор

NeverC компилирует стандартный C в hosted-бинарники, freestanding-исполняемые файлы и позиционно-независимый shellcode — всё из одной цепочки инструментов. Поддерживаются **x86_64** и **AArch64** (только little-endian).

## Возможности

- **[Компилятор shellcode](../shellcode-compiler/README.ru.md)** — многоступенчатый конвейер IR/MIR, кроссплатформенное извлечение, разрешение импортов/syscall, режим ядра, аудит запрещённых байт, архитектура плагинов
- **Интегрированный линкер** — COFF, ELF и Mach-O в одном бинарнике; внешние `ld` и `link.exe` не нужны
- **Кросс-компиляция** — PE Windows с macOS/Linux и встроенным MSVC SDK
- **[Встроенные среды выполнения](../builtins/README.ru.md)** — встроенные в компилятор LLVM bitcode среды: [`string`](../builtins/string/README.ru.md) (строковый тип с семантикой значения, автоуправление памятью) и [`mimalloc`](../builtins/mimalloc/README.ru.md) (прозрачная замена аллокатора высокой производительности)
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

> **Примечание:** Встроенный тип **`string`** требует **`-fbuiltin-string`** для файлов `.c`. Он автоматически включается для [**файлов `.nc`**](../nc-extension/README.ru.md) и в режиме **`-fshellcode`**.

```bash
# macOS arm64 / x86_64
neverc -fshellcode -target arm64-apple-macos hello.c -o hello.bin
neverc -fshellcode -target x86_64-apple-macos hello.c -o hello.bin

# iOS arm64
neverc -fshellcode -target arm64-apple-ios hello.c -o hello.bin

# Linux x86_64 / arm64
neverc -fshellcode -target x86_64-linux-gnu hello.c -o hello.bin
neverc -fshellcode -target aarch64-linux-gnu hello.c -o hello.bin

# Android arm64 / x86_64
neverc -fshellcode -target aarch64-linux-android hello.c -o hello.bin
neverc -fshellcode -target x86_64-linux-android hello.c -o hello.bin

# Windows x86_64 / arm64
neverc -fshellcode -target x86_64-pc-windows-msvc hello.c -o hello.bin
neverc -fshellcode -target aarch64-pc-windows-msvc hello.c -o hello.bin
```

Подробности: **[индекс документации](../README.ru.md)** — дизайн, матрица платформ, справочник CLI, примеры.

## Готовые бинарники macOS

Релиз подписан в ad‑hoc (без Apple Developer ID, без нотаризации). Если вы скачали его через браузер, после распаковки достаточно один раз снять атрибут quarantine:

```bash
xattr -dr com.apple.quarantine /path/to/extracted/install
```

## Сборка

### Требования

- CMake 3.20+
- Ninja
- Хост-компилятор C++17 (GCC, Clang или MSVC)

### Конфигурация

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake
```

### Сборка

```bash
cmake --build build-neverc --target neverc
```

`ccache` / `sccache` определяется автоматически и включается при наличии.

### Тестирование

```bash
cmake --build build-neverc --target neverc-tests
ctest --test-dir build-neverc --output-on-failure
```

### Проверка

```bash
./build-neverc/bin/neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
./build-neverc/bin/neverc -c /tmp/hello.c -o /tmp/hello.o
```

## Кросс-компиляция под Windows

Разместить splat SDK [xwin](https://github.com/Jake-Shadle/xwin) в `build-neverc/sdk/msvc/`.

```bash
./build-neverc/bin/neverc --target=x86_64-pc-windows-msvc \
  -fbuiltin-string -o hello.exe hello.c -lkernel32
```

Shellcode для Windows (`-fshellcode`, разрешение через PEB и т.д.): [документация компилятора shellcode](../shellcode-compiler/README.ru.md).

## Участие в разработке

Ветка разработки по умолчанию — **`dev`**. Перед началом работы клонируйте репозиторий и переключитесь на `dev`; pull request отправляйте в `dev`.

```bash
git clone https://github.com/NeverSight/NeverC.git
cd NeverC
git checkout dev
```

## Лицензия

[AGPL-3.0](../../LICENSE)

Компоненты LLVM сохраняют лицензию [Apache-2.0 WITH LLVM-exception](../../llvm/LICENSE.TXT).
