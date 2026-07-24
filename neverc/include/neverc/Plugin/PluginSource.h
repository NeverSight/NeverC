/*===-- PluginSource.h - NeverC source and IO plugin C ABI --------- C ---===*/

#ifndef NEVERC_PLUGIN_PLUGINSOURCE_H
#define NEVERC_PLUGIN_PLUGINSOURCE_H

#include "neverc/Plugin/PluginCore.h"
#include "neverc/Plugin/PluginPhaseSchema.h" /* IWYU pragma: export */

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_IO_API_MAJOR UINT16_C(1)
#define NEVERC_IO_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_IO_HIGH UINT64_C(0x4e4350494f000001)
#define NEVERC_INTERFACE_IO_LOW UINT64_C(0x0000000000000001)

#define NEVERC_SOURCE_LOCATION_API_MAJOR UINT16_C(1)
#define NEVERC_SOURCE_LOCATION_API_MINOR UINT16_C(0)
#define NEVERC_SOURCE_API_MAJOR NEVERC_SOURCE_LOCATION_API_MAJOR
#define NEVERC_SOURCE_API_MINOR NEVERC_SOURCE_LOCATION_API_MINOR
#define NEVERC_INTERFACE_SOURCE_LOCATION_HIGH UINT64_C(0x4e43505352430001)
#define NEVERC_INTERFACE_SOURCE_LOCATION_LOW UINT64_C(0x0000000000000001)

#define NEVERC_ARTIFACT_SOURCE_INPUT_HIGH \
  NEVERC_PHASE_SOURCE_RESOLVE_INPUT_INPUT_HIGH
#define NEVERC_ARTIFACT_SOURCE_INPUT_LOW \
  NEVERC_PHASE_SOURCE_RESOLVE_INPUT_INPUT_LOW
#define NEVERC_ARTIFACT_SOURCE_UNIT_HIGH \
  NEVERC_PHASE_SOURCE_OPEN_OUTPUT_HIGH
#define NEVERC_ARTIFACT_SOURCE_UNIT_LOW \
  NEVERC_PHASE_SOURCE_OPEN_OUTPUT_LOW
#define NEVERC_ARTIFACT_SOURCE_OUTPUT_SINK_HIGH \
  UINT64_C(0x4e43415246524e01)
#define NEVERC_ARTIFACT_SOURCE_OUTPUT_SINK_LOW \
  UINT64_C(0x0000000000001003)

typedef uint32_t NevercSourceInputKind;
#define NEVERC_SOURCE_INPUT_FILE UINT32_C(1)
#define NEVERC_SOURCE_INPUT_BUFFER UINT32_C(2)

typedef uint32_t NevercSourceLocationKind;
#define NEVERC_SOURCE_LOCATION_FILE UINT32_C(1)
#define NEVERC_SOURCE_LOCATION_MACRO UINT32_C(2)

typedef uint32_t NevercSourceRangeKind;
#define NEVERC_SOURCE_RANGE_CHARACTER UINT32_C(1)
#define NEVERC_SOURCE_RANGE_TOKEN UINT32_C(2)

typedef uint32_t NevercFileCharacteristic;
#define NEVERC_FILE_CHARACTERISTIC_USER UINT32_C(0)
#define NEVERC_FILE_CHARACTERISTIC_SYSTEM UINT32_C(1)
#define NEVERC_FILE_CHARACTERISTIC_EXTERN_C_SYSTEM UINT32_C(2)

typedef uint32_t NevercVFSFileType;
#define NEVERC_VFS_FILE_UNKNOWN UINT32_C(0)
#define NEVERC_VFS_FILE_REGULAR UINT32_C(1)
#define NEVERC_VFS_FILE_DIRECTORY UINT32_C(2)
#define NEVERC_VFS_FILE_SYMLINK UINT32_C(3)
#define NEVERC_VFS_FILE_OTHER UINT32_C(4)

typedef uint32_t NevercVFSResultDisposition;
#define NEVERC_VFS_RESULT_HANDLED UINT32_C(1)
#define NEVERC_VFS_RESULT_NOT_HANDLED UINT32_C(2)

