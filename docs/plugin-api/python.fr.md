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
`NEVERC_BUNDLE_PYTHON_RUNTIME=ON`. CMake peut employer le Python système pour
les scripts de build, mais cet interpréteur ne choisit pas l'ABI des plugins.
NeverC télécharge séparément une distribution de développement/runtime CPython
3.12.10 figée et vérifiée par SHA-256, lie le bridge des plugins à celle-ci,
la place dans `build/python`, puis installe le même runtime dans le répertoire
adjacent `python/`. Les builds source ordinaires et les archives officielles
exécutent donc les plugins avec CPython 3.12.10 sans Python runtime externe,
`PYTHONHOME` ni `PYTHONPATH`.

Pour un build hors ligne, utilisez `-DNEVERC_MANAGED_PYTHON_ROOT=/path/to/cpython-3.12.10` pour un arbre de
développement/runtime CPython 3.12.10 exact déjà extrait. NeverC le valide et
le copie dans le build sans modifier la source fournie.

Sous Linux, le bundler d'installation exige `patchelf` dans `PATH`. Comme CMake
exécute une sonde ABI, les builds de plugins Python gérés doivent actuellement
être natifs ; pour un cross build, désactivez Python ou utilisez une étape de
packaging native sur la plateforme target. Pour un compiler sans Python, passez
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
observer doivent renvoyer `None`. Par défaut, ils sont session-serial et non
réentrants ; `@Plugin` peut choisir les mêmes modèles qu'un plugin natif.

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

Le binding Python n'est pas une API réduite : les définitions `ctypes` générées
et les trampolines natifs couvrent les 36 tables officielles de l'ABI C, tous
les records, fonctions et callbacks, y compris mutations, interceptors et
providers. Les durées de vie, transactions et continuations sont contrôlées.
Un exemple OLLVM Python complet avec SUB, BCF et FLA se trouve dans
`pluginsdk/python/examples/ollvm/`.
Les définitions brutes se trouvent dans `neverc_plugin.abi` et les descripteurs
de tables dans `neverc_plugin.domains`.
