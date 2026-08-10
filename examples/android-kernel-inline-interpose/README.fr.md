**Langues** : [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Exemples NeverC](../../docs/examples/README.fr.md)

# Android Kernel Function Interpose

Interpose de `do_faccessat` à son point d'entrée avec `neverc_krt_interpose_register`. Démontre :

- **Chaînage automatique** : plusieurs handlers sur la même cible, exécutés par priorité
- **Appel de l'original** : le handler reçoit un pointeur `orig` pour appeler la fonction originale
- **Contrôle de priorité** : valeur plus basse = exécution en premier ; utiliser des valeurs négatives pour passer avant d'autres interposes
- **Coexistence** : fonctionne même si la cible est déjà interposeée par un autre module

## API

```c
int neverc_krt_interpose_register(void *target, void *handler, int priority,
                             void **orig, struct neverc_krt_interpose_ref *ref);
int neverc_krt_interpose_unregister(struct neverc_krt_interpose_ref *ref);
```

Signature du handler :

```c
long my_interpose(void *orig, void *a0, void *a1, void *a2, void *a3, void *a4, void *a5);
```

## Compilation

```bash
cd examples/android-kernel-inline-interpose
neverc make          # debug : -g (première construction par défaut)
neverc make release  # release : -O2 --strip
neverc make debug    # retour au profil debug
```

Sélectionnez un autre profil du noyau avec, par exemple,
`neverc make KERNEL=612 release`. `neverc make release` sélectionne
`-O2 --strip`. Le Makefile inscrit les valeurs `KERNEL` et `PROFILE` choisies
dans `.nvk-build-flags` ; les commandes ultérieures `make push`, `make run` et
`make` sans cible réutilisent donc le même artefact. Sans ce fichier d'état,
`make` utilise debug par défaut. `make debug` ou un `PROFILE=...` explicite
remplace le profil enregistré ; `make clean` supprime le fichier et ramène la
construction suivante à debug.

NeverC écrit cinq classes de noms de publication inspirés d'IDA mais non réservés :
les fonctions `fn_HEX`, les étiquettes sans type exécutables `code_HEX`, les
objets `obj_HEX`, les autres étiquettes sans type `sym_HEX` et les symboles
absolus `abs_HEX`. Pour une définition ordinaire allouée, `HEX` est une
`analysis EA` déterministe dérivée de la disposition finale des sections
`SHF_ALLOC` (`abs_HEX` utilise plutôt la valeur `st_value` absolue) ; ce n'est ni
un hash (hachage), ni une encryption (chiffrement), ni un file offset (décalage
de fichier), ni une ELF virtual address (adresse virtuelle ELF), ni une runtime
kernel address (adresse noyau à l'exécution). NeverC ne stocke ni les formes
réservées `sub_`/`loc_`, ni des noms ordinaires volontairement vides.

Pour les noms à conserver exactement, la vue `extern` synthétique d'IDA, les
limites de sécurité et l'ordre entre finalisation et signature, consultez la
[politique de publication et de dépouillement](../../docs/release-builds/README.fr.md).

## Déploiement et exécution

```bash
neverc make run
```

Ou manuellement :

```bash
adb push nvk_interpose_demo.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_interpose_demo.ko'
adb shell su -c 'dmesg | grep neverc_krt_interpose_demo'
```

## Journal noyau (temps réel)

Sur l'appareil, `cat /proc/kmsg` diffuse le ring buffer noyau en temps réel — un peu comme **DbgView** sous Windows. Utilisez-le quand `insmod` échoue avec une erreur vague ou quand vous devez voir le vrai motif de refus (vermagic, modversions, taille de section, etc.).

Terminal 1 (laisser tourner) :

```bash
adb shell
su
cat /proc/kmsg
```

Terminal 2 :

```bash
adb shell su -c 'insmod /data/local/tests/nvk_interpose_demo.ko'
```

Les nouvelles lignes apparaissent dans le terminal 1 au moment du chargement. Ctrl+C pour arrêter.

Note : `dmesg -w` manque sur certaines builds Android ; `/proc/kmsg` exige root mais suit la sortie noyau en direct de façon fiable.

## Déchargement

```bash
neverc make rmmod
```
