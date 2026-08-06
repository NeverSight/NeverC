**Idiomas**: [English](python.md) | [简体中文](python.zh-CN.md) | [繁體中文](python.zh-TW.md) | [日本語](python.ja.md) | [한국어](python.ko.md) | [Français](python.fr.md) | [Deutsch](python.de.md) | [Español](python.es.md) | [Italiano](python.it.md) | [Русский](python.ru.md) | [العربية](python.ar.md)

[← ABI de plugins de NeverC](README.es.md)

# Plugins de Python

NeverC puede cargar un archivo fuente de Python mediante la misma opción
`-fplugin=` que usan los plugins nativos. Una compilación normal desde fuentes
activa por defecto los plugins Python y la instalación del runtime incluido:

```sh
cmake -S llvm -B build -C neverc/cmake/caches/NeverC.cmake \
  -DCMAKE_INSTALL_PREFIX="$PWD/neverc-install"
cmake --build build --target install
```

Los builds nuevos usan `NEVERC_ENABLE_PYTHON_PLUGINS=ON` y
`NEVERC_BUNDLE_PYTHON_RUNTIME=ON` por defecto. CMake puede usar el Python del
sistema para los scripts de build, pero ese intérprete no selecciona la ABI de
plugins. NeverC descarga por separado una distribución development/runtime de
CPython 3.12.10 fija y verificada por SHA-256, enlaza el bridge de plugins con
ella, la prepara en `build/python` e instala el mismo runtime en el directorio
adyacente `python/`. Por tanto, tanto los builds desde fuentes como los archivos
oficiales ejecutan plugins con CPython 3.12.10 sin Python runtime externo,
`PYTHONHOME` ni `PYTHONPATH`.

Para un build sin conexión, configure `-DNEVERC_MANAGED_PYTHON_ROOT=/path/to/cpython-3.12.10` con un árbol
development/runtime CPython 3.12.10 exacto ya extraído. NeverC lo valida y lo
copia al build sin modificar el directorio fuente indicado.

En Linux, el bundler de instalación requiere `patchelf` en `PATH`. Como CMake
ejecuta una sonda ABI, los builds con plugins Python gestionados deben ser
nativos por ahora; un cross build debe desactivar Python o usar una fase de
packaging nativa en la plataforma target. Para un compiler sin Python, pase juntos
`-DNEVERC_ENABLE_PYTHON_PLUGINS=OFF` y
`-DNEVERC_BUNDLE_PYTHON_RUNTIME=OFF`.

Instale el paquete de autoría con `python3 -m pip install
./pluginsdk/python`, añada ese directorio a
`PYTHONPATH`, o compile e instale el componente `neverc-pluginsdk`. NeverC
también descubre el SDK preparado en
`<directorio de neverc>/../pluginsdk/python`.

## Plugin mínimo

```python
from neverc_plugin import Plugin


@Plugin(id="com.example.minimal", name="Minimal Python Plugin", version="1.0.0")
class MinimalPlugin:
    def on_process_begin(self, ctx):
        ctx.state = {"sessions": 0}
```

Cárguelo mediante una ruta del sistema de archivos:

```sh
neverc -fplugin=/absolute/path/to/minimal.py -fsyntax-only input.c
```

El decorador acepta un ID canónico, un nombre visible no vacío y una versión
semántica estricta. Un script declara exactamente una clase de plugin. Los
scripts son módulos independientes y pueden mezclarse con plugins nativos.

## Ciclo de vida

Todos los hooks son opcionales:

- `on_process_begin(ctx)` y `on_destroy(ctx)` delimitan el proceso del compilador.
- `register(ctx)` registra opciones y observers antes de congelar el grafo de fases.
- `on_session_begin(ctx)` y `on_session_end(ctx)` delimitan una invocación.
- `on_task_begin(ctx)` y `on_task_end(ctx)` delimitan una unidad de compilación.

Un hook begin puede devolver un valor de Python o asignar `ctx.state`; el hook
end correspondiente puede leerlo. Los demás hooks y callbacks observer deben
devolver `None`. Por defecto son session-serial y no reentrantes; `@Plugin`
puede elegir los mismos modelos que un plugin nativo.

