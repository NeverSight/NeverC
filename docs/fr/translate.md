**Langues**: [English](../translate.md) | [简体中文](../zh-CN/translate.md) | [繁體中文](../zh-TW/translate.md) | [日本語](../ja/translate.md) | [한국어](../ko/translate.md) | [Français](translate.md) | [Deutsch](../de/translate.md) | [Español](../es/translate.md) | [Italiano](../it/translate.md) | [Русский](../ru/translate.md) | [العربية](../ar/translate.md)

[← Documentation](README.md)

# Traduire C++ vers NeverC

La commande expérimentale `neverc translate` produit du code source `.nc` vérifiable avec `cpp-core-v1`, `cpp-core-v2`, `cpp-project-v1` et `cpp-math-v1`.

**Seule la traduction de code C++ est actuellement implémentée.** La prise en charge d’E Language (易语言, `.e`), de Python, Go, Rust, TypeScript et JavaScript est prévue ; leurs traducteurs ne sont pas encore disponibles.

Core v2 prend en charge les variables globales modifiables de type entier, booléen ou énumération dans un espace de noms, avec initialisation à zéro ou initialisation constante entièrement vérifiée. Leur stockage et leur adresse persistent entre les appels ; références, pointeurs et arguments par défaut accèdent à la même variable. Une déclaration `extern` doit avoir une définition dans la même unité source. Les globales const restent en lecture seule. Les restrictions propres aux initialisations dynamiques, aux records/tableaux/pointeurs/références globaux, au stockage local aux threads et aux variables locales statiques restent applicables.

Core v2 prend en charge les boucles `for` par plage de C++17 sur les tableaux fixes admis et les plages définies dans le source, avec résolution des appels membres ou ADL à `begin/end`. Les variables par valeur ou référence, les itérateurs de type record et les sentinelles de types distincts conservent les règles ordinaires d’appel et de durée de vie. La plage et `begin/end` sont initialisés une seule fois ; les objets de chaque itération sont détruits avant l’incrément ou la sortie, y compris avec `continue`, `break` et `return`. Les templates, en-têtes standard, conteneurs STL, liaisons structurées et instructions d’initialisation C++20 restent hors de cette extension. La validation native exige le CI de la révision concernée.

Core v2 accepte les membres de données privés et protégés dans les classes à disposition standard par ailleurs prises en charge. Clang intégré vérifie les accès avant la traduction ; méthodes, constructeurs, fabriques, arguments par défaut et opérations de copie/déplacement autorisés utilisent le même stockage typé des membres. Les accès externes illégaux restent des diagnostics C++. Les spécificateurs d’accès sont des règles du langage source, sans garantie de confidentialité à l’exécution. Les classes hors disposition standard à accès mixtes, les déclarations d’amis templates, l’héritage et les types de champs non pris en charge restent limités. Les résultats natifs exigent le CI de la révision concernée.

Core v2 accepte les fonctions amies non templates et les types amis résolus dans les classes prises en charge. ADL des amis cachés, opérateurs, arguments par défaut, définitions dans un espace de noms, classes amies et membres amis désignés conservent les contrôles d’accès de Clang et les appels typés ordinaires. Les redéclarations conservent une seule identité de fonction. Les formes amies non prises en charge ou dépendantes et les amis templates restent rejetés ; tous les corps des fonctions amies sont inspectés. Les résultats natifs exigent le CI de la révision concernée.

Core v2 accepte les records imbriqués nommés non templates, y compris les types privés ou protégés exposés par des alias ou fabriques autorisés. Chaque objet conserve son propre stockage et récepteur ; références explicites à l’objet externe, identité de type par portée, copies et déplacements de membres et tableaux, destruction et itérateurs imbriqués conservent leur sémantique habituelle. Les dépendances par valeur sont émises en premier. Records imbriqués anonymes, templates, héritage et STL complète restent à réaliser ; la validation native exige le CI de la révision concernée.

