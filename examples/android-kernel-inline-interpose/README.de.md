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

Wählen Sie ein anderes Kernel-Preset z. B. mit
`neverc make KERNEL=612 release`. `neverc make release` wählt
`-O2 --strip`. Das Makefile schreibt die gewählten Werte für `KERNEL` und
`PROFILE` in `.nvk-build-flags`, sodass spätere Aufrufe von `make push`,
`make run` und `make` ohne Ziel dasselbe Artefakt verwenden. Ohne diese
Statusdatei verwendet `make` standardmäßig debug. `make debug` oder ein
ausdrückliches `PROFILE=...` ersetzt das gespeicherte Profil; `make clean`
löscht die Statusdatei, sodass der nächste Build wieder debug verwendet.

NeverC schreibt fünf Klassen von IDA inspirierten, nicht reservierten Release-Namen:
Funktionen `fn_HEX`, ausführbare typfreie Labels `code_HEX`, Objekte `obj_HEX`,
andere typfreie Labels `sym_HEX` und absolute Symbole `abs_HEX`. Für gewöhnliche
allozierte Definitionen ist `HEX` eine deterministische `analysis EA`, die aus
dem endgültigen Layout der `SHF_ALLOC`-Sektionen abgeleitet wird (`abs_HEX`
verwendet stattdessen den absoluten `st_value`); sie ist weder hash (Hashwert)
noch encryption (Verschlüsselung), file offset (Datei-Offset), ELF virtual
address (virtuelle ELF-Adresse) oder runtime kernel address
(Kernel-Laufzeitadresse). NeverC speichert weder reservierte `sub_`/`loc_`-Formen
noch absichtlich leere gewöhnliche Namen.

Die [Release- und Strip-Richtlinie](../../docs/release-builds/README.de.md)
beschreibt die exakt zu erhaltenden Namen, IDAs synthetische `extern`-Ansicht,
die Sicherheitsgrenzen sowie die Reihenfolge von Finalisierung und Signatur.

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
