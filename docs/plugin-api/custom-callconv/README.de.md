**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Benutzerdefinierte Aufrufkonventionen

NeverC unterstützt **datengesteuerte benutzerdefinierte Aufrufkonventionen** — Sie können beliebigen Funktionen über ein externes Plugin oder Quellcode-Attribute willkürliche physische Register für Argumente und Rückgabewerte zuweisen, ohne den Compiler oder TableGen-Definitionen zu ändern.

## Überblick

Traditionelle LLVM-Aufrufkonventionen sind über `.td` / `.inc`-Dateien im Backend hartcodiert. NeverC ersetzt dies durch einen **laufzeitdatengesteuerten** Ansatz:

- Eine **Register-Zuweisungsspezifikation** (Klartext-String) wird jeder Funktion als String-Attribut angehängt.
- Das Backend liest diese Spezifikation und weist Parameter/Rückgabewerte den angegebenen physischen Registern zu.
- Die Spezifikation kann von einem **externen Plugin** (IR-Pass), **Quellcode-Attributen** (`__attribute__` / `__declspec`) oder beidem stammen.

## Spezifikationsformat

Semikolon-getrennte Zeichenkette. Jedes Segment hat einen Schlüssel und eine kommagetrennte Liste von Registernamen (Groß-/Kleinschreibung egal, Leerzeichen toleriert):

```
gpr:rcx,rdx,r8,r9; xmm:xmm0,xmm1; ret:rax; ret_xmm:xmm0
```

| Segment | Alias | Bedeutung |
|---|---|---|
| `args` | | **Positionsmodus**: Jedes Token ist ein Registername oder `stack`/`mem` |
| `gpr` | `arg_gpr` | **Pool-Modus**: Integer-/Pointer-Argumentregister |
| `xmm` | `arg_xmm` | **Pool-Modus**: Float-/Vektor-Argumentregister |
| `ret_gpr` | `ret` | Integer-/Pointer-Rückgaberegister |
| `csr` | | Benutzerdefinierter Callee-Saved-Registersatz |

### Unterstützte Architekturen

| Architektur | GPR | SIMD | Breitenauswahl |
|---|---|---|---|
| **x86-64** | `rax`, `rcx`, `rdx`, `rsi`, `rdi`, `r8`–`r11` | `xmm0`–`xmm15` | i32→32 Bit, i64→64 Bit |
| **AArch64** | `x0`–`x28` | `v0`–`v31` | i32→`w`, i64→`x`, f32→`s`, f64→`d` |

## Verwendung

### 1. Plugin-gesteuert (empfohlen)

```bash
cd pluginsdk/examples && make CustomCallConvPlugin.dylib
# Attribut-Modus (Standard)
neverc -fplugin-pass=./CustomCallConvPlugin.dylib input.c -o output.o
# Globaler Modus
neverc -fplugin-pass=./CustomCallConvPlugin.dylib \
       -fplugin-pass-arg=cc-all=1 \
       -fplugin-pass-arg=ccspec="gpr:r10,r11,rsi;ret:rdx" \
       input.c -o output.o
```

### 2. Quellcode-Attribute

```c
__attribute__((custom_attr("neverc-callconv", "gpr:r10,r11,rsi;ret:rdx")))
int add3(int a, int b, int c) { return a + b + c; }

__declspec(custom_attr("neverc-callconv", "gpr:r10;ret:rdx"))
int msfunc(int a) { return a; }
```

## LTO-Unterstützung

Das Plugin registriert sich bei `NEVERC_INTERPOSE_POST_OPT` und `NEVERC_INTERPOSE_LTO_POST_OPT` und stellt sicher, dass benutzerdefinierte Konventionen auch nach LTO-Zusammenführung angewendet werden.

## Plugin-API

```c
API->FunctionSetCustomCallConv(F, "gpr:r10,r11,rsi;ret:rdx");
```

Setzt `CallingConv::NeverC_Custom` (CC 1000), schreibt das Attribut und **synchronisiert alle direkten Aufrufstellen**. `NULL` oder `""` löscht die Konvention.

## Tests

GoogleTest-Suite (22 Tests, alle PASS):

```bash
ninja -C build-neverc neverc-tests
build-neverc/bin/neverc-tests --gtest_filter='CustomCallConvTest.*'
```
