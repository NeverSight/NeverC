/*===-- PluginLTO.h - NeverC LTO plugin C ABI --------------------- C ---===*/

#ifndef NEVERC_PLUGIN_PLUGINLTO_H
#define NEVERC_PLUGIN_PLUGINLTO_H

#include "neverc/Plugin/PluginLink.h" /* IWYU pragma: export */

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_LTO_API_MAJOR UINT16_C(1)
#define NEVERC_LTO_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_LTO_HIGH UINT64_C(0x4e43504c544f0001)
#define NEVERC_INTERFACE_LTO_LOW UINT64_C(0x0000000000000001)
#define NEVERC_LTO_INTERFACE_STABILITY NEVERC_INTERFACE_STABLE

#define NEVERC_LTO_REGISTRAR_API_MAJOR UINT16_C(1)
#define NEVERC_LTO_REGISTRAR_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_LTO_REGISTRAR_HIGH UINT64_C(0x4e43504c544f5201)
#define NEVERC_INTERFACE_LTO_REGISTRAR_LOW UINT64_C(0x0000000000000001)
#define NEVERC_LTO_REGISTRAR_INTERFACE_STABILITY NEVERC_INTERFACE_STABLE

/*
 * Ownership follows PluginLink.h: handles are host-owned and task-scoped;
 * views passed to providers are borrowed for the callback; caller-buffer
 * queries never retain Data; successful product candidates are adopted by
 * the host and failed candidates remain the provider's responsibility.
 */

typedef NevercHandle NevercLTORequestHandle;
typedef NevercHandle NevercLTOModuleHandle;
typedef NevercHandle NevercLTOSummaryHandle;
typedef NevercHandle NevercLTOInputModuleHandle;
typedef NevercHandle NevercLTOResolutionHandle;
typedef NevercHandle NevercLTOProviderHandle;
typedef NevercHandle NevercLTOCacheEntryHandle;
typedef NevercHandle NevercLTOProductHandle;

typedef uint32_t NevercLTOType;
typedef NevercLTOType NevercLTOMode;
#define NEVERC_LTO_NONE UINT32_C(0)
#define NEVERC_LTO_FULL UINT32_C(1)
#define NEVERC_LTO_THIN UINT32_C(2)

typedef uint32_t NevercLTOCacheScope;
#define NEVERC_LTO_CACHE_DISABLED UINT32_C(0)
#define NEVERC_LTO_CACHE_TASK UINT32_C(1)
#define NEVERC_LTO_CACHE_LOCAL_SHARED UINT32_C(2)
#define NEVERC_LTO_CACHE_REMOTE_SHARED UINT32_C(3)

typedef uint64_t NevercLTOOptionFlags;
#define NEVERC_LTO_OPTION_NONE UINT64_C(0)
#define NEVERC_LTO_OPTION_EMIT_OPTIMIZED_BITCODE (UINT64_C(1) << 0)
#define NEVERC_LTO_OPTION_EMIT_INDEX (UINT64_C(1) << 1)
#define NEVERC_LTO_OPTION_SAVE_TEMPS (UINT64_C(1) << 2)
#define NEVERC_LTO_OPTION_WHOLE_PROGRAM_VISIBILITY (UINT64_C(1) << 3)
#define NEVERC_LTO_OPTION_UNIFIED_LTO (UINT64_C(1) << 4)
#define NEVERC_LTO_OPTION_DETERMINISTIC (UINT64_C(1) << 5)

typedef uint64_t NevercLTOSymbolResolutionFlags;
#define NEVERC_LTO_SYMBOL_PREVAILING (UINT64_C(1) << 0)
#define NEVERC_LTO_SYMBOL_VISIBLE_TO_REGULAR_OBJECT (UINT64_C(1) << 1)
#define NEVERC_LTO_SYMBOL_EXPORTED (UINT64_C(1) << 2)
#define NEVERC_LTO_SYMBOL_FINAL_DEFINITION (UINT64_C(1) << 3)
#define NEVERC_LTO_SYMBOL_CAN_INLINE (UINT64_C(1) << 4)
#define NEVERC_LTO_SYMBOL_CAN_INTERNALIZE (UINT64_C(1) << 5)
#define NEVERC_LTO_SYMBOL_LINKER_REDEFINED (UINT64_C(1) << 6)
#define NEVERC_LTO_SYMBOL_REFERENCED_BY_REGULAR_OBJECT (UINT64_C(1) << 7)

typedef uint64_t NevercLTOProviderFlags;
#define NEVERC_LTO_PROVIDER_FULL (UINT64_C(1) << 0)
#define NEVERC_LTO_PROVIDER_THIN (UINT64_C(1) << 1)
#define NEVERC_LTO_PROVIDER_DETERMINISTIC (UINT64_C(1) << 2)
#define NEVERC_LTO_PROVIDER_CACHEABLE (UINT64_C(1) << 3)
#define NEVERC_LTO_PROVIDER_REPLAY_REQUIRED (UINT64_C(1) << 4)

typedef struct NevercLTORequest NevercLTORequest;
typedef struct NevercLTOProductCandidate NevercLTOProductCandidate;

