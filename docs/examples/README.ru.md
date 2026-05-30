**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Документация](../README.ru.md) · [← Проект NeverC](../../docs/i18n/README.ru.md)

# Примеры NeverC

Полные собираемые примеры, демонстрирующие кроссплатформенную компиляцию NeverC. Все кросс-компилируются с macOS / Linux — среда Windows не требуется.

---

## Доступные примеры

| Пример | Описание | Ключевые возможности |
|--------|----------|---------------------|
| [Драйвер ядра Windows](../../examples/windows-driver/README.ru.md) | Минимальный WDM-драйвер ядра | Кросс-компиляция `.sys` с macOS/Linux, авто-LTO, встроенный линкер |
| [Драйвер Windows + CET](../../examples/windows-driver-cet/README.ru.md) | Драйвер с Intel CET Shadow Stack | CET-совместимый код ядра, `/guard:ehcont` |
| [Драйвер Windows + плавающая точка](../../examples/windows-driver-float/README.ru.md) | Драйвер с плавающей точкой/SIMD | Безопасная плавающая точка в режиме ядра |

---

## Быстрый старт

```bash
cd examples/<имя-примера>
make
```

Указать путь компилятора: `make NEVERC=/path/to/neverc`

Все примеры используют **neverc** и генерируют бинарники Windows PE (`.sys`) через встроенный линкер.
