#include "NeverCTestFixture.h"

class DynCodeTest : public NeverCTest {};

// Pure computation tests
TEST_F(DynCodeTest, Add) {
  dyncodeTest("add", (testDir() / "dyncode/test_dyncode_add.c").string(),
                3, 4, 7);
}
TEST_F(DynCodeTest, Fib) {
  dyncodeTest("fib", (testDir() / "dyncode/test_dyncode_fib.c").string(),
                10, 0, 55);
}
TEST_F(DynCodeTest, Popcount) {
  dyncodeTest("popcount",
                (testDir() / "dyncode/test_dyncode_popcount.c").string(),
                255, 0, 8);
}
TEST_F(DynCodeTest, StringInline) {
  dyncodeTest(
      "neverc_string_inline",
      (testDir() / "dyncode/test_dyncode_string_inline.c").string(), 0, 0,
      131);
}

// NeverC builtin string pairs
TEST_F(DynCodeTest, StringBase) { dyncodeStringPair(""); }
TEST_F(DynCodeTest, StringSearch) { dyncodeStringPair("search"); }
TEST_F(DynCodeTest, StringSafety) { dyncodeStringPair("safety"); }
TEST_F(DynCodeTest, StringOverloads) { dyncodeStringPair("overloads"); }
TEST_F(DynCodeTest, StringCapacity) { dyncodeStringPair("capacity"); }
TEST_F(DynCodeTest, StringLifecycle) { dyncodeStringPair("lifecycle"); }
TEST_F(DynCodeTest, StringPassing) { dyncodeStringPair("passing"); }
TEST_F(DynCodeTest, StringCompare) { dyncodeStringPair("compare"); }
TEST_F(DynCodeTest, StringAssign) { dyncodeStringPair("assign"); }
TEST_F(DynCodeTest, StringEdge) { dyncodeStringPair("edge"); }
TEST_F(DynCodeTest, StringMethods) { dyncodeStringPair("methods"); }
TEST_F(DynCodeTest, StringArena) { dyncodeStringPair("arena"); }
TEST_F(DynCodeTest, StringFrag) { dyncodeStringPair("frag"); }
TEST_F(DynCodeTest, StringArith) { dyncodeStringPair("arith"); }
TEST_F(DynCodeTest, StringChain) { dyncodeStringPair("chain"); }
TEST_F(DynCodeTest, StringNoinline) { dyncodeStringPair("noinline"); }
TEST_F(DynCodeTest, StringSTLParity) { dyncodeStringPair("stl_parity"); }
TEST_F(DynCodeTest, StringUTF8) { dyncodeStringPair("utf8"); }
TEST_F(DynCodeTest, StringEncrypt) { dyncodeStringPair("encrypt"); }

