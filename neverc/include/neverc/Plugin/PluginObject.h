/*===-- PluginObject.h - NeverC object plugin C ABI --------------- C ---===*/

#ifndef NEVERC_PLUGIN_PLUGINOBJECT_H
#define NEVERC_PLUGIN_PLUGINOBJECT_H

#include "neverc/Plugin/PluginMC.h"                    /* IWYU pragma: export */
#include "neverc/Plugin/PluginSource.h"                /* IWYU pragma: export */
#include "neverc/Plugin/PluginTarget.h"                /* IWYU pragma: export */
#include "neverc/Plugin/Schema/PluginObjectSchema.inc" /* IWYU pragma: export */

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_OBJECT_API_MAJOR UINT16_C(1)
#define NEVERC_OBJECT_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_OBJECT_HIGH UINT64_C(0x4e43504f424a0001)
#define NEVERC_INTERFACE_OBJECT_LOW UINT64_C(0x0000000000000001)
#define NEVERC_OBJECT_INTERFACE_STABILITY NEVERC_INTERFACE_STABLE

#define NEVERC_OBJECT_FORMAT_API_MAJOR UINT16_C(1)
#define NEVERC_OBJECT_FORMAT_API_MINOR UINT16_C(1)
#define NEVERC_INTERFACE_OBJECT_FORMAT_HIGH UINT64_C(0x4e4350464d540001)
#define NEVERC_INTERFACE_OBJECT_FORMAT_LOW UINT64_C(0x0000000000000001)
#define NEVERC_OBJECT_FORMAT_INTERFACE_STABILITY NEVERC_INTERFACE_STABLE

#define NEVERC_OBJECT_PHASE_API_MAJOR UINT16_C(1)
#define NEVERC_OBJECT_PHASE_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_OBJECT_PHASE_HIGH UINT64_C(0x4e43504f42504801)
#define NEVERC_INTERFACE_OBJECT_PHASE_LOW UINT64_C(0x0000000000000001)
#define NEVERC_OBJECT_PHASE_INTERFACE_STABILITY NEVERC_INTERFACE_STABLE

typedef NevercInterfaceID NevercObjectFormatID;

typedef NevercHandle NevercObjectGraphHandle;
typedef NevercHandle NevercObjectSectionHandle;
typedef NevercHandle NevercObjectSymbolHandle;
typedef NevercHandle NevercObjectRelocationHandle;
typedef NevercHandle NevercObjectComdatHandle;
typedef NevercHandle NevercObjectExtensionHandle;
typedef NevercHandle NevercObjectBuilderHandle;
typedef NevercHandle NevercObjectMutationHandle;
typedef NevercHandle NevercObjectImageHandle;
typedef NevercHandle NevercObjectFormatHandle;
typedef NevercHandle NevercObjectProbeHandle;
typedef NevercHandle NevercObjectLayoutProofHandle;
typedef NevercHandle NevercMutableBinaryBuilderHandle;

typedef uint32_t NevercObjectSectionKind;
typedef uint32_t NevercObjectSymbolBinding;
typedef uint32_t NevercObjectSymbolVisibility;
typedef uint32_t NevercObjectSymbolType;
typedef uint32_t NevercObjectSymbolDefinition;
typedef uint32_t NevercObjectRelocationKind;
typedef uint32_t NevercObjectComdatSelection;

typedef uint64_t NevercObjectSectionFlags;
#define NEVERC_OBJECT_SECTION_ALLOCATED (UINT64_C(1) << 0)
#define NEVERC_OBJECT_SECTION_EXECUTABLE (UINT64_C(1) << 1)
#define NEVERC_OBJECT_SECTION_WRITABLE (UINT64_C(1) << 2)
#define NEVERC_OBJECT_SECTION_MERGEABLE (UINT64_C(1) << 3)
#define NEVERC_OBJECT_SECTION_STRINGS (UINT64_C(1) << 4)
#define NEVERC_OBJECT_SECTION_TLS (UINT64_C(1) << 5)
#define NEVERC_OBJECT_SECTION_DEBUG (UINT64_C(1) << 6)
#define NEVERC_OBJECT_SECTION_UNWIND (UINT64_C(1) << 7)
#define NEVERC_OBJECT_SECTION_DISCARDABLE (UINT64_C(1) << 8)
#define NEVERC_OBJECT_SECTION_RETAIN (UINT64_C(1) << 9)

