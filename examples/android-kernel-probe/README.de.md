**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Beispiele](../../docs/examples/README.de.md)

# Android Kernel Probe

Interpose einer beliebigen Instruktion innerhalb von `do_faccessat` (nicht der Einstiegspunkt) mit `neverc_krt_probe_register`. Demonstriert:

- **Interpose an beliebiger Adresse**: jede Instruktion interposebar, nicht nur Funktionseinstiege
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
neverc make          # debug: -g (Standard beim ersten Build)
neverc make release  # release: -O2 --strip
neverc make debug    # zurück zu debug
```

Wählen Sie ein anderes Preset z. B. mit `neverc make KERNEL=612 release`.
Das Makefile speichert `KERNEL` und `PROFILE`, sodass spätere
`make push`/`run`-Aufrufe beim gewählten Artefakt bleiben.

Release-Stripping ist in NeverC integriert und auf eine kernelmodulsichere
Teilmenge begrenzt. DWARF, `.comment` und für Relokationen unnötige private bzw.
undefinierte Symbolnamen werden entfernt; ET_REL-Symbol-/Stringtabellen,
Relokationen, Importe, globale Definitionen, `__versions`,
`.codetag.alloc_tags` und Loader-ABI-Daten bleiben erhalten. Das ist weder
strip-all noch Obfuskation; für Relokationen benötigte Namen können verbleiben.
Signieren Sie erst nach dem Stripping. Strippen Sie nie in `clean`, verwenden
Sie für `.ko` kein `llvm-strip --strip-all` und entfernen Sie
`.codetag.alloc_tags` oder `__codetag_*` nicht blind.


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

## Kernel-Log (Live)

Auf dem Gerät streamt `cat /proc/kmsg` den Kernel-Ringpuffer in Echtzeit — ähnlich wie **DbgView** unter Windows. Nutzen Sie es, wenn `insmod` nur mit einer vagen Meldung scheitert oder Sie den echten Ablehnungsgrund sehen müssen (vermagic, modversions, Section-Größe usw.).

Terminal 1 (laufen lassen):

```bash
adb shell
su
cat /proc/kmsg
```

Terminal 2:

```bash
adb shell su -c 'insmod /data/local/tests/nvk_probe.ko'
```

Neue Zeilen erscheinen in Terminal 1 beim Laden. Mit Ctrl+C beenden.

Hinweis: `dmesg -w` fehlt auf manchen Android-Builds; `/proc/kmsg` braucht root, ist für Modul-Bring-up aber zuverlässig.

## Entladen

```bash
neverc make rmmod
```