typedef uint64_t NevercIOErrorCode;
#define NEVERC_IO_ERROR_NONE UINT64_C(0)
#define NEVERC_IO_ERROR_NOT_FOUND UINT64_C(1)
#define NEVERC_IO_ERROR_PERMISSION_DENIED UINT64_C(2)
#define NEVERC_IO_ERROR_NOT_DIRECTORY UINT64_C(3)
#define NEVERC_IO_ERROR_IS_DIRECTORY UINT64_C(4)
#define NEVERC_IO_ERROR_INVALID_PATH UINT64_C(5)
#define NEVERC_IO_ERROR_IO UINT64_C(6)

typedef uint32_t NevercOutputKind;
#define NEVERC_OUTPUT_MEMORY UINT32_C(1)
#define NEVERC_OUTPUT_FILE UINT32_C(2)
#define NEVERC_OUTPUT_STREAM UINT32_C(3)

typedef uint32_t NevercOutputState;
#define NEVERC_OUTPUT_OPEN UINT32_C(1)
#define NEVERC_OUTPUT_FINISHED UINT32_C(2)
#define NEVERC_OUTPUT_COMMITTED UINT32_C(3)
#define NEVERC_OUTPUT_ABORTED UINT32_C(4)
#define NEVERC_OUTPUT_FAILED_PARTIAL UINT32_C(5)

typedef uint32_t NevercOutputStream;
#define NEVERC_OUTPUT_STREAM_STDOUT UINT32_C(1)
#define NEVERC_OUTPUT_STREAM_STDERR UINT32_C(2)

typedef uint64_t NevercOutputFlags;
#define NEVERC_OUTPUT_FLAG_NONE UINT64_C(0)
#define NEVERC_OUTPUT_FLAG_PUBLISHED UINT64_C(1)
#define NEVERC_OUTPUT_FLAG_DURABLE UINT64_C(2)
#define NEVERC_OUTPUT_FLAG_MAY_BE_PARTIAL UINT64_C(4)
#define NEVERC_OUTPUT_FLAG_RECOVERY_REQUIRED UINT64_C(8)
#define NEVERC_OUTPUT_FLAG_DURABILITY_UNCONFIRMED UINT64_C(16)

typedef uint32_t NevercInputDependencyKind;
#define NEVERC_INPUT_DEPENDENCY_SOURCE UINT32_C(1)
#define NEVERC_INPUT_DEPENDENCY_INCLUDE UINT32_C(2)
#define NEVERC_INPUT_DEPENDENCY_MODULE UINT32_C(3)
#define NEVERC_INPUT_DEPENDENCY_RESOURCE UINT32_C(4)
#define NEVERC_INPUT_DEPENDENCY_TOOL UINT32_C(5)
#define NEVERC_INPUT_DEPENDENCY_PLUGIN UINT32_C(6)

NEVERC_ABI_PACK_BEGIN

typedef NevercSourceLocationHandle NevercSourceLocation;
typedef NevercSourceRangeHandle NevercSourceRange;
typedef NevercHandle NevercFileHandle;
typedef NevercHandle NevercBufferHandle;
typedef NevercHandle NevercDirectoryCursorHandle;
typedef NevercHandle NevercOutputSinkHandle;
typedef NevercHandle NevercOutputSealHandle;
typedef NevercHandle NevercDependencyHandle;

typedef struct NevercOutputSealList {
  const NevercOutputSealHandle *Data;
  uint64_t Count;
  uint64_t ElementStride;
} NevercOutputSealList;

typedef struct NevercSourceLocationInfo {
  NevercABITableHeader Header;
  NevercSourceLocationKind Kind;
  uint32_t Reserved;
  uint64_t FileOffset;
  uint32_t Line;
  uint32_t Column;
} NevercSourceLocationInfo;

typedef struct NevercSourceRangeInfo {
  NevercABITableHeader Header;
  NevercSourceLocation Begin;
  NevercSourceLocation End;
  NevercSourceRangeKind Kind;
  uint32_t Reserved;
} NevercSourceRangeInfo;

typedef struct NevercBufferView {
  NevercABITableHeader Header;
  const uint8_t *Data;
  uint64_t Length;
  NevercBool NullTerminated;
  uint32_t Reserved;
} NevercBufferView;

