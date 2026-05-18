#include "NeverCTestFixture.h"

class DebugTest : public NeverCTest {};

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
  auto which = exec("which", {"dwarfdump"});
  if (which.exitCode != 0) {
    GTEST_SKIP() << "dwarfdump not available";
    return;
  }

  auto dump = exec("dwarfdump", {"--debug-info", obj.string()});
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

  auto lineDump = exec("dwarfdump", {"--debug-line", obj.string()});
  EXPECT_TRUE(lineDump.contains("test_dwarf_debug.c"));
}

TEST_F(DebugTest, WindowsCOFFDWARF) {
  auto src = (testDir() / "debug/test_dwarf_debug.c").string();
  auto obj = tmpFile("win_dwarf.obj");

  auto r = ncc({"--target=x86_64-pc-windows-msvc", "-gdwarf-5", "-fno-lto",
                "-c", src, "-o", obj.string()});
  ASSERT_EQ(r.exitCode, 0) << "windows-dwarf compile\n" << r.err;

  auto sections = exec("objdump", {"-h", obj.string()});
  for (auto *sect : {".debug_info", ".debug_abbrev", ".debug_line",
                     ".debug_str"}) {
    SCOPED_TRACE(sect);
    EXPECT_TRUE(sections.contains(sect)) << sect << " section missing";
  }

  auto which = exec("which", {"dwarfdump"});
  if (which.exitCode == 0) {
    auto dump = exec("dwarfdump", {"--debug-info", obj.string()});
    EXPECT_TRUE(dump.contains("0x0005") || dump.contains("DWARF version 5"))
        << "DWARF version 5 expected";
  }
}

TEST_F(DebugTest, LinuxELFDWARF) {
  auto src = (testDir() / "debug/test_dwarf_debug.c").string();
  auto obj = tmpFile("linux_dwarf.o");

  auto r = ncc({"--target=x86_64-linux-gnu", "-gdwarf-5", "-fno-lto", "-c",
                src, "-o", obj.string()});
  ASSERT_EQ(r.exitCode, 0) << "linux-dwarf compile\n" << r.err;

  auto which = exec("which", {"dwarfdump"});
  if (which.exitCode == 0) {
    auto dump = exec("dwarfdump", {"--debug-info", obj.string()});
    EXPECT_TRUE(dump.contains("0x0005") || dump.contains("DWARF version 5"));
    EXPECT_TRUE(dump.contains("main"));
  }
}

TEST_F(DebugTest, AArch64DWARF) {
  auto src = (testDir() / "debug/test_dwarf_debug.c").string();
  auto obj = tmpFile("aarch64_dwarf.o");

  auto r = ncc({"--target=aarch64-linux-gnu", "-gdwarf-5", "-fno-lto", "-c",
                src, "-o", obj.string()});
  ASSERT_EQ(r.exitCode, 0) << "aarch64-dwarf compile\n" << r.err;

  auto which = exec("which", {"dwarfdump"});
  if (which.exitCode == 0) {
    auto dump = exec("dwarfdump", {"--debug-info", obj.string()});
    EXPECT_TRUE(dump.contains("0x0005") || dump.contains("DWARF version 5"));
    EXPECT_TRUE(dump.contains("main"));
  }
}

TEST_F(DebugTest, WindowsDefaultDebug) {
  auto src = (testDir() / "debug/test_dwarf_debug.c").string();
  auto obj = tmpFile("win_default_debug.obj");

  auto r = ncc({"--target=x86_64-pc-windows-msvc", "-g", "-fno-lto", "-c",
                src, "-o", obj.string()});
  ASSERT_EQ(r.exitCode, 0);

  auto sections = exec("objdump", {"-h", obj.string()});
  EXPECT_TRUE(sections.contains(".debug_info"))
      << "Windows -g should produce DWARF, not CodeView";
}
