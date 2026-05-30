**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Windows-Kerneltreiber mit Gleitkomma

Ein WDM-Kerneltreiber, erstellt mit NeverC, der die **sichere Verwendung
von Gleitkomma- / SIMD-Operationen im Kernelmodus** demonstriert.
Cross-Kompilierung von macOS / Linux.

## Bauen

```bash
cd examples/windows-driver-float
make
```

Mit einer eigenständigen NeverC-Version:

```bash
make NEVERC=/path/to/neverc
```

Die Ausgabe ist `FloatDriver.sys` (auto-LTO-optimiert).
Der Standard-Build enthält `-g` zum Debuggen; entfernen Sie `-g` für Release-Builds.

---

## Zwei Probleme zu lösen

Gleitkomma im Kernelmodus hat zwei eigenständige Probleme:

### Problem 1 — der `_fltused`-ABI-Marker (Kompilier-/Linkzeit)

Der MSVC-Compiler erzeugt einen undefinierten Verweis auf das Symbol
`_fltused`, sobald eine Übersetzungseinheit eine Gleitkommaoperation
durchführt. In Benutzermodus-Programmen liefert `libcmt.lib` dieses Symbol,
sodass der Linker zufrieden ist und einige FP-spezifische CRT-Teile
eingebunden werden.

Kerneltreiber werden **nicht** gegen `libcmt` gelinkt (wir übergeben
`-nostdlib` und `-Xlinker --nodefaultlib`), daher würde ein ungelöstes
`_fltused` einen Linkfehler verursachen.

**Wie NeverC das löst**: Mit `-fms-kernel` definiert das X86-Backend von
LLVM `_fltused` lokal als 0. Sie können dies in der generierten Assembly sehen:

```asm
# Benutzermodus-Ziel:
    .globl  _fltused              # externer Verweis -- benötigt libcmt
```

```asm
# -fms-kernel-Ziel:
    .globl  _fltused
    .set    _fltused, 0           # lokale Definition! kein externes Symbol nötig
```

Sie müssen also **nie manuell `int _fltused = 0;`** in Ihren Treiber schreiben.

### Problem 2 — der Kernel sichert FP/SIMD-Register NICHT (Laufzeit)

Der Windows-Kernel speichert/restauriert die x87 / XMM / YMM / ZMM-Register
bei Kontextwechseln standardmäßig **nicht**. Wenn ein Treiber diese Register
von beliebigem Kernelcode aus berührt, beschädigt er stillschweigend den
SIMD-Zustand des Benutzermodus-Threads, der gerade auf der CPU läuft.

**Lösung**: Klammern Sie jeden Gleitkomma- / SIMD-Bereich mit
[`KeSaveExtendedProcessorState`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kesaveextendedprocessorstate)
und `KeRestoreExtendedProcessorState` ein:

```c
XSTATE_SAVE save;
NTSTATUS status = KeSaveExtendedProcessorState(XSTATE_MASK_LEGACY, &save);
if (!NT_SUCCESS(status))
    return status;

// ... Ihr FP / SIMD-Code hier ...

KeRestoreExtendedProcessorState(&save);
```

### XSTATE-Masken

| Maske | Deckt ab |
|-------|----------|
| `XSTATE_MASK_LEGACY_FLOATING_POINT` (Bit 0) | x87-Stack |
| `XSTATE_MASK_LEGACY_SSE` (Bit 1) | XMM0–15 |
| `XSTATE_MASK_LEGACY` | Bit 0 \| Bit 1 (deckt den meisten einfachen `double` / SSE-Code ab) |
| `XSTATE_MASK_GSSE` / AVX (Bit 2) | obere Hälften von YMM0–15 |
| `XSTATE_MASK_AVX512` | AVX-512 ZMM-Register |

Übergeben Sie die OR-kombinierte Maske, die zu den breitesten von Ihrem Code verwendeten Registern passt.

---

## Was dieser Treiber tut

- Erstellt ein Geräteobjekt unter `\Device\FloatDriver` und einen
  symbolischen Link unter `\DosDevices\FloatDriver`
- Ruft in `DriverEntry` zweimal `ComputeAreaSafe()` auf (das `ComputeArea()`
  mit FP-Zustandsspeicherung/-wiederherstellung umhüllt) mit `radius=1.0` und `radius=5.0`
- Gibt die rohen Bits des Doubles über `DbgPrint` aus (da `%f` von `DbgPrint`
  nicht unterstützt wird — wir verwenden `RtlCopyMemory`, um das 64-Bit-Muster zu extrahieren)
- Definiert `_fltused` implizit über `-fms-kernel`

## Überprüfung der `_fltused`-Ausgabe

Vergleichen Sie die Compiler-Ausgabe mit und ohne `-fms-kernel`:

```bash
# Benutzermodus (bräuchte libcmt):
neverc --target=x86_64-pc-windows-msvc -S /tmp/foo.c -o - | grep fltused
#  .globl  _fltused

# Kernel (lokal als 0 definiert):
neverc --target=x86_64-pc-windows-msvc -fms-kernel -S /tmp/foo.c -o - | grep fltused
#  .globl  _fltused
#  .set    _fltused, 0
```

## Laden (auf einem Windows-Testrechner)

```cmd
sc create FloatDriver type= kernel binPath= C:\path\to\FloatDriver.sys
sc start FloatDriver
sc stop FloatDriver
sc delete FloatDriver
```

Aktivieren Sie die Testsignierung oder verwenden Sie ein Codesignaturzertifikat für die Produktion.

## Vorbehalte

- **`%f` funktioniert nicht mit `DbgPrint`** — die Kernel-Debug-Print-Routine
  hat keine Gleitkomma-Formatierung. Konvertieren Sie Ihren Double in eine
  Festkomma-Ganzzahl zur Anzeige, oder geben Sie die rohen Bits aus, wie
  dieses Beispiel zeigt.
- **Verwenden Sie keine Gleitkomma bei IRQL ≥ DISPATCH_LEVEL**, es sei denn,
  es ist absolut notwendig. `KeSaveExtendedProcessorState` dokumentiert die
  IRQL-Einschränkungen.
- **Leistung**: Zustandsspeicherung/-wiederherstellung ist nicht kostenlos;
  für Hot-Pfade sollten Sie das Bündeln von FP-Arbeit in einer einzigen
  eingeklammerten Region in Betracht ziehen.