typedef struct NevercPresumedLocation {
  NevercABITableHeader Header;
  NevercStringView Filename;
  uint32_t Line;
  uint32_t Column;
  NevercSourceLocation IncludeLocation;
} NevercPresumedLocation;

typedef struct NevercFileUniqueID {
  uint64_t Device;
  uint64_t File;
} NevercFileUniqueID;

typedef struct NevercFileInfo {
  NevercABITableHeader Header;
  NevercStringView Path;
  NevercStringView CanonicalPath;
  uint64_t Size;
  int64_t ModificationTime;
  NevercFileUniqueID UniqueID;
  NevercFileCharacteristic Characteristic;
  NevercBool NamedPipe;
} NevercFileInfo;

typedef struct NevercSourceInputInfo {
  NevercABITableHeader Header;
  NevercStringView Path;
  NevercSourceInputKind Kind;
  uint32_t Language;
  NevercBool System;
  NevercBool Preprocessed;
} NevercSourceInputInfo;

typedef struct NevercMemorySourceUnitDescriptor {
  NevercABITableHeader Header;
  NevercStringView LogicalPath;
  NevercStringView CanonicalIdentity;
  NevercByteView Content;
  NevercStringView ProviderID;
  NevercBool System;
  NevercBool Deterministic;
  NevercBool Cacheable;
  uint32_t Reserved;
} NevercMemorySourceUnitDescriptor;

typedef struct NevercSourceUnitInfo {
  NevercABITableHeader Header;
  NevercStringView LogicalPath;
  NevercStringView CanonicalIdentity;
  NevercStringView ProviderID;
  uint64_t Size;
  NevercBool MemoryBacked;
  NevercBool System;
  NevercBool Deterministic;
  NevercBool Cacheable;
} NevercSourceUnitInfo;

typedef struct NevercVFSStatus {
  NevercABITableHeader Header;
  NevercVFSFileType Type;
  uint32_t Permissions;
  uint64_t Size;
  int64_t ModificationTime;
  NevercFileUniqueID UniqueID;
  NevercBool Local;
  uint32_t Reserved;
} NevercVFSStatus;

typedef struct NevercVFSDirectoryEntry {
  NevercABITableHeader Header;
  NevercStringView Path;
  NevercVFSFileType Type;
  uint32_t Reserved;
} NevercVFSDirectoryEntry;

typedef struct NevercVFSStatusResult {
  NevercABITableHeader Header;
  NevercVFSResultDisposition Disposition;
  uint32_t Reserved;
  NevercVFSStatus Status;
} NevercVFSStatusResult;

typedef struct NevercVFSOpenReadResult {
  NevercABITableHeader Header;
  NevercVFSResultDisposition Disposition;
  uint32_t Reserved;
  NevercVFSStatus Status;
  NevercBufferView Content;
} NevercVFSOpenReadResult;

typedef struct NevercVFSDirectoryResult {
  NevercABITableHeader Header;
  NevercVFSResultDisposition Disposition;
  uint32_t Reserved;
  const NevercVFSDirectoryEntry *Entries;
  uint64_t EntryCount;
} NevercVFSDirectoryResult;

typedef struct NevercVFSCanonicalPathResult {
  NevercABITableHeader Header;
  NevercVFSResultDisposition Disposition;
  uint32_t Reserved;
  NevercStringView Path;
} NevercVFSCanonicalPathResult;

typedef struct NevercOutputSeal {
  NevercABITableHeader Header;
  NevercOutputSealHandle Handle;
  NevercOutputKind Kind;
  uint32_t Reserved;
  uint64_t Size;
  uint8_t Digest[32];
} NevercOutputSeal;

typedef struct NevercOutputSummary {
  NevercABITableHeader Header;
  NevercOutputState State;
  NevercOutputKind Kind;
  NevercOutputFlags Flags;
  uint64_t Size;
  uint64_t PublicationGeneration;
  uint8_t Digest[32];
} NevercOutputSummary;

typedef struct NevercDependencyDescriptor {
  NevercABITableHeader Header;
  NevercStringView CanonicalPath;
  NevercByteView ContentDigest;
  NevercInputDependencyKind Kind;
  NevercBool System;
  NevercStringView ProviderID;
  uint64_t Reserved;
} NevercDependencyDescriptor;

