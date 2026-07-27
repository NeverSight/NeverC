**Langues** : [English](dyncode.md) | [简体中文](dyncode.zh-CN.md) | [繁體中文](dyncode.zh-TW.md) | [日本語](dyncode.ja.md) | [한국어](dyncode.ko.md) | [Français](dyncode.fr.md) | [Deutsch](dyncode.de.md) | [Español](dyncode.es.md) | [Italiano](dyncode.it.md) | [Русский](dyncode.ru.md) | [العربية](dyncode.ar.md)

[← ABI de plugin NeverC](README.fr.md)

# Plugins DynCode

`-fdyncode` compile une unité de traduction en une image plate et indépendante
de la position (`.bin`) dont le code ne comporte aucune relocation ni section
de données. Elle cible arm64/x86_64 sous macOS, Linux, Android et Windows, au
niveau d'exécution utilisateur ou noyau. Les plugins observent, interceptent ou
remplacent les phases typées qui transforment le C en cette image, au travers
du même ABI purement C que les autres domaines : aucun objet C++ de LLVM,
aucun type STL, aucune exception, et aucun pointeur hôte dont la durée de vie
ne serait pas énoncée par une table d'API.

## Interfaces

```c
#include "neverc/Plugin/PluginDynCode.h"
```

| Interface | Table | Emplacements | Rôle |
|---|---|--:|---|
| `NEVERC_INTERFACE_DYNCODE_{HIGH,LOW}` | `NevercDynCodeAPI` | 16 | Lire la requête, l'image, le rapport et les tables de sections/symboles/relocations/références externes |
| `NEVERC_INTERFACE_DYNCODE_REGISTRAR_{HIGH,LOW}` | `NevercDynCodeRegistrarAPI` | 5 | `RegisterTarget`, `RegisterImportProvider`, `RegisterExtractor`, `RegisterCharsetEncoder`, `RegisterBinaryVerifier` |
| `NEVERC_INTERFACE_DYNCODE_PHASE_{HIGH,LOW}` | `NevercDynCodePhaseAPI` | 4 | `GetPhaseInfo`, `GetRequest`, `GetImage`, `GetReport` |

Toutes trois sont `NEVERC_INTERFACE_STABLE` en majeure 1. Depuis l'intérieur
d'un rappel de phase, `NevercDynCodePhaseAPI` est le point d'entrée : elle
convertit la frame en handles que consomme l'autre table :

```c
NevercDynCodeRequestHandle Request;
Phase->GetRequest(Phase->Context, Frame, Frame->Input, &Request);

NevercDynCodeRequestInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info), NEVERC_DYNCODE_API_MAJOR,
                                     NEVERC_DYNCODE_API_MINOR, 0};
DynCode->GetRequestInfo(DynCode->Context, Task, Request, &Info);
```

Les quatre familles de tables — tables de sections, tables de symboles,
relocations et références externes — se parcourent toutes avec le même triplet
first/next/info, par exemple `GetFirstRelocation`, `GetNextRelocation`,
`GetRelocationInfo`. C'est ainsi qu'un plugin lit les décisions prises par
l'extraction sans analyser le JSON du rapport.

## DynCode est un produit de compilation, pas une étape après `main()`

`-fdyncode` est une Action/un Job ordinaire du DAG du pilote. Le job de
compilation publie un `ObjectGraph` vérifié en mémoire ; un job
`-dyncode-extract` consomme ce graphe et écrit l'image `-o` de l'utilisateur.
`-###`, l'affichage des phases et le graphe de jobs montrent tous le job
d'extraction, si bien qu'un plugin n'a jamais à reconstruire un argv réécrit
pour découvrir le mode. La requête gelée est partagée localement à la tâche
avec la génération de code en cours de processus ; il n'y a pas de
`getCurrentDynCodeOptions()`, pas d'indicateur de mode global au processus, et
pas d'aller-retour par objet temporaire.

Exactement une unité de traduction est abaissée en une image. Les entrées
multiples, `-c/-S/-E` et les triplets non pris en charge sont rejetés d'emblée
avec des diagnostics stables.

## Niveaux de compatibilité

Les identifiants de phase, les identifiants d'artefact, les conteneurs de
requête/rapport/image et les contrats de rappel relèvent de l'ABI STABLE de la
première version. Les genres de relocation propres à une cible et les schémas
de sections/symboles du format objet sont LOCKSTEP : comparez l'identifiant et
l'empreinte du schéma de cible avant de les consommer. NeverC rejette un schéma
non concordant avant d'invoquer un fournisseur.

## La requête gelée

