**Langues** : [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Exemples NeverC](../../docs/examples/README.fr.md)

# Android Kernel Multi-File Module

Module noyau NeverC multi-fichiers. Points cles :

- **Bootstrap unique** : `NEVERC_KRT_BOOTSTRAP()` n'est appele qu'une fois dans `module_init`
- **Etat partage** : le compilateur promeut tout l'etat `neverc_krt_*` en linkage `weak_odr`, tous les `.c` partagent le meme resolveur, cache et etat
- **Architecture divisee** : `main.c` (init/exit), `interposes.c` (logique interpose), `utils.c` (helpers)

## Compilation

```bash
cd examples/android-kernel-multifile
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


## Deploiement et execution

```bash
neverc make run
```

Ou manuellement :

```bash
adb push nvk_multi.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_multi.ko'
adb shell su -c 'dmesg | grep neverc_krt_multi'
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
adb shell su -c 'insmod /data/local/tests/nvk_multi.ko'
```

Les nouvelles lignes apparaissent dans le terminal 1 au moment du chargement. Ctrl+C pour arrêter.

Note : `dmesg -w` manque sur certaines builds Android ; `/proc/kmsg` exige root mais suit la sortie noyau en direct de façon fiable.

## Dechargement

```bash
neverc make rmmod
```
