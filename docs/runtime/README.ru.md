**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Указатель документации](../README.ru.md) · [← Проект NeverC](../../README.md)

# `neverc runtime`

Управляет пакетами **runtime для кросс-компиляции** (sysroot / SDK) из
[GitHub Releases](https://github.com/NeverSight/NeverC/releases). Они лежат в
`<NeverC-root>/runtime/` рядом с компилятором (при установке по умолчанию —
`~/.neverc/runtime/`).

Предпочитайте `neverc runtime install …` ручной распаковке
`neverc-runtime-<target>.zip`.

## Синтаксис

```text
neverc runtime install <target> [--version <tag>]
neverc runtime install all [--version <tag>]
neverc runtime update <target> [--version <tag>]
neverc runtime remove <target>
neverc runtime list
neverc runtime --help
```

Псевдонимы: `upgrade` → `update`; `uninstall` → `remove`; `ls` → `list`.

## Доступные цели

| Цель | Раскладка под `runtime/` |
|------|--------------------------|
| `windows-x64` | `windows/x64` (+ общий `windows/shared`) |
| `windows-arm64` | `windows/arm64` (+ общий `windows/shared`) |
| `linux-x64` | `linux/x64` |
| `linux-arm64` | `linux/arm64` |
| `macos-arm64` | `macos/arm64` |
| `android-arm64` | `android/arm64` |
| `android-kernel-arm64` | `android/kernel` |

## Подкоманды

### `install`

Устанавливает одну цель с **тегом release компилятора** по умолчанию (или
`--version <tag>`). Имя актива: `neverc-runtime-<target>.zip`.

```bash
neverc runtime install windows-x64
neverc runtime install linux-arm64 --version v3389.1.2
```

Если цель уже установлена:

- Тот же тег → сообщить и успешно выйти.
- Другой / неизвестный тег → подтвердить `[Y/n]` перед переустановкой.

### `install all`

Устанавливает **все отсутствующие** цели каталога в версии компилятора (или
`--version`). Уже установленные пропускаются; чтобы сменить pin, снова вызовите
`install` для одной цели.

```bash
neverc runtime install all
```

### `update` / `upgrade`

Принудительно загружает одну цель без интерактивного запроса. Версия по
умолчанию — **latest** (в отличие от `install`, который следует тегу
компилятора). Передайте `--version`, чтобы зафиксировать.

```bash
neverc runtime update windows-x64
neverc runtime update android-arm64 --version v3389.1.2
```

### `remove` / `uninstall`

Удаляет каталог установленной цели и обновляет `runtime/manifest.json`.

```bash
neverc runtime remove linux-x64
```

### `list` / `ls`

Показывает каждую цель каталога как установленную (с записанным тегом) или нет,
плюс текущий тег компилятора.

```bash
neverc runtime list
```

## Правила версий

| Команда | По умолчанию без `--version` |
|---------|------------------------------|
| `install` / `install all` | Тег release компилятора |
| `update` | Последний release с этим runtime-активом |

Теги выглядят как `vMAJOR.MINOR.PATCH`. Архивы проверяются по `SHA256SUMS`
release перед распаковкой.

## Связь с `neverc update`

- `neverc runtime …` меняет **только sysroot**.
- [`neverc update`](../update/README.ru.md) переводит **компилятор и все уже
  установленные runtime** на один тег одной транзакцией.

После обновления компилятора через `neverc update` установленные runtime уже
согласованы; `runtime install` нужен только для **новых** целей.

## Связанные команды

| Команда | Когда использовать |
|---------|--------------------|
| [`neverc update`](../update/README.ru.md) | Обновить/откатить компилятор + установленные runtime вместе |
| [`neverc build` / `make`](../build/README.ru.md) | Собирать примеры кросс-компиляции против этих sysroot |
| [Examples](../examples/README.ru.md) | Примеры `Makefile` с `--target=…` |
| `neverc runtime --help` | Встроенная краткая справка |
