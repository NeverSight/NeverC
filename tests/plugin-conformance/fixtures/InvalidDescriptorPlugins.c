/*===-- InvalidDescriptorPlugins.c - descriptor validation fixtures *- C -*-===*\
|*                                                                            *|
|* One source that builds a valid descriptor and then, under a -D selector,    *|
|* introduces exactly one defect (or the positive "unknown tail" case). The    *|
|* conformance test builds each variant and asserts the host accepts or        *|
|* rejects it as the ABI requires.                                             *|
|*                                                                            *|
|* Selectors (define at most one): NCF_WRONG_MAJOR, NCF_NEWER_MINOR,           *|
|* NCF_BAD_FLAGS, NCF_SHORT_STRUCT, NCF_NULL_REGISTER, NCF_EMPTY_ID,           *|
|* NCF_NONCANON_ID, NCF_EMPTY_NAME, NCF_BAD_CONCURRENCY, NCF_BAD_REENTRANCY,   *|
|* NCF_ENTRY_ERROR, NCF_MISSING_CAP, NCF_UNKNOWN_TAIL.                         *|
\*===----------------------------------------------------------------------===*/

#include "ConformanceFixture.h"

#define PLUGIN_ID "com.neverc.conformance.invalid"

static NevercStatus NEVERC_CALL ncf_register(const NevercCoreAPI *Core,
                                             const NevercRegistrarAPI *Registrar,
                                             void *RegistrarContext,
                                             void *ProcessState) {
  (void)Core;
  (void)Registrar;
  (void)RegistrarContext;
  (void)ProcessState;
  ncf_log("invalid:register");
  return neverc_status_ok();
}

#if defined(NCF_MISSING_CAP)
/* Must outlive neverc_plugin_entry: the host copies the array after it returns. */
static NevercInterfaceRequirement ncf_missing_requirement;
#endif

NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor;
  if (Bootstrap == NULL || OutPlugin == NULL ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return ncf_status(NEVERC_STATUS_INVALID_ARGUMENT);

#if defined(NCF_ENTRY_ERROR)
  (void)Descriptor;
  return ncf_status(NEVERC_STATUS_PLUGIN_FAILURE);
#else
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      (uint32_t)sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR,
      NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = NCF_SV(PLUGIN_ID);
  Descriptor.DisplayName = NCF_SV("Conformance Invalid-Descriptor Plugin");
  Descriptor.Version = (NevercSemanticVersion){1, 0, 0, 0, {0, 0}, {0, 0}};
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_ALLOWED;
  Descriptor.Register = ncf_register;

#if defined(NCF_WRONG_MAJOR)
  Descriptor.Header.Major = (uint16_t)(NEVERC_PLUGIN_ABI_MAJOR + 1);
#endif
#if defined(NCF_NEWER_MINOR)
  Descriptor.Header.Minor = (uint16_t)(NEVERC_PLUGIN_ABI_MINOR + 1);
#endif
#if defined(NCF_BAD_FLAGS)
  Descriptor.Header.Flags = UINT64_C(1);
#endif
#if defined(NCF_NULL_REGISTER)
  Descriptor.Register = NULL;
#endif
#if defined(NCF_EMPTY_ID)
  Descriptor.PluginID = (NevercStringView){"", 0};
#endif
#if defined(NCF_NONCANON_ID)
  Descriptor.PluginID = NCF_SV("Bad.ID");
#endif
#if defined(NCF_EMPTY_NAME)
  Descriptor.DisplayName = (NevercStringView){"", 0};
#endif
#if defined(NCF_BAD_CONCURRENCY)
  Descriptor.Concurrency = (NevercConcurrencyModel)999;
#endif
#if defined(NCF_BAD_REENTRANCY)
  Descriptor.Reentrancy = (NevercReentrancyModel)999;
#endif
#if defined(NCF_MISSING_CAP)
  memset(&ncf_missing_requirement, 0, sizeof(ncf_missing_requirement));
  ncf_missing_requirement.Header = (NevercABITableHeader){
      (uint32_t)sizeof(ncf_missing_requirement), NEVERC_CORE_API_MAJOR,
      NEVERC_CORE_API_MINOR, 0};
  /* An interface ID the host does not provide. */
  ncf_missing_requirement.Interface =
      (NevercInterfaceID){UINT64_C(0x4e4f545245414cff), UINT64_C(0xdeadbeef)};
  ncf_missing_requirement.Major = 1;
  ncf_missing_requirement.MinimumMinor = 0;
  ncf_missing_requirement.Required = NEVERC_TRUE;
  ncf_missing_requirement.Stability = NEVERC_INTERFACE_STABLE;
  ncf_missing_requirement.Compatibility.Header = (NevercABITableHeader){
      (uint32_t)sizeof(ncf_missing_requirement.Compatibility),
      NEVERC_CORE_API_MAJOR, NEVERC_CORE_API_MINOR, 0};
  Descriptor.RequiredInterfaces = (NevercStructArrayView){
      &ncf_missing_requirement, 1, sizeof(ncf_missing_requirement)};
#endif

  ncf_write_descriptor(OutPlugin, &Descriptor);

#if defined(NCF_SHORT_STRUCT)
  OutPlugin->Header.StructSize = (uint32_t)sizeof(NevercABITableHeader);
#elif defined(NCF_UNKNOWN_TAIL)
  /* Advertise a larger descriptor than the host knows; it must read only its
   * own prefix and accept the plugin (forward compatibility). */
  OutPlugin->Header.StructSize = (uint32_t)sizeof(Descriptor) + 64;
#endif
  return neverc_status_ok();
#endif /* NCF_ENTRY_ERROR */
}
