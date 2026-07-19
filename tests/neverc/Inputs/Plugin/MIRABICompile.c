#pragma pack(push, 1)
#include "neverc/Plugin/PluginMIR.h"

typedef struct NevercMIRCallerPackProbe {
  uint8_t Prefix;
  uint64_t Value;
} NevercMIRCallerPackProbe;
#pragma pack(pop)

_Static_assert(NEVERC_MIR_API_MAJOR == 1,
               "MIR ABI must start at major one");
_Static_assert(sizeof(NevercMIRModuleHandle) == sizeof(NevercHandle),
               "MIR module must remain opaque");
_Static_assert(sizeof(NevercMachineFunctionHandle) == sizeof(NevercHandle),
               "machine function must remain opaque");
_Static_assert(sizeof(NevercMachineBasicBlockHandle) == sizeof(NevercHandle),
               "machine basic block must remain opaque");
_Static_assert(sizeof(NevercMachineInstrHandle) == sizeof(NevercHandle),
               "machine instruction must remain opaque");
_Static_assert(offsetof(NevercMIRAPI, Header) == 0,
               "MIR table must begin with ABI header");
_Static_assert(offsetof(NevercMIRCallerPackProbe, Value) == 1,
               "MIR header did not restore caller packing");

int neverc_plugin_mir_c_compile_fixture(void) {
  NevercMIRModuleHandle Module = {0, 0};
  NevercMachineFunctionHandle Function = {0, 0};
  NevercMachineBasicBlockHandle Block = {0, 0};
  NevercMachineInstrHandle Instruction = {0, 0};
  return neverc_handle_is_null(Module) == NEVERC_TRUE &&
         neverc_handle_is_null(Function) == NEVERC_TRUE &&
         neverc_handle_is_null(Block) == NEVERC_TRUE &&
         neverc_handle_is_null(Instruction) == NEVERC_TRUE;
}
