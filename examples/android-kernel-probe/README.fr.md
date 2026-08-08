**Langues** : [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Exemples NeverC](../../docs/examples/README.fr.md)

# Android Kernel Probe

Interpose d'une instruction arbitraire à l'intérieur de `do_faccessat` (pas le point d'entrée) avec `neverc_krt_probe_register`. Démontre :

- **Interpose à adresse arbitraire** : sonde n'importe quelle instruction, pas seulement les entrées de fonctions
- **Contexte registre complet** : lecture/écriture de tous les GPR via `neverc_krt_reg_ctx`
- **Chaînage automatique** : plusieurs handlers sur la même adresse, exécutés par priorité
- **Contrôle de flux** : `NEVERC_KRT_CTX_SKIP` pour annuler, `NEVERC_KRT_CTX_REDIRECT` pour rediriger

## API

```c
int neverc_krt_probe_register(void *addr, neverc_krt_ctx_handler_t handler,
                              int priority, struct neverc_krt_probe_ref *ref);
int neverc_krt_probe_unregister(struct neverc_krt_probe_ref *ref);
```

Signature du handler :

```c
void my_probe(neverc_krt_reg_ctx *ctx);
```

## Compilation

```bash
cd examples/android-kernel-probe
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
adb push nvk_probe.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_probe.ko'
adb shell su -c 'dmesg | grep neverc_krt_probe_demo'
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
adb shell su -c 'insmod /data/local/tests/nvk_probe.ko'
```

Les nouvelles lignes apparaissent dans le terminal 1 au moment du chargement. Ctrl+C pour arrêter.

Note : `dmesg -w` manque sur certaines builds Android ; `/proc/kmsg` exige root mais suit la sortie noyau en direct de façon fiable.

## Déchargement

```bash
neverc make rmmod
```
