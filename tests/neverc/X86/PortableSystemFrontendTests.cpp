#include "NativeBuiltinFixture.h"

#include <iterator>
#include <regex>
#include <utility>
#include <vector>

class X86PortableSystemTest : public X86NativeBuiltinTest {};

TEST_F(X86PortableSystemTest, BoundedTranslationInterfacesRetainNativeEffects) {
  const auto Source = tmpFile("translation-control.c");
  const auto IR = tmpFile("translation-control.ll");
  writeFile(Source, R"C(
unsigned long long probe(unsigned long long control, unsigned ids,
                         unsigned count, unsigned long long *memory) {
  *memory = control;
  __builtin_ia32_tlbsync();
  __builtin_ia32_tlbsync_control64(control);
  __builtin_ia32_tlbsync_control64(control);
  __builtin_ia32_invlpgb(control, ids, count);
  __builtin_ia32_vmfunc_eptp_switch(count);
  return *memory;
}
)C");
  for (const char *Target : {"x86_64-unknown-linux-gnu",
                             "x86_64-pc-windows-msvc", "x86_64-apple-darwin"}) {
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(Target);
      SCOPED_TRACE(Opt);
      const auto Result =
          ncc({std::string("--target=") + Target, Opt, "-ffreestanding",
               "-fno-lto", "-fno-builtin-mimalloc", "-Werror", "-S",
               "-emit-llvm", Source.string(), "-o", IR.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      const auto Text = readFile(IR);
      const std::regex Call("call void @llvm\\.x86\\.tlbsync\\.control64\\(");
      EXPECT_EQ(
          std::distance(std::sregex_iterator(Text.begin(), Text.end(), Call),
                        std::sregex_iterator()),
          2)
          << Text;
      for (const char *Expected :
           {"declare void @llvm.x86.tlbsync.control64(i64)",
            "@llvm.x86.invlpgb(i64", "@llvm.x86.vmfunc.eptp.switch(i32",
            "load i64"})
        EXPECT_NE(Text.find(Expected), std::string::npos) << Text;
      for (const char *Invalid :
           {" asm ", "memory(none)", "noreturn", "unreachable"})
        EXPECT_EQ(Text.find(Invalid), std::string::npos) << Text;
    }
  }
}

TEST_F(X86PortableSystemTest, NativeSystemInterfacesOnSupportedPlatforms) {
  const auto Source = tmpFile("portable-system.c");
  const auto IR = tmpFile("portable-system.ll");
  writeFile(Source, R"C(
#pragma clang diagnostic error "-Wincompatible-pointer-types-discards-qualifiers"
void effects(unsigned index, unsigned long long value, int *out,
             unsigned leaf, unsigned subleaf, unsigned short port,
             void *destination, const void *source, unsigned count) {
  __builtin_ia32_wrmsrns(index, value);
  __builtin_ia32_cpuidex(out, leaf, subleaf);
  __builtin_ia32_int(255);
  __builtin_ia32_inbytestring(port, destination, count);
  __builtin_ia32_inwordstring(port, destination, count);
  __builtin_ia32_indwordstring(port, destination, count);
  __builtin_ia32_outbytestring(port, source, count);
  __builtin_ia32_outwordstring(port, source, count);
  __builtin_ia32_outdwordstring(port, source, count);
}
)C");
  for (const char *Triple : {"x86_64-unknown-linux-gnu",
                             "x86_64-pc-windows-msvc", "x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(Triple);
      SCOPED_TRACE(Opt);
      const auto Result =
          ncc({std::string("--target=") + Triple, Opt, "-std=c23",
               "-ffreestanding", "-fno-lto", "-fno-builtin-mimalloc", "-Werror",
               "-S", "-emit-llvm", Source.string(), "-o", IR.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      const auto Text = readFile(IR);
      for (const char *Name : {"wrmsrns", "cpuidex", "int", "inbytestring",
                               "inwordstring", "indwordstring", "outbytestring",
                               "outwordstring", "outdwordstring"}) {
        const std::regex Call(std::string("call void @llvm\\.x86\\.") + Name +
                              "\\(");
        EXPECT_EQ(
            std::distance(std::sregex_iterator(Text.begin(), Text.end(), Call),
                          std::sregex_iterator()),
            1)
            << Name << '\n'
            << Text;
      }
      EXPECT_NE(Text.find("declare void @llvm.x86.wrmsrns(i32, i64)"),
                std::string::npos)
          << Text;
      EXPECT_NE(Text.find("declare void @llvm.x86.inwordstring(i16, ptr, i32)"),
                std::string::npos)
          << Text;
      for (const char *Invalid :
           {" asm ", "noreturn", "unreachable", "memory(none)"})
        EXPECT_EQ(Text.find(Invalid), std::string::npos) << Text;
      EXPECT_NE(Text.find("ret void"), std::string::npos) << Text;
    }
}

TEST_F(X86PortableSystemTest, InterruptValidatesBeforeNarrowing) {
  const auto Source = tmpFile("interrupt-byte.c");
  const auto IR = tmpFile("interrupt-byte.ll");
  for (const char *Vector : {"0", "1", "3", "4", "44", "128", "255", "v", "-1",
                             "256", "0xffffffffU"}) {
    SCOPED_TRACE(Vector);
    writeFile(Source, "void probe(unsigned v) { __builtin_ia32_int(" +
                          std::string(Vector) + "); }\n");
    const auto Result =
        ncc({"--target=x86_64-unknown-linux-gnu", "-O2", "-ffreestanding",
             "-fno-lto", "-fno-builtin-mimalloc", "-S", "-emit-llvm",
             Source.string(), "-o", IR.string()});
    const bool Valid =
        std::string(Vector) != "v" && std::string(Vector) != "-1" &&
        std::string(Vector) != "256" && std::string(Vector) != "0xffffffffU";
    EXPECT_EQ(Result.exitCode == 0, Valid) << Result.err;
    if (!Valid)
      EXPECT_NE(
          Result.err.find(std::string(Vector) == "v" ? "constant" : "range"),
          std::string::npos)
          << Result.err;
  }
}

TEST_F(X86PortableSystemTest, LegacyOnlyBuiltinsAreUnavailable) {
  const auto Source = tmpFile("removed-builtins.c");
  std::string Text;
  for (const char *Name : {
           "__builtin_ia32_arpl_mem",
           "__builtin_ia32_arpl_mem_address32",
           "__builtin_ia32_arpl_reg",
           "__builtin_ia32_bndcl_address32",
           "__builtin_ia32_bndcl_reg32",
           "__builtin_ia32_bndcn_address32",
           "__builtin_ia32_bndcn_reg32",
           "__builtin_ia32_bndcu_address32",
           "__builtin_ia32_bndcu_reg32",
           "__builtin_ia32_bndldx32",
           "__builtin_ia32_bndmk32",
           "__builtin_ia32_bndmov_load32",
           "__builtin_ia32_bndmov_store32",
           "__builtin_ia32_bndmov_store_lock32",
           "__builtin_ia32_bndstx32",
           "__builtin_ia32_bound16",
           "__builtin_ia32_bound16_address32",
           "__builtin_ia32_bound32",
           "__builtin_ia32_bound32_address32",
           "__builtin_ia32_invept32",
           "__builtin_ia32_invept_address32",
           "__builtin_ia32_invlpga32",
           "__builtin_ia32_invlpga_addr16",
           "__builtin_ia32_invlpgb32",
           "__builtin_ia32_invvpid32",
           "__builtin_ia32_invvpid_address32",
           "__builtin_ia32_lds16",
           "__builtin_ia32_lds32",
           "__builtin_ia32_les16",
           "__builtin_ia32_les32",
           "__builtin_ia32_movsb32",
           "__builtin_ia32_movsd_rep32",
           "__builtin_ia32_movsw32",
           "__builtin_ia32_readcr32",
           "__builtin_ia32_readdr32",
           "__builtin_ia32_readeflags_u32",
           "__builtin_ia32_rep_cmps_state32",
           "__builtin_ia32_rep_ins_state32",
           "__builtin_ia32_rep_lods_state32",
           "__builtin_ia32_rep_movs_state32",
           "__builtin_ia32_rep_outs_state32",
           "__builtin_ia32_rep_scas_state32",
           "__builtin_ia32_rep_stos_state32",
           "__builtin_ia32_stosb32",
           "__builtin_ia32_stosd32",
           "__builtin_ia32_stosw32",
           "__builtin_ia32_tlbsync_control32",
           "__builtin_ia32_vmclear_address32",
           "__builtin_ia32_vmload32",
           "__builtin_ia32_vmload_addr16",
           "__builtin_ia32_vmptrld_address32",
           "__builtin_ia32_vmptrst_address32",
           "__builtin_ia32_vmread32",
           "__builtin_ia32_vmread_address32",
           "__builtin_ia32_vmread_reg32",
           "__builtin_ia32_vmsave32",
           "__builtin_ia32_vmsave_addr16",
           "__builtin_ia32_vmwrite32",
           "__builtin_ia32_vmwrite_mem32",
           "__builtin_ia32_vmwrite_mem_address32",
           "__builtin_ia32_vmxon_address32",
           "__builtin_ia32_writecr32",
           "__builtin_ia32_writedr32",
           "__builtin_ia32_writeeflags_u32",
           "__builtin_ia32_lcall16_imm",
           "__builtin_ia32_lcall32_imm",
           "__builtin_ia32_ljmp16_imm",
           "__builtin_ia32_ljmp32_imm",
       }) {
    Text += "#if __has_builtin(" + std::string(Name) +
            ")\n#error unexpected legacy builtin\n#endif\n";
  }
  Text += "#ifdef __NEVERC_X86_REP_STATE32__\n#error legacy REP API\n#endif\n";
  writeFile(Source, Text);
  for (const char *Triple :
       {"x86_64-unknown-linux-gnu", "x86_64-pc-windows-msvc",
        "x86_64-apple-darwin", "aarch64-unknown-linux-gnu"}) {
    SCOPED_TRACE(Triple);
    const auto Result = ncc(
        {std::string("--target=") + Triple, "-fsyntax-only", Source.string()});
    EXPECT_EQ(Result.exitCode, 0) << Result.err;
  }
}
