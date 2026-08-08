**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Индекс документации](../README.ru.md) · [← Проект NeverC](../../README.md)

# `neverc update`

Обновляет **release-установку**: компилятор и все уже установленные runtime
кросс-компиляции переходят вместе на **один конкретный тег release**.
`neverc upgrade` — синоним.

Для установок через `install.sh` (обычно `~/.neverc`). **Не** обновляет дерево
сборки CMake/Ninja — смените PATH и пересоберите; см.
[Локальная разработка](../local-dev/README.ru.md).

## Синтаксис

```text
neverc update
neverc update <version>
neverc update --version <version>
neverc update --help
```

Примеры:

```bash
neverc update                 # новейший полный release для этого хоста
neverc update v3389.1.2       # точный тег (апгрейд или откат)
neverc update 3389.1.2        # ведущая «v» необязательна
neverc upgrade                # то же, что neverc update
```

`-y` / `--yes` принимаются для скриптов; обновление неинтерактивно.

## Область синхронизации

| Компонент | Поведение |
|-----------|-----------|
| Компилятор (`bin/`, `lib/`, `pluginsdk/`) | Заменяется при другом целевом теге |
| Уже установленные runtime в `runtime/` | Повторно загружаются **только** уже стоящие цели |
| Отсутствующие runtime | **Не** ставятся сами — [`neverc runtime install`](../runtime/README.ru.md) |

## Модель безопасности

1. Эксклюзивная блокировка `<install>/.neverc-update.lock`.
2. Разрешение целевого тега.
3. Загрузка и проверка `SHA256SUMS` и архивов.
4. Staging, валидация, commit; при сбое — откат.

Если runtime-релиз плохой, укажите более ранний тег:

```bash
neverc update v3389.0.1
```

## Ограничения

- Только корень release-установки (обычно `~/.neverc`). Отказывает корням ФС и деревьям CMake.
- Хост должен соответствовать опубликованному asset компилятора.
- В Windows короткий helper может заменить `neverc.exe` после выхода.

## Связанные команды

| Команда | Когда использовать |
|---------|--------------------|
| [`neverc runtime`](../runtime/README.ru.md) | Отдельные sysroot без смены компилятора |
| [`neverc run`](../run/README.ru.md) | Временный бинарник на хосте |
| [`neverc build` / `make`](../build/README.ru.md) | Makefile примеров/проектов |
| `neverc update --help` | Встроенная справка |
