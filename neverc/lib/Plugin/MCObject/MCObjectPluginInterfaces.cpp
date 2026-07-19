#include "neverc/Plugin/PluginMC.h"
#include "neverc/Plugin/PluginObject.h"
#include <cstddef>
#include <type_traits>

namespace neverc::plugin {

static_assert(std::is_standard_layout_v<NevercMCAPI>);
static_assert(std::is_standard_layout_v<NevercObjectAPI>);
static_assert(std::is_standard_layout_v<NevercObjectFormatAPI>);
static_assert(offsetof(NevercMCAPI, Header) == 0);
static_assert(offsetof(NevercObjectAPI, Header) == 0);
static_assert(offsetof(NevercObjectFormatAPI, Header) == 0);

} // namespace neverc::plugin
