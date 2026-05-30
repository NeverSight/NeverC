**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Компилятор shellcode](../README.ru.md)

# Дорожная карта

Этот документ отслеживает запланированные, текущие или намеренно отложенные функции.

## Текущее состояние

Конвейер shellcode NeverC охватывает:

- Полный конвейер LLVM IR с 11+ выделенными проходами
- Экстракторы COFF / ELF / Mach-O
- Разрешение импортов Win32 PEB-walk (хеш ROR-13, 6 бакетов DLL)
- Прямое понижение системных вызовов (Darwin `svc #0x80`, Linux `svc #0` / `syscall`)
- Поддержка режима ядра (Windows, Linux)
- Аудит запрещённых байтов с настраиваемыми профилями
- SDK плагинов для перезаписчиков запрещённых байтов и кодировщиков наборов символов
- Ограничения размера / выравнивания / заполнения (`-fshellcode-max-length=`, `-fshellcode-align=`, `-fshellcode-pad=`)
- 11 хуков обфускации на уровнях IR, MIR и байтового потока

## Завершено (2026-04)

1. **Ограничения размера / выравнивания / заполнения** — Встроено. `-fshellcode-max-length=`, `-fshellcode-align=`, `-fshellcode-pad=` выполняются в конце `finalizeShellcodeBytes`. Драйвер отклоняет противоречивые конфигурации (например, байт заполнения в наборе запрещённых байтов или заполнение без align/max-length).

2. **Интерфейс перезаписчика запрещённых байтов** — Каркас встроен, стратегий нет. `Plugin.h::registerBadByteRewriteStrategy` предоставляет SDK. `-fshellcode-bad-byte-rewrite` / `-fno-...` управляет вызовом перезаписчиков в цепочке финализации. Отключение переключает на режим только аудита. Downstream-библиотеки регистрируют стратегии на основе Capstone или пользовательские.

3. **Интерфейс кодировщика наборов символов** — Каркас встроен, наборов нет. `Plugin.h::registerCharsetEncoder` предоставляет кортеж `(name, Encode, Stub, IsCharsetMember)`. При установке `-fshellcode-charset=<name>` фаза финализации заменяет `.text` на `Stub(target) || Encode(text, target)` и валидирует все выходные байты по набору символов. Печатные / алфавитно-цифровые / пользовательские кодировщики регистрируются downstream-библиотеками.

## Запланировано — Слой плагинов (через хуки)

Эти возможности **намеренно не встроены**. Они принадлежат слою стратегий/обфускации и предназначены для предоставления сторонними плагинами через интерфейсы хуков и плагинов.

| Функция | Точка хука | Примечания |
|---------|-----------|------------|
| Анти-дизассемблирование | `RunBeforePreEmit` / `RunAfterPreEmit` / `RunAfterFinalMIR` | Помехи префикса инструкций, перестановка переходов, вставка мусора |
| Полиморфизм | `RunAfterFinalMIR` / `RunPostExtract` | Вариация вывода на основе seed при каждой компиляции |
| Поэтапный кодировщик (XOR / RC4 / самодешифрующий) | `RunPostExtract` / `RunPostFinalize` | Генерация стаба при компиляции + шифрование полезной нагрузки |
| Непрямые системные вызовы (Halos / Tartarus / Recycled Gate) | Плагин уровня IR или `RunPostExtract` | Сканирование гаджетов ntdll во время выполнения |
| Маска сна / подмена стека вызовов | Плагин прохода IR | Паттерны Ekko / FOLIAGE / Cronos |
| Патчинг ETW / AMSI | Плагин прохода IR | Последовательности патчей во время выполнения |
| Module stomping / unhooking | Плагин прохода IR | Паттерны манипуляции памятью |

## Обзор хуков плагинов

11 хуков в трёх слоях:

**Слой IR (6 хуков, получают `ModulePassManager &`)**:
- `RunBeforePrep` — Перед любым проходом shellcode
- `RunAfterPrep` — После унификации линковки
- `RunBeforeInlining` — Последний шанс перед AlwaysInliner
- `RunAfterInlining` — IR полностью сведён в одну функцию
- `RunAfterStackify` — Финальная форма IR перед кодогенерацией
- `RunAfterFinalIR` — После `AllBlrPass`, абсолютно последний IR-хук

**Слой MIR (3 хука, получают `TargetPassConfig &`)**:
- `RunBeforePreEmit` — Регистры распределены, псевдоинструкции CFI/EH ещё присутствуют
- `RunAfterPreEmit` — После очистки `MIRPrepPass`, ближайшее состояние к финальным байтам
- `RunAfterFinalMIR` — После LLVM `addPreEmitPass2()`, прямо перед AsmPrinter

**Слой байтового потока (2 хука, получают `SmallVectorImpl<uint8_t> &`)**:
- `RunPostExtract` — Пре-финализация, ещё обрабатывается перезаписчиком/кодировщиком/аудитом/размером
- `RunPostFinalize` — Пост-финализация, последний момент перед записью на диск; NeverC больше не выполняет аудит

## Конвейер финализации

Каждый экстрактор вызывает `finalizeShellcodeBytes` перед записью `.bin`:

```
applyPostExtractObfuscationHook       (C Plugin API: NEVERC_HOOK_SC_POST_EXTRACT)
        |
auditFinalBadBytes                    (встроенный жёсткий аудит)
        |
applyShellcodeSizing                  (-fshellcode-align/-max-length/-pad)
        |
applyPostFinalizeObfuscationHook      (C Plugin API: NEVERC_HOOK_SC_POST_FINALIZE)
```

Использование и примеры кода см. в [документации Plugin API](../../plugin-api/README.ru.md).

## Не запланировано

- **Кросс-языковой фронтенд** — NeverC принимает только собственный C23-фронтенд. IR-конвейер отделён от фронтенда, но приём внешнего биткода (например, от `rustc` или `zig`) не является целью проекта.
