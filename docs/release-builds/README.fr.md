**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Index de la documentation](../README.fr.md) · [← Projet NeverC](../../README.md)

# Binaires de publication et `--strip`

Utilisez `--strip` pour produire un exécutable ou une bibliothèque partagée à
distribuer. Son alias court est `-s` ; les deux formes sont identiques.

## Démarrage rapide

```bash
neverc -O2 --strip app.c -o app
neverc -O2 -s app.c -o app
```

NeverC effectue le dépouillement dans son éditeur de liens intégré. Il ne lance
pas de `llvm-strip` externe ; la même commande fonctionne donc pour les sorties
ELF, Mach-O et PE/COFF en compilation croisée.

Ne confondez pas cette option CLI avec l'option de paquetage CMake
`NEVERC_STRIP_BINARY` : celle-ci ne post-traite que l'exécutable du compilateur
`neverc` et peut lancer un outil strip externe. Elle n'affecte pas les
programmes compilés par NeverC.

## Politique de débogage et de symboles

| Invocation | Informations de débogage source | Noms de symboles statiques ordinaires | `.dSYM` Darwin |
|------------|----------------------------------|---------------------------------------|----------------|
| Par défaut (sans `-g`) | Non générées | Peuvent rester ; le défaut exact dépend du format | Non généré |
| `-g` | Générées | Conservés | Généré par un lien Darwin normal |
| `--strip` | Supprimées si présentes | Les noms non requis à l'exécution sont supprimés | Non généré |
| `-g --strip` | La politique de dépouillement prévaut ; absentes de l'image livrée | Les noms non requis à l'exécution sont supprimés | Supprimé |

Sans `-g`, le frontal ne génère aucune information de débogage au niveau source.
Cela ne signifie **pas** que la sortie est entièrement dépouillée : ELF et
Mach-O peuvent encore contenir des noms ordinaires, tandis que PE n'a
normalement pas de table de symboles COFF statique sauf demande du débogage.
Auto-LTO peut retirer certains noms locaux, sans garantir un strip-all.

`-g` passe de l'absence de débogage source à sa génération ; il n'ajoute pas
« davantage » d'informations à un débogage produit par défaut. Les données de
déroulage comme `.eh_frame` pour ELF/Mach-O ou `.pdata`/`.xdata` pour PE sont
des métadonnées d'exécution, pas du DWARF source, et peuvent rester.

## Implémentation et comportement par format

Le pilote convertit `--strip` en une politique de lien fortement typée et la
transmet aux trois backends. Chacun l'applique tout en comprenant le format et
préserve les noms et enregistrements nécessaires au chargeur ou à l'ABI dynamique.

| Format | Supprimé | Conservé si nécessaire |
|--------|----------|------------------------|
| ELF | Données `.debug*` et tables ordinaires de symboles/chaînes statiques | Imports/exports dynamiques, relocalisations et métadonnées du chargeur, déroulage |
| Mach-O | Cartes de débogage/STABS, entrées locales/globales non requises à l'exécution et génération du `.dSYM` associé | Données de liaison/import, noms d'ABI exportés, export trie, symboles référencés à l'exécution |
| PE/COFF | Sections DWARF intégrées et table statique COFF de symboles/chaînes si présente | Imports/exports PE, tables de déroulage, configuration de chargement et métadonnées du chargeur |

## Portée et priorité

- `--strip` prend en charge les exécutables et bibliothèques partagées liés.
- NeverC le rejette avec `-c`, `-r`, `--emit-static-lib` ou `-fdyncode`, au lieu
  de produire silencieusement un artefact intermédiaire non dépouillé.
- La politique de dépouillement prévaut sur `-g` et les options de débogage backend.
- Le pipeline Auto-LTO par défaut et `-fno-lto` sont tous deux couverts.
- Les noms d'import/export indispensables à l'ABI dynamique restent présents.

## Limite de sécurité

Le dépouillement retire des noms et métadonnées précieux et augmente le coût de
l'analyse, mais ce n'est **pas** de l'obscurcissement et il ne rend pas le code
machine impossible à désassembler. Un binaire correctement dépouillé peut garder :

- les noms dynamiques d'import et d'export requis par le chargeur ;
- les littéraux, tables de réflexion ou métadonnées de l'application ;
- les enregistrements de déroulage, relocalisation, signature et chargement ;
- le code machine et son flot de contrôle observable.

`--strip` ne régit que l'image finale. Il ne supprime pas les artefacts demandés
séparément, comme les cartes de liens, les rapports d'optimisation ou les
sorties de `-save-temps` ; vérifiez le répertoire de publication et ne diffusez
pas ces fichiers annexes.

Ajoutez séparément chiffrement de chaînes, obscurcissement et anti-altération si
nécessaire, et n'intégrez jamais un secret devant rester confidentiel au client.

## Vérification d'un artefact

Inspectez les artefacts de publication en CI avec les outils objet de LLVM.
Adaptez les commandes au format et autorisez explicitement les noms ABI requis.

```bash
llvm-readobj --sections --symbols --dyn-symbols app
llvm-dwarfdump app
strings app | grep neverc_private_release_symbol
test ! -e app.dSYM
```

Un artefact dépouillé ne doit contenir ni section de débogage source ni nom de
symbole statique privé. Les noms dynamiques et métadonnées d'exécution requis
sont attendus et ne constituent pas un échec.
