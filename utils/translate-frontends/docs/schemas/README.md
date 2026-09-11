# Translation artifact schemas

These version-1 schemas describe core, project, and gated math artifacts independently of the
[internal frontend protocol](../protocol.md). A change to the frontend protocol does
not automatically change manifest, report, or source-map versions. Consumers
must reject incompatible major versions.

- `frontend-request.schema.json`: private process request, including absolute
  process-context input paths; never published as an artifact.
- `manifest.schema.json`: completion marker, normalized toolchain/input/output
  metadata, required runtime facilities and an object compilation recipe.
- `source-map.schema.json`: one-based inclusive generated line ranges mapped to
  original source locations. Check `generated_sha256` before using a map after
  manual edits. This is diagnostic mapping, not a debugger format.
- `report.schema.json`: completed or failed pipeline stage and stable structured
  diagnostics. Reports may contain machine-specific error detail and are not
  part of reproducible semantic output.

Core v2 manifests require `target.carrier_layout` and `record_layouts`.
The compiler independently verifies carrier and record layout evidence and
emits static layout assertions. V1 profiles reject these fields; older
experimental v2 output must be regenerated with the current built-in frontend.

The production C++ verifier additionally validates semantic references, types,
operators, identifier namespaces, target consistency, normalized paths, and
resource limits. JSON Schema does not express those invariants by itself.

For single-file output, the manifest is published last. If a process is forcibly
killed or the machine stops before it appears, the source and map are incomplete
output: inspect them, move or remove only files belonging to that invocation,
and rerun with unused output paths. Sibling `.neverc-translate-*` directories can
be removed after confirming their process is no longer running. Ordinary failures
and handled cancellation clean temporary work and newly published files. Multiple
unrelated output files are not an atomic transaction. New output directories are
published with an exclusive rename.

Math manifests require a separate floating-point contract, the approved source
SDK distribution/catalog with `delivery: "builtin"`, consumed named-root
dependencies, and exact runtime capability evidence. Mappings contain resolved declaration identities and typed
signatures; their emitted symbols are selected by the consumer's fixed table.
Core/project manifests and reports continue to require an empty mapping list.
A schema-valid math artifact is not itself capability approval: the driver must
validate SDK bytes, runtime identities, generated code, and linkage first.

The private request supplies only the built-in SDK distribution ID and catalog
hash. SDK roots identify embedded virtual trees; external descriptor paths and
root overrides are not accepted. The driver always runs the current NeverC
executable’s internal frontend mode.
