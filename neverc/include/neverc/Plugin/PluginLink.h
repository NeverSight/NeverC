/*===-- PluginLink.h - NeverC linker plugin C ABI ----------------- C ---===*/

#ifndef NEVERC_PLUGIN_PLUGINLINK_H
#define NEVERC_PLUGIN_PLUGINLINK_H

#include "neverc/Plugin/PluginObject.h" /* IWYU pragma: export */

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_LINK_API_MAJOR UINT16_C(1)
#define NEVERC_LINK_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_LINK_HIGH UINT64_C(0x4e43504c494e4b01)
#define NEVERC_INTERFACE_LINK_LOW UINT64_C(0x0000000000000001)
#define NEVERC_LINK_INTERFACE_STABILITY NEVERC_INTERFACE_STABLE

#define NEVERC_LINK_REGISTRAR_API_MAJOR UINT16_C(1)
#define NEVERC_LINK_REGISTRAR_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_LINK_REGISTRAR_HIGH UINT64_C(0x4e43504c4e4b5201)
#define NEVERC_INTERFACE_LINK_REGISTRAR_LOW UINT64_C(0x0000000000000001)
#define NEVERC_LINK_REGISTRAR_INTERFACE_STABILITY NEVERC_INTERFACE_STABLE

#define NEVERC_LINK_PHASE_API_MAJOR UINT16_C(1)
#define NEVERC_LINK_PHASE_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_LINK_PHASE_HIGH UINT64_C(0x4e43504c4e504801)
#define NEVERC_INTERFACE_LINK_PHASE_LOW UINT64_C(0x0000000000000001)
#define NEVERC_LINK_PHASE_INTERFACE_STABILITY NEVERC_INTERFACE_STABLE

/*
 * Ownership rules:
 * - Handles are opaque, host-owned and valid only in the task that issued them.
 * - String, byte and struct-array views supplied to a callback are borrowed
 *   until that callback returns unless a narrower lifetime is documented.
 * - NevercLinkEntityPage::Data is caller-owned writable storage; the host
 *   writes no more than ElementCapacity entries of ElementStride bytes.
 * - On callback success the host adopts candidate handles. On failure the
 *   provider retains responsibility for resources it created.
 */

typedef NevercHandle NevercLinkRequestHandle;
typedef NevercHandle NevercLinkInputHandle;
typedef NevercHandle NevercLinkArchiveHandle;
typedef NevercHandle NevercLinkArchiveMemberHandle;
typedef NevercHandle NevercLinkSharedLibraryHandle;
typedef NevercHandle NevercLinkBitcodeModuleHandle;
typedef NevercHandle NevercLinkGraphHandle;
typedef NevercHandle NevercLinkSectionHandle;
typedef NevercHandle NevercLinkAtomHandle;
typedef NevercHandle NevercLinkSymbolHandle;
typedef NevercHandle NevercLinkEdgeHandle;
typedef NevercHandle NevercLinkComdatHandle;
typedef NevercHandle NevercLinkImportHandle;
typedef NevercHandle NevercLinkExportHandle;
typedef NevercHandle NevercLinkUnwindHandle;
typedef NevercHandle NevercLinkSyntheticHandle;
typedef NevercHandle NevercLinkConstraintHandle;
typedef NevercHandle NevercLinkMutationHandle;
typedef NevercHandle NevercLinkProofHandle;
typedef NevercHandle NevercBinaryImageHandle;
typedef NevercHandle NevercBinarySegmentHandle;
typedef NevercHandle NevercBinarySectionHandle;
typedef NevercHandle NevercOutputBundleHandle;
typedef NevercHandle NevercLinkerProviderHandle;
typedef NevercHandle NevercObjectMergeProviderHandle;
typedef NevercHandle NevercBinaryImageVerifierHandle;

typedef uint32_t NevercLinkInputKind;
#define NEVERC_LINK_INPUT_UNKNOWN UINT32_C(0)
#define NEVERC_LINK_INPUT_OBJECT UINT32_C(1)
#define NEVERC_LINK_INPUT_ARCHIVE UINT32_C(2)
#define NEVERC_LINK_INPUT_SHARED_LIBRARY UINT32_C(3)
#define NEVERC_LINK_INPUT_BITCODE UINT32_C(4)
#define NEVERC_LINK_INPUT_SCRIPT UINT32_C(5)
#define NEVERC_LINK_INPUT_BLOB UINT32_C(6)

typedef uint32_t NevercLinkOutputKind;
#define NEVERC_LINK_OUTPUT_RELOCATABLE UINT32_C(1)
#define NEVERC_LINK_OUTPUT_EXECUTABLE UINT32_C(2)
#define NEVERC_LINK_OUTPUT_SHARED_LIBRARY UINT32_C(3)
#define NEVERC_LINK_OUTPUT_BUNDLE UINT32_C(4)

