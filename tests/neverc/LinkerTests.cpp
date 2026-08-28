#include "NeverCTestFixture.h"
#include "llvm/Support/JSON.h"

class LinkerTest : public NeverCTest {
protected:
  CmdResult compileObject(const fs::path &source,
                          const fs::path &object) const {
    std::vector<std::string> args;
    for (const std::string &flag : sysrootFlags())
      args.push_back(flag);
    for (const std::string &flag : archFlags())
      args.push_back(flag);
    args.insert(args.end(),
                {"-fno-lto", "-c", source.string(), "-o", object.string()});
    return ncc(args);
  }

  std::vector<std::string> baseLinkArgs() const {
    std::vector<std::string> args;
    for (const std::string &flag : sysrootFlags())
      args.push_back(flag);
    for (const std::string &flag : archFlags())
      args.push_back(flag);
    for (const std::string &flag : linkFlags())
      args.push_back(flag);
    args.push_back("-fno-lto");
    return args;
  }
};

TEST_F(LinkerTest, EmbeddedLinkerDefault) {
  auto src = tmpFile("fallback.c");
  writeFile(src, "int main(void){return 0;}");
  auto r = ncc({"-###"} );
  // The -### output should reference neverc and (in-process)
  auto args = std::vector<std::string>();
  for (auto &f : sysrootFlags()) args.push_back(f);
  for (auto &f : archFlags()) args.push_back(f);
  args.push_back("-###");
  args.push_back(src.string());
  args.push_back("-o");
  args.push_back(tmpFile("fallback").string());
  auto dr = ncc(args);
  auto all = dr.err + dr.out;
  EXPECT_TRUE(all.find("(in-process)") != std::string::npos)
      << "embedded linker: missing (in-process) marker\n" << all;
}

TEST_F(LinkerTest, AutorouteObjectInput) {
  auto src = tmpFile("autoroute.c");
  writeFile(src, "int main(void){return 0;}");
  auto obj = tmpFile("autoroute.o");
  auto exe = tmpFile("autoroute");

  std::vector<std::string> c;
  for (auto &f : sysrootFlags()) c.push_back(f);
  for (auto &f : archFlags()) c.push_back(f);
  c.insert(c.end(), {"-c", src.string(), "-o", obj.string()});
  ASSERT_EQ(ncc(c).exitCode, 0);

  std::vector<std::string> l;
  for (auto &f : sysrootFlags()) l.push_back(f);
  for (auto &f : archFlags()) l.push_back(f);
  for (auto &f : linkFlags()) l.push_back(f);
  l.insert(l.end(), {obj.string(), "-o", exe.string()});
  ASSERT_EQ(ncc(l).exitCode, 0);

  auto r = exec(exe.string(), {});
  EXPECT_EQ(r.exitCode, 0);
}

