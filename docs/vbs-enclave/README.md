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
  vertdll.lib bcrypt.lib libcmt.lib libvcruntime.lib ucrt.lib `
  -Xmslink /INCREMENTAL:NO `
  -Xmslink /NODEFAULTLIB `
  -Xmslink /ENCLAVE `
  -Xmslink /INTEGRITYCHECK `
  -Xmslink /GUARD:MIXED `
  -Xmslink /DYNAMICBASE `
  -Xmslink /MACHINE:X64 `
  -o game-security-enclave.dll
```

Use the enclave variants of the MSVC CRT and UCRT libraries. Their exact paths
come from the installed Visual C++ toolset and Windows SDK.

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
explicit in the build pipeline.

## Build and deployment flow

1. Compile security-sensitive sources with CFG enabled, for example
   `-fms-guard=cf`. Legacy objects may remain uninstrumented when the final link
   uses `/GUARD:MIXED`.
2. Define the enclave configuration and entry point, then link against the
   enclave CRT/UCRT plus the required Vertdll import libraries.
3. Inspect the unsigned PE image and verify its load-config directory, CFG
   tables, enclave configuration pointer, and base relocations.
4. Run the Windows SDK VEIID tool on the completed image.
5. Sign the VEIID-processed image. Signing must be the final file mutation.
6. In the host, check `IsEnclaveTypeSupported(ENCLAVE_TYPE_VBS)`, allocate the
   enclave with `CreateEnclave`, load the DLL with `LoadEnclaveImage`, and call
   `InitializeEnclave`.

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