## Opciones y observers

```python
from neverc_plugin import Plugin
from neverc_plugin.domains import driver


@Plugin(id="com.example.trace", name="Trace", version="1.0.0")
class TracePlugin:
    def register(self, ctx):
        ctx.option(
            "--trace-python",
            kind="flag",
            value_type="bool",
            help="Trace raw driver arguments",
        )
        ctx.observer(
            driver.RAW_ARGUMENTS,
            when=("before", "after"),
            fn=self.observe,
        )

    def observe(self, frame):
        if frame.option_values("--trace-python"):
            frame.check_cancelled()
            frame.emit_remark(f"arguments: {frame.arguments}", code=1001)
```

`neverc_plugin.phases` contiene las 130 constantes de fases incorporadas,
generadas desde el esquema normativo. Los frames observer exponen datos de fase
y ruta, handles opacos de entrada/salida, opciones analizadas, diagnósticos,
comprobación de cancelación y argumentos sin procesar para
`driver.RAW_ARGUMENTS`. Los handles nativos comprueban su vida útil: usar un
objeto retenido después de su callback genera `RuntimeError`.

Los kinds de opción son `flag`, `joined`, `separate` y `multi_arg`; los tipos
son `bool`, `int`, `uint`, `string`, `enum` y `path`; las multiplicidades son
`single`, `last_wins` y `append`. Una enum recibe el mapping
`enum_values={nombre: entero}`. `argument_count` solo se aplica a `multi_arg`.

## Acceso completo a la ABI de C

Las ayudas de ciclo de vida, opciones y observers se apoyan en toda la ABI
pública de plugins en C. `neverc_plugin.abi` contiene las definiciones
`ctypes` generadas y los módulos de `neverc_plugin.domains` consultan todas las
tablas oficiales con comprobaciones de versión y tamaño. `bind_callbacks`
conecta callbacks de Python mediante trampolines nativos de firma exacta.

```python
from neverc_plugin import abi
from neverc_plugin.domains import ir
from neverc_plugin.ffi import bind_callbacks, require_ok


def register(self, context):
    scope = context.ffi
    core = ir.CORE.query(scope)
    builder = ir.BUILDER.query(scope)
    passes = ir.PASS.query(scope)
```

## Ejemplo OLLVM en Python

El SDK incluye en
[`pluginsdk/python/examples/ollvm`](../../pluginsdk/python/examples/ollvm/README.md)
un ejemplo escrito únicamente contra este binding público que implementa
sustitución de instrucciones (SUB), flujo de control falso (BCF) y aplanado de
flujo de control (FLA) de forma determinista:

```sh
neverc -fplugin=/path/to/ollvm_plugin.py \
  --ollvm-sub --ollvm-bcf --ollvm-fla \
  --ollvm-seed 42 --ollvm-probability 80 \
  input.c -o output
```

## Errores, seguridad y alcance actual

Una excepción de Python no capturada se convierte en
`NEVERC_STATUS_PLUGIN_EXCEPTION`. Durante un callback session/task activo,
NeverC emite el traceback formateado como diagnóstico estructurado; los fallos
de importación y activación lo incluyen en el error del loader. El intérprete
embebido se comparte en todo el proceso y no se finaliza deliberadamente,
mientras que los objetos propios del plugin se liberan al descargarlo.

Los plugins Python son extensiones de compilador de confianza. Se ejecutan en
el proceso, pueden importar cualquier módulo y tienen los mismos permisos de
sistema que NeverC. No existe sandbox.

El binding de Python no es una API reducida: las definiciones `ctypes` generadas
y los trampolines nativos cubren las 36 tablas oficiales de la ABI C, todos los
records, funciones y callbacks, incluidas las mutaciones, los interceptors y
los providers. También se comprueban lifetimes, transactions y continuations.
Hay un ejemplo OLLVM completo en Python con SUB, BCF y FLA en
`pluginsdk/python/examples/ollvm/`.
Las definiciones crudas están en `neverc_plugin.abi` y los descriptores de tabla
en `neverc_plugin.domains`.