TEST_F(LinkerTest, DuplicateLazyLibraryIsLoadedOnce) {
  if (!isLinux())
    GTEST_SKIP() << "duplicate -l coalescing is an ELF linker behavior";

  const fs::path dir = tmpFile("duplicate_lazy_library");
  fs::create_directories(dir);
  const fs::path librarySource = dir / "repeat.c";
  const fs::path libraryObject = dir / "repeat.o";
  const fs::path archive = dir / "librepeat.a";
  const fs::path mainSource = dir / "main.c";
  const fs::path mainObject = dir / "main.o";
  const fs::path executable = dir / "repeat";

  writeFile(librarySource, "int repeated_value(void) { return 29; }");
  writeFile(mainSource,
            "int repeated_value(void); "
            "int main(void) { return repeated_value() == 29 ? 0 : 1; }");

  CmdResult libraryCompile = compileObject(librarySource, libraryObject);
  ASSERT_EQ(libraryCompile.exitCode, 0) << libraryCompile.err;
  CmdResult mainCompile = compileObject(mainSource, mainObject);
  ASSERT_EQ(mainCompile.exitCode, 0) << mainCompile.err;

  CmdResult archiveBuild = ncc(
      {"--emit-static-lib", libraryObject.string(), "-o", archive.string()});
  ASSERT_EQ(archiveBuild.exitCode, 0) << archiveBuild.err;

  std::vector<std::string> linkArgs = baseLinkArgs();
  linkArgs.insert(linkArgs.end(),
                  {"-ftime-trace", "-ftime-trace-granularity=1",
                   mainObject.string(), "-L" + dir.string(), "-lrepeat",
                   "-lrepeat", "-o", executable.string()});
  CmdResult link = ncc(linkArgs);
  ASSERT_EQ(link.exitCode, 0) << link.err;
  EXPECT_EQ(exec(executable.string(), {}).exitCode, 0);

  const fs::path timeTrace(executable.string() + ".time-trace");
  ASSERT_TRUE(fs::exists(timeTrace));
  auto parsed = llvm::json::parse(readFile(timeTrace));
  ASSERT_TRUE(static_cast<bool>(parsed));
  const llvm::json::Object *root = parsed->getAsObject();
  ASSERT_NE(root, nullptr);
  const llvm::json::Array *events = root->getArray("traceEvents");
  ASSERT_NE(events, nullptr);

  size_t archiveLoads = 0;
  for (const llvm::json::Value &value : *events) {
    const llvm::json::Object *event = value.getAsObject();
    if (!event || event->getString("name") != "Load input files")
      continue;
    const llvm::json::Object *eventArgs = event->getObject("args");
    if (eventArgs && eventArgs->getString("detail") == archive.string())
      ++archiveLoads;
  }
  EXPECT_EQ(archiveLoads, 1U)
      << "a repeated normal -l archive must be loaded at most once";
}

TEST_F(LinkerTest, LibraryScriptOccurrencesAreNotCoalesced) {
  if (!isLinux())
    GTEST_SKIP() << "GNU linker scripts are an ELF linker behavior";

  const fs::path dir = tmpFile("library_script_occurrences");
  fs::create_directories(dir);
  const fs::path memberSource = dir / "member.c";
  const fs::path memberObject = dir / "member.o";
  const fs::path libraryScript = dir / "libscript.a";
  const fs::path mainSource = dir / "main.c";
  const fs::path mainObject = dir / "main.o";

  writeFile(memberSource,
            "int library_script_side_effect(void) { return 43; }");
  writeFile(mainSource, "int main(void) { return 0; }");
  CmdResult memberCompile = compileObject(memberSource, memberObject);
  ASSERT_EQ(memberCompile.exitCode, 0) << memberCompile.err;
  CmdResult mainCompile = compileObject(mainSource, mainObject);
  ASSERT_EQ(mainCompile.exitCode, 0) << mainCompile.err;

  // A file found through -l may itself be a linker script.  INPUT(object)
  // has positional effects, so two occurrences must be parsed twice rather
  // than treated like repeated lazy archives.
  writeFile(libraryScript, "INPUT(\"" + memberObject.string() + "\")\n");

  std::vector<std::string> linkArgs = baseLinkArgs();
  linkArgs.insert(linkArgs.end(),
                  {mainObject.string(), "-L" + dir.string(), "-lscript",
                   "-lscript", "-o", (dir / "main").string()});
  CmdResult link = ncc(linkArgs);
  EXPECT_NE(link.exitCode, 0)
      << "two library-script occurrences must retain positional INPUT "
         "semantics";
  EXPECT_TRUE(link.stderrContains("duplicate symbol")) << link.err;
}

