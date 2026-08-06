**Sprachen**: [English](python.md) | [简体中文](python.zh-CN.md) | [繁體中文](python.zh-TW.md) | [日本語](python.ja.md) | [한국어](python.ko.md) | [Français](python.fr.md) | [Deutsch](python.de.md) | [Español](python.es.md) | [Italiano](python.it.md) | [Русский](python.ru.md) | [العربية](python.ar.md)

[← NeverC-Plugin-ABI](README.de.md)

# Python-Plugins

NeverC kann eine Python-Quelldatei über dieselbe Option `-fplugin=` laden, die
für native Plugins verwendet wird. Ein normaler Quellbuild aktiviert Python-
Plugins und die gebündelte Laufzeitinstallation standardmäßig:

```sh
cmake -S llvm -B build -C neverc/cmake/caches/NeverC.cmake \
  -DCMAKE_INSTALL_PREFIX="$PWD/neverc-install"
cmake --build build --target install
```

Neue Builds verwenden standardmäßig `NEVERC_ENABLE_PYTHON_PLUGINS=ON` und
`NEVERC_BUNDLE_PYTHON_RUNTIME=ON`. CMake darf das System-Python für Build-Skripte
verwenden, doch dieser Interpreter bestimmt nicht die Plugin-ABI. NeverC lädt
separat eine per SHA-256 geprüfte, fest vorgegebene CPython-3.12.10-Development-
und Runtime-Distribution, linkt die Plugin-Bridge dagegen, legt sie unter
`build/python` ab und installiert dieselbe Runtime in das benachbarte Verzeichnis
`python/`. Normale Quellbuilds und offizielle Archive führen Plugins daher mit
CPython 3.12.10 aus und benötigen keine externe Python-Runtime, kein `PYTHONHOME`
und kein `PYTHONPATH`.

Für Offline-Builds kann `-DNEVERC_MANAGED_PYTHON_ROOT=/path/to/cpython-3.12.10` auf einen bereits
entpackten, exakt passenden CPython-3.12.10-Development/Runtime-Baum zeigen.
NeverC prüft und kopiert ihn, ohne das angegebene Quellverzeichnis zu verändern.

Unter Linux benötigt der Installations-Bundler `patchelf` in `PATH`. Da CMake
einen ABI-Probe ausführt, müssen verwaltete Python-Plugin-Builds derzeit nativ
sein; Cross-Builds deaktivieren Python oder verwenden eine eigene native
Packaging-Stufe auf der Target-Plattform. Für einen Compiler ohne Python
geben Sie sowohl `-DNEVERC_ENABLE_PYTHON_PLUGINS=OFF` als auch
`-DNEVERC_BUNDLE_PYTHON_RUNTIME=OFF` an.

Installieren Sie das Autorenpaket mit `python3 -m pip install
./pluginsdk/python`, nehmen Sie das Verzeichnis in
`PYTHONPATH` auf oder bauen und installieren Sie die Komponente
`neverc-pluginsdk`. NeverC erkennt außerdem ein bereitgestelltes SDK unter
`<neverc-Verzeichnis>/../pluginsdk/python`.

## Minimales Plugin

```python
from neverc_plugin import Plugin


@Plugin(id="com.example.minimal", name="Minimal Python Plugin", version="1.0.0")
class MinimalPlugin:
    def on_process_begin(self, ctx):
        ctx.state = {"sessions": 0}
```

Laden Sie es über einen Dateisystempfad:

```sh
neverc -fplugin=/absolute/path/to/minimal.py -fsyntax-only input.c
```

Der Decorator akzeptiert eine kanonische Plugin-ID, einen nicht leeren
Anzeigenamen und eine strikte semantische Version. Ein Skript deklariert genau
eine Plugin-Klasse. Verschiedene Skripte sind unabhängige Module und können mit
nativen Plugins kombiniert werden.

## Lebenszyklus

Alle Hooks sind optional:

- `on_process_begin(ctx)` und `on_destroy(ctx)` umschließen den Compilerprozess.
- `register(ctx)` registriert Optionen und Observer vor dem Einfrieren des Phasengraphen.
- `on_session_begin(ctx)` und `on_session_end(ctx)` umschließen einen Aufruf.
- `on_task_begin(ctx)` und `on_task_end(ctx)` umschließen eine Compilereinheit.

Ein Begin-Hook kann einen Python-Wert zurückgeben oder `ctx.state` setzen; der
zugehörige End-Hook sieht diesen Wert. Andere Hooks und Observer-Callbacks
müssen `None` zurückgeben. Standardmäßig sind Python-Plugins session-serial und
nicht reentrant; `@Plugin` kann dieselben Modelle wie ein natives Plugin wählen.

## Optionen und Observer

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

`neverc_plugin.phases` enthält alle 130 aus dem normativen Phasenschema
generierten eingebauten Phasenkonstanten. Observer-Frames stellen Phasen- und
Routendaten, opake Ein-/Ausgabe-Handles, geparste Pluginoptionen, Diagnosen,
Abbruchprüfungen und rohe Argumente für `driver.RAW_ARGUMENTS` bereit. Native
Kontext- und Frame-Handles prüfen ihre Lebensdauer: Die Verwendung eines nach
dem Callback behaltenen Objekts löst `RuntimeError` aus.

Options-kinds sind `flag`, `joined`, `separate` und `multi_arg`; Werttypen sind
`bool`, `int`, `uint`, `string`, `enum` und `path`; Multiplizitäten sind
`single`, `last_wins` und `append`. Enum-Optionen erhalten ein Mapping
`enum_values={Name: Ganzzahl}`. `argument_count` gilt nur für `multi_arg`.

## Fehler, Sicherheit und aktueller Umfang

Eine nicht behandelte Python-Ausnahme wird zu
`NEVERC_STATUS_PLUGIN_EXCEPTION`. Während eines aktiven Session-/Task-Callbacks
gibt NeverC den formatierten Traceback als strukturierte Plugindiagnose aus;
Import- und Aktivierungsfehler enthalten ihn im Loaderfehler. Der eingebettete
Interpreter wird prozessweit geteilt und absichtlich nicht finalisiert,
während plugin-eigene Objekte beim Entladen freigegeben werden.

Python-Plugins sind vertrauenswürdige Compilererweiterungen. Sie laufen im
Prozess, können beliebige Module importieren und besitzen dieselben Datei- und
Prozessrechte wie NeverC. Es gibt keine Sandbox.

Das Python-Binding ist keine reduzierte API: Die generierten `ctypes`-
Definitionen und nativen Trampolines decken alle 36 offiziellen C-ABI-Tabellen,
Records, Funktionen und Callback-Familien ab, einschließlich Mutation,
Interceptors und Providers. Lebensdauern, Transactions und Continuations werden
geprüft. Ein vollständiges Python-OLLVM-Beispiel mit SUB, BCF und FLA liegt in
`pluginsdk/python/examples/ollvm/`.
Die Rohdefinitionen liegen in `neverc_plugin.abi`, die Tabellendeskriptoren in
`neverc_plugin.domains`.