typedef NevercStatus(NEVERC_CALL *NevercVFSPathPredicateFn)(
    NevercTaskHandle Task, NevercStringView Path, void *UserData,
    NevercBool *OutMatches);
typedef NevercStatus(NEVERC_CALL *NevercVFSProviderStatusFn)(
    NevercTaskHandle Task, NevercStringView Path, void *UserData,
    NevercVFSStatusResult *OutResult);
typedef NevercStatus(NEVERC_CALL *NevercVFSProviderOpenReadFn)(
    NevercTaskHandle Task, NevercStringView Path, void *UserData,
    NevercVFSOpenReadResult *OutResult);
typedef NevercStatus(NEVERC_CALL *NevercVFSProviderReadDirectoryFn)(
    NevercTaskHandle Task, NevercStringView Path, void *UserData,
    NevercVFSDirectoryResult *OutResult);
typedef NevercStatus(NEVERC_CALL *NevercVFSProviderCanonicalizeFn)(
    NevercTaskHandle Task, NevercStringView Path, void *UserData,
    NevercVFSCanonicalPathResult *OutResult);

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

typedef NevercStatus(NEVERC_CALL *NevercGetSourceLocationInfoFn)(
    void *Context, NevercTaskHandle Task, NevercSourceLocation Location,
    NevercSourceLocationInfo *OutInfo);
typedef NevercStatus(NEVERC_CALL *NevercTransformSourceLocationFn)(
    void *Context, NevercTaskHandle Task, NevercSourceLocation Location,
    NevercSourceLocation *OutLocation);
typedef NevercStatus(NEVERC_CALL *NevercGetSourceRangeInfoFn)(
    void *Context, NevercTaskHandle Task, NevercSourceRange Range,
    NevercSourceRangeInfo *OutInfo);
typedef NevercStatus(NEVERC_CALL *NevercGetSourceTextFn)(
    void *Context, NevercTaskHandle Task, NevercSourceRange Range,
    NevercBufferView *OutText);
typedef NevercStatus(NEVERC_CALL *NevercGetCharacterDataFn)(
    void *Context, NevercTaskHandle Task, NevercSourceLocation Location,
    NevercBufferView *OutData);
typedef NevercStatus(NEVERC_CALL *NevercGetSourceLocationInfoBatchFn)(
    void *Context, NevercTaskHandle Task,
    const NevercSourceLocation *Locations, uint64_t LocationCount,
    NevercSourceLocationInfo *OutInfos, uint64_t OutInfoCapacity);
typedef NevercStatus(NEVERC_CALL *NevercGetPresumedLocationFn)(
    void *Context, NevercTaskHandle Task, NevercSourceLocation Location,
    NevercPresumedLocation *OutLocation);
typedef NevercStatus(NEVERC_CALL *NevercGetLocationFileFn)(
    void *Context, NevercTaskHandle Task, NevercSourceLocation Location,
    NevercFileHandle *OutFile);
typedef NevercStatus(NEVERC_CALL *NevercGetFileInfoFn)(
    void *Context, NevercTaskHandle Task, NevercFileHandle File,
    NevercFileInfo *OutInfo);
typedef NevercStatus(NEVERC_CALL *NevercGetSourceInputFn)(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Input, NevercSourceInputInfo *OutInfo);
typedef NevercStatus(NEVERC_CALL *NevercCreateMemorySourceUnitFn)(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Input,
    const NevercMemorySourceUnitDescriptor *Descriptor,
    NevercArtifactHandle *OutUnit);
typedef NevercStatus(NEVERC_CALL *NevercGetSourceUnitFn)(
    void *Context, const NevercPhaseFrame *Frame,
    NevercArtifactHandle Unit, NevercSourceUnitInfo *OutInfo);
typedef NevercStatus(NEVERC_CALL *NevercRegisterVFSProviderFn)(
    void *Context, void *Registrar,
    const NevercVFSProviderDescriptor *Descriptor);
typedef NevercStatus(NEVERC_CALL *NevercIOStatFn)(
    void *Context, NevercTaskHandle Task, NevercStringView Path,
    NevercVFSStatus *OutStatus);
typedef NevercStatus(NEVERC_CALL *NevercIOOpenFileForReadFn)(
    void *Context, NevercTaskHandle Task, NevercStringView Path,
    NevercFileHandle *OutFile);
