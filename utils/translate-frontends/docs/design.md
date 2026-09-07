# C++ translation design and contracts

Status: experimental C++ development implementation for
[issue #16](https://github.com/NeverSight/NeverC/issues/16). NeverC contains the
full-Clang frontend, typed IR, artifact checks, owned-project translation and
bounded math mappings. The approved math SDK headers are embedded as well;
translation requires no separate frontend executable, Clang installation or SDK
descriptor. Consult the [support matrix](support-matrix.md) for the difference between feasibility
evidence and accepted translation inputs.

## Implementation decisions

1. Statically embed LLVM/Clang **20.1.8** in NeverC behind a C ABI. A private
   upstream build and mandatory symbol-isolation audit keep its C++ namespaces,
   C symbols and support libraries separate from NeverC's modified LLVM. No
   upstream AST/Sema types cross that boundary. Each analysis job invokes the
   current NeverC executable in its private frontend mode, preserving process
   limits and isolation without a separately installed frontend.
2. Use a structured, versioned semantic protocol across the process boundary.
   The feasibility prototype has its own explicitly nonproduction format. It
   does not establish production protocol compatibility.
3. Start with one native, hosted C++17 translation unit and the immutable
   `cpp-core-v1` profile. No standard library, includes, allocator, or foreign
   runtime is needed by this scalar/aggregate profile.
4. The embedded distribution contains 209 original approved SDK headers and
   NeverC-authored SDK version metadata. Its immutable catalog, source provenance
   and license notices are under [Cpp/SDK](../../../neverc/lib/Translate/Cpp/SDK/README.md).
   The [P0 SDK probes](p0-sdk-probes.md) retain the historical external-toolchain
   feasibility evidence; they are not installation instructions.
5. Enable no C++ runtime mappings in P0–P2. P3B now implements the two explicitly
   approved math mappings; the [mapping inventory](runtime-mapping.md) records
   their numeric, error, capability, and installation evidence and remaining gates.

Production integration follows the prototype's build, resolved-AST, sequencing,
dependency-isolation, and measurement checks. Parsing a header or one example
does not satisfy a complete language profile or close issue #16.

The normal NeverC build uses [BuiltinCppFrontend.cmake](../../../neverc/cmake/modules/BuiltinCppFrontend.cmake)
to obtain the upstream 20.1.8 source archive with a pinned SHA-256.
`NEVERC_CPP_LLVM_SOURCE_ARCHIVE` can supply the same archive offline;
`NEVERC_CPP_BUILD_JOBS` bounds its private build concurrency. These are build
inputs, not translation-time tool selectors. Before linking, the audit compares
every private definition and reference with the host libraries' definitions.
The pinned LLVM reader checks private objects; a compiler-compatible host reader
handles newer Clang/GCC LTO, while MSVC uses validated COFF linker indexes for
LTCG archives. `NEVERC_CPP_HOST_NM` overrides the host reader at build time. A
failed inspection stops the link. Installed translation does not load external Clang/LLVM
libraries. Existing NeverC runtime and optional plugin dependencies are separate
from this frontend contract.

## Pipeline and ownership

```text
source + explicit source compiler options
  -> current NeverC's built-in full-Clang child: parse, resolve, enforce source subset
  -> C++ adapter: explicit evaluation, conversions, semantic identities
  -> versioned typed Translate IR
  -> language-neutral verifier -> NC emitter
  -> temporary .nc + metadata -> NeverC syntax check -> NeverC object check
  -> optional mapping capability/link probes -> publish
```

The [production protocol contract](protocol.md) specifies the concrete wire
representation and its validation limits. The C++ adapter owns declaration
resolution, overload selection, source
initialization and sequencing, and later library provenance. The shared IR
contains typed scalar/record values, uniquely identified per-function storage,
source locations, and explicit assignment, call, label, jump, branch, and return
operations. Its expression operands are pure. The adapter snapshots reads and
materializes effects in their source-permitted order before passing IR to the
emitter; printing a C++ expression as a C expression is insufficient.

For example, C++17 sequences `f(i++, i++)` arguments without interleaving their
evaluations, while naïvely reproducing that call in C can introduce undefined
behavior. Select and document a permitted argument order, compute both
arguments into temporaries, then call `f`. Preserve short-circuiting with control
flow. A test for unspecified order accepts the permitted result set rather than
the accidental choice of one reference compiler.

The verifier checks IDs, types, calls, record members, control-flow references,
and instruction invariants before emission. No AST pointers or host-memory
layouts cross the protocol. Symbol identities use resolved signatures and
project-relative file identity for internal linkage, never the output directory
or absolute checkout root. Source-side layout decisions must match the target
validated by NeverC. Source locations initially support diagnostics rather than
full source-level debugging.

## Production command contract

The experimental core implementation accepts:

```sh
neverc translate --from cpp input.cpp -o generated/input.nc -- -std=c++17
neverc translate --from cpp input.cpp --out-dir generated -- -std=c++17
neverc translate --from cpp input.cpp --check --report report.json -- -std=c++17
neverc generated/input.nc -o app
neverc generated/module.nc -c -o module.o
```

Project and math profiles add explicit source selection and build context:

```sh
neverc translate --from cpp --profile cpp-project-v1 \
  --project-root project --compdb project/compile_commands.json \
  project/src/a.cpp project/src/b.cpp --out-dir generated
neverc translate --from cpp --profile cpp-math-v1 \
  --project-root project --compdb project/compile_commands.json \
  --target arm64-apple-macosx15.0.0 \
  project/src/math.cpp --check --report report.json
```

Both require `--project-root` and `--compdb`, and accept only `--out-dir` or
`--check`. Resolve ambiguous database entries with repeatable
`--compdb-entry FILE=INDEX`, where INDEX is the zero-based database array index.
`--compdb-quoting gnu|windows` controls command/response tokenization and defaults
to the build host's convention. The [context contract](p3-compilation-context.md)
defines accepted arguments and input limits.

The driver always invokes its current NeverC executable. The old `--frontend`
and `--cpp-sdk` options are rejected; `NEVERC_CPP_FRONTEND` and `NEVERC_CPP_SDK`
do not select external tools or SDK inputs. The public command uses the same
built-in frontend and catalog in development and installed builds.

`--from` is required. `cpp` defaults to `cpp-core-v1`; `--profile` selects an
available profile explicitly. Other language names and unimplemented profiles
fail. Core takes exactly one source; project profiles take an explicit list.
Arguments after `--` are parsed as source
compiler options; they are not executed as a shell command. Only options
explicitly accepted by the selected profile may affect analysis. Reject target
changes, packing/layout changes, fast-math, plugins, arbitrary frontend actions,
and options whose semantics would be applied to only one compiler.

Exactly one of `-o`, `--out-dir`, and `--check` is required. `-o` writes a single
`.nc`, `.nc.map.json`, and `.nc.manifest.json`. `--out-dir` owns a new directory
with generated source files, any generated headers/helpers, a source map, and
`translate-manifest.json`. `--check` publishes no generated source or sidecars.
An explicit `--report` is allowed in every mode and is the only permitted
published output of check mode.
For `--out-dir`, a report placed directly inside that new directory is published
with a successful artifact set. Use an external report path to retain diagnostics
on failure: the failed invocation does not create the generated output directory.

Normal and check modes run the same analysis, subset validation, lowering, IR
verification, temporary emission, and NeverC syntax/object-code validation.
Both use the same recorded target and normalized options. They do not
execute the user's program. A module without `main` receives no synthetic entry
point. Successful translation establishes completion of those checks; executable
and library link/run evidence is additionally required by profile release tests.

Math additionally loads the immutable built-in SDK catalog, checks actual
consumed-header and declaration evidence, verifies the precise installed runtime
header and embedded implementation identities, and links a separate capability
probe. Required mappings fail with `TR0403` when `-fno-builtin-std` disables their
runtime; math code requiring no final mapping remains admissible.
Generated-code validation isolates implicit compiler environment/configuration;
the recorded recipe includes `--no-default-config`. Neither a header search
environment variable nor a default configuration may replace an approved input.

Reject an invocation when any selected owned declaration or definition is
unsupported. Do not skip functions, retain an undeclared foreign dependency,
or publish placeholder bodies. Stop dependent stages after invalid input while
collecting independent diagnostics where useful.

## Target and build context

P2 derives one native target and checks that both frontends agree on OS,
architecture, endianness, integer widths, pointer widths, and relevant layout.
Generated source contains compile-time guards for representable target/data-model
requirements; the manifest also records the exact triple and deployment/build
requirements. An incompatible compilation fails and requires retranslating the
source. P0's macOS arm64 probe target is `arm64-apple-macosx15.0.0` using SDK 15.5;
that experiment does not advertise cross-target support or a minimum supported
release OS for the built-in frontend. A NeverC build running under Rosetta may
instead have an x86_64 default target: the driver must explicitly request its validated
target from the frontend rather than infer arm64 from the machine's hardware.

P3A implements explicit `--target` and compilation-database selection. Resolve
paths relative to each entry's `directory`; prefer its structured `arguments`.
Parse supported response files and record their hashes. Never execute stored
commands. Multiple configurations require explicit selection. Conflicting target
options, missing generated headers, incompatible declarations, unresolved
non-C-library dependencies, and unsupported flags fail. A compilation database
is not a complete link graph.

An omitted project target defaults to NeverC's normalized native target. Math
requires an explicit x86_64/arm64 macOS 15.0 target and the approved Clang 20.1.8,
libc++ 200100, macOS 15.5 header distribution embedded in NeverC. Equivalent
Darwin spellings are checked by their effective deployment version. Compilation-database target
options must agree; SDK/resource/sysroot overrides are rejected. The
[SDK/runtime contract](p3-math-capabilities.md) defines separate SDK dependency
identities, the approved catalog and canonical implementation fingerprints.

Each project translation unit is analyzed independently. The shared merger
checks signatures, internal/C linkage, definition closure, source snapshots,
and bounded ODR evidence, then emits one `translated.nc` plus `translated.h`.
It does not concatenate original source files. The [project contract](p3-project-emission.md)
describes conservative repeated-definition restrictions and scalar ABI scope.

## Artifact publication and recovery

Reserve and check every destination, including report and sidecars, before
writing. Refuse existing files and directories and input/output aliases,
including aliases through symlinks. Use exclusive creation at publication time
as well, so a concurrently created path is not overwritten.

Stage in a sibling location on the destination filesystem. A new output
directory is published by a no-overwrite rename. Single-file outputs publish the
manifest last; it is the completion marker. Roll back only files newly created
by this invocation on ordinary failure or cancellation. A report outside the
output directory is an independent publication and is not covered by the
directory rename. Do not claim a filesystem-wide atomic transaction.

Cancellation is scoped to the entire translation invocation. Check it between
stages and before publication/commit, request termination of any running frontend
or validation process, and roll back newly published files before retaining an
external failure report. Termination targets the owned child only. The runner
polls for at most 200 ms to reap a terminated child; an OS process that remains
unreapable cannot block artifact cleanup. The diagnostic records that condition
and any Windows process handle is closed. `TR0005` identifies explicit cancellation. If the final manifest
or directory rename already committed successfully, the completed artifact set
remains valid.

After a crash, a staging directory or source/map without its final manifest may
remain. Such an output is incomplete. A later invocation refuses those paths
rather than trusting or overwriting them; inspect and remove the incomplete
artifacts or choose new destinations before retrying. A manifest with mismatched
generated hashes likewise cannot validate an edited or partial output.

## Versioned data contracts

Protocol, manifest, report, and source map have independent schema versions.
Their initial major version is 1. Unknown incompatible major versions fail
explicitly; compatible minor additions may add optional fields without changing
existing meanings. P1 owns concrete serialization and schema validation tests.
The following information is mandatory; no prototype JSON can substitute for it.

| Format | Required information |
| --- | --- |
| Internal frontend semantic protocol | Protocol version, frontend name/version/build ID, requested profile, normalized source options, target/data model, source/dependency identities and hashes, typed IR and references, source locations, mapping operation IDs/preconditions, diagnostics/status. |
| Manifest | Manifest/profile version, input/dependency hashes, normalized options, frontend and NeverC build IDs, target/data model, mappings, required headers/modules/options, exports/linkage, generated file paths and SHA-256 hashes, reproducible compilation recipe. Empty mapping/dependency requirements are recorded explicitly for the scalar profile. |
| Report | Report version, profile, stage, success/failure status, diagnostics, applied mappings, unresolved mapping gaps. Operational timings and machine-specific diagnostic context may appear here, not in deterministic manifests. |
| Source map | Map version, project-relative input identities, generated-relative file identities and hashes, generated location ranges mapped to original file/line/column ranges, semantic symbol or operation identity where available. |

Use SHA-256 over exact file bytes. Canonicalize deterministic object-key and
collection ordering. Generated artifacts use project-relative paths; diagnostic
display may use an absolute path. Do not include timestamps, elapsed times, or
build-root paths in deterministic metadata. Record macro and supported
response-file inputs. Reject `__DATE__`, `__TIME__`, `__TIMESTAMP__`, `__FILE__`,
`__BASE_FILE__`, and `__FILE_NAME__` in the current profiles. Semantic IDs
must survive relocation of that root. Reproducibility requires the same declared
inputs, profile, toolchain, and target.

Compilation databases and response files also retain their raw
byte hashes. Rewriting their absolute paths during relocation changes those
declared inputs and their provenance hashes intentionally. Normalized context
IDs and generated code remain stable for equivalent supported contexts; complete
manifest byte identity is promised only when the declared raw inputs also match.
Source and owned/SDK header hashes always describe exact consumed bytes.
The SDK catalog hash identifies the embedded inventory and metadata; manifests
record `sdk.delivery: "builtin"`, with no machine-specific SDK roots or descriptor.

## Stable diagnostic meanings

These code meanings are frozen for initial production implementation. Prose may
improve. Every diagnostic carries `code`, original `file`, `line`, `column`,
`construct`, `reason`, and `guidance`. Invocation/process errors with no source
location use the input path at 1:1 as a diagnostic anchor; their `construct`
identifies invocation/process context rather than implying a token error there.
Reports also identify the failing stage. Success exits zero; failures exit
nonzero. Callers should use the structured code rather than parse prose.

| Code | Meaning |
| --- | --- |
| `TR0001` | Invalid command syntax or conflicting output options. |
| `TR0002` | Unsupported input language. |
| `TR0003` | Unsupported or unimplemented profile. |
| `TR0004` | Unsupported/conflicting source compiler option. |
| `TR0005` | Explicit translation cancellation before publication completed. |
| `TR0101` | Frontend or required pinned SDK unavailable or unusable. |
| `TR0102` | Frontend process termination/failure. |
| `TR0103` | Invalid frontend protocol or incompatible protocol/version. |
| `TR0201` | Unsupported source construct. |
| `TR0202` | Invalid C++ source according to the pinned frontend. |
| `TR0203` | Unresolved source/library dependency. |
| `TR0204` | Target or data-model mismatch. |
| `TR0301` | Invalid or inconsistent typed IR. |
| `TR0401` | Generated NeverC syntax validation failed. |
| `TR0402` | Generated NeverC object-code validation failed. |
| `TR0403` | Required runtime capability unavailable. |
| `TR0404` | Required runtime link probe failed. |
| `TR0501` | Output collision or input/output alias. |
| `TR0502` | Artifact I/O or publication failure. |
| `TR0601` | Invalid, unavailable, oversized, or changed compilation database/context input. |
| `TR0602` | Missing, ambiguous, inconsistent, or unowned source/configuration selection. |
| `TR0603` | Invalid, unavailable, cyclic, oversized, or changed response-file expansion. |

## Delivery gates

P0 selects and measures the full frontend and SDK arrangement, including the
sequencing experiment. P1 establishes the driver/IR/artifact infrastructure with
synthetic semantic input. P2 ships the experimental scalar profile only after
complete positive/negative, differential `-O0`/`-O2`, deterministic-input,
failure-publication, and installed-frontend checks. Built-in delivery additionally
requires a symbol-isolated static archive, installation without external
Clang/LLVM or SDK inputs, embedded-header provenance checks, and the same complete
profile regressions. P3A adds bounded project
support; P3B adds the explicitly verified math operations. String/vector and
later languages remain separate profile work. The parent roadmap remains open
until its P0–P3 acceptance and linked follow-up conditions are met.
