**Langues**: [English](../translate.md) | [简体中文](../zh-CN/translate.md) | [繁體中文](../zh-TW/translate.md) | [日本語](../ja/translate.md) | [한국어](../ko/translate.md) | [Français](translate.md) | [Deutsch](../de/translate.md) | [Español](../es/translate.md) | [Italiano](../it/translate.md) | [Русский](../ru/translate.md) | [العربية](../ar/translate.md)

[← Documentation](README.md)

# Traduire C++ vers NeverC

La commande expérimentale `neverc translate` produit du code source `.nc` vérifiable avec `cpp-core-v1`, `cpp-core-v2`, `cpp-project-v1` et `cpp-math-v1`.

**Seule la traduction de code C++ est actuellement implémentée.** La prise en charge d’E Language (易语言, `.e`), de Python, Go, Rust, TypeScript et JavaScript est prévue ; leurs traducteurs ne sont pas encore disponibles.

## Installation et traduction scalaire

Utilisez une installation normale de NeverC avec ses ressources standard. Le frontend C++ et les en-têtes SDK approuvés sont intégrés ; aucune installation séparée de Clang n’est nécessaire. Voir les [notes de compilation du frontend](../../utils/translate-frontends/cpp/README.md).

```sh
neverc translate --from cpp input.cpp -o output.nc
neverc output.nc -c -o output.o
```

Le profil `cpp-core-v1` accepte un unique fichier C++17 autonome sans include. Il prend en charge `int`, `unsigned int`, `bool`, `void`, les structures triviales, les fonctions non membres, les espaces de noms, surcharges et le contrôle de flux documenté. Toutes les déclarations du projet sont vérifiées, même inutilisées.

Sélectionnez `--profile cpp-core-v2` pour ajouter les alias `typedef`/`using` vérifiés, les énumérations de type sous-jacent entier pris en charge, `static_assert`, ainsi que des pointeurs d’objet et références de lvalue dans un périmètre limité. Les paramètres et résultats par référence conservent les alias ; les pointeurs nuls et les qualifications `const` imbriquées sont pris en charge. Les restrictions à un seul fichier source sans include restent applicables. Voir le [contrat core v2](../../utils/translate-frontends/docs/cpp-core-v2.md).

Core v2 ajoute les tableaux locaux de taille fixe et les champs tableau, l’indexation multidimensionnelle et les pointeurs ou références vers des tableaux. L’initialisation partielle met à zéro les éléments scalaires omis ; les enregistrements suivent l’initialisation sélectionnée et conserve l’ordre ainsi que les alias. La taille et l’expansion de l’initialisation sont limitées. Les tableaux globaux et de taille variable restent exclus.

Core v2 prend en charge `switch`/`case`/`default`, les instructions d’initialisation C++17, le passage au cas suivant et les annotations `[[fallthrough]]` validées. Le sélecteur est évalué une seule fois ; les switch et boucles imbriqués conservent les cibles de `break`/`continue`. Les plages case GNU et les autres attributs d’instruction restent exclus.

Core v2 prend désormais en charge les entiers signés et non signés de 8, 16, 32 et 64 bits, les types et littéraux caractères, ainsi que les requêtes constantes `sizeof`/`alignof`. Les promotions et la résolution des surcharges précèdent la normalisation ; `long`, `wchar_t` et le type de taille suivent la cible. Les types sous-jacents des enums peuvent aussi être plus étroits ou plus larges. Les chaînes à l’exécution et la STL restent en développement.

Core v2 prend aussi en charge les décalages et différences de pointeurs sur objets, leur incrémentation/décrémentation et les affectations composées, avec parcours des tableaux et pas multidimensionnels. Les fonctions auxiliaires générées préservent les règles C++17 pour un pointeur nul plus ou moins zéro et la différence de deux pointeurs nuls. Les comparaisons d’ordre entre pointeurs, les conversions pointeur/entier et les conteneurs et algorithmes STL restent exclus.

Core v2 compare les tailles et alignements ABI des types source au modèle cible propre à NeverC, y compris la taille des structures et les décalages des champs. Des assertions statiques vérifient à nouveau cette disposition à la compilation, et le manifeste en conserve les données.

Core v2 prend en charge les fonctions membres nommées non virtuelles des types enregistrement admis, dont les surcharges const, les méthodes qualifiées lvalue, les méthodes statiques et `this`. Les appels conservent l’identité de l’objet et évaluent le récepteur avant les arguments. Les appels non statiques sur des objets temporaires, l’héritage, les templates et la STL générale restent non pris en charge.

Core v2 prend en charge les constructeurs ordinaires définis par l’utilisateur pour les enregistrements à disposition standard et opérations de copie prises en charge. Objets locaux, champs et éléments de tableau sont construits dans leur stockage final ; les champs suivent l’ordre de déclaration. Les constructeurs explicitement default, délégués ou de déplacement et les exceptions restent exclus.

