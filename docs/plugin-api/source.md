# Source and I/O plugin API

The first public plugin ABI exposes source input, virtual files, dependencies,
and compiler outputs through `PluginSource.h`. All paths are normalized VFS
paths and all handles are scoped to the current `TranslationUnit` task.

## Source phases

The stable source pipeline is:

1. `neverc.source.resolve_input` validates and normalizes the requested input.
2. `neverc.source.open` opens it through the composed host/plugin VFS.
3. `neverc.source.after_open` publishes a read-only event for the verified
   `SourceUnit`.

`resolve_input` is observable and interceptable. `open` is also replaceable.
The host verifies every replacement before publishing it as a `SourceUnit`.
Plugins cannot replace `after_open`.

## VFS providers

Query `NevercIOAPI` during plugin registration and call
`RegisterVFSProvider`. A provider first answers `MatchesPath`, then implements
the operations it owns. Returning `NEVERC_VFS_RESULT_NOT_HANDLED` delegates to
the next provider; returning `HANDLED` makes malformed status or content a hard
error rather than silently falling back.

Buffers returned by a provider are borrowed only for the callback. NeverC
copies accepted bytes into task-owned storage. Providers must declare whether
their result is deterministic and cacheable.

The buildable
[`VirtualHeaderPlugin.c`](../../pluginsdk/examples/VirtualHeaderPlugin.c)
example supplies an in-memory header without bypassing the host VFS.

## Output sinks and dependencies

File and memory outputs use the same transactional sink:

- write to a candidate;
- call finish to make it eligible for verification;
- let the sealed host gate verify it;
- atomically commit on task success, or abort on any error or cancellation.

Plugins never publish by writing directly to the destination path. Streaming
destinations that cannot be rolled back reject transformations requiring an
atomic candidate. Dependency records use normalized VFS identities so native
and plugin-provided files have the same provenance and cache semantics.

## Safety rules

- Do not retain source, file, buffer, sink, or task handles after the callback.
- Treat `NevercStringView` and `NevercByteView` as length-delimited views.
- Use the host allocator when data must outlive a callback.
- Do not use host filesystem APIs behind the VFS contract.
- Check cancellation before expensive provider work.
