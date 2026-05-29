#ifndef NEVERC_LIB_PLUGIN_HOSTAPIBRIDGE_H
#define NEVERC_LIB_PLUGIN_HOSTAPIBRIDGE_H

#include "neverc/Plugin/NevercPluginAPI.h"

namespace neverc {
namespace plugin {

/// Build a fully-populated NevercHostAPI vtable.
/// The returned struct uses function pointers that bridge C calls into
/// LLVM C++ API invocations, with memory routed through the host allocator.
NevercHostAPI buildHostAPI();

} // namespace plugin
} // namespace neverc

#endif