typedef uint32_t NevercLinkState;
#define NEVERC_LINK_STATE_INITIAL UINT32_C(0)
#define NEVERC_LINK_STATE_INPUT_PROBED UINT32_C(1)
#define NEVERC_LINK_STATE_INPUTS_READ UINT32_C(2)
#define NEVERC_LINK_STATE_LTO_RESOLUTION_READY UINT32_C(3)
#define NEVERC_LINK_STATE_LTO_GENERATED UINT32_C(4)
#define NEVERC_LINK_STATE_SYMBOLS_RESOLVED UINT32_C(5)
#define NEVERC_LINK_STATE_COMDAT_SELECTED UINT32_C(6)
#define NEVERC_LINK_STATE_GC_COMPLETE UINT32_C(7)
#define NEVERC_LINK_STATE_ICF_COMPLETE UINT32_C(8)
#define NEVERC_LINK_STATE_SYNTHETICS_READY UINT32_C(9)
#define NEVERC_LINK_STATE_THUNKS_RELAXED UINT32_C(10)
#define NEVERC_LINK_STATE_LAYOUT_COMPLETE UINT32_C(11)
#define NEVERC_LINK_STATE_RELOCATIONS_APPLIED UINT32_C(12)
#define NEVERC_LINK_STATE_IMAGE_EMITTED UINT32_C(13)

typedef uint64_t NevercLinkOptionFlags;
#define NEVERC_LINK_OPTION_NONE UINT64_C(0)
#define NEVERC_LINK_OPTION_PIE (UINT64_C(1) << 0)
#define NEVERC_LINK_OPTION_STATIC (UINT64_C(1) << 1)
#define NEVERC_LINK_OPTION_GC_SECTIONS (UINT64_C(1) << 2)
#define NEVERC_LINK_OPTION_ICF (UINT64_C(1) << 3)
#define NEVERC_LINK_OPTION_EXPORT_DYNAMIC (UINT64_C(1) << 4)
#define NEVERC_LINK_OPTION_ALLOW_UNDEFINED (UINT64_C(1) << 5)
#define NEVERC_LINK_OPTION_WHOLE_ARCHIVE (UINT64_C(1) << 6)
#define NEVERC_LINK_OPTION_DETERMINISTIC (UINT64_C(1) << 7)

typedef uint64_t NevercLinkInputFlags;
#define NEVERC_LINK_INPUT_FLAG_NONE UINT64_C(0)
#define NEVERC_LINK_INPUT_FLAG_WHOLE_ARCHIVE (UINT64_C(1) << 0)
#define NEVERC_LINK_INPUT_FLAG_AS_NEEDED (UINT64_C(1) << 1)
#define NEVERC_LINK_INPUT_FLAG_START_GROUP (UINT64_C(1) << 2)
#define NEVERC_LINK_INPUT_FLAG_END_GROUP (UINT64_C(1) << 3)
#define NEVERC_LINK_INPUT_FLAG_LAZY (UINT64_C(1) << 4)

typedef uint32_t NevercLinkSymbolBinding;
#define NEVERC_LINK_SYMBOL_BINDING_LOCAL UINT32_C(1)
#define NEVERC_LINK_SYMBOL_BINDING_GLOBAL UINT32_C(2)
#define NEVERC_LINK_SYMBOL_BINDING_WEAK UINT32_C(3)
#define NEVERC_LINK_SYMBOL_BINDING_COMMON UINT32_C(4)

typedef uint32_t NevercLinkSymbolVisibility;
#define NEVERC_LINK_SYMBOL_VISIBILITY_DEFAULT UINT32_C(1)
#define NEVERC_LINK_SYMBOL_VISIBILITY_HIDDEN UINT32_C(2)
#define NEVERC_LINK_SYMBOL_VISIBILITY_PROTECTED UINT32_C(3)
#define NEVERC_LINK_SYMBOL_VISIBILITY_INTERNAL UINT32_C(4)

typedef uint32_t NevercLinkSymbolDefinition;
#define NEVERC_LINK_SYMBOL_UNDEFINED UINT32_C(1)
#define NEVERC_LINK_SYMBOL_DEFINED UINT32_C(2)
#define NEVERC_LINK_SYMBOL_ABSOLUTE UINT32_C(3)
#define NEVERC_LINK_SYMBOL_COMMON UINT32_C(4)
#define NEVERC_LINK_SYMBOL_SHARED UINT32_C(5)

typedef uint32_t NevercLinkEdgeKind;
#define NEVERC_LINK_EDGE_RELOCATION UINT32_C(1)
#define NEVERC_LINK_EDGE_ASSOCIATION UINT32_C(2)
#define NEVERC_LINK_EDGE_KEEP_ALIVE UINT32_C(3)
#define NEVERC_LINK_EDGE_UNWIND UINT32_C(4)
#define NEVERC_LINK_EDGE_FORMAT_EXTENSION UINT32_C(5)

typedef uint32_t NevercLinkComdatSelection;
#define NEVERC_LINK_COMDAT_ANY UINT32_C(1)
#define NEVERC_LINK_COMDAT_EXACT_MATCH UINT32_C(2)
#define NEVERC_LINK_COMDAT_SAME_SIZE UINT32_C(3)
#define NEVERC_LINK_COMDAT_LARGEST UINT32_C(4)
#define NEVERC_LINK_COMDAT_NEWEST UINT32_C(5)
#define NEVERC_LINK_COMDAT_NO_DUPLICATES UINT32_C(6)

