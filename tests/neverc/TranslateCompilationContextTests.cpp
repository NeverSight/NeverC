#include "../../neverc/lib/Translate/Cpp/CompilationContext.h"
#include "../../neverc/lib/Translate/JSON.h"
#include "NeverCTestFixture.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>

namespace {
using namespace neverc::translate;
using llvm::json::Array;
using llvm::json::Object;

class TranslateCompilationContextTest : public NeverCTest {
protected:
  fs::path Root, Build, Source, Database;
  ProjectContextOptions Options;
  ProjectContext Result;
  Diagnostics Errors;

  void SetUp() override {
    NeverCTest::SetUp();
    Root = tmpFile("project");
    Build = Root / "build";
    Source = Root / "src" / "a.cpp";
    Database = Build / "compile_commands.json";
    fs::create_directories(Build);
    fs::create_directories(Source.parent_path());
    fs::create_directories(Root / "include one");
    fs::create_directories(Root / "include two");
    writeFile(Source, "int f() { return 7; }\n");
    Options.Database = Database.string();
    Options.ProjectRoot = Root.string();
    Options.TargetTriple = "x86_64-unknown-linux-gnu";
    Options.Selections = {{Source.string(), std::nullopt}};
  }

  Object entry(std::vector<std::string> Args = {"clang++", "-c",
                                                "../src/a.cpp"},
               std::string File = "../src/a.cpp", std::string Directory = "") {
    Array A;
    for (const auto &Arg : Args)
      A.push_back(jsonString(Arg));
    return Object{{"directory",
                   jsonString(Directory.empty() ? Build.string() : Directory)},
                  {"file", jsonString(File)},
                  {"arguments", std::move(A)}};
  }

  void database(std::initializer_list<llvm::json::Value> Entries) {
    std::string Text;
    llvm::raw_string_ostream OS(Text);
    OS << llvm::json::Value(Array(Entries));
    writeFile(Database, Text);
  }

  bool parse() {
    Errors.clear();
    return parseProjectContext(Options, Result, Errors);
  }

