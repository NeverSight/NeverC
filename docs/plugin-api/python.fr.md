**Langues** : [English](python.md) | [简体中文](python.zh-CN.md) | [繁體中文](python.zh-TW.md) | [日本語](python.ja.md) | [한국어](python.ko.md) | [Français](python.fr.md) | [Deutsch](python.de.md) | [Español](python.es.md) | [Italiano](python.it.md) | [Русский](python.ru.md) | [العربية](python.ar.md)

[← ABI des plugins NeverC](README.fr.md)

# Plugins Python

NeverC peut charger un fichier source Python avec la même option `-fplugin=`
que les plugins natifs. Une compilation source normale active par défaut les
plugins Python et l'installation du runtime embarqué :

```sh
cmake -S llvm -B build -C neverc/cmake/caches/NeverC.cmake \
  -DCMAKE_INSTALL_PREFIX="$PWD/neverc-install"
cmake --build build --target install
```

Les nouveaux builds utilisent par défaut `NEVERC_ENABLE_PYTHON_PLUGINS=ON` et
`NEVERC_BUNDLE_PYTHON_RUNTIME=ON`. La configuration exige CPython 3.10 ou plus,
ses headers d'embedding et sa bibliothèque partagée. L'installation copie
automatiquement la version exacte sélectionnée dans le répertoire adjacent
`python/`. L'exécutable du build tree peut encore utiliser le Python de build,
mais le compiler installé n'a besoin ni de Python externe, ni de `PYTHONHOME`,
ni de `PYTHONPATH` à l'exécution. Les archives officielles sélectionnent et
embarquent CPython 3.12.

Sous Linux, le bundler d'installation exige `patchelf` dans `PATH`. Avec
`CMAKE_CROSSCOMPILING`, l'emballage automatique est refusé pour éviter de placer
l'interpréteur host dans un compiler target ; désactivez-le et packagez
explicitement un runtime target. Pour un compiler sans Python, passez
`-DNEVERC_ENABLE_PYTHON_PLUGINS=OFF` et
`-DNEVERC_BUNDLE_PYTHON_RUNTIME=OFF` ensemble.

Installez le package d'écriture avec `python3 -m pip install
./pluginsdk/python`, ajoutez ce répertoire à
`PYTHONPATH`, ou compilez et installez le composant `neverc-pluginsdk`. NeverC
détecte aussi le SDK préparé dans
`<répertoire de neverc>/../pluginsdk/python`.

## Plugin minimal

```python
from neverc_plugin import Plugin


@Plugin(id="com.example.minimal", name="Minimal Python Plugin", version="1.0.0")
class MinimalPlugin:
    def on_process_begin(self, ctx):
        ctx.state = {"sessions": 0}
```

Chargez-le par son chemin dans le système de fichiers :

```sh
neverc -fplugin=/absolute/path/to/minimal.py -fsyntax-only input.c
```

Le décorateur accepte un identifiant canonique, un nom d'affichage non vide et
une version sémantique stricte. Un script déclare exactement une classe de
plugin. Les scripts sont des modules indépendants et peuvent être combinés
avec des plugins natifs.

## Cycle de vie

Tous les hooks sont facultatifs :

- `on_process_begin(ctx)` et `on_destroy(ctx)` encadrent le processus du compilateur.
- `register(ctx)` enregistre options et observers avant le gel du graphe de phases.
- `on_session_begin(ctx)` et `on_session_end(ctx)` encadrent une invocation.
- `on_task_begin(ctx)` et `on_task_end(ctx)` encadrent une unité de compilation.

Un hook begin peut renvoyer une valeur Python ou affecter `ctx.state`; le hook
end correspondant retrouve cette valeur. Les autres hooks et callbacks
observer doivent renvoyer `None`. En v1, les plugins Python sont session-serial
et non réentrants.

## Options et observers

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

`neverc_plugin.phases` contient les 130 constantes de phases intégrées générées
depuis le schéma normatif. Les frames observer exposent les données de phase et
de route, des handles d'entrée/sortie opaques, les options analysées, les
diagnostics, l'annulation et les arguments bruts de `driver.RAW_ARGUMENTS`.
Les handles de contexte et de frame vérifient leur durée de vie : utiliser un
objet conservé après son callback lève `RuntimeError`.

Les kinds d'option sont `flag`, `joined`, `separate` et `multi_arg`; les types
de valeur sont `bool`, `int`, `uint`, `string`, `enum` et `path`; les
multiplicités sont `single`, `last_wins` et `append`. Une enum reçoit le mapping
`enum_values={nom: entier}`. `argument_count` ne s'applique qu'à `multi_arg`.

## Erreurs, sécurité et périmètre actuel

Une exception Python non interceptée devient `NEVERC_STATUS_PLUGIN_EXCEPTION`.
Dans un callback session/task actif, NeverC émet la traceback formatée comme
diagnostic structuré ; les erreurs d'import et d'activation l'incluent dans le
message du loader. L'interpréteur embarqué est partagé par le processus et
n'est volontairement pas finalisé, tandis que les objets propres au plugin
sont libérés au déchargement.

Les plugins Python sont des extensions de compilateur de confiance. Ils
s'exécutent dans le processus, peuvent importer tout module et possèdent les
mêmes droits système que NeverC. Il n'existe aucune sandbox.

La v1 reste volontairement en lecture seule, hormis l'enregistrement des
options. Elle n'expose ni interceptor, ni provider, ni mutation d'artifact, ni
modèle d'objet IR/MIR/Link spécialisé, ni subinterpreter, manifest ou point
d'entrée module/factory. Ces fonctions exigent des wrappers de transaction et
de continuation dont la durée de vie peut être imposée ; l'ABI C native reste
disponible lorsqu'elles sont nécessaires.
