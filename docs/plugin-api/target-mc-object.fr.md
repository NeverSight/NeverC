**Langues** : [English](target-mc-object.md) | [简体中文](target-mc-object.zh-CN.md) | [繁體中文](target-mc-object.zh-TW.md) | [日本語](target-mc-object.ja.md) | [한국어](target-mc-object.ko.md) | [Français](target-mc-object.fr.md) | [Deutsch](target-mc-object.de.md) | [Español](target-mc-object.es.md) | [Italiano](target-mc-object.it.md) | [Русский](target-mc-object.ru.md) | [العربية](target-mc-object.ar.md)

# Plugins de cible, MC, assembleur et objets

L'ABI de plugin de la première version de NeverC permet à un plugin en C de
décrire une cible, de remplacer des routes de génération de code, d'observer
l'émission de code machine, d'analyser ou d'imprimer de l'assembleur, et de lire
ou d'écrire des fichiers objets. La frontière publique est une ABI C pure : un
plugin ne doit pas échanger d'objets C++ de LLVM, de types STL, d'exceptions, ni
de pointeurs appartenant à l'hôte dont la durée de vie n'est pas indiquée par une
table d'API.

## Niveaux de compatibilité

Les descripteurs indépendants de la cible, les identifiants de phase et
d'artefact, les conteneurs MC, les conteneurs ObjectGraph, les transactions de
sortie et les contrats de rappel relèvent de l'ABI STABLE de la première version.
Les schémas spécifiques à la cible — opcodes, registres, opérandes, fixups,
relocalisations et conventions d'appel — sont LOCKSTEP. Un plugin doit comparer
l'identifiant et le condensé du schéma de cible avant de consommer des valeurs
LOCKSTEP. NeverC rejette les schémas discordants avant d'invoquer le fournisseur.

## Enregistrer une cible et une route de génération de code

Interrogez `NevercTargetAPI` pendant l'enregistrement, enregistrez un ou
plusieurs enregistrements `NevercTargetDescriptor`, puis attachez les descripteurs
de machine cible et les arêtes de génération de code. Une route est sélectionnée à
partir de la clé de cible canonique : identifiant de cible, triplet, CPU,
fonctionnalités, ABI, modèle de relocalisation, modèle de code, format d'objet et
condensé de schéma.

Les routes fines utilisent `IR -> MIR -> MC -> ObjectGraph -> ObjectImage`. Une
arête grossière peut remplacer toute la route `IR -> ObjectImage`. Une sortie
grossière passe malgré tout par le vérificateur de produit obligatoire de l'hôte
et par la validation transactionnelle de sortie ; un fournisseur ne peut
contourner ni l'un ni l'autre.

## Construire et observer le MC

`NevercMCAPI` possède les mutations de `MCUnit` locales à la tâche. Commencez une
mutation, créez sections, fragments, symboles, expressions, instructions et
opérandes, puis validez-la ou abandonnez-la. Les poignées sont limitées à la tâche
et contrôlées par génération.

Le flux d'émission indépendant de la cible expose des événements ordonnés pour les
changements de section, les étiquettes, les instructions, l'alignement, les
attributs de symboles, les CFI, les emplacements de débogage et les données.
`neverc.mc.emission.pre_instruction` est remplaçable ; les autres phases
d'événements sont des points d'observation en lecture seule. Voir
`pluginsdk/examples/MCObserverPlugin.c`.

Les fournisseurs d'encodage, de décodage et de disposition opèrent sur la même clé
de cible et le même condensé de schéma. La disposition prend en charge la
relaxation et émet un condensé de preuve. Toute mutation après la disposition
invalide cette preuve et impose une nouvelle disposition avant l'écriture de
l'objet.

## Remplacer la syntaxe de l'assembleur

Un fournisseur d'analyseur d'assembleur consomme des octets sources et publie un
`MCUnit`. Un imprimeur d'assembleur consomme un `MCUnit` et n'écrit qu'à travers
la transaction de sortie fournie. L'assembleur prétraité (`.S`) passe par le
préprocesseur frontal habituel avant le fournisseur d'analyse ; l'assembleur brut
(`.s`) entre directement dans l'analyseur.

Les fournisseurs mettent d'abord la sortie en attente. La vérification
d'analyse/impression et la barrière de validation de l'hôte s'exécutent avant que
les octets ne deviennent visibles, de sorte qu'un échec ne laisse aucune sortie
partielle.

## Lire, réécrire et écrire des objets

`NevercObjectAPI` représente un fichier relogeable sous la forme d'un ObjectGraph
normalisé : sections, symboles, relocalisations, groupes/COMDAT,
imports/exports, métadonnées TLS, enregistrements de déroulement et
enregistrements de débogage. Les adaptateurs intégrés couvrent ELF, COFF et
Mach-O, et les plugins peuvent enregistrer des formats supplémentaires.

Le pipeline objet est le suivant :

1. sonder et lire les octets dans un ObjectGraph ;
2. exécuter les intercepteurs de graphe `object.pre_write` ;
3. disposer puis exécuter `object.post_layout` (nouvelle disposition après
   mutation) ;
4. écrire une image candidate bornée ;
5. exécuter les intercepteurs binaires `object.post_write` ;
6. exécuter le vérificateur final scellé et la validation atomique de l'hôte.

Les observateurs reçoivent des ponts en lecture seule. Toute mutation tentée
depuis un observateur est rejetée avec `NEVERC_STATUS_POLICY_VIOLATION`. Les
écrivains et les intercepteurs post-écriture n'ont accès qu'au constructeur
transactionnel borné ; un dépassement, un échec de rappel ou un échec de
vérification annule la mise en attente. Voir
`pluginsdk/examples/ObjectRewritePlugin.c`.

## Règles de concurrence et d'échec

- Gardez l'état mutable dans l'état process/session/task fourni par l'hôte.
- Ne mettez pas en cache de poignées de tâche ni de vues empruntées après le
  retour d'un rappel.
- N'invoquez la continuation d'un intercepteur qu'au plus une fois, sur le fil du
  rappel.
- Renvoyez le `NevercStatus` d'origine ; ne publiez pas de produits partiels.
- Déclarez les modes de concurrence et de réentrance les plus étroits qui soient
  véridiques.

Le contrat de couverture exécutable est `docs/plugin-api/coverage.json`. Il
associe chaque phase stable à des tests positifs, négatifs, de remplacement,
d'observateur en lecture seule et de barrière scellée.
