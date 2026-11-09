#include "NativeBuiltinFixture.h"

#include <iterator>
#include <regex>
#include <utility>
#include <vector>

class X86PortableSystemTest : public X86NativeBuiltinTest {};

TEST_F(X86PortableSystemTest, BoundedTranslationInterfacesRetainNativeEffects) {
  const auto Source = tmpFile("translation-control.c");
  const auto IR = tmpFile("translation-control.ll");
  for (const bool Wide : {false, true}) {
    const std::string Word = Wide ? "unsigned long long" : "unsigned";
    const std::string Bits = Wide ? "64" : "32";
    writeFile(Source, Word + " probe(" + Word +
                          " control, unsigned ids, "
                          "unsigned count, " +
                          Word +
                          " *memory) {\n"
                          "  *memory = control;\n"
                          "  __builtin_ia32_tlbsync();\n"
                          "  __builtin_ia32_tlbsync_control" +
                          Bits +
                          "(control);\n"
                          "  __builtin_ia32_tlbsync_control" +
                          Bits +
                          "(control);\n"
                          "  __builtin_ia32_invlpgb" +
                          (Wide ? "" : "32") +
                          "(control, ids, count);\n"
                          "  __builtin_ia32_vmfunc_eptp_switch(count);\n"
                          "  return *memory;\n}\n");
    for (const char *Opt : {"-O0", "-O2"}) {
      const auto Result = ncc(
          {std::string("--target=") +
               (Wide ? "x86_64-unknown-linux-gnu" : "i386-unknown-linux-gnu"),
           Opt, "-ffreestanding", "-fno-lto", "-fno-builtin-mimalloc",
           "-Werror", "-S", "-emit-llvm", Source.string(), "-o", IR.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      const auto Text = readFile(IR);
      const std::regex Call("call void @llvm\\.x86\\.tlbsync\\.control" + Bits +
                            "\\(");
      EXPECT_EQ(
          std::distance(std::sregex_iterator(Text.begin(), Text.end(), Call),
                        std::sregex_iterator()),
          2)
          << Text;
      EXPECT_NE(Text.find("declare void @llvm.x86.tlbsync.control" + Bits +
                          "(i" + Bits + ")"),
                std::string::npos)
          << Text;
      EXPECT_NE(Text.find("@llvm.x86.invlpgb" + std::string(Wide ? "" : "32") +
                          "(i" + Bits),
                std::string::npos)
          << Text;
      EXPECT_NE(Text.find("@llvm.x86.vmfunc.eptp.switch(i32"),
                std::string::npos);
      EXPECT_NE(Text.find("load i" + Bits), std::string::npos) << Text;
      for (const char *Invalid :
           {" asm ", "memory(none)", "noreturn", "unreachable"})
        EXPECT_EQ(Text.find(Invalid), std::string::npos) << Text;
    }
  }
}

TEST_F(X86PortableSystemTest, TranslationControlRejectsWrongCodeMode) {
  const auto Source = tmpFile("translation-mode.c");
  for (const auto &[Triple, Call] :
       std::vector<std::pair<const char *, const char *>>{
           {"x86_64-unknown-linux-gnu", "__builtin_ia32_tlbsync_control32(1)"},
           {"x86_64-unknown-linux-gnu", "__builtin_ia32_invlpgb32(1,2,3)"},
           {"i386-unknown-linux-gnu", "__builtin_ia32_tlbsync_control64(1)"},
           {"i386-unknown-linux-gnu", "__builtin_ia32_invlpgb(1,2,3)"},
           {"i386-unknown-linux-gnu",
            "__builtin_ia32_invlpgb_addr32(1,2,3)"}}) {
    SCOPED_TRACE(Triple);
    SCOPED_TRACE(Call);
    writeFile(Source, "void probe(void) { " + std::string(Call) + "; }\n");
    const auto Result = ncc(
        {std::string("--target=") + Triple, "-fsyntax-only", Source.string()});
    EXPECT_NE(Result.exitCode, 0) << Result.err;
    EXPECT_NE(Result.err.find(Triple[0] == 'x' ? "requires a 32-bit x86 target"
                                               : "unknown builtin"),
              std::string::npos)
        << Result.err;
  }
}

TEST_F(X86PortableSystemTest, I386CpuidHeaderUsesNativeAvailabilityProbe) {
  const auto Source = tmpFile("cpuid-availability.c");
  const auto IR = tmpFile("cpuid-availability.ll");
  writeFile(Source, "#include <cpuid.h>\n"
                    "unsigned probe(unsigned leaf, unsigned *sig) {\n"
                    "  return __get_cpuid_max(leaf, sig);\n}\n");
  for (const char *Opt : {"-O0", "-O2"}) {
    const auto Result =
        ncc({"--target=i386-unknown-linux-gnu", Opt, "-ffreestanding",
             "-fno-lto", "-fno-builtin-mimalloc", "-S", "-emit-llvm",
             Source.string(), "-o", IR.string()});
    ASSERT_EQ(Result.exitCode, 0) << Result.err;
    const auto Text = readFile(IR);
    for (const char *Name : {"cpuidex", "flags.read.u32", "flags.write.u32"})
      EXPECT_NE(Text.find(std::string("@llvm.x86.") + Name + "("),
                std::string::npos)
          << Text;
    EXPECT_EQ(Text.find(" asm "), std::string::npos) << Text;
  }
}

TEST_F(X86PortableSystemTest, BothCodeModesAdmitNativeSystemInterfaces) {
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
  for (const char *Triple :
       {"i386-unknown-linux-gnu", "x86_64-unknown-linux-gnu",
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

TEST_F(X86PortableSystemTest, I386InterruptValidatesBeforeNarrowing) {
  const auto Source = tmpFile("interrupt-byte.c");
  const auto IR = tmpFile("interrupt-byte.ll");
  for (const char *Vector : {"0", "1", "3", "4", "44", "128", "255", "v", "-1",
                             "256", "0xffffffffU"}) {
    SCOPED_TRACE(Vector);
    writeFile(Source, "void probe(unsigned v) { __builtin_ia32_int(" +
                          std::string(Vector) + "); }\n");
    const auto Result =
        ncc({"--target=i386-unknown-linux-gnu", "-O2", "-ffreestanding",
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
