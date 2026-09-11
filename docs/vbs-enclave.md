**Languages**: [English](vbs-enclave.md) | [简体中文](zh-CN/vbs-enclave.md) | [繁體中文](zh-TW/vbs-enclave.md) | [日本語](ja/vbs-enclave.md) | [한국어](ko/vbs-enclave.md) | [Français](fr/vbs-enclave.md) | [Deutsch](de/vbs-enclave.md) | [Español](es/vbs-enclave.md) | [Italiano](it/vbs-enclave.md) | [Русский](ru/vbs-enclave.md) | [العربية](ar/vbs-enclave.md)

[← Documentation index](README.md) · [← NeverC project](../README.md)

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
stages. See [Target runtimes](runtime.md) for runtime installation and
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
files. It emits five-byte GFID and GIAT entries: a four-byte RVA followed by
one byte of metadata, which is zero for current ordinary targets. Its
`GuardFlags` carry the CFG and entry-size bits. Legacy objects contribute
address-taken targets by conservatively scanning relocations while excluding
unwind metadata.
When `/GUARD:MIXED` is combined with `/GUARD:EHCONT`, the EH continuation
target table also uses five-byte entries: a four-byte RVA followed by a zero
metadata byte.

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
7. Resolve an exported enclave function with `GetProcAddress`, enter it through
   `CallEnclave`, and verify its returned value. Exercise repeated calls,
   including guarded and legacy indirect-call paths.
8. Terminate and release the enclave with `TerminateEnclave` and
   `DeleteEnclave`, checking that both operations succeed.

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

The x64 runtime probe executes the Microsoft image first, then both NeverC
candidates. It resolves an export with `GetProcAddress`, makes repeated
`CallEnclave` calls through guarded and legacy indirect-call paths, and checks
the returned values. `PASS` requires ordered evidence for every lifecycle
stage, including successful termination and deletion; loading and
initialization alone are insufficient. In this workflow, ARM64 is covered by
static validation and differential linking; the regular runtime probe remains
x64-only.

A separate validation on native Windows 11 ARM64 passed on 2026-09-11
([run 34554067852](https://github.com/NeverSight/NeverC/actions/runs/34554067852)).
It reused both NeverC ARM64 DLLs built from
`c46e3a4cc4b3732823a7241a43bca6c44b0f0b74` and tested them alongside a freshly built
MSVC reference image. All 48 lifecycle stages and 12 calls with verified
results passed; enclave teardown and signing-certificate cleanup also
succeeded. This verifies those fixed artifacts, not a fresh NeverC compiler
build or a permanent ARM64 runtime CI gate.

An optional runtime probe may report `SKIP` only for recognized environment
setup unavailability before function execution. Crashes, timeouts, missing or
out-of-order stage evidence, and functional errors are `FAIL`, including in the
Microsoft reference. A configured self-hosted VBS runner can make runtime
success mandatory.

The linker supports x86-64 and ARM64 COFF enclave images. It validates the
published configuration pointer, then derives a contiguous sequence of
80-byte `IMAGE_ENCLAVE_IMPORT` entries from the final ordinary DLL-import set.
Entries initially contain only the import name and zero identity fields for
VEIID to bind; the linker writes back the count, list, and entry size. Active
delay-loaded imports are rejected. The linker does not impose extra policy on
the versioned fields inside `IMAGE_ENCLAVE_CONFIG`.