typedef uint64_t NevercLinkAtomFlags;
#define NEVERC_LINK_ATOM_LIVE (UINT64_C(1) << 0)
#define NEVERC_LINK_ATOM_ROOT (UINT64_C(1) << 1)
#define NEVERC_LINK_ATOM_SYNTHETIC (UINT64_C(1) << 2)
#define NEVERC_LINK_ATOM_FOLDED (UINT64_C(1) << 3)
#define NEVERC_LINK_ATOM_ADDRESS_SIGNIFICANT (UINT64_C(1) << 4)
#define NEVERC_LINK_ATOM_TLS (UINT64_C(1) << 5)
#define NEVERC_LINK_ATOM_UNWIND (UINT64_C(1) << 6)

typedef uint64_t NevercBinarySegmentFlags;
#define NEVERC_BINARY_SEGMENT_READ (UINT64_C(1) << 0)
#define NEVERC_BINARY_SEGMENT_WRITE (UINT64_C(1) << 1)
#define NEVERC_BINARY_SEGMENT_EXECUTE (UINT64_C(1) << 2)

typedef uint64_t NevercLinkProviderFlags;
#define NEVERC_LINK_PROVIDER_DETERMINISTIC (UINT64_C(1) << 0)
#define NEVERC_LINK_PROVIDER_CACHEABLE (UINT64_C(1) << 1)
#define NEVERC_LINK_PROVIDER_REPLAY_REQUIRED (UINT64_C(1) << 2)

typedef uint32_t NevercBinaryImageState;
#define NEVERC_BINARY_IMAGE_CANDIDATE UINT32_C(1)
#define NEVERC_BINARY_IMAGE_VERIFIED UINT32_C(2)
#define NEVERC_BINARY_IMAGE_COMMITTED UINT32_C(3)
#define NEVERC_BINARY_IMAGE_ABORTED UINT32_C(4)
#define NEVERC_BINARY_IMAGE_FAILED_PARTIAL UINT32_C(5)

typedef struct NevercLinkRequest NevercLinkRequest;
typedef struct NevercRawLinkInputSet NevercRawLinkInputSet;
typedef struct NevercLinkerProductCandidate NevercLinkerProductCandidate;
typedef struct NevercObjectMergeInput NevercObjectMergeInput;
typedef struct NevercObjectMergeRequest NevercObjectMergeRequest;
typedef struct NevercObjectMergeCandidate NevercObjectMergeCandidate;

typedef NevercStatus(NEVERC_CALL *NevercLinkerProviderFn)(
    void *UserData, NevercTaskHandle Task, const NevercLinkRequest *Request,
    const NevercRawLinkInputSet *Inputs,
    NevercLinkerProductCandidate *OutCandidate);
typedef NevercStatus(NEVERC_CALL *NevercObjectMergeProviderFn)(
    void *UserData, NevercTaskHandle Task,
    const NevercObjectMergeRequest *Request,
    NevercObjectMergeCandidate *OutCandidate);
typedef NevercStatus(NEVERC_CALL *NevercBinaryImageVerifierFn)(
    void *UserData, NevercTaskHandle Task,
    const NevercLinkRequest *Request, NevercBinaryImageHandle Image);

NEVERC_ABI_PACK_BEGIN

typedef struct NevercLinkExtension {
  NevercABITableHeader Header;
  NevercInterfaceID NamespaceID;
  uint32_t Version;
  NevercBool Required;
  NevercByteView Payload;
  NevercStringView Digest;
} NevercLinkExtension;

typedef struct NevercRawLinkInput {
  NevercABITableHeader Header;
  NevercLinkInputKind Kind;
  uint32_t Reserved;
  NevercLinkInputFlags Flags;
  uint64_t Ordinal;
  NevercStringView LogicalURI;
  NevercByteView AuthorizedBlob;
  NevercObjectGraphHandle ObjectGraph;
  NevercArtifactHandle Artifact;
  NevercStructArrayView Extensions;
} NevercRawLinkInput;

struct NevercRawLinkInputSet {
  NevercABITableHeader Header;
  NevercStructArrayView Inputs;
  uint8_t OrderDigest[32];
};

typedef struct NevercLinkOptions {
  NevercABITableHeader Header;
  NevercLinkOptionFlags Flags;
  NevercStringView EntrySymbol;
  NevercStringView InstallName;
  NevercStringView Soname;
  uint64_t ImageBase;
  uint64_t PageSize;
  uint32_t ThreadBudget;
  uint32_t Reserved;
  NevercStructArrayView SearchPaths;
  NevercStructArrayView Libraries;
  NevercStructArrayView Extensions;
} NevercLinkOptions;

struct NevercLinkRequest {
  NevercABITableHeader Header;
  NevercLinkRequestHandle Request;
  NevercTaskHandle Task;
  NevercTargetKey Target;
  NevercObjectFormatID InputFormat;
  NevercObjectFormatID OutputFormat;
  NevercLinkOutputKind OutputKind;
  uint32_t Reserved;
  NevercStringView OutputURI;
  NevercLinkOptions Options;
  NevercRawLinkInputSet RawInputs;
  uint8_t RequestDigest[32];
};

