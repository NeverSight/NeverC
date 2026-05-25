#include "NeverCTestFixture.h"

class ShellcodeTest : public NeverCTest {};

// Pure computation tests
TEST_F(ShellcodeTest, Add) {
  shellcodeTest("add", (testDir() / "shellcode/test_shellcode_add.c").string(),
                3, 4, 7);
}
TEST_F(ShellcodeTest, Fib) {
  shellcodeTest("fib", (testDir() / "shellcode/test_shellcode_fib.c").string(),
                10, 0, 55);
}
TEST_F(ShellcodeTest, Popcount) {
  shellcodeTest("popcount",
                (testDir() / "shellcode/test_shellcode_popcount.c").string(),
                255, 0, 8);
}
TEST_F(ShellcodeTest, StringInline) {
  shellcodeTest(
      "neverc_string_inline",
      (testDir() / "shellcode/test_shellcode_string_inline.c").string(), 0, 0,
      131);
}

// NeverC builtin string pairs
TEST_F(ShellcodeTest, StringBase) { shellcodeStringPair(""); }
TEST_F(ShellcodeTest, StringSearch) { shellcodeStringPair("search"); }
TEST_F(ShellcodeTest, StringSafety) { shellcodeStringPair("safety"); }
TEST_F(ShellcodeTest, StringOverloads) { shellcodeStringPair("overloads"); }
TEST_F(ShellcodeTest, StringCapacity) { shellcodeStringPair("capacity"); }
TEST_F(ShellcodeTest, StringLifecycle) { shellcodeStringPair("lifecycle"); }
TEST_F(ShellcodeTest, StringPassing) { shellcodeStringPair("passing"); }
TEST_F(ShellcodeTest, StringCompare) { shellcodeStringPair("compare"); }
TEST_F(ShellcodeTest, StringAssign) { shellcodeStringPair("assign"); }
TEST_F(ShellcodeTest, StringEdge) { shellcodeStringPair("edge"); }
TEST_F(ShellcodeTest, StringMethods) { shellcodeStringPair("methods"); }
TEST_F(ShellcodeTest, StringArena) { shellcodeStringPair("arena"); }
TEST_F(ShellcodeTest, StringFrag) { shellcodeStringPair("frag"); }
TEST_F(ShellcodeTest, StringArith) { shellcodeStringPair("arith"); }
TEST_F(ShellcodeTest, StringChain) { shellcodeStringPair("chain"); }
TEST_F(ShellcodeTest, StringNoinline) { shellcodeStringPair("noinline"); }
TEST_F(ShellcodeTest, StringSTLParity) { shellcodeStringPair("stl_parity"); }
TEST_F(ShellcodeTest, StringUTF8) { shellcodeStringPair("utf8"); }
TEST_F(ShellcodeTest, StringEncrypt) { shellcodeStringPair("encrypt"); }

