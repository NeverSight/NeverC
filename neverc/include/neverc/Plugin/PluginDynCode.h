/*===-- PluginDynCode.h - NeverC dyncode plugin C ABI ------------- C ---===*/
/*                                                                          */
/* First public dyncode (position-independent flat image) plugin ABI.       */
/* Covers the frozen DynCodeRequest/TargetDescriptor, the typed image and   */
/* report products, the import/extractor/charset/binary-verifier providers,  */
/* and the phase-frame accessors that expose those artifacts to plugins.    */
/*                                                                          */
/* This header reuses Core/Source handles plus the IR, MIR, Target and      */
/* Object domain tables. It intentionally does NOT include PluginLink.h:    */
/* dyncode consumes exactly one verified ObjectGraph and never routes       */
/* through the linker.                                                       */
/*===----------------------------------------------------------------------===*/

#ifndef NEVERC_PLUGIN_PLUGINDYNCODE_H
#define NEVERC_PLUGIN_PLUGINDYNCODE_H

#include "neverc/Plugin/PluginIR.h"     /* IWYU pragma: export */
#include "neverc/Plugin/PluginMIR.h"    /* IWYU pragma: export */
#include "neverc/Plugin/PluginObject.h" /* IWYU pragma: export */

#ifdef __cplusplus
extern "C" {
#endif

/*===----------------------------------------------------------------------===*/
/* Interface identity and versions.                                          */
/*===----------------------------------------------------------------------===*/

#define NEVERC_DYNCODE_API_MAJOR UINT16_C(1)
#define NEVERC_DYNCODE_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_DYNCODE_HIGH UINT64_C(0x4e43504459430001)
#define NEVERC_INTERFACE_DYNCODE_LOW UINT64_C(0x0000000000000001)
#define NEVERC_DYNCODE_INTERFACE_STABILITY NEVERC_INTERFACE_STABLE

#define NEVERC_DYNCODE_REGISTRAR_API_MAJOR UINT16_C(1)
#define NEVERC_DYNCODE_REGISTRAR_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_DYNCODE_REGISTRAR_HIGH UINT64_C(0x4e43504459520001)
#define NEVERC_INTERFACE_DYNCODE_REGISTRAR_LOW UINT64_C(0x0000000000000001)
#define NEVERC_DYNCODE_REGISTRAR_INTERFACE_STABILITY NEVERC_INTERFACE_STABLE

#define NEVERC_DYNCODE_PHASE_API_MAJOR UINT16_C(1)
#define NEVERC_DYNCODE_PHASE_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_DYNCODE_PHASE_HIGH UINT64_C(0x4e43504459500001)
#define NEVERC_INTERFACE_DYNCODE_PHASE_LOW UINT64_C(0x0000000000000001)
#define NEVERC_DYNCODE_PHASE_INTERFACE_STABILITY NEVERC_INTERFACE_STABLE

typedef NevercInterfaceID NevercDynCodeTargetID;

/*===----------------------------------------------------------------------===*/
/* Opaque handles.                                                           */
/*===----------------------------------------------------------------------===*/

typedef NevercHandle NevercDynCodeRequestHandle;
typedef NevercHandle NevercDynCodeTargetHandle;
typedef NevercHandle NevercDynCodePlanHandle;
typedef NevercHandle NevercDynCodeImageHandle;
typedef NevercHandle NevercDynCodeImageBuilderHandle;
typedef NevercHandle NevercDynCodeReportHandle;
typedef NevercHandle NevercDynCodeExternalRefHandle;
typedef NevercHandle NevercDynCodeSectionMapHandle;
typedef NevercHandle NevercDynCodeSymbolMapHandle;
typedef NevercHandle NevercDynCodeRelocationHandle;
typedef NevercHandle NevercDynCodeImportProviderHandle;

/*===----------------------------------------------------------------------===*/
/* Fixed-width discriminants (never C/C++ enum types).                       */
/*===----------------------------------------------------------------------===*/

typedef uint32_t NevercDynCodeExecutionLevel;
#define NEVERC_DYNCODE_LEVEL_USER UINT32_C(1)
#define NEVERC_DYNCODE_LEVEL_KERNEL UINT32_C(2)

typedef uint32_t NevercDynCodeEntryPolicyKind;
#define NEVERC_DYNCODE_ENTRY_EXPLICIT UINT32_C(1)
#define NEVERC_DYNCODE_ENTRY_CANDIDATE_LIST UINT32_C(2)
#define NEVERC_DYNCODE_ENTRY_AT_ZERO UINT32_C(3)

typedef uint32_t NevercDynCodeImageState;
#define NEVERC_DYNCODE_IMAGE_CANDIDATE UINT32_C(1)
#define NEVERC_DYNCODE_IMAGE_VERIFIED UINT32_C(2)
#define NEVERC_DYNCODE_IMAGE_COMMITTED UINT32_C(3)
#define NEVERC_DYNCODE_IMAGE_ABORTED UINT32_C(4)
#define NEVERC_DYNCODE_IMAGE_FAILED_PARTIAL UINT32_C(5)

typedef uint32_t NevercDynCodeSectionDisposition;
#define NEVERC_DYNCODE_SECTION_SELECTED UINT32_C(1)
#define NEVERC_DYNCODE_SECTION_DISCARDED UINT32_C(2)

typedef uint32_t NevercDynCodeRelocationDisposition;
#define NEVERC_DYNCODE_RELOC_APPLIED UINT32_C(1)
#define NEVERC_DYNCODE_RELOC_RUNTIME_CONTRACT UINT32_C(2)
#define NEVERC_DYNCODE_RELOC_REJECTED UINT32_C(3)
#define NEVERC_DYNCODE_RELOC_PENDING UINT32_C(4)

typedef uint32_t NevercDynCodeExternalRefDisposition;
#define NEVERC_DYNCODE_EXTERNAL_ELIMINATED UINT32_C(1)
#define NEVERC_DYNCODE_EXTERNAL_RESOLVED_INTERNAL UINT32_C(2)
#define NEVERC_DYNCODE_EXTERNAL_RUNTIME_CONTRACT UINT32_C(3)
#define NEVERC_DYNCODE_EXTERNAL_UNRESOLVED UINT32_C(4)

typedef uint32_t NevercDynCodeImportKind;
#define NEVERC_DYNCODE_IMPORT_SYSCALL UINT32_C(1)
#define NEVERC_DYNCODE_IMPORT_PEB UINT32_C(2)
#define NEVERC_DYNCODE_IMPORT_KERNEL UINT32_C(3)
#define NEVERC_DYNCODE_IMPORT_CUSTOM UINT32_C(4)

/*===----------------------------------------------------------------------===*/
/* Flag words.                                                               */
/*===----------------------------------------------------------------------===*/

typedef uint64_t NevercDynCodePICFlags;
#define NEVERC_DYNCODE_PIC_ALLOW_ABSOLUTE (UINT64_C(1) << 0)
#define NEVERC_DYNCODE_PIC_ALLOW_PC_RELATIVE (UINT64_C(1) << 1)
#define NEVERC_DYNCODE_PIC_ALLOW_WRITABLE_DATA (UINT64_C(1) << 2)
#define NEVERC_DYNCODE_PIC_REQUIRE_ENTRY_AT_ZERO (UINT64_C(1) << 3)

typedef uint64_t NevercDynCodeRequestFlags;
#define NEVERC_DYNCODE_REQUEST_REWRITE_BAD_BYTES (UINT64_C(1) << 0)
#define NEVERC_DYNCODE_REQUEST_INLINE (UINT64_C(1) << 1)
#define NEVERC_DYNCODE_REQUEST_ALL_BLR (UINT64_C(1) << 2)
#define NEVERC_DYNCODE_REQUEST_KEEP_OBJECT (UINT64_C(1) << 3)
#define NEVERC_DYNCODE_REQUEST_DETERMINISTIC (UINT64_C(1) << 4)

typedef uint64_t NevercDynCodeTargetFlags;
#define NEVERC_DYNCODE_TARGET_SUPPORTS_USER (UINT64_C(1) << 0)
#define NEVERC_DYNCODE_TARGET_SUPPORTS_KERNEL (UINT64_C(1) << 1)

/*===----------------------------------------------------------------------===*/
/* Forward declarations for request/result structs used in callbacks.        */
/*===----------------------------------------------------------------------===*/

typedef struct NevercDynCodeImportRequest NevercDynCodeImportRequest;
typedef struct NevercDynCodeImportResult NevercDynCodeImportResult;
typedef struct NevercDynCodeExtractionRequest NevercDynCodeExtractionRequest;
typedef struct NevercDynCodeCharsetEncodeRequest NevercDynCodeCharsetEncodeRequest;
typedef struct NevercDynCodeCharsetEncodeResult NevercDynCodeCharsetEncodeResult;
typedef struct NevercDynCodeBinaryVerifyRequest NevercDynCodeBinaryVerifyRequest;
typedef struct NevercDynCodeBinaryVerifyResult NevercDynCodeBinaryVerifyResult;

typedef NevercStatus(NEVERC_CALL *NevercDynCodeTransformFn)(
    void *UserData, const NevercPhaseFrame *Frame);
typedef NevercStatus(NEVERC_CALL *NevercDynCodeImportProviderFn)(
    void *UserData, const NevercDynCodeImportRequest *Request,
    NevercDynCodeImportResult *Result);
typedef NevercStatus(NEVERC_CALL *NevercDynCodeExtractorFn)(
    void *UserData, const NevercDynCodeExtractionRequest *Request);
typedef NevercStatus(NEVERC_CALL *NevercDynCodeCharsetEncoderFn)(
    void *UserData, const NevercDynCodeCharsetEncodeRequest *Request,
    NevercDynCodeCharsetEncodeResult *Result);
typedef NevercStatus(NEVERC_CALL *NevercDynCodeBinaryVerifierFn)(
    void *UserData, const NevercDynCodeBinaryVerifyRequest *Request,
    NevercDynCodeBinaryVerifyResult *Result);

NEVERC_ABI_PACK_BEGIN

/*===----------------------------------------------------------------------===*/
/* Frozen request (section 2.3).                                             */
/*===----------------------------------------------------------------------===*/

typedef struct NevercDynCodeEntryPolicy {
  NevercDynCodeEntryPolicyKind Kind;
  uint32_t Reserved;
  NevercStringView ExplicitSymbol;
  NevercStringArrayView CandidateSymbols;
} NevercDynCodeEntryPolicy;

typedef struct NevercDynCodeRequestInfo {
  NevercABITableHeader Header;
  NevercTargetKey Target;
  NevercStringView TargetSchemaDigest;
  NevercObjectFormatID ObjectFormat;
  NevercDynCodeExecutionLevel ExecutionLevel;
  uint32_t Reserved;
  NevercDynCodeEntryPolicy Entry;
  NevercDynCodePICFlags PICFlags;
  NevercDynCodeRequestFlags Flags;
  uint64_t MaxLength;
  uint64_t Alignment;
  uint32_t PadByte;
  uint32_t BadByteCount;
  NevercByteView BadByteSet;
  NevercStringView BadByteProfile;
  NevercStringView CharsetProviderID;
  NevercStringView TransformConfigNamespace;
  NevercStringView MainOutputSinkID;
  NevercStringView ObjectOutputSinkID;
  NevercStringView ReportOutputSinkID;
  uint8_t RequestDigest[32];
} NevercDynCodeRequestInfo;

/*===----------------------------------------------------------------------===*/
/* Target descriptor (section 2.3).                                          */
/*===----------------------------------------------------------------------===*/

typedef struct NevercDynCodeTargetDescriptor {
  NevercABITableHeader Header;
  NevercDynCodeTargetID DynCodeTargetID;
  NevercTargetKey Target;
  NevercStringView TargetSchemaDigest;
  NevercObjectFormatID ObjectFormat;
  NevercStringView CodeSectionRole;
  NevercStringView CodeSectionName;
  uint64_t DefaultFragmentAlignment;
  NevercDynCodeTargetFlags Flags;
  NevercInterfaceID RelocationApplicatorID;
  NevercInterfaceID UserImportStrategyID;
  NevercInterfaceID KernelImportStrategyID;
  NevercInterfaceID EntryABIID;
  NevercDynCodePICFlags PICConstraints;
  NevercInterfaceID ExtensionOwner;
  uint32_t ExtensionVersion;
  uint32_t ReservedExtension;
  NevercByteView Extension;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercDynCodeTargetDescriptor;

/*===----------------------------------------------------------------------===*/
/* Image, report and audit records (sections 2.7, 2.8).                      */
/*===----------------------------------------------------------------------===*/

typedef struct NevercDynCodeImageInfo {
  NevercABITableHeader Header;
  NevercDynCodeImageHandle Image;
  NevercDynCodeTargetID TargetID;
  NevercObjectFormatID ObjectFormat;
  NevercDynCodeImageState State;
  NevercOutputState OutputState;
  NevercOutputFlags OutputFlags;
  uint64_t Generation;
  uint64_t Size;
  uint64_t OutputAlignment;
  uint64_t EntryOffset;
  NevercStringView EntrySymbol;
  uint8_t Digest[32];
  const NevercMutableBinaryAPI *Binary;
  NevercDynCodeImageBuilderHandle Builder;
  uint64_t SectionMapCount;
  uint64_t SymbolMapCount;
  uint64_t RelocationCount;
  uint64_t ExternalRefCount;
} NevercDynCodeImageInfo;

typedef struct NevercDynCodeSectionMapInfo {
  NevercABITableHeader Header;
  NevercStringView SourceName;
  NevercObjectSectionKind SourceKind;
  NevercDynCodeSectionDisposition Disposition;
  uint64_t OutputOffset;
  uint64_t OutputSize;
  uint64_t Alignment;
  NevercStringView Reason;
} NevercDynCodeSectionMapInfo;

typedef struct NevercDynCodeSymbolMapInfo {
  NevercABITableHeader Header;
  NevercStringView Name;
  uint64_t OutputOffset;
  NevercBool IsEntry;
  uint8_t Reserved8[7];
} NevercDynCodeSymbolMapInfo;

typedef struct NevercDynCodeRelocationInfo {
  NevercABITableHeader Header;
  uint64_t SiteOffset;
  uint64_t TargetOffset;
  int64_t Addend;
  uint32_t Width;
  NevercBool IsPCRelative;
  uint8_t Reserved8[3];
  NevercObjectRelocationKind Kind;
  NevercDynCodeRelocationDisposition Disposition;
  NevercStringView RuntimeContract;
} NevercDynCodeRelocationInfo;

typedef struct NevercDynCodeExternalRefInfo {
  NevercABITableHeader Header;
  NevercStringView Symbol;
  NevercDynCodeExternalRefDisposition Disposition;
  NevercDynCodeImportKind ImportKind;
  NevercStringView ResolverContract;
  NevercStringView ProviderID;
} NevercDynCodeExternalRefInfo;

typedef struct NevercDynCodeReportInfo {
  NevercABITableHeader Header;
  uint8_t RequestDigest[32];
  uint8_t RouteDigest[32];
  uint8_t InputDigest[32];
  uint8_t OutputDigest[32];
  uint64_t SelectedSectionCount;
  uint64_t RejectedSectionCount;
  uint64_t PatchedRelocationCount;
  uint64_t RuntimeContractCount;
  uint64_t RemainingExternalCount;
  uint64_t ImageSize;
  uint64_t Alignment;
  uint64_t PaddingSize;
  uint64_t BadByteHitCount;
  uint64_t EntryOffset;
  NevercStringView EntrySymbol;
} NevercDynCodeReportInfo;

/*===----------------------------------------------------------------------===*/
/* Provider request/result payloads.                                         */
/*===----------------------------------------------------------------------===*/

struct NevercDynCodeImportRequest {
  NevercABITableHeader Header;
  NevercTaskHandle Task;
  NevercDynCodeRequestHandle Request;
  NevercStringView Symbol;
  NevercDynCodeExecutionLevel ExecutionLevel;
  NevercDynCodeImportKind Kind;
  NevercIRModuleHandle Module;
};

struct NevercDynCodeImportResult {
  NevercABITableHeader Header;
  NevercDynCodeExternalRefDisposition Disposition;
  NevercDynCodeImportKind ResolvedKind;
  NevercStringView ResolverContract;
  NevercStringView CookieParameter;
  NevercBool EntryABIChanged;
  uint8_t Reserved8[7];
};

struct NevercDynCodeExtractionRequest {
  NevercABITableHeader Header;
  NevercTaskHandle Task;
  NevercDynCodeRequestHandle Request;
  NevercDynCodeTargetID TargetID;
  const struct NevercObjectAPI *Object;
  NevercObjectGraphHandle Graph;
  NevercDynCodePlanHandle Plan;
  NevercDynCodeImageHandle Image;
  const NevercMutableBinaryAPI *Binary;
  NevercDynCodeImageBuilderHandle Builder;
};

struct NevercDynCodeCharsetEncodeRequest {
  NevercABITableHeader Header;
  NevercTaskHandle Task;
  NevercDynCodeRequestHandle Request;
  NevercDynCodeImageHandle Image;
  const NevercMutableBinaryAPI *Binary;
  NevercDynCodeImageBuilderHandle Builder;
  NevercByteView BadByteSet;
};

struct NevercDynCodeCharsetEncodeResult {
  NevercABITableHeader Header;
  uint64_t EncodedEntryOffset;
  NevercBool EntryUpdated;
  uint8_t Reserved8[7];
  NevercStringView DecoderStubProvenance;
};

struct NevercDynCodeBinaryVerifyRequest {
  NevercABITableHeader Header;
  NevercTaskHandle Task;
  NevercDynCodeRequestHandle Request;
  NevercDynCodeTargetID TargetID;
  NevercDynCodeImageHandle Image;
  const NevercMutableBinaryAPI *Binary;
  NevercDynCodeImageBuilderHandle Builder;
  uint64_t ImageGeneration;
  uint8_t ImageDigest[32];
};

struct NevercDynCodeBinaryVerifyResult {
  NevercABITableHeader Header;
  NevercBool Accepted;
  uint8_t Reserved8[7];
  NevercStringView Diagnostic;
};

/*===----------------------------------------------------------------------===*/
/* Provider descriptors.                                                     */
/*===----------------------------------------------------------------------===*/

typedef struct NevercDynCodeImportProviderDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID ProviderID;
  NevercStringView CanonicalName;
  NevercDynCodeImportKind Kind;
  NevercTargetKey Target;
  NevercDynCodeExecutionLevel ExecutionLevel;
  uint32_t Reserved;
  NevercStringArrayView SymbolMatchers;
  NevercDynCodeImportProviderFn Import;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercDynCodeImportProviderDescriptor;

typedef struct NevercDynCodeCharsetEncoderDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID EncoderID;
  NevercStringView CanonicalName;
  NevercBool Deterministic;
  uint8_t Reserved8[7];
  NevercDynCodeCharsetEncoderFn Encode;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercDynCodeCharsetEncoderDescriptor;

typedef struct NevercDynCodeBinaryVerifierDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID VerifierID;
  NevercDynCodeTargetID TargetID;
  NevercTargetKey Target;
  NevercDynCodeBinaryVerifierFn Verify;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercDynCodeBinaryVerifierDescriptor;

typedef struct NevercDynCodeExtractorDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID ExtractorID;
  NevercDynCodeTargetID TargetID;
  NevercTargetKey Target;
  NevercObjectFormatID ObjectFormat;
  NevercDynCodeExtractorFn Extract;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercDynCodeExtractorDescriptor;

/*===----------------------------------------------------------------------===*/
/* Phase-frame accessor table.                                               */
/*===----------------------------------------------------------------------===*/

typedef struct NevercDynCodePhaseInfo {
  NevercABITableHeader Header;
  const struct NevercDynCodeAPI *DynCode;
  NevercDynCodeRequestHandle Request;
  NevercDynCodePlanHandle Plan;
  NevercDynCodeImageHandle Image;
  NevercDynCodeReportHandle Report;
  uint64_t Generation;
} NevercDynCodePhaseInfo;

typedef struct NevercDynCodePhaseAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *GetPhaseInfo)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercDynCodePhaseInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetRequest)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Artifact, NevercDynCodeRequestHandle *OutRequest);
  NevercStatus(NEVERC_CALL *GetImage)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Artifact, NevercDynCodeImageHandle *OutImage);
  NevercStatus(NEVERC_CALL *GetReport)(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Artifact, NevercDynCodeReportHandle *OutReport);
} NevercDynCodePhaseAPI;

