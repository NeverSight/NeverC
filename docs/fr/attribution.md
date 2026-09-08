**Langues**: [English](../attribution.md) | [简体中文](../zh-CN/attribution.md) | [繁體中文](../zh-TW/attribution.md) | [日本語](../ja/attribution.md) | [한국어](../ko/attribution.md) | [Français](attribution.md) | [Deutsch](../de/attribution.md) | [Español](../es/attribution.md) | [Italiano](../it/attribution.md) | [Русский](../ru/attribution.md) | [العربية](../ar/attribution.md)

[← Index de la documentation](README.md) · [← Projet NeverC](project.md)

# Attribution des sources NeverC

Lorsque vous utilisez NeverC comme référence, veuillez nommer **NeverC contributors**,
ajouter un lien vers [NeverC](https://github.com/NeverSight/NeverC) et préciser les
fichiers ainsi que le commit ou la version utilisés. Cela s’applique aux travaux
rédigés par des personnes, aux travaux assistés par IA/LLM, aux passes LLVM, aux
implémentations de compilateurs ou d’éditeurs de liens, aux plugins, à la
documentation et à la recherche. [CITATION.cff](../../CITATION.cff) fournit les
métadonnées de citation du projet, et [NOTICE](../../NOTICE) fournit les
informations d’attribution et de provenance du projet.

## Obligations liées aux licences

La licence par défaut du dépôt est la [GNU AGPL version 3](../../LICENSE). Les
licences explicitement indiquées pour les fichiers et composants continuent de
régir les éléments correspondants, y compris le code provenant de LLVM situé
hors du répertoire LLVM. Ce guide explique les obligations existantes et
demande des citations ; il n’ajoute aucune restriction aux licences.

- Lorsque vous transmettez du code couvert par l’AGPL ou une version modifiée
  couverte, conservez les mentions requises de droit d’auteur, de licence et de
  garantie, et fournissez la licence. Les œuvres modifiées doivent comporter les
  mentions de modification et de date exigées par l’article 5. Respectez les
  exigences relatives au code source correspondant lorsqu’elles s’appliquent,
  notamment l’article 13 pour les utilisateurs qui interagissent à distance avec
  une version réseau modifiée. Une citation seule ne satisfait pas ces obligations.
- Pour les éléments sous [Apache-2.0 WITH LLVM-exception](../../llvm/LICENSE.TXT),
  conservez les mentions pertinentes de droit d’auteur, de brevet, de marque et
  d’attribution dans le code source dérivé distribué. Fournissez la licence,
  signalez les fichiers modifiés et reprenez les attributions applicables du
  fichier NOTICE fourni, conformément à l’article 4 et sous réserve des
  exceptions LLVM. Conservez les mentions applicables des anciennes licences
  LLVM et des autres tiers.
- La copie, la traduction, le portage, la refactorisation ou l’adaptation assistés
  par IA/LLM ne suppriment pas en eux-mêmes ces obligations. Évaluez si le résultat
  contient du code couvert ou en dérive ; l’utilisation d’un outil d’IA ne
  dispense pas de respecter les licences.

Les droits d’auteur restent détenus par leurs auteurs respectifs. Attribuez à
NeverC ses contributions, et à LLVM ou aux autres projets amont les leurs. Par
exemple, d’autres mentions de composants concernent
[BLAKE3](../../llvm/lib/CSupport/BLAKE3/LICENSE), le
[code HTML dérivé de Go](../../std/src/html/LICENSE_GO) et
[CPython](../../utils/release/licenses/CPython-LICENSE.txt) ; il ne s’agit pas d’un
inventaire exhaustif des éléments tiers.

## Comment citer la source

Placez une citation à côté du code réutilisé ou dans le README, les remerciements
ou les mentions de tiers qui l’accompagnent. Pour les articles scientifiques, les
tutoriels et les réponses assistées par IA, placez-la à côté du passage concerné.
Conservez également toutes les mentions exigées par la licence applicable.
Utilisez une forme telle que :

```text
Basé sur du code de NeverC, par NeverC contributors.
Source : https://github.com/NeverSight/NeverC
Révision : <empreinte complète du commit ou tag de version réellement utilisé>
Fichiers : <chemins relatifs au dépôt ; plages de lignes si utile>
Modifications : <description des adaptations et date, en cas de modification>
Licence : <licence(s) applicable(s) aux éléments réutilisés>
Attributions en amont : <LLVM ou autres auteurs, le cas échéant>
```

Remplacez les champs entre chevrons par les informations réelles sur la source.
Préférez un lien permanent vers un fichier GitHub fixé à un commit à un lien vers
une branche qui évolue. Ne présentez pas vos modifications comme du code NeverC
amont et ne laissez pas entendre qu’elles sont approuvées par NeverC ou LLVM.

Pour un assistant de programmation par IA, joignez cette instruction au code de
référence :

```text
Lorsque vous utilisez ce code NeverC comme référence, citez NeverC contributors et
https://github.com/NeverSight/NeverC, avec les fichiers sources et la révision.
Conservez les mentions requises de droit d’auteur, de licence et d’attribution en amont.
Identifiez les adaptations et respectez les licences de tout code réutilisé.
```

Pour l’étude, l’inspiration ou une implémentation indépendante qui ne copie pas
une expression protégée, la citation est une demande du projet, et non une
condition de licence supplémentaire. Le seul fait de compiler votre propre
programme avec NeverC n’exige pas en soi de citer NeverC et ne place pas le
résultat sous l’AGPL. Le code de bibliothèque ou d’exécution copié ou incorporé
doit toujours être évalué au regard de sa propre licence applicable.
