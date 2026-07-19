/*
 * First-release ABI microbenchmark example.
 *
 * The pass deliberately performs many read-only table calls and preserves all
 * analyses. It is useful for measuring plugin call overhead without changing
 * the compiled program.
 */
#include "neverc/Plugin/NevercPluginAPI.h"
#include <stddef.h>
#include <string.h>

#define PLUGIN_ID "org.neverc.example.abi-bench"
#define SV(Text)                                                               \
  (NevercStringView) { (Text), (uint64_t)(sizeof(Text) - 1) }

static const NevercIRPassAPI *PassAPI;
static const NevercCoreAPI *CoreAPI;

static NevercStatus failure(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static NevercStatus NEVERC_CALL
run_benchmark(const NevercIRPassInvocation *Invocation,
              NevercIRPreservedAnalyses *OutPreserved, void *UserData) {
  NevercStringView Identifier = {0};
  NevercDiagnosticDescriptor Diagnostic;
  NevercDiagnosticHandle DiagnosticHandle = {0};
  NevercStatus Status = neverc_status_ok();
  uint32_t Iteration;
  (void)UserData;
  if (!Invocation || !Invocation->Core || !OutPreserved || !CoreAPI)
    return failure(NEVERC_STATUS_INVALID_ARGUMENT);

  for (Iteration = 0; Iteration != 10000; ++Iteration) {
    Status = Invocation->Core->GetModuleIdentifier(
        Invocation->Core->Context, Invocation->Task, &Identifier);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }

  memset(&Diagnostic, 0, sizeof(Diagnostic));
  Diagnostic.Header =
      (NevercABITableHeader){sizeof(Diagnostic), NEVERC_CORE_API_MAJOR,
                            NEVERC_CORE_API_MINOR, 0};
  Diagnostic.Severity = NEVERC_DIAGNOSTIC_REMARK;
  Diagnostic.Code = 5201;
  Diagnostic.PluginID = SV(PLUGIN_ID);
  Diagnostic.Message =
      SV("BenchPlugin completed 10000 typed IR table calls");
  Status = CoreAPI->EmitDiagnostic(CoreAPI->Context, &Diagnostic,
                                   &DiagnosticHandle);
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
  Descriptor.PassID = SV("bench.typed-table-calls");
  Descriptor.Phase =
      (NevercInterfaceID){NEVERC_PHASE_IR_PASS_PRE_CODEGEN_HIGH,
                          NEVERC_PHASE_IR_PASS_PRE_CODEGEN_LOW};
  Descriptor.Level = NEVERC_IR_PASS_LEVEL_MODULE;
  Descriptor.Deterministic = NEVERC_TRUE;
  Descriptor.Run = run_benchmark;
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
  Descriptor.DisplayName = SV("NeverC first-release ABI benchmark");
  Descriptor.Version.Major = 1;
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_NONE;
  Descriptor.Register = register_plugin;
  Writable = Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  memcpy(OutPlugin, &Descriptor, Writable);
  OutPlugin->Header.StructSize = sizeof(Descriptor);
  return neverc_status_ok();
}
