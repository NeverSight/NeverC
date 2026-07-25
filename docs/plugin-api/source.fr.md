**Langues** : [English](source.md) | [简体中文](source.zh-CN.md) | [繁體中文](source.zh-TW.md) | [日本語](source.ja.md) | [한국어](source.ko.md) | [Français](source.fr.md) | [Deutsch](source.de.md) | [Español](source.es.md) | [Italiano](source.it.md) | [Русский](source.ru.md) | [العربية](source.ar.md)

# API de plugin Source et E/S

La première ABI publique de plugin expose les entrées source, les fichiers
virtuels, les dépendances et les sorties du compilateur via `PluginSource.h`.
Tous les chemins sont des chemins VFS normalisés et toutes les poignées sont
limitées à la tâche `TranslationUnit` courante.

## Phases source

Le pipeline source stable est le suivant :

1. `neverc.source.resolve_input` valide et normalise l'entrée demandée.
2. `neverc.source.open` l'ouvre via le VFS composé hôte/plugin.
3. `neverc.source.after_open` publie un événement en lecture seule pour le
   `SourceUnit` vérifié.

`resolve_input` est observable et interceptable ; `open` est en outre
remplaçable. L'hôte vérifie chaque remplacement avant de le publier en tant que
`SourceUnit`. Un plugin ne peut pas remplacer `after_open`.

## Fournisseurs VFS

Interrogez `NevercIOAPI` pendant l'enregistrement du plugin et appelez
`RegisterVFSProvider`. Un fournisseur répond d'abord à `MatchesPath`, puis
implémente les opérations dont il a la charge. Renvoyer
`NEVERC_VFS_RESULT_NOT_HANDLED` délègue au fournisseur suivant ; renvoyer
`HANDLED` fait d'un statut ou d'un contenu malformé une erreur fatale plutôt
qu'un repli silencieux.

Les tampons renvoyés par un fournisseur ne sont empruntés que pour la durée du
rappel. NeverC copie les octets acceptés dans un stockage appartenant à la
tâche. Un fournisseur doit déclarer si son résultat est déterministe et
cacheable.

L'exemple compilable
[`VirtualHeaderPlugin.c`](../../pluginsdk/examples/VirtualHeaderPlugin.c)
fournit un en-tête en mémoire sans contourner le VFS de l'hôte.

## Puits de sortie et dépendances

Les sorties fichier et mémoire utilisent le même puits transactionnel :

- écrire dans un candidat ;
- appeler finish pour le rendre éligible à la vérification ;
- laisser la barrière scellée de l'hôte le vérifier ;
- valider atomiquement si la tâche réussit, ou abandonner en cas d'erreur ou
  d'annulation.

Un plugin ne publie jamais en écrivant directement dans le chemin de
destination. Les destinations en flux qui ne peuvent pas être annulées rejettent
les transformations exigeant un candidat atomique. Les enregistrements de
dépendance utilisent des identités VFS normalisées, de sorte que les fichiers
natifs et ceux fournis par un plugin partagent la même provenance et la même
sémantique de cache.

## Règles de sûreté

- Ne conservez pas les poignées source, fichier, tampon, puits ou tâche après le
  rappel.
- Traitez `NevercStringView` et `NevercByteView` comme des vues délimitées par
  une longueur.
- Utilisez l'allocateur de l'hôte lorsque des données doivent survivre au rappel.
- N'utilisez pas les API de système de fichiers de l'hôte derrière le contrat
  VFS.
- Vérifiez l'annulation avant tout travail coûteux d'un fournisseur.