/*===----------------------------------------------------------------------===*/
/* Query table: read the frozen request, target, image and report.          */
/*===----------------------------------------------------------------------===*/

typedef struct NevercDynCodeAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *GetRequestInfo)(
      void *Context, NevercTaskHandle Task, NevercDynCodeRequestHandle Request,
      NevercDynCodeRequestInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetTargetDescriptor)(
      void *Context, NevercTaskHandle Task, NevercDynCodeRequestHandle Request,
      NevercDynCodeTargetDescriptor *OutDescriptor);
  NevercStatus(NEVERC_CALL *GetImageInfo)(
      void *Context, NevercTaskHandle Task, NevercDynCodeImageHandle Image,
      NevercDynCodeImageInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetReportInfo)(
      void *Context, NevercTaskHandle Task, NevercDynCodeReportHandle Report,
      NevercDynCodeReportInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetFirstSectionMap)(
      void *Context, NevercTaskHandle Task, NevercDynCodeImageHandle Image,
      NevercDynCodeSectionMapHandle *OutSection);
  NevercStatus(NEVERC_CALL *GetNextSectionMap)(
      void *Context, NevercTaskHandle Task,
      NevercDynCodeSectionMapHandle Section,
      NevercDynCodeSectionMapHandle *OutSection);
  NevercStatus(NEVERC_CALL *GetSectionMapInfo)(
      void *Context, NevercTaskHandle Task,
      NevercDynCodeSectionMapHandle Section,
      NevercDynCodeSectionMapInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetFirstSymbolMap)(
      void *Context, NevercTaskHandle Task, NevercDynCodeImageHandle Image,
      NevercDynCodeSymbolMapHandle *OutSymbol);
  NevercStatus(NEVERC_CALL *GetNextSymbolMap)(
      void *Context, NevercTaskHandle Task, NevercDynCodeSymbolMapHandle Symbol,
      NevercDynCodeSymbolMapHandle *OutSymbol);
  NevercStatus(NEVERC_CALL *GetSymbolMapInfo)(
      void *Context, NevercTaskHandle Task, NevercDynCodeSymbolMapHandle Symbol,
      NevercDynCodeSymbolMapInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetFirstRelocation)(
      void *Context, NevercTaskHandle Task, NevercDynCodeImageHandle Image,
      NevercDynCodeRelocationHandle *OutRelocation);
  NevercStatus(NEVERC_CALL *GetNextRelocation)(
      void *Context, NevercTaskHandle Task,
      NevercDynCodeRelocationHandle Relocation,
      NevercDynCodeRelocationHandle *OutRelocation);
  NevercStatus(NEVERC_CALL *GetRelocationInfo)(
      void *Context, NevercTaskHandle Task,
      NevercDynCodeRelocationHandle Relocation,
      NevercDynCodeRelocationInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetFirstExternalRef)(
      void *Context, NevercTaskHandle Task, NevercDynCodeImageHandle Image,
      NevercDynCodeExternalRefHandle *OutRef);
  NevercStatus(NEVERC_CALL *GetNextExternalRef)(
      void *Context, NevercTaskHandle Task, NevercDynCodeExternalRefHandle Ref,
      NevercDynCodeExternalRefHandle *OutRef);
  NevercStatus(NEVERC_CALL *GetExternalRefInfo)(
      void *Context, NevercTaskHandle Task, NevercDynCodeExternalRefHandle Ref,
      NevercDynCodeExternalRefInfo *OutInfo);
} NevercDynCodeAPI;

/*===----------------------------------------------------------------------===*/
/* Registrar table: register dyncode target and providers on the session.    */
/*===----------------------------------------------------------------------===*/

typedef struct NevercDynCodeRegistrarAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *RegisterTarget)(
      void *Context, void *RegistrarContext,
      const NevercDynCodeTargetDescriptor *Descriptor);
  NevercStatus(NEVERC_CALL *RegisterImportProvider)(
      void *Context, void *RegistrarContext,
      const NevercDynCodeImportProviderDescriptor *Descriptor);
  NevercStatus(NEVERC_CALL *RegisterExtractor)(
      void *Context, void *RegistrarContext,
      const NevercDynCodeExtractorDescriptor *Descriptor);
  NevercStatus(NEVERC_CALL *RegisterCharsetEncoder)(
      void *Context, void *RegistrarContext,
      const NevercDynCodeCharsetEncoderDescriptor *Descriptor);
  NevercStatus(NEVERC_CALL *RegisterBinaryVerifier)(
      void *Context, void *RegistrarContext,
      const NevercDynCodeBinaryVerifierDescriptor *Descriptor);
} NevercDynCodeRegistrarAPI;

NEVERC_ABI_PACK_END

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_PLUGIN_PLUGINDYNCODE_H */
