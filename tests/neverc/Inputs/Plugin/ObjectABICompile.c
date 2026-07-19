#pragma pack(push, 1)
#include "neverc/Plugin/PluginObject.h"

typedef struct NevercObjectCallerPackProbe {
  uint8_t Prefix;
  uint64_t Value;
} NevercObjectCallerPackProbe;
#pragma pack(pop)

_Static_assert(NEVERC_OBJECT_API_MAJOR == 1,
               "Object ABI must start at major one");
_Static_assert(NEVERC_OBJECT_FORMAT_API_MAJOR == 1,
               "object-format ABI must start at major one");
_Static_assert(sizeof(NevercObjectGraphHandle) == sizeof(NevercHandle),
               "object graph handle must remain opaque");
_Static_assert(sizeof(NevercObjectImageHandle) == sizeof(NevercHandle),
               "object image handle must remain opaque");
_Static_assert(sizeof(NevercObjectBuilderHandle) == sizeof(NevercHandle),
               "object builder handle must remain opaque");
_Static_assert(offsetof(NevercObjectAPI, Header) == 0,
               "Object table must begin with ABI header");
_Static_assert(offsetof(NevercObjectFormatAPI, Header) == 0,
               "object-format table must begin with ABI header");
_Static_assert(offsetof(NevercObjectCallerPackProbe, Value) == 1,
               "Object header did not restore caller packing");

int neverc_plugin_object_c_compile_fixture(void) {
  NevercObjectGraphHandle Graph = {0, 0};
  NevercObjectImageHandle Image = {0, 0};
  NevercObjectBuilderHandle Builder = {0, 0};
  return neverc_handle_is_null(Graph) == NEVERC_TRUE &&
         neverc_handle_is_null(Image) == NEVERC_TRUE &&
         neverc_handle_is_null(Builder) == NEVERC_TRUE;
}
