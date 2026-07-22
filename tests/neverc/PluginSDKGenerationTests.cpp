//===- PluginSDKGenerationTests.cpp - SDK header generation tests --------===//
//
// Verifies the generated distributed single header is consistent with the
// modular headers it is inlined from: identical declared struct sizes and
// discriminant constants, and a stable first-version ABI version. This
// translation unit includes the modular aggregate; PluginSDKGenerationSingleHeader.cpp
// includes the distributed single header. The two probe tables must match.
//
//===----------------------------------------------------------------------===//

#include "neverc/Plugin/NevercPluginAPI.h"

#include "PluginSDKGenerationProbe.h"
#include "gtest/gtest.h"

#include <cstddef>
#include <vector>

namespace {

std::vector<NevercSDKGenProbe> modularProbes() {
  return {
#define NEVERC_ABI_VALUE(expr)                                                  \
  {#expr, static_cast<unsigned long long>(expr)},
#define NEVERC_ABI_SIZE(type)                                                   \
  {"sizeof:" #type, static_cast<unsigned long long>(sizeof(type))},
#include "PluginSDKGenerationValues.def"
#undef NEVERC_ABI_VALUE
#undef NEVERC_ABI_SIZE
  };
}

TEST(PluginSDKGenerationTest, SingleHeaderMatchesModularHeaders) {
  const std::vector<NevercSDKGenProbe> Modular = modularProbes();
  std::size_t Count = 0;
  const NevercSDKGenProbe *Single = nevercSDKGenSingleHeaderProbes(&Count);
  ASSERT_EQ(Count, Modular.size());
  ASSERT_GT(Count, 0u);
  for (std::size_t I = 0; I < Count; ++I) {
    EXPECT_STREQ(Single[I].Name, Modular[I].Name);
    EXPECT_EQ(Single[I].Value, Modular[I].Value)
        << "single header and modular headers disagree on " << Modular[I].Name;
  }
}

TEST(PluginSDKGenerationTest, LocksFirstVersionABIVersion) {
  EXPECT_EQ(NEVERC_PLUGIN_ABI_MAJOR, UINT16_C(1));
  EXPECT_EQ(NEVERC_PLUGIN_ABI_MINOR, UINT16_C(0));
}

} // namespace
