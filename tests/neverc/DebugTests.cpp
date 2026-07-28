#include "NeverCTestFixture.h"

class DebugTest : public NeverCTest {
protected:
  struct DwarfDumpTool {
    std::string path;
    std::string infoFlag;
    std::string lineFlag;
    bool isLibdwarf = false;
  };

  DwarfDumpTool findDwarfDump() const {
    if (exec("which", {"llvm-dwarfdump"}).exitCode == 0)
      return {"llvm-dwarfdump", "--debug-info", "--debug-line", false};

    if (exec("which", {"dwarfdump"}).exitCode == 0) {
      auto ver = exec("dwarfdump", {"--version"});
      if (ver.contains("libdwarf") || ver.contains("dwarfdump 2."))
        return {"dwarfdump", "-v -i", "-l", true};
      return {"dwarfdump", "--debug-info", "--debug-line", false};
    }

    return {};
  }

  CmdResult dwarfDumpInfo(const DwarfDumpTool &tool,
                          const std::string &obj) const {
    auto flags = splitFlags(tool.infoFlag);
    flags.push_back(obj);
    return exec(tool.path, flags);
  }

  CmdResult dwarfDumpLine(const DwarfDumpTool &tool,
                          const std::string &obj) const {
    auto flags = splitFlags(tool.lineFlag);
    flags.push_back(obj);
    return exec(tool.path, flags);
  }
};

TEST_F(DebugTest, HostDWARF) {
  auto src = (testDir() / "debug/test_dwarf_debug.c").string();
  auto obj = tmpFile("host_dwarf.o");
  auto exe = tmpFile("host_dwarf");

  std::vector<std::string> base;
  for (auto &f : sysrootFlags()) base.push_back(f);
  for (auto &f : archFlags()) base.push_back(f);

  auto c = base;
  c.insert(c.end(), {"-std=c11", "-g", "-fno-lto", "-c", src, "-o",
                     obj.string()});
  ASSERT_EQ(ncc(c).exitCode, 0) << "host-dwarf compile";

  auto l = base;
  l.insert(l.end(),
           {"-std=c11", "-g", "-fno-lto", src, "-o", exe.string()});
  ASSERT_EQ(ncc(l).exitCode, 0) << "host-dwarf link";

  // dwarfdump verification
  auto tool = findDwarfDump();
  if (tool.path.empty()) {
    GTEST_SKIP() << "dwarfdump not available";
    return;
  }

  auto dump = dwarfDumpInfo(tool, obj.string());
  EXPECT_TRUE(dump.contains("DW_TAG_compile_unit"));
  EXPECT_TRUE(dump.contains("neverc")) << "producer not neverc";

  for (auto *fn : {"compute_area", "shape_score", "main"}) {
    SCOPED_TRACE(fn);
    EXPECT_TRUE(dump.contains(fn)) << fn << " subprogram missing";
  }

  EXPECT_TRUE(dump.contains("argc")) << "argc parameter missing";

  for (auto *ty : {"Point", "Rect", "Shape", "Color"}) {
    SCOPED_TRACE(ty);
    EXPECT_TRUE(dump.contains(ty)) << ty << " type missing";
  }

  auto lineDump = dwarfDumpLine(tool, obj.string());
  EXPECT_TRUE(lineDump.contains("test_dwarf_debug.c"));
}

TEST_F(DebugTest, WindowsCOFFDWARF) {
  auto src = (testDir() / "debug/test_dwarf_debug.c").string();
  auto obj = tmpFile("win_dwarf.obj");

  auto r = ncc({"--target=x86_64-pc-windows-msvc", "-gdwarf-5", "-fno-lto",
                "-c", src, "-o", obj.string()});
  ASSERT_EQ(r.exitCode, 0) << "windows-dwarf compile\n" << r.err;

  std::string objdumpTool;
  if (exec("which", {"llvm-objdump"}).exitCode == 0)
    objdumpTool = "llvm-objdump";
  else if (exec("which", {"objdump"}).exitCode == 0)
    objdumpTool = "objdump";
  else {
    GTEST_SKIP() << "objdump not available";
    return;
  }
  auto sections = exec(objdumpTool, {"-h", obj.string()});
  if (!sections.contains(".text")) {
    GTEST_SKIP() << objdumpTool << " cannot parse COFF format on this host";
    return;
  }
  for (auto *sect : {".debug_info", ".debug_abbrev", ".debug_line",
                     ".debug_str"}) {
    SCOPED_TRACE(sect);
    EXPECT_TRUE(sections.contains(sect)) << sect << " section missing";
  }

  auto tool = findDwarfDump();
  if (!tool.path.empty()) {
    auto dump = dwarfDumpInfo(tool, obj.string());
    if (dump.contains("DW_TAG_compile_unit")) {
      EXPECT_TRUE(dump.contains("0x0005") || dump.contains("DWARF version 5")
                  || dump.contains("version_stamp    = 0x0005"))
          << "DWARF version 5 expected";
    }
  }
}