typedef uint64_t NevercObjectSymbolFlags;
#define NEVERC_OBJECT_SYMBOL_IMPORTED (UINT64_C(1) << 0)
#define NEVERC_OBJECT_SYMBOL_EXPORTED (UINT64_C(1) << 1)

typedef uint32_t NevercObjectRelocationTargetKind;
#define NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL UINT32_C(1)
#define NEVERC_OBJECT_RELOCATION_TARGET_SECTION UINT32_C(2)
#define NEVERC_OBJECT_RELOCATION_TARGET_ABSOLUTE UINT32_C(3)
#define NEVERC_OBJECT_RELOCATION_TARGET_FORMAT_EXTENSION UINT32_C(4)

typedef uint32_t NevercObjectArtifactKind;
#define NEVERC_OBJECT_ARTIFACT_UNKNOWN UINT32_C(0)
#define NEVERC_OBJECT_ARTIFACT_RELOCATABLE UINT32_C(1)
#define NEVERC_OBJECT_ARTIFACT_ARCHIVE UINT32_C(2)
#define NEVERC_OBJECT_ARTIFACT_EXECUTABLE_IMAGE UINT32_C(3)
#define NEVERC_OBJECT_ARTIFACT_SHARED_IMAGE UINT32_C(4)
#define NEVERC_OBJECT_ARTIFACT_UNIVERSAL_BINARY UINT32_C(5)

typedef uint64_t NevercObjectFormatFlags;
#define NEVERC_OBJECT_FORMAT_CAN_PROBE (UINT64_C(1) << 0)
#define NEVERC_OBJECT_FORMAT_CAN_READ (UINT64_C(1) << 1)
#define NEVERC_OBJECT_FORMAT_CAN_WRITE (UINT64_C(1) << 2)

/* NevercObjectWriteRequest.Header.Flags, introduced by object-format API 1.1.
 * A host sends nonzero flags only to a format descriptor advertising at least
 * NEVERC_OBJECT_WRITE_REQUEST_FLAGS_API_MINOR; 1.0 callbacks continue to
 * receive zero. The canonical-table request asks an ELF writer to emit
 * distinct canonical `.strtab` and `.shstrtab` sections with every dependent
 * section/symbol/relocation index remapped. It is not a relocatable link:
 * COMDAT groups, linker metadata, symbol multiplicity, and non-name-table
 * payloads remain intact; unrelated SHT_STRTAB sections remain independent.
 * The Android release request additionally makes the serialized ELF image
 * authoritative: it prunes Writer-synthesized symbols and replays release
 * names from the actual serialized section coordinates. DROP_DEBUG_INFO is
 * meaningful only with one of those two ELF policies. ANDROID_KERNEL_RELEASE
 * and DROP_DEBUG_INFO are invalid unless CANONICAL_ELF_TABLES is also set. A
 * 1.1 writer must reject unknown bits and illegal combinations rather than
 * silently ignoring them. */
typedef uint64_t NevercObjectWriteRequestFlags;
#define NEVERC_OBJECT_WRITE_REQUEST_FLAGS_API_MINOR UINT16_C(1)
#define NEVERC_OBJECT_WRITE_CANONICAL_ELF_TABLES (UINT64_C(1) << 0)
#define NEVERC_OBJECT_WRITE_ANDROID_KERNEL_RELEASE (UINT64_C(1) << 1)
#define NEVERC_OBJECT_WRITE_DROP_DEBUG_INFO (UINT64_C(1) << 2)
#define NEVERC_OBJECT_WRITE_REQUEST_KNOWN_FLAGS                                \
  (NEVERC_OBJECT_WRITE_CANONICAL_ELF_TABLES |                                  \
   NEVERC_OBJECT_WRITE_ANDROID_KERNEL_RELEASE |                                \
   NEVERC_OBJECT_WRITE_DROP_DEBUG_INFO)

#define NEVERC_OBJECT_PROBE_MAX_CONFIDENCE UINT32_C(1000)
#define NEVERC_OBJECT_PROBE_MAX_CONSUMED_MINIMUM UINT64_C(65536)

typedef uint32_t NevercObjectImageState;
#define NEVERC_OBJECT_IMAGE_CANDIDATE UINT32_C(1)
#define NEVERC_OBJECT_IMAGE_VERIFIED UINT32_C(2)
#define NEVERC_OBJECT_IMAGE_COMMITTED UINT32_C(3)
#define NEVERC_OBJECT_IMAGE_ABORTED UINT32_C(4)
#define NEVERC_OBJECT_IMAGE_FAILED_PARTIAL UINT32_C(5)

