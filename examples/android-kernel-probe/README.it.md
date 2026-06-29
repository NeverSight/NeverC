**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Probe

Hook di un'istruzione arbitraria all'interno di `do_faccessat` (non il punto di ingresso) con `neverc_krt_probe_register`. Dimostra:

- **Hook a indirizzo arbitrario**: sondare qualsiasi istruzione, non solo ingressi di funzioni
- **Contesto registri completo**: lettura/scrittura di tutti i GPR via `neverc_krt_reg_ctx`
- **Concatenamento automatico**: piu handler sullo stesso indirizzo, eseguiti per priorita
- **Controllo flusso**: `NEVERC_KRT_CTX_SKIP` per abortire, `NEVERC_KRT_CTX_REDIRECT` per reindirizzare

## API

```c
int neverc_krt_probe_register(void *addr, neverc_krt_ctx_handler_t handler,
                              int priority, struct neverc_krt_probe_ref *ref);
int neverc_krt_probe_unregister(struct neverc_krt_probe_ref *ref);
```

Firma dell'handler:

```c
void my_probe(neverc_krt_reg_ctx *ctx);
```

## Compilazione

```bash
cd examples/android-kernel-probe
neverc make
```

Cambiare `KERNEL` in `515`, `601`, `606` o `612` per altre versioni del kernel.

## Deploy ed esecuzione

```bash
neverc make run
```

O manualmente:

```bash
adb push nvk_probe.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_probe.ko'
adb shell su -c 'dmesg | grep neverc_krt_probe_demo'
```

## Scaricamento

```bash
neverc make rmmod
```
