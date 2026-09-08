**Языки**: [English](../../dyncode-compiler/kernel-mode-dyncode.md) | [简体中文](../../zh-CN/dyncode-compiler/kernel-mode-dyncode.md) | [繁體中文](../../zh-TW/dyncode-compiler/kernel-mode-dyncode.md) | [日本語](../../ja/dyncode-compiler/kernel-mode-dyncode.md) | [한국어](../../ko/dyncode-compiler/kernel-mode-dyncode.md) | [Français](../../fr/dyncode-compiler/kernel-mode-dyncode.md) | [Deutsch](../../de/dyncode-compiler/kernel-mode-dyncode.md) | [Español](../../es/dyncode-compiler/kernel-mode-dyncode.md) | [Italiano](../../it/dyncode-compiler/kernel-mode-dyncode.md) | [Русский](kernel-mode-dyncode.md) | [العربية](../../ar/dyncode-compiler/kernel-mode-dyncode.md)

[← Компилятор dyncode](README.md)

# Поддержка dyncode режима ядра (Ring-0)

`-fdyncode` изначально покрывал только payload ring-3. Payload ring-0 не могут переиспользовать ABI ring-3: нет TEB/PEB, инструкции syscall = ловушки пользователь→ядро, x86_64 требует другую модель кода и отключение красной зоны.

## 1. `-mdyncode-context={user,kernel}`
- **User** (по умолчанию): Конвейер PEB/syscall.
- **Kernel**: SyscallStub/WinPEB отключены, флаги ядра введены, KernelImportPass активирован.

## 2–3. Поля TargetDesc и флаги драйвера
`Level`, `KernelImport`, `KernelInjectFlags`. x86_64: `-mno-red-zone -mcmodel=kernel -mno-sse`; AArch64: `-mgeneral-regs-only`.

## 4. KernelImportPass
Автоматическая перезапись неразрешённых extern вызовов в непрямые вызовы через резолвер. Хэш FNV-1a 64-бит. Трёхслойная защита.

## 5–7. Ядро Android, заголовки, написание кода Ring-0
`<neverc/dyncode/kernel.h>` предоставляет `neverc_kern_resolve_t` и `neverc_kern_hash()`. Payload чистого вычисления или на основе резолвера.

## 8. Дорожная карта
Переключение контекста ядра, перезапись резолвера, оба типа payload — всё завершено. Заголовки SDK ядра запланированы.
