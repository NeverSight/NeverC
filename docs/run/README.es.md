**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Índice de documentación](../README.es.md) · [← Proyecto NeverC](../../README.md)

# `neverc run`

Compila un programa C o NeverC en un **ejecutable temporal**, lo ejecuta en el **host local**, devuelve su código de salida y elimina el artefacto después. El flujo está pensado para ser similar a `go run`.

Cuando necesites conservar el binario, distribuirlo o depurarlo, usa la invocación normal del compilador (`neverc ... -o output`).

## Sintaxis

```text
neverc run [flags del compilador] file.c [file2.nc ...] [argumentos del programa...]
neverc run [argumentos del compilador...] -- [argumentos del programa...]
```

`neverc run --help` también muestra un resumen integrado.

## Análisis de argumentos

`neverc run` divide los argumentos en una **invocación del compilador** y **argumentos del programa** opcionales usando una de dos reglas.

### División predeterminada (estilo Go)

1. Escanear de izquierda a derecha hasta el primer argumento que termine en `.c` o `.nc` y no empiece por `-`.
2. **Todo hasta el final de la serie continua de `.c`/`.nc` (incluida), junto con lo anterior al primer archivo fuente**, va al compilador.
3. **Todo después** va a `argv` del programa temporal.

Ejemplos:

```bash
neverc run -O2 -fbuiltin-string hello.c
neverc run -O2 main.c helper.nc -- --verbose two words
neverc run -DGENERATED=.c -O2 main.c argument
```

Notas:

- Solo `.c` y `.nc` cuentan como fuentes run. Un flag como `-DGENERATED=.c` permanece en el compilador.
- Varias fuentes producen un solo binario temporal, como un link multiarchivo normal.

### Separador `--` explícito

Cuando el compilador necesita argumentos **después** de la lista de fuentes (flags de enlace, entradas no fuente, `-x c -`, etc.), pon `--` entre la cola del compilador y los argumentos del programa:

```bash
neverc run hello.c helper.o -lm -- arg.c -x
neverc run hello.c -O1 -- x
```

Todo antes de `--` se reenvía a `neverc` (más un `-o <temp>` interno). Todo después son argumentos del programa.

## Comportamiento en tiempo de ejecución

| Tema | Comportamiento |
|------|----------------|
| Directorio de trabajo | El programa temporal se ejecuta en el **directorio actual** |
| Entorno | Hereda el entorno actual (`PATH`, variables exportadas, etc.) |
| E/S estándar | stdin/stdout/stderr conectados al proceso temporal |
| Código de salida | En éxito, el del **programa**; si falla la compilación, el del **compilador** sin ejecutar el programa |
| Archivos temporales | El ejecutable vive en `neverc-run-*`; el directorio se elimina después. Si falla la limpieza, se informa por separado. |

## Ejemplos

```bash
neverc run -O2 -fbuiltin-string hello.c
neverc run -fbuiltin-string greet.c -- Alice "two words"
neverc run -O2 main.c util.nc -- --port 8080
neverc run app.c extra.o -lm -- --config prod.json
```

## Limitaciones y advertencias

- **Solo ejecución en el host.** Los flags de cross-compilación (`-target ...`) pueden compilar, pero el binario temporal siempre se ejecuta localmente.
- **Sin artefacto persistente.** El binario se borra al terminar — usa `neverc ... -o out` para depurar.
- **Misma toolchain que `neverc`.** La orden reinvoca el mismo binario `neverc`.
- **Fuentes `.nc`.** Mismas reglas que `.c`; las extensiones NeverC se aplican automáticamente.

## Comandos relacionados

| Comando | Cuándo usarlo |
|---------|---------------|
| `neverc file.c -o out` | Conservar binario, cross-compilar, scripts de build |
| [`neverc build` / `neverc make`](../build/README.es.md) | Builds compatibles con GNU Make impulsados por un Makefile |
| `neverc run --help` | Resumen integrado |
