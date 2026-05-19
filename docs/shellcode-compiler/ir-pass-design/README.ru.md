**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Компилятор shellcode](../README.ru.md)

# Проектирование проходов IR — Принципы, конвейер и примеры до/после

> Этот документ объясняет **почему** каждый проход существует в конвейере компиляции shellcode.

## 0. Центральная идея

Цель одним предложением: **Устранить всё в `.o`, что стало бы перемещением, оставив только чистый поток инструкций для прямого `mmap(RWX)` + `memcpy` + `blr`.**

## 1–13. Проходы

| Проход | Функция |
|--------|---------|
| ZeroRelocPass | Prep: унификация линковки + alwaysinline. Stackify: мутабельные глобалы → alloca |
| IndirectBrPass | computed-goto → switch |
| SyscallStubPass | libc extern → табличные inline-asm-трапы + совместимость POSIX + автоисправление K&R |
| WinPEBImportPass | Win32 extern → PEB-walk-резолвер (~190 API) + зашифрованный кеш адресов + Windows POSIX совместимость |
| MemIntrinPass | mem*/str*/abs → inline хелперы байтовых циклов |
| CompilerRtPass | `__int128` div/mod → inline длинное деление |
| Data2TextPass | Фаза 1+2: константные GV → немедленные/стек + split остатков SROA |
| AllBlrPass | (опционально) прямые вызовы → непрямые |
| KernelImportPass | (ring-0) extern → непрямые вызовы через резолвер |
| StringRuntimePass | методы встроенного `string` → варианты на стековой арене |

11 хуков обфускации. Философия диагностики: 1 ошибка = 1 действенная диагностика. См. [plugin-interface.md §6](../plugin-interface/README.ru.md#6-registration-position-selection--pic-coverage-matrix) и [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.ru.md).