TEST_F(LinkerTest, ArchiveWarningsPreserveDuplicateLibraryOccurrences) {
  if (!isLinux())
    GTEST_SKIP() << "ELF archive diagnostics are an ELF linker behavior";

  const fs::path dir = tmpFile("archive_warning_occurrences");
  fs::create_directories(dir);
  const fs::path payload = dir / "payload.txt";
  const fs::path archive = dir / "libwarning.a";
  const fs::path mainSource = dir / "main.c";
  const fs::path mainObject = dir / "main.o";

  writeFile(payload, "not an ELF relocatable object\n");
  writeFile(mainSource, "int main(void) { return 0; }");
  CmdResult mainCompile = compileObject(mainSource, mainObject);
  ASSERT_EQ(mainCompile.exitCode, 0) << mainCompile.err;
  CmdResult archiveBuild =
      ncc({"--emit-static-lib", payload.string(), "-o", archive.string()});
  ASSERT_EQ(archiveBuild.exitCode, 0) << archiveBuild.err;

  std::vector<std::string> linkArgs = baseLinkArgs();
  linkArgs.insert(linkArgs.end(),
                  {mainObject.string(), "-L" + dir.string(), "-lwarning",
                   "-lwarning", "-o", (dir / "main").string()});
  CmdResult link = ncc(linkArgs);
  ASSERT_EQ(link.exitCode, 0) << link.err;

  const std::string diagnostics = link.out + link.err;
  constexpr llvm::StringLiteral warningText =
      "is neither ET_REL nor LLVM bitcode";
  size_t warningCount = 0;
  for (size_t offset = 0;
       (offset = diagnostics.find(warningText.str(), offset)) !=
       std::string::npos;
       offset += warningText.size())
    ++warningCount;
  EXPECT_EQ(warningCount, 2U)
      << "occurrence-oriented archive warnings must not be suppressed";
}

TEST_F(LinkerTest, WholeArchiveDuplicateLibraryIsNotCoalesced) {
  if (!isLinux())
    GTEST_SKIP() << "--whole-archive is an ELF linker behavior";

  const fs::path dir = tmpFile("whole_archive_duplicate");
  fs::create_directories(dir);
  const fs::path librarySource = dir / "repeat.c";
  const fs::path libraryObject = dir / "repeat.o";
  const fs::path archive = dir / "librepeat.a";
  const fs::path mainSource = dir / "main.c";
  const fs::path mainObject = dir / "main.o";

  writeFile(librarySource, "int whole_archive_value(void) { return 31; }");
  writeFile(mainSource,
            "int whole_archive_value(void); "
            "int main(void) { return whole_archive_value() == 31 ? 0 : 1; }");
  CmdResult libraryCompile = compileObject(librarySource, libraryObject);
  ASSERT_EQ(libraryCompile.exitCode, 0) << libraryCompile.err;
  CmdResult mainCompile = compileObject(mainSource, mainObject);
  ASSERT_EQ(mainCompile.exitCode, 0) << mainCompile.err;
  CmdResult archiveBuild = ncc(
      {"--emit-static-lib", libraryObject.string(), "-o", archive.string()});
  ASSERT_EQ(archiveBuild.exitCode, 0) << archiveBuild.err;

  std::vector<std::string> linkArgs = baseLinkArgs();
  linkArgs.insert(linkArgs.end(),
                  {mainObject.string(), "-L" + dir.string(),
                   "-Wl,--whole-archive", "-lrepeat", "-lrepeat",
                   "-Wl,--no-whole-archive", "-o", (dir / "repeat").string()});
  CmdResult link = ncc(linkArgs);
  EXPECT_NE(link.exitCode, 0)
      << "two --whole-archive occurrences must retain duplicate-definition "
         "semantics";
  EXPECT_TRUE(link.stderrContains("duplicate symbol")) << link.err;
}

