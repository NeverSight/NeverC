#include "NeverCTestFixture.h"

class LTOTest : public NeverCTest {};

TEST_F(LTOTest, HelloLTO) {
  auto src = (testDir() / "lto/hello_lto.c").string();
  auto obj = tmpFile("hello_lto.o");
  auto exe = tmpFile("hello_lto");

  std::vector<std::string> base = {"-std=c11"};
  for (auto &f : sysrootFlags()) base.push_back(f);
  for (auto &f : archFlags()) base.push_back(f);

  auto c = base;
  c.insert(c.end(), {"-flto", "-c", src, "-o", obj.string()});
  ASSERT_EQ(ncc(c).exitCode, 0);

  auto l = base;
  l.erase(l.begin()); // remove -std=c11 for link
  l.insert(l.end(), {"-flto", obj.string(), "-o", exe.string()});
  ASSERT_EQ(ncc(l).exitCode, 0);

  auto r = exec(exe.string(), {});
  EXPECT_EQ(r.exitCode, 3) << "hello_lto should exit 3";
}

TEST_F(LTOTest, MultiTU_AB) {
  auto ltoDir = testDir() / "lto";
  auto objA = tmpFile("lto_a.o");
  auto objB = tmpFile("lto_b.o");
  auto exe = tmpFile("lto_ab");

  std::vector<std::string> base = {"-std=c11"};
  for (auto &f : sysrootFlags()) base.push_back(f);
  for (auto &f : archFlags()) base.push_back(f);

  auto a1 = base;
  a1.insert(a1.end(),
            {"-flto", "-c", (ltoDir / "test_lto_a.c").string(), "-o",
             objA.string()});
  ASSERT_EQ(ncc(a1).exitCode, 0);

  auto a2 = base;
  a2.insert(a2.end(),
            {"-flto", "-c", (ltoDir / "test_lto_b.c").string(), "-o",
             objB.string()});
  ASSERT_EQ(ncc(a2).exitCode, 0);

  std::vector<std::string> link;
  for (auto &f : sysrootFlags()) link.push_back(f);
  for (auto &f : archFlags()) link.push_back(f);
  link.insert(link.end(),
              {"-flto", objA.string(), objB.string(), "-o", exe.string()});
  ASSERT_EQ(ncc(link).exitCode, 0);

  auto r = exec(exe.string(), {});
  EXPECT_EQ(r.exitCode, 0);
  EXPECT_TRUE(r.contains("add(3,4)=7"));
}

TEST_F(LTOTest, InlineAsmLTO) {
  auto asmDir = testDir() / "asm";
  auto objMain = tmpFile("asm_lto_main.o");
  auto objHelper = tmpFile("asm_lto_helper.o");
  auto exe = tmpFile("asm_lto");

  std::vector<std::string> base = {"-std=gnu11"};
  for (auto &f : sysrootFlags()) base.push_back(f);
  for (auto &f : archFlags()) base.push_back(f);

  auto a1 = base;
  a1.insert(a1.end(),
            {"-flto", "-c", (asmDir / "test_inline_asm_lto_main.c").string(),
             "-o", objMain.string()});
  ASSERT_EQ(ncc(a1).exitCode, 0);

  auto a2 = base;
  a2.insert(a2.end(),
            {"-flto", "-c", (asmDir / "test_inline_asm_lto_helper.c").string(),
             "-o", objHelper.string()});
  ASSERT_EQ(ncc(a2).exitCode, 0);

  std::vector<std::string> link;
  for (auto &f : sysrootFlags()) link.push_back(f);
  for (auto &f : archFlags()) link.push_back(f);
  link.insert(link.end(), {"-flto", objMain.string(), objHelper.string(), "-o",
                           exe.string()});
  ASSERT_EQ(ncc(link).exitCode, 0);

  auto r = exec(exe.string(), {});
  EXPECT_EQ(r.exitCode, 0);
  EXPECT_TRUE(r.contains("test_inline_asm_lto: ALL PASSED"));
}

TEST_F(LTOTest, InlineAsmGCCWithLTO) {
  auto src = (testDir() / "asm/test_inline_asm_gcc.c").string();
  auto obj = tmpFile("inline_asm_gcc_lto.o");
  auto exe = tmpFile("inline_asm_gcc_lto");

  std::vector<std::string> base = {"-std=gnu11"};
  for (auto &f : sysrootFlags()) base.push_back(f);
  for (auto &f : archFlags()) base.push_back(f);

  auto c = base;
  c.insert(c.end(), {"-flto", "-c", src, "-o", obj.string()});
  ASSERT_EQ(ncc(c).exitCode, 0);

  std::vector<std::string> link;
  for (auto &f : sysrootFlags()) link.push_back(f);
  for (auto &f : archFlags()) link.push_back(f);
  link.insert(link.end(), {"-flto", obj.string(), "-o", exe.string()});
  ASSERT_EQ(ncc(link).exitCode, 0);

  auto r = exec(exe.string(), {});
  EXPECT_EQ(r.exitCode, 0);
  EXPECT_TRUE(r.contains("test_inline_asm_gcc: ALL PASSED"));
}