Core v2 accepte les membres statiques définis de type entier, booléen ou énumération, avec définitions inline/constexpr ou hors classe et initialisation constante ou à zéro. Toutes les instances partagent le même stockage typé. Les effets du récepteur et la destruction des temporaires sont conservés ; les références aux membres statiques restent valides après un récepteur temporaire. Les membres const non inline de type entier, booléen ou énumération, avec une initialisation constante vérifiée dans la classe, permettent aussi la lecture de leur valeur ou son abandon sans définition séparée. Aucun objet global n’est créé ; prendre leur adresse ou leur lier une référence exige toujours une définition dans la même unité source. Initialisation dynamique, autres types statiques et STL complète restent inachevés ; la validation native exige le CI de la révision concernée.

Core v2 accepte les variables locales statiques non volatile de type entier, booléen ou énumération, avec initialisation à zéro ou initialisation constante vérifiée. Valeurs et adresses persistent entre les appels, la récursion et les sorties de bloc ; références et pointeurs restent valides après le retour. Des variables homonymes dans des portées différentes désignent des objets distincts. Les locales static constexpr sont admises dans les fonctions ordinaires. Les locales statiques dans les fonctions constexpr, l’initialisation dynamique, le stockage local aux threads et les autres types statiques restent exclus. C++/STL complet reste inachevé ; la validation native exige le CI de la révision concernée.

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

Core v2 prend en charge les fonctions membres nommées non virtuelles des types enregistrement admis, dont les surcharges const, les méthodes qualifiées lvalue, les méthodes statiques et `this`. Les appels conservent l’identité de l’objet et évaluent le récepteur avant les arguments.

Core v2 prend en charge les constructeurs ordinaires définis par l’utilisateur pour les enregistrements à disposition standard et opérations de copie prises en charge. Objets locaux, champs et éléments de tableau sont construits dans leur stockage final ; les champs suivent l’ordre de déclaration. Les constructeurs délégués et les exceptions restent exclus.

Core v2 crée un objet distinct pour chaque paramètre enregistrement passé par valeur et écrit le résultat directement dans la destination de l’appelant. Les constructeurs et méthodes suivent les mêmes règles ; les copies requises et les alias des références sont conservés. Pour les types triviaux admissibles, d’autres implémentations C++17 peuvent ajouter des copies d’arguments ou de résultats.

Core v2 prend en charge les destructeurs ordinaires définis par l’utilisateur et la destruction implicite des membres lors des sorties normales. Objets locaux, champs et éléments de tableau sont détruits dans l’ordre inverse ; les temporaires à la fin de l’expression complète, après capture des valeurs nécessaires. Retours, branches, boucles, break et continue effectuent le nettoyage requis. Les paramètres par valeur sont détruits à la sortie de la fonction appelée ; les objets retournés appartiennent à l’appelant. Appels explicites de destructeur, destruction statique et déroulement des exceptions restent exclus.

Core v2 exécute les constructeurs de copie et opérateurs d’affectation par copie ordinaires définis par l’utilisateur, avec un paramètre source `R&` ou `const R&`. La copie utilise la destination réelle et conserve les effets de bord ainsi que la référence retournée. La syntaxe d’affectation évalue d’abord l’opérande droit ; un appel explicite à `operator=` évalue d’abord l’objet récepteur.

Core v2 prend aussi en charge les constructeurs par défaut générés ou marqués default et les destructeurs default, y compris les membres de type enregistrement ou tableau imbriqué. La construction utilise la destination réelle et l’ordre de déclaration ; l’initialisation par valeur ne met à zéro que lorsque C++ l’exige. Les règles distinctes du default défini hors classe sont conservées. Un constructeur inutilisé ou présent seulement dans une expression non évaluée ne nécessite aucun corps artificiel.

Core v2 prend en charge les constructeurs de copie implicites et explicitement default, y compris les enregistrements imbriqués et tableaux multidimensionnels. Les copies de membres sélectionnées utilisent la destination réelle, dans l’ordre des déclarations et des éléments ; l’adresse du tableau source est évaluée une fois. Les copies triviales conservent les pointeurs stockés. Les paramètres par valeur et retours depuis un objet source gardent les effets de copie ; transmettre directement une prvalue n’en ajoute pas.

