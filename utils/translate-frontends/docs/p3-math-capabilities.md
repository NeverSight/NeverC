# P3B mathematics SDK and runtime capabilities

Status: implemented in `Cpp/CppSdk` and `Cpp/LibraryMappings`, integrated with
the built-in full-Clang frontend, shared typed IR and translation driver. SDK
headers and their approval catalog are embedded in NeverC. This document defines
the source, provenance and runtime contracts; the [support matrix](support-matrix.md)
records execution evidence separately from delivery architecture.

## Source and execution boundary

The shared mapping table owns `cpp.math.fabs.f64.v1` and
`cpp.math.floor.f64.v1`, which map binary64 to binary64 through
`neverc_math_abs`/`math_abs` and `neverc_math_floor`/`math_floor`. Both require
`neverc/std/math.h`; the frontend cannot provide an arbitrary emitted symbol.
The [support matrix](support-matrix.md) specifies accepted double operations,
source options and rejected constructs. `cpp.math.binary64.masked.v1` is an
independent FP contract, not inferred from the existence of either mapping.

SDK admission is restricted to the embedded Clang 20.1.8, libc++ 200100 and
macOS 15.5 header distribution.
Math requires an explicit x86_64 or arm64 macOS 15.0 target. LLVM target parsing
and actual Clang deployment macros verify equivalent spellings: Darwin 24.6.0
and Darwin 24.0.0 both select macOS 15.0 in this pinned toolchain. Other effective
deployment versions, environments and platforms fail `TR0204`; existing NeverC
payload tables do not establish their source-language or numeric equivalence.

The runtime floor correction and 32,944-case per-configuration numeric evidence
are described in the [mapping inventory](runtime-mapping.md). A constant sNaN
source call at `-O2` exposed different exception effects from a dynamic call;
source builtin NaN/infinity constructors are therefore rejected. Dynamic values
continue through the tested module boundary. No general floating arithmetic or
source `fenv` API is implied by the runtime probes.

## Built-in SDK and approved catalog

`cpp-math-v1` always uses distribution
`neverc-embedded-clang20.1.8-libcxx200100-macos15.5`. No SDK descriptor, host SDK
discovery or external root selection is involved. The removed `--cpp-sdk` option
is rejected; `NEVERC_CPP_SDK` is ignored. Failed built-in SDK integrity checks use
`TR0101` with the required distribution identity.

The implementation-owned [catalog.json](../../../neverc/lib/Translate/Cpp/SDK/catalog.json)
and its source files are compiled into NeverC. The `<cmath>` dependency union
contains 209 original headers: 127 libc++ headers, one Clang resource header and
81 platform headers. The original full Apple `SDKSettings.json` is not distributed.
NeverC supplies only `Version: "15.5"` and
`MaximumDeploymentTarget: "15.5.99"`, the required version facts for the admitted
macOS targets. Its metadata SHA-256 is
`58499bbeb3eb1aa9ca96358a097bc237a9beb14cfd3db9876d986534e59ea17e`.
Catalog SHA-256 is
`e9e2be353baded7be350900ae52d5c1a5f0fc7724e29c2dbe18c9c5fdef5dbe3`.
The [SDK provenance and license inventory](../../../neverc/lib/Translate/Cpp/SDK/README.md)
records original byte hashes, public upstream sources and retained notices.
This is a bounded header distribution, not a complete Apple SDK.

Loading validates the complete embedded inventory. Read-only VFS buffers supply
the approved files under a reserved synthetic root; a physical root collision or
an overlap with owned sources is rejected. Each frontend job hashes the exact
consumed Clang buffers. The consumer independently verifies their named-root,
path and hash identities against its embedded catalog and bytes.
`CppSdkContext::ApprovedFiles` is convenience data, not an authority a caller can
mutate to approve a new dependency. Rechecks reconstruct the immutable catalog.

Compilation database options cannot override SDK/resource/sysroot settings.
Owned `-I`, `-iquote`, and `-isystem` inputs retain the owned-code policy. The
frontend uses explicit approved search paths with `--no-default-config`,
`-nostdinc`, and `-nostdinc++`, and clears implicit compiler configuration.
Generated-code validation must likewise isolate inherited include/configuration
state so the approved runtime header is the one actually consumed.

Normal NeverC installation supplies the built-in frontend and SDK together.
Corresponding header sources, provenance and licenses accompany distribution;
they are not runtime search paths. A system Clang and its own SDK may be used as
an independent regression oracle, but cannot replace translation inputs.

## Wire evidence and declaration approval

The request adds a single SDK envelope:

```json
{
  "sdk": {
    "distribution_id": "neverc-embedded-clang20.1.8-libcxx200100-macos15.5",
    "catalog_sha256": "<compiled catalog SHA-256>"
  }
}
```

Response fields `sdk_distribution_id`, `sdk_catalog_sha256`, and
`sdk_dependencies` use separate named-root-relative SDK identities. An SDK
entry has `root`, `path`, and `sha256`. Owned dependencies retain their original
project-relative guard. The three root names `libcxx`, `resource` and `platform`
identify virtual embedded trees, not host directories. The request permits only
the two SDK fields above. Manifests add `sdk.delivery: "builtin"` and record
consumed dependencies, without an external descriptor hash or physical roots.

Mapping evidence contains `id`, `declaration_id`, `result: "double"`,
`parameters: ["double"]`, and `origin: {root,path,sha256,line,column}`. The pinned
libc++ `cmath` imports global functions through using declarations. The frontend
checks the resolved `std` import chain, exact overload and all redeclarations;
canonical function spelling alone is insufficient. Owned redeclarations,
lookalike namespaces, aliases and replacement headers fail.

