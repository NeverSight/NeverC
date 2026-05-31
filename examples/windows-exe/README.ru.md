**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Пример Windows Ring3 EXE

Исполняемый файл Windows пользовательского режима, кросс-компилированный с NeverC. Демонстрирует Win32 API.

## Сборка

```bash
cd examples/windows-exe
make
```

## Ручная сборка

```bash
neverc --target=x86_64-pc-windows-msvc -Wall -fms-extensions -fms-compatibility -D_AMD64_ -Xlinker --subsystem=console -lkernel32 -luser32 -lmsvcrt -o example.exe main.c
```

## Функциональность

- Запрос информации о системе через `GetSystemInfo`
- Перечисление процессов через `CreateToolhelp32Snapshot`
- Демо `VirtualAlloc`/`VirtualQuery`/`VirtualFree`

