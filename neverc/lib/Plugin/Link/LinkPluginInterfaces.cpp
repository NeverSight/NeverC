#include "neverc/Plugin/Host/LinkPluginInterfaces.h"
#include "neverc/Plugin/PluginLTO.h"
#include "neverc/Plugin/PluginPhaseSchema.h"
#include <array>
#include <type_traits>

namespace neverc::plugin {
namespace {

#define NEVERC_LINK_PHASE_ID(Symbol)                                          \
  NevercInterfaceID {                                                        \
    NEVERC_PHASE_##Symbol##_HIGH, NEVERC_PHASE_##Symbol##_LOW                \
  }

constexpr std::array<NevercInterfaceID, NEVERC_BUILTIN_LINK_PHASE_COUNT>
    LinkPhaseIDs = {{
        NEVERC_LINK_PHASE_ID(LINK_INPUT_PROBE),
        NEVERC_LINK_PHASE_ID(LINK_READ_INPUTS),
        NEVERC_LINK_PHASE_ID(LINK_LTO_RESOLVE),
        NEVERC_LINK_PHASE_ID(LINK_LTO_GENERATE),
        NEVERC_LINK_PHASE_ID(LINK_RESOLVE_SYMBOLS),
        NEVERC_LINK_PHASE_ID(LINK_SELECT_COMDAT),
        NEVERC_LINK_PHASE_ID(LINK_GC),
        NEVERC_LINK_PHASE_ID(LINK_ICF),
        NEVERC_LINK_PHASE_ID(LINK_SYNTHESIZE),
        NEVERC_LINK_PHASE_ID(LINK_RELAX_THUNKS),
        NEVERC_LINK_PHASE_ID(LINK_LAYOUT),
        NEVERC_LINK_PHASE_ID(LINK_RELOCATE),
        NEVERC_LINK_PHASE_ID(LINK_EMIT_IMAGE),
        NEVERC_LINK_PHASE_ID(LINK_FULL),
        NEVERC_LINK_PHASE_ID(LINK_OBJECT_MERGE),
        NEVERC_LINK_PHASE_ID(LINK_POST_EMIT),
        NEVERC_LINK_PHASE_ID(LINK_IMAGE_VERIFY),
        NEVERC_LINK_PHASE_ID(LINK_SIDE_OUTPUTS_VERIFY),
        NEVERC_LINK_PHASE_ID(LINK_COMMIT),
        NEVERC_LINK_PHASE_ID(LINK_AFTER_COMMIT),
    }};

#undef NEVERC_LINK_PHASE_ID

constexpr bool hasUniquePhaseIDs() {
  for (size_t I = 0; I != LinkPhaseIDs.size(); ++I)
    for (size_t J = I + 1; J != LinkPhaseIDs.size(); ++J)
      if (LinkPhaseIDs[I].High == LinkPhaseIDs[J].High &&
          LinkPhaseIDs[I].Low == LinkPhaseIDs[J].Low)
        return false;
  return true;
}

static_assert(std::is_standard_layout_v<NevercLinkAPI>);
static_assert(std::is_standard_layout_v<NevercLinkRegistrarAPI>);
static_assert(std::is_standard_layout_v<NevercLTOAPI>);
static_assert(std::is_standard_layout_v<NevercLTORegistrarAPI>);
static_assert(LinkPhaseIDs.size() == 20);
static_assert(hasUniquePhaseIDs());

} // namespace

NevercInterfaceID linkInterfaceID() {
  return {NEVERC_INTERFACE_LINK_HIGH, NEVERC_INTERFACE_LINK_LOW};
}

NevercInterfaceID linkRegistrarInterfaceID() {
  return {NEVERC_INTERFACE_LINK_REGISTRAR_HIGH,
          NEVERC_INTERFACE_LINK_REGISTRAR_LOW};
}

NevercInterfaceID ltoInterfaceID() {
  return {NEVERC_INTERFACE_LTO_HIGH, NEVERC_INTERFACE_LTO_LOW};
}

NevercInterfaceID ltoRegistrarInterfaceID() {
  return {NEVERC_INTERFACE_LTO_REGISTRAR_HIGH,
          NEVERC_INTERFACE_LTO_REGISTRAR_LOW};
}

llvm::ArrayRef<NevercInterfaceID> builtInLinkPhaseIDs() {
  return LinkPhaseIDs;
}

} // namespace neverc::plugin