Core v2 prend aussi en charge l’affectation par copie implicite ou explicitement default. Membres et éléments de tableau conservent leurs opérations sélectionnées et leur ordre ; les copies triviales générées deviennent des affectations typées sans appel externe de copie mémoire. L’affectation triviale conserve les pointeurs et renvoie le récepteur réel. La syntaxe opérateur capture d’abord la référence source, l’appel membre explicite d’abord le récepteur ; les valeurs sources sont lues après ces effets. Aucun objet supplémentaire n’est construit ou détruit pour l’affectation.

Core v2 prend en charge les initialiseurs par défaut des champs admis, avec accès aux membres précédents, appels ordinaires, records imbriqués et tableaux. Le défaut sélectionné utilise son objet réel comme `this` ; les clauses explicites d’agrégat conservent le `this` de l’appelant. Une initialisation explicite remplace le défaut du membre. Copie et affectation implicites/default ne relancent pas les défauts ; un constructeur de copie utilisateur peut les sélectionner pour les membres omis. Initialisation des membres du constructeur et initialisation d’agrégat conservent leurs limites respectives de destruction des temporaires. Modèles et STL complète restent en développement.

Core v2 accepte les références rvalue vers des objets déjà vivants : alias de scalaires, pointeurs, records et tableaux, paramètres et retours par référence, xvalues conditionnelles et méthodes ordinaires qualifiées `&&`. `static_cast<R&&>(live)` conserve le même objet ; une variable de référence rvalue nommée reste une lvalue. La surcharge et les copies existantes suivent le choix de Clang. Les références ne créent aucune responsabilité de destruction supplémentaire.

Core v2 exécute les constructeurs et affectations de déplacement ordinaires définis par l’utilisateur depuis une source vivante `R&&` ou `const R&&`. La construction utilise le stockage final ; l’affectation conserve les modifications de la source, l’ordre d’évaluation et l’alias `R&` retourné, y compris avec un receveur qualifié `&`/`&&`. Une référence rvalue nommée choisit toujours les surcharges lvalue ; les constructeurs explicites gardent leurs règles d’initialisation. Le déplacement ne termine pas la vie de la source : source et destination sont détruites normalement.

Core v2 prend aussi en charge la construction et l’affectation de déplacement implicites ou explicitement default, avec records imbriqués et tableaux multidimensionnels. Les membres exécutent la copie ou le déplacement choisi par C++, dans l’ordre de déclaration et des éléments. Ces déplacements ne relancent pas les initialiseurs par défaut ; les opérations triviales conservent les valeurs des pointeurs. Les sources sont détruites normalement et aucun corps fictif n’est requis pour une opération générée inutilisée ou triviale.

Core v2 accepte les déclarations standard résolues `noexcept`, `noexcept(true/false)` et `throw()` de C++17, ainsi que les requêtes constantes `noexcept(expression)`. Ces requêtes respectent les spécifications des fonctions et destructeurs sélectionnés sans exécuter leurs opérandes. Tous les opérandes et spécifications écrites sont inspectés, même dans le code inutilisé. Levée et capture d’exceptions, déroulement de pile, modèles et STL complète restent en développement.

Core v2 accepte les opérateurs ordinaires surchargés, membres ou libres : arithmétique, comparaisons, indexation, déréférencement, incréments, foncteurs et signatures générales d’affectation. Fonctions sélectionnées, alias de références et objets résultats sont conservés. La notation opérateur respecte l’ordre C++17 ; les opérateurs logiques surchargés évaluent les deux opérandes. Les opérateurs libres d’affectation initialisent les paramètres de droite à gauche puis les détruisent dans l’ordre inverse. Modèles, allocation et STL complète restent en développement.

