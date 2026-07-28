#include "NeverCTestFixture.h"

class PluginSDKExampleTest : public NeverCTest {
protected:
  static std::string pluginLeaf(const std::string &Stem) {
    if (isWindows())
      return Stem + ".dll";
    if (isDarwin())
      return Stem + ".dylib";
    return Stem + ".so";
  }

  fs::path buildSDKExample(const std::string &Name) {
    const fs::path Source =
        fs::path(NEVERC_PLUGINSDK_DIR) / "examples" / (Name + ".c");
    const fs::path Plugin = tmpFile(pluginLeaf(Name));
    CmdResult Result =
        ncc({"--no-default-config", "-shared",
             "-I" + std::string(NEVERC_NEVERC_INCLUDE_DIR), "-o",
             Plugin.string(), Source.string()});
    EXPECT_EQ(Result.exitCode, 0) << Name << " build failed:\n" << Result.err;
    return Plugin;
  }
};

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

TEST_F(PluginSDKExampleTest, VirtualHeaderSuppliesDeterministicInclude) {
  const fs::path Plugin = buildSDKExample("VirtualHeaderPlugin");
  ASSERT_TRUE(fs::exists(Plugin));

  const fs::path Source = tmpFile("virtual_header_example.c");
  writeFile(Source,
            "#include <neverc-example/virtual.h>\n"
            "_Static_assert(NEVERC_VIRTUAL_ANSWER == 42, \"bad header\");\n");

  CmdResult Result =
      ncc({std::string("-fplugin=") + Plugin.string(), "--no-default-config",
           "-fsyntax-only", Source.string()});
  EXPECT_EQ(Result.exitCode, 0) << Result.err;
}

TEST_F(PluginSDKExampleTest, ASTRewriteAtomicallyReplacesInitializer) {
  const fs::path Plugin = buildSDKExample("ASTRewritePlugin");
  ASSERT_TRUE(fs::exists(Plugin));

  const fs::path Source = tmpFile("ast_rewrite_example.c");
  writeFile(Source,
            "int neverc_rewrite_target = 1;\n"
            "int read_rewrite_target(void) { return neverc_rewrite_target; }\n");

  CmdResult Result =
      ncc({std::string("-fplugin=") + Plugin.string(), "--no-default-config",
           "-fsyntax-only", Source.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_NE(Result.err.find("[plugin-4200]"), std::string::npos) << Result.err;
  EXPECT_NE(Result.err.find("rewrote neverc_rewrite_target initializer to 42"),
            std::string::npos)
      << Result.err;
}

TEST_F(PluginSDKExampleTest, FirstReleaseIRExamplesBuildLoadAndRun) {
  const fs::path Source = tmpFile("first_release_examples.c");
  writeFile(Source, "int first_release_example(void) { return 42; }\n");

  struct Example {
    const char *Name;
    const char *ExpectedDiagnostic;
  };
  const Example Examples[] = {
      {"ExamplePlugin", "ExamplePlugin observed the module function list"},
      {"CrtShimPlugin", "CrtShimPlugin queried IR without C runtime calls"},
      {"BenchPlugin", "BenchPlugin completed 10000 typed IR table calls"},
      {"CustomCallConvPlugin", nullptr},
  };

  for (const Example &Entry : Examples) {
    SCOPED_TRACE(Entry.Name);
    const fs::path Plugin = buildSDKExample(Entry.Name);
    ASSERT_TRUE(fs::exists(Plugin));
    const fs::path IR = tmpFile(std::string(Entry.Name) + ".ll");
    CmdResult Result =
        ncc({std::string("-fplugin=") + Plugin.string(), "--no-default-config",
             "-S", "-emit-llvm", Source.string(), "-o", IR.string()});
    ASSERT_EQ(Result.exitCode, 0) << Result.err;
    EXPECT_TRUE(fs::exists(IR));
    if (Entry.ExpectedDiagnostic)
      EXPECT_NE(Result.err.find(Entry.ExpectedDiagnostic), std::string::npos)
          << Result.err;
  }
}

TEST_F(PluginSDKExampleTest, StableIRAndMIRPassExamplesRun) {
  const fs::path Source = tmpFile("stable_pass_examples.c");
  writeFile(Source, "int stable_pass_example(int x) { return x + 1; }\n");
  for (const char *Name : {"FunctionPass", "MachinePass"}) {
    SCOPED_TRACE(Name);
    const fs::path Plugin = buildSDKExample(Name);
    const fs::path Object = tmpFile(std::string(Name) + ".o");
    CmdResult Result =
        ncc({std::string("-fplugin=") + Plugin.string(),
             "--no-default-config", "-O2", "-fno-lto", "-c",
             Source.string(), "-o", Object.string()});
    ASSERT_EQ(Result.exitCode, 0) << Result.err;
    EXPECT_TRUE(fs::exists(Object));
  }
}

TEST_F(PluginSDKExampleTest, MCObserverExampleRunsDuringObjectEmission) {
  const fs::path Plugin = buildSDKExample("MCObserverPlugin");
  ASSERT_TRUE(fs::exists(Plugin));
  const fs::path Source = tmpFile("mc_observer_example.c");
  const fs::path Object = tmpFile("mc_observer_example.o");
  writeFile(Source, "int mc_observer_example(void) { return 42; }\n");

  CmdResult Result =
      ncc({std::string("-fplugin=") + Plugin.string(), "--no-default-config",
           "-fno-lto", "-c", Source.string(), "-o", Object.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_TRUE(fs::exists(Object));
  EXPECT_NE(Result.err.find("MCObserverPlugin observed an instruction"),
            std::string::npos)
      << Result.err;
}

TEST_F(PluginSDKExampleTest,
       ObjectRewriteExampleAddsSectionThroughTransactionalPipeline) {
  const fs::path Plugin = buildSDKExample("ObjectRewritePlugin");
  ASSERT_TRUE(fs::exists(Plugin));
  const fs::path Source = tmpFile("object_rewrite_example.c");
  const fs::path Object = tmpFile("object_rewrite_example.o");
  writeFile(Source, "int object_rewrite_example(void) { return 42; }\n");

  // -fno-builtin-mimalloc: the example plugin rewrites the object it is given
  // and expects the one function above, not the several hundred the default
  // allocator would add.
  CmdResult Result =
      ncc({std::string("-fplugin=") + Plugin.string(), "--no-default-config",
           "-fno-lto", "-fno-builtin-mimalloc", "-c", Source.string(), "-o",
           Object.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  ASSERT_TRUE(fs::exists(Object));
  EXPECT_NE(readFile(Object).find("NeverC object rewrite example"),
            std::string::npos);
}
