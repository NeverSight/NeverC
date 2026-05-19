**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Компилятор shellcode](../README.ru.md)

# Руководство по расширению платформ

Этот документ объясняет, как расширить компилятор shellcode для новых целевых платформ. Текущая поддержка: **arm64 / x86_64 на macOS / Linux / Android / Windows** (8 троек), каждая с независимыми контекстами **User** / **Kernel** (16 вариантов). Добавление новой платформы обычно требует нескольких сотен строк кода.

## Философия проектирования: Табличное управление, а не ветвление

Все проходы независимы от цели. Платформенные различия сосредоточены в **двух местах**:

1. Записи таблицы `describeTriple()` в `TargetDesc.cpp`
2. Архитектурные switch трёх экстракторов (Mach-O / ELF / COFF)

Добавление платформы = одна строка в (1) + один case в (2).

## Шаги

### 1. Добавить строку в `TargetDesc`

Добавить соответствующую ветку ОС в `describeTriple()`:

```cpp
if (TT.isOSFreeBSD()) {
  D.OS = ShellcodeOS::FreeBSD;
  D.Format = ObjectFormat::ELF;
  D.TextSectionName = ".text";
  if (D.Arch == ShellcodeArch::X86_64) {
    D.Syscall = SyscallABI::FreeBSDSyscall;
    D.AsmTemplate = "syscall";
    D.SyscallNumberReg = "rax";
    D.SyscallRetReg = "rax";
    D.ArgRegs = kX86_64FreeBSDArgRegs;
    D.NumArgRegs = 6;
    D.DriverInjectFlags = kX86_64UnixInjectFlags;
  }
  return D;
}
```

**Обязательные поля** (все в `TargetDesc.h`):

| Поле | Назначение | При отсутствии |
|------|-----------|----------------|
| `OS` / `Arch` / `Format` | Ключ диспетчеризации | `describeTriple` возвращает Unknown → драйвер отклоняет рано |
| `TextSectionName` | Экстрактор ищет секцию входа | `.text` не найден → отказ |
| `Syscall` | Решение о замене SyscallStubPass | `None` → SyscallStubPass no-op |
| `AsmTemplate` / `SyscallNumberReg` / `SyscallRetReg` / `ArgRegs` | Генерация InlineAsm SyscallStubPass | Любое пустое → SyscallStubPass no-op |
| `TCBReadAsm` / `TCBReadConstraint` | InlineAsm чтения TEB WinPEBImportPass | Пустое → PEB walk генерирует пустой InlineAsm (Windows: обязательно) |
| `DriverInjectFlags` | Платформенные флаги в режиме shellcode | null → без инъекции |

### 2. Расширить `SyscallStub` / `SyscallTables` (если у ОС есть ловушки ядра)

- Добавить значение enum в `SyscallABI` в `TargetDesc.h`
- Добавить `kXxxTable` в `SyscallTables.cpp`
- Добавить case в switch `lookupSyscall`
- `SyscallStubPass` без изменений — шаблоны/ограничения InlineAsm из `TargetDesc`

### 2.5 Расширение белого списка Win32 API Windows

Windows не имеет стабильного ABI системных вызовов. Белый список — мульти-DLL таблица в `Tables/Win32Apis.def`.

**Добавить API**: 1 строка в `Win32Apis.def` + 1 объявление в `lib/Headers/windows.h`.

### 3. Расширить соответствующий экстрактор

1. Определить типы перемещений → патчить байты или отклонить
2. Обновить список запрещённых имён секций данных
3. Обновить валидацию диапазона цели перемещения вход-на-смещении-0

### 4. Добавить Loader (только инструмент тестирования)

Ссылка `loader_linux.c` и `loader_windows.c`. Типично: `mmap(RWX) → memcpy → icache flush → call`.

### 5. Обновить тесты

Добавить проверку кросс-компиляции в `tests/neverc/ShellcodeCrossTargetTests.cpp`.

---

## Известные кроссплатформенные подводные камни

- **Порядок байтов**: NeverC поддерживает только little-endian (LE).
- **Различия ABI**: Win64 vs System V AMD64 имеют полностью различные регистры аргументов. Обрабатывается на уровне фронтенда Clang.
- **Номера syscall**: На Linux различаются по архитектуре, Android = Linux, Darwin имеет собственные номера BSD, Windows без стабильных номеров (PEB walk).
- **Когерентность кэша**: ARM требует явный сброс i-кэша; x86 нет.
- **SELinux / W^X**: Android ограничен SELinux `execmem`; не-jailbreak iOS полностью отклоняет `mmap(RWX)`.

## Дорожная карта будущих расширений

| Цель | Оценка трудозатрат | Зависимости |
|------|-------------------|-------------|
| **iOS arm64** (jailbreak / `MAP_JIT`) | 1 день | Переиспользование Mach-O экстрактора |
| **FreeBSD / OpenBSD x86_64** | Полдня | Переиспользование ELF экстрактора + новая таблица syscall |
| **RISC-V64 Linux** | 2 дня | Нужен RISC-V TargetDesc + новый вариант AllBlr + патчинг перемещений RISC-V |

## Интерфейс расширения прохода обфускации

Конвейер shellcode предоставляет 11 хуков через `Pipeline.h::ObfuscationHooks` для сторонних библиотек обфускации. Встроенный MIR-патчинг также табличный: `Tables/MIRRewritePatterns.def` и `Tables/MIRRewriteOpcodes.def`. Предпочитайте записи в таблицах и узкие хелперы вместо разбрасывания целевых ветвей в теле прохода.