Core v2 crée un objet distinct pour chaque paramètre enregistrement passé par valeur et écrit le résultat directement dans la destination de l’appelant. Les constructeurs et méthodes suivent les mêmes règles ; les copies requises et les alias des références sont conservés. Pour les types triviaux admissibles, d’autres implémentations C++17 peuvent ajouter des copies d’arguments ou de résultats.

Core v2 prend en charge les destructeurs ordinaires définis par l’utilisateur et la destruction implicite des membres lors des sorties normales. Objets locaux, champs et éléments de tableau sont détruits dans l’ordre inverse ; les temporaires à la fin de l’expression complète, après capture des valeurs nécessaires. Retours, branches, boucles, break et continue effectuent le nettoyage requis. Les paramètres par valeur sont détruits à la sortie de la fonction appelée ; les objets retournés appartiennent à l’appelant. Appels explicites de destructeur, spécifications d’exception écrites, destruction statique et déroulement des exceptions restent exclus. La copie implicite non triviale, les déplacements et la STL complète sont encore en développement.

Core v2 exécute les constructeurs de copie et opérateurs d’affectation par copie ordinaires définis par l’utilisateur, avec un paramètre source `R&` ou `const R&`. La copie utilise la destination réelle et conserve les effets de bord ainsi que la référence retournée. La syntaxe d’affectation évalue d’abord l’opérande droit ; un appel explicite à `operator=` évalue d’abord l’objet récepteur. Les membres spéciaux default et la copie implicite non triviale restent exclus.

## Projets à plusieurs fichiers

Sélectionnez explicitement les unités de traduction dans une base de données de compilation et indiquez le répertoire racine du projet. Le frontend intégré analyse chaque unité séparément ; la fusion vérifie les définitions, la liaison, les types partagés et le respect de la règle de définition unique (ODR) de façon conservatrice.

```sh
neverc translate --from cpp --profile cpp-project-v1 \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/a.cpp src/b.cpp --out-dir generated
```

La sortie comprend `translated.nc` et `translated.h`. Seuls les en-têtes du projet situés sous cette racine sont admis. Si un fichier possède plusieurs configurations, sélectionnez son indice de base zéro avec `--compdb-entry src/a.cpp=4`. Les commandes de compilation stockées sont lues comme des données et ne sont jamais exécutées.

## Mathématiques double précision limitées

`cpp-math-v1` ajoute aux projets `double`, les conversions/comparaisons documentées et les signatures exactes `std::fabs(double)` et `std::floor(double)`. Il utilise les en-têtes intégrés de Clang 20.1.8 / libc++ 200100 / macOS 15.5 et exige une cible macOS 15.0 explicite (arm64 ou x86_64). Les opérations arithmétiques flottantes générales restent exclues. Les interruptions flottantes doivent être masquées et la mise à zéro des subnormaux désactivée ; les quatre modes d’arrondi standard sont testés.

```sh
neverc translate --from cpp --profile cpp-math-v1 \
  --target arm64-apple-macosx15.0.0 \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/math.cpp --out-dir generated-math
```

La traduction mathématique vérifie aussi les en-têtes NeverC installés, l’identité des implémentations intégrées et une édition de liens réelle. Les modules générés utilisent le runtime NeverC. Avec `-fno-builtin-std`, la traduction échoue avant l’écriture des fichiers uniquement si le code final nécessite les correspondances `fabs`/`floor`. Le code mathématique qui n’en a pas besoin reste traduisible.

## Validation et sortie

Remplacez l’option de sortie par `--check` pour effectuer les mêmes analyses, génération et validations syntaxiques et objet sans conserver les fichiers générés. `--report PATH` écrit les diagnostics structurés. La traduction n’exécute jamais le programme source.

Les sorties et fichiers annexes ne sont jamais écrasés. `--out-dir` exige un nouveau répertoire sous un parent existant ; `-o` exige un nouveau chemin `.nc`. Le manifeste indique les exigences de cible, les empreintes des entrées et sorties et la procédure de compilation ; la carte source relie les lignes générées aux positions d’origine.

Les résultats de CI, les vérifications d’exécution et d’installation ainsi que les tests ignorés sont consignés par plateforme. macOS arm64 natif et macOS x86_64 via Rosetta restent des environnements de validation distincts. Le périmètre annoncé ne couvre pas l’ensemble de C++/STL, ni les exceptions, les templates, les chaînes de caractères ou `std::vector`. Consultez la [matrice de prise en charge](../../utils/translate-frontends/docs/support-matrix.md), le [protocole et les règles de récupération](../../utils/translate-frontends/docs/protocol.md) et le [projet exemple](../../tests/neverc/Inputs/translate/cpp/project).
