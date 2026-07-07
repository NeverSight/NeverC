**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Function Interpose

Interpose di `do_faccessat` al punto di ingresso con `neverc_krt_interpose_register`. Dimostra:

- **Concatenamento automatico**: più handler sullo stesso target, eseguiti per priorità
- **Pattern di chiamata all'originale**: l'handler riceve un puntatore `orig` per invocare la funzione originale
- **Controllo priorità**: valore inferiore = esecuzione prima; usare valori negativi per eseguire prima di altri interpose
- **Coesistenza**: funziona anche se il target è già interposeato da un altro modulo

## API

```c
int neverc_krt_interpose_register(void *target, void *handler, int priority,
                             void **orig, struct neverc_krt_interpose_ref *ref);
int neverc_krt_interpose_unregister(struct neverc_krt_interpose_ref *ref);
```

Firma dell'handler:

```c
long my_interpose(void *orig, void *a0, void *a1, void *a2, void *a3, void *a4, void *a5);
```

## Compilazione

```bash
cd examples/android-kernel-inline-interpose
neverc make
```

Cambiare `KERNEL` in `515`, `601`, `606`, `612` o `618` per altre versioni del kernel.

## Deploy ed esecuzione

```bash
neverc make run
```

O manualmente:

```bash
adb push nvk_interpose_demo.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_interpose_demo.ko'
adb shell su -c 'dmesg | grep neverc_krt_interpose_demo'
```

## Scaricamento

```bash
neverc make rmmod
```
