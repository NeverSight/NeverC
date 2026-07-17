#ifndef NEVERC_TESTS_PLUGINFRONTENDTESTSUPPORT_H
#define NEVERC_TESTS_PLUGINFRONTENDTESTSUPPORT_H

#include "neverc/Plugin/PluginCore.h"
#include "neverc/Scan/Token.h"
#include "gtest/gtest.h"
#include <array>
#include <memory>
#include <vector>

namespace neverc {
class CompilerInstance;
class PrepEngine;
class SourceManager;

namespace plugin {
class FrontendPluginBridge;
class PluginActivationPlan;
class PluginPrepBridge;
class PluginProcessServices;
class PluginSession;
class PluginTaskContext;
} // namespace plugin

namespace test {

std::array<NevercInterfaceID, 6> frontendInterfaceIDs();

class PluginPrepTest : public testing::Test {
protected:
  PluginPrepTest();
  ~PluginPrepTest() override;

  void SetUp() override;
  void TearDown() override;

  plugin::PluginTaskContext &task();
  PrepEngine &prep();
  SourceManager &sourceManager();
  plugin::FrontendPluginBridge &locations();
  plugin::PluginPrepBridge &prepBridge();
  std::vector<Token> lexAll();

private:
  std::unique_ptr<plugin::PluginProcessServices> Services;
  std::unique_ptr<plugin::PluginActivationPlan> Plan;
  std::unique_ptr<plugin::PluginSession> Session;
  std::unique_ptr<plugin::PluginTaskContext> Task;
  std::unique_ptr<CompilerInstance> Compiler;
  std::unique_ptr<plugin::FrontendPluginBridge> Locations;
  std::unique_ptr<plugin::PluginPrepBridge> PrepBridge;
};

} // namespace test
} // namespace neverc

#endif
