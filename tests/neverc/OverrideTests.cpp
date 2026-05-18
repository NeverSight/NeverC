#include "NeverCTestFixture.h"

class OverrideTest : public NeverCTest {
protected:
  fs::path overrideDir() { return testDir() / "override"; }

  std::string ovrSym() {
    return isDarwin() ? "_compute" : "compute";
  }

  void runOverrideTest(const std::string &name, const std::string &mode,
                       const std::vector<std::string> &linkExtra,
                       const std::string &mainC,
                       const std::vector<std::string> &sources,
                       int expectedExit, bool expectLinkError = false) {
    SCOPED_TRACE(name);
    std::string flto = (mode == "lto") ? "-flto" : "-fno-lto";

    // Compile all sources
    std::vector<std::string> objs;
    std::vector<std::string> allSrcs = {mainC};
    allSrcs.insert(allSrcs.end(), sources.begin(), sources.end());

    for (auto &src : allSrcs) {
      auto bn = fs::path(src).stem().string();
      auto obj = tmpFile(name + "_" + bn + ".o");
      std::vector<std::string> c = {flto};
      for (auto &f : sysrootFlags()) c.push_back(f);
      for (auto &f : archFlags()) c.push_back(f);
      c.insert(c.end(), {"-c", src, "-o", obj.string()});
      ASSERT_EQ(ncc(c).exitCode, 0) << "compile " << bn;
      objs.push_back(obj.string());
    }

    // Link
    auto exe = tmpFile(name + ".exe");
    std::vector<std::string> link;
    for (auto &f : sysrootFlags()) link.push_back(f);
    for (auto &f : archFlags()) link.push_back(f);
    link.push_back(flto);
    for (auto &o : objs) link.push_back(o);
    for (auto &l : linkExtra) link.push_back(l);
    link.insert(link.end(), {"-o", exe.string()});

    auto lr = ncc(link);
    if (expectLinkError) {
      EXPECT_NE(lr.exitCode, 0) << name << ": should have link-errored";
      return;
    }
    ASSERT_EQ(lr.exitCode, 0) << name << ": link\n" << lr.err;

    auto r = exec(exe.string(), {});
    EXPECT_EQ(r.exitCode, expectedExit)
        << name << ": run exit " << r.exitCode << " expected " << expectedExit;
  }
};

TEST_F(OverrideTest, BasicNonLTO) {
  auto d = overrideDir();
  runOverrideTest("basic_nolto", "nolto", {},
                  (d / "main_expect_42.c").string(),
                  {(d / "override_compute.c").string(),
                   (d / "lib_compute.c").string()},
                  0);
}

TEST_F(OverrideTest, BasicLTO) {
  auto d = overrideDir();
  runOverrideTest("basic_lto", "lto", {},
                  (d / "main_expect_42.c").string(),
                  {(d / "override_compute.c").string(),
                   (d / "lib_compute.c").string()},
                  0);
}

TEST_F(OverrideTest, DeclspecNonLTO) {
  auto d = overrideDir();
  runOverrideTest("declspec_nolto", "nolto", {},
                  (d / "main_expect_42.c").string(),
                  {(d / "override_compute_declspec.c").string(),
                   (d / "lib_compute.c").string()},
                  0);
}

TEST_F(OverrideTest, TwoOverridesNonLTO) {
  auto d = overrideDir();
  runOverrideTest("two_overrides_nolto", "nolto", {},
                  (d / "main_expect_100.c").string(),
                  {(d / "override_compute.c").string(),
                   (d / "override_compute_alt.c").string(),
                   (d / "lib_compute.c").string()},
                  0);
}

TEST_F(OverrideTest, FlagOverrideNonLTO) {
  auto d = overrideDir();
  runOverrideTest("flag_override_nolto", "nolto",
                  {"-Wl,--override=" + ovrSym()},
                  (d / "main_expect_99.c").string(),
                  {(d / "plain_compute_a.c").string(),
                   (d / "plain_compute_b.c").string()},
                  0);
}

