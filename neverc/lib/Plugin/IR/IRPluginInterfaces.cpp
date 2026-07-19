#include "neverc/Plugin/PluginIR.h"
#include "neverc/Plugin/PluginMIR.h"
#include <array>
#include <cstddef>

namespace {

constexpr bool sameInterface(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

constexpr std::array<NevercInterfaceID, 9> InterfaceIDs = {{
    {NEVERC_INTERFACE_IR_CORE_HIGH, NEVERC_INTERFACE_IR_CORE_LOW},
    {NEVERC_INTERFACE_IR_GEN_HIGH, NEVERC_INTERFACE_IR_GEN_LOW},
    {NEVERC_INTERFACE_IR_OPTIMIZATION_HIGH,
     NEVERC_INTERFACE_IR_OPTIMIZATION_LOW},
    {NEVERC_INTERFACE_IR_BUILDER_HIGH, NEVERC_INTERFACE_IR_BUILDER_LOW},
    {NEVERC_INTERFACE_IR_ANALYSIS_HIGH, NEVERC_INTERFACE_IR_ANALYSIS_LOW},
    {NEVERC_INTERFACE_IR_PASS_HIGH, NEVERC_INTERFACE_IR_PASS_LOW},
    {NEVERC_INTERFACE_MIR_HIGH, NEVERC_INTERFACE_MIR_LOW},
    {NEVERC_INTERFACE_MIR_ANALYSIS_HIGH, NEVERC_INTERFACE_MIR_ANALYSIS_LOW},
    {NEVERC_INTERFACE_MIR_PASS_HIGH, NEVERC_INTERFACE_MIR_PASS_LOW},
}};

constexpr bool uniqueInterfaces() {
  for (size_t I = 0; I != InterfaceIDs.size(); ++I) {
    if (InterfaceIDs[I].High == 0 && InterfaceIDs[I].Low == 0)
      return false;
    for (size_t J = I + 1; J != InterfaceIDs.size(); ++J)
      if (sameInterface(InterfaceIDs[I], InterfaceIDs[J]))
        return false;
  }
  return true;
}

static_assert(uniqueInterfaces(),
              "IR and MIR interfaces require unique nonzero IDs");
static_assert(offsetof(NevercIRCoreAPI, Header) == 0);
static_assert(offsetof(NevercIRGenAPI, Header) == 0);
static_assert(offsetof(NevercIRBuilderAPI, Header) == 0);
static_assert(offsetof(NevercIRAnalysisAPI, Header) == 0);
static_assert(offsetof(NevercIRPassAPI, Header) == 0);
static_assert(offsetof(NevercMIRAPI, Header) == 0);
static_assert(offsetof(NevercMIRAnalysisAPI, Header) == 0);
static_assert(offsetof(NevercMIRPassAPI, Header) == 0);

} // namespace