typedef NevercStatus(NEVERC_CALL *NevercIOReadFileFn)(
    void *Context, NevercTaskHandle Task, NevercFileHandle File,
    uint64_t Offset, uint64_t Length, NevercBufferHandle *OutBuffer);
typedef NevercStatus(NEVERC_CALL *NevercIOCloseFileFn)(
    void *Context, NevercTaskHandle Task, NevercFileHandle File);
typedef NevercStatus(NEVERC_CALL *NevercIOCopyBufferFn)(
    void *Context, NevercTaskHandle Task, NevercByteView Bytes,
    NevercBool NullTerminated, NevercBufferHandle *OutBuffer);
typedef NevercStatus(NEVERC_CALL *NevercIOGetBufferViewFn)(
    void *Context, NevercTaskHandle Task, NevercBufferHandle Buffer,
    NevercBufferView *OutView);
typedef NevercStatus(NEVERC_CALL *NevercIOReleaseBufferFn)(
    void *Context, NevercTaskHandle Task, NevercBufferHandle Buffer);
typedef NevercStatus(NEVERC_CALL *NevercIOPathToBufferFn)(
    void *Context, NevercTaskHandle Task, NevercStringView Path,
    NevercBufferHandle *OutBuffer);
typedef NevercStatus(NEVERC_CALL *NevercIOGetWorkingDirectoryFn)(
    void *Context, NevercTaskHandle Task, NevercBufferHandle *OutBuffer);
typedef NevercStatus(NEVERC_CALL *NevercIOSetWorkingDirectoryFn)(
    void *Context, NevercTaskHandle Task, NevercStringView Path);
typedef NevercStatus(NEVERC_CALL *NevercIOOpenDirectoryFn)(
    void *Context, NevercTaskHandle Task, NevercStringView Path,
    NevercDirectoryCursorHandle *OutCursor);
typedef NevercStatus(NEVERC_CALL *NevercIOReadDirectoryFn)(
    void *Context, NevercTaskHandle Task, NevercDirectoryCursorHandle Cursor,
    NevercVFSDirectoryEntry *OutEntry, NevercBool *OutHasEntry);
typedef NevercStatus(NEVERC_CALL *NevercIOCloseDirectoryFn)(
    void *Context, NevercTaskHandle Task,
    NevercDirectoryCursorHandle Cursor);
typedef NevercStatus(NEVERC_CALL *NevercIOAddMemoryFileFn)(
    void *Context, NevercSessionHandle Session, NevercStringView Path,
    NevercByteView Content, int64_t ModificationTime);
typedef NevercStatus(NEVERC_CALL *NevercIOBeginMemoryOutputFn)(
    void *Context, NevercTaskHandle Task, NevercStringView LogicalName,
    uint64_t SizeBudget, NevercOutputSinkHandle *OutSink);
typedef NevercStatus(NEVERC_CALL *NevercIOBeginFileOutputFn)(
    void *Context, NevercTaskHandle Task, NevercStringView FinalPath,
    uint64_t SizeBudget, NevercOutputSinkHandle *OutSink);
typedef NevercStatus(NEVERC_CALL *NevercIOBeginStreamOutputFn)(
    void *Context, NevercTaskHandle Task, NevercOutputStream Stream,
    uint64_t SizeBudget, NevercOutputSinkHandle *OutSink);
typedef NevercStatus(NEVERC_CALL *NevercIOOutputWriteFn)(
    void *Context, NevercTaskHandle Task, NevercOutputSinkHandle Sink,
    NevercByteView Bytes);
typedef NevercStatus(NEVERC_CALL *NevercIOOutputWriteAtFn)(
    void *Context, NevercTaskHandle Task, NevercOutputSinkHandle Sink,
    uint64_t Offset, NevercByteView Bytes);
typedef NevercStatus(NEVERC_CALL *NevercIOOutputTellFn)(
    void *Context, NevercTaskHandle Task, NevercOutputSinkHandle Sink,
    uint64_t *OutPosition);
typedef NevercStatus(NEVERC_CALL *NevercIOOutputTruncateFn)(
    void *Context, NevercTaskHandle Task, NevercOutputSinkHandle Sink,
    uint64_t Size);
