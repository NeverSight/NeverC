**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Проект NeverC](../README.ru.md)

> **Tip:** Use the language bar above; links on this page point to the same locale (shellcode README and breadcrumbs).

# Документация NeverC

Описания дизайна, справочник API и руководства по каждой подсистеме NeverC.

---

## Компилятор shellcode

Конвейер компиляции shellcode — основной исследовательский фокус NeverC. Архитектура, опции CLI, матрица платформ и примеры:

**[Компилятор shellcode →](shellcode-compiler/README.ru.md)**

| Документ | Описание |
|----------|----------|
| [README](shellcode-compiler/README.ru.md) | Обзор, быстрый старт, поддерживаемые цели |
| [Pipeline & PIC](shellcode-compiler/pipeline-and-pic/README.ru.md) | Дизайн IR → объект → извлечение |
| [IR Pass Design](shellcode-compiler/ir-pass-design/README.ru.md) | Обоснование каждого IR-прохода |
| [MIR Pass Design](shellcode-compiler/mir-pass-design/README.ru.md) | MIR-проходы бэкенда |
| [Kernel-Mode Shellcode](shellcode-compiler/kernel-mode-shellcode/README.ru.md) | Компиляция Ring-0 |
| [Plugin Interface](shellcode-compiler/plugin-interface/README.ru.md) | Плагины обфускации и кодирования |
| [Cross-Platform Architecture](shellcode-compiler/cross-platform-architecture/README.ru.md) | `TargetDesc` и экстракторы |
| [Platform Extension Guide](shellcode-compiler/platform-extension-guide/README.ru.md) | Добавление платформы |
| [ARM64 Assembly Tutorial](shellcode-compiler/arm64-assembly-tutorial/README.ru.md) | Инструкции ARM64 с точки зрения shellcode |
| [Roadmap](shellcode-compiler/roadmap/README.ru.md) | Запланированная работа |
| [Progress](shellcode-compiler/progress/README.ru.md) | Статус реализации |

---

## Встроенный тип `string`

NeverC предоставляет встроенный тип-значение `string` для C, сочетающий эргономику `std::string` и поддержку Unicode уровня `QString`. Включается через `-fbuiltin-string` (автоматически в режиме `-fshellcode`).

**[Встроенная строка →](builtins/string/README.ru.md)**
