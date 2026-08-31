# VBS Enclave COFF Linker Support Specification

## Objective

Add an original NeverC COFF linker implementation for the Microsoft-compatible
VBS enclave link contract used by enclave DLLs:

```text
/DLL /INCREMENTAL:NO /INTEGRITYCHECK /ENCLAVE /GUARD:MIXED
```

The implementation must be based on the public PE/COFF ABI and verified against
Microsoft `link.exe` as a black-box compatibility oracle. It must not copy an
upstream implementation.

## Required behavior

- Accept the internal normalized options `--enclave` and `--guard=mixed`.
- Accept their normal MSVC spellings when they are forwarded through the
  NeverC Windows driver (`/ENCLAVE`, `/GUARD:MIXED`, `/INTEGRITYCHECK`, and
  `/INCREMENTAL:NO`).
- Treat `MIXED` as a linker input policy, not a new compiler instrumentation
  mode and not a new PE GuardFlags bit.
- Make `MIXED` enable CFG output and reuse NeverC's conservative collection of
  address-taken functions from both guarded and unguarded object files. It must
  not implicitly enable the long-jump table.
- Preserve the existing non-enclave `__enclave_config = 0` compatibility
  fallback.
- In enclave mode, require a real image-data definition of
  `__enclave_config`, force archive extraction when necessary, and keep the
  definition live through `/OPT:REF`.
- In enclave mode, require a live `_load_config_used` whose
  `EnclaveConfigurationPointer` field is present and whose relocated VA equals
  `ImageBase + RVA(__enclave_config)`.
- Reject an explicit incremental link request in enclave mode. Do not silently
  produce an enclave image after an explicitly incompatible request.
- Do not make `/ENCLAVE` silently imply DLL output, CFG, integrity checking,
  enclave CRT selection, VEIID processing, or signing. Those are separate
  build-pipeline choices.
- Keep the implementation valid for the NeverC-supported 64-bit Windows
  targets (x86-64 and ARM64).

The first version intentionally does not enforce policy inside the pointed-to
`IMAGE_ENCLAVE_CONFIG64`. Its `Size`, import list, and policy fields are loader
inputs with versioning semantics; the linker only proves that the published
load-config VA is structurally present and targets the live configuration
object.

## Verification contract

Local tests must cover parsing, guarded plus unguarded CFG target collection,
archive extraction, GC liveness, load-config bounds, pointer correctness,
missing symbols, x86-64, ARM64, and preservation of the non-enclave fallback.

A Windows GitHub Actions workflow must:

1. Build the changed NeverC compiler/linker.
2. Build a minimal VBS enclave fixture.
3. Link equivalent images with Microsoft `link.exe` and NeverC.
4. Compare public PE semantics rather than whole-file bytes.
5. Run VEIID before signing when the SDK tool is available.
6. Attempt a differential `CreateEnclave` / `LoadEnclaveImage` /
   `InitializeEnclave` test.

Static PE/COFF checks are fail-hard. Runtime loading is best-effort on a GitHub
hosted Windows runner because that environment does not guarantee VBS, HVCI,
nested virtualization, test-signing state, or a reboot. The same workflow must
support a preconfigured self-hosted VBS runner where runtime loading can be
made a required gate.

## Completion criteria

- All focused local COFF tests pass on the development host.
- Existing targeted linker and driver tests remain green.
- The feature branch is pushed and the new Windows workflow runs.
- The Windows static/reference job passes.
- Runtime results are classified as pass, candidate regression, or explicit
  environment skip; an environment skip must never hide a static mismatch.
