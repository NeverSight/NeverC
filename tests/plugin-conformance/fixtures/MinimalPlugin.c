/*===-- MinimalPlugin.c - smallest valid conformance fixture ------*- C -*-===*\
|*                                                                            *|
|* The canonical valid plugin: negotiates the Core ABI, registers nothing, and *|
|* records that it loaded. Used as the positive baseline and, with a           *|
|* -DNCF_PLUGIN_ID override, as a second identity for conflict tests.          *|
\*===----------------------------------------------------------------------===*/

#include "ConformanceFixture.h"

#ifndef NCF_PLUGIN_ID
#define NCF_PLUGIN_ID "com.neverc.conformance.minimal"
#endif

static NevercStatus NEVERC_CALL ncf_register(const NevercCoreAPI *Core,
                                             const NevercRegistrarAPI *Registrar,
                                             void *RegistrarContext,
                                             void *ProcessState) {
  (void)RegistrarContext;
  (void)ProcessState;
  if (Registrar == NULL)
    return ncf_status(NEVERC_STATUS_INVALID_ARGUMENT);
  ncf_log("minimal:register");
  ncf_emit(Core, NCF_PLUGIN_ID, 7000, "conformance minimal plugin registered");
  return neverc_status_ok();
}

NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor;
  if (Bootstrap == NULL || OutPlugin == NULL ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return ncf_status(NEVERC_STATUS_INVALID_ARGUMENT);
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      (uint32_t)sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR,
      NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = (NevercStringView){NCF_PLUGIN_ID,
                                           (uint64_t)strlen(NCF_PLUGIN_ID)};
  Descriptor.DisplayName = NCF_SV("Conformance Minimal Plugin");
  Descriptor.Version = (NevercSemanticVersion){1, 0, 0, 0, {0, 0}, {0, 0}};
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_ALLOWED;
  Descriptor.Register = ncf_register;
  ncf_write_descriptor(OutPlugin, &Descriptor);
  return neverc_status_ok();
}
