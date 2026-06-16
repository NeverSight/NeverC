**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Hook inline kernel Android

Hook inline su `do_faccessat`. Default: sostituzione semplice con trampoline. Con `-DNVK_CONTEXT_HOOK`: hook contestuale che riceve lo stato completo dei registri `nvk_reg_ctx`. Dimostra patching sicuro BTI/PAC, rilocazione relativa al PC e trampoline coerente D-cache→I-cache.

## Modalità di hook

| | Simple Hook (predefinito) | Context Hook (`-DNVK_CONTEXT_HOOK`) |
|---|---|---|
| **Firma** | Typedef esatto richiesto | Non necessario — tramite `ctx->regs[0..7]` |
| **Guardia rientro** | Manuale (`nvk_hook_enter`/`leave`) | Integrata (`guard_task`) |
| **Abilitare/disabilitare** | Manuale (`WRITE_ONCE`) | Controllo rapido integrato nello stub |
| **Chiamata originale** | Tramite puntatore `orig` | Automatica (dopo l'handler) |
| **Saltare l'originale** | Non chiamare `orig` | `NVK_CTX_SKIP(ctx, ret)` |
| **Reindirizzamento** | N/A | `NVK_CTX_REDIRECT(ctx, addr)` |
| **Modificare args** | Cambiare parametri prima di `orig` | `NVK_CTX_SET_ARG(ctx, n, val)` |
| **Sicurezza FP** | Convenzione caller-save | `NVK_CTX_FP_GUARD_BEGIN`/`END` |
| **Costo** | Basso (4 insns patch + trampoline) | Maggiore (116 insns stub + salvataggio completo) |
| **Ideale per** | Firme note, prestazioni critiche | Monitoraggio, ABI instabile, prototipazione rapida |

**Raccomandazione**: preferire context hook a meno che non sia necessario intercettare il valore di ritorno o si abbiano vincoli di prestazione stringenti.

## Compilazione

```bash
cd examples/android-kernel-inline-hook
neverc make
```

Cambiare `KERNEL` in `515`, `601`, `606` o `612` per altre versioni.

## Distribuzione ed esecuzione

```bash
neverc make run
```

Oppure manualmente:

```bash
adb push nvk_inline_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_inline_hook.ko'
adb shell su -c 'dmesg | grep nvk_inline_hook'
```

## Scaricamento

```bash
neverc make rmmod
```

Oppure manualmente:

```bash
adb shell su -c 'rmmod nvk_inline_hook'
```
