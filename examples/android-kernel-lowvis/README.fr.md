**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Exemples NeverC](../../docs/examples/README.fr.md)

# Gestion de visibilité module noyau Android

Démo de gestion de visibilité de module. Drapeaux : aucun=visibilité liste basique, `-DNVK_LOWVIS_FILTER`=filtre de visibilité complet (liste+sysfs+proc), `-DNVK_LOWVIS_FILTER_FULL`=étendu (dmesg+PID+mount+maps), `-DNVK_LOWVIS_CRED`=démo wrappers d'identifiants (`struct cred`), `-DNVK_LOWVIS_SELINUX`=démo état d'application SELinux (permissive).

## Construction

```bash
cd examples/android-kernel-lowvis
neverc make
```

Changez `KERNEL` en `515`, `601`, `606`, `612` ou `618` pour d'autres versions.

## Déploiement et exécution

```bash
neverc make run
```

Ou manuellement :

```bash
adb push nvk_lowvis.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_lowvis.ko'
adb shell su -c 'dmesg | grep neverc_krt_lowvis'
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
adb shell su -c 'insmod /data/local/tests/nvk_lowvis.ko'
```

Les nouvelles lignes apparaissent dans le terminal 1 au moment du chargement. Ctrl+C pour arrêter.

Note : `dmesg -w` manque sur certaines builds Android ; `/proc/kmsg` exige root mais suit la sortie noyau en direct de façon fiable.

## Déchargement

```bash
neverc make rmmod
```

Ou manuellement :

```bash
adb shell su -c 'rmmod neverc_krt_lowvis'
```
