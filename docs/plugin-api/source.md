**Languages**: [English](source.md) | [简体中文](source.zh-CN.md) | [繁體中文](source.zh-TW.md) | [日本語](source.ja.md) | [한국어](source.ko.md) | [Français](source.fr.md) | [Deutsch](source.de.md) | [Español](source.es.md) | [Italiano](source.it.md) | [Русский](source.ru.md) | [العربية](source.ar.md)

[← NeverC Plugin ABI](README.md)

# NeverC Plugin Source and I/O API

[`PluginSource.h`] publishes two tables. `NevercIOAPI` is the file system:
virtual-file providers, reads, directory walks, output sinks, and dependency
records. `NevercSourceLocationAPI` maps compiler positions back to files,
lines, and spelled text. Between them a plugin can serve a header that exists
only in memory, resolve a macro expansion to its spelling location, or write a
side-band output that participates in the build's durability accounting.

## Interfaces

```c
#include "neverc/Plugin/PluginSource.h"
```

| Interface | Table | Version macros |
|---|---|---|
| `NEVERC_INTERFACE_IO_{HIGH,LOW}` | `NevercIOAPI` | `NEVERC_IO_API_MAJOR` / `_MINOR` |
| `NEVERC_INTERFACE_SOURCE_LOCATION_{HIGH,LOW}` | `NevercSourceLocationAPI` | `NEVERC_SOURCE_LOCATION_API_MAJOR` / `_MINOR` |

`NEVERC_SOURCE_API_MAJOR` and `_MINOR` are aliases of the source-location
pair.

## The three source phases

| Phase | Policy | Meaning |
|---|---|---|
| `neverc.source.resolve_input` | OBSERVABLE, INTERCEPTABLE | Turn a driver input into a source input |
| `neverc.source.open` | + REPLACEABLE | Produce the source unit for an input |
| `neverc.source.after_open` | OBSERVABLE | Notification that a unit is available |

Because `neverc.source.open` is replaceable, a provider can hand back a unit
whose bytes it synthesized, which is the supported way to inject generated
code without touching the disk.

## Virtual file system providers

A VFS provider claims a path prefix and answers the four questions the
compiler asks about a file.

```c
typedef struct NevercVFSProviderDescriptor {
  NevercABITableHeader Header;
  NevercStringView ProviderID;
  NevercStringView RoutePrefix;
  NevercBool Deterministic;
  NevercBool Cacheable;
  uint64_t Reserved;
  NevercVFSPathPredicateFn MatchesPath;
  NevercVFSProviderStatusFn Status;
  NevercVFSProviderOpenReadFn OpenRead;
  NevercVFSProviderReadDirectoryFn ReadDirectory;
  NevercVFSProviderCanonicalizeFn Canonicalize;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercVFSProviderDescriptor;
```

Each callback fills a result whose `Disposition` says whether the provider
handled the request:

```c
static NevercStatus NEVERC_CALL
open_read(NevercTaskHandle Task, NevercStringView Path, void *UserData,
          NevercVFSOpenReadResult *OutResult) {
  static const char Header[] = "#define GENERATED 1\n";
  if (!path_matches(Path)) {
    OutResult->Disposition = NEVERC_VFS_RESULT_NOT_HANDLED;
    return neverc_status_ok();
  }
  OutResult->Disposition   = NEVERC_VFS_RESULT_HANDLED;
  OutResult->Status.Type   = NEVERC_VFS_FILE_REGULAR;
  OutResult->Status.Size   = sizeof(Header) - 1;
  OutResult->Content.Data  = (const uint8_t *)Header;
  OutResult->Content.Length = sizeof(Header) - 1;
  OutResult->Content.NullTerminated = NEVERC_TRUE;
  return neverc_status_ok();
}
```

Returning `NEVERC_VFS_RESULT_NOT_HANDLED` falls through to the next provider
and finally to the real file system. File types are `NEVERC_VFS_FILE_UNKNOWN`,
`REGULAR`, `DIRECTORY`, `SYMLINK`, and `OTHER`.

Register during `Register`:

```c
IO->RegisterVFSProvider(IO->Context, RegistrarContext, &Descriptor);
```

For a single in-memory file that only needs to exist for one session, skip the
provider entirely:

```c
IO->AddMemoryFile(IO->Context, Session, SV("/virtual/config.h"),
                  Content, ModificationTime);
```

[`pluginsdk/examples/VirtualHeaderPlugin.c`] is a complete working provider.

## Reading files

