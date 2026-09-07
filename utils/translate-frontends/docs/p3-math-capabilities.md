# P3B mathematics SDK and runtime capabilities

Status: implemented in `Cpp/CppSdk` and `Cpp/LibraryMappings`, integrated with
the full-Clang helper, shared typed IR and translation driver. The 21 focused
SDK/runtime tests pass, including real external SDK admission and both actual
bootstrapped architecture payloads. The expanded integrated suite passes all 126 tests on native arm64 and
Rosetta x86_64, including ambient-header/default-config isolation. Fresh-prefix
math output builds and runs at O0/O2 after removing the helper and SDK descriptor. This document describes implementation
contracts, not a self-contained frontend/SDK release.

## Source and execution boundary

The shared mapping table owns `cpp.math.fabs.f64.v1` and
`cpp.math.floor.f64.v1`, which map binary64 to binary64 through
`neverc_math_abs`/`math_abs` and `neverc_math_floor`/`math_floor`. Both require
`neverc/std/math.h`; the helper cannot provide an arbitrary emitted symbol.
The [support matrix](support-matrix.md) specifies accepted double operations,
source options and rejected constructs. `cpp.math.binary64.masked.v1` is an
independent FP contract, not inferred from the existence of either mapping.

SDK admission is restricted to Clang 20.1.8, libc++ 200100 and Apple SDK 15.5.
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

## External SDK descriptor and approved catalog

`--cpp-sdk PATH` overrides `neverc-cpp-sdk.json` beside the selected helper. Only
`cpp-math-v1` consults it. There is no automatic host SDK discovery. Missing or
mismatched SDK admission uses `TR0101` with the required distribution identity.

```json
{
  "schema": "neverc.cpp.sdk",
  "version": 1,
  "distribution_id": "clang20.1.8-libcxx200100-macos15.5",
  "roots": {
    "libcxx": "/path/to/llvm/include/c++/v1",
    "resource": "/path/to/llvm/lib/clang/20",
    "platform": "/path/to/MacOSX15.5.sdk"
  }
}
```

These are exactly the four permitted descriptor fields and three named roots.
Roots may instead be relative to the descriptor directory. They must resolve
to existing, canonical, disjoint directories separate from the owned project.
Descriptor JSON is limited to 64 KiB and depth 16; admitted SDK files are bounded
regular files of at most 4 MiB. A symlink changing a root-relative file identity
cannot silently import a different path. Descriptor claims and self-supplied
hashes cannot approve files.

The implementation-owned [approved-sdk.json](../cpp/sdk/approved-sdk.json) is
compiled into both binaries. It contains the actual `<cmath>` dependency union
for both accepted targets: each probe consumed 201 headers, and the union has
209 entries, plus `SDKSettings.json` metadata. Catalog SHA-256 is
`0c72f368ab38180821ca5c51fdab9f7fd2c3e9d6106d5c327fa09a8c8c795fa4`.
Its paths and hashes contain no machine paths or Apple SDK content.

Loading validates the complete approved inventory. Each helper job also hashes
exact consumed Clang buffers; the consumer independently verifies those reported
root/path/hash identities against its compiled catalog and actual files.
`CppSdkContext::ApprovedFiles` is convenience data, not an authority a caller can
mutate to approve a new dependency. Rechecks reconstruct the immutable catalog.
The descriptor and `SDKSettings.json` snapshots are verified before publication.

Compilation database options cannot override SDK/resource/sysroot settings.
Owned `-I`, `-iquote`, and `-isystem` inputs retain the owned-code policy. The
helper uses explicit approved search paths with `--no-default-config`,
`-nostdinc`, and `-nostdinc++`, and clears implicit compiler configuration.
Generated-code validation must likewise isolate inherited include/configuration
state so the approved runtime header is the one actually consumed.

The helper installation supplies a descriptor template and catalog data; a
configured descriptor may locate an existing external SDK for development.
It does not package that SDK. LLVM redistribution notices and Apple SDK limits
remain as recorded in [P0](p0-sdk-probes.md).

## Wire evidence and declaration approval

The request adds a single SDK envelope:

```json
{
  "sdk": {
    "distribution_id": "clang20.1.8-libcxx200100-macos15.5",
    "catalog_sha256": "<compiled catalog SHA-256>",
    "roots": {"libcxx": "<absolute>", "resource": "<absolute>", "platform": "<absolute>"}
  }
}
```

Response fields `sdk_distribution_id`, `sdk_catalog_sha256`, and
`sdk_dependencies` use separate named-root-relative SDK identities. An SDK
entry has `root`, `path`, and `sha256`. Owned dependencies retain their original
project-relative guard. Host SDK root locations are process context and do not
become generated identifiers or artifact paths.

Mapping evidence contains `id`, `declaration_id`, `result: "double"`,
`parameters: ["double"]`, and `origin: {root,path,sha256,line,column}`. The pinned
libc++ `cmath` imports global functions through using declarations. The helper
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
| fabs | `c:@F@fabs` | `f3ca0fb6a3bcfcc5dc17c1c23aa24f17ca350364ec3f88fb2ee305011ba67c3e` |
| floor | `c:@F@floor` | `6bf45a2f8a4b03ce7d791a0c8425efecaf9afa3f0cef99a9cc084d408fc7e6e9` |

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

[CppSdk.h](../../../neverc/lib/Translate/Cpp/CppSdk.h) exposes `loadCppSdk`,
`cppSdkRequestJSON`, `verifyCppSdkDependencies`, and `verifyCppSdkMappings` using
owning context records and shared `SDKDependency`/`MappingEvidence` types.
[LibraryMappings.h](../../../neverc/lib/Translate/Cpp/LibraryMappings.h) exposes
`inspectMathRuntime`, fixed approval lookup, and identity computation. The shared
`findMappingSpec` table also serves the verifier/emitter, avoiding duplicate
symbol mappings. Capability inspection links LLVM `Core` and `BitReader` plus
their transitive support components; it adds no full-Clang dependency to NeverC.

Optional controlled header/module providers are a unit-test seam, never user
configuration. Eleven SDK tests and ten runtime tests cover actual inventory and
payload approval, missing/modified headers, forged catalog data, provenance,
descriptor mutation, malformed/empty/duplicate payloads, wrong symbols/types/
calling conventions/targets, unexpected dependencies, disabled std and stale
implementation identities. Failures return no partial approval. The integrated
suite exercises actual helper output, generated NC, runtime linking, numeric
module comparison and disabled-runtime rejection. These tests complement the
installed-prefix checks and source-subset tests rather than replacing them.
