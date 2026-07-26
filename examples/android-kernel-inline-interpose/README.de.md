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
neverc make
```

`KERNEL` auf `515`, `601`, `606`, `612` oder `618` ändern für andere Kernelversionen.

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

## Entladen

```bash
neverc make rmmod
```
