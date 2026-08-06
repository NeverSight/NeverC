**Sprachen**: [English](python.md) | [简体中文](python.zh-CN.md) | [繁體中文](python.zh-TW.md) | [日本語](python.ja.md) | [한국어](python.ko.md) | [Français](python.fr.md) | [Deutsch](python.de.md) | [Español](python.es.md) | [Italiano](python.it.md) | [Русский](python.ru.md) | [العربية](python.ar.md)

[← NeverC-Plugin-ABI](README.de.md)

# Python-Plugins

NeverC kann eine Python-Quelldatei über dieselbe Option `-fplugin=` laden, die
für native Plugins verwendet wird. Python-Unterstützung ist optional, sodass
normale Builds keine CPython-Abhängigkeit erhalten:

```sh
cmake -S llvm -B build -DNEVERC_ENABLE_PYTHON_PLUGINS=ON
cmake --build build --target neverc
```

Ein aktivierter Build benötigt CPython 3.10 oder neuer sowie dessen
Embedding-Entwicklungsdateien. Installieren Sie das Autorenpaket mit
`python3 -m pip install ./pluginsdk/python`, nehmen Sie das Verzeichnis in
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
müssen `None` zurückgeben. Python-Plugins der v1 sind session-serial und nicht
reentrant.

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

Die v1 ist außer der Optionsregistrierung absichtlich schreibgeschützt. Sie
stellt keine Interceptors, Providers, Artifact-Mutationen, domänenspezifischen
IR/MIR/Link-Objektmodelle, Subinterpreter, Manifeste oder Module-/Factory-
Einstiegspunkte bereit. Dafür sind Transaction- und Continuation-Wrapper mit
erzwingbarer Lebensdauer nötig; wenn diese Fähigkeiten gebraucht werden,
bleibt die native C-ABI verfügbar.