```c
NevercVFSStatus Status;
IO->Stat(IO->Context, Task, Path, &Status);

NevercFileHandle File;
IO->OpenFileForRead(IO->Context, Task, Path, &File);

NevercBufferHandle Buffer;
IO->ReadFile(IO->Context, Task, File, /*Offset=*/0, /*Length=*/Status.Size,
             &Buffer);

NevercBufferView View;
IO->GetBufferView(IO->Context, Task, Buffer, &View);
/* View.Data / View.Length / View.NullTerminated */

IO->ReleaseBuffer(IO->Context, Task, Buffer);
IO->CloseFile(IO->Context, Task, File);
```

`CopyBuffer` turns bytes you own into a host buffer, `Canonicalize` resolves a
path, and `GetWorkingDirectory` / `SetWorkingDirectory` handle the task's
current directory. Directories are walked with `OpenDirectory`,
`ReadDirectory` (which sets `OutHasEntry` to `NEVERC_FALSE` at the end), and
`CloseDirectory`.

The I/O error codes are reported in `NevercStatus.Detail`:
`NEVERC_IO_ERROR_NOT_FOUND`, `PERMISSION_DENIED`, `NOT_DIRECTORY`,
`IS_DIRECTORY`, `INVALID_PATH`, and `IO`.

## Writing outputs

Outputs are transactional. You open a sink, write, then finish to obtain a
seal — a size and a 32-byte digest that the build system can verify.

```c
NevercOutputSinkHandle Sink;
IO->BeginFileOutput(IO->Context, Task, SV("out.json"), /*SizeBudget=*/0, &Sink);
IO->OutputWrite(IO->Context, Task, Sink, Bytes);
IO->OutputMetadataSet(IO->Context, Task, Sink, SV("content-type"),
                      SV("application/json"));

NevercOutputSeal Seal = {0};
Seal.Header = (NevercABITableHeader){sizeof(Seal), NEVERC_IO_API_MAJOR,
                                     NEVERC_IO_API_MINOR, 0};
IO->OutputFinish(IO->Context, Task, Sink, &Seal);
```

| Function | Purpose |
|---|---|
| `BeginMemoryOutput` | Sink backed by memory, named logically |
| `BeginFileOutput` | Sink that lands at a final path atomically |
| `BeginStreamOutput` | Sink on `NEVERC_OUTPUT_STREAM_STDOUT` or `_STDERR` |
| `OutputWrite`, `OutputWriteAt` | Append, or write at an offset |
| `OutputTell`, `OutputTruncate` | Position and size control |
| `OutputMetadataSet` | Attach a key/value pair to the output |
| `OutputFinish` | Seal the output and produce `NevercOutputSeal` |
| `OutputAbort` | Discard everything written |
| `OutputGetSummary` | Inspect state, flags, size, digest at any time |

`NevercOutputSummary.State` moves through `NEVERC_OUTPUT_OPEN`, `FINISHED`,
`COMMITTED`, `ABORTED`, or `FAILED_PARTIAL`, and `Flags` records
`PUBLISHED`, `DURABLE`, `MAY_BE_PARTIAL`, `RECOVERY_REQUIRED`, and
`DURABILITY_UNCONFIRMED`. Those flags are the same information the driver
surfaces in `NevercStatus.Flags`, so a crash mid-write is distinguishable
from a clean failure.

A `SizeBudget` of zero means unbounded; a non-zero budget makes an overrun
fail with `NEVERC_STATUS_RESOURCE_EXHAUSTED` rather than filling a disk.

## Recording dependencies

If a plugin reads something the build system should track, say so. Otherwise
an incremental build will not rebuild when that input changes.

```c
NevercDependencyDescriptor Dependency = {0};
Dependency.Header = (NevercABITableHeader){sizeof(Dependency),
                                           NEVERC_IO_API_MAJOR,
                                           NEVERC_IO_API_MINOR, 0};
Dependency.CanonicalPath = SV("/etc/mytool/rules.txt");
Dependency.ContentDigest = Digest;
Dependency.Kind          = NEVERC_INPUT_DEPENDENCY_RESOURCE;
Dependency.System        = NEVERC_FALSE;
Dependency.ProviderID    = SV("com.example.myplugin");

NevercDependencyHandle Handle;
IO->RecordDependency(IO->Context, Task, &Dependency, &Handle);
```

Kinds are `NEVERC_INPUT_DEPENDENCY_SOURCE`, `INCLUDE`, `MODULE`, `RESOURCE`,
`TOOL`, and `PLUGIN`.

## Source locations

A `NevercSourceLocation` is opaque. The location table turns it into
something you can print or compare.