  void rejected(const std::string &Code) {
    ASSERT_FALSE(parse());
    ASSERT_FALSE(Errors.empty());
    EXPECT_EQ(Errors.front().Code, Code) << Errors.front().Reason;
    EXPECT_TRUE(Result.Units.empty());
    EXPECT_TRUE(Result.Database.AbsolutePath.empty());
    EXPECT_FALSE(Errors.front().Construct.empty());
    EXPECT_FALSE(Errors.front().Guidance.empty());
    EXPECT_EQ(Errors.front().Location.Line, 1u);
    EXPECT_EQ(Errors.front().Location.Column, 1u);
  }
};

TEST_F(TranslateCompilationContextTest,
       StructuredArgumentsPreserveSemanticOrder) {
  database({entry({"/unexecuted/tool/clang++-20",
                   "-D",
                   "VALUE=1",
                   "-UVALUE",
                   "-DVALUE=2",
                   "-I../include one",
                   "-iquote",
                   "../include two",
                   "-isystem",
                   "../include one",
                   "-std=c++17",
                   "-O2",
                   "-Wall",
                   "-Wextra",
                   "-m64",
                   "--target=x86_64-unknown-linux-gnu",
                   "-c",
                   "../src/a.cpp",
                   "-o",
                   "a.o",
                   "-MMD",
                   "-MF",
                   "a.d",
                   "-MTa.o",
                   "-MQ",
                   "a target",
                   "-MP"})});
  ASSERT_TRUE(parse());
  ASSERT_EQ(Result.Units.size(), 1u);
  const auto &Unit = Result.Units.front();
  EXPECT_EQ(Unit.SourceRelative, "src/a.cpp");
  EXPECT_EQ(Unit.WorkingDirectoryRelative, "build");
  EXPECT_EQ(Unit.CompilerExecutable, "/unexecuted/tool/clang++-20");
  EXPECT_EQ(Unit.OriginalOutput, "a.o");
  EXPECT_EQ(Unit.NormalizedArguments,
            (std::vector<std::string>{"-std=c++17", "-DVALUE=1", "-UVALUE",
                                      "-DVALUE=2", "-I", "$PROJECT/include one",
                                      "-iquote", "$PROJECT/include two",
                                      "-isystem", "$PROJECT/include one", "-O2",
                                      "-Wall", "-Wextra"}));
  EXPECT_EQ(Unit.FrontendArguments[5],
            fs::canonical(Root / "include one").string());
  EXPECT_EQ(Result.Database.RelativePath, "build/compile_commands.json");
  EXPECT_EQ(Result.Database.SHA256.size(), 64u);
  EXPECT_EQ(Unit.ConfigurationID.size(), 64u);
  EXPECT_TRUE(verifyProjectContextInputs(Result, Errors));
}

TEST_F(TranslateCompilationContextTest,
       ArgumentsOverrideAnUnusedCommandWithoutExecution) {
  auto E = entry();
  const auto Marker = Root / "must-not-exist";
  E["command"] = jsonString("touch " + Marker.string());
  database({std::move(E)});
  ASSERT_TRUE(parse());
  EXPECT_FALSE(fs::exists(Marker));
}

TEST_F(TranslateCompilationContextTest,
       GnuAndWindowsCommandQuotingProduceEquivalentMacros) {
  const auto QuotedSource = Source.parent_path() / "空 格.cpp";
  writeFile(QuotedSource, "int f() { return 1; }\n");
  Options.Selections = {{QuotedSource.string(), std::nullopt}};
  auto CommandEntry = [&](const std::string &Command) {
    return Object{{"directory", jsonString(Build.string())},
                  {"file", jsonString("../src/空 格.cpp")},
                  {"command", jsonString(Command)}};
  };
  Options.Quoting = CommandQuoting::GNU;
  database({CommandEntry("'tool dir/clang++' '-DVALUE=hello world' "
                         "-I'../include one' -c '../src/空 格.cpp'")});
  ASSERT_TRUE(parse());
  const auto GNUArguments = Result.Units.front().NormalizedArguments;
  const auto GNUHash = Result.Units.front().ConfigurationID;
  Options.Quoting = CommandQuoting::Windows;
  database({CommandEntry("\"tool dir/clang++\" \"-DVALUE=hello world\" "
                         "-I\"../include one\" -c \"../src/空 格.cpp\"")});
  ASSERT_TRUE(parse());
  EXPECT_EQ(Result.Units.front().NormalizedArguments, GNUArguments);
  EXPECT_EQ(Result.Units.front().ConfigurationID, GNUHash);
}

TEST_F(TranslateCompilationContextTest,
       AmbiguousEntriesRequireAnExplicitMatchingIndex) {
  auto A = entry(), B = entry({"clang++", "-DVALUE=2", "../src/a.cpp"});
  A["output"] = jsonString("a-debug.o");
  B["output"] = jsonString("a-release.o");
  database({std::move(A), std::move(B)});
  rejected("TR0602");
  EXPECT_NE(Errors.front().Reason.find("index 0"), std::string::npos);
  EXPECT_NE(Errors.front().Reason.find("a-release.o"), std::string::npos);
  Options.Selections.front().EntryIndex = 1;
  ASSERT_TRUE(parse());
  EXPECT_EQ(Result.Units.front().EntryIndex, 1u);
  EXPECT_EQ(Result.Units.front().NormalizedArguments.back(), "-DVALUE=2");
  Options.Selections.front().EntryIndex = 3;
  rejected("TR0602");
}

TEST_F(TranslateCompilationContextTest,
       WrongSourceIndexAndDuplicateSelectionReturnNoPartialJobs) {
  const auto B = Source.parent_path() / "b.cpp";
  writeFile(B, "int b() { return 2; }\n");
  database({entry(), entry({"clang++", "../src/b.cpp"}, "../src/b.cpp")});
  Options.Selections.front().EntryIndex = 1;
  rejected("TR0602");
  Options.Selections = {{Source.string(), std::nullopt},
                        {Source.string(), std::nullopt}};
  rejected("TR0602");
  Options.Selections = {{Source.string(), std::nullopt}, {B.string(), 0}};
  rejected("TR0602");
  Options.Selections.clear();
  rejected("TR0602");
}

TEST_F(TranslateCompilationContextTest,
       MissingEntryAndSourceHaveDistinctDiagnostics) {
  database({entry()});
  const auto Missing = Source.parent_path() / "missing.cpp";
  Options.Selections = {{Missing.string(), std::nullopt}};
  rejected("TR0203");
  writeFile(Missing, "int other() { return 0; }\n");
  rejected("TR0602");
}

TEST_F(TranslateCompilationContextTest,
       UnselectedMissingGeneratedInputsAndResponsesAreNotExpanded) {
  database({entry(), entry({"clang++", "@missing.rsp", "../src/generated.cpp"},
                           "../src/generated.cpp")});
  ASSERT_TRUE(parse());
  EXPECT_EQ(Result.Units.size(), 1u);
  EXPECT_TRUE(Result.Units.front().ResponseFiles.empty());
}

TEST_F(TranslateCompilationContextTest,
       NestedResponseFilesUseOriginalCompilationDirectory) {
  fs::create_directories(Build / "sub");
  auto CheckMode = [&](CommandQuoting Quoting, const char *Flags,
                       const char *Label) {
    SCOPED_TRACE(Label);
    Options.Quoting = Quoting;
    Options.ExtraSourceArguments.clear();
    writeFile(Build / "inner.rsp", Flags);
    writeFile(Build / "sub" / "inner.rsp", "-DWRONG_DIRECTORY=1\n");
    writeFile(Build / "sub" / "outer.rsp", "@inner.rsp -O2\n");
    database({entry({"clang++", "@sub/outer.rsp", "-c", "../src/a.cpp"})});
    ASSERT_TRUE(parse());
    const auto &Unit = Result.Units.front();
    ASSERT_EQ(Unit.ResponseFiles.size(), 2u);
    EXPECT_EQ(Unit.ResponseFiles[0].RelativePath, "build/inner.rsp");
    EXPECT_EQ(Unit.ResponseFiles[1].RelativePath, "build/sub/outer.rsp");
    EXPECT_EQ(Unit.NormalizedArguments,
              (std::vector<std::string>{"-std=c++17", "-DVALUE=hello world", "-I",
                                        "$PROJECT/include one", "-O2"}));
    EXPECT_TRUE(verifyProjectContextInputs(Result, Errors));
    writeFile(Build / "inner.rsp", "-DVALUE=changed\n");
    EXPECT_FALSE(verifyProjectContextInputs(Result, Errors));
    ASSERT_FALSE(Errors.empty());
    EXPECT_EQ(Errors.back().Code, "TR0603");
  };
  EXPECT_NO_FATAL_FAILURE(CheckMode(
      CommandQuoting::GNU, "'-DVALUE=hello world' -I'../include one'\n", "GNU"));
  EXPECT_NO_FATAL_FAILURE(CheckMode(
      CommandQuoting::Windows, "\"-DVALUE=hello world\" -I\"../include one\"\n",
      "Windows"));
}

TEST_F(TranslateCompilationContextTest,
       ResponseCyclesMissingFilesAndEncodingFailBeforeJobs) {
  database({entry({"clang++", "@one.rsp", "../src/a.cpp"})});
  rejected("TR0603");
  writeFile(Build / "one.rsp", "@two.rsp\n");
  writeFile(Build / "two.rsp", "@one.rsp\n");
  rejected("TR0603");
  EXPECT_NE(Errors.front().Reason.find("cycle"), std::string::npos);
  writeFile(Build / "one.rsp", std::string("-DVALUE=x\0y", 11));
  rejected("TR0603");
  writeFile(Build / "one.rsp", std::string(1, char(0xff)));
  rejected("TR0603");
}

TEST_F(TranslateCompilationContextTest,
       ResponseDepthAllowsTheBoundaryAndRejectsTheNextLevel) {
  for (std::size_t I = 0; I < MaxResponseFileDepth; ++I)
    writeFile(Build / (std::to_string(I) + ".rsp"),
              I + 1 == MaxResponseFileDepth
                  ? "-DVALUE=1"
                  : "@" + std::to_string(I + 1) + ".rsp");
  database({entry({"clang++", "@0.rsp", "../src/a.cpp"})});
  ASSERT_TRUE(parse());
  EXPECT_EQ(Result.Units.front().ResponseFiles.size(), MaxResponseFileDepth);
  writeFile(Build / (std::to_string(MaxResponseFileDepth - 1) + ".rsp"),
            "@extra.rsp");
  writeFile(Build / "extra.rsp", "-DVALUE=2");
  rejected("TR0603");
  EXPECT_NE(Errors.front().Reason.find("nesting limit"), std::string::npos);
}

TEST_F(TranslateCompilationContextTest,
       ResponseCumulativeByteLimitCountsRepeatedExpansions) {
  writeFile(Build / "large.rsp", std::string(MaxResponseFileBytes / 2, ' '));
  database({entry({"clang++", "@large.rsp", "@large.rsp", "../src/a.cpp"})});
  ASSERT_TRUE(parse());
  EXPECT_EQ(Result.Units.front().ResponseFiles.size(), 1u);
  database({entry(
      {"clang++", "@large.rsp", "@large.rsp", "@large.rsp", "../src/a.cpp"})});
  rejected("TR0603");
}

TEST_F(TranslateCompilationContextTest,
       ExpandedArgumentCountHasAnEnforcedBoundary) {
  std::vector<std::string> Args(MaxCompilationArguments - 2, "-Wall");
  Args.insert(Args.begin(), "clang++");
  Args.push_back("../src/a.cpp");
  database({entry(Args)});
  ASSERT_TRUE(parse());
  Args.push_back("-Wall");
  database({entry(Args)});
  rejected("TR0601");
  writeFile(Build / "many.rsp", std::string());
  std::string Text;
  for (std::size_t I = 0; I < MaxCompilationArguments; ++I)
    Text += "-Wall ";
  writeFile(Build / "many.rsp", Text);
  database({entry({"clang++", "@many.rsp", "../src/a.cpp"})});
  rejected("TR0603");
}

TEST_F(TranslateCompilationContextTest,
       DatabaseShapeEncodingAndNestingAreBounded) {
  for (const std::string &Bad :
       {std::string("{}"), std::string("[{}]"),
        std::string("[{\"directory\":\"relative\",\"file\":\"a.cpp\","
                    "\"arguments\":[\"clang++\"]}]"),
        std::string(40, '[') + std::string(40, ']'),
        std::string(1, char(0xff))}) {
    writeFile(Database, Bad);
    rejected("TR0601");
  }
  auto E = entry();
  E["arguments"] = Array{42};
  database({std::move(E)});
  rejected("TR0601");
  writeFile(Database, std::string(MaxCompilationDatabaseBytes + 1, ' '));
  rejected("TR0601");
}

TEST_F(TranslateCompilationContextTest,
       SourceTargetArchitectureAndWidthMustAgree) {
  database({entry({"clang++", "-target", Options.TargetTriple, "-arch",
                   "x86_64", "-m64", "../src/a.cpp"})});
  ASSERT_TRUE(parse());
  for (const auto &Flags : std::vector<std::vector<std::string>>{
           {"--target=aarch64-unknown-linux-gnu"},
           {"-arch", "arm64"},
           {"-m32"},
           {"-target", Options.TargetTriple, "-target",
            "x86_64-pc-windows-msvc"}}) {
    std::vector<std::string> Args = {"clang++", "../src/a.cpp"};
    Args.insert(Args.end(), Flags.begin(), Flags.end());
    database({entry(Args)});
    rejected("TR0204");
  }
  Options.TargetTriple.clear();
  database({entry()});
  ASSERT_TRUE(parse());
  EXPECT_EQ(Result.TargetTriple,
            llvm::Triple::normalize(llvm::sys::getDefaultTargetTriple()));
}

TEST_F(TranslateCompilationContextTest,
       UnsupportedDriverOptionsAndMissingOperandsAreRejected) {
  for (const auto &Flags : std::vector<std::vector<std::string>>{
           {"-O3"},
           {"-Ofast"},
           {"-ffast-math"},
           {"-fpack-struct"},
           {"-std=gnu++17"},
           {"-std=c++20"},
           {"-isysroot", "/"},
           {"--sysroot=/"},
           {"-resource-dir", "/"},
           {"-include", "../src/a.cpp"},
           {"-Xclang", "-load"},
           {"-Wl,anything"},
           {"-Wp,-DVALUE=1"},
           {"-fmodules"},
           {"-stdlib=libc++"},
           {"-g"},
           {"-Wunknown-flag"},
           {"-I-"},
           {"-isystem-after", "../include one"},
           {"-obscure-unknown-option"},
           {"-D"},
           {"-U"},
           {"-I"},
           {"-o"},
           {"-MF"},
           {"-x", "c"}}) {
    std::vector<std::string> Args = {"clang++", "../src/a.cpp"};
    Args.insert(Args.end(), Flags.begin(), Flags.end());
    database({entry(Args)});
    rejected("TR0004");
  }
  for (const std::string &Driver :
       {"ccache", "env", "g++", "clang-cl", "clang++-bad"}) {
    database({entry({Driver, "../src/a.cpp"})});
    rejected("TR0004");
  }
}

TEST_F(TranslateCompilationContextTest,
       CommandMustContainExactlyTheSelectedSource) {
  database({entry({"clang++", "-DVALUE=1"})});
  rejected("TR0602");
  database({entry({"clang++", "../src/a.cpp", "../src/a.cpp"})});
  rejected("TR0602");
  database({entry({"clang++", "../src/a.cpp", "&&", "touch", "marker"})});
  rejected("TR0602");
  EXPECT_FALSE(fs::exists(Build / "marker"));
}

TEST_F(TranslateCompilationContextTest,
       ExtraArgumentsFollowEntryArgumentsUnderTheSamePolicy) {
  database({entry({"clang++", "-DVALUE=1", "../src/a.cpp"})});
  Options.ExtraSourceArguments = {"-UVALUE", "-DVALUE=2", "-I../include two",
                                  "-O2"};
  ASSERT_TRUE(parse());
  EXPECT_EQ(Result.Units.front().NormalizedArguments,
            (std::vector<std::string>{"-std=c++17", "-DVALUE=1", "-UVALUE",
                                      "-DVALUE=2", "-I", "$PROJECT/include two",
                                      "-O2"}));
  Options.ExtraSourceArguments = {"-fpack-struct"};
  rejected("TR0004");
}

TEST_F(TranslateCompilationContextTest,
       InvalidArgumentTextNeverReachesFrontendJobs) {
  database({entry()});
  for (const auto &Arg :
       {std::string("-DVALUE=x\ny"), std::string("-DVALUE=x\ry"),
        std::string("-DVALUE=") + char(0xff)}) {
    Options.ExtraSourceArguments = {Arg};
    rejected("TR0004");
  }
  auto CheckResponseMode = [&](CommandQuoting Quoting, const char *Flags,
                               const char *Label) {
    SCOPED_TRACE(Label);
    Options.Quoting = Quoting;
    Options.ExtraSourceArguments.clear();
    writeFile(Build / "flags.rsp", Flags);
    database({entry({"clang++", "@flags.rsp", "../src/a.cpp"})});
    ASSERT_NO_FATAL_FAILURE(rejected("TR0004"));
  };
  EXPECT_NO_FATAL_FAILURE(
      CheckResponseMode(CommandQuoting::GNU, "'-DVALUE=x\ny'", "GNU"));
  EXPECT_NO_FATAL_FAILURE(
      CheckResponseMode(CommandQuoting::Windows, "\"-DVALUE=x\ny\"", "Windows"));
}

TEST_F(TranslateCompilationContextTest,
       ContextSnapshotDetectsDatabaseChangesAndRemovedResponses) {
  writeFile(Build / "flags.rsp", "-DVALUE=1");
  database({entry({"clang++", "@flags.rsp", "../src/a.cpp"})});
  ASSERT_TRUE(parse());
  writeFile(Database, readFile(Database) + "\n");
  EXPECT_FALSE(verifyProjectContextInputs(Result, Errors));
  EXPECT_EQ(Errors.back().Code, "TR0601");
  ASSERT_TRUE(parse());
  fs::remove(Build / "flags.rsp");
  EXPECT_FALSE(verifyProjectContextInputs(Result, Errors));
  EXPECT_EQ(Errors.back().Code, "TR0603");
}

TEST_F(TranslateCompilationContextTest,
       SemanticIdentityIgnoresDatabaseOrderOutputAndResponseFormatting) {
  writeFile(Build / "flags.rsp", "-DVALUE=1 -O2");
  database({entry({"clang++", "@flags.rsp", "../src/a.cpp", "-o", "one.o"})});
  ASSERT_TRUE(parse());
  const auto ID = Result.Units.front().ConfigurationID;
  const auto DBHash = Result.Database.SHA256;
  const auto ResponseHash = Result.Units.front().ResponseFiles.front().SHA256;
  writeFile(Build / "flags.rsp", "  -DVALUE=1\n-O2\n");
  database({entry({"clang++", "missing.cpp"}, "missing.cpp"),
            entry({"clang++", "@flags.rsp", "../src/a.cpp", "-o", "two.o"})});
  ASSERT_TRUE(parse());
  EXPECT_EQ(Result.Units.front().ConfigurationID, ID);
  EXPECT_NE(Result.Database.SHA256, DBHash);
  EXPECT_NE(Result.Units.front().ResponseFiles.front().SHA256, ResponseHash);
  writeFile(Build / "flags.rsp", "-DVALUE=2 -O2");
  ASSERT_TRUE(parse());
  EXPECT_NE(Result.Units.front().ConfigurationID, ID);
}

TEST_F(TranslateCompilationContextTest,
       RelocatedAbsoluteDatabaseChangesRawHashButPreservesSemanticIdentity) {
  database({entry(
      {"clang++", "-I", (Root / "include one").string(), Source.string()})});
  ASSERT_TRUE(parse());
  const auto Original = Result;
  const auto Relocated = tmpFile("relocated project");
  fs::copy(Root, Relocated, fs::copy_options::recursive);
  Root = Relocated;
  Build = Root / "build";
  Source = Root / "src" / "a.cpp";
  Database = Build / "compile_commands.json";
  Options.ProjectRoot = Root.string();
  Options.Database = Database.string();
  Options.Selections = {{Source.string(), std::nullopt}};
  database({entry(
      {"clang++", "-I", (Root / "include one").string(), Source.string()})});
  ASSERT_TRUE(parse());
  EXPECT_EQ(Result.Units.front().ConfigurationID,
            Original.Units.front().ConfigurationID);
  EXPECT_EQ(Result.Units.front().NormalizedArguments,
            Original.Units.front().NormalizedArguments);
  EXPECT_NE(Result.Database.SHA256, Original.Database.SHA256);
  EXPECT_NE(Result.Units.front().SourceAbsolute,
            Original.Units.front().SourceAbsolute);
}

TEST_F(TranslateCompilationContextTest,
       OwnedPathsRejectSiblingPrefixesAndSymlinkEscapes) {
  const auto Outside = tmpFile("project-other");
  fs::create_directories(Outside);
  database({entry({"clang++", "-I", Outside.string(), "../src/a.cpp"})});
  rejected("TR0004");
  std::error_code EC;
  fs::create_directory_symlink(Outside, Root / "external", EC);
  if (EC)
    GTEST_SKIP() << "symlink creation unavailable: " << EC.message();
  database({entry({"clang++", "-isystem", "../external", "../src/a.cpp"})});
  rejected("TR0004");
  writeFile(Outside / "flags.rsp", "-DVALUE=1");
  database({entry({"clang++", "@../external/flags.rsp", "../src/a.cpp"})});
  rejected("TR0603");
  writeFile(Outside / "external.cpp", "int other() { return 1; }\n");
  Options.Selections = {
      {(Root / "external" / "external.cpp").string(), std::nullopt}};
  rejected("TR0602");
}

TEST_F(TranslateCompilationContextTest,
       ParentTraversalResolvesSymlinksBeforeDotDot) {
  const auto Outside = tmpFile("outside");
  fs::create_directories(Outside / "nested");
  fs::create_directories(Outside / "include");
  fs::create_directories(Root / "include");
  std::error_code EC;
  fs::create_directory_symlink(Outside / "nested", Root / "alias", EC);
  if (EC)
    GTEST_SKIP() << "symlink creation unavailable: " << EC.message();
  database({entry({"clang++", "-I", "../alias/../include", "../src/a.cpp"})});
  ASSERT_NO_FATAL_FAILURE(rejected("TR0004"));
  EXPECT_NE(Errors.front().Reason.find("outside --project-root"),
            std::string::npos);
}

} // namespace