TEST_F(OverrideTest, DuplicateErrorsNonLTO) {
  auto d = overrideDir();
  runOverrideTest("duplicate_errors_nolto", "nolto", {},
                  (d / "main_expect_42.c").string(),
                  {(d / "lib_compute.c").string(),
                   (d / "plain_compute_b.c").string()},
                  0, true);
}

TEST_F(OverrideTest, VariableOverrideNonLTO) {
  auto d = overrideDir();
  runOverrideTest("variable_nolto", "nolto", {},
                  (d / "main_var_expect_42.c").string(),
                  {(d / "override_data.c").string(),
                   (d / "lib_data.c").string()},
                  0);
}

TEST_F(OverrideTest, MultiOverridesNonLTO) {
  auto d = overrideDir();
  runOverrideTest("multi_overrides_nolto", "nolto", {},
                  (d / "main_multi.c").string(),
                  {(d / "override_multi.c").string(),
                   (d / "lib_multi.c").string()},
                  0);
}

TEST_F(OverrideTest, WeakOverrideNonLTO) {
  auto d = overrideDir();
  runOverrideTest("weak_override_nolto", "nolto", {},
                  (d / "main_expect_42.c").string(),
                  {(d / "override_compute_weak.c").string(),
                   (d / "lib_compute.c").string()},
                  0);
}

TEST_F(OverrideTest, IndirectNonLTO) {
  auto d = overrideDir();
  runOverrideTest("indirect_nolto", "nolto", {},
                  (d / "main_indirect.c").string(),
                  {(d / "lib_indirect.c").string(),
                   (d / "override_foo.c").string(),
                   (d / "lib_foo.c").string()},
                  0);
}

TEST_F(OverrideTest, HiddenVisibilityNonLTO) {
  auto d = overrideDir();
  runOverrideTest("hidden_visibility_nolto", "nolto", {},
                  (d / "main_expect_42.c").string(),
                  {(d / "override_hidden.c").string(),
                   (d / "lib_compute.c").string()},
                  0);
}

TEST_F(OverrideTest, SectionLeakCheck) {
  if (!isDarwin()) {
    GTEST_SKIP() << "section leak check requires macOS otool";
    return;
  }
  auto d = overrideDir();
  auto exe = tmpFile("basic_leak_check.exe");

  // Build a basic override binary
  auto obj1 = tmpFile("lc_main.o");
  auto obj2 = tmpFile("lc_ovr.o");
  auto obj3 = tmpFile("lc_lib.o");

  std::vector<std::string> base = {"-fno-lto"};
  for (auto &f : sysrootFlags()) base.push_back(f);
  for (auto &f : archFlags()) base.push_back(f);

  auto c1 = base;
  c1.insert(c1.end(), {"-c", (d / "main_expect_42.c").string(), "-o",
                       obj1.string()});
  ncc(c1);
  auto c2 = base;
  c2.insert(c2.end(), {"-c", (d / "override_compute.c").string(), "-o",
                       obj2.string()});
  ncc(c2);
  auto c3 = base;
  c3.insert(c3.end(),
            {"-c", (d / "lib_compute.c").string(), "-o", obj3.string()});
  ncc(c3);

  std::vector<std::string> link;
  for (auto &f : sysrootFlags()) link.push_back(f);
  for (auto &f : archFlags()) link.push_back(f);
  link.insert(link.end(),
              {"-fno-lto", obj1.string(), obj2.string(), obj3.string(), "-o",
               exe.string()});
  if (ncc(link).exitCode != 0) return;

  auto otool = exec("otool", {"-l", exe.string()});
  EXPECT_EQ(otool.out.find("__neverc_ovr"), std::string::npos)
      << "override section leaked into final binary";

  auto nm = exec("nm", {exe.string()});
  EXPECT_EQ(nm.out.find("__neverc_ovr"), std::string::npos)
      << "override marker symbol leaked";
}
