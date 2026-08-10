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

Seleziona un altro profilo del kernel, ad esempio, con
`neverc make KERNEL=612 release`. `neverc make release` seleziona
`-O2 --strip`. Il Makefile registra i valori `KERNEL` e `PROFILE` scelti in
`.nvk-build-flags`, quindi `make push`, `make run` e `make` senza target
continuano a usare lo stesso artefatto. Senza questo file di stato, `make` usa
debug per impostazione predefinita. `make debug` o un `PROFILE=...` esplicito
sostituisce il profilo salvato; `make clean` elimina il file e riporta la
compilazione successiva a debug.

NeverC scrive cinque classi di nomi di rilascio ispirati a IDA ma non riservati:
funzioni `fn_HEX`, etichette eseguibili senza tipo `code_HEX`, oggetti `obj_HEX`,
altre etichette senza tipo `sym_HEX` e simboli assoluti `abs_HEX`. Per una
definizione allocata ordinaria, `HEX` è una `analysis EA` deterministica derivata
dal layout finale delle sezioni `SHF_ALLOC` (`abs_HEX` usa invece lo `st_value`
assoluto); non è un hash, una encryption (crittografia), un file offset (offset
del file), una ELF virtual address (indirizzo virtuale ELF) né una runtime kernel
address (indirizzo del kernel a runtime). NeverC non memorizza le forme riservate
`sub_`/`loc_` né nomi ordinari svuotati intenzionalmente.

Per i nomi da conservare esattamente, la vista `extern` sintetica di IDA, i limiti
di sicurezza e l'ordine tra finalizzazione e firma, consulta la
[policy di rilascio e strip](../../docs/release-builds/README.it.md).

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