TEST_F(DebugTest, LinuxELFDWARF) {
  auto src = (testDir() / "debug/test_dwarf_debug.c").string();
  auto obj = tmpFile("linux_dwarf.o");

  auto r = ncc({"--target=x86_64-linux-gnu", "-gdwarf-5", "-fno-lto", "-c",
                src, "-o", obj.string()});
  ASSERT_EQ(r.exitCode, 0) << "linux-dwarf compile\n" << r.err;

  auto tool = findDwarfDump();
  if (!tool.path.empty()) {
    auto dump = dwarfDumpInfo(tool, obj.string());
    if (dump.contains("DW_TAG_compile_unit")) {
      EXPECT_TRUE(dump.contains("0x0005") || dump.contains("DWARF version 5")
                  || dump.contains("version_stamp    = 0x0005"));
      EXPECT_TRUE(dump.contains("main"));
    }
  }
}

TEST_F(DebugTest, AArch64DWARF) {
  auto src = (testDir() / "debug/test_dwarf_debug.c").string();
  auto obj = tmpFile("aarch64_dwarf.o");

  auto r = ncc({"--target=aarch64-linux-gnu", "-gdwarf-5", "-fno-lto", "-c",
                src, "-o", obj.string()});
  ASSERT_EQ(r.exitCode, 0) << "aarch64-dwarf compile\n" << r.err;

  auto tool = findDwarfDump();
  if (!tool.path.empty()) {
    auto dump = dwarfDumpInfo(tool, obj.string());
    if (dump.contains("DW_TAG_compile_unit")) {
      EXPECT_TRUE(dump.contains("0x0005") || dump.contains("DWARF version 5")
                  || dump.contains("version_stamp    = 0x0005"));
      EXPECT_TRUE(dump.contains("main"));
    }
  }
}

// Flags that make parallel codegen split a small module, so the partition
// paths below are exercised without needing hundreds of functions.
static std::vector<std::string> forcePartitions(unsigned N) {
  return {"-fparallel-codegen=" + std::to_string(N), "-mllvm",
          "-neverc-pcg-min-funcs=2", "-mllvm", "-neverc-pcg-weight-floor=0"};
}

// Turns the merger's silent serial-codegen fallback into a hard error, so a
// partitioning regression fails the test instead of quietly compiling slower.
class StrictParallelCodegen {
public:
  StrictParallelCodegen() {
#ifdef _WIN32
    _putenv_s("NEVERC_PCG_STRICT", "1");
#else
    setenv("NEVERC_PCG_STRICT", "1", 1);
#endif
  }
  ~StrictParallelCodegen() {
#ifdef _WIN32
    _putenv_s("NEVERC_PCG_STRICT", "");
#else
    unsetenv("NEVERC_PCG_STRICT");
#endif
  }
  StrictParallelCodegen(const StrictParallelCodegen &) = delete;
  StrictParallelCodegen &operator=(const StrictParallelCodegen &) = delete;
};

// Whatever an alias resolves to has to keep its definition in every partition.
// The rewrite that drops aliases from partitions other than 0 cannot run until
// materializeAll(), which is itself what runs the verifier, so demoting the
// target to a declaration first leaves the module malformed and the compile
// aborts with "Alias must point to a definition".  Only -g reaches it, because
// UpgradeDebugInfo is what calls verifyModule.
//
// Function targets were already handled; global variables, thread-locals and
// alias chains reach the same verifier and were not.
//
// The target has to survive only that long, though: once the aliases are gone
// it must stop being a definition, or every partition emits one and the merge
// refuses the duplicates.  Strict mode is what makes that second half fail the
// test rather than fall back to serial codegen unnoticed.
TEST_F(DebugTest, ParallelCodegenKeepsAliasTargetsDefined) {
  StrictParallelCodegen Strict;
  auto src = tmpFile("pcg_alias.c");
  std::string code = R"(
int plain_var = 42;
extern int plain_alias __attribute__((alias("plain_var")));

__thread int tls_var = 7;
extern __thread int tls_alias __attribute__((alias("tls_var")));

int chain_var = 5;
extern int chain_mid __attribute__((alias("chain_var")));
extern int chain_top __attribute__((alias("chain_mid")));

int base_fn(int x) { return x + 1; }
extern int fn_alias(int) __attribute__((alias("base_fn")));
)";
  // Enough binnable functions that the targets above cannot all land in
  // partition 0 by chance.
  for (int I = 0; I < 24; ++I)
    code += "int spread_" + std::to_string(I) + "(int a){int s=0;for(int k=0;"
            "k<a;k++)s+=k*" + std::to_string(I) + ";return s;}\n";
  code += "int main(void){int t=plain_alias+tls_alias+chain_top+fn_alias(1);\n";
  for (int I = 0; I < 24; ++I)
    code += "  t += spread_" + std::to_string(I) + "(3);\n";
  code += "  return t;}\n";
  writeFile(src, code);

  auto obj = tmpFile("pcg_alias.o");
  // ELF: the only format of the three whose C frontend accepts alias
  // attributes on variables.
  std::vector<std::string> args = {"--target=x86_64-unknown-linux-gnu",
                                   "-gdwarf-5", "-fno-lto"};
  for (auto &f : forcePartitions(4))
    args.push_back(f);
  args.insert(args.end(), {"-c", src.string(), "-o", obj.string()});

  auto r = ncc(args);
  EXPECT_EQ(r.exitCode, 0)
      << "alias targets lost their definition during partitioning\n"
      << r.err;
}

