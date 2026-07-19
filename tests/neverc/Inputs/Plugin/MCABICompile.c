#pragma pack(push, 1)
#include "neverc/Plugin/PluginMC.h"

typedef struct NevercMCCallerPackProbe {
  uint8_t Prefix;
  uint64_t Value;
} NevercMCCallerPackProbe;
#pragma pack(pop)

_Static_assert(NEVERC_MC_API_MAJOR == 1,
               "MC ABI must start at major one");
_Static_assert(sizeof(NevercMCUnitHandle) == sizeof(NevercHandle),
               "MC unit handle must remain opaque");
_Static_assert(sizeof(NevercMCInstHandle) == sizeof(NevercHandle),
               "MC instruction handle must remain opaque");
_Static_assert(sizeof(NevercMCFixupHandle) == sizeof(NevercHandle),
               "MC fixup handle must remain opaque");
_Static_assert(offsetof(NevercMCAPI, Header) == 0,
               "MC table must begin with ABI header");
_Static_assert(offsetof(NevercMCCallerPackProbe, Value) == 1,
               "MC header did not restore caller packing");

int neverc_plugin_mc_c_compile_fixture(void) {
  NevercMCUnitHandle Unit = {0, 0};
  NevercMCInstHandle Instruction = {0, 0};
  NevercMCFixupHandle Fixup = {0, 0};
  return neverc_handle_is_null(Unit) == NEVERC_TRUE &&
         neverc_handle_is_null(Instruction) == NEVERC_TRUE &&
         neverc_handle_is_null(Fixup) == NEVERC_TRUE;
}
