**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Компилятор shellcode](../README.ru.md)

# Кроссплатформенная архитектура NeverC Shellcode — Обзор

Этот документ описывает принципы проектирования «один набор проходов для macOS / Linux / Android / Windows × arm64 / x86_64 × User / Kernel». Прочитайте перед расширением на новую платформу.

Связанные документы:
- [README.md](../README.ru.md) — Обзор, опции CLI, быстрый старт
- [ir-pass-design.md](../ir-pass-design/README.ru.md) — Ответственности проходов IR
- [mir-pass-design.md](../mir-pass-design/README.ru.md) — Слой MIR
- [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.ru.md) — Контекст ядра
- [platform-extension-guide.md](../platform-extension-guide/README.ru.md) — Добавление платформ

---

## 1. Трёхмерная матрица: OS × Arch × ExecutionLevel

Все различия сходятся в **3D матрицу**: 8 (OS, arch) × 2 ExecutionLevel = **16 записей таблицы** из `describeTriple()`.

**Основной принцип**: Проходы всегда читают из таблицы, никогда `if (OS == Darwin)`. Новая платформа = 1 строка + 1 case в экстракторе.

## 2–3. Конвейер и PIC

Фиксированный порядок с 11 хуками обфускации. `isPICDefaultForced()` возвращает **true** повсюду.

## 4. User / Kernel ортогонально

- **User**: Конвейер PEB walk / syscall stub.
- **Kernel**: SyscallStub/WinPEB замыкаются; KernelImportPass активируется.

## 5. Матрица поддержки «обычного C» режима пользователя

Большие массивы, FP-константы, computed-goto, memcpy, `__int128`, атомики, заголовки POSIX/Win32 — всё **напрямую поддержано** без вмешательства пользователя.

## 6. Слой MIR: 3-стадийный конвейер (Исправить / Откат / Извлечь)

1. Кроссплатформенная очистка псевдоинструкций
2. Табличная перезапись инструкций
3. Аудит внешних ссылок / пула констант

## 7–8. Экстрактор и хуки обфускации

Экстрактор: «принять PC-rel внутри .text, отклонить всё остальное». 11 хуков на всех слоях.

## 9–10. Расширение и не-цели

Стоимость: 1 строка TargetDesc + таблица syscall + case экстрактора + тесты. Не-цели: C++/ObjC, 32-бит, встраивание libc, абсолютные адреса.
