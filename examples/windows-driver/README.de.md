**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Beispiele](../../docs/examples/README.de.md)

# Windows-Kerneltreiber-Beispiel

Ein minimaler WDM-Kerneltreiber, erstellt mit NeverC. Zielt standardmäßig auf
**x64** und kann auch für ARM64 gebaut werden. Cross-Kompilierung von macOS / Linux.

NeverC ist ein All-in-One-Compiler — ein einziger Aufruf übernimmt Preprocessing,
Kompilierung, Optimierung (auto-LTO) und Linken über den integrierten Linker.

## Bauen

Aus dem Repository:

```bash
cd examples/windows-driver
neverc make
```

Das erzeugt `ExampleDriver-x64.sys`. Für ARM64 oder für beide Architekturen:

```bash
neverc make ARCH=arm64
neverc make all-arch
```

Mit einer eigenständigen NeverC-Version:

```bash
neverc make NEVERC=/path/to/neverc
```

Die Ausgabe ist `ExampleDriver-<arch>.sys` (auto-LTO-optimiert).
Der Standard-Build enthält `-g` zum Debuggen; **Release-Builds sollten `-g`
entfernen**, um Debug-Symbole zu entfernen und die Binärgröße zu reduzieren
(~38 KB → ~3 KB).

## Manuelles Bauen (ohne Make)

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fms-kernel \
  -Wall -nostdlib -shared \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -lntoskrnl -lhal \
  -o ExampleDriver-x64.sys driver.c
```

Für ARM64 genügt es, das Ziel auf `aarch64-pc-windows-msvc` zu ändern; sonst
bleibt alles gleich. `-fms-kernel` wählt die zum Ziel passenden WDK-Header und
Importbibliotheken aus und definiert die vom WDK erwarteten Architekturmakros,
sodass diese nie von Hand übergeben werden müssen.

> `-g` bettet DWARF-Debug-Informationen in die PE ein; prüfen Sie mit
> `llvm-dwarfdump`. Lassen Sie diese Option bei Release-Builds weg, um die
> Binärgröße zu reduzieren.

## Funktionen

- Erstellt ein Geräteobjekt unter `\Device\ExampleDriver`
- Erstellt einen symbolischen Link unter `\DosDevices\ExampleDriver`
- Behandelt `IRP_MJ_CREATE`, `IRP_MJ_CLOSE`, `IRP_MJ_DEVICE_CONTROL`
- Gibt Lade-/Entlade-Nachrichten über `DbgPrint` aus

## Laden (auf einem Windows-Testrechner)

```cmd
sc create ExampleDriver type= kernel binPath= C:\path\to\ExampleDriver-x64.sys
sc start ExampleDriver
sc stop ExampleDriver
sc delete ExampleDriver
```

Aktivieren Sie die Testsignierung oder verwenden Sie ein Codesignaturzertifikat für die Produktion.
