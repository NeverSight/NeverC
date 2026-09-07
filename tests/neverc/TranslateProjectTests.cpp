#include "../../neverc/lib/Translate/ArtifactWriter.h"
#include "../../neverc/lib/Translate/JSON.h"
#include "NeverCTestFixture.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstdlib>

namespace {
using neverc::translate::jsonString;
class TranslateProjectTest : public NeverCTest {
protected:
  std::string frontend() {
    const char *Value = std::getenv("NEVERC_CPP_FRONTEND");
    return Value ? Value : "";
  }
  std::string referenceCompiler() {
    const char *Value = std::getenv("NEVERC_CPP_REFERENCE_COMPILER");
    return Value ? Value : "";
  }
  std::string target() {
    auto Result = ncc({"-dumpmachine"});
    EXPECT_EQ(Result.exitCode, 0) << Result.err;
    while (!Result.out.empty() &&
           (Result.out.back() == '\n' || Result.out.back() == '\r'))
      Result.out.pop_back();
    return Result.out;
  }
  fs::path fixture(const std::string &Name) {
    return testDir() / "Inputs" / "translate" / "cpp" / "project" / Name;
  }
  fs::path project(const std::string &Name, bool Main = false) {
    const auto Root = tmpFile(Name);
    fs::create_directories(Root / "src");
    fs::create_directory(Root / "include");
    fs::create_directory(Root / "build");
    for (const auto *File : {"first.cpp", "second.cpp", "main.cpp"})
      writeFile(Root / "src" / File, readFile(fixture(File)));
    writeFile(Root / "include" / "compute.hpp",
              readFile(fixture("compute.hpp")));
    database(Root, Main);
    return Root;
  }
  void database(const fs::path &Root, bool Main = false,
                bool Duplicate = false) {
    llvm::json::Array Entries;
    std::vector<std::string> Names{"first.cpp", "second.cpp"};
    if (Main)
      Names.push_back("main.cpp");
    if (Duplicate)
      Names.push_back("first.cpp");
    for (const auto &Name : Names)
      Entries.push_back(llvm::json::Object{
          {"directory", jsonString((Root / "build").string())},
          {"file", jsonString("../src/" + Name)},
          {"arguments",
           llvm::json::Array{"clang++", "-std=c++17", "-I../include", "-O2",
                             "-c", jsonString("../src/" + Name), "-o",
                             jsonString(Name + ".o")}}});
    std::string Text;
    llvm::raw_string_ostream OS(Text);
    OS << llvm::formatv("{0:2}", llvm::json::Value(std::move(Entries)));
    writeFile(Root / "build" / "compile_commands.json", Text);
  }
  std::vector<std::string> args(const fs::path &Root, bool Main = false) {
    std::vector<std::string> Args{
        "translate",
        "--from",
        "cpp",
        "--profile",
        "cpp-project-v1",
        "--project-root",
        Root.string(),
        "--compdb",
        (Root / "build" / "compile_commands.json").string(),
        "--frontend",
        frontend(),
        "--target",
        target(),
        (Root / "src" / "first.cpp").string(),
        (Root / "src" / "second.cpp").string()};
    if (Main)
      Args.push_back((Root / "src" / "main.cpp").string());
    return Args;
  }
  CmdResult translate(const fs::path &Root, const fs::path &Output,
                      bool Main = false,
                      const std::vector<std::string> &Extra = {}) {
    auto Args = args(Root, Main);
    Args.insert(Args.end(), {"--out-dir", Output.string()});
    Args.insert(Args.end(), Extra.begin(), Extra.end());
    return ncc(Args);
  }
  CmdResult compile(const fs::path &Input, const fs::path &Output,
                    const std::string &Optimization,
                    const std::vector<std::string> &Extra = {}) {
    std::vector<std::string> Args{Input.string(), Optimization,
                                  "-fno-lto",     "-fno-builtin-mimalloc",
                                  "-o",           Output.string()};
    Args.insert(Args.end(), Extra.begin(), Extra.end());
    return ncc(Args);
  }
  void rejects(const CmdResult &Result, const fs::path &Output,
               const std::string &Code) {
    EXPECT_NE(Result.exitCode, 0) << Result.out << Result.err;
    EXPECT_TRUE(Result.stderrContains(Code)) << Result.err;
    EXPECT_FALSE(fs::exists(Output) || fs::is_symlink(Output));
  }
};

TEST_F(TranslateProjectTest, ProgramAndSharedHeaderCompileAtBothOptimizations) {
  if (frontend().empty() || referenceCompiler().empty())
    GTEST_SKIP();
  const auto Root = project("program", true), Output = Root / "generated";
  auto Translation = translate(Root, Output, true);
  ASSERT_EQ(Translation.exitCode, 0) << Translation.err;
  for (const auto *Name : {"translated.nc", "translated.h",
                           "translate.map.json", "translate-manifest.json"}) {
    ASSERT_TRUE(fs::is_regular_file(Output / Name)) << Name;
    EXPECT_FALSE(fs::is_symlink(Output / Name));
  }
  auto Header = ncc({"-x", "c", "-std=c23", (Output / "translated.h").string(),
                     "-fsyntax-only"});
  ASSERT_EQ(Header.exitCode, 0) << Header.err;
  const auto Target = target();
  for (const std::string Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Reference = tmpFile("program-reference" + Optimization);
    auto ReferenceBuild =
        exec(referenceCompiler(),
             {"--target=" + Target, "-std=c++17", Optimization,
              "-I" + (Root / "include").string(),
              (Root / "src" / "first.cpp").string(),
              (Root / "src" / "second.cpp").string(),
              (Root / "src" / "main.cpp").string(), "-o", Reference.string()});
    ASSERT_EQ(ReferenceBuild.exitCode, 0) << ReferenceBuild.err;
    const auto Generated = tmpFile("program-generated" + Optimization);
    auto GeneratedBuild =
        compile(Output / "translated.nc", Generated, Optimization);
    ASSERT_EQ(GeneratedBuild.exitCode, 0) << GeneratedBuild.err;
    EXPECT_EQ(exec(Reference.string(), {}).exitCode, 0);
    EXPECT_EQ(exec(Generated.string(), {}).exitCode, 0);
  }
}

TEST_F(TranslateProjectTest, LibraryMatchesSeparateCppObjectsAndCClient) {
  if (frontend().empty() || referenceCompiler().empty())
    GTEST_SKIP();
  const auto Root = project("library"), Output = Root / "generated";
  auto Translation = translate(Root, Output);
  ASSERT_EQ(Translation.exitCode, 0) << Translation.err;
  const auto Target = target();
  for (const std::string Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    std::vector<std::string> Objects;
    for (const auto *Name : {"first.cpp", "second.cpp"}) {
      const auto Object = tmpFile(std::string(Name) + Optimization + ".o");
      auto Build =
          exec(referenceCompiler(),
               {"--target=" + Target, "-std=c++17", Optimization,
                "-I" + (Root / "include").string(), "-c",
                (Root / "src" / Name).string(), "-o", Object.string()});
      ASSERT_EQ(Build.exitCode, 0) << Build.err;
      Objects.push_back(Object.string());
    }
    const auto Reference = tmpFile("library-reference" + Optimization);
    auto ReferenceLink =
        compile(fixture("harness.c"), Reference, Optimization, Objects);
    ASSERT_EQ(ReferenceLink.exitCode, 0) << ReferenceLink.err;
    const auto GeneratedObject =
        tmpFile("library-generated" + Optimization + ".o");
    auto GeneratedBuild = compile(Output / "translated.nc", GeneratedObject,
                                  Optimization, {"-c"});
    ASSERT_EQ(GeneratedBuild.exitCode, 0) << GeneratedBuild.err;
    const auto Client = tmpFile("client.c");
    writeFile(Client,
              "#include \"translated.h\"\n" + readFile(fixture("harness.c")));
    const auto Generated = tmpFile("library-generated" + Optimization);
    auto GeneratedLink =
        compile(Client, Generated, Optimization,
                {"-I" + Output.string(), GeneratedObject.string()});
    ASSERT_EQ(GeneratedLink.exitCode, 0) << GeneratedLink.err;
    auto ReferenceRun = exec(Reference.string(), {}),
         GeneratedRun = exec(Generated.string(), {});
    ASSERT_EQ(ReferenceRun.exitCode, 0) << ReferenceRun.err;
    ASSERT_EQ(GeneratedRun.exitCode, 0) << GeneratedRun.err;
    ASSERT_EQ(
        std::count(ReferenceRun.out.begin(), ReferenceRun.out.end(), '\n'),
        512);
    EXPECT_EQ(GeneratedRun.out, ReferenceRun.out);
  }
}

TEST_F(TranslateProjectTest,
       AmbiguityRequiresExplicitSelectionAndCheckPublishesOnlyReport) {
  if (frontend().empty())
    GTEST_SKIP();
  const auto Root = project("configuration"), Output = Root / "generated";
  database(Root, false, true);
  rejects(translate(Root, Output), Output, "TR0602");
  auto Args = args(Root);
  const auto Report = Root / "report.json";
  Args.insert(Args.end(),
              {"--check", "--report", Report.string(), "--compdb-entry",
               (Root / "src" / "first.cpp").string() + "=2"});
  auto Result = ncc(Args);
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_FALSE(fs::exists(Output));
  ASSERT_TRUE(fs::is_regular_file(Report));
  EXPECT_NE(readFile(Report).find("cpp-project-v1"), std::string::npos);
  EXPECT_NE(readFile(Report).find("success"), std::string::npos);
}

TEST_F(TranslateProjectTest, MissingOwnedHeaderAndForeignCallNeverPublish) {
  if (frontend().empty())
    GTEST_SKIP();
  const auto Root = project("dependencies"), Output = Root / "generated";
  fs::remove(Root / "include" / "compute.hpp");
  rejects(translate(Root, Output), Output, "TR0203");
  writeFile(Root / "include" / "compute.hpp", readFile(fixture("compute.hpp")));
  writeFile(Root / "src" / "second.cpp",
            "#include \"compute.hpp\"\nint missing(int);\n"
            "extern \"C\" unsigned translate_project(unsigned value) { return "
            "(unsigned)missing((int)value); }\n");
  rejects(translate(Root, Output), Output, "TR0203");
}

TEST_F(TranslateProjectTest, AllOwnedHeaderDeclarationsAreChecked) {
  if (frontend().empty())
    GTEST_SKIP();
  const auto Root = project("unsupported-header"), Output = Root / "generated";
  writeFile(Root / "include" / "compute.hpp",
            readFile(fixture("compute.hpp")) +
                "\ntemplate<class T> T unused(T value) { return value; }\n");
  rejects(translate(Root, Output), Output, "TR0201");
}

TEST_F(TranslateProjectTest, RelocationPreservesSourceMapsAndSemanticContext) {
  if (frontend().empty())
    GTEST_SKIP();
  const auto First = project("first project"),
             Second = project("second project");
  for (const auto &Root : {First, Second}) {
    auto Result = translate(Root, Root / "generated");
    ASSERT_EQ(Result.exitCode, 0) << Result.err;
  }
  for (const auto *Name :
       {"translated.nc", "translated.h", "translate.map.json"}) {
    ASSERT_TRUE(fs::is_regular_file(First / "generated" / Name));
    ASSERT_TRUE(fs::is_regular_file(Second / "generated" / Name));
    EXPECT_EQ(readFile(First / "generated" / Name),
              readFile(Second / "generated" / Name));
  }
  std::vector<std::string> Contexts[2];
  unsigned Index = 0;
  for (const auto &Root : {First, Second}) {
    auto Value = llvm::json::parse(
        readFile(Root / "generated" / "translate-manifest.json"));
    ASSERT_TRUE(static_cast<bool>(Value))
        << llvm::toString(Value.takeError()).str().str();
    const auto *Manifest = Value->getAsObject();
    ASSERT_NE(Manifest, nullptr);
    const auto *Database = Manifest->getObject("compilation_database");
    ASSERT_NE(Database, nullptr);
    EXPECT_TRUE(Database->getString("sha256") ==
                neverc::translate::sha256(
                    readFile(Root / "build" / "compile_commands.json")));
    const auto *Units = Manifest->getArray("translation_units");
    ASSERT_NE(Units, nullptr);
    ASSERT_EQ(Units->size(), 2u);
    for (const auto &Unit : *Units) {
      ASSERT_NE(Unit.getAsObject(), nullptr);
      Contexts[Index].push_back(
          Unit.getAsObject()->getString("configuration_id").str());
    }
    ++Index;
  }
  EXPECT_EQ(Contexts[0], Contexts[1]);
}
} // namespace
