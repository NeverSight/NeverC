**Langues** : [English](mir.md) | [简体中文](mir.zh-CN.md) | [繁體中文](mir.zh-TW.md) | [日本語](mir.ja.md) | [한국어](mir.ko.md) | [Français](mir.fr.md) | [Deutsch](mir.de.md) | [Español](mir.es.md) | [Italiano](mir.it.md) | [Русский](mir.ru.md) | [العربية](mir.ar.md)

# API MIR des plugins NeverC

La première ABI publique de plugin expose la Machine IR via `PluginMIR.h`. L'API
utilise des identifiants C stables et des poignées opaques ; un plugin ne dépend
ni de la disposition des classes LLVM, ni des valeurs d'énumération, ni de l'ABI
C++.

## Négociation

Interrogez `NEVERC_INTERFACE_MIR` pour `NevercMIRAPI` et
`NEVERC_INTERFACE_MIR_PASS` pour `NevercMIRPassAPI`. Vérifiez la taille de table
renvoyée avant d'utiliser un pointeur de fonction et ignorez les champs ajoutés
par un hôte plus récent.

Le condensé de schéma identifie la correspondance exacte entre les valeurs
stables et l'hôte. `GetEntityInfo`, `GetOperandKindInfo`,
`GetGenericOpcodeInfo` et `GetMachinePropertyInfo` exposent les noms canoniques
et indiquent si une opération nécessite un schéma de cible.

## Modèle stable

Les poignées opaques représentent :

- les fonctions machine et les blocs de base ;
- les instructions machine et les opérandes ;
- les transactions de mutation ;
- les résultats d'analyse ;
- les entrées du pool de constantes, les objets de pile, les tables de saut, les
  opérandes mémoire et les références de cible.

Une poignée appartient à une seule tâche de génération de code. Les entités
effacées, les entités annulées et les résultats d'analyse invalidés par une
mutation deviennent périmés.

Le schéma générique couvre les opcodes indépendants de la cible, les genres
d'opérandes, les propriétés machine, les types bas niveau, les drapeaux
d'instruction, les affectations de registres, les objets de pile, les constantes,
les tables de saut, les formes de pointeur mémoire et les ordres atomiques. Les
opcodes spécifiques à une cible exigent un schéma de cible explicitement négocié.

## Lire la MIR

`NevercMIRAPI` prend en charge :

- les propriétés de fonction machine et le parcours des blocs ;
- l'énumération des prédécesseurs, successeurs, live-in, instructions et
  opérandes ;
- les interrogations d'opcode et de drapeaux d'instruction ;
- toutes les formes publiques d'opérande machine ;
- les informations sur les registres virtuels et physiques ;
- l'état des piles, pools de constantes, tables de saut et opérandes mémoire.

Utilisez les paires comptage/interrogation et des tampons de sortie bornés. Sauf
mention contraire, les vues renvoyées sont empruntées pour le rappel courant.

## Mutation transactionnelle

Les changements de MIR se font sous un bail de mutation :

1. `BeginMutation` pour une fonction machine.
2. Créer, déplacer ou effacer des blocs et des instructions.
3. Ajouter ou mettre à jour des opérandes et des arêtes du CFG.
4. Appliquer les changements de propriétés machine avec la preuve requise.
5. `CommitMutation` ou `AbortMutation`.

La validation effectue un contrôle structurel préalable et la vérification de la
Machine IR. Les opérandes, le CFG, l'usage d'opcodes génériques ou les
affirmations de propriétés invalides sont annulés atomiquement. L'abandon
restaure l'ordre des blocs, les instructions, les opérandes, les arêtes du CFG et
les propriétés machine.

Les changements de propriétés utilisent `NevercMIRPropertyProof`. Une preuve doit
soit invalider une propriété dont les hypothèses ne tiennent plus, soit demander
un contrôle structurel avant de l'établir.

## Passes et phases

`NevercMIRPassDescriptor.Level` prend en charge les adaptateurs MachineModule,
MachineFunction et MachineBasicBlock. Les points d'accroche stables sont :

- après la sélection d'instructions ;
- après la légalisation ;
- avant et après l'ordonnanceur ;
- avant et après l'allocation de registres ;
- après le prologue/épilogue ;
- pre-emit ;
- l'emplacement final réservé aux plugins.

Les passes de fonction peuvent s'exécuter dans des partitions de génération de
code parallèles. Les passes au niveau module s'exécutent à des barrières de
pipeline sérialisées. Les déclarations de concurrence et de réentrance du plugin
restent applicables.

Chaque pipeline de génération de code se termine par un `MachineVerifier`
appartenant à l'hôte, après l'emplacement final des plugins. C'est une barrière
scellée qu'un plugin ne peut pas désactiver.

## Analyses

La table d'analyses expose les variables vivantes, les intervalles de vie, les
index d'emplacements, l'arbre de domination, les informations de boucles et la
pression sur les registres. La disponibilité dépend du point d'accroche choisi,
car certaines analyses LLVM n'existent pas avant ou après leur étape native de
pipeline.

Déclarez les analyses requises et préservées dans le descripteur de passe. Une
mutation validée invalide les poignées de résultats concernées. Affirmer
« tout préserver » après une mutation est rejeté.

## Exemple minimal

`pluginsdk/examples/MachinePass.c` enregistre une passe de fonction machine en
lecture seule au point d'accroche stable pre-emit.

```sh
cmake --build build-neverc --target neverc-plugin-example-machine-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/MachinePass.so \
  -O2 -fno-lto -c input.c -o input.o
```

Utilisez le suffixe de module produit par CMake pour la plateforme.

## Exigences de sûreté

- Ne conservez pas de poignées de tâche, de poignées MIR ni de vues empruntées
  après un rappel.
- Ne fabriquez pas de valeurs de poignée ni de numéros d'opcode LLVM.
- Ne mutez pas en dehors d'un bail.
- Initialisez les en-têtes de table et le stockage réservé.
- Renvoyez des statuts à travers la frontière C ; ne laissez jamais une exception
  C++ la franchir.

Voir `PluginMIR.h`, `MIRSchema.json`, `PluginPhaseSchema.h` et `coverage.json`
pour les déclarations normatives et les preuves de couverture.