```c
NevercSourceLocationInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info),
                                     NEVERC_SOURCE_LOCATION_API_MAJOR,
                                     NEVERC_SOURCE_LOCATION_API_MINOR, 0};
Source->GetLocationInfo(Source->Context, Task, Location, &Info);
/* Info.Kind is NEVERC_SOURCE_LOCATION_FILE or _MACRO;
   Info.FileOffset, Info.Line, Info.Column follow. */
```

Four transforms move between the views of a location, all sharing the
`NevercTransformSourceLocationFn` signature:

| Function | Returns |
|---|---|
| `GetSpellingLocation` | Where the token's characters are actually written |
| `GetExpansionLocation` | Where a macro expansion appears in the source |
| `GetFileLocation` | The nearest file location |
| `GetIncludeLocation` | The `#include` that pulled the file in |
| `GetTokenEnd` | One past the last character of the token |

`GetPresumedLocation` applies `#line` directives and yields a filename, line,
column, and include location. `GetLocationFile` plus `GetFileInfo` give the
canonical path, size, modification time, unique ID, and whether the file is
user, system, or extern-C system:

```c
typedef struct NevercFileInfo {
  NevercABITableHeader Header;
  NevercStringView Path;
  NevercStringView CanonicalPath;
  uint64_t Size;
  int64_t ModificationTime;
  NevercFileUniqueID UniqueID;      /* {Device, File} */
  NevercFileCharacteristic Characteristic;
  NevercBool NamedPipe;
} NevercFileInfo;
```

Ranges are read with `GetRangeInfo` (which reports `Begin`, `End`, and whether
the range is `NEVERC_SOURCE_RANGE_CHARACTER` or `_TOKEN`), and the bytes
themselves with `GetSourceText` or `GetCharacterData`.

When you need many locations at once — a diagnostic pass over a whole
function, say — use the batch form instead of a call per location:

```c
Source->GetLocationInfoBatch(Source->Context, Task, Locations, LocationCount,
                             OutInfos, OutInfoCapacity);
```

## Source units

The phase-level view of an input and its bytes:

```c
NevercSourceInputInfo Input = {0};
Source->GetSourceInput(Source->Context, Frame, Frame->Input, &Input);
/* Input.Path, .Kind (FILE or BUFFER), .Language, .System, .Preprocessed */
```

A provider for `neverc.source.open` answers with a memory-backed unit:

```c
NevercMemorySourceUnitDescriptor Unit = {0};
Unit.Header = (NevercABITableHeader){sizeof(Unit),
                                     NEVERC_SOURCE_LOCATION_API_MAJOR,
                                     NEVERC_SOURCE_LOCATION_API_MINOR, 0};
Unit.LogicalPath      = SV("/virtual/generated.c");
Unit.CanonicalIdentity = SV("com.example:generated:v1");
Unit.Content          = Bytes;
Unit.ProviderID       = SV("com.example.myplugin");
Unit.Deterministic    = NEVERC_TRUE;
Unit.Cacheable        = NEVERC_TRUE;

NevercArtifactHandle Output;
Source->CreateMemorySourceUnit(Source->Context, Frame, Frame->Input, &Unit,
                               &Output);
```

`CanonicalIdentity` is what the cache keys on, so it must change whenever the
content changes. `GetSourceUnit` reads a unit back and additionally reports
`MemoryBacked`.

## Rules

- Buffers from `ReadFile`, `CopyBuffer`, and `PathToBuffer` are host-owned;
  release each one with `ReleaseBuffer`.
- Every `OpenFileForRead` needs a `CloseFile`; every `OpenDirectory` needs a
  `CloseDirectory`; every output sink needs an `OutputFinish` or
  `OutputAbort`.
- Views inside `NevercFileInfo`, `NevercVFSStatus`, and location results are
  borrowed for the callback.
- A VFS provider callback runs on the task thread and must not call back into
  the compiler; answer from data you already have.
- Declare `Deterministic` and `Cacheable` truthfully. A provider that reads
  the clock or the environment and claims determinism will produce a
  poisoned build cache.
- `AddMemoryFile` is session-scoped; a provider is the right tool when the
  content depends on the task.

See [`PluginSource.h`] for the normative declarations and
[`pluginsdk/examples/VirtualHeaderPlugin.c`] for a complete provider.

<!-- reference links -->
[`pluginsdk/examples/VirtualHeaderPlugin.c`]: ../../pluginsdk/examples/VirtualHeaderPlugin.c
[`PluginSource.h`]: ../../neverc/include/neverc/Plugin/PluginSource.h
