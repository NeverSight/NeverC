**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Index de la documentation](../README.fr.md) · [← Projet NeverC](../../README.md)

# Binaires de publication et `--strip`

Utilisez `--strip` pour produire un exécutable, une bibliothèque partagée ou un
module noyau Android final à distribuer. Sa forme courte est `-s` ; les deux
formes sont identiques.

## Démarrage rapide

```bash
neverc -O2 --strip app.c -o app
neverc -O2 -s app.c -o app

cd examples/android-kernel-hello
neverc make release
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
| Noyau Android `.ko` (ELF ET_REL) | `.debug*`, `.comment`, entrées locales/non définies inutiles aux relocalisations et noms lisibles des définitions ordinaires conservées | Un `.symtab` lié à `.strtab`, toutes les relocalisations et cibles, noms exacts du chargeur/CFI, imports exacts, noms des sections protégées et métadonnées ABI du module |
| Mach-O | Cartes de débogage/STABS, entrées locales/globales non requises à l'exécution et génération du `.dSYM` associé | Données de liaison/import, noms d'ABI exportés, export trie, symboles référencés à l'exécution |
| PE/COFF | Sections DWARF intégrées et table statique COFF de symboles/chaînes si présente | Imports/exports PE, tables de déroulage, configuration de chargement et métadonnées du chargeur |

## Portée et priorité

- `--strip` prend en charge les exécutables, bibliothèques partagées et
  l'exception stricte du `.ko` Android final décrite ci-dessous.
- NeverC le rejette avec `-c`, un `-r` ordinaire, un `.o` Android intermédiaire,
  `--emit-static-lib` ou `-fdyncode`.
- La politique de dépouillement prévaut sur `-g` et les options de débogage backend.
- Le pipeline Auto-LTO par défaut et `-fno-lto` sont tous deux couverts.
- Les noms d'import/export indispensables à l'ABI dynamique restent présents.

## Modules noyau Android

Un `.ko` final reste un ELF `ET_REL`. Le chargeur de modules Linux exige une
table de symboles, sa table de chaînes, les imports non définis et les
relocalisations ; il rejette donc strip-all. NeverC n'accepte `-r --strip` que
pour une cible Android avec `-fandroid-kernel-driver-mode`, `-r` et un nom de
sortie finissant par `.ko`. Les liens `-r` ordinaires et les `.o` intermédiaires
restent refusés.

`neverc make release` reste la commande recommandée et se développe en
`-O2 --strip`. Ces exemples exigent `neverc make` et refusent un `make` externe,
car la propagation récursive des variables de ligne de commande, la
vérification d'état et le nettoyage sous verrou font partie du contrat de
build. Sans paire `.nvk-build-flags` et `.nvk-build-integrity` vérifiée, `neverc make`
utilise debug par défaut et ne choisit pas release de lui-même. Les fichiers
d'état restent à côté de `MODULE`, même si celui-ci désigne un sous-répertoire.
Les Makefile d'exemple conservent un profil
choisi explicitement afin que `neverc make push`, `neverc make run` et un
`neverc make` sans cible
réutilisent le même artefact. Les exemples acceptant `EXTRA` conservent sa
valeur complète de plusieurs mots dans les builds récursifs et ultérieurs, et
les cibles profile récursives conservent les autres substitutions de ligne de
commande.
`neverc make debug`, `neverc make release` et `neverc make clean` doivent chacun être lancés comme
seule cible ; les combiner avec une autre cible est refusé afin d'éviter les
courses sur les sorties partagées. `neverc make debug` ou un
`neverc make PROFILE=...` explicite ne
remplace ce choix que lorsque le lot module/table/état est publié ; un échec
antérieur à la publication laisse l'artefact et l'état précédents intacts.
`.nvk-build-integrity` lie les SHA-256 du module, de l'identifiant de build et
de l'état `EXTRA` facultatif. Après une publication interrompue, un état absent
ou incohérent est ignoré et force une reconstruction. `neverc make clean` efface
l'état sous le verrou de publication et ramène la build suivante à debug. Avec
un profil release vérifié, un `neverc make` sans cible
reconstruit si la table est absente. Un `neverc make release` explicite reconstruit une
fois, sans condition, le lot module/table/état ; le relancer répare donc aussi un lot
dont l'empreinte ne correspond plus. Sur cette voie finale, NeverC retire les
sections de débogage, `.comment` et les entrées locales/non définies inutiles
aux relocalisations, puis reconstruit `.strtab`.

Après une release réussie, NeverC publie de façon transactionnelle le module
et `<module>.ko.symbols.json` à côté de celui-ci. Les fichiers existants
restent visibles jusqu'à chaque remplacement atomique. Les publications
concurrentes visant le même répertoire de sortie et `neverc make clean` sont sérialisés
via `.neverc-output.lock`. Le nettoyage supprime le lot transactionnellement,
mais conserve intentionnellement ce fichier de verrouillage interne. Les
erreurs antérieures à la publication annulent l'ensemble
du lot ; les erreurs tardives de durabilité conservent un
journal de récupération. Deux entrées de répertoire ne pouvant pas être
remplacées par une seule opération du système de fichiers, l'intégrité de l'état
de build est vérifiée automatiquement ; vérifiez néanmoins toujours
`image_sha256` de la table après un arrêt anormal. La table enregistre les noms `original`
et `release` de chaque symbole conservé dont le nom a changé :

```json
{
  "format": "neverc.android-kernel-symbol-map",
  "version": 2,
  "image_sha256": "…",
  "symbols": [
    {"original": "worker_dispatch", "release": "fn_C000"}
  ]
}
```

Les entrées sont triées par `release`. Les symboles supprimés et les noms
exacts du chargeur, des imports ou du CFI sont omis puisqu'ils ne nécessitent
aucune traduction. Si une build debug ou toute autre build sans strip écrase
le même chemin de sortie, NeverC supprime l'ancienne table. ELF autorise des
octets non UTF-8 dans les noms de symboles ; ces rares noms d'origine sont
stockés en Base64 dans `original` avec
`"original_encoding": "base64"`. Les autres noms d'origine restent lisibles.
NeverC publie ce fichier annexe avec le mode `0600` sur POSIX et une
`Windows ACL` protégée réservée au propriétaire sous Windows ; la publication
échoue si cette restriction ne peut pas être appliquée. Archivez la table comme
artefact de débogage privé, ne la distribuez pas avec le `.ko` et ne l'envoyez
pas sur l'appareil. Avant de traduire un nom de release issu d'un rapport de
plantage, vérifiez la liaison :

```bash
actual="$(python3 -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest())' \
  nvk_hello.ko)" &&
