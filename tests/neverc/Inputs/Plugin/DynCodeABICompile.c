#pragma pack(push, 1)
#include "neverc/Plugin/PluginDynCode.h"

typedef struct NevercDynCodeCallerPackProbe {
  uint8_t Prefix;
  uint64_t Value;
} NevercDynCodeCallerPackProbe;
#pragma pack(pop)

_Static_assert(NEVERC_DYNCODE_API_MAJOR == 1,
               "DynCode ABI must start at major one");
_Static_assert(NEVERC_DYNCODE_REGISTRAR_API_MAJOR == 1,
               "DynCode registrar ABI must start at major one");
_Static_assert(NEVERC_DYNCODE_PHASE_API_MAJOR == 1,
               "DynCode phase ABI must start at major one");
_Static_assert(sizeof(NevercDynCodeRequestHandle) == sizeof(NevercHandle),
               "dyncode request handle must remain opaque");
_Static_assert(sizeof(NevercDynCodeImageHandle) == sizeof(NevercHandle),
               "dyncode image handle must remain opaque");
_Static_assert(sizeof(NevercDynCodeReportHandle) == sizeof(NevercHandle),
               "dyncode report handle must remain opaque");
_Static_assert(offsetof(NevercDynCodeAPI, Header) == 0,
               "DynCode table must begin with ABI header");
_Static_assert(offsetof(NevercDynCodeRegistrarAPI, Header) == 0,
               "DynCode registrar table must begin with ABI header");
_Static_assert(offsetof(NevercDynCodePhaseAPI, Header) == 0,
               "DynCode phase table must begin with ABI header");
_Static_assert(offsetof(NevercDynCodeRequestInfo, Header) == 0,
               "dyncode request info must begin with ABI header");
_Static_assert(offsetof(NevercDynCodeCallerPackProbe, Value) == 1,
               "DynCode header did not restore caller packing");

int neverc_plugin_dyncode_c_compile_fixture(void) {
  NevercDynCodeRequestHandle Request = {0, 0};
  NevercDynCodeImageHandle Image = {0, 0};
  NevercDynCodeReportHandle Report = {0, 0};
  return neverc_handle_is_null(Request) == NEVERC_TRUE &&
         neverc_handle_is_null(Image) == NEVERC_TRUE &&
         neverc_handle_is_null(Report) == NEVERC_TRUE;
}
