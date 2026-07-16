/*===-- PluginSema.h - NeverC semantic analysis plugin C ABI ------- C ---===*/

#ifndef NEVERC_PLUGIN_PLUGINSEMA_H
#define NEVERC_PLUGIN_PLUGINSEMA_H

#include "neverc/Plugin/PluginAST.h"
#include "neverc/Plugin/PluginCore.h"
#include "neverc/Plugin/PluginPhaseSchema.h" /* IWYU pragma: export */

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_SEMA_API_MAJOR UINT16_C(1)
#define NEVERC_SEMA_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_SEMA_HIGH UINT64_C(0x4e435053454d0001)
#define NEVERC_INTERFACE_SEMA_LOW UINT64_C(0x0000000000000001)

NEVERC_ABI_PACK_BEGIN

typedef NevercHandle NevercSemaScopeHandle;
typedef NevercHandle NevercLookupResultHandle;
typedef NevercHandle NevercConversionSequenceHandle;
typedef NevercHandle NevercConstantValueHandle;
typedef NevercHandle NevercSemanticUnitHandle;

typedef struct NevercSemaAPI {
  NevercABITableHeader Header;
  void *Context;
} NevercSemaAPI;

NEVERC_ABI_PACK_END

#ifdef __cplusplus
}
#endif

#endif
