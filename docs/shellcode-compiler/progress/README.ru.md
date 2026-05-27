**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Компилятор shellcode](../README.ru.md)

# Компилятор shellcode — отслеживание прогресса

## Этап 0 — macOS arm64 MVP (завершён)

- [x] Скелет каталогов и CMake (библиотека `nevercShellcode`)
- [x] `ZeroRelocPass`: двухфазный (Prep + Stackify), автоматический перенос изменяемых глобалов на стек
- [x] `Data2TextPass`: двухфазный (массивы констант → хранение блоками на стеке; разделение векторных констант после SROA; ConstantFP → битовые шаблоны через volatile-загрузку)
- [x] `SyscallStubPass`: табличный белый список для Darwin BSD / Linux arm64 / Linux x86_64 / Android syscall
- [x] `AllBlrPass`: опциональная агрессивная замена прямых вызовов на косвенные
- [x] `ShellcodeExtractor`: Mach-O `.o` → плоский `.bin` с патчами внутрисекционных релокаций
- [x] CLI-опции через сгенерированный `neverc/include/neverc/Invoke/Options.td.h`: `-fshellcode`, `-fshellcode-all-blr`, `-mshellcode-syscall`, `-fshellcode-keep-obj=`, `-fshellcode-entry=`
- [x] PIC по умолчанию на всех платформах (`isPICDefault()` возвращает `true` повсеместно)
- [x] Обобщённый рекурсивный перенос на стек (таблицы указателей на функции, таблицы строковых указателей, вложенные таблицы структур, инициализаторы ConstantExpr GEP/BitCast)
- [x] `IndirectBrPass`: GCC computed-goto (`&&label`) → switch, включая разделение таблиц мульти-диспатч-сайтов
- [x] Инлайнинг SIMD-векторных констант (`inlineVectorConstants`)
- [x] Автоматическое понижение `_Thread_local` до static
- [x] Нативный загрузчик macOS arm64 (MAP_JIT + i-cache flush)

**Тесты**: 108/108 shellcode-ассертов пройдено. Размеры бинарников: `add` 8B, `fib` 64B, `hello` 64B, `big_const` 632B.

## Этап 1 — Linux / Android / Windows кроссплатформенность (завершён)

- [x] Абстракция `TargetDesc`: табличные различия платформ
- [x] Кроссплатформенная семантика `-mshellcode-syscall` (заменяет `-mshellcode-libsystem` только для Darwin)
- [x] Таблицы номеров syscall Linux / Android (Darwin BSD 100+, Linux arm64 130+, Linux x86_64 150+)
- [x] `ShellcodeExtractor` рефакторирован в `MachOExtractor` / `ELFExtractor` / `COFFExtractor`
- [x] ELF-экстрактор (arm64: `R_AARCH64_CALL26`/`JUMP26`/`ADR_PREL_PG_HI21`/и т.д.; x86_64: `R_X86_64_PC32`/`PLT32`)
- [x] COFF-экстрактор (arm64: `IMAGE_REL_ARM64_BRANCH26`/и т.д.; x86_64: `IMAGE_REL_AMD64_REL32`/и т.д.)
- [x] Проход импорта PEB Windows (`WinPEBImportPass`) с реальным PEB-walk-резолвером
- [x] Мульти-DLL белый список Win32 API (~210 API в kernel32/ntdll/user32/ws2_32/advapi32/shell32)
- [x] `MemIntrinPass`: memcpy/memset/memmove/memcmp/bcmp/bzero/memchr + strlen/strcpy/strcmp/и т.д. → инлайн-хелперы на основе байтовых циклов
- [x] `CompilerRtPass`: деление/остаток `__int128` → инлайн-хелперы длинного деления
- [x] Поддержка фронтенда Windows `aarch64-pc-windows-msvc`
- [x] `MIRPrepPass`: кроссплатформенное удаление псевдо-инструкций (CFI/EH/XRay/StackMap/SEH/FENTRY/и т.д.)
- [x] MIR + хуки обфускации на уровне байтов (11 хуков на уровнях IR/MIR/байтового потока)
- [x] Автоматическое понижение AArch64 не-Darwin `long double` до binary64
- [x] Шим-заголовки shellcode: `<windows.h>`, `<unistd.h>`, `<fcntl.h>`, `<sys/stat.h>`, `<sys/mman.h>`, `<string.h>`, `<stdlib.h>`
- [x] Слой совместимости Windows POSIX (13 мостов POSIX→Win32: write→WriteFile, mmap→VirtualAlloc и т.д.)
- [x] Автоисправление неявных объявлений K&R (50+ канонических POSIX-сигнатур)
- [x] Табличная очистка (жёсткое кодирование архитектурных ветвей → ноль)
- [x] `KernelImportPass`: автоматическая перезапись callsite ring-0 с поддержкой резолвера
- [x] Табличная диагностика имён хелперов ядра (`KernelHelperNames.def`)
- [x] `<neverc/kernel.h>` для конвенций точки входа ring-0
- [x] Принудительное смещение точки входа на ноль (`placeEntryFirst`)
- [x] Конвейер финализации: SDK перезаписи плохих байтов + SDK кодировщика charset + ограничения размера
- [x] SDK плагинов (`Plugin.h`): `registerBadByteRewriteStrategy` + `registerCharsetEncoder`
- [x] Инъекция `-mno-implicit-float` для x86_64 (предотвращает вынос SSE-констант бэкенда в пул)
- [x] Кроссплатформенные загрузчики (macOS/Linux/Windows)

**Тесты**: 743+ shellcode-ассертов, все пройдены на 8 тройках. Полный набор тестов NeverC: 1000+ тестов пройдено.

## Этап 2 — печатный / буквенно-цифровой кодировщик (запланирован)

- [ ] ARM64 кодировщик печатного shellcode (поднабор инструкций 0x20–0x7e)
- [ ] x86_64 буквенно-цифровой кодировщик
- [ ] Генерация самодекодирующего стаба (decoder stub)
- [ ] Статистика размера / энтропии после кодирования

## Этап 3 — полиморфизм / самомодификация (запланирован)

- [ ] Полиморфный движок: один и тот же исходный код → разные эквивалентные байтовые последовательности при каждой компиляции
- [ ] Самомодифицирующийся код: расшифровка / декомпрессия тела полезной нагрузки во время выполнения
- [ ] Антидетектирование: избегание известных шаблонов сигнатур shellcode

## Будущие расширения

- [ ] iOS arm64 (подпись кода + сценарии JIT-джейлбрейка)
- [ ] Cortex-M / Thumb
- [ ] RISC-V 64
