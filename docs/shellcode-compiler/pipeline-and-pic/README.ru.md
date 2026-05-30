**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Компилятор shellcode](../README.ru.md)

# Конвейер shellcode, MIR и стратегия PIC (заметки по проектированию)

Этот документ описывает проектные компромиссы в режиме shellcode NeverC по цепочке **IR → оптимизация LLVM → бэкенд MIR → объектный файл → извлечение/патчинг** и связь с политикой **PIC по умолчанию на уровне компилятора**. Детали реализации авторитетны в исходном коде и английских комментариях.

## 1. Почему PIC по умолчанию принудителен (включая не-shellcode)

Экстрактор shellcode предполагает, что ссылки на внешние символы попадают на **PC-относительные** или внутри-`.text` разрешимые перемещения, а не на жёстко закодированные абсолютные адреса или пулы констант, требующие загрузчика для заполнения `.data`.

NeverC возвращает **true** из `Generic_GCC::isPICDefaultForced()`, `MachO::isPICDefaultForced()` и `MSVCToolChain::isPICDefaultForced()`, отличаясь от поведения Clang upstream «опциональный PIC по умолчанию»: **все кроссплатформенные компиляции всегда используют только PIC**. Это означает:

- Обычная компиляция C и компиляция `-fshellcode` разделяют одни привычки перемещений, снижая когнитивную нагрузку «работает нормально, ломается под shellcode».
- Бэкенды Linux / Android / macOS / Windows разделяют одни предположения под табличными дескрипторами (`TargetDesc` + `Options.td.h`), избегая жёсткого кодирования `if (linux)` в драйвере.

Эта политика не различает, включён ли `-fshellcode` или контекст user/kernel. Даже при передаче `-fno-pic` / `-static` / `-mkernel` / `-mdynamic-no-pic`, `ParsePICArgs()` сохраняет `Reloc::PIC_`.

## 2. Двухфазное разделение труда IR и MIR

### 2.1 Слой IR (`registerShellcodePasses`)

Отвечает за сжатие семантики «обычного C» в форму **единственный вход, без независимой секции данных, без проблемных глобалов**: `ZeroRelocPass`, `IndirectBrPass`, `MemIntrinPass`, `StringRuntimePass`, `HeapArenaPass`, `CompilerRtPass`, `SyscallStubPass`, `WinPEBImportPass`, `KernelImportPass` (только ядро), `Data2TextPass` и др.

**Принцип**: Задачи, решаемые в IR структурно, исправляются сначала в IR (пулы констант, BlockAddress, проваливание `memcpy` в libc, проваливание `__int128 /` в `__udivti3` и т.д.), упрощая байтовый поток для бэкенда и экстрактора. Для сценариев с высокой когнитивной нагрузкой, но безопасно интернализуемых, драйвер проактивно вводит правила (например, `long double` AArch64 Linux / Android / Windows понижается до binary64 в режиме shellcode). Только конструкции, не поддерживаемые без runtime, активируют диагностику MIR/экстрактора.

### 2.2 Слой MIR (`registerShellcodeMachinePasses`)

Регистрирует коллбэки в `TargetPassConfig` LLVM **после распределения регистров, перед `addPreEmitPass`**:

1. Пользователь/библиотека обфускации: `RunBeforePreEmit` (псевдоинструкции CFI / EH ещё присутствуют).
2. **`ShellcodeMIRPrepPass`**: Удаляет псевдоинструкции, которые порождали бы `.eh_frame` / `.pdata` / `.xray_*`.
3. Пользователь/библиотека обфускации: `RunAfterPreEmit` (подходит для подстановки инструкций, переименования регистров).

**Принцип**: Если нативные последовательности инструкций ещё имеют проблемы — исправлять в MIR; **извлечение и патчинг — последняя страховочная сеть**.

Имена опкодов MIR не разбросаны в потоке управления прохода; `ShellcodeMIRPrepPass` использует таблицу `Tables/MIRRewriteOpcodes.def`. При добавлении shellcode-дружественных подстановок предпочтительны записи в таблице; изменения выбора инструкций `.td` — крайний случай.

> Примечание: `ShellcodeMIRPrepPass` регистрируется только при `-fshellcode`. Обычные программы не должны глобально удалять CFI/EH.

Глобальные коллбэки IR и MIR используют паттерн **регистрация один раз, чтение текущего снапшота `ShellcodeOptions` во время выполнения**.

## 3. Табличные различия платформ

- **Triple → поведение**: В `describeTriple()` и полях `TargetDesc`. Для нового OS/Arch предпочтительно **добавление строки в таблицу**.
- **Опции CLI**: В `neverc/include/neverc/Invoke/Options.td.h`; потребляются через `OPT_*` перечисления.

## 4. Цепочка инструментов Windows MSVC и расположение SDK

NeverC поддерживает два источника SDK **без жёстко закодированных путей**:

1. **Встроенный SDK** (по умолчанию): NeverC поставляет полный Windows SDK и WDK в `runtime/`. Заголовки в `runtime/windows/shared/`, библиотеки по архитектуре в `runtime/windows/{x64,arm64}/`. Раскладка после сборки:

   ```
   build-neverc/bin/neverc
   build-neverc/runtime/windows/shared/msvc/  (заголовки)
   build-neverc/runtime/windows/x64/msvc/     (библиотеки x64)
   build-neverc/runtime/windows/arm64/msvc/   (библиотеки arm64)
   ```

2. **Явный sysroot в стиле VS** (опционально): через `-vctoolsdir=<path>` или `-winsysroot=<path>`. Указанный путь имеет приоритет над встроенным SDK.

Оба работают без реестра или переменных среды VS, обеспечивая кросс-компиляцию shellcode Windows из macOS / Linux.

## 5. Точки обфускации и расширения

- **IR-хуки**: 6 точек подключения IR (`NEVERC_HOOK_SC_BEFORE_PREP` — `NEVERC_HOOK_SC_AFTER_FINAL_IR`) через [API плагинов](../../plugin-api/README.ru.md). 11 хуков всего (6 IR + 3 MIR + 2 байтовый поток).
- **MIR обфускация**: `RunBeforePreEmit` / `RunAfterPreEmit` / `RunAfterFinalMIR`. `-fshellcode-mir-obfuscate=` для отдельного MIR spec.
- **Хуки байтового потока**: `RunPostExtract` (пре-финализация) и `RunPostFinalize` (пост-финализация).
- **Размер / выравнивание / заполнение**: `-fshellcode-max-length=`, `-fshellcode-align=`, `-fshellcode-pad=`.
- **Выбор дизайна**: Обфускация, полиморфизм, поэтапные кодировщики, непрямые системные вызовы **намеренно не встроены**, доступны только как опциональные плагины.

## 6. Измерение режима ядра (Ring-0)

`-mshellcode-context=user|kernel` как второе измерение конвейера:

- **Режим пользователя**: Конвейер PEB walk / syscall stub.
- **Режим ядра**: `SyscallStubPass` / `WinPEBImportPass` досрочно возвращают; `KernelImportPass` переписывает неразрешённые extern вызовы; `<neverc/kernel.h>` предоставляет типы ядра.

См. [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.ru.md).

## 7. Слой совместимости Windows POSIX

**Ноль осведомлённости пользователя**: Тот же C-исходник компилируется для всех 8 тройок без `#ifdef _WIN32`. `WinPEBImportPass` реализует 3 фазы: сканирование POSIX, генерация мостовых обёрток (13 групп функций), разрешение PEB. Детали: `write` → `GetStdHandle` + `WriteFile`, `mmap` → `VirtualAlloc`, `exit` → `ExitProcess` и т.д.

## 8. Автоисправление неявных объявлений K&R

`SyscallStubPass` поддерживает таблицу `getCanonicalSyscallType()` с 50+ каноническими POSIX-сигнатурами. Объявления K&R с 0 параметрами автоматически подставляются канонической сигнатурой.

## 9. Итоги

| Тема | Подход |
|------|--------|
| PIC по умолчанию | Все тулчейны `isPICDefaultForced()==true` |
| Сначала исправить в IR | Константы, непрямые переходы, интринсики памяти удалены в IR |
| Страховочная сеть MIR | `ShellcodeMIRPrepPass` + хуки до/после |
| Минимизация хардкода | `TargetDesc` + `Options.td.h` табличные |
| Два измерения user/kernel | `-fshellcode` × `-mshellcode-context={user,kernel}` |
| Совместимость Windows POSIX | `WinPEBImportPass` мостит 13 групп POSIX |
| Автоисправление K&R | `SyscallStubPass` откатывается к каноническим POSIX-сигнатурам |

## 10. Кроссплатформенные константы заголовков shim

Заголовки shim (`sys/mman.h`, `fcntl.h` и т.д.) предоставляют константы, которые должны соответствовать ABI целевого ядра. Ключевые различия:

| Константа | Darwin | Linux/Android |
|-----------|--------|---------------|
| `AT_FDCWD` | `-2` | `-100` |
| `MAP_ANONYMOUS` | `0x1000` | `0x20` |
| `O_CREAT` | `0x0200` | `0x0040` |
| `O_TRUNC` | `0x0400` | `0x0200` |
| `O_CLOEXEC` | `0x1000000` | `0x80000` |

Реализация: защита `#if defined(__APPLE__)` в заголовках shim. Таблица совместимости POSIX `SyscallTables.cpp` использует значения Linux (`AT_FDCWD = -100`), активна только на путях `SyscallABI::LinuxSvc0` / `LinuxSyscall`. Цели Windows не используют эти POSIX-заголовки; мост POSIX→Win32 обрабатывается обёртками совместимости `WinPEBImportPass`.