typedef struct NevercObjectProbeRequest NevercObjectProbeRequest;
typedef struct NevercObjectProbeResult NevercObjectProbeResult;
typedef struct NevercObjectReadRequest NevercObjectReadRequest;
typedef struct NevercObjectWriteRequest NevercObjectWriteRequest;
typedef struct NevercMutableBinaryAPI NevercMutableBinaryAPI;

typedef NevercStatus(NEVERC_CALL *NevercObjectProbeFn)(
    void *UserData, const NevercObjectProbeRequest *Request,
    NevercObjectProbeResult *Result);
typedef NevercStatus(NEVERC_CALL *NevercObjectReaderFn)(
    void *UserData, const NevercObjectReadRequest *Request);
typedef NevercStatus(NEVERC_CALL *NevercObjectWriterFn)(
    void *UserData, const NevercObjectWriteRequest *Request);

NEVERC_ABI_PACK_BEGIN

typedef struct NevercObjectGraphInfo {
  NevercABITableHeader Header;
  NevercTargetKey Target;
  NevercStringView ObjectSchemaDigest;
  uint64_t Generation;
  uint64_t SectionCount;
  uint64_t SymbolCount;
  uint64_t RelocationCount;
  uint64_t ComdatCount;
  NevercBool HasLayoutProof;
  uint8_t Reserved8[7];
} NevercObjectGraphInfo;

typedef struct NevercObjectSectionDescriptor {
  NevercABITableHeader Header;
  NevercStringView Name;
  NevercObjectSectionKind Kind;
  uint32_t Reserved;
  NevercObjectSectionFlags Flags;
  uint64_t Alignment;
  NevercByteView Data;
  uint64_t ZeroFillSize;
  NevercObjectComdatHandle Comdat;
  NevercObjectFormatID ExtensionOwner;
  uint32_t ExtensionVersion;
  uint32_t ReservedExtension;
  NevercByteView Extension;
} NevercObjectSectionDescriptor;

typedef NevercObjectSectionDescriptor NevercObjectSectionInfo;

typedef struct NevercObjectSymbolDescriptor {
  NevercABITableHeader Header;
  /* May be empty for a native anonymous symbol. Empty names do not claim the
   * strong-definition namespace. A format writer may reject them when its
   * portable encoding cannot reproduce the native anonymous entry exactly. */
  NevercStringView Name;
  NevercObjectSymbolBinding Binding;
  NevercObjectSymbolVisibility Visibility;
  NevercObjectSymbolType Type;
  NevercObjectSymbolDefinition Definition;
  NevercObjectSectionHandle Section;
  uint64_t Value;
  uint64_t Size;
  uint64_t Alignment;
  NevercObjectComdatHandle Comdat;
  NevercObjectSymbolFlags Flags;
  NevercObjectFormatID ExtensionOwner;
  uint32_t ExtensionVersion;
  uint32_t Reserved;
  NevercByteView Extension;
} NevercObjectSymbolDescriptor;

typedef NevercObjectSymbolDescriptor NevercObjectSymbolInfo;

typedef struct NevercObjectRelocationDescriptor {
  NevercABITableHeader Header;
  NevercObjectSectionHandle Section;
  uint64_t Offset;
  NevercObjectRelocationKind Kind;
  NevercObjectRelocationTargetKind TargetKind;
  uint32_t Width;
  NevercBool IsPCRelative;
  NevercBool IsSigned;
  uint8_t Reserved8[2];
  int64_t Addend;
  NevercObjectSymbolHandle TargetSymbol;
  NevercObjectSectionHandle TargetSection;
  uint64_t TargetValue;
  uint32_t TargetExtensionKind;
  uint32_t Reserved;
  NevercObjectFormatID ExtensionOwner;
  uint32_t ExtensionVersion;
  uint32_t ReservedExtension;
  NevercByteView Extension;
} NevercObjectRelocationDescriptor;

typedef NevercObjectRelocationDescriptor NevercObjectRelocationInfo;

typedef struct NevercObjectComdatDescriptor {
  NevercABITableHeader Header;
  NevercStringView Name;
  NevercObjectComdatSelection Selection;
  uint32_t Reserved;
  NevercObjectComdatHandle AssociatedComdat;
  NevercObjectFormatID ExtensionOwner;
  uint32_t ExtensionVersion;
  uint32_t ReservedExtension;
  NevercByteView Extension;
} NevercObjectComdatDescriptor;

