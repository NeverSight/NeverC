/*
 * Freestanding-style first-release plugin example.
 *
 * This source does not call the C runtime. Diagnostics and all compiler access
 * go through host-owned ABI tables, while tiny local byte helpers initialize
 * and copy descriptors.
 */
#include "neverc/Plugin/NevercPluginAPI.h"
#include <stddef.h>

#define PLUGIN_ID "org.neverc.example.crt-shim"
#define SV(Text)                                                               \
  (NevercStringView) { (Text), (uint64_t)(sizeof(Text) - 1) }

static const NevercIRPassAPI *PassAPI;
static const NevercCoreAPI *CoreAPI;

static void zero_bytes(void *Pointer, size_t Size) {
  unsigned char *Bytes = (unsigned char *)Pointer;
  size_t Index;
  for (Index = 0; Index != Size; ++Index)
    Bytes[Index] = 0;
}

static void copy_bytes(void *Destination, const void *Source, size_t Size) {
  unsigned char *Out = (unsigned char *)Destination;
  const unsigned char *In = (const unsigned char *)Source;
  size_t Index;
  for (Index = 0; Index != Size; ++Index)
    Out[Index] = In[Index];
}

static NevercStatus failure(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static NevercStatus NEVERC_CALL
run_census(const NevercIRPassInvocation *Invocation,
           NevercIRPreservedAnalyses *OutPreserved, void *UserData) {
  NevercIRValueCursor Cursor;
  NevercIRValueHandle Functions[8];
  NevercDiagnosticDescriptor Diagnostic;
  NevercDiagnosticHandle DiagnosticHandle = {0};
  NevercStatus Status;
  uint64_t Count = 0;
  (void)UserData;
  if (!Invocation || !Invocation->Core || !OutPreserved || !CoreAPI)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  zero_bytes(&Cursor, sizeof(Cursor));
  Status = Invocation->Core->BeginValueCursor(
      Invocation->Core->Context, Invocation->Task, Invocation->Module,
      NEVERC_IR_COLLECTION_MODULE_FUNCTIONS, &Cursor);
  if (Status.Code == NEVERC_STATUS_OK)
    Status = Invocation->Core->CollectValueCursor(
        Invocation->Core->Context, Invocation->Task, &Cursor, Functions,
        sizeof(Functions) / sizeof(Functions[0]), &Count);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  zero_bytes(&Diagnostic, sizeof(Diagnostic));
  Diagnostic.Header =
      (NevercABITableHeader){sizeof(Diagnostic), NEVERC_CORE_API_MAJOR,
                            NEVERC_CORE_API_MINOR, 0};
  Diagnostic.Severity = NEVERC_DIAGNOSTIC_REMARK;
  Diagnostic.Code = 5301;
  Diagnostic.PluginID = SV(PLUGIN_ID);
  Diagnostic.Message =
      Count == 0 ? SV("CrtShimPlugin observed no functions")
                 : SV("CrtShimPlugin queried IR without C runtime calls");
  Status = CoreAPI->EmitDiagnostic(CoreAPI->Context, &Diagnostic,
                                   &DiagnosticHandle);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  zero_bytes(OutPreserved, sizeof(*OutPreserved));
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
  zero_bytes(&Descriptor, sizeof(Descriptor));
  Descriptor.Header =
      (NevercABITableHeader){sizeof(Descriptor), NEVERC_IR_PASS_API_MAJOR,
                            NEVERC_IR_PASS_API_MINOR, 0};
  Descriptor.PassID = SV("crt-shim.census");
  Descriptor.Phase =
      (NevercInterfaceID){NEVERC_PHASE_IR_PASS_POST_OPT_HIGH,
                          NEVERC_PHASE_IR_PASS_POST_OPT_LOW};
  Descriptor.Level = NEVERC_IR_PASS_LEVEL_MODULE;
  Descriptor.Deterministic = NEVERC_TRUE;
  Descriptor.Run = run_census;
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
  zero_bytes(&Descriptor, sizeof(Descriptor));
  Descriptor.Header =
      (NevercABITableHeader){sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR,
                            NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = SV(PLUGIN_ID);
  Descriptor.DisplayName = SV("NeverC freestanding-style plugin example");
  Descriptor.Version.Major = 1;
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_NONE;
  Descriptor.Register = register_plugin;
  Writable = Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  copy_bytes(OutPlugin, &Descriptor, Writable);
  OutPlugin->Header.StructSize = sizeof(Descriptor);
  return neverc_status_ok();
}
