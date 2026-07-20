#pragma pack(push, 1)
#include "neverc/Plugin/PluginLink.h"

typedef struct NevercLinkCallerPackProbe {
  uint8_t Prefix;
  uint64_t Value;
} NevercLinkCallerPackProbe;
#pragma pack(pop)

_Static_assert(NEVERC_LINK_API_MAJOR == 1,
               "Link ABI must start at major one");
_Static_assert(NEVERC_LINK_REGISTRAR_API_MAJOR == 1,
               "Link registrar ABI must start at major one");
_Static_assert(sizeof(NevercLinkRequestHandle) == sizeof(NevercHandle),
               "link request handle must remain opaque");
_Static_assert(sizeof(NevercLinkGraphHandle) == sizeof(NevercHandle),
               "link graph handle must remain opaque");
_Static_assert(sizeof(NevercBinaryImageHandle) == sizeof(NevercHandle),
               "binary image handle must remain opaque");
_Static_assert(offsetof(NevercLinkAPI, Header) == 0,
               "Link table must begin with ABI header");
_Static_assert(offsetof(NevercLinkRegistrarAPI, Header) == 0,
               "Link registrar table must begin with ABI header");
_Static_assert(offsetof(NevercLinkRequest, Header) == 0,
               "link request must begin with ABI header");
_Static_assert(offsetof(NevercLinkCallerPackProbe, Value) == 1,
               "Link header did not restore caller packing");

int neverc_plugin_link_c_compile_fixture(void) {
  NevercLinkRequestHandle Request = {0, 0};
  NevercLinkGraphHandle Graph = {0, 0};
  NevercBinaryImageHandle Image = {0, 0};
  return neverc_handle_is_null(Request) == NEVERC_TRUE &&
         neverc_handle_is_null(Graph) == NEVERC_TRUE &&
         neverc_handle_is_null(Image) == NEVERC_TRUE;
}