TEST_F(LinkerTest, AsNeededStateChangeIsNotCoalesced) {
  if (!isLinux())
    GTEST_SKIP() << "--as-needed is an ELF linker behavior";

  const fs::path dir = tmpFile("as_needed_state_change");
  fs::create_directories(dir);
  const fs::path marker = dir / "loaded";
  const fs::path librarySource = dir / "needed.c";
  const fs::path library = dir / "libneeded.so";
  const fs::path mainSource = dir / "main.c";
  const fs::path mainObject = dir / "main.o";
  const fs::path executable = dir / "main";

  writeFile(librarySource,
            "#include <stdio.h>\n"
            "__attribute__((constructor)) static void mark_loaded(void) {\n"
            "  FILE *file = fopen(\"" +
                marker.string() +
                "\", \"wb\");\n"
                "  if (file) { fputc(1, file); fclose(file); }\n"
                "}\n");
  writeFile(mainSource, "int main(void) { return 0; }");

  std::vector<std::string> sharedLinkArgs = baseLinkArgs();
  sharedLinkArgs.insert(
      sharedLinkArgs.end(),
      {"-fPIC", "-shared", librarySource.string(), "-o", library.string()});
  CmdResult sharedLink = ncc(sharedLinkArgs);
  ASSERT_EQ(sharedLink.exitCode, 0) << sharedLink.err;
  CmdResult mainCompile = compileObject(mainSource, mainObject);
  ASSERT_EQ(mainCompile.exitCode, 0) << mainCompile.err;

  std::vector<std::string> linkArgs = baseLinkArgs();
  linkArgs.insert(linkArgs.end(),
                  {mainObject.string(), "-L" + dir.string(), "-Wl,--as-needed",
                   "-lneeded", "-Wl,--no-as-needed", "-lneeded", "-o",
                   executable.string()});
  CmdResult link = ncc(linkArgs);
  ASSERT_EQ(link.exitCode, 0) << link.err;

  CmdResult run = exec(
      "/usr/bin/env", {"LD_LIBRARY_PATH=" + dir.string(), executable.string()});
  ASSERT_EQ(run.exitCode, 0) << run.err;
  EXPECT_TRUE(fs::exists(marker))
      << "the later --no-as-needed occurrence must force a DT_NEEDED entry";
}

TEST_F(LinkerTest, WarnBackrefsLibrarySandwichIsNotCoalesced) {
  if (!isLinux())
    GTEST_SKIP() << "--warn-backrefs is an ELF linker behavior";

  const fs::path dir = tmpFile("warn_backrefs_sandwich");
  fs::create_directories(dir);
  const fs::path librarySource = dir / "definition.c";
  const fs::path libraryObject = dir / "definition.o";
  const fs::path archive = dir / "libdefinition.a";
  const fs::path referenceSource = dir / "reference.c";
  const fs::path referenceObject = dir / "reference.o";
  const fs::path mainSource = dir / "main.c";
  const fs::path mainObject = dir / "main.o";
  const fs::path executable = dir / "main";

  writeFile(librarySource, "int backref_value(void) { return 37; }");
  writeFile(referenceSource,
            "int backref_value(void); "
            "int reference_value(void) { return backref_value(); }");
  writeFile(mainSource,
            "int reference_value(void); "
            "int main(void) { return reference_value() == 37 ? 0 : 1; }");
  CmdResult libraryCompile = compileObject(librarySource, libraryObject);
  ASSERT_EQ(libraryCompile.exitCode, 0) << libraryCompile.err;
  CmdResult referenceCompile = compileObject(referenceSource, referenceObject);
  ASSERT_EQ(referenceCompile.exitCode, 0) << referenceCompile.err;
  CmdResult mainCompile = compileObject(mainSource, mainObject);
  ASSERT_EQ(mainCompile.exitCode, 0) << mainCompile.err;
  CmdResult archiveBuild = ncc(
      {"--emit-static-lib", libraryObject.string(), "-o", archive.string()});
  ASSERT_EQ(archiveBuild.exitCode, 0) << archiveBuild.err;

  std::vector<std::string> linkArgs = baseLinkArgs();
  linkArgs.insert(linkArgs.end(),
                  {"-Wl,--warn-backrefs", "-L" + dir.string(), "-ldefinition",
                   referenceObject.string(), "-ldefinition",
                   mainObject.string(), "-o", executable.string()});
  CmdResult link = ncc(linkArgs);
  ASSERT_EQ(link.exitCode, 0) << link.err;
  EXPECT_EQ((link.out + link.err).find("backward reference detected"),
            std::string::npos)
      << "a later lazy definition in a library sandwich must retain the "
         "existing --warn-backrefs behavior";
  EXPECT_EQ(exec(executable.string(), {}).exitCode, 0);
}

