#include "neverc/Plugin/Host/PluginRegistration.h"
#include "gtest/gtest.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include <array>

using namespace llvm;
using namespace neverc::plugin;

namespace {

std::string takeErrorMessage(Error ErrorValue) {
  auto Message = toString(std::move(ErrorValue));
  return Message.str().str();
}

std::string takeErrorMessage(Expected<PluginActivationPlan> &Result) {
  return takeErrorMessage(Result.takeError());
}

std::shared_ptr<const PluginModule> load(PluginRegistry &Registry,
                                         StringRef Path) {
  auto Result = Registry.load(Path);
  if (!Result) {
    ADD_FAILURE() << takeErrorMessage(Result.takeError());
    return nullptr;
  }
  return *Result;
}

TEST(PluginDependencyTest, RequiredDependencyDeterminesStableOrder) {
  PluginRegistry Registry("neverc-plugin-dependency-tests",
                          LLVM_VERSION_MAJOR);
  ASSERT_NE(load(Registry, NEVERC_TEST_DEPENDENCY_B_PLUGIN), nullptr);
  ASSERT_NE(load(Registry, NEVERC_TEST_DEPENDENCY_A_PLUGIN), nullptr);

  const std::array<StringRef, 2> Selected = {
      "org.neverc.test.dependency.b", "org.neverc.test.dependency.a"};
  auto Plan = makePluginActivationPlan(Registry, Selected);
  ASSERT_TRUE(static_cast<bool>(Plan));
  ASSERT_EQ(Plan->plugins().size(), 2u);
  EXPECT_EQ(Plan->plugins()[0]->descriptor().PluginID,
            "org.neverc.test.dependency.a");
  EXPECT_EQ(Plan->plugins()[1]->descriptor().PluginID,
            "org.neverc.test.dependency.b");
}

TEST(PluginDependencyTest, ResidentButUnselectedPluginCannotSatisfyDependency) {
  PluginRegistry Registry("neverc-plugin-dependency-tests",
                          LLVM_VERSION_MAJOR);
  ASSERT_NE(load(Registry, NEVERC_TEST_DEPENDENCY_A_PLUGIN), nullptr);
  ASSERT_NE(load(Registry, NEVERC_TEST_DEPENDENCY_MISSING_PLUGIN), nullptr);

  const std::array<StringRef, 1> Selected = {
      "org.neverc.test.dependency.missing"};
  auto Plan = makePluginActivationPlan(Registry, Selected);
  ASSERT_FALSE(static_cast<bool>(Plan));
  std::string Message = takeErrorMessage(Plan);
  EXPECT_NE(Message.find("org.neverc.test.dependency.a"), std::string::npos);
  EXPECT_NE(Message.find("activation set"), std::string::npos);
}

TEST(PluginDependencyTest, RejectsDependencyVersionMismatch) {
  PluginRegistry Registry("neverc-plugin-dependency-tests",
                          LLVM_VERSION_MAJOR);
  ASSERT_NE(load(Registry, NEVERC_TEST_DEPENDENCY_A_PLUGIN), nullptr);
  ASSERT_NE(load(Registry, NEVERC_TEST_DEPENDENCY_MISMATCH_PLUGIN), nullptr);

  const std::array<StringRef, 2> Selected = {
      "org.neverc.test.dependency.a",
      "org.neverc.test.dependency.mismatch"};
  auto Plan = makePluginActivationPlan(Registry, Selected);
  ASSERT_FALSE(static_cast<bool>(Plan));
  EXPECT_NE(takeErrorMessage(Plan).find("version"), std::string::npos);
}

TEST(PluginDependencyTest, ReportsDependencyCycleWithPluginIDs) {
  PluginRegistry Registry("neverc-plugin-dependency-tests",
                          LLVM_VERSION_MAJOR);
  ASSERT_NE(load(Registry, NEVERC_TEST_CYCLE_A_PLUGIN), nullptr);
  ASSERT_NE(load(Registry, NEVERC_TEST_CYCLE_B_PLUGIN), nullptr);

  const std::array<StringRef, 2> Selected = {
      "org.neverc.test.cycle.a", "org.neverc.test.cycle.b"};
  auto Plan = makePluginActivationPlan(Registry, Selected);
  ASSERT_FALSE(static_cast<bool>(Plan));
  std::string Message = takeErrorMessage(Plan);
  EXPECT_NE(Message.find("cycle"), std::string::npos);
  EXPECT_NE(Message.find("org.neverc.test.cycle.a"), std::string::npos);
  EXPECT_NE(Message.find("org.neverc.test.cycle.b"), std::string::npos);
}

TEST(PluginDependencyTest, ActivationPlanPinsRegistrySnapshot) {
  PluginRegistry Registry("neverc-plugin-dependency-tests",
                          LLVM_VERSION_MAJOR);
  ASSERT_NE(load(Registry, NEVERC_TEST_DEPENDENCY_A_PLUGIN), nullptr);

  const std::array<StringRef, 1> Selected = {
      "org.neverc.test.dependency.a"};
  {
    auto Plan = makePluginActivationPlan(Registry, Selected);
    ASSERT_TRUE(static_cast<bool>(Plan));
    EXPECT_EQ(Registry.activeSnapshotLeases(), 1u);
    Error Shutdown = Registry.shutdown();
    ASSERT_TRUE(static_cast<bool>(Shutdown));
    EXPECT_NE(takeErrorMessage(std::move(Shutdown)).find("snapshot lease"),
              std::string::npos);
  }
  EXPECT_EQ(Registry.activeSnapshotLeases(), 0u);
}

TEST(PluginDependencyTest, RejectsUnloadWhileRequiredDependentIsResident) {
  PluginRegistry Registry("neverc-plugin-dependency-tests",
                          LLVM_VERSION_MAJOR);
  ASSERT_NE(load(Registry, NEVERC_TEST_DEPENDENCY_A_PLUGIN), nullptr);
  ASSERT_NE(load(Registry, NEVERC_TEST_DEPENDENCY_B_PLUGIN), nullptr);

  Error DependencyInUse =
      Registry.unload("org.neverc.test.dependency.a");
  ASSERT_TRUE(static_cast<bool>(DependencyInUse));
  EXPECT_NE(takeErrorMessage(std::move(DependencyInUse)).find("required by"),
            std::string::npos);
  EXPECT_EQ(Registry.moduleCount(), 2u);
  EXPECT_EQ(Registry.generation(), 2u);

  EXPECT_FALSE(Registry.unload("org.neverc.test.dependency.b"));
  EXPECT_FALSE(Registry.unload("org.neverc.test.dependency.a"));
  EXPECT_EQ(Registry.moduleCount(), 0u);
  EXPECT_EQ(Registry.generation(), 4u);
}

} // namespace