expected="$(jq -er '.image_sha256 | strings | select(test("^[0-9a-f]{64}$"))' \
  nvk_hello.ko.symbols.json)" &&
test "$actual" = "$expected" &&

python3 - nvk_hello.ko.symbols.json fn_C000 <<'PY'
import base64, json, sys
with open(sys.argv[1], encoding="utf-8") as stream:
    entry = next(item for item in json.load(stream)["symbols"]
                 if item["release"] == sys.argv[2])
original = entry["original"]
print(repr(base64.b64decode(original))
      if entry.get("original_encoding") == "base64" else original)
PY
```

Les définitions conservées éligibles reçoivent des noms structurels
déterministes inspirés d'IDA, sans employer ses préfixes réservés :

- `STT_FUNC` devient `fn_HEX` ;
- `STT_OBJECT` devient `obj_HEX` ;
- un `STT_NOTYPE` exécutable devient `code_HEX` ;
- tout autre `STT_NOTYPE` alloué devient `sym_HEX` ;
- `SHN_ABS` devient `abs_HEX` ;
- une définition hors `SHF_ALLOC` devient
  `sym_S<FINAL_SECTION_ORDINAL_HEX>_<OFFSET_HEX>`.

Chaque champ `HEX`, y compris les deux champs de la forme non allouée, est en
majuscules et sans zéro initial superflu. Si plusieurs symboles requièrent la
même graphie, les variantes décimales déterministes `_1`, `_2`, etc. sont
ajoutées.

Ces graphies s'inspirent d'IDA sans occuper son espace de noms factices. Dans
une base IDA 9.4 neuve, les symboles utilisateur ELF `sub_0`, `sub_4` et
`loc_8` s'affichent comme `_sub_0`, `_sub_4` et `_loc_8`, alors que `fn_0`,
`code_8` et `obj_10` restent inchangés. La documentation Hex-Rays de
[`SN_NODUMMY`](https://python.docs.hex-rays.com/ida_name/index.html) confirme
qu'un nom utilisateur commençant par un préfixe factice tel que `sub_` reçoit
un soulignement initial. NeverC n'efface pas volontairement le `st_name` d'une
définition ordinaire pour faire synthétiser `sub_` par IDA : kallsyms des
modules Android/Linux ignore historiquement les entrées sans nom, et un nom
vide supprimerait le contrat sérialisé vérifiable. Les entrées qui doivent déjà
être vides et les symboles de section restent exacts.

ELF permet à plusieurs symboles de partager la même canonical analysis EA.
NeverC conserve ou produit dans `.symtab` l'ensemble complet des alias ; le
modèle de noms par adresse d'IDA 9.4 peut toutefois ne matérialiser qu'un seul
nom principal parmi les symboles à cette adresse. Un alias absent de
l'affichage d'IDA n'est donc pas nécessairement perdu dans l'ELF ; l'ensemble
complet doit être contrôlé avec `llvm-readelf` ou `llvm-nm`.

Pour un symbole alloué, `HEX` est la canonical analysis EA de NeverC,
c'est-à-dire l'adresse effective canonique réservée à l'analyse statique. Depuis
un curseur nul, NeverC parcourt les sections `SHF_ALLOC` finales conservées dans
l'ordre final de la table des sections, aligne le curseur sur
`max(sh_addralign, 1)`, enregistre la base, puis avance de `max(sh_size, 1)` ;
l'EA est cette base plus le `st_value` final. `abs_HEX` utilise le `st_value`
absolu final. Dans la forme non allouée, `FINAL_SECTION_ORDINAL_HEX` est
l'ordinal final de section et `OFFSET_HEX` le `st_value` final dans cette
section. Ces coordonnées ne sont ni un hachage, ni un chiffrement, ni un
décalage de fichier, ni une adresse virtuelle ELF ou une adresse d'exécution du
noyau. Le chargeur et KASLR peuvent placer le module ailleurs à l'exécution.

Restent exactement inchangés :

- chaque import `SHN_UNDEF`, que le chargeur résout par son nom ;
- les symboles définis dans `.modinfo`, `.text.ftrace_trampoline`,
  `.gnu.linkonce.this_module`, `__versions` ou `.codetag.alloc_tags` ;
- `init_module`, `cleanup_module`, `__cfi_check`, `__cfi_check_fail`,
  `__cfi_jt_init_module` et `__cfi_jt_cleanup_module` ;
- les noms commençant par `__typeid__` ou `__kcfi_typeid_`.

La zone `extern` affichée par IDA est une vue d'analyse synthétique, et non une
véritable section ELF. Dans un `.ko` `ET_REL` final, les cibles de relocalisation
externes sont des entrées `SHN_UNDEF` de `.symtab`, dont le chargeur exige les
noms exacts. La politique suit donc la classe ELF réelle du symbole et sa
section de définition : les imports non définis gardent leur nom exact, tandis
que les définitions éligibles sont renommées quelle que soit leur présentation.

Tous les noms sont planifiés globalement avant mutation. Les définitions qui
partagent le même candidat de base reçoivent, dans un ordre déterministe, le nom sans
suffixe puis `_1`, `_2`, etc. ; ce cas normal n'est pas une erreur. La
finalisation s'arrête si un nom produit entre en conflit avec l'espace réservé
aux noms qui doivent rester inchangés, ou si le calcul des coordonnées ou des
numéros dépasse la plage numérique. Elle rejette aussi le résultat par sécurité,
sans supposition, devant `SHN_COMMON`,
`SHN_LIVEPATCH` ou un indice de section ELF réservé inconnu. `SHN_COMMON`
n'est pas valide dans un module final chargeable : compilez avec `-fno-common`.
Un module livepatch exige l'ordre et les indices originaux de sa table de
symboles ainsi que des métadonnées de relocalisation supplémentaires, que cette
politique ne prétend pas préserver.

La détection emploie plusieurs signaux : tout symbole `SHN_LIVEPATCH`, toute
section `.klp.*`, tout drapeau `SHF_RELA_LIVEPATCH` ou tout champ `.modinfo`
séparé par NUL et commençant par `livepatch=` identifie un module livepatch et
entraîne son rejet par sécurité. Le marqueur `.modinfo` suffit à lui seul, même sans
section `.klp.*` ni drapeau de relocalisation livepatch.

Seuls les noms éligibles de `.symtab` sont remplacés. Un `.ko` chargeable a
toujours besoin de `.symtab`, de sa `.strtab` liée et des relocalisations ;
les outils génériques peuvent donc légitimement l'indiquer `not stripped`.
Des magasins et interfaces indépendants comme BTF, les exports du module,
`.modinfo`, `__versions`, les métadonnées de trace, `__ksymtab_strings`,
`.rodata` et les chaînes littérales peuvent encore révéler des noms d'origine
ou du texte identifiant. Les noms ordinaires changent aussi dans kallsyms et les
diagnostics, ce qui réduit l'utilité de ftrace par symbole, des attaches
kprobe/BPF et des rapports de plantage. Diagnostiquez avec une build debug non
dépouillée et ne dépendez pas du nom d'origine d'un symbole privé en release.

### Frontière plugin d'une release Android finalisée

La finalisation établit deux frontières d'identité indépendantes et fermées en
cas d'échec autour des phases de sortie plugin :

- Avant toute phase `ObjectGraph` remplaçable, le sceau du graphe lie le
  `section ID`, le `final ordinal` et le nom exact de chaque section logique
  conservée. Il lie aussi le `symbol ID` de chaque symbole à nom exact à son
  nom, sa classe, sa section, sa valeur, sa taille, son binding, son type et son
  `st_other` complet. Le vérificateur release recalcule séparément les noms
  structurels ordinaires.
- Après que l'hôte a établi une base d'écriture fiable et avant
  `neverc.object.post_write`, le sceau de l'image lie l'ordinal et le nom de
  chaque section logique conservée, le nombre total d'entrées `.symtab`, ainsi
  que le nom et les attributs de chaque symbole à nom exact à son `slot`
  `.symtab` brut.

La matrice de capacités est donc volontairement étroite :

| Binding de phase | Comportement d'une release Android finalisée |
|------------------|----------------------------------------------|
| `neverc.object.write` `provider` / `interceptor` | `REJECTED` avant de pouvoir remplacer la base d'écriture fiable établie par l'hôte |
| `plugin-owned ObjectFormat graph writer` | `REJECTED` ; une release Android finalisée exige le graph writer appartenant à l'hôte qui établit la base fiable |
| `observer` | `READ_ONLY` ; l'observation reste permise, sans mutation de l'artefact |
| `neverc.object.post_write` `interceptor` | `VALIDATED` ; seuls des octets payload hors de la surface d'identité peuvent changer, et le résultat doit encore satisfaire le vérificateur release, le contrat ABI d'entrée et les deux sceaux d'identité |

La propriété du merge finalisé est elle aussi scellée par l'hôte. Tout
`MergedImage` ou octet indépendant d'un `third-party ObjectMergeProvider` est
écarté ; le `host-owned graph writer` sérialise le graphe vérifié et finalisé
de ce provider. Inversement, `built-in finalized input serialization` contourne
les `external object phases` et fournit au merger de l'hôte les exacts
`audited native bytes` ; cette étape d'entrée interne ne contourne pas la
frontière de sortie ci-dessus.

La finalisation n'est acceptée qu'avec `Android module merge semantics` ; elle
exige aussi à la fois une `relocatable output request` et une
`relocatable driver configuration`, sinon elle échoue `before routing`. Pour
une release Android relocatable finalisée, `frozen input format`,
`TargetKey.ObjectFormatID` et `frozen output format` doivent partager
`one format identity`. Une divergence est refusée `before provider dispatch`,
donc également avant le route planning ou la création du sink ; le capability
preflight et le graph-writer dispatch réel ne peuvent ainsi observer des
formats différents.

Pour une entrée représentable intégralement par le graphe, les interceptors de
graphe antérieurs ne peuvent s'exécuter qu'en préservant le sceau et toute la
sémantique release. Si l'entrée exige un passthrough de l'image native pour des
faits non représentables par l'`ObjectGraph`, chaque
`route-matching provider` remplaçable et chaque interceptor sont refusés. Un
provider dont la route target/CPU/features/object-format/execution-level ne
correspond pas ne s'exécute pas et ne bloque pas la release ; seuls les
observers en lecture seule sont admis. Seul
un refus ou un échec de validation `before sealed commit` annule le staging et
ne publie aucun fichier. L'échec d'un observer `AFTER_COMMIT` est signalé après
publication et ne peut pas annuler le fichier déjà publié.

Ne post-traitez pas un `.ko` avec `llvm-strip --strip-all` ou `objcopy` et ne
supprimez pas aveuglément les sections codetag/BTF/ABI. Dépouillez avant de signer
les octets finaux : toute mutation ultérieure invalide la signature. `clean` ne
doit que supprimer des fichiers, jamais dépouiller ou signer un module existant.

## Limite de sécurité

Le dépouillement retire des noms et métadonnées précieux et augmente le coût de
l'analyse, mais il ne rend pas le code machine impossible à désassembler. Un
binaire correctement dépouillé peut garder :

- les noms dynamiques d'import et d'export requis par le chargeur ;
- les noms requis par le chargeur et ceux stockés hors de `.symtab` dans un `.ko` ;
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
Le contrôle `strings` inversé ci-dessous ne doit trouver aucune correspondance
et ne réussit que dans ce cas.

```bash
llvm-readobj --sections --symbols --dyn-symbols app
llvm-dwarfdump app
! strings app | grep -Fq -- neverc_private_release_symbol
test ! -e app.dSYM

file examples/android-kernel-hello/nvk_hello.ko
llvm-readelf -h -S -s -r examples/android-kernel-hello/nvk_hello.ko
llvm-dwarfdump examples/android-kernel-hello/nvk_hello.ko
```

Pour un `.ko` ELF `ET_REL` chargeable, l'outil générique `file` peut encore
afficher `not stripped`, car `.symtab` est conservée volontairement. N'utilisez
pas cette étiquette pour décider si la release a réussi. Vérifiez plutôt
l'absence de DWARF et de `.comment`, les formes hexadécimales majuscules
canoniques `fn_`/`obj_`/`code_`/`sym_`/`abs_` pour les définitions
éligibles, l'intégrité des imports `SHN_UNDEF` et des noms requis par le
chargeur/CFI, ainsi que la validité des relocalisations. Auditez séparément BTF,
exports, modinfo, versions, métadonnées de trace et chaînes si la divulgation de
noms est importante.

Un artefact dépouillé ne doit contenir ni section de débogage source ni nom de
symbole statique privé. Les noms dynamiques et métadonnées d'exécution requis
sont attendus et ne constituent pas un échec.
