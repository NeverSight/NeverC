**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Hook inline noyau Android

Hook inline sur `do_faccessat`. Par défaut : remplacement simple avec trampoline. Avec `-DNVK_CONTEXT_HOOK` : hook contextuel recevant l'état complet des registres `nvk_reg_ctx`. Démontre le patching sûr BTI/PAC, la relocation relative au PC et le trampoline cohérent D-cache→I-cache.

## Modes de hook

| | Simple Hook (défaut) | Context Hook (`-DNVK_CONTEXT_HOOK`) |
|---|---|---|
| **Signature** | Typedef exacte requise | Non requise — via `ctx->regs[0..7]` |
| **Garde de réentrance** | Manuelle (`nvk_hook_enter`/`leave`) | Intégrée (`guard_task`) |
| **Activer/désactiver** | Manuel (`WRITE_ONCE`) | Vérification rapide intégrée au stub |
| **Appel original** | Via pointeur `orig` | Automatique (après le handler) |
| **Ignorer l'original** | Ne pas appeler `orig` | `NVK_CTX_SKIP(ctx, ret)` |
| **Redirection** | N/A | `NVK_CTX_REDIRECT(ctx, addr)` |
| **Modifier les args** | Modifier avant d'appeler `orig` | `NVK_CTX_SET_ARG(ctx, n, val)` |
| **Sécurité FP** | Convention caller-save | `NVK_CTX_FP_GUARD_BEGIN`/`END` |
| **Coût** | Faible (4 insns patch + trampoline) | Plus élevé (116 insns stub + sauvegarde complète) |
| **Cas d'usage** | Signatures connues, perf critique | Monitoring, ABI instable, prototypage rapide |

**Recommandation** : préférez le context hook sauf si vous devez intercepter la valeur de retour ou si les performances sont critiques.

## Construction

```bash
cd examples/android-kernel-inline-hook
neverc make
```

Changez `KERNEL` en `515`, `601`, `606` ou `612` pour d'autres versions.

## Déploiement et exécution

```bash
neverc make run
```

Ou manuellement :

```bash
adb push nvk_inline_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_inline_hook.ko'
adb shell su -c 'dmesg | grep nvk_inline_hook'
```

## Déchargement

```bash
neverc make rmmod
```

Ou manuellement :

```bash
adb shell su -c 'rmmod nvk_inline_hook'
```
