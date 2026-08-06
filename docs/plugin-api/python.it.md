**Lingue**: [English](python.md) | [简体中文](python.zh-CN.md) | [繁體中文](python.zh-TW.md) | [日本語](python.ja.md) | [한국어](python.ko.md) | [Français](python.fr.md) | [Deutsch](python.de.md) | [Español](python.es.md) | [Italiano](python.it.md) | [Русский](python.ru.md) | [العربية](python.ar.md)

[← ABI dei plugin NeverC](README.it.md)

# Plugin Python

NeverC può caricare un file sorgente Python tramite la stessa opzione
`-fplugin=` usata dai plugin nativi. Il supporto Python è opzionale, quindi una
build normale non acquisisce una dipendenza da CPython:

```sh
cmake -S llvm -B build -DNEVERC_ENABLE_PYTHON_PLUGINS=ON
cmake --build build --target neverc
```

La build abilitata richiede CPython 3.10 o successivo e i relativi file di
sviluppo per l'embedding. Installare il pacchetto di authoring con
`python3 -m pip install ./pluginsdk/python`, aggiungere la directory a
`PYTHONPATH`, oppure compilare e installare il componente `neverc-pluginsdk`.
NeverC rileva anche l'SDK preparato in
`<directory di neverc>/../pluginsdk/python`.

## Plugin minimo

```python
from neverc_plugin import Plugin


@Plugin(id="com.example.minimal", name="Minimal Python Plugin", version="1.0.0")
class MinimalPlugin:
    def on_process_begin(self, ctx):
        ctx.state = {"sessions": 0}
```

Caricarlo tramite il percorso nel filesystem:

```sh
neverc -fplugin=/absolute/path/to/minimal.py -fsyntax-only input.c
```

Il decorator accetta un ID canonico, un nome visualizzato non vuoto e una
versione semantica rigorosa. Uno script dichiara esattamente una classe plugin.
Script diversi sono moduli indipendenti e possono essere combinati con plugin
nativi.

## Ciclo di vita

Tutti gli hook sono opzionali:

- `on_process_begin(ctx)` e `on_destroy(ctx)` racchiudono il processo compiler.
- `register(ctx)` registra opzioni e observer prima del freeze del grafo delle fasi.
- `on_session_begin(ctx)` e `on_session_end(ctx)` racchiudono un'invocazione.
- `on_task_begin(ctx)` e `on_task_end(ctx)` racchiudono un'unità di compilazione.

Un hook begin può restituire un valore Python o assegnare `ctx.state`; l'hook
end corrispondente può leggerlo. Gli altri hook e callback observer devono
restituire `None`. I plugin Python v1 sono session-serial e non rientranti.

## Opzioni e observer

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

`neverc_plugin.phases` contiene tutte le 130 costanti delle fasi integrate,
generate dallo schema normativo. I frame observer espongono dati di fase e
route, handle opachi di input/output, opzioni analizzate, diagnostica, controllo
della cancellazione e argomenti grezzi per `driver.RAW_ARGUMENTS`. Gli handle
nativi verificano la propria lifetime: usare un oggetto conservato dopo il
callback genera `RuntimeError`.

I kind delle opzioni sono `flag`, `joined`, `separate` e `multi_arg`; i tipi
sono `bool`, `int`, `uint`, `string`, `enum` e `path`; le molteplicità sono
`single`, `last_wins` e `append`. Una enum riceve il mapping
`enum_values={nome: intero}`. `argument_count` vale solo per `multi_arg`.

## Errori, sicurezza e ambito attuale

Un'eccezione Python non gestita diventa `NEVERC_STATUS_PLUGIN_EXCEPTION`.
Durante un callback session/task attivo, NeverC emette il traceback formattato
come diagnostica strutturata; gli errori di import e activation lo includono
nell'errore del loader. L'interprete embedded è condiviso nel processo e non
viene intenzionalmente finalizzato, mentre gli oggetti del plugin vengono
rilasciati all'unload.

I plugin Python sono estensioni compiler fidate. Vengono eseguiti nel processo,
possono importare moduli arbitrari e hanno gli stessi permessi di NeverC. Non è
presente una sandbox.

La v1 è intenzionalmente read-only oltre alla registrazione delle opzioni. Non
espone interceptor, provider, mutazioni di artifact, modelli IR/MIR/Link
specifici del dominio, subinterpreter, manifest o entry point module/factory.
Queste capacità richiedono wrapper transaction/continuation con lifetime
vincolabile; quando servono resta disponibile l'ABI C nativa.
