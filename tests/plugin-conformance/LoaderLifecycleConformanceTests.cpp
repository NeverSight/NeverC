//===- LoaderLifecycleConformanceTests.cpp - loader & lifecycle ---------===//
//
// Exercises the loader's dependency handling (ordering, cycles, missing
// dependencies, ID conflicts) and the full plugin lifecycle (process ->
// register -> session -> task -> destroy) including that per-scope user data
// survives across callbacks and Destroy runs on unload. Observations use a
// deterministic on-disk lifecycle log written by the fixtures.
//
//===----------------------------------------------------------------------===//

#include "ConformanceTest.h"

#include <filesystem>

namespace neverc::conformance {
namespace {

constexpr const char *DepA = "com.neverc.conformance.dep.a";
constexpr const char *DepB = "com.neverc.conformance.dep.b";

class LoaderLifecycleConformance : public ConformanceTest {
protected:
  std::string label() const override { return "loader"; }

  std::string buildIn(const std::string &Sub, const std::string &Fixture,
                      const std::vector<std::string> &Defines) {
    const std::string SubDir = (std::filesystem::path(Dir) / Sub).string();
    std::error_code EC;
    std::filesystem::create_directories(SubDir, EC);
    std::string Error;
    const std::string Path = Env.buildPlugin(SubDir, Fixture, Defines, Error);
    EXPECT_FALSE(Path.empty()) << "build " << Fixture << " failed:\n" << Error;
    return Path;
  }

  static std::string idDefine(const char *Id) {
    return std::string("NCF_ID=\"") + Id + "\"";
  }
  static std::string depDefine(const char *Id) {
    return std::string("NCF_DEP_ID=\"") + Id + "\"";
  }
};

TEST_F(LoaderLifecycleConformance, LifecycleCallbacksRunInOrderWithState) {
  const std::string Plugin = buildOrFail("LifecyclePlugin");
  ASSERT_FALSE(Plugin.empty());
  const std::string Input = trivialInput();
  const std::string Object = (std::filesystem::path(Dir) / "out.o").string();

  const RunResult R = Env.runNeverc(
      {"-fplugin=" + Plugin, "--no-default-config", "-c", Input, "-o", Object},
      {{"NEVERC_CONFORMANCE_LOG", logPath()}});
  ASSERT_EQ(R.exitCode, 0) << R.err;

  const std::string Log = readLog();
  SCOPED_TRACE(Log);
  // No callback ever saw the wrong state.
  EXPECT_EQ(Log.find("state_bad"), std::string::npos);

  auto Idx = [&](const std::string &Needle) { return Log.find(Needle); };
  const size_t Process = Idx("process_begin");
  const size_t Register = Idx("register:state_ok");
  const size_t Session = Idx("session_begin:state_ok");
  const size_t Task = Idx("task_begin:state_ok");
  const size_t SessionEnd = Idx("session_end:state_ok");
  const size_t Destroy = Idx("destroy:state_ok");

  ASSERT_NE(Process, std::string::npos);
  ASSERT_NE(Register, std::string::npos);
  ASSERT_NE(Session, std::string::npos);
  ASSERT_NE(Task, std::string::npos);
  ASSERT_NE(SessionEnd, std::string::npos);
  ASSERT_NE(Destroy, std::string::npos) << "Destroy must run on unload";

  EXPECT_LT(Process, Register);
  EXPECT_LT(Register, Session);
  EXPECT_LT(Session, Task);
  EXPECT_LT(Task, SessionEnd);
  EXPECT_LT(SessionEnd, Destroy);
}

TEST_F(LoaderLifecycleConformance, DuplicatePluginIdIsRejected) {
  // Two distinct files sharing one plugin ID must conflict.
  const std::string First = buildIn("first", "MinimalPlugin", {});
  const std::string Second = buildIn("second", "MinimalPlugin", {});
  ASSERT_FALSE(First.empty());
  ASSERT_FALSE(Second.empty());
  const std::string Input = trivialInput();

  const RunResult R = Env.runNeverc({"-fplugin=" + First, "-fplugin=" + Second,
                                     "--no-default-config", "-fsyntax-only",
                                     Input});
  EXPECT_NE(R.exitCode, 0) << R.err;
  EXPECT_TRUE(R.errContains("duplicate")) << R.err;
}

TEST_F(LoaderLifecycleConformance, DependencyCycleIsRejected) {
  const std::string A = buildIn(
      "a", "DependencyPlugins",
      {idDefine(DepA), depDefine(DepB), "NCF_DEP_KIND=NEVERC_DEPENDENCY_REQUIRED"});
  const std::string B = buildIn(
      "b", "DependencyPlugins",
      {idDefine(DepB), depDefine(DepA), "NCF_DEP_KIND=NEVERC_DEPENDENCY_REQUIRED"});
  ASSERT_FALSE(A.empty());
  ASSERT_FALSE(B.empty());
  const std::string Input = trivialInput();

  const RunResult R =
      Env.runNeverc({"-fplugin=" + A, "-fplugin=" + B, "--no-default-config",
                     "-fsyntax-only", Input});
  EXPECT_NE(R.exitCode, 0) << R.err;
  EXPECT_TRUE(R.errContains("cycle")) << R.err;
}

TEST_F(LoaderLifecycleConformance, MissingRequiredDependencyIsRejected) {
  const std::string A = buildIn(
      "a", "DependencyPlugins",
      {idDefine(DepA), depDefine(DepB), "NCF_DEP_KIND=NEVERC_DEPENDENCY_REQUIRED"});
  ASSERT_FALSE(A.empty());
  const std::string Input = trivialInput();

  const RunResult R = Env.runNeverc(
      {"-fplugin=" + A, "--no-default-config", "-fsyntax-only", Input});
  EXPECT_NE(R.exitCode, 0) << R.err;
  EXPECT_TRUE(R.errContains("requires plugin") || R.errContains("dependenc"))
      << R.err;
}

TEST_F(LoaderLifecycleConformance, AfterDependencyOrdersRegistration) {
  // A declares it must load AFTER B; B has no dependency.
  const std::string A =
      buildIn("a", "DependencyPlugins",
              {idDefine(DepA), depDefine(DepB), "NCF_DEP_KIND=NEVERC_DEPENDENCY_AFTER"});
  const std::string B = buildIn("b", "DependencyPlugins", {idDefine(DepB)});
  ASSERT_FALSE(A.empty());
  ASSERT_FALSE(B.empty());
  const std::string Input = trivialInput();

  // Deliberately pass A before B on the command line; ordering must still hold.
  const RunResult R = Env.runNeverc(
      {"-fplugin=" + A, "-fplugin=" + B, "--no-default-config", "-c", Input,
       "-o", (std::filesystem::path(Dir) / "out.o").string()},
      {{"NEVERC_CONFORMANCE_LOG", logPath()}});
  ASSERT_EQ(R.exitCode, 0) << R.err;

  const std::string Log = readLog();
  SCOPED_TRACE(Log);
  const size_t PosB = Log.find(std::string(DepB) + ":register");
  const size_t PosA = Log.find(std::string(DepA) + ":register");
  ASSERT_NE(PosB, std::string::npos);
  ASSERT_NE(PosA, std::string::npos);
  EXPECT_LT(PosB, PosA) << "B must register before A (A depends AFTER B)";
}

} // namespace
} // namespace neverc::conformance
