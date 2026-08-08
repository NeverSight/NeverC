**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Esempi NeverC](../../docs/examples/README.it.md)

# Android Kernel Probe

Interpose di un'istruzione arbitraria all'interno di `do_faccessat` (non il punto di ingresso) con `neverc_krt_probe_register`. Dimostra:

- **Interpose a indirizzo arbitrario**: sondare qualsiasi istruzione, non solo ingressi di funzioni
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
adb push nvk_probe.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_probe.ko'
adb shell su -c 'dmesg | grep neverc_krt_probe_demo'
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
adb shell su -c 'insmod /data/local/tests/nvk_probe.ko'
```

Le nuove righe compaiono nel terminale 1 al momento del caricamento. Ctrl+C per fermare.

Nota: su alcune build Android manca `dmesg -w`; `/proc/kmsg` richiede root ma segue l'output kernel live in modo affidabile.

## Scaricamento

```bash
neverc make rmmod
```