// Struct/dispatch/table tests
TEST_F(DynCodeTest, Switch) {
  dyncodeTest("switch",
                (testDir() / "dyncode/test_dyncode_switch.c").string(), 2,
                20, 13);
}
TEST_F(DynCodeTest, Struct) {
  dyncodeTest("struct",
                (testDir() / "dyncode/test_dyncode_struct.c").string(), 5,
                9, 90);
}
TEST_F(DynCodeTest, Float) {
  dyncodeTest("float",
                (testDir() / "dyncode/test_dyncode_float.c").string(), 20,
                40, 82);
}
TEST_F(DynCodeTest, Dispatch) {
  dyncodeTest("dispatch",
                (testDir() / "dyncode/test_dyncode_dispatch.c").string(), 2,
                100, 156);
}
TEST_F(DynCodeTest, ConstStruct) {
  dyncodeTest(
      "const_struct",
      (testDir() / "dyncode/test_dyncode_const_struct.c").string(), 2, 0,
      30);
}
TEST_F(DynCodeTest, FnptrTable) {
  dyncodeTest(
      "fnptr_table",
      (testDir() / "dyncode/test_dyncode_fnptr_table.c").string(), 1, 6,
      42);
}
TEST_F(DynCodeTest, MixedTable) {
  dyncodeTest(
      "mixed_table",
      (testDir() / "dyncode/test_dyncode_mixed_table.c").string(), 1, 0,
      118);
}
TEST_F(DynCodeTest, Vtable) {
  dyncodeTest("vtable",
                (testDir() / "dyncode/test_dyncode_vtable.c").string(), 0,
                5, 25);
}
TEST_F(DynCodeTest, NestedTable) {
  dyncodeTest(
      "nested_table",
      (testDir() / "dyncode/test_dyncode_nested_table.c").string(), 0, 2,
      102);
}
TEST_F(DynCodeTest, LinkedList) {
  dyncodeTest(
      "linked_list",
      (testDir() / "dyncode/test_dyncode_linked_list.c").string(), 1, 0,
      30);
}
TEST_F(DynCodeTest, ThreadLocal) {
  dyncodeTest(
      "thread_local",
      (testDir() / "dyncode/test_dyncode_thread_local.c").string(), 0, 0,
      42);
}
TEST_F(DynCodeTest, ComputedGoto) {
  dyncodeTest(
      "computed_goto",
      (testDir() / "dyncode/test_dyncode_computed_goto.c").string(), 4, 0,
      200);
}
TEST_F(DynCodeTest, ComputedGotoDual) {
  dyncodeTest(
      "computed_goto_dual",
      (testDir() / "dyncode/test_dyncode_computed_goto_dual.c").string(), 1,
      1, 20);
}
TEST_F(DynCodeTest, AsmGoto) {
  if (!isArm64())
    GTEST_SKIP() << "asm_goto test uses AArch64 inline assembly";
  dyncodeTest("asm_goto",
                (testDir() / "dyncode/test_dyncode_asm_goto.c").string(), 0,
                0, 0);
}
TEST_F(DynCodeTest, SIMD) {
  dyncodeTest("simd",
                (testDir() / "dyncode/test_dyncode_simd.c").string(), 3, 4,
                43);
}
TEST_F(DynCodeTest, SIMDFP) {
  dyncodeTest("simd_fp",
                (testDir() / "dyncode/test_dyncode_simd_fp.c").string(), 3,
                4, 51);
}
TEST_F(DynCodeTest, BigNum) {
  dyncodeTest("bignum",
                (testDir() / "dyncode/test_dyncode_bignum.c").string(), 5,
                7, 155);
}
TEST_F(DynCodeTest, HugeStatic) {
  dyncodeTest(
      "huge_static",
      (testDir() / "dyncode/test_dyncode_huge_static.c").string(), 500, 0,
      11);
}
TEST_F(DynCodeTest, BitfieldConst) {
  dyncodeTest(
      "bitfield_const",
      (testDir() / "dyncode/test_dyncode_bitfield_const.c").string(), 2, 0,
      36);
}
TEST_F(DynCodeTest, Const3D) {
  dyncodeTest("const_3d",
                (testDir() / "dyncode/test_dyncode_const_3d.c").string(), 1,
                1, 19);
}
TEST_F(DynCodeTest, RichStruct) {
  dyncodeTest(
      "rich_struct",
      (testDir() / "dyncode/test_dyncode_rich_struct.c").string(), 0, 5,
      120);
}
TEST_F(DynCodeTest, CrossRef) {
  dyncodeTest("cross_ref",
                (testDir() / "dyncode/test_dyncode_cross_ref.c").string(),
                2, 0, 1);
}
TEST_F(DynCodeTest, MemsetLike) {
  dyncodeTest(
      "memset_like",
      (testDir() / "dyncode/test_dyncode_memset_like.c").string(), 0, 0,
      30);
}
TEST_F(DynCodeTest, BigConst) {
  dyncodeTest("big_const",
                (testDir() / "dyncode/test_dyncode_big_const.c").string(),
                0, 0, 128);
}
TEST_F(DynCodeTest, MutableGlobal) {
  dyncodeTest(
      "mutable_global",
      (testDir() / "dyncode/test_dyncode_mutable_global.c").string(), 0, 0,
      27);
}