typedef struct NevercLinkInputInfo {
  NevercABITableHeader Header;
  NevercLinkInputHandle Input;
  NevercLinkInputKind Kind;
  uint32_t Reserved;
  NevercLinkInputFlags Flags;
  uint64_t Ordinal;
  NevercStringView LogicalURI;
  uint8_t ContentDigest[32];
  NevercStringView ReaderRoute;
  NevercObjectGraphHandle ObjectGraph;
  NevercLinkArchiveHandle Archive;
  NevercLinkSharedLibraryHandle SharedLibrary;
  NevercLinkBitcodeModuleHandle BitcodeModule;
  NevercStructArrayView Extensions;
} NevercLinkInputInfo;

typedef struct NevercLinkGraphInfo {
  NevercABITableHeader Header;
  NevercLinkGraphHandle Graph;
  NevercTargetKey Target;
  NevercObjectFormatID FormatID;
  NevercLinkState State;
  uint32_t Reserved;
  uint64_t Generation;
  uint64_t InputCount;
  uint64_t ArchiveCount;
  uint64_t ArchiveMemberCount;
  uint64_t SharedLibraryCount;
  uint64_t BitcodeModuleCount;
  uint64_t SectionCount;
  uint64_t AtomCount;
  uint64_t SymbolCount;
  uint64_t EdgeCount;
  uint64_t ComdatCount;
  uint64_t ImportCount;
  uint64_t ExportCount;
  uint64_t UnwindCount;
  uint64_t SyntheticCount;
  uint64_t ConstraintCount;
  uint8_t SemanticDigest[32];
} NevercLinkGraphInfo;

typedef struct NevercLinkOrigin {
  NevercABITableHeader Header;
  NevercLinkInputHandle Input;
  NevercLinkArchiveMemberHandle ArchiveMember;
  NevercObjectGraphHandle ObjectGraph;
  uint64_t ObjectEntityID;
  NevercInterfaceID CreatedByPhase;
  NevercStringView CreatedByProvider;
  NevercInterfaceID LastMutationPhase;
  NevercStringView LastMutationPlugin;
} NevercLinkOrigin;

typedef struct NevercLinkArchiveInfo {
  NevercABITableHeader Header;
  NevercLinkArchiveHandle Archive;
  NevercLinkInputHandle Input;
  NevercStringView Name;
  NevercBool Thin;
  uint8_t Reserved8[3];
  NevercLinkOrigin Origin;
  NevercStructArrayView Extensions;
} NevercLinkArchiveInfo;

typedef struct NevercLinkArchiveMemberInfo {
  NevercABITableHeader Header;
  NevercLinkArchiveMemberHandle Member;
  NevercLinkInputHandle Input;
  NevercLinkArchiveHandle Archive;
  NevercStringView Name;
  uint64_t Ordinal;
  uint8_t ContentDigest[32];
  NevercBool Materialized;
  uint8_t Reserved8[3];
  NevercStringView MaterializationReason;
  NevercLinkOrigin Origin;
  NevercStructArrayView Extensions;
} NevercLinkArchiveMemberInfo;

typedef struct NevercLinkSharedLibraryInfo {
  NevercABITableHeader Header;
  NevercLinkSharedLibraryHandle SharedLibrary;
  NevercLinkInputHandle Input;
  NevercStringView Name;
  NevercStringView InstallName;
  uint8_t ContentDigest[32];
  NevercLinkOrigin Origin;
  NevercStructArrayView Extensions;
} NevercLinkSharedLibraryInfo;

typedef struct NevercLinkBitcodeModuleInfo {
  NevercABITableHeader Header;
  NevercLinkBitcodeModuleHandle Module;
  NevercLinkInputHandle Input;
  NevercStringView Name;
  uint8_t ContentDigest[32];
  NevercHandle Summary;
  NevercLinkOrigin Origin;
  NevercStructArrayView Extensions;
} NevercLinkBitcodeModuleInfo;

typedef struct NevercLinkSectionInfo {
  NevercABITableHeader Header;
  NevercLinkSectionHandle Section;
  NevercStringView Name;
  NevercObjectSectionKind Kind;
  uint32_t Reserved;
  NevercObjectSectionFlags Flags;
  uint64_t Alignment;
  uint64_t Address;
  uint64_t FileOffset;
  uint64_t Size;
  NevercLinkComdatHandle Comdat;
  NevercLinkOrigin Origin;
  NevercStructArrayView Extensions;
} NevercLinkSectionInfo;

typedef struct NevercLinkAtomInfo {
  NevercABITableHeader Header;
  NevercLinkAtomHandle Atom;
  NevercLinkSectionHandle Section;
  NevercStringView Name;
  NevercLinkAtomFlags Flags;
  uint64_t Alignment;
  uint64_t Address;
  uint64_t FileOffset;
  NevercByteView Content;
  uint64_t ZeroFillSize;
  NevercLinkComdatHandle Comdat;
  NevercLinkAtomHandle FoldLeader;
  NevercLinkOrigin Origin;
  NevercStructArrayView Extensions;
} NevercLinkAtomInfo;

