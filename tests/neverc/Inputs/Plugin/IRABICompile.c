#pragma pack(push, 1)
#include "neverc/Plugin/PluginIR.h"

typedef struct NevercIRCallerPackProbe {
  uint8_t Prefix;
  uint64_t Value;
} NevercIRCallerPackProbe;
#pragma pack(pop)

_Static_assert(NEVERC_IR_CORE_API_MAJOR == 1,
               "IR Core ABI must start at major one");
_Static_assert(NEVERC_IR_BUILDER_API_MAJOR == 1,
               "IR Builder ABI must start at major one");
_Static_assert(NEVERC_IR_ANALYSIS_API_MAJOR == 1,
               "IR Analysis ABI must start at major one");
_Static_assert(NEVERC_IR_PASS_API_MAJOR == 1,
               "IR Pass ABI must start at major one");
_Static_assert(sizeof(NevercIRContextHandle) == sizeof(NevercHandle),
               "IR context must remain opaque");
_Static_assert(sizeof(NevercIRModuleHandle) == sizeof(NevercHandle),
               "IR module must remain opaque");
_Static_assert(sizeof(NevercIRValueHandle) == sizeof(NevercHandle),
               "IR value must remain opaque");
_Static_assert(sizeof(NevercIRTypeHandle) == sizeof(NevercHandle),
               "IR type must remain opaque");
_Static_assert(sizeof(NevercIRMetadataHandle) == sizeof(NevercHandle),
               "IR metadata must remain opaque");
_Static_assert(sizeof(NevercIRNamedMetadataHandle) == sizeof(NevercHandle),
               "IR named metadata must remain opaque");
_Static_assert(sizeof(NevercIRAttributeHandle) == sizeof(NevercHandle),
               "IR attribute must remain opaque");
_Static_assert(NEVERC_IR_TYPE_KIND_COUNT == 22,
               "IR type schema must cover every LLVM type");
_Static_assert(NEVERC_IR_VALUE_KIND_COUNT == 29,
               "IR value schema must cover every LLVM value kind");
_Static_assert(NEVERC_IR_OPCODE_COUNT == 67,
               "IR opcode schema must cover every LLVM opcode");
_Static_assert(sizeof(NEVERC_IR_SCHEMA_DIGEST) == 65,
               "IR schema digest must be SHA-256 text");
_Static_assert(offsetof(NevercIRCoreAPI, Header) == 0,
               "IR Core table must begin with ABI header");
_Static_assert(offsetof(NevercIRCoreAPI, GetContext) >
                   offsetof(NevercIRCoreAPI, Context),
               "IR Core slots must follow the table context");
_Static_assert(offsetof(NevercIRCoreAPI, AddFunctionAttribute) >
                   offsetof(NevercIRCoreAPI, GetContext),
               "IR Core function table must preserve append-only order");
_Static_assert(sizeof(NevercIRDebugLocationInfo) == 56,
               "debug location ABI layout changed");
_Static_assert(offsetof(NevercIRBuilderAPI, Header) == 0,
               "IR Builder table must begin with ABI header");
_Static_assert(offsetof(NevercIRAnalysisAPI, Header) == 0,
               "IR Analysis table must begin with ABI header");
_Static_assert(offsetof(NevercIRPassAPI, Header) == 0,
               "IR Pass table must begin with ABI header");
_Static_assert(offsetof(NevercIRCallerPackProbe, Value) == 1,
               "IR header did not restore caller packing");

int neverc_plugin_ir_c_compile_fixture(void) {
  NevercIRContextHandle Context = {0, 0};
  NevercIRModuleHandle Module = {0, 0};
  NevercIRValueHandle Value = {0, 0};
  NevercIRTypeHandle Type = {0, 0};
  return neverc_handle_is_null(Context) == NEVERC_TRUE &&
         neverc_handle_is_null(Module) == NEVERC_TRUE &&
         neverc_handle_is_null(Value) == NEVERC_TRUE &&
         neverc_handle_is_null(Type) == NEVERC_TRUE;
}