TEST_F(LinkerTest, BinaryFormatDuplicateLibraryIsNotCoalesced) {
  if (!isLinux())
    GTEST_SKIP() << "--format=binary is an ELF linker behavior";

  const fs::path dir = tmpFile("binary_format_duplicate");
  fs::create_directories(dir);
  const fs::path payload = dir / "libpayload.a";
  const fs::path mainSource = dir / "main.c";
  const fs::path mainObject = dir / "main.o";
  writeFile(payload, "opaque archive-shaped library payload");
  writeFile(mainSource, "int main(void) { return 0; }");
  CmdResult mainCompile = compileObject(mainSource, mainObject);
  ASSERT_EQ(mainCompile.exitCode, 0) << mainCompile.err;

  std::vector<std::string> linkArgs = baseLinkArgs();
  linkArgs.insert(linkArgs.end(),
                  {mainObject.string(), "-L" + dir.string(),
                   "-Wl,--format=binary", "-lpayload", "-lpayload",
                   "-Wl,--format=elf", "-o", (dir / "main").string()});
  CmdResult link = ncc(linkArgs);
  EXPECT_NE(link.exitCode, 0)
      << "binary-format occurrences define input-specific symbols and must "
         "not be coalesced";
  EXPECT_TRUE(link.stderrContains("duplicate symbol")) << link.err;
}

TEST_F(LinkerTest, ArchiveStatsPreserveDuplicateLibraryOccurrences) {
  if (!isLinux())
    GTEST_SKIP() << "--print-archive-stats is an ELF linker behavior";

  const fs::path dir = tmpFile("archive_stats_duplicate");
  fs::create_directories(dir);
  const fs::path librarySource = dir / "repeat.c";
  const fs::path libraryObject = dir / "repeat.o";
  const fs::path archive = dir / "librepeat.a";
  const fs::path mainSource = dir / "main.c";
  const fs::path mainObject = dir / "main.o";
  const fs::path stats = dir / "archive-stats.tsv";
  writeFile(librarySource, "int stats_value(void) { return 41; }");
  writeFile(mainSource,
            "int stats_value(void); "
            "int main(void) { return stats_value() == 41 ? 0 : 1; }");
  CmdResult libraryCompile = compileObject(librarySource, libraryObject);
  ASSERT_EQ(libraryCompile.exitCode, 0) << libraryCompile.err;
  CmdResult mainCompile = compileObject(mainSource, mainObject);
  ASSERT_EQ(mainCompile.exitCode, 0) << mainCompile.err;
  CmdResult archiveBuild = ncc(
      {"--emit-static-lib", libraryObject.string(), "-o", archive.string()});
  ASSERT_EQ(archiveBuild.exitCode, 0) << archiveBuild.err;

  std::vector<std::string> linkArgs = baseLinkArgs();
  linkArgs.insert(linkArgs.end(),
                  {"-Wl,--print-archive-stats=" + stats.string(),
                   mainObject.string(), "-L" + dir.string(), "-lrepeat",
                   "-lrepeat", "-o", (dir / "main").string()});
  CmdResult link = ncc(linkArgs);
  ASSERT_EQ(link.exitCode, 0) << link.err;
  ASSERT_TRUE(fs::exists(stats));

  const std::string contents = readFile(stats);
  size_t occurrences = 0;
  for (size_t offset = 0;
       (offset = contents.find(archive.string(), offset)) != std::string::npos;
       offset += archive.string().size())
    ++occurrences;
  EXPECT_EQ(occurrences, 2U)
      << "archive statistics are occurrence-oriented diagnostics";
}

