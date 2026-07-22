//===- PluginConformanceMain.cpp - conformance suite entry point --------===//
//
// Standalone entry point for the NeverC plugin conformance suite. It reports the
// resolved environment (compiler-under-test, system C compiler, staged SDK) so
// failures are diagnosable, then runs the GoogleTest suite. It links only
// GoogleTest and the public SDK -- never the host's private libraries.
//
//===----------------------------------------------------------------------===//

#include "ConformanceEnvironment.h"

#include "gtest/gtest.h"

#include <cstdio>

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  const auto &Env = neverc::conformance::Environment::get();
  if (Env.usable())
    std::fprintf(stderr,
                 "neverc-plugin-conformance:\n  neverc = %s\n  cc     = %s\n"
                 "  sdk    = %s\n  fixtures = %s\n",
                 Env.neverc().c_str(), Env.cc().c_str(),
                 Env.sdkInclude().c_str(), Env.fixturesDir().c_str());
  else
    std::fprintf(stderr,
                 "neverc-plugin-conformance: environment incomplete: %s\n",
                 Env.whyUnusable().c_str());
  return RUN_ALL_TESTS();
}
