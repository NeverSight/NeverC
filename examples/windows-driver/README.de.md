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
neverc make          # debug: -g (Standard beim ersten Build)
neverc make release  # release: -O2 --strip
neverc make debug    # zurück zu debug
```

Das Makefile speichert `ARCH`, `PROFILE` und `TESTSIGN`. Für Release:
`neverc make release` (`-O2 --strip`; PE-Imports/Exports und
Loader-Metadaten bleiben). Mit Testsignatur:
`neverc make release TESTSIGN=1` (Strip vor Signatur im selben Link).
Siehe [Release-Builds](../../docs/release-builds/README.de.md).

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

## Manuelles Bauen (ohne Make)

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fms-kernel \
  -Wall -nostdlib -shared \
  -Xlinker --driver \
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
`--driver` kennzeichnet das Image als Kernel-Modus: Code und Daten werden nicht
auslagerbar, die Importtabellen wandern in den verwerfbaren INIT-Abschnitt, und
der Linker trägt die PE-Prüfsumme ein, die der Kernel-Loader prüft.

> `-g` bettet DWARF-Debug-Informationen in die PE ein; prüfen Sie mit
> `llvm-dwarfdump`. Lassen Sie diese Option bei Release-Builds weg, um die
> Binärgröße zu reduzieren.

## Testsignierung

Windows verweigert das Laden eines unsignierten Kerneltreibers. `-ftest-sign`
hängt eine Authenticode-Signatur an, damit das Image diese Prüfung auf einem
Testrechner besteht:

```bash
neverc make TESTSIGN=1
neverc make ARCH=arm64 TESTSIGN=1
```

oder fügen Sie `-ftest-sign` einem manuellen Aufruf hinzu. Die Option wird nur
zusammen mit `-fms-kernel` akzeptiert, da eine Testsignatur für eine
User-Mode-Binärdatei bedeutungslos ist.

Die Signaturidentität ist im Compiler eingebaut — ein selbstsigniertes
Zertifikat, dessen privater Schlüssel konstruktionsbedingt öffentlich ist. Sie
gewährt keine Authentizität, sondern erfüllt nur die Codeintegritätsprüfung auf
einem Rechner, den Sie bewusst geöffnet haben. Richten Sie diesen Rechner
einmalig als Administrator ein:

```cmd
bcdedit /set testsigning on
certutil -addstore Root neverc-test-signing.cer
certutil -addstore TrustedPublisher neverc-test-signing.cer
```

und starten Sie dann neu. Exportieren Sie das Zertifikat aus dem Compiler
selbst, dann passt es immer zu der Identität, mit der signiert wird:

```bash
neverc --print-test-sign-cert > neverc-test-signing.cer
```

(Eine Kopie liegt auch unter `utils/neverc-test-signing.cer` im Quellbaum, sie
ist aber nicht Teil eines Release-Pakets.)

Ohne Windows-Rechner lässt sich die Signatur mit `osslsigncode` prüfen. Beachten
Sie, dass `-CAfile` PEM erwartet, das Zertifikat aber DER ist — konvertieren Sie
es zuerst. Übergibt man das DER direkt, scheitert es mit einem irreführenden
„signature verification failed“, dessen wahre Ursache „no certificate found“ ist:

```bash
openssl x509 -inform DER -in neverc-test-signing.cer -out nc.pem
osslsigncode verify -CAfile nc.pem ExampleDriver-x64.sys
```

**Verwenden Sie dies niemals für etwas, das einen Testrechner verlässt.** Für
die Produktion signieren Sie mit einem echten Codesignaturzertifikat (und ab
Windows 10 1607 zusätzlich mit einer Attestierungssignatur des Microsoft
Hardware Dev Center).

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
