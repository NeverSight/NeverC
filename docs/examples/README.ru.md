**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Документация](../README.ru.md) · [← Проект NeverC](../../docs/i18n/README.ru.md)

# Примеры NeverC

Полные собираемые примеры, демонстрирующие кроссплатформенную компиляцию NeverC. Все кросс-компилируются с macOS / Linux — среда Windows не требуется.

---

## Доступные примеры

### Windows

| Пример | Описание | Ключевые возможности |
|--------|----------|---------------------|
| [Драйвер ядра Windows](../../examples/windows-driver/README.ru.md) | Минимальный WDM-драйвер ядра | Кросс-компиляция `.sys` для **x64** (по умолчанию) и **ARM64**, авто-LTO, встроенный линкер |
| [Драйвер Windows + CET](../../examples/windows-driver-cet/README.ru.md) | Драйвер с Intel CET Shadow Stack | CET-совместимый код ядра (**только x64**), `/guard:ehcont` |
| [Драйвер Windows + плавающая точка](../../examples/windows-driver-float/README.ru.md) | Драйвер с плавающей точкой/SIMD | Безопасная плавающая точка в режиме ядра на **x64** и **ARM64** |
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

### Модули ядра Android (.ko)

Исходный код ядра не требуется — NeverC компилирует против встроенного минимального рантайма. Один файл поддерживает GKI 5.10–6.12.

| Пример | Описание | Ключевые особенности |
|--------|---------|---------------------|
| [Kernel Hello](../../examples/android-kernel-hello/README.ru.md) | Минимальный `.ko` модуль | Бутстрап kallsyms через kprobe, простейшая проверка insmod |
| [Шаблон драйвера](../../examples/android-kernel-driver/README.ru.md) | Шаблон динамического разрешения символов | `kallsyms_lookup_name`, стабильный ABI GKI, 5.10–6.12 |
| [Kernel Inline Interpose](../../examples/android-kernel-inline-interpose/README.ru.md) | Инлайн-хук на `do_faccessat` | BTI/PAC-безопасный патч, контекстный хук, PC-относительная релокация |
| [Kernel Syscall Interpose](../../examples/android-kernel-syscall-interpose/README.ru.md) | Таблица syscall / inline / context interpose | Замена `sys_call_table`, инлайн-хук, контекстный хук |
| [Kernel Lowvis](../../examples/android-kernel-lowvis/README.ru.md) | Управление видимостью модуля | Видимость list/sysfs/proc, обёртки учётных данных, состояние enforcement SELinux |
| [Kernel Full SDK](../../examples/android-kernel-full/README.ru.md) | Полная интеграция SDK | Netlink IPC, interpose, обёртки учётных данных, видимость модуля, управление политикой SELinux, VMA, файловый I/O |
| [Kernel Chardev](../../examples/android-kernel-chardev/README.ru.md) | Символьное устройство + ioctl | `misc_register`, диспетчеризация ioctl, `/proc` seq_file |
| [Kernel Netlink](../../examples/android-kernel-netlink/README.ru.md) | Двунаправленный netlink IPC | Команды PING/VERSION/ECHO, `nvk_nl_open`/`nvk_nl_reply` |
| [Kernel Probe](../../examples/android-kernel-probe/README.ru.md) | Зонд на произвольной инструкции | `neverc_krt_probe_register`, полный контекст регистров, цепочка по приоритету, пропуск/перенаправление |
| [Kernel Multi-File](../../examples/android-kernel-multifile/README.ru.md) | Модуль ядра из нескольких файлов | Один `NEVERC_KRT_BOOTSTRAP()`, общее состояние `weak_odr`, разделение init/interpose/утилит |

---

## Быстрый старт

Каждый пример следует одному шаблону:

```bash
cd examples/example-name
neverc make
```

При необходимости переопределите путь к компилятору:

```bash
neverc make NEVERC=/path/to/neverc
```

Примеры драйверов Windows выбирают архитектуру через `ARCH` (по умолчанию x64).
Пример CET — только x64: CET — это функция x86:

```bash
neverc make ARCH=x64        # Build for x64 (default)
neverc make ARCH=arm64      # Build for ARM64
neverc make all-arch        # Build every architecture the example supports
neverc make TESTSIGN=1      # Attach an Authenticode test signature
```

Примеры Linux поддерживают выбор архитектуры:

```bash
neverc make TARGET=aarch64-linux-gnu   # Build for ARM64
neverc make TARGET=x86_64-linux-gnu    # Build for x86_64 (default)
```

Примеры macOS поддерживают выбор архитектуры:

```bash
neverc make TARGET=arm64-apple-macos     # Build for Apple Silicon (default)
neverc make TARGET=x86_64-apple-macos    # Build for Intel
```

Примеры Android по умолчанию нацелены на ARM64:

```bash
cd examples/android-elf
neverc make            # Build
neverc make run        # Build + push to device + run via adb
```

---

## Кроссплатформенные особенности

- **Единая тулчейн**: NeverC выполняет препроцессинг, компиляцию, оптимизацию (авто-LTO) и линковку за один вызов
- **Встроенный SDK**: Windows SDK/WDK, Linux sysroot (Ubuntu 22.04), macOS sysroot (macOS 14) и Android sysroot (NDK r26c, API 21+) встроены в `runtime/` — ноль внешних зависимостей
- **Независимость от хоста**: Сборка из macOS (arm64/x86_64), Linux (x86_64/aarch64) или Windows идентичными командами
- **Мультиплатформенность**: Кросс-компиляция в Windows PE (`.sys`/`.exe`/`.dll`), Linux ELF, macOS Mach-O (`.dylib`) и Android ELF с любого хоста
- **Поддержка отладки**: Передайте `-g` для отладочной информации DWARF; исследуйте с `llvm-dwarfdump`