typedef NevercObjectComdatDescriptor NevercObjectComdatInfo;

typedef struct NevercObjectLayoutProofInfo {
  NevercABITableHeader Header;
  uint64_t GraphGeneration;
  NevercTargetID TargetID;
  NevercObjectFormatID FormatID;
} NevercObjectLayoutProofInfo;

typedef struct NevercObjectPhaseGraphInfo {
  NevercABITableHeader Header;
  const struct NevercObjectAPI *Object;
  NevercObjectGraphHandle Graph;
  NevercObjectLayoutProofHandle LayoutProof;
  uint64_t Generation;
} NevercObjectPhaseGraphInfo;

typedef struct NevercObjectImageInfo {
  NevercABITableHeader Header;
  NevercObjectImageHandle Image;
  NevercObjectFormatID FormatID;
  NevercTargetID TargetID;
  uint64_t GraphGeneration;
  NevercObjectImageState State;
  NevercOutputKind OutputKind;
  NevercOutputState OutputState;
  uint32_t Reserved;
  NevercOutputFlags OutputFlags;
  uint64_t Size;
  uint64_t PublicationGeneration;
  uint8_t Digest[32];
  const NevercMutableBinaryAPI *Binary;
  NevercMutableBinaryBuilderHandle Builder;
  NevercStringView Provenance;
  NevercBool HasLayoutReport;
  uint8_t ReservedLayout[7];
  NevercObjectLayoutProofInfo LayoutReport;
} NevercObjectImageInfo;

struct NevercObjectProbeRequest {
  NevercABITableHeader Header;
  NevercTaskHandle Task;
  NevercBufferView Input;
  NevercStringView LogicalPath;
  NevercTargetKey Target;
};

struct NevercObjectProbeResult {
  NevercABITableHeader Header;
  uint32_t Confidence;
  NevercObjectArtifactKind ArtifactKind;
  uint64_t ConsumedMinimum;
};

struct NevercObjectReadRequest {
  NevercABITableHeader Header;
  NevercTaskHandle Task;
  NevercBufferView Input;
  NevercStringView LogicalPath;
  NevercTargetKey Target;
  const struct NevercObjectAPI *Object;
  NevercObjectGraphHandle Graph;
  NevercObjectMutationHandle Mutation;
};

struct NevercObjectWriteRequest {
  NevercABITableHeader Header;
  NevercTaskHandle Task;
  NevercTargetKey Target;
  NevercObjectFormatID FormatID;
  const struct NevercObjectAPI *Object;
  NevercObjectGraphHandle Graph;
  NevercObjectLayoutProofHandle LayoutProof;
  const NevercMutableBinaryAPI *Binary;
  NevercMutableBinaryBuilderHandle Builder;
};

struct NevercMutableBinaryAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *Reserve)(void *Context, NevercTaskHandle Task,
                                     NevercMutableBinaryBuilderHandle Builder,
                                     uint64_t Size);
  NevercStatus(NEVERC_CALL *Write)(void *Context, NevercTaskHandle Task,
                                   NevercMutableBinaryBuilderHandle Builder,
                                   NevercByteView Bytes);
  NevercStatus(NEVERC_CALL *WriteAt)(void *Context, NevercTaskHandle Task,
                                     NevercMutableBinaryBuilderHandle Builder,
                                     uint64_t Offset, NevercByteView Bytes);
  NevercStatus(NEVERC_CALL *Tell)(void *Context, NevercTaskHandle Task,
                                  NevercMutableBinaryBuilderHandle Builder,
                                  uint64_t *OutPosition);
  NevercStatus(NEVERC_CALL *ReadAt)(void *Context, NevercTaskHandle Task,
                                    NevercMutableBinaryBuilderHandle Builder,
                                    uint64_t Offset,
                                    NevercMutableByteView Bytes);
  NevercStatus(NEVERC_CALL *Insert)(void *Context, NevercTaskHandle Task,
                                    NevercMutableBinaryBuilderHandle Builder,
                                    uint64_t Offset, NevercByteView Bytes);
  NevercStatus(NEVERC_CALL *Append)(void *Context, NevercTaskHandle Task,
                                    NevercMutableBinaryBuilderHandle Builder,
                                    NevercByteView Bytes);
  NevercStatus(NEVERC_CALL *Resize)(void *Context, NevercTaskHandle Task,
                                    NevercMutableBinaryBuilderHandle Builder,
                                    uint64_t Size);
};

