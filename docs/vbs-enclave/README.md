**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Documentation index](../README.md) · [← NeverC project](../../README.md)

# VBS enclave DLLs on Windows

NeverC can link Microsoft-compatible VBS enclave DLLs for 64-bit Windows
targets. The supported linker contract is:

```text
/DLL /INCREMENTAL:NO /INTEGRITYCHECK /ENCLAVE /GUARD:MIXED
```

Pass Microsoft linker options through the Windows driver with `-Xmslink` or
`-Wl,`:

```powershell
neverc.exe --target=x86_64-pc-windows-msvc -fno-lto -shared -nostdlib `
  enclave.obj guarded.obj legacy.obj `
  -lvertdll -lbcrypt -llibcmt -llibvcruntime -lucrt `
  -Xmslink /INCREMENTAL:NO `
  -Xmslink /NODEFAULTLIB `
  -Xmslink /ENCLAVE `
  -Xmslink /INTEGRITYCHECK `
  -Xmslink /GUARD:MIXED `
  -Xmslink /DYNAMICBASE `
  -Xmslink /MACHINE:X64 `
  -o game-security-enclave.dll
```

This example explicitly selects the enclave variants of the MSVC CRT and UCRT
libraries with `-l`. Any explicit `-vctoolsdir` or `-winsysroot` selection retains
its normal precedence. Without those overrides, every `/ENCLAVE` link on
macOS, Linux, or Windows resolves Windows libraries only from NeverC's bundled
target runtime; it does not auto-detect or fall back to a Visual Studio toolset
or Windows SDK installed on the host.

## Cross-host builds with the bundled runtime

Compilation and COFF linking are host-independent. The same command can run on
macOS, Linux, or Windows after installing the target runtime:

```text
neverc runtime install windows-x64
neverc runtime install windows-arm64
```

The target package contains the Windows headers, enclave CRT, enclave UCRT,
`vertdll.lib`, `bcrypt.lib`, and the other required Windows import libraries.
When bundled resolution is active, only explicit `/ENCLAVE` combined with global
`/NODEFAULTLIB` switches from the ordinary bundled CRT/UCRT directories to the
enclave CRT/UCRT directories. In that mode, before linking, the driver verifies
that all five bundled libraries exist: `libcmt.lib`, `libvcruntime.lib`,
`ucrt.lib`, `vertdll.lib`, and `bcrypt.lib`. The libraries are still selected
explicitly with `-l...`. `/ENCLAVE` by itself neither enables the enclave
CRT/UCRT directories nor selects their libraries; it keeps the bundled ordinary
runtime search paths.

The cross-host link stage produces the unsigned, unprocessed enclave DLL.
VEIID processing, SignTool signing, and actual loading through
`CreateEnclave`/`LoadEnclaveImage` remain Windows-only, so move a DLL linked on
macOS or Linux to a Windows packaging or test machine for the final three
stages. See [Target runtimes](../runtime/README.md) for runtime installation and
discovery.

## Required image inputs

An enclave link must provide both of these image-data definitions:

- `__enclave_config`, containing the image's `IMAGE_ENCLAVE_CONFIG` data.
- `_load_config_used`, with a load-config structure large enough to contain
  `EnclaveConfigurationPointer`.

NeverC keeps `__enclave_config` live through dead stripping, extracts it from
an archive when necessary, and verifies that the final relocated load-config
pointer equals the virtual address of that configuration object. A missing,
absolute, discarded, truncated, or incorrectly relocated definition is a link
error.

`/GUARD:MIXED` enables CFG output for a mixture of guarded and legacy object
files. It includes the conservative address-taken targets needed by the
unguarded objects; it is not a separate compiler instrumentation mode or a new
PE `GuardFlags` bit.

An explicit incremental-link request is incompatible with `/ENCLAVE` and is
rejected. The last effective `/INCREMENTAL` option is used, including options
originating in object-file directives.

`/ENCLAVE` does not implicitly select DLL output, CFG, integrity checking,
enclave CRT libraries, VEIID processing, or signing. Keep those choices
explicit in the build pipeline. In bundled-runtime mode, the enclave CRT/UCRT
search paths and five-library validation described above activate only with
explicit global `/NODEFAULTLIB`; without that option, the bundled ordinary
Windows runtime paths remain in use. Explicit user toolchain overrides keep
their normal precedence.

## Build and deployment flow

1. Compile security-sensitive sources with CFG enabled, for example
   `-fms-guard=cf`. Legacy objects may remain uninstrumented when the final link
   uses `/GUARD:MIXED`.
2. Define the enclave configuration and entry point, then link against the
   enclave CRT/UCRT plus the required Vertdll and BCrypt import libraries.
3. Inspect the unsigned PE image and verify its load-config directory, CFG
   tables, enclave configuration pointer, and base relocations.
4. On Windows, run the Windows SDK VEIID tool on the completed image.
5. On Windows, sign the VEIID-processed image with SignTool. Signing must be
   the final file mutation.
6. In the Windows host, check `IsEnclaveTypeSupported(ENCLAVE_TYPE_VBS)`,
   allocate the enclave with `CreateEnclave`, load the DLL with
   `LoadEnclaveImage`, and call `InitializeEnclave`.

For anti-cheat systems, the enclave is suitable for a small verification or
key-handling component whose code and private state need a stronger boundary
from the ordinary game process. Keep the enclave interface narrow and validate
all host-supplied data: the host still controls inputs, scheduling, storage,
and availability. A VBS enclave complements server-side authority, telemetry,
driver defenses, and ordinary process hardening; it does not replace them.

## Validation

The `VBS enclave differential CI` workflow runs on Windows. Its static gate:

- builds the NeverC linker and focused COFF tests;
- creates equivalent Microsoft-linked and NeverC-linked enclave DLLs;
- compares public PE/load-config/CFG semantics;
- runs mutation tests against the PE verifier; and
- prepares VEIID-processed images for a differential runtime probe.

The runtime probe executes the Microsoft image first. If the hosted runner
lacks VBS or a usable signing environment, the result is an explicit
environment skip. Once the Microsoft reference loads successfully, either
NeverC candidate failing is a hard test failure. A configured self-hosted VBS
runner can make runtime success mandatory.

The linker supports x86-64 and ARM64 COFF enclave images. It validates the
published configuration pointer but does not impose extra policy on the
versioned fields inside `IMAGE_ENCLAVE_CONFIG`.