typedef struct NevercLinkSymbolInfo {
  NevercABITableHeader Header;
  NevercLinkSymbolHandle Symbol;
  NevercStringView Name;
  NevercStringView Version;
  NevercLinkSymbolBinding Binding;
  NevercLinkSymbolVisibility Visibility;
  NevercLinkSymbolDefinition Definition;
  NevercObjectSymbolType Type;
  NevercLinkAtomHandle Atom;
  uint64_t Value;
  uint64_t Size;
  NevercBool IsPrevailing;
  NevercBool IsExported;
  NevercBool IsImported;
  NevercBool IsRoot;
  NevercLinkOrigin Origin;
  NevercStructArrayView Extensions;
} NevercLinkSymbolInfo;

typedef struct NevercLinkSymbolResolutionUpdate {
  NevercABITableHeader Header;
  NevercLinkSymbolBinding Binding;
  NevercLinkSymbolVisibility Visibility;
  NevercLinkSymbolDefinition Definition;
  NevercBool IsPrevailing;
  NevercBool IsExported;
  uint8_t Reserved8[2];
} NevercLinkSymbolResolutionUpdate;

typedef struct NevercLinkEdgeInfo {
  NevercABITableHeader Header;
  NevercLinkEdgeHandle Edge;
  NevercLinkEdgeKind Kind;
  uint32_t Reserved;
  NevercLinkAtomHandle Source;
  uint64_t Offset;
  NevercObjectRelocationKind RelocationKind;
  uint32_t Width;
  int64_t Addend;
  NevercBool IsPCRelative;
  NevercBool IsSigned;
  uint8_t Reserved8[2];
  NevercLinkSymbolHandle TargetSymbol;
  NevercLinkAtomHandle TargetAtom;
  NevercLinkOrigin Origin;
  NevercStructArrayView Extensions;
} NevercLinkEdgeInfo;

typedef struct NevercLinkComdatInfo {
  NevercABITableHeader Header;
  NevercLinkComdatHandle Comdat;
  NevercStringView Name;
  NevercLinkComdatSelection Selection;
  uint32_t Reserved;
  NevercLinkComdatHandle Selected;
  NevercLinkOrigin Origin;
  NevercStructArrayView Extensions;
} NevercLinkComdatInfo;

typedef struct NevercLinkImportInfo {
  NevercABITableHeader Header;
  NevercLinkImportHandle Import;
  NevercStringView Name;
  NevercStringView Library;
  NevercLinkSymbolHandle Symbol;
  NevercLinkOrigin Origin;
  NevercStructArrayView Extensions;
} NevercLinkImportInfo;

typedef struct NevercLinkExportInfo {
  NevercABITableHeader Header;
  NevercLinkExportHandle Export;
  NevercStringView Name;
  NevercLinkSymbolHandle Symbol;
  NevercLinkOrigin Origin;
  NevercStructArrayView Extensions;
} NevercLinkExportInfo;

typedef struct NevercLinkUnwindInfo {
  NevercABITableHeader Header;
  NevercLinkUnwindHandle Unwind;
  NevercLinkAtomHandle Atom;
  NevercLinkSymbolHandle PersonalitySymbol;
  NevercLinkOrigin Origin;
  NevercStructArrayView Extensions;
} NevercLinkUnwindInfo;

typedef struct NevercLinkSyntheticInfo {
  NevercABITableHeader Header;
  NevercLinkSyntheticHandle Synthetic;
  NevercStringView Role;
  NevercLinkSectionHandle Section;
  NevercLinkAtomHandle Atom;
  NevercLinkOrigin Origin;
  NevercStructArrayView Extensions;
} NevercLinkSyntheticInfo;

typedef struct NevercLinkConstraintInfo {
  NevercABITableHeader Header;
  NevercLinkConstraintHandle Constraint;
  NevercStringView Kind;
  uint64_t SubjectID;
  uint64_t Value;
  NevercBool Required;
  uint8_t Reserved8[3];
  NevercLinkOrigin Origin;
  NevercStructArrayView Extensions;
} NevercLinkConstraintInfo;

typedef struct NevercLinkEntityPage {
  NevercABITableHeader Header;
  void *Data;
  uint64_t ElementCapacity;
  uint64_t ElementStride;
  uint64_t OutCount;
  uint64_t NextCursor;
  NevercBool HasMore;
  uint32_t Reserved;
} NevercLinkEntityPage;

typedef struct NevercLinkProofInfo {
  NevercABITableHeader Header;
  NevercLinkProofHandle Proof;
  NevercLinkGraphHandle Graph;
  NevercLinkState State;
  uint32_t Reserved;
  uint64_t GraphGeneration;
  NevercTargetID TargetID;
  NevercObjectFormatID FormatID;
  NevercInterfaceID OutputArtifact;
  uint8_t RouteDigest[32];
  uint8_t SemanticDigest[32];
  uint64_t ImageBase;
  uint64_t EntryAddress;
} NevercLinkProofInfo;

typedef struct NevercLinkPhaseGraphInfo {
  NevercABITableHeader Header;
  const struct NevercLinkAPI *Link;
  NevercLinkGraphHandle Graph;
  NevercLinkProofHandle Proof;
  NevercLinkState State;
  uint32_t Reserved;
  uint64_t Generation;
} NevercLinkPhaseGraphInfo;

