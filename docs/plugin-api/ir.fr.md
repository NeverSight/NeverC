**Langues** : [English](ir.md) | [简体中文](ir.zh-CN.md) | [繁體中文](ir.zh-TW.md) | [日本語](ir.ja.md) | [한국어](ir.ko.md) | [Français](ir.fr.md) | [Deutsch](ir.de.md) | [Español](ir.es.md) | [Italiano](ir.it.md) | [Русский](ir.ru.md) | [العربية](ir.ar.md)

# API IR des plugins NeverC

La première ABI publique de plugin expose l'IR de LLVM au moyen de tables C
stables. Les plugins n'incluent pas d'en-têtes LLVM et ne doivent pas convertir
une poignée NeverC en objet LLVM.

## Interfaces

Interrogez les interfaces depuis `neverc_plugin_entry` avec
`NevercBootstrapAPI.QueryInterface` :

- `NEVERC_INTERFACE_IR_CORE` — interrogations de modules, types, valeurs, CFG,
  métadonnées, attributs, constantes et sérialisation.
- `NEVERC_INTERFACE_IR_BUILDER` — construction et mutation transactionnelles de
  l'IR.
- `NEVERC_INTERFACE_IR_ANALYSIS` — analyses intégrées et définies par un plugin.
- `NEVERC_INTERFACE_IR_PASS` — passes Module, CGSCC, Function et Loop.
- `NEVERC_INTERFACE_IR_GEN` — remplacement de la génération d'IR à partir d'une
  SemanticUnit.
- `NEVERC_INTERFACE_IR_OPTIMIZATION` — remplacement complet du pipeline
  d'optimisation.

Demandez toujours le couple majeur/mineur de l'en-tête et vérifiez que le
`StructSize` renvoyé atteint le dernier pointeur de fonction utilisé par le
plugin. Un hôte plus récent peut ajouter des champs ; un plugin doit ignorer les
extrémités inconnues.

## Poignées et propriété

Les poignées IR sont des paires opaques `{Owner, Value}` limitées à une tâche.
L'hôte possède tous les objets qu'elles référencent.

- Ne conservez jamais une poignée de portée tâche après la fin de son rappel ou
  de sa tâche.
- N'utilisez jamais une poignée dans une autre session ou une autre tâche.
- Un remplacement validé invalide les poignées des objets remplacés.
- Une mutation abandonnée rend périmées les poignées qu'elle a créées.
- Les API signalent `NEVERC_STATUS_STALE_HANDLE`, `WRONG_OWNER` ou `WRONG_TYPE`
  au lieu d'exposer un pointeur LLVM.

Les chaînes et vues d'octets renvoyées par les interrogations sont empruntées,
sauf si une API renvoie explicitement un tampon libérable.

## Lire l'IR

`NevercIRCoreAPI` fournit :

- l'identifiant de module, le triplet, la disposition des données et
  l'assembleur en ligne ;
- des curseurs de valeurs stables pour les fonctions, globales, blocs,
  instructions, utilisations et opérandes ;
- des identifiants stables de types et d'opcodes ;
- les propriétés des fonctions, globales, instructions, métadonnées et attributs ;
- les constantes entières, flottantes, agrégées, nulles, poison et undef ;
- l'export/import de bitcode et des artefacts de module vérifiés.

Les curseurs de collection sont bornés : passez une capacité de sortie et répétez
la collecte jusqu'à ce que le nombre renvoyé soit nul.

## Mutation transactionnelle

Toute mutation structurelle passe par `NevercIRBuilderAPI` :

1. Commencer une mutation de module ou de fonction.
2. Créer un constructeur lié à cette mutation.
3. Définir le point d'insertion et construire instructions, fonctions ou blocs.
4. Valider la mutation.
5. Détruire les constructeurs et la poignée de mutation.

La validation vérifie l'IR candidate et la publie atomiquement. En cas d'échec du
vérificateur, l'hôte annule la mutation et conserve le module précédent.
`AbortMutation` annule toujours les changements en attente.

Ne revendiquez pas `NEVERC_IR_PRESERVE_ALL` après avoir modifié l'IR.
L'adaptateur de passe contrôle la génération du module et rejette une déclaration
de préservation incohérente.

## Niveaux de passes et phases

`NevercIRPassDescriptor.Level` prend en charge :

- `NEVERC_IR_PASS_LEVEL_MODULE`
- `NEVERC_IR_PASS_LEVEL_CGSCC`
- `NEVERC_IR_PASS_LEVEL_FUNCTION`
- `NEVERC_IR_PASS_LEVEL_LOOP`

Les phases d'insertion stables sont `PRE_OPT`, `PIPELINE_START`,
`OPTIMIZER_LAST`, `POST_OPT` et `PRE_CODEGEN`. L'invocation ne contient que les
poignées valides pour son niveau. Les passes de fonction et de boucle peuvent
s'exécuter en parallèle : l'état mutable du plugin doit donc respecter le contrat
de concurrence déclaré.

L'hôte exécute toujours le vérificateur IR scellé final. Un plugin ne peut ni le
remplacer, ni l'intercepter, ni le sauter.

## Analyses

Les identifiants d'analyses intégrées couvrent le graphe d'appels, l'arbre de
domination, l'arbre de post-domination, les informations de boucles, l'évolution
scalaire, MemorySSA et l'analyse d'alias.

Les analyses de plugin déclarent leurs dépendances et leurs rappels de cycle de
vie. Les résultats sont mis en cache par invocation et invalidés selon le
résultat de préservation de la passe. Les cycles de dépendances récursifs et les
mutations depuis un rappel d'analyse sont rejetés.

## Fournisseurs complets

Un fournisseur de génération d'IR peut remplacer l'abaissement intégré et publier
un artefact de module vérifié. Un fournisseur d'optimisation peut remplacer tout
le pipeline d'optimisation intégré. Dans les deux cas :

- consommer une entrée de phase explicite ;
- publier via une API de l'hôte plutôt que de renvoyer un pointeur LLVM ;
- vérifier la compatibilité de la cible et la validité du module ;
- conserver atomiquement l'ancien module si la publication échoue.

Le vérificateur final reste obligatoire après un fournisseur d'optimisation.

## Exemple minimal

`pluginsdk/examples/FunctionPass.c` est une passe de fonction en lecture seule.
`pluginsdk/examples/ExamplePlugin.c` montre l'énumération d'un module, et
`pluginsdk/examples/CustomCallConvPlugin.c` illustre les attributs et les
propriétés de site d'appel.

Construire et charger un exemple :

```sh
cmake --build build-neverc --target neverc-plugin-example-function-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

Utilisez le suffixe de module produit par CMake pour la plateforme.

## Règles d'échec

Renvoyez un `NevercStatus` depuis chaque rappel. Les échecs de plugin deviennent
des diagnostics structurés ; ne laissez pas d'exception traverser la frontière C.
Initialisez chaque en-tête de table de sortie et chaque champ réservé, et
renvoyez `INVALID_ARGUMENT` pour un pointeur obligatoire manquant.

Voir `PluginIR.h`, `PluginPhaseSchema.h` et `coverage.json` pour les déclarations
normatives de l'ABI, les politiques de phases et les preuves de test.
