**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Компилятор shellcode](../README.ru.md)

# Проектирование проходов MIR — Принципы и точки хуков

> Сопутствующий документ к [ir-pass-design.md](../ir-pass-design/README.ru.md). Слой IR устраняет конструкции, которые явно порождают перемещения на уровне IR. Слой MIR служит **страховочной сетью** после выбора инструкций и распределения регистров: удаляет псевдо-/метаданные-инструкции, введённые кодогенерацией, и предоставляет точки хуков для сторонних проходов обфускации.

---

## 0. Зачем нужен слой MIR

Бэкенд LLVM вводит дополнительные конструкции при **IR → MIR понижении**: псевдо CFI/EH_LABEL, стабы XRay/patchable, метаданные санитайзеров, фиксапы MC. Хуки MIR также позволяют **обфускацию на уровне инструкций** третьими сторонами (подстановка, переименование регистров).

## 1. Интеграция с LLVM

Регистрация в `Pipeline.cpp` через глобальный коллбэк `TargetPassConfig`.

## 2. Встроенный MIRPrepPass

Сканирует каждый `MachineBasicBlock` и удаляет три категории псевдо: метаданные боковых секций (`CFI_INSTRUCTION`, `EH_LABEL`, `STACKMAP` и т.д.), Windows SEH (сопоставление префикса `SEH_`), и табличные перезаписи инструкций (`MIRRewritePatterns.def`).

Два зарегистрированных паттерна:
1. **`aarch64-cpi-fp-to-fmov-imm`**: `ADRP + LDR CPI` → `FMOV #imm8`.
2. **`x86-cpi-zero-fp-to-xorps`**: `movss/movsd +0.0` → `xorps xmm, xmm`.

## 3. Пользовательские хуки обфускации

11 точек хуков: 6 IR + 3 MIR + 2 байтовый уровень.

- `RunBeforePreEmit`: MIR с псевдо CFI/EH.
- `RunAfterPreEmit`: Очищенный MIR — ближайший к AsmPrinter.
- `RunPostExtract`: Чистый поток байтов.

## 4. Полный порядок выполнения

```
[IR] → Prep → Проходы → Data2Text → Инлайнинг → Stackify → AllBlr
[Codegen] → RunBeforePreEmit → MIRPrepPass → RunAfterPreEmit
[AsmPrinter → .o → Экстрактор → RunPostExtract → .bin]
```

## 5. Обоснование проектирования

| Проблема | IR? | MIR? |
|----------|-----|------|
| Устранение константных GV | Да | Не нужно |
| Псевдо CFI | Нет (бэкенд) | Да |
| Обфускация уровня инструкций | Нет | Да |

## 6. Руководство по расширению

- **Добавление удаления псевдо**: Один case в `isShellcodeStripPseudo`.
- **Добавление перезаписи MIR**: Написать `tryRewriteXxx` + файлы `.def`.
- **Сторонние**: `setShellcodeObfuscationHooks()`.

## 7. Связь с ShellcodeExtractor

MIR исправляет первым (может манипулировать инструкциями); экстрактор — последняя инстанция для патчей на уровне байтов.
