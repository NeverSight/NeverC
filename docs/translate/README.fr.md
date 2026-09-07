**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Documentation](../README.fr.md)

# Traduire C++ vers NeverC

La commande expérimentale `neverc translate` produit du code source `.nc` vérifiable avec `cpp-core-v1`, `cpp-project-v1` et `cpp-math-v1`.

**Seule la traduction de code C++ est actuellement implémentée.** La prise en charge d’E Language (易语言, `.e`), de Python, Go, Rust, TypeScript et JavaScript est prévue ; leurs traducteurs ne sont pas encore disponibles.

## Installation et traduction scalaire

Compilez ou installez le programme auxiliaire basé sur la version imposée Clang 20.1.8 en suivant les [instructions du frontend](../../utils/translate-frontends/cpp/README.md). Placez-le à côté de NeverC, définissez `NEVERC_CPP_FRONTEND` ou passez `--frontend PATH`. Il est nécessaire pour traduire, mais pas pour compiler les sorties scalaires ou de projet déjà générées.

```sh
neverc translate --from cpp input.cpp -o output.nc
neverc output.nc -c -o output.o
```

Le profil `cpp-core-v1` accepte un unique fichier C++17 autonome sans include. Il prend en charge `int`, `unsigned int`, `bool`, `void`, les structures triviales, les fonctions non membres, les espaces de noms, surcharges et le contrôle de flux documenté. Toutes les déclarations du projet sont vérifiées, même inutilisées.

## Projets à plusieurs fichiers

Sélectionnez explicitement les unités de traduction dans une base de données de compilation et indiquez le répertoire racine du projet. L’auxiliaire analyse chaque unité séparément ; la fusion vérifie les définitions, la liaison, les types partagés et le respect de la règle de définition unique (ODR) de façon conservatrice.

```sh
neverc translate --from cpp --profile cpp-project-v1 \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/a.cpp src/b.cpp --out-dir generated
```

La sortie comprend `translated.nc` et `translated.h`. Seuls les en-têtes du projet situés sous cette racine sont admis. Si un fichier possède plusieurs configurations, sélectionnez son indice de base zéro avec `--compdb-entry src/a.cpp=4`. Les commandes de compilation stockées sont lues comme des données et ne sont jamais exécutées.

## Mathématiques double précision limitées

`cpp-math-v1` ajoute aux projets `double`, les conversions/comparaisons documentées et les signatures exactes `std::fabs(double)` et `std::floor(double)`. Il exige Clang 20.1.8 / libc++ 200100 / SDK macOS 15.5, un descripteur SDK et une cible macOS 15.0 explicite (arm64 ou x86_64). Les opérations arithmétiques flottantes générales restent exclues. Les interruptions flottantes doivent être masquées et la mise à zéro des subnormaux désactivée ; les quatre modes d’arrondi standard sont testés.

```sh
neverc translate --from cpp --profile cpp-math-v1 \
  --target arm64-apple-macosx15.0.0 --cpp-sdk /path/to/neverc-cpp-sdk.json \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/math.cpp --out-dir generated-math
```

La traduction mathématique vérifie aussi les en-têtes NeverC installés, l’identité des implémentations intégrées et une édition de liens réelle. Les modules générés utilisent le runtime NeverC sans auxiliaire C++ ni SDK. Avec `-fno-builtin-std`, la traduction échoue avant l’écriture des fichiers uniquement si le code final nécessite les correspondances `fabs`/`floor`. Le code mathématique qui n’en a pas besoin reste traduisible.

## Validation et sortie

Remplacez l’option de sortie par `--check` pour effectuer les mêmes analyses, génération et validations syntaxiques et objet sans conserver les fichiers générés. `--report PATH` écrit les diagnostics structurés. La traduction n’exécute jamais le programme source.

Les sorties et fichiers annexes ne sont jamais écrasés. `--out-dir` exige un nouveau répertoire sous un parent existant ; `-o` exige un nouveau chemin `.nc`. Le manifeste indique les exigences de cible, les empreintes des entrées et sorties et la procédure de compilation ; la carte source relie les lignes générées aux positions d’origine.

L’exécution a été vérifiée sur macOS arm64 en mode natif et sur macOS x86_64 via Rosetta. Ces résultats ne valident pas l’exécution native sur Mac Intel, ni la prise en charge de Linux, Windows ou d’autres cibles. Le périmètre annoncé ne couvre pas l’ensemble de C++/STL, ni les pointeurs, les références, les tableaux, les exceptions, les templates, les chaînes de caractères ou `std::vector`. Consultez la [matrice de prise en charge](../../utils/translate-frontends/docs/support-matrix.md), le [protocole et les règles de récupération](../../utils/translate-frontends/docs/protocol.md) et le [projet exemple](../../tests/neverc/Inputs/translate/cpp/project/).