// Struct/dispatch/table tests
TEST_F(ShellcodeTest, Switch) {
  shellcodeTest("switch",
                (testDir() / "shellcode/test_shellcode_switch.c").string(), 2,
                20, 13);
}
TEST_F(ShellcodeTest, Struct) {
  shellcodeTest("struct",
                (testDir() / "shellcode/test_shellcode_struct.c").string(), 5,
                9, 90);
}
TEST_F(ShellcodeTest, Float) {
  shellcodeTest("float",
                (testDir() / "shellcode/test_shellcode_float.c").string(), 20,
                40, 82);
}
TEST_F(ShellcodeTest, Dispatch) {
  shellcodeTest("dispatch",
                (testDir() / "shellcode/test_shellcode_dispatch.c").string(), 2,
                100, 156);
}
TEST_F(ShellcodeTest, ConstStruct) {
  shellcodeTest(
      "const_struct",
      (testDir() / "shellcode/test_shellcode_const_struct.c").string(), 2, 0,
      30);
}
TEST_F(ShellcodeTest, FnptrTable) {
  shellcodeTest(
      "fnptr_table",
      (testDir() / "shellcode/test_shellcode_fnptr_table.c").string(), 1, 6,
      42);
}
TEST_F(ShellcodeTest, MixedTable) {
  shellcodeTest(
      "mixed_table",
      (testDir() / "shellcode/test_shellcode_mixed_table.c").string(), 1, 0,
      118);
}
TEST_F(ShellcodeTest, Vtable) {
  shellcodeTest("vtable",
                (testDir() / "shellcode/test_shellcode_vtable.c").string(), 0,
                5, 25);
}
TEST_F(ShellcodeTest, NestedTable) {
  shellcodeTest(
      "nested_table",
      (testDir() / "shellcode/test_shellcode_nested_table.c").string(), 0, 2,
      102);
}
TEST_F(ShellcodeTest, LinkedList) {
  shellcodeTest(
      "linked_list",
      (testDir() / "shellcode/test_shellcode_linked_list.c").string(), 1, 0,
      30);
}
TEST_F(ShellcodeTest, ThreadLocal) {
  shellcodeTest(
      "thread_local",
      (testDir() / "shellcode/test_shellcode_thread_local.c").string(), 0, 0,
      42);
}
TEST_F(ShellcodeTest, ComputedGoto) {
  shellcodeTest(
      "computed_goto",
      (testDir() / "shellcode/test_shellcode_computed_goto.c").string(), 4, 0,
      200);
}
TEST_F(ShellcodeTest, ComputedGotoDual) {
  shellcodeTest(
      "computed_goto_dual",
      (testDir() / "shellcode/test_shellcode_computed_goto_dual.c").string(), 1,
      1, 20);
}
TEST_F(ShellcodeTest, AsmGoto) {
  shellcodeTest("asm_goto",
                (testDir() / "shellcode/test_shellcode_asm_goto.c").string(), 0,
                0, 0);
}
TEST_F(ShellcodeTest, SIMD) {
  shellcodeTest("simd",
                (testDir() / "shellcode/test_shellcode_simd.c").string(), 3, 4,
                43);
}
TEST_F(ShellcodeTest, SIMDFP) {
  shellcodeTest("simd_fp",
                (testDir() / "shellcode/test_shellcode_simd_fp.c").string(), 3,
                4, 51);
}
TEST_F(ShellcodeTest, BigNum) {
  shellcodeTest("bignum",
                (testDir() / "shellcode/test_shellcode_bignum.c").string(), 5,
                7, 155);
}
TEST_F(ShellcodeTest, HugeStatic) {
  shellcodeTest(
      "huge_static",
      (testDir() / "shellcode/test_shellcode_huge_static.c").string(), 500, 0,
      11);
}
TEST_F(ShellcodeTest, BitfieldConst) {
  shellcodeTest(
      "bitfield_const",
      (testDir() / "shellcode/test_shellcode_bitfield_const.c").string(), 2, 0,
      36);
}
TEST_F(ShellcodeTest, Const3D) {
  shellcodeTest("const_3d",
                (testDir() / "shellcode/test_shellcode_const_3d.c").string(), 1,
                1, 19);
}
TEST_F(ShellcodeTest, RichStruct) {
  shellcodeTest(
      "rich_struct",
      (testDir() / "shellcode/test_shellcode_rich_struct.c").string(), 0, 5,
      120);
}
TEST_F(ShellcodeTest, CrossRef) {
  shellcodeTest("cross_ref",
                (testDir() / "shellcode/test_shellcode_cross_ref.c").string(),
                2, 0, 1);
}
TEST_F(ShellcodeTest, MemsetLike) {
  shellcodeTest(
      "memset_like",
      (testDir() / "shellcode/test_shellcode_memset_like.c").string(), 0, 0,
      30);
}
TEST_F(ShellcodeTest, BigConst) {
  shellcodeTest("big_const",
                (testDir() / "shellcode/test_shellcode_big_const.c").string(),
                0, 0, 128);
}
TEST_F(ShellcodeTest, MutableGlobal) {
  shellcodeTest(
      "mutable_global",
      (testDir() / "shellcode/test_shellcode_mutable_global.c").string(), 0, 0,
      27);
}

// Rejection tests
TEST_F(ShellcodeTest, RejectLTO) {
  shellcodeExpectFail(
      "reject_lto", (testDir() / "shellcode/test_shellcode_add.c").string(),
      "LTO emits bitcode", {"-flto"});
}
TEST_F(ShellcodeTest, RejectSanitize) {
  shellcodeExpectFail(
      "reject_sanitize",
      (testDir() / "shellcode/test_shellcode_add.c").string(),
      "sanitizers require a runtime", {"-fsanitize=address"});
}
TEST_F(ShellcodeTest, RejectStackProtector) {
  shellcodeExpectFail(
      "reject_stack_protector",
      (testDir() / "shellcode/test_shellcode_add.c").string(),
      "__stack_chk_guard", {"-fstack-protector-all"});
}

TEST_F(ShellcodeTest, RejectBadContext) {
  auto src = (testDir() / "shellcode/test_shellcode_add.c").string();
  auto bin = tmpFile("reject_bad_context.bin");
  auto r = ncc({"-fshellcode", "-mshellcode-context=driver", src, "-o",
                bin.string()});
  EXPECT_NE(r.exitCode, 0);
  EXPECT_TRUE(r.stderrContains("expects 'user' or 'kernel'") ||
              r.contains("expects 'user' or 'kernel'"));
}

// Size regression guards
TEST_F(ShellcodeTest, SizeRegression) {
  if (!isDarwin() || !isArm64()) {
    GTEST_SKIP() << "size checks require arm64 macOS";
    return;
  }
  auto shellDir = testDir() / "shellcode";
  struct SizeCase {
    const char *name;
    const char *file;
    size_t maxSize;
  };
  SizeCase cases[] = {
      {"add", "test_shellcode_add.c", 32},
      {"fib", "test_shellcode_fib.c", 256},
      {"popcount", "test_shellcode_popcount.c", 128},
      {"big_const", "test_shellcode_big_const.c", 2048},
  };
  for (auto &c : cases) {
    SCOPED_TRACE(c.name);
    auto bin = tmpFile(std::string(c.name) + "_size.bin");
    auto r = ncc({"-fshellcode", (shellDir / c.file).string(), "-o",
                  bin.string()});
    if (r.exitCode != 0) continue;
    auto sz = fileSize(bin);
    EXPECT_LE(sz, c.maxSize)
        << c.name << ": " << sz << "B > " << c.maxSize << "B";
  }
}

