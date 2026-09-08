**Idiomas**: [English](../build.md) | [简体中文](../zh-CN/build.md) | [繁體中文](../zh-TW/build.md) | [日本語](../ja/build.md) | [한국어](../ko/build.md) | [Français](../fr/build.md) | [Deutsch](../de/build.md) | [Español](build.md) | [Italiano](../it/build.md) | [Русский](../ru/build.md) | [العربية](../ar/build.md)

[← Índice de documentación](README.md) · [← Proyecto NeverC](project.md)

# `neverc build` / `neverc make`

NeverC incluye un controlador **compatible con GNU Make**. `neverc build` y
`neverc make` son el mismo comando: leen el Makefile, expanden y ejecutan
recetas. Los [`examples/`](examples.md) siguen este flujo.

**No** es una herramienta de proyecto `neverc.toml`. Pase opciones Make
normales y `VAR=value`.

## Sintaxis

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

Opciones: `neverc make --help`.

## Opciones

| Opción | Significado |
|--------|-------------|
| `-f FILE` | Leer este Makefile |
| `-j [N]` | Trabajos en paralelo (`-j` solo = nº CPU) |
| `-C DIR` | Cambiar de directorio antes de leer |
| `-n`, `--dry-run` | Mostrar sin ejecutar |
| `-k`, `--keep-going` | Continuar tras errores |
| `-s`, `--silent` | No eco de recetas |
| `-B`, `--always-make` | Reconstruir todo |
| `-p` | Imprimir BD de reglas/variables |
| `VAR=VALUE` | Variable de línea de comandos |
| `-h`, `--help` | Mostrar ayuda |

## Descubrimiento del Makefile

Sin `-f`: `GNUmakefile` → `makefile` → `Makefile`.

## Superficie Make admitida (resumen)

Reglas y patrones, `.PHONY`, prefijos de receta, asignaciones, condicionales,
`include`/`export`, funciones habituales (`subst`, `patsubst`, `wildcard`,
`foreach`, `call`, `eval`, `shell`, …). `MAKE_VERSION` informa `4.3`.
Subconjunto intencional, no un GNU Make completo.

## Makefile típico

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

Los ejemplos de cross suelen pasar `ARCH=…` o `TARGET=…`. Ver
[Examples](examples.md).

## Comandos relacionados

| Comando | Uso |
|---------|-----|
| `neverc file.c -o out` | Compilación sin Makefile |
| [`neverc run`](run.md) | Compilar y ejecutar temporalmente |
| [`neverc runtime`](runtime.md) | Instalar sysroots de cross |
| [Publicación y `--strip`](release-builds.md) | Strip de la imagen final |
