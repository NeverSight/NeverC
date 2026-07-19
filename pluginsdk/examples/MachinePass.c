/* Minimal first-release machine-function pass plugin. */
#include "neverc/Plugin/NevercPluginAPI.h"
#include <stddef.h>
#include <string.h>

#define SV(Text)                                                               \
  (NevercStringView) { (Text), (uint64_t)(sizeof(Text) - 1) }

static const NevercMIRPassAPI *PassAPI;

static NevercStatus fail(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static NevercStatus NEVERC_CALL
run_machine_function(const NevercMIRPassInvocation *Invocation,
                     NevercMIRPreservedAnalyses *OutPreserved,
                     void *UserData) {
  uint64_t BlockCount = 0;
  NevercStatus Status;
  (void)UserData;
  if (!Invocation || !Invocation->Core || !OutPreserved ||
      Invocation->Level != NEVERC_MIR_PASS_LEVEL_FUNCTION ||
      neverc_handle_is_null(Invocation->Function))
    return fail(NEVERC_STATUS_INVALID_ARGUMENT);
  Status = Invocation->Core->GetBasicBlockCount(
      Invocation->Core->Context, Invocation->Task, Invocation->Function,
      &BlockCount);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  (void)BlockCount;
  memset(OutPreserved, 0, sizeof(*OutPreserved));
  OutPreserved->Header =
      (NevercABITableHeader){sizeof(*OutPreserved), NEVERC_MIR_PASS_API_MAJOR,
                            NEVERC_MIR_PASS_API_MINOR, 0};
  OutPreserved->Flags = NEVERC_MIR_PRESERVE_ALL;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *ProcessState) {
  NevercMIRPassDescriptor Pass;
  (void)Core;
  (void)Registrar;
  (void)ProcessState;
  if (!PassAPI)
    return fail(NEVERC_STATUS_INVALID_STATE);
  memset(&Pass, 0, sizeof(Pass));
  Pass.Header =
      (NevercABITableHeader){sizeof(Pass), NEVERC_MIR_PASS_API_MAJOR,
                            NEVERC_MIR_PASS_API_MINOR, 0};
  Pass.PassID = SV("example.machine-pass");
  Pass.Phase =
      (NevercInterfaceID){NEVERC_PHASE_MIR_PASS_PREEMIT_HIGH,
                          NEVERC_PHASE_MIR_PASS_PREEMIT_LOW};
  Pass.Level = NEVERC_MIR_PASS_LEVEL_FUNCTION;
  Pass.Deterministic = NEVERC_TRUE;
  Pass.Run = run_machine_function;
  return PassAPI->RegisterPass(PassAPI->Context, RegistrarContext, &Pass);
}

NEVERC_EXPORT NevercStatus NEVERC_CALL neverc_plugin_entry(
    const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Plugin;
  const void *Table = NULL;
  uint16_t Minor = 0;
  uint64_t StructSize = 0;
  uint32_t Capacity;
  size_t Writable;
  NevercStatus Status;
  if (!Bootstrap || !Bootstrap->QueryInterface || !OutPlugin ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return fail(NEVERC_STATUS_INVALID_ARGUMENT);
  Status = Bootstrap->QueryInterface(
      Bootstrap->Context,
      (NevercInterfaceID){NEVERC_INTERFACE_MIR_PASS_HIGH,
                          NEVERC_INTERFACE_MIR_PASS_LOW},
      NEVERC_MIR_PASS_API_MAJOR, NEVERC_MIR_PASS_API_MINOR, &Table, &Minor,
      &StructSize);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Table ||
      StructSize < offsetof(NevercMIRPassAPI, RegisterPass) +
                       sizeof(((NevercMIRPassAPI *)0)->RegisterPass))
    return fail(NEVERC_STATUS_ABI_MISMATCH);
  PassAPI = (const NevercMIRPassAPI *)Table;

  Capacity = OutPlugin->Header.StructSize;
  memset(&Plugin, 0, sizeof(Plugin));
  Plugin.Header =
      (NevercABITableHeader){sizeof(Plugin), NEVERC_PLUGIN_ABI_MAJOR,
                            NEVERC_PLUGIN_ABI_MINOR, 0};
  Plugin.PluginID = SV("org.neverc.example.machine-pass");
  Plugin.DisplayName = SV("NeverC machine pass example");
  Plugin.Version.Major = 1;
  Plugin.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Plugin.Reentrancy = NEVERC_REENTRANCY_NONE;
  Plugin.Register = register_plugin;
  Writable = Capacity < sizeof(Plugin) ? Capacity : sizeof(Plugin);
  memcpy(OutPlugin, &Plugin, Writable);
  OutPlugin->Header.StructSize = sizeof(Plugin);
  return neverc_status_ok();
}
