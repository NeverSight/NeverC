**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Convenciones de llamada personalizadas

NeverC soporta **convenciones de llamada personalizadas basadas en datos** — puedes asignar registros físicos arbitrarios a los argumentos y valores de retorno de cualquier función, completamente desde un plugin externo o atributos a nivel de código fuente, sin modificar el compilador ni definiciones TableGen.

## Descripción general

Las convenciones de llamada LLVM tradicionales están codificadas en el backend mediante archivos `.td` / `.inc`. NeverC reemplaza esto con un enfoque **basado en datos en tiempo de ejecución**:

- Una **especificación de asignación de registros** (cadena de texto) se adjunta a cada función como atributo de cadena.
- El backend lee esta especificación y asigna parámetros/valores de retorno a los registros físicos indicados.
- La especificación puede provenir de un **plugin externo** (pase IR), **atributos de código fuente** (`__attribute__` / `__declspec`), o ambos.

## Formato de especificación

Cadena delimitada por punto y coma. Cada segmento tiene una clave y una lista de nombres de registro separados por comas (insensible a mayúsculas, tolerante con espacios):

```
gpr:rcx,rdx,r8,r9; xmm:xmm0,xmm1; ret:rax; ret_xmm:xmm0
```

| Segmento | Alias | Significado |
|---|---|---|
| `args` | | **Modo posicional**: cada token es un nombre de registro o `stack`/`mem` |
| `gpr` | `arg_gpr` | **Modo pool**: registros de argumentos enteros/punteros |
| `xmm` | `arg_xmm` | **Modo pool**: registros de argumentos flotantes/vectores |
| `ret_gpr` | `ret` | Registros de valor de retorno enteros/punteros |
| `csr` | | Conjunto callee-saved personalizado |

### Arquitecturas soportadas

| Arquitectura | GPR | SIMD | Selección de ancho |
|---|---|---|---|
| **x86-64** | `rax`, `rcx`, `rdx`, `rsi`, `rdi`, `r8`–`r11` | `xmm0`–`xmm15` | i32→32 bits, i64→64 bits |
| **AArch64** | `x0`–`x28` | `v0`–`v31` | i32→`w`, i64→`x`, f32→`s`, f64→`d` |

## Uso

### 1. Dirigido por plugin (recomendado)

```bash
cd pluginsdk/examples && make CustomCallConvPlugin.dylib
# Modo atributo (predeterminado)
neverc -fplugin-pass=./CustomCallConvPlugin.dylib input.c -o output.o
# Modo global
neverc -fplugin-pass=./CustomCallConvPlugin.dylib \
       -fplugin-pass-arg=cc-all=1 \
       -fplugin-pass-arg=ccspec="gpr:r10,r11,rsi;ret:rdx" \
       input.c -o output.o
```

### 2. Atributos de código fuente

```c
__attribute__((custom_attr("neverc-callconv", "gpr:r10,r11,rsi;ret:rdx")))
int add3(int a, int b, int c) { return a + b + c; }

__declspec(custom_attr("neverc-callconv", "gpr:r10;ret:rdx"))
int msfunc(int a) { return a; }
```

## Soporte LTO

El plugin se registra en `NEVERC_HOOK_POST_OPT` y `NEVERC_HOOK_LTO_POST_OPT`, asegurando que las convenciones personalizadas se apliquen después de la fusión LTO.

## API del plugin

```c
API->FunctionSetCustomCallConv(F, "gpr:r10,r11,rsi;ret:rdx");
```

Establece `CallingConv::NeverC_Custom` (CC 1000), escribe el atributo y **sincroniza todos los sitios de llamada directos**. Pasar `NULL` o `""` limpia la convención.

## Pruebas

Suite GoogleTest (18 pruebas, todas PASS):

```bash
ninja -C build-neverc neverc-tests
build-neverc/bin/neverc-tests --gtest_filter='CustomCallConvTest.*'
```
