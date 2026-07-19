/*===-- PluginObject.h - NeverC object plugin C ABI --------------- C ---===*/

#ifndef NEVERC_PLUGIN_PLUGINOBJECT_H
#define NEVERC_PLUGIN_PLUGINOBJECT_H

#include "neverc/Plugin/PluginMC.h"     /* IWYU pragma: export */
#include "neverc/Plugin/PluginSource.h" /* IWYU pragma: export */
#include "neverc/Plugin/PluginTarget.h" /* IWYU pragma: export */

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_OBJECT_API_MAJOR UINT16_C(1)
#define NEVERC_OBJECT_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_OBJECT_HIGH UINT64_C(0x4e43504f424a0001)
#define NEVERC_INTERFACE_OBJECT_LOW UINT64_C(0x0000000000000001)
#define NEVERC_OBJECT_INTERFACE_STABILITY NEVERC_INTERFACE_STABLE

#define NEVERC_OBJECT_FORMAT_API_MAJOR UINT16_C(1)
#define NEVERC_OBJECT_FORMAT_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_OBJECT_FORMAT_HIGH UINT64_C(0x4e4350464d540001)
#define NEVERC_INTERFACE_OBJECT_FORMAT_LOW UINT64_C(0x0000000000000001)
#define NEVERC_OBJECT_FORMAT_INTERFACE_STABILITY NEVERC_INTERFACE_STABLE

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

NEVERC_ABI_PACK_BEGIN

typedef struct NevercObjectFormatDescriptor {
  NevercABITableHeader Header;
  NevercObjectFormatID FormatID;
  NevercStringView CanonicalName;
  NevercStringArrayView Aliases;
  NevercInterfaceIDArrayView SupportedTargets;
  NevercStringView DefaultExtension;
  uint64_t Flags;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercObjectFormatDescriptor;

typedef struct NevercObjectAPI {
  NevercABITableHeader Header;
  void *Context;
} NevercObjectAPI;

typedef struct NevercObjectFormatAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercStatus(NEVERC_CALL *RegisterFormat)(
      void *Context, void *RegistrarContext,
      const NevercObjectFormatDescriptor *Descriptor);
} NevercObjectFormatAPI;

NEVERC_ABI_PACK_END

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_PLUGIN_PLUGINOBJECT_H */
