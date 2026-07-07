**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Convenzioni di chiamata personalizzate

NeverC supporta le **convenzioni di chiamata personalizzate basate sui dati** — puoi assegnare registri fisici arbitrari agli argomenti e ai valori di ritorno di qualsiasi funzione, interamente da un plugin esterno o attributi a livello di codice sorgente, senza modificare il compilatore o definizioni TableGen.

## Panoramica

Le convenzioni di chiamata LLVM tradizionali sono codificate nel backend tramite file `.td` / `.inc`. NeverC sostituisce questo con un approccio **basato sui dati a runtime**:

- Una **specifica di assegnazione registri** (stringa di testo) viene allegata a ciascuna funzione come attributo stringa.
- Il backend legge questa specifica e assegna parametri/valori di ritorno ai registri fisici specificati.
- La specifica può provenire da un **plugin esterno** (passo IR), **attributi sorgente** (`__attribute__` / `__declspec`), o entrambi.

## Formato della specifica

Stringa delimitata da punto e virgola. Ogni segmento ha una chiave e una lista di nomi di registro separati da virgola:

```
gpr:rcx,rdx,r8,r9; xmm:xmm0,xmm1; ret:rax; ret_xmm:xmm0
```

| Segmento | Alias | Significato |
|---|---|---|
| `args` | | **Modalità posizionale**: ogni token è un nome di registro o `stack`/`mem` |
| `gpr` | `arg_gpr` | **Modalità pool**: registri argomento interi/puntatori |
| `xmm` | `arg_xmm` | **Modalità pool**: registri argomento float/vettori |
| `ret_gpr` | `ret` | Registri valore di ritorno interi/puntatori |
| `csr` | | Insieme callee-saved personalizzato |

### Architetture supportate

| Architettura | GPR | SIMD | Selezione larghezza |
|---|---|---|---|
| **x86-64** | `rax`, `rcx`, `rdx`, `rsi`, `rdi`, `r8`–`r11` | `xmm0`–`xmm15` | i32→32 bit, i64→64 bit |
| **AArch64** | `x0`–`x28` | `v0`–`v31` | i32→`w`, i64→`x`, f32→`s`, f64→`d` |

## Utilizzo

### 1. Guidato da plugin (consigliato)

```bash
cd pluginsdk/examples && make CustomCallConvPlugin.dylib
# Modalità attributo (predefinita)
neverc -fplugin-pass=./CustomCallConvPlugin.dylib input.c -o output.o
# Modalità globale
neverc -fplugin-pass=./CustomCallConvPlugin.dylib \
       -fplugin-pass-arg=cc-all=1 \
       -fplugin-pass-arg=ccspec="gpr:r10,r11,rsi;ret:rdx" \
       input.c -o output.o
```

### 2. Attributi sorgente

```c
__attribute__((custom_attr("neverc-callconv", "gpr:r10,r11,rsi;ret:rdx")))
int add3(int a, int b, int c) { return a + b + c; }

__declspec(custom_attr("neverc-callconv", "gpr:r10;ret:rdx"))
int msfunc(int a) { return a; }
```

## Supporto LTO

Il plugin si registra su `NEVERC_INTERPOSE_POST_OPT` e `NEVERC_INTERPOSE_LTO_POST_OPT`, garantendo che le convenzioni personalizzate vengano applicate anche dopo la fusione LTO.

## API plugin

```c
API->FunctionSetCustomCallConv(F, "gpr:r10,r11,rsi;ret:rdx");
```

Imposta `CallingConv::NeverC_Custom` (CC 1000), scrive l'attributo e **sincronizza tutti i siti di chiamata diretti**. Passare `NULL` o `""` cancella la convenzione.

## Test

Suite GoogleTest (22 test, tutti PASS):

```bash
ninja -C build-neverc neverc-tests
build-neverc/bin/neverc-tests --gtest_filter='CustomCallConvTest.*'
```