// Rejection tests
TEST_F(DynCodeTest, RejectLTO) {
  dyncodeExpectFail(
      "reject_lto", (testDir() / "dyncode/test_dyncode_add.c").string(),
      "LTO emits bitcode", {"-flto"});
}
TEST_F(DynCodeTest, RejectSanitize) {
  dyncodeExpectFail(
      "reject_sanitize",
      (testDir() / "dyncode/test_dyncode_add.c").string(),
      "sanitizers require a runtime", {"-fsanitize=address"});
}
TEST_F(DynCodeTest, RejectStackProtector) {
  dyncodeExpectFail(
      "reject_stack_protector",
      (testDir() / "dyncode/test_dyncode_add.c").string(),
      "__stack_chk_guard", {"-fstack-protector-all"});
}

TEST_F(DynCodeTest, RejectBadContext) {
  auto src = (testDir() / "dyncode/test_dyncode_add.c").string();
  auto bin = tmpFile("reject_bad_context.bin");
  auto r = ncc({"-fdyncode", "-mdyncode-context=driver", src, "-o",
                bin.string()});
  EXPECT_NE(r.exitCode, 0);
  EXPECT_TRUE(r.stderrContains("expects 'user' or 'kernel'") ||
              r.contains("expects 'user' or 'kernel'"));
}

// Size regression guards
TEST_F(DynCodeTest, SizeRegression) {
  if (!isDarwin() || !isArm64()) {
    GTEST_SKIP() << "size checks require arm64 macOS";
    return;
  }
  auto shellDir = testDir() / "dyncode";
  struct SizeCase {
    const char *name;
    const char *file;
    size_t maxSize;
  };
  SizeCase cases[] = {
      {"add", "test_dyncode_add.c", 32},
      {"fib", "test_dyncode_fib.c", 256},
      {"popcount", "test_dyncode_popcount.c", 128},
      {"big_const", "test_dyncode_big_const.c", 2048},
  };
  for (auto &c : cases) {
    SCOPED_TRACE(c.name);
    auto bin = tmpFile(std::string(c.name) + "_size.bin");
    auto r = ncc({"-fdyncode", (shellDir / c.file).string(), "-o",
                  bin.string()});
    if (r.exitCode != 0) continue;
    auto sz = fileSize(bin);
    EXPECT_LE(sz, c.maxSize)
        << c.name << ": " << sz << "B > " << c.maxSize << "B";
  }
}

// Finalize flag tests
TEST_F(DynCodeTest, FinalizePadWithoutSize) {
  dyncodeExpectFail(
      "pad_without_size",
      (testDir() / "dyncode/test_dyncode_add.c").string(),
      "requires at least one of -fdyncode-align", {"-fdyncode-pad=00"});
}

TEST_F(DynCodeTest, FinalizeAlignNotPow2) {
  dyncodeExpectFail(
      "align_not_pow2",
      (testDir() / "dyncode/test_dyncode_add.c").string(),
      "must be a power of two", {"-fdyncode-align=3"});
}

TEST_F(DynCodeTest, FinalizeMaxLengthZero) {
  dyncodeExpectFail(
      "max_length_zero",
      (testDir() / "dyncode/test_dyncode_add.c").string(),
      "expects a positive byte count", {"-fdyncode-max-length=0"});
}

TEST_F(DynCodeTest, FinalizeAlign16) {
  auto src = (testDir() / "dyncode/test_dyncode_add.c").string();
  auto bin = tmpFile("align_16.bin");
  auto r = ncc({"-fdyncode", "-fdyncode-align=16", src, "-o",
                bin.string()});
  if (r.exitCode != 0) return;
  auto sz = fileSize(bin);
  EXPECT_EQ(sz % 16, 0u) << "size " << sz << " not aligned to 16";
  EXPECT_GT(sz, 0u);
}

