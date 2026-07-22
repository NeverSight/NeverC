//===- PluginSDKGenerationProbe.h - single vs modular ABI probes ----------===//
//
// Shared declaration for the SDK single-header generation test. One translation
// unit includes the distributed single header and fills a probe table; the test
// translation unit includes the modular aggregate and compares it entry by
// entry, proving the generated single header is byte-for-byte equivalent in
// declared sizes and discriminant constants.
//
//===----------------------------------------------------------------------===//

#ifndef NEVERC_TESTS_PLUGIN_SDK_GENERATION_PROBE_H
#define NEVERC_TESTS_PLUGIN_SDK_GENERATION_PROBE_H

#include <cstddef>

struct NevercSDKGenProbe {
  const char *Name;
  unsigned long long Value;
};

extern "C" const NevercSDKGenProbe *
nevercSDKGenSingleHeaderProbes(std::size_t *Count);

#endif
