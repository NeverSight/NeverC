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
_Static_assert(NEVERC_OBJECT_FORMAT_API_MINOR == 1,
               "object-format ABI minor must advertise writer policies");
_Static_assert(NEVERC_OBJECT_WRITE_REQUEST_FLAGS_API_MINOR == 1,
               "object writer flags require object-format ABI 1.1");
_Static_assert(NEVERC_OBJECT_PHASE_API_MAJOR == 1,
               "object-phase ABI must start at major one");
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
_Static_assert(offsetof(NevercMutableBinaryAPI, Header) == 0,
               "mutable-binary table must begin with ABI header");
_Static_assert(offsetof(NevercObjectPhaseAPI, Header) == 0,
               "object-phase table must begin with ABI header");
_Static_assert(offsetof(NevercObjectWriteRequest, Header) == 0,
               "object writer request must begin with ABI header");
_Static_assert(NEVERC_OBJECT_WRITE_REQUEST_KNOWN_FLAGS ==
                   (NEVERC_OBJECT_WRITE_CANONICAL_ELF_TABLES |
                    NEVERC_OBJECT_WRITE_ANDROID_KERNEL_RELEASE |
                    NEVERC_OBJECT_WRITE_DROP_DEBUG_INFO),
               "object writer request flag mask must be exhaustive");
_Static_assert(NEVERC_OBJECT_WRITE_CANONICAL_ELF_TABLES == UINT64_C(1),
               "canonical ELF table request flag ABI changed");
_Static_assert(NEVERC_OBJECT_WRITE_ANDROID_KERNEL_RELEASE == UINT64_C(2),
               "Android release writer request flag ABI changed");
_Static_assert(NEVERC_OBJECT_WRITE_DROP_DEBUG_INFO == UINT64_C(4),
               "debug-strip writer request flag ABI changed");
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
