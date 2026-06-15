**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Документация](../README.ru.md) · [← Проект NeverC](../../docs/i18n/README.ru.md)

# Примеры NeverC

Полные собираемые примеры, демонстрирующие кроссплатформенную компиляцию NeverC. Все кросс-компилируются с macOS / Linux — среда Windows не требуется.

---

## Доступные примеры

| Пример | Описание | Ключевые возможности |
|--------|----------|---------------------|
| [Драйвер ядра Windows](../../examples/windows-driver/README.ru.md) | Минимальный WDM-драйвер ядра | Кросс-компиляция `.sys` с macOS/Linux, авто-LTO, встроенный линкер |
| [Драйвер Windows + CET](../../examples/windows-driver-cet/README.ru.md) | Драйвер с Intel CET Shadow Stack | CET-совместимый код ядра, `/guard:ehcont` |
| [Драйвер Windows + плавающая точка](../../examples/windows-driver-float/README.ru.md) | Драйвер с плавающей точкой/SIMD | Безопасная плавающая точка в режиме ядра |
| [Windows Ring3 EXE](../../examples/windows-exe/README.ru.md) | Консольное приложение пользовательского режима | GetSystemInfo, перечисление процессов, VirtualAlloc |
| [Windows Ring3 DLL](../../examples/windows-dll/README.ru.md) | DLL пользовательского режима | ReadProcessMemory, VirtualAllocEx, перечисление модулей |

### Linux

| Пример | Описание | Ключевые особенности |
|--------|---------|---------------------|
| [Linux Hello World](../../examples/linux-hello/README.ru.md) | Минимальная программа на C | Кросс-компиляция с macOS/Windows |
| [Linux POSIX](../../examples/linux-posix/README.ru.md) | Системное программирование POSIX | pthreads, mmap, pipe, сигналы |
| [Linux Статический](../../examples/linux-static/README.ru.md) | Полностью статический бинарник | Линковка `-static` |
| [Linux Сеть](../../examples/linux-network/README.ru.md) | Демо TCP-сокетов | Клиент/сервер |
| [Linux Math + zlib](../../examples/linux-math/README.ru.md) | Математика + сжатие | Тригонометрия, zlib, CRC32 |

### macOS

| Пример | Описание | Ключевые особенности |
|--------|---------|---------------------|
| [Приложение macOS](../../examples/macos-app/README.ru.md) | Нативный исполняемый файл Mach-O | sysctl, uname, Mach host_info/task_info, информация о процессах |
| [Динамическая библиотека macOS](../../examples/macos-dylib/README.ru.md) | Нативная `.dylib` библиотека | Mach vm_read/vm_write, vm_alloc/vm_dealloc, task_info, XOR |

### Android

| Пример | Описание | Ключевые особенности |
|--------|---------|---------------------|
| [Android ELF](../../examples/android-elf/README.ru.md) | Нативный ARM64-бинарник для рутированных устройств | Кросс-компиляция для Android, dlopen/liblog, /proc, проверка root |
| [Общая библиотека Android](../../examples/android-so/README.ru.md) | Нативная ARM64 `.so` библиотека | Разделяемая библиотека, mmap RWX, XOR-шифрование |

---

## Быстрый старт

```bash
cd examples/<имя-примера>
neverc make
```

Указать путь компилятора: `make NEVERC=/path/to/neverc`

Все примеры используют **neverc** и генерируют бинарники Windows PE (`.sys`) через встроенный линкер.