typedef NevercStatus(NEVERC_CALL *NevercIOOutputMetadataSetFn)(
    void *Context, NevercTaskHandle Task, NevercOutputSinkHandle Sink,
    NevercStringView Key, NevercStringView Value);
typedef NevercStatus(NEVERC_CALL *NevercIOOutputFinishFn)(
    void *Context, NevercTaskHandle Task, NevercOutputSinkHandle Sink,
    NevercOutputSeal *OutSeal);
typedef NevercStatus(NEVERC_CALL *NevercIOOutputAbortFn)(
    void *Context, NevercTaskHandle Task, NevercOutputSinkHandle Sink);
typedef NevercStatus(NEVERC_CALL *NevercIOOutputGetSummaryFn)(
    void *Context, NevercTaskHandle Task, NevercOutputSinkHandle Sink,
    NevercOutputSummary *OutSummary);
typedef NevercStatus(NEVERC_CALL *NevercIORecordDependencyFn)(
    void *Context, NevercTaskHandle Task,
    const NevercDependencyDescriptor *Descriptor,
    NevercDependencyHandle *OutDependency);

/*
 * These prefixes reserve independently negotiated tables. Function slots are
 * added only with the task that implements and tests them; no null placeholder
 * slots are published.
 */
typedef struct NevercIOAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercRegisterVFSProviderFn RegisterVFSProvider;
  NevercIOStatFn Stat;
  NevercIOOpenFileForReadFn OpenFileForRead;
  NevercIOReadFileFn ReadFile;
  NevercIOCloseFileFn CloseFile;
  NevercIOCopyBufferFn CopyBuffer;
  NevercIOGetBufferViewFn GetBufferView;
  NevercIOReleaseBufferFn ReleaseBuffer;
  NevercIOPathToBufferFn Canonicalize;
  NevercIOGetWorkingDirectoryFn GetWorkingDirectory;
  NevercIOSetWorkingDirectoryFn SetWorkingDirectory;
  NevercIOOpenDirectoryFn OpenDirectory;
  NevercIOReadDirectoryFn ReadDirectory;
  NevercIOCloseDirectoryFn CloseDirectory;
  NevercIOAddMemoryFileFn AddMemoryFile;
  NevercIOBeginMemoryOutputFn BeginMemoryOutput;
  NevercIOBeginFileOutputFn BeginFileOutput;
  NevercIOBeginStreamOutputFn BeginStreamOutput;
  NevercIOOutputWriteFn OutputWrite;
  NevercIOOutputWriteAtFn OutputWriteAt;
  NevercIOOutputTellFn OutputTell;
  NevercIOOutputTruncateFn OutputTruncate;
  NevercIOOutputMetadataSetFn OutputMetadataSet;
  NevercIOOutputFinishFn OutputFinish;
  NevercIOOutputAbortFn OutputAbort;
  NevercIOOutputGetSummaryFn OutputGetSummary;
  NevercIORecordDependencyFn RecordDependency;
} NevercIOAPI;

typedef struct NevercSourceLocationAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercGetSourceLocationInfoFn GetLocationInfo;
  NevercTransformSourceLocationFn GetSpellingLocation;
  NevercTransformSourceLocationFn GetExpansionLocation;
  NevercTransformSourceLocationFn GetFileLocation;
  NevercGetSourceRangeInfoFn GetRangeInfo;
  NevercGetSourceTextFn GetSourceText;
  NevercGetPresumedLocationFn GetPresumedLocation;
  NevercGetLocationFileFn GetLocationFile;
  NevercTransformSourceLocationFn GetIncludeLocation;
  NevercGetFileInfoFn GetFileInfo;
  NevercGetCharacterDataFn GetCharacterData;
  NevercTransformSourceLocationFn GetTokenEnd;
  NevercGetSourceLocationInfoBatchFn GetLocationInfoBatch;
  NevercGetSourceInputFn GetSourceInput;
  NevercCreateMemorySourceUnitFn CreateMemorySourceUnit;
  NevercGetSourceUnitFn GetSourceUnit;
} NevercSourceLocationAPI;

NEVERC_ABI_PACK_END

#ifdef __cplusplus
}
#endif

#endif