typedef struct NevercLinkPhaseImageInfo {
  NevercABITableHeader Header;
  const struct NevercLinkAPI *Link;
  NevercBinaryImageHandle Image;
  NevercOutputBundleHandle Outputs;
  NevercBinaryImageState State;
  uint32_t Reserved;
  uint64_t Generation;
} NevercLinkPhaseImageInfo;

typedef struct NevercBinarySegmentInfo {
  NevercABITableHeader Header;
  NevercBinarySegmentHandle Segment;
  NevercStringView Name;
  NevercBinarySegmentFlags Flags;
  uint64_t Address;
  uint64_t MemorySize;
  uint64_t FileOffset;
  uint64_t FileSize;
  uint64_t Alignment;
} NevercBinarySegmentInfo;

typedef struct NevercBinarySectionInfo {
  NevercABITableHeader Header;
  NevercBinarySectionHandle Section;
  NevercBinarySegmentHandle Segment;
  NevercStringView Name;
  NevercObjectSectionKind Kind;
  uint32_t Reserved;
  NevercObjectSectionFlags Flags;
  uint64_t Address;
  uint64_t MemorySize;
  uint64_t FileOffset;
  uint64_t FileSize;
  uint64_t Alignment;
} NevercBinarySectionInfo;

typedef struct NevercBinaryImageInfo {
  NevercABITableHeader Header;
  NevercBinaryImageHandle Image;
  NevercBinaryImageState State;
  NevercLinkOutputKind OutputKind;
  NevercTargetID TargetID;
  NevercObjectFormatID FormatID;
  uint64_t EntryAddress;
  uint64_t ImageBase;
  uint64_t Size;
  uint64_t SegmentCount;
  uint64_t SectionCount;
  uint64_t ImportCount;
  uint64_t ExportCount;
  uint64_t DynamicRelocationCount;
  uint8_t ContentDigest[32];
  const NevercMutableBinaryAPI *Binary;
  NevercMutableBinaryBuilderHandle Builder;
  NevercStructArrayView Extensions;
} NevercBinaryImageInfo;

struct NevercLinkerProductCandidate {
  NevercABITableHeader Header;
  NevercBinaryImageHandle Image;
  NevercOutputBundleHandle Outputs;
  NevercInterfaceID ProductID;
  uint8_t ProducerRouteDigest[32];
};

struct NevercObjectMergeInput {
  NevercABITableHeader Header;
  const struct NevercObjectAPI *Object;
  NevercObjectGraphHandle Graph;
};

struct NevercObjectMergeRequest {
  NevercABITableHeader Header;
  NevercTaskHandle Task;
  NevercTargetKey Target;
  NevercObjectFormatID FormatID;
  NevercStructArrayView Objects;
  NevercLinkOptionFlags Flags;
  NevercStructArrayView Extensions;
  const struct NevercObjectAPI *OutputObject;
  NevercObjectGraphHandle OutputGraph;
  NevercObjectMutationHandle OutputMutation;
};

struct NevercObjectMergeCandidate {
  NevercABITableHeader Header;
  NevercObjectGraphHandle Object;
  NevercInterfaceID ProductID;
  uint8_t ProducerRouteDigest[32];
};

