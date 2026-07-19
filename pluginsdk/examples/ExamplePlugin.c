/*
 * Minimal first-release IR pass example.
 *
 * Build:
 *   neverc --no-default-config -shared -I../include \
 *     ExamplePlugin.c -o ExamplePlugin.so
 *
 * Use:
 *   neverc -fplugin=./ExamplePlugin.so -c input.c
 */
#include "neverc/Plugin/NevercPluginAPI.h"
#include <stddef.h>
#include <string.h>

#define PLUGIN_ID "org.neverc.example.ir-overview"
#define SV(Text)                                                               \
  (NevercStringView) { (Text), (uint64_t)(sizeof(Text) - 1) }

static const NevercIRPassAPI *PassAPI;
static const NevercCoreAPI *CoreAPI;

static NevercStatus failure(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static NevercStatus emit_note(const char *Message) {
  NevercDiagnosticDescriptor Diagnostic;
  NevercDiagnosticHandle Handle = {0};
  memset(&Diagnostic, 0, sizeof(Diagnostic));
  Diagnostic.Header =
      (NevercABITableHeader){sizeof(Diagnostic), NEVERC_CORE_API_MAJOR,
                            NEVERC_CORE_API_MINOR, 0};
  Diagnostic.Severity = NEVERC_DIAGNOSTIC_REMARK;
  Diagnostic.Code = 5101;
  Diagnostic.PluginID = SV(PLUGIN_ID);
  Diagnostic.Message =
      (NevercStringView){Message, (uint64_t)strlen(Message)};
  return CoreAPI->EmitDiagnostic(CoreAPI->Context, &Diagnostic, &Handle);
}

static NevercStatus NEVERC_CALL
run_pass(const NevercIRPassInvocation *Invocation,
         NevercIRPreservedAnalyses *OutPreserved, void *UserData) {
  NevercIRValueCursor Cursor;
  NevercIRValueHandle Values[16];
  uint64_t TotalFunctions = 0;
  NevercStatus Status;
  (void)UserData;
  if (!Invocation || !Invocation->Core || !OutPreserved || !CoreAPI)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  memset(&Cursor, 0, sizeof(Cursor));
  Status = Invocation->Core->BeginValueCursor(
      Invocation->Core->Context, Invocation->Task, Invocation->Module,
      NEVERC_IR_COLLECTION_MODULE_FUNCTIONS, &Cursor);
  while (Status.Code == NEVERC_STATUS_OK) {
    uint64_t Count = 0;
    Status = Invocation->Core->CollectValueCursor(
        Invocation->Core->Context, Invocation->Task, &Cursor, Values,
        sizeof(Values) / sizeof(Values[0]), &Count);
    if (Status.Code != NEVERC_STATUS_OK)
      break;
    TotalFunctions += Count;
    if (Count != sizeof(Values) / sizeof(Values[0]))
      break;
  }
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = emit_note(TotalFunctions == 0
                         ? "ExamplePlugin observed an empty IR module"
                         : "ExamplePlugin observed the module function list");
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(OutPreserved, 0, sizeof(*OutPreserved));
  OutPreserved->Header =
      (NevercABITableHeader){sizeof(*OutPreserved), NEVERC_IR_PASS_API_MAJOR,
                            NEVERC_IR_PASS_API_MINOR, 0};
  OutPreserved->Flags = NEVERC_IR_PRESERVE_ALL;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *PluginProcessState) {
  NevercIRPassDescriptor Descriptor;
  (void)Registrar;
  (void)PluginProcessState;
  if (!Core || !PassAPI)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
  CoreAPI = Core;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header =
      (NevercABITableHeader){sizeof(Descriptor), NEVERC_IR_PASS_API_MAJOR,
                            NEVERC_IR_PASS_API_MINOR, 0};
  Descriptor.PassID = SV("example.module-overview");
  Descriptor.Phase =
      (NevercInterfaceID){NEVERC_PHASE_IR_PASS_POST_OPT_HIGH,
                          NEVERC_PHASE_IR_PASS_POST_OPT_LOW};
  Descriptor.Level = NEVERC_IR_PASS_LEVEL_MODULE;
  Descriptor.Deterministic = NEVERC_TRUE;
  Descriptor.Run = run_pass;
  return PassAPI->RegisterPass(PassAPI->Context, RegistrarContext, &Descriptor);
}

NEVERC_EXPORT NevercStatus NEVERC_CALL neverc_plugin_entry(
    const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor;
  const void *Table = NULL;
  uint16_t Minor = 0;
  uint64_t StructSize = 0;
  uint32_t Capacity;
  size_t Writable;
  NevercStatus Status;
  if (!Bootstrap || !Bootstrap->QueryInterface || !OutPlugin ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);
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
    return failure(NEVERC_STATUS_ABI_MISMATCH);
  PassAPI = (const NevercIRPassAPI *)Table;

  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header =
      (NevercABITableHeader){sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR,
                            NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = SV(PLUGIN_ID);
  Descriptor.DisplayName = SV("NeverC first-release IR example");
  Descriptor.Version.Major = 1;
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_NONE;
  Descriptor.Register = register_plugin;
  Writable = Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  memcpy(OutPlugin, &Descriptor, Writable);
  OutPlugin->Header.StructSize = sizeof(Descriptor);
  return neverc_status_ok();
}
