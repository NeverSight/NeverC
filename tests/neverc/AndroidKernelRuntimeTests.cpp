#include "NeverCTestFixture.h"

class AndroidKernelRuntimeTest : public NeverCTest {};

TEST_F(AndroidKernelRuntimeTest, EmbeddedRuntimeLinkage) {
  if (isWindows())
    GTEST_SKIP() << "NVK runtime linkage test requires a POSIX shell";

  const fs::path Script =
      testDir() / "../../runtime/android/kernel/tools/test-runtime-linkage.sh";
  ASSERT_TRUE(fs::exists(Script)) << Script;

  const CmdResult Result =
      exec("bash", {Script.string(), neverc().string(), "--smoke"});
  EXPECT_EQ(Result.exitCode, 0) << Result.out << Result.err;
}

TEST_F(AndroidKernelRuntimeTest, PublicSdkLayouts) {
  if (isWindows())
    GTEST_SKIP() << "NVK SDK layout test requires a POSIX shell";

  const fs::path Script =
      testDir() / "../../runtime/android/kernel/tools/test-sdk-layouts.sh";
  ASSERT_TRUE(fs::exists(Script)) << Script;

  const CmdResult Result = exec("sh", {Script.string(), neverc().string()});
  EXPECT_EQ(Result.exitCode, 0) << Result.out << Result.err;
}
