/*===-- DependencyPlugins.c - dependency/ordering conformance fixture *-C-*-===*\
|*                                                                            *|
|* A configurable plugin used to build several identities that declare         *|
|* dependencies on each other. It logs "<id>:register" so the test can observe *|
|* load ordering, and pairs of these reproduce dependency cycles and ID        *|
|* conflicts.                                                                  *|
|*                                                                            *|
|* Configure with: -DNCF_ID="<canonical id>" (required for real use),          *|
|*   -DNCF_DEP_ID="<canonical id>" and -DNCF_DEP_KIND=<kind constant>          *|
|*   (NEVERC_DEPENDENCY_REQUIRED / _BEFORE / _AFTER).                          *|
\*===----------------------------------------------------------------------===*/

#include "ConformanceFixture.h"

#ifndef NCF_ID
#define NCF_ID "com.neverc.conformance.dep.self"
#endif

#ifndef NCF_DEP_KIND
#define NCF_DEP_KIND NEVERC_DEPENDENCY_REQUIRED
#endif

static NevercStatus NEVERC_CALL ncf_register(const NevercCoreAPI *Core,
                                             const NevercRegistrarAPI *Registrar,
                                             void *RegistrarContext,
                                             void *ProcessState) {
  (void)Registrar;
  (void)RegistrarContext;
  (void)ProcessState;
  ncf_log(NCF_ID ":register");
  ncf_emit(Core, NCF_ID, 7100, "conformance dependency plugin registered");
  return neverc_status_ok();
}

#if defined(NCF_DEP_ID)
/* Must outlive neverc_plugin_entry: the host copies the array afterwards. */
static NevercPluginDependency ncf_dependency;
#endif

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
  Descriptor.PluginID =
      (NevercStringView){NCF_ID, (uint64_t)strlen(NCF_ID)};
  Descriptor.DisplayName = NCF_SV("Conformance Dependency Plugin");
  Descriptor.Version = (NevercSemanticVersion){1, 0, 0, 0, {0, 0}, {0, 0}};
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_ALLOWED;
  Descriptor.Register = ncf_register;

#if defined(NCF_DEP_ID)
  memset(&ncf_dependency, 0, sizeof(ncf_dependency));
  ncf_dependency.Header = (NevercABITableHeader){
      (uint32_t)sizeof(ncf_dependency), NEVERC_CORE_API_MAJOR,
      NEVERC_CORE_API_MINOR, 0};
  ncf_dependency.PluginID =
      (NevercStringView){NCF_DEP_ID, (uint64_t)strlen(NCF_DEP_ID)};
  ncf_dependency.Kind = NCF_DEP_KIND;
  ncf_dependency.Version.MinimumInclusive =
      (NevercSemanticVersion){0, 0, 0, 0, {0, 0}, {0, 0}};
  ncf_dependency.Version.MaximumExclusive =
      (NevercSemanticVersion){0, 0, 0, 0, {0, 0}, {0, 0}};
  ncf_dependency.Version.HasMaximum = NEVERC_FALSE;
  ncf_dependency.Version.AllowPrerelease = NEVERC_FALSE;
  ncf_dependency.Version.Reserved = 0;
  Descriptor.Dependencies =
      (NevercStructArrayView){&ncf_dependency, 1, sizeof(ncf_dependency)};
#endif

  ncf_write_descriptor(OutPlugin, &Descriptor);
  return neverc_status_ok();
}
