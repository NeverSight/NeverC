**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Examples](../../docs/examples/README.md)

# Windows Kernel Driver Example

A minimal WDM kernel driver built with NeverC. Targets **x64** by default, and
can also be built for ARM64. Cross-compiles from macOS / Linux.

NeverC is an all-in-one compiler — a single invocation handles preprocessing,
compilation, optimization (auto-LTO), and linking via the built-in linker.

## Build

From the repo:

```bash
cd examples/windows-driver
neverc make          # debug: -g (default on the first build)
neverc make release  # release: -O2 --strip
neverc make debug    # switch back to debug
```

The Makefile persists `ARCH`, `PROFILE`, and `TESTSIGN`. Use
`neverc make release` for `-O2 --strip` (PE imports/exports and loader
metadata remain). Prefer `neverc make release TESTSIGN=1` so strip runs
before the Authenticode test signature in the same link.
See [Release builds](../../docs/release-builds/README.md).

That builds `ExampleDriver-x64.sys`. To build for ARM64 instead, or for both:

```bash
neverc make ARCH=arm64
neverc make all-arch
```

From a standalone NeverC release:

```bash
neverc make NEVERC=/path/to/neverc
```

The output is `ExampleDriver-<arch>.sys` (auto-LTO optimized).

## Manual build (without Make)

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

For ARM64, swap in `--target=aarch64-pc-windows-msvc`; nothing else changes.
`-fms-kernel` picks the WDK headers and import libraries matching the target
and defines the architecture macros the WDK expects, so they never have to be
passed by hand.
`--driver` marks the image as kernel-mode: code and data become non-paged, the
import tables move into the discardable INIT section, and the linker fills in
the PE checksum the kernel loader verifies.

> `-g` emits DWARF debug info into the PE; inspect with `llvm-dwarfdump`.

## Test signing

Windows refuses to load an unsigned kernel driver. `-ftest-sign` attaches an
Authenticode signature so the image passes that check on a test machine:

```bash
neverc make TESTSIGN=1
neverc make ARCH=arm64 TESTSIGN=1
```

or add `-ftest-sign` to a manual invocation. It is only accepted together with
`-fms-kernel`, since a test signature means nothing for a user-mode binary.

The signing identity is built into the compiler — a self-signed certificate
whose private key is public by construction. It grants no authenticity; it only
satisfies code integrity on a machine you have deliberately opened up. Set that
machine up once, as administrator:

```cmd
bcdedit /set testsigning on
certutil -addstore Root neverc-test-signing.cer
certutil -addstore TrustedPublisher neverc-test-signing.cer
```

then reboot. Get the certificate from the compiler itself, which always matches
the identity it signs with:

```bash
neverc --print-test-sign-cert > neverc-test-signing.cer
```

(A copy also sits at `utils/neverc-test-signing.cer` in the source tree, but it
is not part of a release package.)

To check a signature without a Windows machine, use `osslsigncode`. Note that
`-CAfile` wants PEM while the certificate is DER, so convert it first — passing
the DER directly fails with a confusing "signature verification failed" that is
really "no certificate found":

```bash
openssl x509 -inform DER -in neverc-test-signing.cer -out nc.pem
osslsigncode verify -CAfile nc.pem ExampleDriver-x64.sys
```

**Never use this for anything that leaves a test machine.** For production,
sign with a real code-signing certificate (and, for Windows 10 1607 and later,
an attestation signature from the Microsoft Hardware Dev Center).

## What it does

- Creates a device object at `\Device\ExampleDriver`
- Creates a symbolic link at `\DosDevices\ExampleDriver`
- Handles `IRP_MJ_CREATE`, `IRP_MJ_CLOSE`, `IRP_MJ_DEVICE_CONTROL`
- Prints load/unload messages via `DbgPrint`

## Loading (on a Windows test machine)

```cmd
sc create ExampleDriver type= kernel binPath= C:\path\to\ExampleDriver-x64.sys
sc start ExampleDriver
sc stop ExampleDriver
sc delete ExampleDriver
```

Enable test signing or use a code signing certificate for production.
