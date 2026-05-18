#include "NeverCTestFixture.h"

class HostTest : public NeverCTest {};

// C standards
TEST_F(HostTest, C99) {
  compileRunAndCheck("test_c99", (testDir() / "standards/test_c99.c").string(),
                     "-std=c99", 0, "");
}
TEST_F(HostTest, CFeatures) {
  compileRunAndCheck("c_features",
                     (testDir() / "standards/c_features.c").string(),
                     "-std=c11", 0, "");
}
TEST_F(HostTest, C23Features) {
  compileRunAndCheck("c23_features",
                     (testDir() / "standards/c23_features.c").string(),
                     "-std=c23", 0, "");
}
TEST_F(HostTest, GNUExtensions) {
  compileRunAndCheck("test_gnu_extensions",
                     (testDir() / "gnu/test_gnu_extensions.c").string(),
                     "-std=gnu11", 0, "");
}
TEST_F(HostTest, KernelPatterns) {
  compileRunAndCheck("test_kernel_patterns",
                     (testDir() / "kernel/test_kernel_patterns.c").string(),
                     "-std=gnu11", 0, "");
}
TEST_F(HostTest, KernelAdvanced) {
  compileRunAndCheck("test_kernel_advanced",
                     (testDir() / "kernel/test_kernel_advanced.c").string(),
                     "-std=gnu11", 0, "test_kernel_advanced: ALL PASSED");
}
TEST_F(HostTest, KernelAdvanced2) {
  compileRunAndCheck(
      "test_kernel_advanced2",
      (testDir() / "kernel/test_kernel_advanced2.c").string(),
      "-std=gnu11 -Wno-zero-length-array", 0,
      "test_kernel_advanced2: ALL PASSED");
}
TEST_F(HostTest, KernelSUPatterns) {
  compileRunAndCheck(
      "test_kernelsu_patterns",
      (testDir() / "kernel/test_kernelsu_patterns.c").string(), "-std=gnu11",
      0, "test_kernelsu_patterns: ALL PASSED");
}
TEST_F(HostTest, C11Atomics) {
  compileRunAndCheck("test_c11_atomics",
                     (testDir() / "standards/test_c11_atomics.c").string(),
                     "-std=c11", 0, "test_c11_atomics: ALL PASSED");
}
TEST_F(HostTest, GNUAdvanced) {
  compileRunAndCheck("test_gnu_advanced",
                     (testDir() / "gnu/test_gnu_advanced.c").string(),
                     "-std=gnu11", 0, "test_gnu_advanced: ALL PASSED");
}
TEST_F(HostTest, C23Advanced) {
  compileRunAndCheck("test_c23_advanced",
                     (testDir() / "standards/test_c23_advanced.c").string(),
                     "-std=c23", 0, "test_c23_advanced: ALL PASSED");
}
TEST_F(HostTest, CStdlibPatterns) {
  compileRunAndCheck(
      "test_c_stdlib_patterns",
      (testDir() / "standards/test_c_stdlib_patterns.c").string(), "-std=c11",
      0, "test_c_stdlib_patterns: ALL PASSED");
}
TEST_F(HostTest, CRealWorld) {
  compileRunAndCheck("test_c_real_world",
                     (testDir() / "standards/test_c_real_world.c").string(),
                     "-std=gnu11", 0, "test_c_real_world: ALL PASSED");
}
TEST_F(HostTest, InlineAsmGCC) {
  compileRunAndCheck("test_inline_asm_gcc",
                     (testDir() / "asm/test_inline_asm_gcc.c").string(),
                     "-std=gnu11", 0, "test_inline_asm_gcc: ALL PASSED");
}
TEST_F(HostTest, UdivConstOpt) {
  compileRunAndCheck("test_udiv_const_opt",
                     (testDir() / "codegen/test_udiv_const_opt.c").string(),
                     "-std=c11 -O2", 0, "test_udiv_const_opt: ALL PASSED");
}

// Codegen tests — each with O2 and often O0
#define CODEGEN_TEST(Name, File, Grep)                                         \
  TEST_F(HostTest, Name) {                                                     \
    compileRunAndCheck(#Name, (testDir() / "codegen" / File).string(),          \
                       "-std=gnu11 -O2", 0, Grep);                             \
  }                                                                            \
  TEST_F(HostTest, Name##_O0) {                                                \
    compileRunAndCheck(#Name "_O0", (testDir() / "codegen" / File).string(),    \
                       "-std=gnu11 -O0", 0, Grep);                             \
  }

#define CODEGEN_TEST_O2_ONLY(Name, File, Grep)                                 \
  TEST_F(HostTest, Name) {                                                     \
    compileRunAndCheck(#Name, (testDir() / "codegen" / File).string(),          \
                       "-std=gnu11 -O2", 0, Grep);                             \
  }

CODEGEN_TEST_O2_ONLY(Arch64With32BitOps, "test_arch64_with_32bit_ops.c",
                      "test_arch64_with_32bit_ops: ALL PASSED")
CODEGEN_TEST_O2_ONLY(Bit64OnlyCleanup, "test_64bit_only_cleanup.c",
                      "test_64bit_only_cleanup: ALL PASSED")
CODEGEN_TEST_O2_ONLY(ObjFormat64Bit, "test_objformat_64bit.c",
                      "test_objformat_64bit: ALL PASSED")
CODEGEN_TEST_O2_ONLY(Ops32BitIn64Bit, "test_32bit_ops_in_64bit.c",
                      "test_32bit_ops_in_64bit: ALL PASSED")
CODEGEN_TEST_O2_ONLY(MC64BitFormat, "test_mc_64bit_format.c",
                      "test_mc_64bit_format: ALL PASSED")