typedef NevercStatus(NEVERC_CALL *NevercLTOBuildCacheKeyFn)(
    void *UserData, NevercTaskHandle Task, const NevercLTORequest *Request,
    NevercMutableByteView OutputKey, uint64_t *OutKeySize);
typedef NevercStatus(NEVERC_CALL *NevercLTOCodegenFn)(
    void *UserData, NevercTaskHandle Task, const NevercLTORequest *Request,
    NevercLTOProductCandidate *OutCandidate);

NEVERC_ABI_PACK_BEGIN

typedef struct NevercLTOInputModuleInfo {
  NevercABITableHeader Header;
  NevercLTOInputModuleHandle Module;
  NevercLinkInputHandle LinkInput;
  NevercArtifactHandle BitcodeArtifact;
  NevercStringView LogicalURI;
  NevercStringView ModuleIdentifier;
  NevercStringView DataLayout;
  uint8_t ContentDigest[32];
  uint64_t SymbolCount;
  NevercStructArrayView Extensions;
} NevercLTOInputModuleInfo;

typedef struct NevercLTOSymbolResolution {
  NevercABITableHeader Header;
  NevercLTOResolutionHandle Resolution;
  NevercLTOInputModuleHandle Module;
  NevercStringView SymbolName;
  NevercLinkSymbolHandle LinkSymbol;
  NevercLTOSymbolResolutionFlags Flags;
  NevercStringView ComdatName;
  NevercStringView Version;
  NevercStructArrayView Extensions;
} NevercLTOSymbolResolution;

typedef struct NevercLTOOptions {
  NevercABITableHeader Header;
  NevercLTOMode Mode;
  NevercLTOCacheScope CacheScope;
  NevercLTOOptionFlags Flags;
  uint32_t OptimizationLevel;
  uint32_t CodeGenOptimizationLevel;
  uint32_t ThreadBudget;
  uint32_t ThinBackendPartitions;
  NevercStringView CPU;
  NevercStringView Features;
  NevercStringView CacheNamespace;
  NevercStructArrayView Extensions;
} NevercLTOOptions;

struct NevercLTORequest {
  NevercABITableHeader Header;
  NevercLTORequestHandle Request;
  NevercTaskHandle Task;
  NevercLinkRequestHandle LinkRequest;
  NevercLinkGraphHandle LinkGraph;
  NevercTargetKey Target;
  NevercObjectFormatID OutputFormat;
  NevercLTOOptions Options;
  NevercStructArrayView Modules;
  NevercStructArrayView Resolutions;
  uint8_t ResolutionDigest[32];
  uint8_t RequestDigest[32];
};

typedef struct NevercLTOObjectProduct {
  NevercABITableHeader Header;
  NevercLTOInputModuleHandle SourceModule;
  NevercObjectGraphHandle ObjectGraph;
  NevercArtifactHandle ObjectArtifact;
  NevercStringView LogicalName;
  uint8_t ContentDigest[32];
  NevercStructArrayView Extensions;
} NevercLTOObjectProduct;

struct NevercLTOProductCandidate {
  NevercABITableHeader Header;
  NevercLTOProductHandle Product;
  NevercStructArrayView Objects;
  NevercArtifactHandle OptimizedBitcode;
  NevercArtifactHandle ThinIndex;
  NevercStringView CacheKey;
  NevercInterfaceID ProductID;
  uint8_t ProducerRouteDigest[32];
};

typedef struct NevercLTOProviderDescriptor {
  NevercABITableHeader Header;
  NevercStringView ProviderID;
  NevercTargetID TargetID;
  NevercLTOProviderFlags Flags;
  NevercStringView CompatibilityKey;
  NevercInterfaceID ProductID;
  NevercLTOBuildCacheKeyFn BuildCacheKey;
  NevercLTOCodegenFn Codegen;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercLTOProviderDescriptor;

typedef struct NevercLTOAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *GetRequest)(
      void *Context, NevercTaskHandle Task, NevercLTORequestHandle Request,
      NevercLTORequest *OutRequest);
  NevercStatus(NEVERC_CALL *GetModulePage)(
      void *Context, NevercTaskHandle Task, NevercLTORequestHandle Request,
      uint64_t Cursor, NevercLinkEntityPage *InOutPage);
  NevercStatus(NEVERC_CALL *GetResolutionPage)(
      void *Context, NevercTaskHandle Task, NevercLTORequestHandle Request,
      uint64_t Cursor, NevercLinkEntityPage *InOutPage);
  NevercStatus(NEVERC_CALL *GetModuleInfo)(
      void *Context, NevercTaskHandle Task,
      NevercLTOInputModuleHandle Module,
      NevercLTOInputModuleInfo *OutInfo);
  NevercStatus(NEVERC_CALL *GetResolutionInfo)(
      void *Context, NevercTaskHandle Task,
      NevercLTOResolutionHandle Resolution,
      NevercLTOSymbolResolution *OutInfo);
} NevercLTOAPI;

typedef struct NevercLTORegistrarAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *RegisterProvider)(
      void *Context, void *RegistrarContext,
      const NevercLTOProviderDescriptor *Descriptor);
} NevercLTORegistrarAPI;

NEVERC_ABI_PACK_END

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* NEVERC_PLUGIN_PLUGINLTO_H */
