#pragma pack(push, 1)
#include "neverc/Plugin/PluginLTO.h"

typedef struct NevercLTOCallerPackProbe {
  uint8_t Prefix;
  uint64_t Value;
} NevercLTOCallerPackProbe;
#pragma pack(pop)

_Static_assert(NEVERC_LTO_API_MAJOR == 1,
               "LTO ABI must start at major one");
_Static_assert(sizeof(NevercLTOModuleHandle) == sizeof(NevercHandle),
               "LTO module handle must remain opaque");
_Static_assert(sizeof(NevercLTOSummaryHandle) == sizeof(NevercHandle),
               "LTO summary handle must remain opaque");
_Static_assert(sizeof(NevercLTOResolutionHandle) == sizeof(NevercHandle),
               "LTO resolution handle must remain opaque");
_Static_assert(offsetof(NevercLTOAPI, Header) == 0,
               "LTO table must begin with ABI header");
_Static_assert(offsetof(NevercLTOProviderDescriptor, Header) == 0,
               "LTO provider descriptor must begin with ABI header");
_Static_assert(offsetof(NevercLTOCallerPackProbe, Value) == 1,
               "LTO header did not restore caller packing");

int neverc_plugin_lto_c_compile_fixture(void) {
  NevercLTOModuleHandle Module = {0, 0};
  NevercLTOSummaryHandle Summary = {0, 0};
  NevercLTOResolutionHandle Resolution = {0, 0};
  return neverc_handle_is_null(Module) == NEVERC_TRUE &&
         neverc_handle_is_null(Summary) == NEVERC_TRUE &&
         neverc_handle_is_null(Resolution) == NEVERC_TRUE;
}