CODEGEN_TEST_O2_ONLY(CMOVAndCleanup, "test_cmov_and_cleanup.c",
                      "test_cmov_and_cleanup: ALL PASSED")
CODEGEN_TEST_O2_ONLY(ArchCleanupRound2, "test_arch_cleanup_round2.c",
                      "test_arch_cleanup_round2: ALL PASSED")
CODEGEN_TEST_O2_ONLY(Arch64OnlyValidation, "test_arch_64only_validation.c",
                      "test_arch_64only_validation: ALL PASSED")
CODEGEN_TEST_O2_ONLY(MC64BitHardcode, "test_mc_64bit_hardcode.c",
                      "test_mc_64bit_hardcode: ALL PASSED")
CODEGEN_TEST_O2_ONLY(ARM64MCCleanup, "test_arm64_mc_cleanup.c",
                      "PASS: arm64_mc_cleanup")
CODEGEN_TEST_O2_ONLY(CMOVGuardRemoval, "test_cmov_guard_removal.c",
                      "PASS: cmov_guard_removal")
CODEGEN_TEST_O2_ONLY(CMOVAlwaysAvailable, "test_cmov_always_available.c",
                      "test_cmov_always_available: ALL PASSED")
CODEGEN_TEST_O2_ONLY(Cleanup32BitValidation, "test_32bit_cleanup_validation.c",
                      "PASS: 32bit_cleanup_validation")
CODEGEN_TEST_O2_ONLY(MC64BitAndArmCleanup, "test_mc_64bit_and_arm_cleanup.c",
                      "PASS: mc_64bit_and_arm_cleanup")
CODEGEN_TEST_O2_ONLY(MCConstructorThumbRemoval,
                      "test_mc_constructor_and_thumb_removal.c",
                      "PASS: mc_constructor_and_thumb_removal")
CODEGEN_TEST_O2_ONLY(CleanupFinalValidation,
                      "test_cleanup_final_validation.c",
                      "PASS: cleanup_final_validation")
CODEGEN_TEST_O2_ONLY(Format64BitOutput, "test_64bit_format_output.c",
                      "PASS: 64bit_format_output")
CODEGEN_TEST_O2_ONLY(ARM64_32BitOps, "test_arm64_32bit_ops.c",
                      "PASS: arm64_32bit_ops")

TEST_F(HostTest, NoCMOVPseudoRemoval) {
  compileRunAndCheck("test_nocmov_pseudo_removal",
                     (testDir() / "codegen/test_nocmov_pseudo_removal.c")
                         .string(),
                     "-std=gnu11 -O0", 0, "PASS: nocmov_pseudo_removal");
}

CODEGEN_TEST(Only64FinalValidation, "test_64only_final_validation.c",
             "PASS: 64only_final_validation")
CODEGEN_TEST(Only64Round3, "test_64only_round3.c", "PASS: 64only_round3")
CODEGEN_TEST(X64CleanupValidation, "test_x64_cleanup_validation.c",
             "PASS: x64_cleanup_validation")
CODEGEN_TEST(AArch64CleanupValidation, "test_aarch64_cleanup_validation.c",
             "PASS: aarch64_cleanup_validation")
CODEGEN_TEST(Arch64Final, "test_64bit_arch_final.c", "PASS: 64bit_arch_final")
CODEGEN_TEST(X64CMOVAnd32BitOps, "test_x64_cmov_and_32bit_ops.c",
             "test_x64_cmov_and_32bit_ops: ALL PASSED")
CODEGEN_TEST(AArch64_32BitOpsValidation,
             "test_aarch64_32bit_ops_validation.c",
             "test_aarch64_32bit_ops_validation: ALL PASSED")
CODEGEN_TEST(LEAAndStackCleanup, "test_lea_and_stack_cleanup.c",
             "PASS: lea_and_stack_cleanup")
CODEGEN_TEST(Comprehensive64BitValidation,
             "test_64bit_comprehensive_validation.c",
             "PASS: 64bit_comprehensive_validation")
CODEGEN_TEST(ArchCleanupComprehensive, "test_arch_cleanup_comprehensive.c",
             "test_arch_cleanup_comprehensive: ALL PASSED")
CODEGEN_TEST(CleanupFinal64Bit, "test_64bit_cleanup_final.c",
             "PASS: 64bit_cleanup_final")

TEST_F(HostTest, GenericCPUCMOV) {
  compileRunAndCheck("test_generic_cpu_cmov",
                     (testDir() / "codegen/test_generic_cpu_cmov.c").string(),
                     "-std=gnu11 -O2 -march=x86-64", 0,
                     "PASS: generic_cpu_cmov");
  compileRunAndCheck("test_generic_cpu_cmov_O0",
                     (testDir() / "codegen/test_generic_cpu_cmov.c").string(),
                     "-std=gnu11 -O0", 0, "PASS: generic_cpu_cmov");
}

TEST_F(HostTest, CrossCompile64Bit) {
  auto src = (testDir() / "codegen/test_crosscompile_64bit.c").string();
  for (auto *triple :
       {"x86_64-linux-gnu", "x86_64-pc-windows-msvc", "aarch64-linux-gnu"}) {
    SCOPED_TRACE(triple);
    auto obj = tmpFile(std::string("xcompile_") + triple + ".o");
    auto r = ncc({"-c", "-target", triple, "-O2", src, "-o", obj.string()});
    EXPECT_EQ(r.exitCode, 0) << "cross-compile " << triple << "\n" << r.err;
    EXPECT_GT(fileSize(obj), 0u) << "cross-compile " << triple << " empty .o";
  }
}
