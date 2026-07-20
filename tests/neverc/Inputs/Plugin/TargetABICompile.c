#pragma pack(push, 1)
#include "neverc/Plugin/PluginTarget.h"

typedef struct NevercTargetCallerPackProbe {
  uint8_t Prefix;
  uint64_t Value;
} NevercTargetCallerPackProbe;
#pragma pack(pop)

_Static_assert(NEVERC_TARGET_API_MAJOR == 1,
               "Target ABI must start at major one");
_Static_assert(NEVERC_TARGET_ABI_API_MAJOR == 1,
               "target ABI description must start at major one");
_Static_assert(NEVERC_CALLING_CONVENTION_API_MAJOR == 1,
               "calling-convention ABI must start at major one");
_Static_assert(NEVERC_CALLING_CONVENTION_API_MINOR == 1,
               "calling-convention plan requires API minor one");
_Static_assert(sizeof(NevercTargetHandle) == sizeof(NevercHandle),
               "target handle must remain opaque");
_Static_assert(sizeof(NevercTargetSnapshotHandle) == sizeof(NevercHandle),
               "target snapshot handle must remain opaque");
_Static_assert(sizeof(NevercTargetSchemaHandle) == sizeof(NevercHandle),
               "target schema handle must remain opaque");
_Static_assert(offsetof(NevercTargetAPI, Header) == 0,
               "Target table must begin with ABI header");
_Static_assert(offsetof(NevercTargetABIAPI, Header) == 0,
               "target ABI table must begin with ABI header");
_Static_assert(offsetof(NevercCallingConventionAPI, Header) == 0,
               "calling-convention table must begin with ABI header");
_Static_assert(offsetof(NevercTargetMacroDescriptor, Header) == 0,
               "Target macro descriptor must begin with ABI header");
_Static_assert(offsetof(NevercTargetBuiltinDescriptor, Header) == 0,
               "Target builtin descriptor must begin with ABI header");
_Static_assert(offsetof(NevercTargetRegisterDescriptor, Header) == 0,
               "Target register descriptor must begin with ABI header");
_Static_assert(offsetof(NevercTargetConstraintDescriptor, Header) == 0,
               "Target constraint descriptor must begin with ABI header");
_Static_assert(offsetof(NevercABITypeDescriptor, Header) == 0,
               "ABI type descriptor must begin with ABI header");
_Static_assert(offsetof(NevercABIArgumentClassification, Header) == 0,
               "ABI classification must begin with ABI header");
_Static_assert(offsetof(NevercABICoercionElement, Header) == 0,
               "ABI coercion element must begin with ABI header");
_Static_assert(offsetof(NevercABIFunctionQuery, Header) == 0,
               "ABI function query must begin with ABI header");
_Static_assert(offsetof(NevercCallingConventionLocation, Header) == 0,
               "CC location must begin with ABI header");
_Static_assert(offsetof(NevercCallingConventionQuery, Header) == 0,
               "CC query must begin with ABI header");
_Static_assert(offsetof(NevercCallingConventionPlan, Header) == 0,
               "CC plan must begin with ABI header");
_Static_assert(offsetof(NevercTargetVAArgDescriptor, Header) == 0,
               "ABI va_arg descriptor must begin with ABI header");
_Static_assert(NEVERC_TARGET_VA_LIST_VOID_POINTER == 2,
               "Target va_list kinds must remain stable");
_Static_assert(NEVERC_TARGET_CONSTRAINT_ALLOWS_REGISTER == 2,
               "Target constraint flags must remain stable");
_Static_assert(NEVERC_ABI_ARGUMENT_INDIRECT == 3,
               "ABI argument kinds must remain stable");
_Static_assert(NEVERC_ABI_ARGUMENT_INDIRECT_ALIASED == 6,
               "ABI indirect-aliased kind must remain stable");
_Static_assert(NEVERC_ABI_ARGUMENT_COERCE_AND_EXPAND == 7,
               "ABI coerce-and-expand kind must remain stable");
_Static_assert(NEVERC_ABI_ARGUMENT_BYVAL == 1,
               "ABI argument flags must remain stable");
_Static_assert(NEVERC_ABI_COERCE_POINTER == 3,
               "ABI coercion kinds must remain stable");
_Static_assert(NEVERC_CC_LOCATION_REGISTER == 1,
               "CC location kinds must remain stable");
_Static_assert(NEVERC_CC_LOCATION_BYVAL == 2,
               "CC location flags must remain stable");
_Static_assert(offsetof(NevercTargetCallerPackProbe, Value) == 1,
               "Target header did not restore caller packing");

int neverc_plugin_target_c_compile_fixture(void) {
  NevercTargetHandle Target = {0, 0};
  NevercTargetSnapshotHandle Snapshot = {0, 0};
  NevercTargetSchemaHandle Schema = {0, 0};
  return neverc_handle_is_null(Target) == NEVERC_TRUE &&
         neverc_handle_is_null(Snapshot) == NEVERC_TRUE &&
         neverc_handle_is_null(Schema) == NEVERC_TRUE;
}