Core v2 accepte aussi les fonctions de conversion ordinaires sur des objets vivants : conversions entières, énumérées et de pointeurs implicites ou explicites, `bool` explicite en contexte conditionnel, résultats par référence ou objet. Chaque conversion appelle une fois la fonction membre sélectionnée. Les références conservent leurs alias ; les prvalues objets initialisent directement leur destination. La conversion d’une référence en valeur conserve la copie ou le déplacement sélectionné. Qualificateurs const/référence, constexpr et noexcept suivent C++17.

Core v2 accepte désormais les objets temporaires récepteurs et les arguments temporaires scalaires ou enregistrement passés par référence jusqu’à la fin de l’expression complète, pour les constructeurs, méthodes, opérateurs et conversions. Chaque évaluation dispose d’un stockage réel ; objets résultats et alias conservent leur destination. Après l’appel et la destruction des paramètres, les temporaires sont détruits dans l’ordre inverse de construction, y compris dans les conditions et boucles. Les sous-objets tableaux des enregistrements temporaires sont acceptés.

Core v2 prolonge désormais la vie des temporaires liés à des références locales automatiques ordinaires, notamment `const T& r{T{...}}` et `T&& r = T{...}`. Scalaires, énumérations, pointeurs et enregistrements disposent d’un stockage réel. Une référence à un membre ou élément de tableau maintient l’enregistrement complet en vie lorsque C++ accorde cette prolongation. Les initialisations entre accolades, avec ou sans signe égal, conservent le même objet. La destruction suit la portée de la référence, y compris conditions, boucles et sorties anticipées ; les autres temporaires de l’initialiseur finissent avec leur expression complète. Les alias n’ajoutent aucun propriétaire ; copies et déplacements ultérieurs conservent les opérations sélectionnées. Références statiques/globales/locales au thread, champs références, modèles et STL complète restent en développement.

Core v2 accepte aussi les tableaux temporaires autonomes de taille fixe dans les appels, conversions en pointeur, indexations, expressions ignorées et références locales automatiques. Chaque tableau a une destination réelle ; valeurs scalaires, constructeurs et initialisateurs par défaut communs initialisent directement les éléments dans l’ordre, sans propriétaires supplémentaires. Lorsque C++ prolonge la durée de vie, les références aux lignes ou éléments multidimensionnels conservent le tableau complet. Les éléments sont détruits en ordre inverse à la fin de l’expression complète ou de la portée de la référence. Adresses typées et limites existantes de taille, stockage et expansion sont conservées. C++/STL complet reste en développement.

Core v2 prend en charge les classes vides à disposition standard et les objets fonctionnels ou de conversion sans état. Les objets vides conservent une taille et un alignement C++ d’un octet, un stockage distinct lorsque nécessaire, les constructeurs et opérateurs sélectionnés et la destruction normale. Les copies triviales évaluent toujours leurs opérandes ; les éléments vides des tableaux et des enregistrements comptent dans les limites de stockage. Le NC généré utilise un octet interne et la liste des champs source reste vide. L’héritage, les templates et la STL complète restent en développement.

Core v2 prend en charge les conversions vers void, `void()` et `void{}`, avec les alias void compatibles, appels, retours, expressions virgule et branches conditionnelles. Ignorer une lvalue non volatile conserve les effets du récepteur et de l’index sans lire sa valeur stockée. Les temporaires sont toujours construits et détruits à la fin de l’expression complète d’origine. Aucune variable ni valeur void n’est générée. Les opérandes constants et noexcept restent vérifiés. Les objets volatile, types non pris en charge, templates et STL complète restent hors de cette étape.

Core v2 accepte les arguments par défaut des fonctions, méthodes, opérateurs d’appel et constructeurs utilisateur pris en charge, y compris les paramètres supplémentaires des constructeurs de copie et de déplacement. Les noms sont résolus au point de déclaration et les expressions évaluées à chaque appel qui omet l’argument. Identité et durée de vie des références et valeurs sont conservées. Pour les éléments de tableau sans initialiseur et les copies de tableau générées, les temporaires des arguments par défaut sont détruits avant l’élément suivant ; les clauses explicites gardent la limite de l’expression complète. Les valeurs inutilisées ou remplacées restent vérifiées. Les templates et la STL complète restent inachevés.

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
