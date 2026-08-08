**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Индекс документации](../README.ru.md) · [← Проект NeverC](../../README.md)

# `neverc runtime`

Управляет пакетами **runtime для кросс-компиляции** (sysroot / SDK) с
[GitHub Releases](https://github.com/NeverSight/NeverC/releases). Живут в
`<NeverC-root>/runtime/` (обычно `~/.neverc/runtime/`).

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

Синонимы: `upgrade` → `update`; `uninstall` → `remove`; `ls` → `list`.

## Доступные цели

| Цель | Раскладка в `runtime/` |
|--------|-------------------------|
| `windows-x64` | `windows/x64` (+ shared `windows/shared`) |
| `windows-arm64` | `windows/arm64` (+ shared `windows/shared`) |
| `linux-x64` | `linux/x64` |
| `linux-arm64` | `linux/arm64` |
| `macos-arm64` | `macos/arm64` |
| `android-arm64` | `android/arm64` |
| `android-kernel-arm64` | `android/kernel` |

## Примеры

```bash
neverc runtime install windows-x64
neverc runtime install all
neverc runtime update linux-arm64 --version v3389.1.2
neverc runtime remove macos-arm64
neverc runtime list
```

## Подкоманды

- **`install`**: ставит одну цель с **тегом компилятора** по умолчанию (или `--version`). Тот же тег → успех; иной → подтверждение `[Y/n]`.
- **`install all`**: ставит все **отсутствующие** цели каталога; уже стоящие пропускаются.
- **`update` / `upgrade`**: принудительная загрузка без запроса. По умолчанию **latest**.
- **`remove` / `uninstall`**: удаляет каталог и обновляет `manifest.json`.
- **`list` / `ls`**: статус установки и тег компилятора.

## Правила версий

| Команда | По умолчанию без `--version` |
|---------|------------------------------|
| `install` / `install all` | Тег release компилятора |
| `update` | Новейший release с этим runtime-asset |

Теги вида `vMAJOR.MINOR.PATCH`; проверка по `SHA256SUMS` до распаковки.

## Связь с `neverc update`

- `neverc runtime …` меняет только **sysroot**.
- [`neverc update`](../update/README.ru.md) синхронизирует **компилятор + уже установленные runtime**.

После обновления компилятора ставьте через `runtime install` только **новые** цели.

## Связанные команды

| Команда | Когда использовать |
|---------|--------------------|
| [`neverc update`](../update/README.ru.md) | Компилятор и установленные runtime вместе |
| [`neverc build` / `make`](../build/README.ru.md) | Сборка примеров кросс-компиляции |
| [Examples](../examples/README.ru.md) | Makefile с `--target=…` |
| `neverc runtime --help` | Встроенная справка |
