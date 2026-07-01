**Langues** : [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Probe

Hook d'une instruction arbitraire à l'intérieur de `do_faccessat` (pas le point d'entrée) avec `neverc_krt_probe_register`. Démontre :

- **Hook à adresse arbitraire** : sonde n'importe quelle instruction, pas seulement les entrées de fonctions
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
neverc make
```

Changer `KERNEL` en `515`, `601`, `606`, `612` ou `618` pour d'autres versions du noyau.

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

## Déchargement

```bash
neverc make rmmod
```
