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
`-O2 --strip`. Sans `.nvk-build-flags`, `make` utilise debug par défaut et ne
choisit pas release de lui-même. Les Makefile d'exemple conservent un profil
choisi explicitement afin que `make push`, `make run` et un `make` sans cible
réutilisent le même artefact. `make debug` ou un `PROFILE=...` explicite
remplace ce choix ; `make clean` efface l'état et ramène la build suivante à
debug. Sur cette voie finale, NeverC retire les sections de débogage,
`.comment` et les entrées locales/non définies inutiles aux relocalisations,
puis reconstruit `.strtab`.

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
