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
`NEVERC_BUNDLE_PYTHON_RUNTIME=ON` por defecto. La configuración requiere
CPython 3.10 o posterior, sus headers de embedding y su biblioteca compartida.
La instalación copia automáticamente la versión exacta elegida al directorio
adyacente `python/`. El ejecutable del build tree aún puede usar el Python de
build, pero el compiler instalado no necesita Python externo, `PYTHONHOME` ni
`PYTHONPATH` al ejecutarse. Los archivos oficiales eligen e incluyen CPython
3.12.10.

En Linux, el bundler de instalación requiere `patchelf` en `PATH`. Con
`CMAKE_CROSSCOMPILING` se rechaza el bundling automático para no empaquetar el
intérprete host con un compiler target; desactívelo y empaquete explícitamente
un runtime target. Para un compiler sin Python, pase juntos
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
devolver `None`. Los plugins Python v1 son session-serial y no reentrantes.

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

La v1 es deliberadamente de solo lectura salvo el registro de opciones. No
expone interceptors, providers, mutación de artifacts, modelos IR/MIR/Link por
dominio, subinterpreters, manifests ni puntos de entrada module/factory. Esas
funciones necesitan wrappers de transacción y continuation cuya vida útil pueda
imponerse; la ABI C nativa sigue disponible cuando sean necesarias.
