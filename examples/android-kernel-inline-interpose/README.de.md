**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Beispiele](../../docs/examples/README.de.md)

# Android Kernel Function Interpose

Interpose von `do_faccessat` am Funktionseinstiegspunkt mit `neverc_krt_interpose_register`. Demonstriert:

- **Automatische Verkettung**: mehrere Handler auf demselben Ziel, nach Priorität ausgeführt
- **Original-Aufruf-Muster**: Handler erhält `orig`-Zeiger zum Aufrufen der Originalfunktion
- **Prioritätskontrolle**: niedrigerer Wert = frühere Ausführung; negative Werte um vor anderen Interposes zu laufen
- **Koexistenz**: funktioniert auch wenn das Ziel bereits von einem anderen Modul geinterposet ist

## API

```c
int neverc_krt_interpose_register(void *target, void *handler, int priority,
                             void **orig, struct neverc_krt_interpose_ref *ref);
int neverc_krt_interpose_unregister(struct neverc_krt_interpose_ref *ref);
```

Handler-Signatur:

```c
long my_interpose(void *orig, void *a0, void *a1, void *a2, void *a3, void *a4, void *a5);
```

## Kompilieren

```bash
cd examples/android-kernel-inline-interpose
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


## Deployment und Ausführung

```bash
neverc make run
```

Oder manuell:

```bash
adb push nvk_interpose_demo.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_interpose_demo.ko'
adb shell su -c 'dmesg | grep neverc_krt_interpose_demo'
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
adb shell su -c 'insmod /data/local/tests/nvk_interpose_demo.ko'
```

Neue Zeilen erscheinen in Terminal 1 beim Laden. Mit Ctrl+C beenden.

Hinweis: `dmesg -w` fehlt auf manchen Android-Builds; `/proc/kmsg` braucht root, ist für Modul-Bring-up aber zuverlässig.

## Entladen

```bash
neverc make rmmod
```