typedef struct NevercObjectFormatDescriptor {
  NevercABITableHeader Header;
  NevercObjectFormatID FormatID;
  NevercStringView CanonicalName;
  NevercStringArrayView Aliases;
  NevercInterfaceIDArrayView SupportedTargets;
  NevercStringView DefaultExtension;
  NevercObjectFormatFlags Flags;
  NevercObjectProbeFn Probe;
  NevercObjectReaderFn Reader;
  NevercObjectWriterFn Writer;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercObjectFormatDescriptor;

typedef struct NevercObjectAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *GetGraphInfo)(void *Context, NevercTaskHandle Task,
                                          NevercObjectGraphHandle Graph,
                                          NevercObjectGraphInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetFirstSection)(
      void *Context, NevercTaskHandle Task, NevercObjectGraphHandle Graph,
      NevercObjectSectionHandle *OutSection);
  NevercStatus(NEVERC_CALL *GetNextSection)(
      void *Context, NevercTaskHandle Task, NevercObjectSectionHandle Section,
      NevercObjectSectionHandle *OutSection);
  NevercStatus(NEVERC_CALL *GetSectionInfo)(void *Context,
                                            NevercTaskHandle Task,
                                            NevercObjectSectionHandle Section,
                                            NevercObjectSectionInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetFirstSymbol)(
      void *Context, NevercTaskHandle Task, NevercObjectGraphHandle Graph,
      NevercObjectSymbolHandle *OutSymbol);
  NevercStatus(NEVERC_CALL *GetNextSymbol)(void *Context, NevercTaskHandle Task,
                                           NevercObjectSymbolHandle Symbol,
                                           NevercObjectSymbolHandle *OutSymbol);
  NevercStatus(NEVERC_CALL *GetSymbolInfo)(void *Context, NevercTaskHandle Task,
                                           NevercObjectSymbolHandle Symbol,
                                           NevercObjectSymbolInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetFirstRelocation)(
      void *Context, NevercTaskHandle Task, NevercObjectGraphHandle Graph,
      NevercObjectRelocationHandle *OutRelocation);
  NevercStatus(NEVERC_CALL *GetNextRelocation)(
      void *Context, NevercTaskHandle Task,
      NevercObjectRelocationHandle Relocation,
      NevercObjectRelocationHandle *OutRelocation);
  NevercStatus(NEVERC_CALL *GetRelocationInfo)(
      void *Context, NevercTaskHandle Task,
      NevercObjectRelocationHandle Relocation,
      NevercObjectRelocationInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetFirstComdat)(
      void *Context, NevercTaskHandle Task, NevercObjectGraphHandle Graph,
      NevercObjectComdatHandle *OutComdat);
  NevercStatus(NEVERC_CALL *GetNextComdat)(void *Context, NevercTaskHandle Task,
                                           NevercObjectComdatHandle Comdat,
                                           NevercObjectComdatHandle *OutComdat);
  NevercStatus(NEVERC_CALL *GetComdatInfo)(void *Context, NevercTaskHandle Task,
                                           NevercObjectComdatHandle Comdat,
                                           NevercObjectComdatInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetLayoutProof)(
      void *Context, NevercTaskHandle Task, NevercObjectGraphHandle Graph,
      NevercObjectLayoutProofHandle *OutProof);
  NevercStatus(NEVERC_CALL *GetLayoutProofInfo)(
      void *Context, NevercTaskHandle Task, NevercObjectLayoutProofHandle Proof,
      NevercObjectLayoutProofInfo *OutInfo);
  NevercStatus(NEVERC_CALL *BeginMutation)(
      void *Context, NevercTaskHandle Task, NevercObjectGraphHandle Graph,
      NevercObjectMutationHandle *OutMutation);
  NevercStatus(NEVERC_CALL *CommitMutation)(
      void *Context, NevercTaskHandle Task,
      NevercObjectMutationHandle Mutation);
  NevercStatus(NEVERC_CALL *AbandonMutation)(
      void *Context, NevercTaskHandle Task,
      NevercObjectMutationHandle Mutation);
  NevercStatus(NEVERC_CALL *CreateSection)(
      void *Context, NevercTaskHandle Task, NevercObjectMutationHandle Mutation,
      const NevercObjectSectionDescriptor *Descriptor,
      NevercObjectSectionHandle *OutSection);
  NevercStatus(NEVERC_CALL *ReplaceSection)(
      void *Context, NevercTaskHandle Task, NevercObjectMutationHandle Mutation,
      NevercObjectSectionHandle Section,
      const NevercObjectSectionDescriptor *Descriptor);
  NevercStatus(NEVERC_CALL *MoveSectionBefore)(
      void *Context, NevercTaskHandle Task, NevercObjectMutationHandle Mutation,
      NevercObjectSectionHandle Section, NevercObjectSectionHandle Position);
  NevercStatus(NEVERC_CALL *EraseSection)(void *Context, NevercTaskHandle Task,
                                          NevercObjectMutationHandle Mutation,
                                          NevercObjectSectionHandle Section);
  NevercStatus(NEVERC_CALL *CreateSymbol)(
      void *Context, NevercTaskHandle Task, NevercObjectMutationHandle Mutation,
      const NevercObjectSymbolDescriptor *Descriptor,
      NevercObjectSymbolHandle *OutSymbol);
  NevercStatus(NEVERC_CALL *ReplaceSymbol)(
      void *Context, NevercTaskHandle Task, NevercObjectMutationHandle Mutation,
      NevercObjectSymbolHandle Symbol,
      const NevercObjectSymbolDescriptor *Descriptor);
  NevercStatus(NEVERC_CALL *MoveSymbolBefore)(
      void *Context, NevercTaskHandle Task, NevercObjectMutationHandle Mutation,
      NevercObjectSymbolHandle Symbol, NevercObjectSymbolHandle Position);
  NevercStatus(NEVERC_CALL *EraseSymbol)(void *Context, NevercTaskHandle Task,
                                         NevercObjectMutationHandle Mutation,
                                         NevercObjectSymbolHandle Symbol);
  NevercStatus(NEVERC_CALL *CreateRelocation)(
      void *Context, NevercTaskHandle Task, NevercObjectMutationHandle Mutation,
      const NevercObjectRelocationDescriptor *Descriptor,
      NevercObjectRelocationHandle *OutRelocation);
  NevercStatus(NEVERC_CALL *ReplaceRelocation)(
      void *Context, NevercTaskHandle Task, NevercObjectMutationHandle Mutation,
      NevercObjectRelocationHandle Relocation,
      const NevercObjectRelocationDescriptor *Descriptor);
  NevercStatus(NEVERC_CALL *MoveRelocationBefore)(
      void *Context, NevercTaskHandle Task, NevercObjectMutationHandle Mutation,
      NevercObjectRelocationHandle Relocation,
      NevercObjectRelocationHandle Position);
  NevercStatus(NEVERC_CALL *EraseRelocation)(
      void *Context, NevercTaskHandle Task, NevercObjectMutationHandle Mutation,
      NevercObjectRelocationHandle Relocation);
  NevercStatus(NEVERC_CALL *CreateComdat)(
      void *Context, NevercTaskHandle Task, NevercObjectMutationHandle Mutation,
      const NevercObjectComdatDescriptor *Descriptor,
      NevercObjectComdatHandle *OutComdat);
  NevercStatus(NEVERC_CALL *ReplaceComdat)(
      void *Context, NevercTaskHandle Task, NevercObjectMutationHandle Mutation,
      NevercObjectComdatHandle Comdat,
      const NevercObjectComdatDescriptor *Descriptor);
  NevercStatus(NEVERC_CALL *MoveComdatBefore)(
      void *Context, NevercTaskHandle Task, NevercObjectMutationHandle Mutation,
      NevercObjectComdatHandle Comdat, NevercObjectComdatHandle Position);
  NevercStatus(NEVERC_CALL *EraseComdat)(void *Context, NevercTaskHandle Task,
                                         NevercObjectMutationHandle Mutation,
                                         NevercObjectComdatHandle Comdat);
} NevercObjectAPI;

typedef struct NevercObjectFormatAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *RegisterFormat)(
      void *Context, void *RegistrarContext,
      const NevercObjectFormatDescriptor *Descriptor);
} NevercObjectFormatAPI;

typedef struct NevercObjectPhaseAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *GetGraph)(void *Context,
                                      const NevercPhaseFrame *Frame,
                                      NevercArtifactHandle Artifact,
                                      NevercObjectPhaseGraphInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetImage)(void *Context,
                                      const NevercPhaseFrame *Frame,
                                      NevercArtifactHandle Artifact,
                                      NevercObjectImageInfo *OutInfo);
} NevercObjectPhaseAPI;

NEVERC_ABI_PACK_END

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_PLUGIN_PLUGINOBJECT_H */
