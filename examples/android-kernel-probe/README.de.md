**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Probe

Hook einer beliebigen Instruktion innerhalb von `do_faccessat` (nicht der Einstiegspunkt) mit `neverc_krt_probe_register`. Demonstriert:

- **Hook an beliebiger Adresse**: jede Instruktion hookbar, nicht nur Funktionseinstiege
- **Vollstaendiger Registerkontext**: alle GPR lesen/schreiben via `neverc_krt_reg_ctx`
- **Automatische Verkettung**: mehrere Handler auf derselben Adresse, nach Prioritaet ausgefuehrt
- **Kontrollfluss**: `NEVERC_KRT_CTX_SKIP` zum Abbrechen, `NEVERC_KRT_CTX_REDIRECT` zum Umleiten

## API

```c
int neverc_krt_probe_register(void *addr, neverc_krt_ctx_handler_t handler,
                              int priority, struct neverc_krt_probe_ref *ref);
int neverc_krt_probe_unregister(struct neverc_krt_probe_ref *ref);
```

Handler-Signatur:

```c
void my_probe(neverc_krt_reg_ctx *ctx);
```

## Kompilieren

```bash
cd examples/android-kernel-probe
neverc make
```

`KERNEL` auf `515`, `601`, `606`, `612` oder `618` aendern fuer andere Kernelversionen.

## Deployment und Ausfuehrung

```bash
neverc make run
```

Oder manuell:

```bash
adb push nvk_probe.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_probe.ko'
adb shell su -c 'dmesg | grep neverc_krt_probe_demo'
```

## Entladen

```bash
neverc make rmmod
```
