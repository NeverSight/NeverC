**Языки**: [English](../../dyncode-compiler/ir-pass-design.md) | [简体中文](../../zh-CN/dyncode-compiler/ir-pass-design.md) | [繁體中文](../../zh-TW/dyncode-compiler/ir-pass-design.md) | [日本語](../../ja/dyncode-compiler/ir-pass-design.md) | [한국어](../../ko/dyncode-compiler/ir-pass-design.md) | [Français](../../fr/dyncode-compiler/ir-pass-design.md) | [Deutsch](../../de/dyncode-compiler/ir-pass-design.md) | [Español](../../es/dyncode-compiler/ir-pass-design.md) | [Italiano](../../it/dyncode-compiler/ir-pass-design.md) | [Русский](ir-pass-design.md) | [العربية](../../ar/dyncode-compiler/ir-pass-design.md)

[← Компилятор dyncode](README.md)

# Проектирование проходов IR — Принципы, конвейер и примеры до/после

> Этот документ объясняет **почему** каждый проход существует в конвейере компиляции dyncode.

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
| HeapArenaPass | `malloc`/`free`/`calloc`/`realloc` → арена-аллокация + OS-фоллбэк для больших выделений |

**Шифрование кеша адресов** (§4.1, общее для WinPEBImportPass и KernelImportPass): разрешённые адреса шифруются перед сохранением через безXOR арифметическую декомпозицию `(a + b) - 2*(a & b)` + `volatile` промежуточные. Три подключаемые функции (`__sc_derive_key`, `__sc_ptr_encrypt`, `__sc_ptr_decrypt`). Слоты кеша на (DLL, API) в секции `.text`. Быстрый/медленный путь с `cmpxchg weak` (потокобезопасно). Пользователь может предоставить собственные реализации (`always_inline`, взаимно обратные, без внешних вызовов). См. [README.md §4.1–4.5](../../dyncode-compiler/ir-pass-design.md#41-address-cache-encryption) для полных деталей.

11 хуков обфускации. Философия диагностики: 1 ошибка = 1 действенная диагностика. См. [mir-pass-design.md §3](mir-pass-design.md#3-пользовательские-хуки-обфускации) и [kernel-mode-dyncode.md](kernel-mode-dyncode.md).
