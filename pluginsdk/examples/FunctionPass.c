/* Minimal first-release function-pass plugin. */
#include "neverc/Plugin/NevercPluginAPI.h"
#include <stddef.h>
#include <string.h>

#define SV(Text)                                                               \
  (NevercStringView) { (Text), (uint64_t)(sizeof(Text) - 1) }

static const NevercIRPassAPI *PassAPI;

static NevercStatus fail(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static NevercStatus NEVERC_CALL
run_function(const NevercIRPassInvocation *Invocation,
             NevercIRPreservedAnalyses *OutPreserved, void *UserData) {
  NevercStringView Name = {0};
  NevercStatus Status;
  (void)UserData;
  if (!Invocation || !Invocation->Core || !OutPreserved ||
      Invocation->Level != NEVERC_IR_PASS_LEVEL_FUNCTION ||
      neverc_handle_is_null(Invocation->Function))
    return fail(NEVERC_STATUS_INVALID_ARGUMENT);
  Status = Invocation->Core->GetValueName(
      Invocation->Core->Context, Invocation->Task, Invocation->Function,
      &Name);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  (void)Name;
  memset(OutPreserved, 0, sizeof(*OutPreserved));
  OutPreserved->Header =
      (NevercABITableHeader){sizeof(*OutPreserved), NEVERC_IR_PASS_API_MAJOR,
                            NEVERC_IR_PASS_API_MINOR, 0};
  OutPreserved->Flags = NEVERC_IR_PRESERVE_ALL;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *ProcessState) {
  NevercIRPassDescriptor Pass;
  (void)Core;
  (void)Registrar;
  (void)ProcessState;
  if (!PassAPI)
    return fail(NEVERC_STATUS_INVALID_STATE);
  memset(&Pass, 0, sizeof(Pass));
  Pass.Header =
      (NevercABITableHeader){sizeof(Pass), NEVERC_IR_PASS_API_MAJOR,
                            NEVERC_IR_PASS_API_MINOR, 0};
  Pass.PassID = SV("example.function-pass");
  Pass.Phase =
      (NevercInterfaceID){NEVERC_PHASE_IR_PASS_PIPELINE_START_HIGH,
                          NEVERC_PHASE_IR_PASS_PIPELINE_START_LOW};
  Pass.Level = NEVERC_IR_PASS_LEVEL_FUNCTION;
  Pass.Deterministic = NEVERC_TRUE;
  Pass.Cacheable = NEVERC_TRUE;
  Pass.Run = run_function;
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
      (NevercInterfaceID){NEVERC_INTERFACE_IR_PASS_HIGH,
                          NEVERC_INTERFACE_IR_PASS_LOW},
      NEVERC_IR_PASS_API_MAJOR, NEVERC_IR_PASS_API_MINOR, &Table, &Minor,
      &StructSize);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Table ||
      StructSize < offsetof(NevercIRPassAPI, RegisterPass) +
                       sizeof(((NevercIRPassAPI *)0)->RegisterPass))
    return fail(NEVERC_STATUS_ABI_MISMATCH);
  PassAPI = (const NevercIRPassAPI *)Table;

  Capacity = OutPlugin->Header.StructSize;
  memset(&Plugin, 0, sizeof(Plugin));
  Plugin.Header =
      (NevercABITableHeader){sizeof(Plugin), NEVERC_PLUGIN_ABI_MAJOR,
                            NEVERC_PLUGIN_ABI_MINOR, 0};
  Plugin.PluginID = SV("org.neverc.example.function-pass");
  Plugin.DisplayName = SV("NeverC function pass example");
  Plugin.Version.Major = 1;
  Plugin.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Plugin.Reentrancy = NEVERC_REENTRANCY_NONE;
  Plugin.Register = register_plugin;
  Writable = Capacity < sizeof(Plugin) ? Capacity : sizeof(Plugin);
  memcpy(OutPlugin, &Plugin, Writable);
  OutPlugin->Header.StructSize = sizeof(Plugin);
  return neverc_status_ok();
}
