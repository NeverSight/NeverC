// -fdyncode participates in the normal Action/Job/Artifact
// DAG.  The dyncode image extraction is an in-process job (DynCodeJobAction /
// DynCodeCommand) rather than a post-`main()` step over a private temp object.
#include "NeverCTestFixture.h"

class PluginDynCodeDriverTest : public NeverCTest {};

// -### shows the compile job followed by an in-process dyncode extraction job
// that writes the user's -o image directly; no separate dyncode temp file.
TEST_F(PluginDynCodeDriverTest, ExtractionIsAnInProcessDagJob) {
  const fs::path Source = tmpFile("dyncode_dag.c");
  const fs::path Image = tmpFile("dyncode_dag.bin");
  writeFile(Source, "int dyncode_entry(void) { return 7; }\n");

  CmdResult Result = ncc({"-###", "-fdyncode", "-target", "arm64-apple-macos",
                          Source.string(), "-o", Image.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;

  // The extraction job appears as an in-process command targeting the image.
  EXPECT_NE(Result.err.find("-dyncode-extract"), std::string::npos)
      << Result.err;
  EXPECT_NE(Result.err.find("(in-process)"), std::string::npos) << Result.err;
  EXPECT_NE(Result.err.find(Image.string()), std::string::npos) << Result.err;

  // Exactly one dyncode extraction job is scheduled.
  size_t Count = 0;
  for (size_t Pos = Result.err.find("-dyncode-extract");
       Pos != std::string::npos;
       Pos = Result.err.find("-dyncode-extract", Pos + 1))
    ++Count;
  EXPECT_EQ(Count, 1u) << Result.err;
}

// Multiple translation units cannot be lowered to a single raw image; the
// driver rejects them up front with a stable capability diagnostic.
TEST_F(PluginDynCodeDriverTest, MultipleInputsRejected) {
  const fs::path A = tmpFile("dyncode_multi_a.c");
  const fs::path B = tmpFile("dyncode_multi_b.c");
  const fs::path Image = tmpFile("dyncode_multi.bin");
  writeFile(A, "int dyncode_entry(void) { return 1; }\n");
  writeFile(B, "int helper(void) { return 2; }\n");

  CmdResult Result = ncc({"-fdyncode", "-target", "arm64-apple-macos",
                          A.string(), B.string(), "-o", Image.string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("exactly one translation unit"), std::string::npos)
      << Result.err;
}

// -c/-S/-E stop before the image and are incompatible with -fdyncode.
TEST_F(PluginDynCodeDriverTest, CompileOnlyRejected) {
  const fs::path Source = tmpFile("dyncode_compile_only.c");
  const fs::path Obj = tmpFile("dyncode_compile_only.o");
  writeFile(Source, "int dyncode_entry(void) { return 3; }\n");

  CmdResult Result = ncc({"-c", "-fdyncode", "-target", "arm64-apple-macos",
                          Source.string(), "-o", Obj.string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("cannot be combined with -c/-S/-E"),
            std::string::npos)
      << Result.err;
}

// Unsupported target triples are rejected with the fixed supported-triple list.
TEST_F(PluginDynCodeDriverTest, UnsupportedTripleRejected) {
  const fs::path Source = tmpFile("dyncode_bad_triple.c");
  const fs::path Image = tmpFile("dyncode_bad_triple.bin");
  writeFile(Source, "int dyncode_entry(void) { return 4; }\n");

  CmdResult Result = ncc({"-fdyncode", "-target", "riscv64-unknown-elf",
                          Source.string(), "-o", Image.string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("does not support triple"), std::string::npos)
      << Result.err;
}

// End-to-end: the in-process dyncode job actually produces a raw image, and
// -fdyncode-keep-obj tees the intermediate relocatable object.
TEST_F(PluginDynCodeDriverTest, ProducesRawImageAndKeepsObject) {
  if (!(isDarwin() && isArm64()))
    GTEST_SKIP() << "host-executed arm64 macOS extraction only";

  const fs::path Source = tmpFile("dyncode_e2e.c");
  const fs::path Image = tmpFile("dyncode_e2e.bin");
  const fs::path Kept = tmpFile("dyncode_e2e_keep.o");
  writeFile(Source, "int dyncode_entry(void) { return 42; }\n");

  CmdResult Result =
      ncc({"-fdyncode", "-target", "arm64-apple-macos",
           std::string("-fdyncode-keep-obj=") + Kept.string(), Source.string(),
           "-o", Image.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_TRUE(fs::exists(Image));
  EXPECT_GT(fileSize(Image), 0u);
  EXPECT_TRUE(fs::exists(Kept));
  EXPECT_GT(fileSize(Kept), 0u);
}

// -fdyncode-report=<path> writes the canonical audit report as a side output of
// the same in-process extraction job.  The report is the single source the
// verbose diagnostics render from; it records the per-phase provider journal and
// the summary counts.
TEST_F(PluginDynCodeDriverTest, ProducesReportJSON) {
  if (!(isDarwin() && isArm64()))
    GTEST_SKIP() << "host-executed arm64 macOS extraction only";

  const fs::path Source = tmpFile("dyncode_report.c");
  const fs::path Image = tmpFile("dyncode_report.bin");
  const fs::path Report = tmpFile("dyncode_report.json");
  writeFile(Source, "int dyncode_entry(void) { return 9; }\n");

  CmdResult Result =
      ncc({"-fdyncode", "-target", "arm64-apple-macos",
           std::string("-fdyncode-report=") + Report.string(), Source.string(),
           "-o", Image.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  ASSERT_TRUE(fs::exists(Report));
  EXPECT_GT(fileSize(Report), 0u);

  const std::string Json = readFile(Report);
  // Canonical top-level keys and the sealed final-verifier journal entry.
  EXPECT_NE(Json.find("\"journal\""), std::string::npos) << Json;
  EXPECT_NE(Json.find("\"selected_section_count\""), std::string::npos) << Json;
  EXPECT_NE(Json.find("builtin.final_verifier"), std::string::npos) << Json;
}
