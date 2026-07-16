#include "NeverCTestFixture.h"

class PluginSDKExampleTest : public NeverCTest {};

TEST_F(PluginSDKExampleTest,
       DriverTraceUsesOptionStateObserverAndSingleNextInterceptor) {
  const fs::path Source = tmpFile("driver_trace_example.c");
  const fs::path Object = tmpFile("driver_trace_example.o");
  writeFile(Source, "int driver_trace_example(void) { return 42; }\n");

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_DRIVER_TRACE_EXAMPLE_PLUGIN,
       "--driver-trace", "-c", Source.string(), "-o", Object.string()});

  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_TRUE(fs::exists(Object));
  EXPECT_NE(Result.err.find("[plugin-1001]"), std::string::npos)
      << Result.err;
  EXPECT_NE(Result.err.find("[plugin-1002]"), std::string::npos)
      << Result.err;
}