Both admitted origins are `platform/usr/include/math.h`, whose SHA-256 is
`00606d7b27eb5db0a3b93c575a7084f432d1e783cfca2eedcdc234c01cb15134`.
`fabs` is at 423:15 and `floor` at 466:15. Declaration IDs are SHA-256 of the
following fields separated by newlines, without a trailing newline:
distribution ID, root name, relative path, canonical Clang USR, `double(double)`.

| Operation | Canonical USR | Declaration ID |
| --- | --- | --- |
| fabs | `c:@F@fabs` | `3b5582378dc6c99969ded4259e116878bc30c63d238676286b4729b7ab12a5ce` |
| floor | `c:@F@floor` | `60841fa6c87218b07cc6556af6e977abac151c2cc1ca59104bb25cb8006df30a` |

The consumer checks these fixed origins/signatures/IDs, requires matching SDK
dependency evidence, then independently constructs a runtime capability ID.
An incoming capability claim cannot authorize a mapping.

## Runtime verification and implementation identity

The driver queries its validation compiler's resource directory. The exact
`include/neverc/std/math.h` must hash to
`2124e02ddab13569fb310376913cda90c3c9effd11daf381b9dcf50c4a2d5093`.
For each required mapping, exactly one nonempty bounded embedded module must
exist. Full bitcode parsing and LLVM verification precede inspection. Require
an externally visible definition with C calling convention and exact
`double(double)` signature; reject aliases, unrelated definitions, unexpected
nonintrinsic external dependencies, wrong targets/layouts and invalid bitcode.
The approved abs/floor definitions have no nonintrinsic external dependencies;
old floor payloads requiring `neverc_math_modf` fail.

Policy `neverc.math.canonical-ir.v1` fully parses the module, strips debug info,
sets the module identifier to `neverc.math.payload.v1`, clears `source_filename`,
removes `llvm.ident`, and removes only `Debug Info Version`, `Dwarf Version`,
`DWARF64`, and `CodeView` module flags. SHA-256 is over `Module::print` output.
All target/layout, functions, attributes, globals, instructions and other
metadata remain. No unknown semantic attribute is erased merely to force a match.

| Payload target | Module | Approved canonical SHA-256 |
| --- | --- | --- |
| macOS x86_64 | `math_abs` | `12a2074d0a47cfab43e3641b21d630ac3fdc3054bd42b48196f3f29f21c1ee66` |
| macOS x86_64 | `math_floor` | `7ef3cbd4cdbbfa8290762500aa616a7d22a1bfc2db233ad7a4af755e30ddbf1d` |
| macOS arm64 | `math_abs` | `53dede0961bd20a20ecb8829a2b7fff04994bf451aa1282736eabcc48c917d86` |
| macOS arm64 | `math_floor` | `38cfac156bacb0705392b44cc347ee10ce524368dd143c65bed1f54e5c6bdb38` |

Source relocation changed every raw bitcode hash but preserved these canonical
identities. Exact bootstrap actions executed by native arm64 and Rosetta NeverC
builders also produced matching canonical identities for both target payloads.
Raw payload hashes remain separate package provenance. Reproduce this check by
bootstrapping the same targets with each builder, computing canonical IR hashes,
and comparing them against the registry above. Keep generated payloads, IR and
machine-specific measurement reports outside version control.

Unknown or stale identities fail `TR0403` with expected/observed evidence.
Updating the fixed registry requires review of the canonical IR diff, complete
numeric matrix and installed-output tests. There is no automatic acceptance of
currently linked payloads. The old incompatible floor is never a fallback.

Capability identity hashes the policy, target, approved header, sorted operation
IDs and canonical module identities. Records additionally contain each raw
module hash, defined symbols and required symbols. Mapping-free math requires no
unused runtime header or payload. With required mappings, `-fno-builtin-std`
fails capability checks. The driver then compiles a separate runtime-valued link
probe through the same NeverC/target/policy; failure is `TR0404`. No source
program is executed by translation.

## Component interfaces and tests

[CppSdk.h](../../../neverc/lib/Translate/Cpp/CppSdk.h) exposes `loadBuiltinCppSdk`,
`cppSdkRequestJSON`, `verifyCppSdkDependencies`, and `verifyCppSdkMappings` using
owning context records and shared `SDKDependency`/`MappingEvidence` types.
[LibraryMappings.h](../../../neverc/lib/Translate/Cpp/LibraryMappings.h) exposes
`inspectMathRuntime`, fixed approval lookup, and identity computation. The shared
`findMappingSpec` table also serves the verifier/emitter, avoiding duplicate
symbol mappings. Capability inspection links LLVM `Core` and `BitReader` plus
their transitive support components from NeverC's LLVM. The built-in full-Clang
frontend remains in its separate, symbol-isolated static archive.

Optional controlled header/module providers are a unit-test seam, never user
configuration. SDK and runtime tests cover actual inventory and
payload approval, missing/modified runtime headers, forged catalog data,
provenance, caller-mutated SDK evidence, malformed/empty/duplicate payloads, wrong symbols/types/
calling conventions/targets, unexpected dependencies, disabled std and stale
implementation identities. Failures return no partial approval. The integrated
suite exercises actual built-in frontend output, generated NC, runtime linking,
numeric module comparison, preserved SDK macro branches, owned shadow-header
rejection, ignored external-tool environment variables, rejected legacy options,
installed runtime resources and disabled-runtime rejection. These tests complement the
installed-prefix checks and source-subset tests rather than replacing them.
