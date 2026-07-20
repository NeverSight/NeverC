#pragma pack(push, 1)
#include "neverc/Plugin/PluginCore.h"
#include "neverc/Plugin/PluginDriver.h"
#include "neverc/Plugin/PluginPhaseSchema.h"

typedef struct CallerPackOneProbe {
  uint8_t Prefix;
  uint64_t Value;
} CallerPackOneProbe;
#pragma pack(pop)

_Static_assert(NEVERC_PLUGIN_ABI_MAJOR == 1, "initial ABI major must be one");
_Static_assert(NEVERC_BUILTIN_DRIVER_PHASE_COUNT == 6,
               "bad builtin driver phase schema");
_Static_assert(NEVERC_BUILTIN_PHASE_COUNT == 96,
               "bad complete builtin phase schema");
_Static_assert(sizeof(NevercABITableHeader) == 16, "bad ABI header size");
_Static_assert(_Alignof(NevercABITableHeader) == 8, "bad ABI header alignment");
_Static_assert(offsetof(NevercABITableHeader, Flags) == 8,
               "bad ABI header layout");
_Static_assert(sizeof(NevercHandle) == 16, "bad handle size");
_Static_assert(sizeof(NevercStatus) == 16, "bad status size");
_Static_assert(offsetof(CallerPackOneProbe, Value) == 1,
               "PluginCore.h did not restore caller packing");

static NevercStatus NEVERC_CALL compile_only_observer(
    const NevercPhaseFrame *Frame, NevercObserverPoint Point, void *UserData) {
  (void)Frame;
  (void)Point;
  (void)UserData;
  return neverc_status_ok();
}

static NevercPhaseObserverFn CompileOnlyObserver = compile_only_observer;

int neverc_plugin_core_c_compile_fixture(void) {
  NevercHandle NullHandle = {0, 0};
  return CompileOnlyObserver != 0 &&
         neverc_handle_is_null(NullHandle) == NEVERC_TRUE;
}
