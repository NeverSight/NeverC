/* Observe target-independent MC emission without depending on LLVM C++ APIs. */
#include "neverc/Plugin/PluginCore.h"
#include "neverc/Plugin/PluginPhaseSchema.h"
#include <stddef.h>
#include <string.h>

#define SV(Text)                                                               \
  (NevercStringView) { (Text), (uint64_t)(sizeof(Text) - 1) }

static NevercStatus fail(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static NevercStatus NEVERC_CALL
observe_instruction(const NevercPhaseFrame *Frame, NevercObserverPoint Point,
                    void *UserData) {
  const NevercCoreAPI *Core = (const NevercCoreAPI *)UserData;
  NevercDiagnosticDescriptor Diagnostic;
  NevercDiagnosticHandle Handle = {0};
  static const char Message[] =
      "MCObserverPlugin observed an instruction before emission";
  if (!Frame || !Core || Point != NEVERC_OBSERVER_BEFORE)
    return fail(NEVERC_STATUS_INVALID_ARGUMENT);

  memset(&Diagnostic, 0, sizeof(Diagnostic));
  Diagnostic.Header =
      (NevercABITableHeader){sizeof(Diagnostic), NEVERC_CORE_API_MAJOR,
                            NEVERC_CORE_API_MINOR, 0};
  Diagnostic.Severity = NEVERC_DIAGNOSTIC_REMARK;
  Diagnostic.Code = 4401;
  Diagnostic.PluginID = SV("org.neverc.example.mc-observer");
  Diagnostic.PhaseID = SV(NEVERC_PHASE_MC_EMISSION_PRE_INSTRUCTION_NAME);
  Diagnostic.Message =
      (NevercStringView){Message, (uint64_t)(sizeof(Message) - 1)};
  return Core->EmitDiagnostic(Core->Context, &Diagnostic, &Handle);
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *ProcessState) {
  NevercObserverDescriptor Observer;
  (void)ProcessState;
  if (!Core || !Registrar || !Registrar->RegisterObserver)
    return fail(NEVERC_STATUS_INVALID_ARGUMENT);

  memset(&Observer, 0, sizeof(Observer));
  Observer.Header =
      (NevercABITableHeader){sizeof(Observer), NEVERC_PLUGIN_ABI_MAJOR,
                            NEVERC_PLUGIN_ABI_MINOR, 0};
  Observer.Phase =
      (NevercInterfaceID){NEVERC_PHASE_MC_EMISSION_PRE_INSTRUCTION_HIGH,
                          NEVERC_PHASE_MC_EMISSION_PRE_INSTRUCTION_LOW};
  Observer.Points = NEVERC_OBSERVER_BEFORE;
  Observer.Callback = observe_instruction;
  Observer.UserData = (void *)Core;
  return Registrar->RegisterObserver(RegistrarContext, &Observer);
}

NEVERC_EXPORT NevercStatus NEVERC_CALL neverc_plugin_entry(
    const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Plugin;
  uint32_t Capacity;
  size_t Writable;
  if (!Bootstrap || !OutPlugin ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return fail(NEVERC_STATUS_INVALID_ARGUMENT);

  Capacity = OutPlugin->Header.StructSize;
  memset(&Plugin, 0, sizeof(Plugin));
  Plugin.Header =
      (NevercABITableHeader){sizeof(Plugin), NEVERC_PLUGIN_ABI_MAJOR,
                            NEVERC_PLUGIN_ABI_MINOR, 0};
  Plugin.PluginID = SV("org.neverc.example.mc-observer");
  Plugin.DisplayName = SV("NeverC MC emission observer example");
  Plugin.Version.Major = 1;
  Plugin.Concurrency = NEVERC_CONCURRENCY_THREAD_SAFE;
  Plugin.Reentrancy = NEVERC_REENTRANCY_NONE;
  Plugin.Register = register_plugin;
  Writable = Capacity < sizeof(Plugin) ? Capacity : sizeof(Plugin);
  memcpy(OutPlugin, &Plugin, Writable);
  OutPlugin->Header.StructSize = sizeof(Plugin);
  return neverc_status_ok();
}