// Mach-O writes cross-section DWARF references as plain offsets rather than
// relocations, so merging partitions has to re-point them.  Under DWARF 5 the
// names in .debug_info are indices through .debug_str_offsets, and leaving
// that table alone makes every partition after the first resolve its names
// against partition 0's strings -- silently, with nothing malformed for a
// debugger to report.  The symptom is duplicated and empty function names.
TEST_F(DebugTest, MachOParallelCodegenResolvesDwarf5Names) {
  if (!isDarwin())
    GTEST_SKIP() << "Mach-O is the only format that offsets DWARF by value";

  StrictParallelCodegen Strict;
  constexpr int NumFns = 24;
  auto src = tmpFile("pcg_names.c");
  std::string code;
  for (int I = 0; I < NumFns; ++I)
    code += "int uniquely_named_" + std::to_string(I) +
            "(int a){int s=0;for(int k=0;k<a;k++)s+=k*" + std::to_string(I) +
            ";return s;}\n";
  code += "int main(void){int t=0;\n";
  for (int I = 0; I < NumFns; ++I)
    code += "  t += uniquely_named_" + std::to_string(I) + "(3);\n";
  code += "  return t;}\n";
  writeFile(src, code);

  auto obj = tmpFile("pcg_names.o");
  std::vector<std::string> args = {"-gdwarf-5", "-fno-lto"};
  for (auto &f : forcePartitions(4))
    args.push_back(f);
  args.insert(args.end(), {"-c", src.string(), "-o", obj.string()});
  auto r = ncc(args);
  ASSERT_EQ(r.exitCode, 0) << r.err;

  auto tool = findDwarfDump();
  if (tool.path.empty())
    GTEST_SKIP() << "dwarfdump not available";

  auto dump = dwarfDumpInfo(tool, obj.string());
  ASSERT_TRUE(dump.contains("DW_TAG_subprogram"));
  for (int I = 0; I < NumFns; ++I) {
    std::string Name = "uniquely_named_" + std::to_string(I);
    // Anchored at the end so uniquely_named_1 cannot match uniquely_named_12.
    // The two dumpers differ in how they close the value: llvm/Apple print
    // DW_AT_name ("x"), libdwarf prints (indexed string: 0x..)x at end of line.
    EXPECT_TRUE(dump.contains(Name + "\"") || dump.contains(Name + "\n"))
        << Name << " missing: a partition's names resolved against another "
                   "partition's string table";
  }
}

TEST_F(DebugTest, WindowsDefaultDebug) {
  auto src = (testDir() / "debug/test_dwarf_debug.c").string();
  auto obj = tmpFile("win_default_debug.obj");

  auto r = ncc({"--target=x86_64-pc-windows-msvc", "-g", "-fno-lto", "-c",
                src, "-o", obj.string()});
  ASSERT_EQ(r.exitCode, 0);

  std::string objdumpTool;
  if (exec("which", {"llvm-objdump"}).exitCode == 0)
    objdumpTool = "llvm-objdump";
  else if (exec("which", {"objdump"}).exitCode == 0)
    objdumpTool = "objdump";
  else {
    GTEST_SKIP() << "objdump not available";
    return;
  }
  auto sections = exec(objdumpTool, {"-h", obj.string()});
  if (!sections.contains(".text")) {
    GTEST_SKIP() << objdumpTool << " cannot parse COFF format on this host";
    return;
  }
  EXPECT_TRUE(sections.contains(".debug_info"))
      << "Windows -g should produce DWARF, not CodeView";
}
