**Языки**: [English](../build.md) | [简体中文](../zh-CN/build.md) | [繁體中文](../zh-TW/build.md) | [日本語](../ja/build.md) | [한국어](../ko/build.md) | [Français](../fr/build.md) | [Deutsch](../de/build.md) | [Español](../es/build.md) | [Italiano](../it/build.md) | [Русский](build.md) | [العربية](../ar/build.md)

[← Индекс документации](README.md) · [← Проект NeverC](project.md)

# `neverc build` / `neverc make`

NeverC поставляет встроенный **совместимый с GNU Make** драйвер.
`neverc build` и `neverc make` — одна команда: читает Makefile, раскрывает
переменные/функции и выполняет рецепты. Каталог [`examples/`](examples.md)
рассчитан на этот поток.

Это **не** инструмент проектов на `neverc.toml`. Передавайте обычные флаги Make
и `VAR=value`.

## Синтаксис

```text
neverc build [options] [target...]
neverc make  [options] [target...]
```

```bash
cd examples/linux-hello
neverc make
neverc make clean
neverc make NEVERC=/path/to/neverc TARGET=aarch64-linux-gnu
```

Опции: `neverc make --help`.

## Опции

| Опция | Смысл |
|-------|-------|
| `-f FILE` | Читать этот Makefile |
| `-j [N]` | Параллельные задания (`-j` = число CPU) |
| `-C DIR` | Сменить каталог перед чтением |
| `-n`, `--dry-run` | Печать без выполнения |
| `-k`, `--keep-going` | Продолжать после ошибок |
| `-s`, `--silent` | Не эхо рецептов |
| `-B`, `--always-make` | Пересобрать всё |
| `-p` | Печать БД правил/переменных |
| `VAR=VALUE` | Переменная командной строки |
| `-h`, `--help` | Справка |

## Поиск Makefile

Без `-f`: `GNUmakefile` → `makefile` → `Makefile`.

## Поддерживаемая поверхность Make (кратко)

Правила и шаблоны, `.PHONY`, префиксы рецептов, присваивания, условия,
`include`/`export`, распространённые функции (`subst`, `patsubst`, `wildcard`,
`foreach`, `call`, `eval`, `shell`, …). `MAKE_VERSION` сообщает `4.3`.
Осознанное подмножество, не полный GNU Make.

## Типичный Makefile

```make
NEVERC ?= neverc
TARGET  = x86_64-linux-gnu
OUTPUT  = hello
SRCS    = main.c

FLAGS = --target=$(TARGET) -O2

all: $(OUTPUT)

$(OUTPUT): $(SRCS)
	$(NEVERC) $(FLAGS) -o $@ $(SRCS)

clean:
	rm -f $(OUTPUT)

.PHONY: all clean
```

Примеры кросса часто передают `ARCH=…` или `TARGET=…`. См.
[Examples](examples.md).

## Связанные команды

| Команда | Когда использовать |
|---------|--------------------|
| `neverc file.c -o out` | Компиляция без Makefile |
| [`neverc run`](run.md) | Временный compile-and-run |
| [`neverc runtime`](runtime.md) | Установка sysroot для кросса |
| [Релиз и `--strip`](release-builds.md) | Strip финального образа |
