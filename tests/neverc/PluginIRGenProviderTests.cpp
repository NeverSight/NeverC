#include "NeverCTestFixture.h"

class PluginIRGenProviderTest : public NeverCTest {};

TEST_F(PluginIRGenProviderTest,
       ProviderBypassesBuiltinIRGenAndPublishesExecutableModule) {
  const fs::path Source = tmpFile("irgen_provider.c");
  const fs::path IR = tmpFile("irgen_provider.ll");
  const fs::path Executable = tmpFile("irgen_provider");
  writeFile(Source,
            "int builtin_only(void) { return 7; }\n"
            "int main(void) { return builtin_only(); }\n");

  CmdResult EmitIR =
      ncc({std::string("-fplugin=") + NEVERC_TEST_IRGEN_PROVIDER_PLUGIN,
           "-std=c11", "-S", "-emit-llvm", Source.string(), "-o",
           IR.string()});
  ASSERT_EQ(EmitIR.exitCode, 0) << EmitIR.err;
  const std::string Module = readFile(IR);
  EXPECT_EQ(Module.find("builtin_only"), std::string::npos);
  EXPECT_NE(Module.find("define i32 @main()"), std::string::npos);
  EXPECT_NE(Module.find("ret i32 42"), std::string::npos);

  // Executability is the proof here; LTO and allocator injection are covered
  // by their dedicated suites. NeverC otherwise enables both for every link.
  std::vector<std::string> Arguments = {
      std::string("-fplugin=") + NEVERC_TEST_IRGEN_PROVIDER_PLUGIN,
      "-fno-lto", "-fno-builtin-mimalloc", "-std=c11", Source.string(), "-o",
      Executable.string()};
  std::vector<std::string> LinkArguments = linkFlags();
  Arguments.insert(Arguments.end(), LinkArguments.begin(), LinkArguments.end());
  CmdResult Compile = ncc(Arguments);
  ASSERT_EQ(Compile.exitCode, 0) << Compile.err;

  CmdResult Run = exec(Executable.string(), {});
  EXPECT_EQ(Run.exitCode, 42) << Run.out << Run.err;
}

// Whether `int x;` at file scope is a definition is the one thing about it
// that cannot be read off the declaration: it is one only if the unit turns
// out to hold no other definition of the name, so Sema reports the answer once
// parsing is over and nothing can change it.  A plugin makes the driver hold
// builtin IRGen back until it knows whether a provider will replace it, and
// that report has to be held with it.  Dropped instead, every tentative
// definition in the unit came out an `external` declaration -- including the
// `static` one, whose name was never meant to leave the file.
TEST_F(PluginIRGenProviderTest,
       TentativeDefinitionsSurviveDeferredBuiltinIRGen) {
  const fs::path Source = tmpFile("irgen_tentative.c");
  const fs::path IR = tmpFile("irgen_tentative.ll");
  const fs::path Executable = tmpFile("irgen_tentative");
  writeFile(Source, "int plain;\n"
                    "static int local;\n"
                    "_Thread_local int threaded;\n"
                    "int main(void) { return plain + local + threaded; }\n");

  // A plugin that only observes, so builtin IRGen is deferred and then is the
  // one that runs.
  const std::string Plugin =
      std::string("-fplugin=") + NEVERC_TEST_TASK_LIFECYCLE_PLUGIN;
  CmdResult EmitIR = ncc({Plugin, "-std=c11", "-S", "-emit-llvm",
                          Source.string(), "-o", IR.string()});
  ASSERT_EQ(EmitIR.exitCode, 0) << EmitIR.err;

  const std::string Module = readFile(IR);
  for (const char *Name : {"@plain =", "@local =", "@threaded ="}) {
    const size_t At = Module.find(Name);
    ASSERT_NE(At, std::string::npos) << Name << " is missing from\n" << Module;
    const std::string Line = Module.substr(At, Module.find('\n', At) - At);
    EXPECT_EQ(Line.find("external"), std::string::npos)
        << "a tentative definition reached the module as a declaration, so "
           "the variable has no storage anywhere: "
        << Line;
  }

  // And the whole way through a native link: a declaration where a definition
  // belongs is an undefined symbol. LTO and the allocator are unrelated to
  // that storage contract.
  std::vector<std::string> Arguments = {
      Plugin, "-fno-lto", "-fno-builtin-mimalloc", "-std=c11", Source.string(),
      "-o", Executable.string()};
  std::vector<std::string> LinkArguments = linkFlags();
  Arguments.insert(Arguments.end(), LinkArguments.begin(), LinkArguments.end());
  CmdResult Compile = ncc(Arguments);
  ASSERT_EQ(Compile.exitCode, 0) << Compile.err;
  EXPECT_EQ(exec(Executable.string(), {}).exitCode, 0);
}

TEST_F(PluginIRGenProviderTest,
       ProviderAcceptsTheCustomSemanticProductItImplements) {
  const fs::path Source = tmpFile("irgen_custom_semantic_product.c");
  const fs::path IR = tmpFile("irgen_custom_semantic_product.ll");
  writeFile(Source, "this source is replaced by plugin providers\n");

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_PARSER_PROVIDER_BINARY_PLUGIN,
       std::string("-fplugin=") + NEVERC_TEST_SEMA_PROVIDER_CUSTOM_PLUGIN,
       std::string("-fplugin=") + NEVERC_TEST_IRGEN_PROVIDER_PLUGIN,
       "-std=c11", "-S", "-emit-llvm", Source.string(), "-o", IR.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_NE(readFile(IR).find("ret i32 42"), std::string::npos);
}