TEST_F(LinkerTest, TraceSymbolSessionPreservesDuplicateLibraryLoads) {
  if (!isLinux())
    GTEST_SKIP() << "--trace-symbol is an ELF linker behavior";

  const fs::path dir = tmpFile("trace_symbol_duplicate");
  fs::create_directories(dir);
  const fs::path librarySource = dir / "repeat.c";
  const fs::path libraryObject = dir / "repeat.o";
  const fs::path archive = dir / "librepeat.a";
  const fs::path mainSource = dir / "main.c";
  const fs::path mainObject = dir / "main.o";
  const fs::path executable = dir / "main";
  writeFile(librarySource, "int traced_value(void) { return 43; }");
  writeFile(mainSource,
            "int traced_value(void); "
            "int main(void) { return traced_value() == 43 ? 0 : 1; }");
  CmdResult libraryCompile = compileObject(librarySource, libraryObject);
  ASSERT_EQ(libraryCompile.exitCode, 0) << libraryCompile.err;
  CmdResult mainCompile = compileObject(mainSource, mainObject);
  ASSERT_EQ(mainCompile.exitCode, 0) << mainCompile.err;
  CmdResult archiveBuild = ncc(
      {"--emit-static-lib", libraryObject.string(), "-o", archive.string()});
  ASSERT_EQ(archiveBuild.exitCode, 0) << archiveBuild.err;

  std::vector<std::string> linkArgs = baseLinkArgs();
  linkArgs.insert(linkArgs.end(),
                  {"-ftime-trace", "-ftime-trace-granularity=1",
                   "-Wl,--trace-symbol=traced_value", mainObject.string(),
                   "-L" + dir.string(), "-lrepeat", "-lrepeat", "-o",
                   executable.string()});
  CmdResult link = ncc(linkArgs);
  ASSERT_EQ(link.exitCode, 0) << link.err;
  EXPECT_NE((link.out + link.err).find("traced_value"), std::string::npos)
      << "the trace-symbol session was not active";

  const fs::path timeTrace(executable.string() + ".time-trace");
  ASSERT_TRUE(fs::exists(timeTrace));
  auto parsed = llvm::json::parse(readFile(timeTrace));
  ASSERT_TRUE(static_cast<bool>(parsed));
  const llvm::json::Object *root = parsed->getAsObject();
  ASSERT_NE(root, nullptr);
  const llvm::json::Array *events = root->getArray("traceEvents");
  ASSERT_NE(events, nullptr);
  size_t archiveLoads = 0;
  for (const llvm::json::Value &value : *events) {
    const llvm::json::Object *event = value.getAsObject();
    if (!event || event->getString("name") != "Load input files")
      continue;
    const llvm::json::Object *eventArgs = event->getObject("args");
    if (eventArgs && eventArgs->getString("detail") == archive.string())
      ++archiveLoads;
  }
  EXPECT_EQ(archiveLoads, 2U)
      << "trace-symbol diagnostics must observe the uncoalesced input stream";
}

TEST_F(LinkerTest, InputListingSessionsPreserveDuplicateLibraryLoads) {
  if (!isLinux())
    GTEST_SKIP() << "ELF input-listing diagnostics are Linux-only";

  const fs::path dir = tmpFile("input_listing_duplicate");
  fs::create_directories(dir);
  const fs::path librarySource = dir / "repeat.c";
  const fs::path libraryObject = dir / "repeat.o";
  const fs::path archive = dir / "librepeat.a";
  const fs::path mainSource = dir / "main.c";
  const fs::path mainObject = dir / "main.o";
  writeFile(librarySource, "int listed_value(void) { return 47; }");
  writeFile(mainSource,
            "int listed_value(void); "
            "int main(void) { return listed_value() == 47 ? 0 : 1; }");
  CmdResult libraryCompile = compileObject(librarySource, libraryObject);
  ASSERT_EQ(libraryCompile.exitCode, 0) << libraryCompile.err;
  CmdResult mainCompile = compileObject(mainSource, mainObject);
  ASSERT_EQ(mainCompile.exitCode, 0) << mainCompile.err;
  CmdResult archiveBuild = ncc(
      {"--emit-static-lib", libraryObject.string(), "-o", archive.string()});
  ASSERT_EQ(archiveBuild.exitCode, 0) << archiveBuild.err;

  for (const std::pair<std::string, std::string> &mode :
       {std::pair<std::string, std::string>{"verbose", "-v"},
        {"trace", "-t"}}) {
    SCOPED_TRACE(mode.first);
    const fs::path executable = dir / ("main-" + mode.first);
    std::vector<std::string> linkArgs = baseLinkArgs();
    linkArgs.insert(linkArgs.end(),
                    {"-ftime-trace", "-ftime-trace-granularity=1", mode.second,
                     mainObject.string(), "-L" + dir.string(), "-lrepeat",
                     "-lrepeat", "-o", executable.string()});
    CmdResult link = ncc(linkArgs);
    ASSERT_EQ(link.exitCode, 0) << link.err;

    const fs::path timeTrace(executable.string() + ".time-trace");
    ASSERT_TRUE(fs::exists(timeTrace));
    auto parsed = llvm::json::parse(readFile(timeTrace));
    ASSERT_TRUE(static_cast<bool>(parsed));
    const llvm::json::Object *root = parsed->getAsObject();
    ASSERT_NE(root, nullptr);
    const llvm::json::Array *events = root->getArray("traceEvents");
    ASSERT_NE(events, nullptr);
    size_t archiveLoads = 0;
    for (const llvm::json::Value &value : *events) {
      const llvm::json::Object *event = value.getAsObject();
      if (!event || event->getString("name") != "Load input files")
        continue;
      const llvm::json::Object *eventArgs = event->getObject("args");
      if (eventArgs && eventArgs->getString("detail") == archive.string())
        ++archiveLoads;
    }
    EXPECT_EQ(archiveLoads, 2U)
        << "input-listing diagnostics must observe both occurrences";
  }
}

