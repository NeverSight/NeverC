//===- ConformanceTest.h - base fixture for the conformance suite -------===//
//
// A GoogleTest base fixture that skips cleanly when the harness has no usable
// environment (e.g. no system C compiler) and gives each test an isolated
// temporary directory, a trivial input translation unit, and a lifecycle log
// path. Tests drive the compiler only through Environment's public helpers.
//
//===----------------------------------------------------------------------===//

#ifndef NEVERC_PLUGIN_CONFORMANCE_TEST_H
#define NEVERC_PLUGIN_CONFORMANCE_TEST_H

#include "ConformanceEnvironment.h"

#include "gtest/gtest.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace neverc::conformance {

class ConformanceTest : public ::testing::Test {
protected:
  const Environment &Env = Environment::get();
  std::string Dir;

  void SetUp() override {
    if (!Env.usable())
      GTEST_SKIP() << "conformance environment unusable: " << Env.whyUnusable();
    Dir = Env.makeTempDir(label());
  }

  virtual std::string label() const { return "conf"; }

  std::string writeSource(const std::string &Name, const std::string &Body) {
    const std::string Path = (std::filesystem::path(Dir) / Name).string();
    std::ofstream(Path, std::ios::binary) << Body;
    return Path;
  }

  std::string trivialInput() {
    return writeSource("input.c",
                       "int neverc_conformance_input(void) { return 0; }\n");
  }

  std::string logPath() const {
    return (std::filesystem::path(Dir) / "lifecycle.log").string();
  }

  std::string readLog() const {
    std::ifstream Stream(logPath(), std::ios::binary);
    std::stringstream Buffer;
    Buffer << Stream.rdbuf();
    return Buffer.str();
  }

  // Build a fixture, failing the test (not skipping) if the SDK cannot compile
  // it -- that would be a real packaging regression.
  std::string buildOrFail(const std::string &Fixture,
                          const std::vector<std::string> &Defines = {}) {
    std::string Error;
    const std::string Path = Env.buildPlugin(Dir, Fixture, Defines, Error);
    EXPECT_FALSE(Path.empty())
        << "system compiler could not build fixture " << Fixture
        << " against the staged SDK:\n"
        << Error;
    return Path;
  }
};

} // namespace neverc::conformance

#endif
