**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Esempi NeverC](../../docs/examples/README.it.md)

# Android Kernel Multi-File Module

Modulo kernel NeverC multi-file. Punti chiave:

- **Bootstrap singolo**: `NEVERC_KRT_BOOTSTRAP()` viene chiamato solo una volta in `module_init`
- **Stato condiviso**: il compilatore promuove tutto lo stato `neverc_krt_*` a linkage `weak_odr`, tutti i `.c` condividono lo stesso resolver, cache e stato
- **Architettura divisa**: `main.c` (init/exit), `interposes.c` (logica interpose), `utils.c` (helper)

## Compilazione

```bash
cd examples/android-kernel-multifile
neverc make          # debug: -g (predefinito alla prima build)
neverc make release  # release: -O2 --strip
neverc make debug    # torna a debug
```

Seleziona un altro preset, ad esempio, con `neverc make KERNEL=612 release`.
Il Makefile salva `KERNEL` e `PROFILE`, quindi i successivi `make push`/`run`
continuano a usare l'artefatto scelto.

Lo strip release è integrato in NeverC e limitato a una policy sicura per i
moduli kernel. Rimuove DWARF, `.comment` e i nomi privati/non definiti non
necessari alle rilocazioni, ma conserva tabelle simboli/stringhe ET_REL,
rilocazioni, import, definizioni globali, `__versions`, `.codetag.alloc_tags` e
l'ABI del loader. Non è strip-all né offuscamento; i nomi richiesti dalle
rilocazioni possono restare. Firma sempre dopo lo strip. Non eseguire strip in
`clean`, non usare `llvm-strip --strip-all` su un `.ko` e non rimuovere alla
cieca `.codetag.alloc_tags` o `__codetag_*`.

## Deploy ed esecuzione

```bash
neverc make run
```

O manualmente:

```bash
adb push nvk_multi.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_multi.ko'
adb shell su -c 'dmesg | grep neverc_krt_multi'
```

## Log del kernel (live)

Sul dispositivo, `cat /proc/kmsg` trasmette il ring buffer del kernel in tempo reale — simile a **DbgView** su Windows. Usarlo quando `insmod` fallisce con un errore generico o serve vedere il vero motivo del rifiuto (vermagic, modversions, dimensione section, ecc.).

Terminale 1 (lasciare in esecuzione):

```bash
adb shell
su
cat /proc/kmsg
```

Terminale 2:

```bash
adb shell su -c 'insmod /data/local/tests/nvk_multi.ko'
```

Le nuove righe compaiono nel terminale 1 al momento del caricamento. Ctrl+C per fermare.

Nota: su alcune build Android manca `dmesg -w`; `/proc/kmsg` richiede root ma segue l'output kernel live in modo affidabile.

## Scaricamento

```bash
neverc make rmmod
```