TEST_F(LinkerTest, NoMmapOutputFilePreservesExecutableContents) {
  if (!isLinux())
    GTEST_SKIP() << "--no-mmap-output-file is an ELF linker option";

  auto src = tmpFile("no_mmap_output.c");
  auto obj = tmpFile("no_mmap_output.o");
  auto mappedExe = tmpFile("mapped_output");
  auto bufferedExe = tmpFile("buffered_output");
  writeFile(src, R"(
volatile unsigned char payload[2 * 1024 * 1024 + 257] = {1};
int main(void) {
  return payload[sizeof(payload) - 1] == 0 ? 23 : 1;
}
)");

  std::vector<std::string> compileArgs;
  for (const std::string &flag : sysrootFlags())
    compileArgs.push_back(flag);
  for (const std::string &flag : archFlags())
    compileArgs.push_back(flag);
  compileArgs.insert(compileArgs.end(),
                     {"-fno-lto", "-c", src.string(), "-o", obj.string()});
  CmdResult compile = ncc(compileArgs);
  ASSERT_EQ(compile.exitCode, 0) << compile.err;

  auto link = [&](const fs::path &output, bool mmapOutput) {
    std::vector<std::string> args;
    for (const std::string &flag : sysrootFlags())
      args.push_back(flag);
    for (const std::string &flag : archFlags())
      args.push_back(flag);
    for (const std::string &flag : linkFlags())
      args.push_back(flag);
    args.push_back("-fno-lto");
    args.push_back("-fbuild-id=fast");
    if (!mmapOutput)
      args.push_back("-Wl,--no-mmap-output-file");
    args.insert(args.end(), {obj.string(), "-o", output.string()});
    return ncc(args);
  };

  CmdResult mappedLink = link(mappedExe, true);
  ASSERT_EQ(mappedLink.exitCode, 0) << mappedLink.err;
  CmdResult bufferedLink = link(bufferedExe, false);
  ASSERT_EQ(bufferedLink.exitCode, 0) << bufferedLink.err;

  ASSERT_GT(fileSize(mappedExe), 2U * 1024U * 1024U);
  EXPECT_TRUE(readFile(bufferedExe) == readFile(mappedExe))
      << "buffered and mmap output bytes differ";
  EXPECT_EQ(exec(bufferedExe.string(), {}).exitCode, 23);
}

