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

Sélectionnez un autre préréglage avec, par exemple,
`neverc make KERNEL=612 release`. Le Makefile mémorise `KERNEL` et `PROFILE` :
les commandes `make push`/`run` suivantes conservent donc l'artefact choisi.

Le dépouillement release est intégré à NeverC et limité pour rester compatible
avec les modules noyau. Il retire DWARF, `.comment` et les noms privés/non
définis inutiles aux relocalisations, mais conserve les tables de symboles et
de chaînes ET_REL, les relocalisations, imports, définitions globales,
`__versions`, `.codetag.alloc_tags` et l'ABI du chargeur. Ce n'est ni strip-all
ni une obfuscation ; les noms requis par les relocalisations peuvent rester.
Signez toujours après le dépouillement. Ne dépouillez jamais dans `clean`,
n'utilisez pas `llvm-strip --strip-all` sur un `.ko` et ne supprimez pas
aveuglément `.codetag.alloc_tags` ou `__codetag_*`.


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