TEST_F(DynCodeTest, FinalizeMaxLengthTooSmall) {
  // The size budget is now enforced by the DynCodeImage as it is populated, so
  // an over-budget payload is rejected with the image-level diagnostic rather
  // than a late driver-side check.
  dyncodeExpectFail(
      "max_length_too_small",
      (testDir() / "dyncode/test_dyncode_add.c").string(),
      "exceeds the image size budget", {"-fdyncode-max-length=2"});
}

TEST_F(DynCodeTest, FinalizeBadByteAuditFail) {
  // The forbidden set must contain a byte that is guaranteed to appear in the
  // emitted payload on every host arch: x86_64 codegen for `a + b` contains no
  // 0x00 byte (it does on AArch64), but it always ends in 0xC3 (`ret`); AArch64
  // contains 0x00. Forbidding both makes the audit deterministically trip
  // regardless of the default target (x86_64 on Windows/Linux, arm64 on macOS).
  // The sealed binary verifier now names the exact forbidden byte and offset it
  // caught in the final image; disabling the rewrite cannot smuggle it past the
  // audit.
  dyncodeExpectFail(
      "bad_byte_audit_fail",
      (testDir() / "dyncode/test_dyncode_add.c").string(),
      "forbidden byte",
      {"-fdyncode-bad-bytes=00,c3", "-fno-dyncode-bad-byte-rewrite"});
}

// HeapArenaPass tests
TEST_F(DynCodeTest, HeapArenaMalloc) {
  dyncodeTest(
      "heap_arena_malloc",
      (testDir() / "dyncode/test_dyncode_heap_arena.c").string(), 0, 0,
      10);
}
TEST_F(DynCodeTest, HeapArenaCalloc) {
  dyncodeTest(
      "heap_arena_calloc",
      (testDir() / "dyncode/test_dyncode_heap_calloc.c").string(), 0, 0,
      0);
}
TEST_F(DynCodeTest, HeapArenaRealloc) {
  dyncodeTest(
      "heap_arena_realloc",
      (testDir() / "dyncode/test_dyncode_heap_realloc.c").string(), 0, 0,
      15);
}
TEST_F(DynCodeTest, HeapArenaMulti) {
  dyncodeTest(
      "heap_arena_multi",
      (testDir() / "dyncode/test_dyncode_heap_multi.c").string(), 0, 0,
      42);
}
TEST_F(DynCodeTest, HeapArenaCrossCompile) {
  dyncodeCrossCompile(
      "heap_arena_cross",
      (testDir() / "dyncode/test_dyncode_heap_arena.c").string());
}
TEST_F(DynCodeTest, HeapArenaDisabled) {
  if (isWindows()) {
    GTEST_SKIP() << "Windows resolves malloc/free via the PEB ProcessHeap "
                    "(WinPEBImportPass / RtlAllocateHeap) independently of the "
                    "HeapArenaPass, so -fno-dyncode-heap-arena leaves no "
                    "undefined heap allocator symbol to diagnose";
    return;
  }
  dyncodeExpectFail(
      "heap_arena_disabled",
      (testDir() / "dyncode/test_dyncode_heap_arena.c").string(),
      "heap allocator call emitted",
      {"-fno-dyncode-heap-arena"});
}
TEST_F(DynCodeTest, HeapArenaReallocWithSyscallFallback) {
  ASSERT_TRUE(dyncodeCompileOnly(
      "heap_arena_realloc_syscall",
      (testDir() / "dyncode/test_dyncode_heap_realloc.c").string(),
      {"-mdyncode-syscall"}));
}
TEST_F(DynCodeTest, HeapArenaCrossCompileRealloc) {
  dyncodeCrossCompile(
      "heap_arena_realloc_cross",
      (testDir() / "dyncode/test_dyncode_heap_realloc.c").string());
}
TEST_F(DynCodeTest, HeapArenaBuiltinCalloc) {
  dyncodeTest(
      "heap_arena_builtin_calloc",
      (testDir() / "dyncode/test_dyncode_heap_builtin_calloc.c").string(),
      0, 0, 0);
}