Au démarrage du job, le pilote normalise la ligne de commande en un
`DynCodeRequest` immuable et le gèle. Les tâches filles empruntent l'instantané
sans jamais le modifier. La requête porte la clé de cible et le format objet,
le niveau d'exécution (utilisateur/noyau), la politique de point d'entrée
(symbole explicite, liste de candidats par défaut, exigence d'entrée à zéro),
la politique PIC/sections, la politique de références externes, l'ensemble ou
le profil d'octets interdits ainsi que l'indicateur de réécriture,
l'identifiant du fournisseur de jeu de caractères, et enfin la longueur
maximale, l'alignement et l'octet de remplissage.

## Le graphe de phases typé

DynCode est un graphe figé de 34 phases. Trente transitions ordinaires sont
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE` ; quatre sont
`OBSERVABLE | SEALED_HOST_GATE`. Les portes scellées sont la vérification
finale de l'IR, la vérification finale de la MIR, la vérification de l'image et
la validation finale (commit). Un plugin peut observer n'importe quelle phase,
envelopper une transition remplaçable avec un intercepteur, ou remplacer son
fournisseur purement et simplement ; il ne peut jamais remplacer, sauter ni
contourner une porte scellée, et il ne peut pas exprimer une transformation
désactivée par un rappel non appelé — une transformation désactivée exécute un
fournisseur no-op explicite dont le vérificateur de l'hôte prouve toujours la
sortie équivalente.

Les phases, dans l'ordre, sont :

1. le gel de la requête ;
2. les transformations IR — prepare, abaissement des branchements indirects,
   abaissement des intrinsèques mémoire (avant le tas), abaissement du runtime
   de chaînes, arène de tas, abaissement des intrinsèques mémoire (après le
   tas), `compiler_rt` (pre), abaissement des imports syscall/PEB/noyau,
   `data_to_text` (pre), optimisation en ligne, `compiler_rt` (post),
   finalisation des chaînes, `data_to_text` (post), stackify, tout-`blr`,
   `compiler_rt` (final), puis la vérification finale scellée de l'IR ;
3. la transformation MIR prepare et la vérification finale scellée de la MIR ;
4. l'import d'objet — lier l'`ObjectGraph` vérifié à la tâche ;
5. l'extraction — planifier, disposer, reloger et construire l'image candidate ;
6. les phases binaires bornées — post-extract, réécriture des octets interdits,
   encodage de jeu de caractères, taille/alignement/remplissage, et pre-verify ;
7. la vérification scellée de l'image ;
8. la validation finale scellée.

La source normative des identifiants, politiques, niveaux de stabilité et
portes est [`Schema/PhaseSchema.json`] ; le contrat de couverture exécutable est
[`coverage.json`].

## Les transformations intégrées sont aussi des fournisseurs

Chaque passe IR/MIR intégrée est enveloppée en fournisseur typé ; l'objet de
passe LLVM n'est jamais exposé au travers de l'ABI C. Remplacer une phase
signifie que le fournisseur intégré ne s'exécute pas — le test qui passe prouve
le comportement ou la trace, et non le simple succès d'un enregistrement. Les
phases `mem_intrin`, `compiler_rt` et `data_to_text` apparaissent à plusieurs
positions ; chaque position est un identifiant de phase distinct doté de sa
propre preuve, si bien qu'une réexécution est idempotente et ne repose jamais
sur un état de passe caché.

## ObjectGraph est la seule entrée en objet ordinaire

L'extraction consomme exactement un `ObjectGraph` vérifié, produit par la route
de génération de code de la cible. `dyncode.object.import` lie ce graphe et
contrôle la clé de cible ainsi que la provenance ; elle ne relit jamais
d'octets depuis le disque et n'exécute pas une seconde analyse d'objet. Un
format objet personnalisé entre dans DynCode dès qu'il peut être lu en
`ObjectGraph` et dispose de fournisseurs de relocation et de cible
correspondants. Les objets multiples et les ensembles de graphes LTO sont
rejetés au gel avec un `CAPABILITY_UNAVAILABLE` stable.

## Références externes et abaissement des imports

L'ensemble des externes autorisés de la requête signifie seulement « un
fournisseur peut traiter ceci » ; il ne permet jamais qu'une relocation non
résolue survive jusque dans l'image plate. Chaque référence externe doit finir
d'une de ces façons : éliminée en IR/MIR, résolue vers un symbole présent dans
l'image, convertie en un contrat de résolveur d'exécution déclaré et accepté
par le vérificateur, ou bien erreur fatale. Le stub d'appel système, l'import
PEB et l'import noyau sont les trois `ImportProvider` intégrés ; chacun déclare
son sélecteur cible/niveau/symbole et le contrat d'ABI qu'il produit. Un plugin
peut ajouter un `ImportProvider`, mais il doit renvoyer la provenance du
remplacement, le changement d'ABI d'entrée, les paramètres du résolveur et les
références résiduelles.

## Image, rapport et éditions d'octets bornées

L'extraction produit une `DynCodeImage` et un `DynCodeReport`. L'image est un
constructeur d'octets borné, complété par l'offset/le symbole d'entrée, les
tables de sortie des sections et symboles sources, le sort des relocations et
les enregistrements de contrats externes/d'exécution. Chaque édition d'octet
passe par l'API vérifiée read/write/insert/append/resize du constructeur ; il
n'existe pas de `uint8_t **`. Une édition met à jour la génération de l'image
et invalide toute preuve de relocation/PIC/entrée qui recouvre la plage
modifiée.

Le rapport est un produit d'audit immuable et déterministe : empreintes de la
requête/route/entrée/sortie, journal des fournisseurs par phase, sections
retenues et rejetées avec la raison, choix du point d'entrée, relocations
corrigées/rejetées/converties en contrat d'exécution, externes restants,
taille/alignement/remplissage, balayage des octets interdits, et liste de
contrôle du vérificateur. `-fdyncode-report=<path>` en écrit le JSON canonique ;
les diagnostics détaillés sont rendus à partir du même rapport plutôt que d'un
second jeu de compteurs.

La chaîne de réécriture des octets interdits s'exécute dans un ordre
topologique gelé et chaque étape renvoie un enregistrement de changement.
L'encodeur de jeu de caractères est sélectionné par identifiant stable exact et
renvoie un stub de décodeur, la charge utile encodée, la mise à jour du point
d'entrée et une preuve de cible ; un identifiant inconnu ou ambigu est une
erreur fatale. Désactiver la réécriture sélectionne une étape no-op explicite —
l'audit final s'exécute malgré tout.

## Vérificateur final et cadence post-finalisation

Toutes les phases inscriptibles s'achèvent avant le vérificateur final scellé.
Le vérificateur contrôle qu'aucune relocation/référence externe non traitée ne
subsiste, qu'aucune section interdite de données/TLS/déroulage/débogage/
métadonnées n'est présente, que le point d'entrée existe, qu'il est correctement
aligné et (lorsque c'est exigé) situé à l'offset zéro, que chaque site de
relocation tient dans les bornes avec une preuve PIC concordante pour les octets
courants de l'image, que les tables de sections/symboles ne se recouvrent pas,
que les règles de longueur/alignement/remplissage sont respectées, et que les
octets finaux — décodeur, en-tête et remplissage compris — ne contiennent aucun
octet interdit. Tout échec renvoie un diagnostic structuré et écarte l'ensemble
du paquet de sortie.

Il n'existe aucun point d'ancrage inscriptible après l'audit. Si une
transformation d'octets touche une plage exécutable, la route gelée doit fournir
une capacité de vérificateur binaire concordante que l'hôte appelle pour
réémettre la preuve PIC sur l'image finale et immuable.

## Options du pilote

`-fdyncode` active le mode. `-fdyncode-entry=` choisit le symbole d'entrée.
`-fdyncode-bad-bytes=` / `-fdyncode-bad-byte-profile=` définissent les octets
interdits, `-fdyncode-bad-byte-rewrite` (activé par défaut) sélectionne la
chaîne de réécriture, et `-fdyncode-charset=` sélectionne un encodeur enregistré.
`-fdyncode-max-length=`, `-fdyncode-align=` et `-fdyncode-pad=` bornent la taille
finale. `-fdyncode-keep-obj=` dérive l'objet relogeable intermédiaire et
`-fdyncode-report=` écrit le rapport d'audit. `-mdyncode-context=user|kernel`
sélectionne le niveau d'exécution.

## Règles de concurrence et d'échec

- Conservez l'état mutable dans les portées processus/session/tâche fournies par
  l'hôte ; n'utilisez jamais un singleton de plugin courant ou d'options
  courantes.
- Ne mettez pas en cache les handles de tâche ni les vues empruntées après le
  retour d'un rappel.
- N'invoquez la continuation d'un intercepteur qu'une seule fois au plus, sur le
  thread du rappel.
- Renvoyez le `NevercStatus` d'origine ; un `REPLACE` déclaré qui échoue ne
  retombe pas silencieusement sur le fournisseur intégré.
- Déclarez les modèles de concurrence et de réentrance les plus étroits qui
  soient exacts.

Voir [`PluginDynCode.h`] pour les déclarations normatives,
[`pluginsdk/examples/DynCodeTracePlugin.c`] pour un traceur de phases en
lecture seule et [`pluginsdk/examples/DynCodeEncoderPlugin.c`] pour un encodeur
de jeu de caractères.

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PluginDynCode.h`]: ../../neverc/include/neverc/Plugin/PluginDynCode.h
[`pluginsdk/examples/DynCodeEncoderPlugin.c`]: ../../pluginsdk/examples/DynCodeEncoderPlugin.c
[`pluginsdk/examples/DynCodeTracePlugin.c`]: ../../pluginsdk/examples/DynCodeTracePlugin.c
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
