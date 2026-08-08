**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Beispiele](../../docs/examples/README.de.md)

# Android Kernel Syscall Interpose

Interpose von `openat` durch Ersetzen des Zeigers in `sys_call_table`. Demonstriert klassische Syscall-Interception auf ARM64 GKI Kerneln mit `neverc_krt_syscall_replace` / `neverc_krt_syscall_restore`.

## API

```c
int neverc_krt_syscall_replace(int nr, neverc_krt_syscall_fn_t new_fn,
                               neverc_krt_syscall_fn_t *orig);
int neverc_krt_syscall_restore(int nr, neverc_krt_syscall_fn_t orig);
```

## Kompilieren

```bash
cd examples/android-kernel-syscall-interpose
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
adb push nvk_syscall_interpose.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_syscall_interpose.ko'
adb shell su -c 'dmesg | grep neverc_krt_syscall'
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
adb shell su -c 'insmod /data/local/tests/nvk_syscall_interpose.ko'
```

Neue Zeilen erscheinen in Terminal 1 beim Laden. Mit Ctrl+C beenden.

Hinweis: `dmesg -w` fehlt auf manchen Android-Builds; `/proc/kmsg` braucht root, ist für Modul-Bring-up aber zuverlässig.

## Entladen

```bash
neverc make rmmod
```
