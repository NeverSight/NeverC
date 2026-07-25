**Langues** : [English](ast-sema.md) | [简体中文](ast-sema.zh-CN.md) | [繁體中文](ast-sema.zh-TW.md) | [日本語](ast-sema.ja.md) | [한국어](ast-sema.ko.md) | [Français](ast-sema.fr.md) | [Deutsch](ast-sema.de.md) | [Español](ast-sema.es.md) | [Italiano](ast-sema.it.md) | [Русский](ast-sema.ru.md) | [العربية](ast-sema.ar.md)

# API de plugin pour l'AST, l'analyseur et la sémantique

`PluginAST.h` et `PluginSema.h` offrent un accès en C pur, limité à la tâche, à
l'arbre du frontal et au pipeline sémantique. Les identifiants stables de nœuds,
de propriétés et d'emplacements enfants sont générés à partir des définitions
concrètes de l'AST de NeverC ; un plugin ne reçoit jamais de pointeur C++
`Decl`, `Stmt`, `Type` ou `Sema`.

## Lire et construire des nœuds d'AST

Utilisez `NevercASTAPI` pour interroger les informations de nœud, les propriétés
de schéma, les enfants, les parents, les contextes de déclaration, les types, les
attributs et les détails des nœuds concrets courants. Les API par lots exigent un
nombre d'éléments, une capacité et un pas explicites.

`NevercASTBuilder` ne construit que des genres de nœuds déclarés dans le schéma.
Les propriétés et emplacements enfants obligatoires sont vérifiés à la
validation. Une validation réussie publie un nœud appartenant à la tâche ; une
validation échouée ne laisse aucun nœud partiellement visible. Détruisez chaque
constructeur après validation ou échec.

## Mutation atomique

Les changements d'AST utilisent `BeginASTMutation`, des opérations mises en
attente, puis `CommitASTMutation`. L'hôte valide l'appartenance, la compatibilité
des emplacements, la cardinalité, les liens parents, les cycles et les invariants
sémantiques avant de modifier l'arbre. `AbortASTMutation` abandonne toutes les
opérations en attente. Les notifications natives `TreeMutationListener` ne sont
envoyées qu'après une validation réussie.

L'exemple compilable
[`ASTRewritePlugin.c`](../../pluginsdk/examples/ASTRewritePlugin.c) montre un
intercepteur d'analyseur qui appelle l'analyseur intégré, construit un littéral
entier et remplace atomiquement l'initialiseur d'une variable.

## Remplacement de l'analyseur et de Sema

`neverc.syntax.parse` transforme un flux de jetons vérifié en `ASTUnit`.
`neverc.sema.analyze` transforme un produit d'AST en `SemanticUnit`. Les deux
phases disposent d'intercepteurs typés et de fournisseurs. Les phases d'extension
fines — déclaration, instruction, expression, nom de type, attribut, recherche,
conversion et mot-clé — restent disponibles lorsqu'on ne remplace qu'une partie
du frontal.

Le chemin intégré fusionné analyseur/Sema publie exactement les mêmes contrats
d'artefact qu'un remplacement. La relecture sémantique n'accepte que les genres
de nœuds pour lesquels NeverC peut reconstruire la portée, la recherche de noms,
la redéclaration et l'état de vérification de types. Rencontrer un genre concret
non pris en charge renvoie `NEVERC_STATUS_UNSUPPORTED_AST_KIND` ; jamais un arbre
partiellement rejoué n'est marqué comme sémantiquement complet.

## Cycle de vie et nettoyage

Les observateurs de cycle de vie de l'AST et de Sema sont livrés dans l'ordre du
source via le pont `TreeConsumer` de l'hôte. Les événements de début et de fin
restent appariés en cas d'erreur de syntaxe, d'erreur de plugin ou d'annulation.
Les poignées de tâche ne deviennent invalides qu'après l'exécution des derniers
événements de fin en lecture seule et des rappels de nettoyage.

## Vérification

```sh
python3 utils/plugin-api/gen-ast-schema.py --check
ctest --test-dir build-neverc \
  -R 'Plugin(AST|Parser|Sema|Frontend)' --output-on-failure
```

Avec `NEVERC_ENABLE_PLUGIN_FUZZERS=ON`, `plugin-ast-mutation-fuzzer` couvre le
décodage des propriétés, les constructeurs malformés, les poignées forgées et le
retour arrière des mutations.
