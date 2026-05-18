#include "NeverCTestFixture.h"

class LinkerTest : public NeverCTest {};

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