TEST_F(LinkerTest, EmitStaticLib) {
  auto dir = tmpFile("eslib");
  fs::create_directories(dir);

  writeFile(dir / "add.c", "int eslib_add(int a, int b) { return a + b; }");
  writeFile(dir / "mul.c", "int eslib_mul(int a, int b) { return a * b; }");
  writeFile(dir / "neg.c", "int eslib_neg(int a) { return -a; }");
  writeFile(dir / "main.c", R"(
extern int eslib_add(int, int);
extern int eslib_mul(int, int);
extern int eslib_neg(int);
int main(void) {
    int r = 0;
    if (eslib_add(3, 4) != 7)  r = 1;
    if (eslib_mul(5, 6) != 30) r = 1;
    if (eslib_neg(9)    != -9) r = 1;
    if (eslib_add(eslib_neg(2), eslib_mul(3, 3)) != 7) r = 1;
    return r;
})");

  std::vector<std::string> base = {"-std=c11"};
  for (auto &f : sysrootFlags()) base.push_back(f);
  for (auto &f : archFlags()) base.push_back(f);

  // Compile all members
  for (auto *unit : {"add", "mul", "neg", "main"}) {
    auto c = base;
    c.insert(c.end(),
             {"-c", (dir / (std::string(unit) + ".c")).string(), "-o",
              (dir / (std::string(unit) + ".o")).string()});
    ASSERT_EQ(ncc(c).exitCode, 0) << "compile " << unit;
  }

  auto ar = dir / "ops.a";

  // -### must show in-process archive marker
  {
    auto dr = ncc({"--emit-static-lib", (dir / "add.o").string(),
                   (dir / "mul.o").string(), (dir / "neg.o").string(), "-o",
                   ar.string(), "-###"});
    auto all = dr.err + dr.out;
    EXPECT_TRUE(all.find("(in-process archive)") != std::string::npos)
        << "missing in-process archive marker";
  }

  // Build the archive
  ASSERT_EQ(ncc({"--emit-static-lib", (dir / "add.o").string(),
                 (dir / "mul.o").string(), (dir / "neg.o").string(), "-o",
                 ar.string()})
                .exitCode,
            0);

  EXPECT_GT(fileSize(ar), 0u);

  // Check magic header
  auto content = readFile(ar);
  EXPECT_TRUE(content.substr(0, 7) == "!<arch>") << "bad archive magic";

  // Link and run
  auto exe = dir / "main";
  std::vector<std::string> link;
  for (auto &f : sysrootFlags()) link.push_back(f);
  for (auto &f : archFlags()) link.push_back(f);
  link.insert(link.end(),
              {(dir / "main.o").string(), ar.string(), "-o", exe.string()});
  ASSERT_EQ(ncc(link).exitCode, 0);

  auto r = exec(exe.string(), {});
  EXPECT_EQ(r.exitCode, 0);

  // Deterministic: build twice, compare
  auto ar1 = dir / "det1.a";
  auto ar2 = dir / "det2.a";
  ncc({"--emit-static-lib", (dir / "add.o").string(), (dir / "mul.o").string(),
       (dir / "neg.o").string(), "-o", ar1.string()});
  ncc({"--emit-static-lib", (dir / "add.o").string(), (dir / "mul.o").string(),
       (dir / "neg.o").string(), "-o", ar2.string()});
  EXPECT_EQ(readFile(ar1), readFile(ar2)) << "archive not deterministic";
}

TEST_F(LinkerTest, EmitStaticLibSingleFile) {
  auto src = (testDir() / "codegen/test_emit_static_lib.c").string();
  auto memberObj = tmpFile("eslib_sf_member.o");
  auto ar = tmpFile("eslib_sf.a");
  auto exe = tmpFile("eslib_sf");

  std::vector<std::string> base;
  for (auto &f : sysrootFlags()) base.push_back(f);
  for (auto &f : archFlags()) base.push_back(f);

  auto c = base;
  c.insert(c.end(), {"-DSTATIC_LIB_MEMBER", "-c", src, "-o",
                     memberObj.string()});
  ASSERT_EQ(ncc(c).exitCode, 0);

  ASSERT_EQ(
      ncc({"--emit-static-lib", memberObj.string(), "-o", ar.string()})
          .exitCode,
      0);

  auto l = base;
  l.insert(l.end(), {src, ar.string(), "-o", exe.string()});
  ASSERT_EQ(ncc(l).exitCode, 0);

  auto r = exec(exe.string(), {});
  EXPECT_EQ(r.exitCode, 0);
}
