**Langues** : [English](prep.md) | [简体中文](prep.zh-CN.md) | [繁體中文](prep.zh-TW.md) | [日本語](prep.ja.md) | [한국어](prep.ko.md) | [Français](prep.fr.md) | [Deutsch](prep.de.md) | [Español](prep.es.md) | [Italiano](prep.it.md) | [Русский](prep.ru.md) | [العربية](prep.ar.md)

# API de plugin du préprocesseur

`PluginPrep.h` expose des schémas stables de jetons, d'identifiants, de macros,
de pragmas et de flux de jetons, sans laisser fuir les types C++ de NeverC ou de
LLVM. Le schéma généré `Schema/PluginPrepSchema.inc` fait autorité pour les
genres numériques stables, les catégories, les orthographes et la
constructibilité.

## Niveaux d'extension

Un plugin peut intervenir à trois niveaux :

- des événements de préprocesseur en lecture seule pour les inclusions, les
  expansions de macros, les conditionnelles, les pragmas et les transitions de
  fichiers ;
- des intercepteurs typés pour les phases de jeton, d'inclusion, de macro, de
  pragma et d'interrogation de fonctionnalité ;
- un fournisseur `neverc.prep.build_token_stream` complet qui publie un
  `TokenStream` vérifié.

La phase de jeton prend en charge le remplacement, la suppression et l'expansion
bornés. L'hôte fait respecter le budget d'expansion et vérifie l'orthographe, la
position, les drapeaux, le placement de l'EOF et l'appartenance des jetons avant
de publier un remplacement.

## Constructeurs de jetons

Créez des jetons synthétisés avec `CreateTokenBuilder`, définissez exactement une
charge utile de jeton, attribuez une position valide appartenant à la tâche, puis
appelez `TokenBuilderCommit`. Détruisez le constructeur sur tous les chemins. Un
constructeur validé est immuable et une validation échouée ne publie aucun jeton.

Les flux de jetons sont des artefacts de tâche contigus et immuables. Un flux de
remplacement doit contenir exactement un jeton EOF final et ne peut pas dépasser
`NEVERC_PREP_TOKEN_STREAM_MAX_TOKENS`.

## Règles pour les observateurs et les intercepteurs

Les observateurs reçoivent des données d'événement en lecture seule et ne peuvent
pas influer sur le prétraitement. Les intercepteurs suivent le contrat de
continuation commun :

- appeler `InvokeNext` au plus une fois puis renvoyer `CONTINUE` ; ou
- ne pas l'appeler et publier un remplacement vérifié.

Les objets de continuation et toutes les poignées du préprocesseur ne sont
valides que pendant la portée de rappel ou de tâche déclarée. Un fil créé par le
plugin doit être joint avant le retour du rappel s'il touche à ces valeurs.

## Vérification

Après avoir modifié des définitions de jetons, exécutez les contrôles de schéma
généré et de couverture :

```sh
python3 utils/plugin-api/gen-prep-schema.py --check
python3 utils/plugin-api/check-coverage.py docs/plugin-api/coverage.json
```

Avec `NEVERC_ENABLE_PLUGIN_FUZZERS=ON`,
`plugin-prep-token-builder-fuzzer` met à l'épreuve des constructeurs de jetons
malformés, des poignées de tâche, des capacités de sortie et des interrogations
de flux de jetons.