typedef struct NevercLinkerProviderDescriptor {
  NevercABITableHeader Header;
  NevercStringView ProviderID;
  NevercTargetID TargetID;
  NevercObjectFormatID InputFormat;
  NevercObjectFormatID OutputFormat;
  NevercLinkOutputKind OutputKind;
  uint32_t Reserved;
  NevercLinkProviderFlags Flags;
  NevercStringView CompatibilityKey;
  NevercInterfaceID ProductID;
  NevercLinkerProviderFn Link;
  NevercBinaryImageVerifierFn VerifyImage;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercLinkerProviderDescriptor;

typedef struct NevercObjectMergeProviderDescriptor {
  NevercABITableHeader Header;
  NevercStringView ProviderID;
  NevercTargetID TargetID;
  NevercObjectFormatID FormatID;
  NevercLinkProviderFlags Flags;
  NevercStringView CompatibilityKey;
  NevercInterfaceID ProductID;
  NevercObjectMergeProviderFn Merge;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercObjectMergeProviderDescriptor;

typedef struct NevercBinaryImageVerifierDescriptor {
  NevercABITableHeader Header;
  NevercStringView VerifierID;
  NevercTargetID TargetID;
  NevercObjectFormatID FormatID;
  NevercLinkOutputKind OutputKind;
  uint32_t Reserved;
  NevercBinaryImageVerifierFn Verify;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercBinaryImageVerifierDescriptor;

typedef struct NevercLinkAPI {
  NevercABITableHeader Header;
  void *Context;

  NevercStatus(NEVERC_CALL *GetRequest)(
      void *Context, NevercTaskHandle Task, NevercLinkRequestHandle Request,
      NevercLinkRequest *OutRequest);
  NevercStatus(NEVERC_CALL *GetGraphInfo)(
      void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
      NevercLinkGraphInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetInputPage)(
      void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
      uint64_t Cursor, NevercLinkEntityPage *InOutPage);
  NevercStatus(NEVERC_CALL *GetArchivePage)(
      void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
      uint64_t Cursor, NevercLinkEntityPage *InOutPage);
  NevercStatus(NEVERC_CALL *GetArchiveMemberPage)(
      void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
      uint64_t Cursor, NevercLinkEntityPage *InOutPage);
  NevercStatus(NEVERC_CALL *GetSharedLibraryPage)(
      void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
      uint64_t Cursor, NevercLinkEntityPage *InOutPage);
  NevercStatus(NEVERC_CALL *GetBitcodeModulePage)(
      void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
      uint64_t Cursor, NevercLinkEntityPage *InOutPage);
  NevercStatus(NEVERC_CALL *GetSectionPage)(
      void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
      uint64_t Cursor, NevercLinkEntityPage *InOutPage);
  NevercStatus(NEVERC_CALL *GetAtomPage)(
      void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
      uint64_t Cursor, NevercLinkEntityPage *InOutPage);
  NevercStatus(NEVERC_CALL *GetSymbolPage)(
      void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
      uint64_t Cursor, NevercLinkEntityPage *InOutPage);
  NevercStatus(NEVERC_CALL *GetEdgePage)(
      void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
      uint64_t Cursor, NevercLinkEntityPage *InOutPage);
  NevercStatus(NEVERC_CALL *GetComdatPage)(
      void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
      uint64_t Cursor, NevercLinkEntityPage *InOutPage);
  NevercStatus(NEVERC_CALL *GetImportPage)(
      void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
      uint64_t Cursor, NevercLinkEntityPage *InOutPage);
  NevercStatus(NEVERC_CALL *GetExportPage)(
      void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
      uint64_t Cursor, NevercLinkEntityPage *InOutPage);
  NevercStatus(NEVERC_CALL *GetUnwindPage)(
      void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
      uint64_t Cursor, NevercLinkEntityPage *InOutPage);
  NevercStatus(NEVERC_CALL *GetSyntheticPage)(
      void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
      uint64_t Cursor, NevercLinkEntityPage *InOutPage);
  NevercStatus(NEVERC_CALL *GetConstraintPage)(
      void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
      uint64_t Cursor, NevercLinkEntityPage *InOutPage);
  NevercStatus(NEVERC_CALL *GetInputInfo)(
      void *Context, NevercTaskHandle Task, NevercLinkInputHandle Input,
      NevercLinkInputInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetArchiveInfo)(
      void *Context, NevercTaskHandle Task, NevercLinkArchiveHandle Archive,
      NevercLinkArchiveInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetArchiveMemberInfo)(
      void *Context, NevercTaskHandle Task,
      NevercLinkArchiveMemberHandle Member,
      NevercLinkArchiveMemberInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetSharedLibraryInfo)(
      void *Context, NevercTaskHandle Task,
      NevercLinkSharedLibraryHandle SharedLibrary,
      NevercLinkSharedLibraryInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetBitcodeModuleInfo)(
      void *Context, NevercTaskHandle Task,
      NevercLinkBitcodeModuleHandle Module,
      NevercLinkBitcodeModuleInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetSectionInfo)(
      void *Context, NevercTaskHandle Task, NevercLinkSectionHandle Section,
      NevercLinkSectionInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetAtomInfo)(
      void *Context, NevercTaskHandle Task, NevercLinkAtomHandle Atom,
      NevercLinkAtomInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetSymbolInfo)(
      void *Context, NevercTaskHandle Task, NevercLinkSymbolHandle Symbol,
      NevercLinkSymbolInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetEdgeInfo)(
      void *Context, NevercTaskHandle Task, NevercLinkEdgeHandle Edge,
      NevercLinkEdgeInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetComdatInfo)(
      void *Context, NevercTaskHandle Task, NevercLinkComdatHandle Comdat,
      NevercLinkComdatInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetImportInfo)(
      void *Context, NevercTaskHandle Task, NevercLinkImportHandle Import,
      NevercLinkImportInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetExportInfo)(
      void *Context, NevercTaskHandle Task, NevercLinkExportHandle Export,
      NevercLinkExportInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetUnwindInfo)(
      void *Context, NevercTaskHandle Task, NevercLinkUnwindHandle Unwind,
      NevercLinkUnwindInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetSyntheticInfo)(
      void *Context, NevercTaskHandle Task,
      NevercLinkSyntheticHandle Synthetic,
      NevercLinkSyntheticInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetConstraintInfo)(
      void *Context, NevercTaskHandle Task,
      NevercLinkConstraintHandle Constraint,
      NevercLinkConstraintInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetProofInfo)(
      void *Context, NevercTaskHandle Task, NevercLinkProofHandle Proof,
      NevercLinkProofInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetBinaryImageInfo)(
      void *Context, NevercTaskHandle Task, NevercBinaryImageHandle Image,
      NevercBinaryImageInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetBinarySegmentPage)(
      void *Context, NevercTaskHandle Task, NevercBinaryImageHandle Image,
      uint64_t Cursor, NevercLinkEntityPage *InOutPage);
  NevercStatus(NEVERC_CALL *GetBinarySectionPage)(
      void *Context, NevercTaskHandle Task, NevercBinaryImageHandle Image,
      uint64_t Cursor, NevercLinkEntityPage *InOutPage);

  NevercStatus(NEVERC_CALL *BeginMutation)(
      void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
      NevercLinkMutationHandle *OutMutation);
  NevercStatus(NEVERC_CALL *CommitMutation)(
      void *Context, NevercTaskHandle Task,
      NevercLinkMutationHandle Mutation);
  NevercStatus(NEVERC_CALL *AbandonMutation)(
      void *Context, NevercTaskHandle Task,
      NevercLinkMutationHandle Mutation);
  NevercStatus(NEVERC_CALL *RebindSymbol)(
      void *Context, NevercTaskHandle Task,
      NevercLinkMutationHandle Mutation, NevercLinkSymbolHandle Symbol,
      NevercLinkAtomHandle Atom);
  NevercStatus(NEVERC_CALL *RetargetEdge)(
      void *Context, NevercTaskHandle Task,
      NevercLinkMutationHandle Mutation, NevercLinkEdgeHandle Edge,
      NevercLinkSymbolHandle TargetSymbol,
      NevercLinkAtomHandle TargetAtom);
  NevercStatus(NEVERC_CALL *SetSymbolRoot)(
      void *Context, NevercTaskHandle Task,
      NevercLinkMutationHandle Mutation, NevercLinkSymbolHandle Symbol,
      NevercBool Root);
  NevercStatus(NEVERC_CALL *SetAtomLive)(
      void *Context, NevercTaskHandle Task,
      NevercLinkMutationHandle Mutation, NevercLinkAtomHandle Atom,
      NevercBool Live);
  NevercStatus(NEVERC_CALL *SetFoldLeader)(
      void *Context, NevercTaskHandle Task,
      NevercLinkMutationHandle Mutation, NevercLinkAtomHandle Atom,
      NevercLinkAtomHandle Leader);
  NevercStatus(NEVERC_CALL *ReplaceAtomContent)(
      void *Context, NevercTaskHandle Task,
      NevercLinkMutationHandle Mutation, NevercLinkAtomHandle Atom,
      NevercByteView Content, uint64_t ZeroFillSize);
  NevercStatus(NEVERC_CALL *CreateSynthetic)(
      void *Context, NevercTaskHandle Task,
      NevercLinkMutationHandle Mutation,
      const NevercLinkSyntheticInfo *Descriptor,
      NevercLinkSyntheticHandle *OutSynthetic);
  NevercStatus(NEVERC_CALL *ReplaceSynthetic)(
      void *Context, NevercTaskHandle Task,
      NevercLinkMutationHandle Mutation,
      NevercLinkSyntheticHandle Synthetic,
      const NevercLinkSyntheticInfo *Descriptor);
  NevercStatus(NEVERC_CALL *EraseSynthetic)(
      void *Context, NevercTaskHandle Task,
      NevercLinkMutationHandle Mutation,
      NevercLinkSyntheticHandle Synthetic);
  NevercStatus(NEVERC_CALL *CreateConstraint)(
      void *Context, NevercTaskHandle Task,
      NevercLinkMutationHandle Mutation,
      const NevercLinkConstraintInfo *Descriptor,
      NevercLinkConstraintHandle *OutConstraint);
  NevercStatus(NEVERC_CALL *ReplaceConstraint)(
      void *Context, NevercTaskHandle Task,
      NevercLinkMutationHandle Mutation,
      NevercLinkConstraintHandle Constraint,
      const NevercLinkConstraintInfo *Descriptor);
  NevercStatus(NEVERC_CALL *EraseConstraint)(
      void *Context, NevercTaskHandle Task,
      NevercLinkMutationHandle Mutation,
      NevercLinkConstraintHandle Constraint);
  NevercStatus(NEVERC_CALL *SetSymbolResolution)(
      void *Context, NevercTaskHandle Task,
      NevercLinkMutationHandle Mutation, NevercLinkSymbolHandle Symbol,
      const NevercLinkSymbolResolutionUpdate *Update);
} NevercLinkAPI;

typedef struct NevercLinkRegistrarAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *RegisterLinkerProvider)(
      void *Context, void *RegistrarContext,
      const NevercLinkerProviderDescriptor *Descriptor);
  NevercStatus(NEVERC_CALL *RegisterObjectMergeProvider)(
      void *Context, void *RegistrarContext,
      const NevercObjectMergeProviderDescriptor *Descriptor);
  NevercStatus(NEVERC_CALL *RegisterBinaryImageVerifier)(
      void *Context, void *RegistrarContext,
      const NevercBinaryImageVerifierDescriptor *Descriptor);
} NevercLinkRegistrarAPI;

typedef struct NevercLinkPhaseAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *GetGraph)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Artifact, NevercLinkPhaseGraphInfo *OutInfo);
  NevercStatus(NEVERC_CALL *PublishGraph)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercLinkGraphHandle Graph, NevercArtifactHandle *OutArtifact);
  NevercStatus(NEVERC_CALL *GetImage)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Artifact, NevercLinkPhaseImageInfo *OutInfo);
} NevercLinkPhaseAPI;

NEVERC_ABI_PACK_END

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* NEVERC_PLUGIN_PLUGINLINK_H */
