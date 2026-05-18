**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Компилятор shellcode](../README.ru.md)

# Учебник по ассемблеру ARM64 (AArch64) — Перспектива Shellcode

> Для читателей, незнакомых с ARM64, с фокусом на инструкции, генерируемые компилятором shellcode.

## 1–7. Регистры, переходы, PC-относительная адресация, загрузка немедленных значений, доступ к памяти, арифметика, сравнения

Общие регистры x0-x30 (64-бит) / w0-w30 (32-бит), sp, xzr/wzr. Соглашение AAPCS64. `b`/`bl`/`br`/`blr`/`ret`. **Shellcode должен избегать `bl`** (перемещение BRANCH26). `adr`/`adrp+add`. `mov+movk` для 64-бит — **ядро Data2TextPass**.

## 8. Типичные генерируемые последовательности инструкций

Чистое вычисление, рекурсивный Фибоначчи, инлайнинг строки на стек (Data2TextPass), системный вызов (SyscallStubPass прямой svc). Поток инструкций 100% в `__TEXT,__text`.

## 9. Ключевой итог

| Концепция | x86_64 | ARM64 | Shellcode |
|-----------|--------|-------|-----------|
| Вызов функции | `call rel32` | `bl imm26` | Экстрактор патчит BRANCH26 |
| Загрузка адреса | `lea rax,[rip+sym]` | `adrp+add` | PAGE21/PAGEOFF12 патчены |
| 64-бит немедленное | `mov rax,imm64` | `mov+movk ×4` | Ноль перемещений |
| Syscall | `syscall` | `svc #0x80` | Darwin: x16=nr |
