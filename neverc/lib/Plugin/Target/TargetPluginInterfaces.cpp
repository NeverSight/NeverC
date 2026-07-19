#include "neverc/Plugin/PluginTarget.h"
#include <cstddef>
#include <type_traits>

namespace neverc::plugin {

static_assert(std::is_standard_layout_v<NevercTargetAPI>);
static_assert(std::is_standard_layout_v<NevercTargetABIAPI>);
static_assert(std::is_standard_layout_v<NevercCallingConventionAPI>);
static_assert(offsetof(NevercTargetAPI, Header) == 0);
static_assert(offsetof(NevercTargetABIAPI, Header) == 0);
static_assert(offsetof(NevercCallingConventionAPI, Header) == 0);

} // namespace neverc::plugin