// Finalize flag tests
TEST_F(ShellcodeTest, FinalizeCharsetUnknown) {
  shellcodeExpectFail(
      "charset_unknown",
      (testDir() / "shellcode/test_shellcode_add.c").string(),
      "no registered encoder", {"-fshellcode-charset=foo"});
}

TEST_F(ShellcodeTest, FinalizePadWithoutSize) {
  shellcodeExpectFail(
      "pad_without_size",
      (testDir() / "shellcode/test_shellcode_add.c").string(),
      "requires at least one of -fshellcode-align", {"-fshellcode-pad=00"});
}

TEST_F(ShellcodeTest, FinalizeAlignNotPow2) {
  shellcodeExpectFail(
      "align_not_pow2",
      (testDir() / "shellcode/test_shellcode_add.c").string(),
      "must be a power of two", {"-fshellcode-align=3"});
}

TEST_F(ShellcodeTest, FinalizeMaxLengthZero) {
  shellcodeExpectFail(
      "max_length_zero",
      (testDir() / "shellcode/test_shellcode_add.c").string(),
      "expects a positive byte count", {"-fshellcode-max-length=0"});
}

TEST_F(ShellcodeTest, FinalizeAlign16) {
  auto src = (testDir() / "shellcode/test_shellcode_add.c").string();
  auto bin = tmpFile("align_16.bin");
  auto r = ncc({"-fshellcode", "-fshellcode-align=16", src, "-o",
                bin.string()});
  if (r.exitCode != 0) return;
  auto sz = fileSize(bin);
  EXPECT_EQ(sz % 16, 0u) << "size " << sz << " not aligned to 16";
  EXPECT_GT(sz, 0u);
}

TEST_F(ShellcodeTest, FinalizeMaxLengthTooSmall) {
  shellcodeExpectFail(
      "max_length_too_small",
      (testDir() / "shellcode/test_shellcode_add.c").string(),
      "exceeds -fshellcode-max-length", {"-fshellcode-max-length=2"});
}

TEST_F(ShellcodeTest, FinalizeBadByteAuditFail) {
  shellcodeExpectFail(
      "bad_byte_audit_fail",
      (testDir() / "shellcode/test_shellcode_add.c").string(),
      "bad-byte audit failed",
      {"-fshellcode-bad-bytes=00", "-fno-shellcode-bad-byte-rewrite"});
}

// HeapArenaPass tests
TEST_F(ShellcodeTest, HeapArenaMalloc) {
  shellcodeTest(
      "heap_arena_malloc",
      (testDir() / "shellcode/test_shellcode_heap_arena.c").string(), 0, 0,
      10);
}
TEST_F(ShellcodeTest, HeapArenaCalloc) {
  shellcodeTest(
      "heap_arena_calloc",
      (testDir() / "shellcode/test_shellcode_heap_calloc.c").string(), 0, 0,
      0);
}
TEST_F(ShellcodeTest, HeapArenaRealloc) {
  shellcodeTest(
      "heap_arena_realloc",
      (testDir() / "shellcode/test_shellcode_heap_realloc.c").string(), 0, 0,
      15);
}
TEST_F(ShellcodeTest, HeapArenaMulti) {
  shellcodeTest(
      "heap_arena_multi",
      (testDir() / "shellcode/test_shellcode_heap_multi.c").string(), 0, 0,
      42);
}
TEST_F(ShellcodeTest, HeapArenaCrossCompile) {
  shellcodeCrossCompile(
      "heap_arena_cross",
      (testDir() / "shellcode/test_shellcode_heap_arena.c").string());
}
TEST_F(ShellcodeTest, HeapArenaDisabled) {
  shellcodeExpectFail(
      "heap_arena_disabled",
      (testDir() / "shellcode/test_shellcode_heap_arena.c").string(),
      "heap allocator call emitted",
      {"-fno-shellcode-heap-arena"});
}
TEST_F(ShellcodeTest, HeapArenaReallocWithSyscallFallback) {
  ASSERT_TRUE(shellcodeCompileOnly(
      "heap_arena_realloc_syscall",
      (testDir() / "shellcode/test_shellcode_heap_realloc.c").string(),
      {"-mshellcode-syscall"}));
}
TEST_F(ShellcodeTest, HeapArenaCrossCompileRealloc) {
  shellcodeCrossCompile(
      "heap_arena_realloc_cross",
      (testDir() / "shellcode/test_shellcode_heap_realloc.c").string());
}
TEST_F(ShellcodeTest, HeapArenaBuiltinCalloc) {
  shellcodeTest(
      "heap_arena_builtin_calloc",
      (testDir() / "shellcode/test_shellcode_heap_builtin_calloc.c").string(),
      0, 0, 0);
}
