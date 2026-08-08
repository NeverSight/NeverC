**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Índice de documentación](../README.es.md) · [← Proyecto NeverC](../../README.md)

# Binarios de publicación y `--strip`

Usa `--strip` al producir un ejecutable o una biblioteca compartida para
distribuir. Su alias corto es `-s`; ambas formas se comportan igual.

## Inicio rápido

```bash
neverc -O2 --strip app.c -o app
neverc -O2 -s app.c -o app
```

NeverC elimina los metadatos dentro de su enlazador integrado. No ejecuta un
`llvm-strip` externo, por lo que el mismo comando sirve al compilar de forma
cruzada salidas ELF, Mach-O y PE/COFF.

No confundas esta opción de CLI con el conmutador de empaquetado de CMake
`NEVERC_STRIP_BINARY`: este último solo procesa después de compilar el
ejecutable del compilador `neverc` y puede invocar una herramienta strip
externa. No afecta a los programas compilados por NeverC.

## Política de depuración y símbolos

| Invocación | Información de depuración a nivel fuente | Nombres de símbolos estáticos ordinarios | `.dSYM` de Darwin |
|------------|------------------------------------------|------------------------------------------|-------------------|
| Predeterminada (sin `-g`) | No se genera | Puede permanecer; el valor exacto depende del formato | No se genera |
| `-g` | Se genera | Permanece | Se genera en un enlace Darwin normal |
| `--strip` | Se elimina si existe | Se eliminan nombres no necesarios en ejecución | No se genera |
| `-g --strip` | Prevalece la política strip; no está en la imagen entregada | Se eliminan nombres no necesarios en ejecución | Se suprime |

Sin `-g`, el frontend no genera depuración a nivel de fuente. Eso **no** implica
que la salida esté totalmente depurada de símbolos: ELF y Mach-O aún pueden
tener nombres ordinarios; PE normalmente no tiene tabla estática COFF salvo que
la depuración la solicite. Auto-LTO puede descartar nombres locales, pero no
garantiza un strip-all.

`-g` cambia de no tener depuración fuente a generarla; no añade «más» sobre
información generada por defecto. Datos de unwinding como `.eh_frame` en
ELF/Mach-O o `.pdata`/`.xdata` en PE son metadatos de ejecución, no DWARF a
nivel fuente, y pueden permanecer tras strip.

## Implementación y comportamiento por formato

El controlador convierte `--strip` en una política de enlace tipada y la pasa
a los tres backends. Cada uno la aplica mientras comprende el formato y
conserva los nombres y registros que exige el cargador o la ABI dinámica.

| Formato | Se elimina | Se conserva cuando es necesario |
|---------|-------------|---------------------------------|
| ELF | Datos `.debug*` y tablas ordinarias estáticas de símbolos/cadenas | Importaciones/exportaciones dinámicas, metadatos de reubicación y carga, información de unwinding |
| Mach-O | Mapas de depuración/STABS, entradas locales/globales no necesarias en ejecución y generación del `.dSYM` asociado | Datos de binding/importación, nombres ABI exportados, export trie y símbolos referenciados en ejecución |
| PE/COFF | Secciones DWARF integradas y tabla estática COFF de símbolos/cadenas si existe | Importaciones/exportaciones PE, tablas de unwinding, configuración de carga y otros metadatos del cargador |

## Alcance y precedencia

- `--strip` admite ejecutables y bibliotecas compartidas enlazados finales.
- NeverC lo rechaza con `-c`, `-r`, `--emit-static-lib` o `-fdyncode` en lugar
  de producir silenciosamente un artefacto intermedio sin strip.
- La política strip prevalece sobre `-g` y los controles de depuración backend.
- Se cubren tanto Auto-LTO predeterminado como `-fno-lto`.
- Se mantienen los nombres de importación/exportación necesarios para la ABI.

## Límite de seguridad

Strip elimina nombres y metadatos valiosos y encarece el análisis, pero **no es**
ofuscación ni impide aplicar ingeniería inversa al código máquina. Un binario
correctamente tratado aún puede contener:

- nombres dinámicos de importación/exportación requeridos por el cargador;
- literales, tablas de reflexión o metadatos de la aplicación;
- registros de unwinding, reubicación, firma y configuración de carga;
- código máquina y su flujo de control observable.

`--strip` solo controla la imagen final. No elimina artefactos solicitados por
separado, como mapas de enlace, registros de optimización o salidas de
`-save-temps`; audita el directorio de publicación y no distribuyas esos
archivos auxiliares.

Usa cifrado de cadenas, ofuscación y medidas antimanipulación como capas
separadas cuando proceda, y no incrustes secretos que deban ser confidenciales.

## Verificación de un artefacto

Inspecciona los artefactos de publicación en CI con las herramientas de objetos
de LLVM. Ajusta los comandos al formato y permite expresamente los nombres ABI.

```bash
llvm-readobj --sections --symbols --dyn-symbols app
llvm-dwarfdump app
strings app | grep neverc_private_release_symbol
test ! -e app.dSYM
```

Un artefacto con strip no debe tener secciones de depuración fuente ni nombres
de símbolos estáticos privados. Los nombres dinámicos y metadatos necesarios
son esperados y no indican un fallo.
