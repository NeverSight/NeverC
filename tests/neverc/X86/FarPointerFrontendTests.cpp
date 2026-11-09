//===- FarPointerFrontendTests.cpp - Native segment loads ----------------===//
//
// NeverC Compiler
//
//===----------------------------------------------------------------------===//

#include "../NeverCTestFixture.h"

#include <array>
#include <string>
#include <vector>

class X86FarPointerTest : public NeverCTest {};

namespace {
constexpr std::array<const char *, 3> Operations = {"lfs", "lgs", "lss"};
const char *type(unsigned Bits) {
  return Bits == 16   ? "unsigned short"
         : Bits == 32 ? "unsigned int"
                      : "unsigned long long";
}
} // namespace

TEST_F(X86FarPointerTest, PackedLoadsRetainNativeIdentityAndUnusedEffects) {
  const auto Source = tmpFile("far-pointer.c");
  std::string Text;
  for (const char *Op : Operations)
    for (unsigned Bits : {16, 32, 64}) {
      const auto Name = std::string(Op) + std::to_string(Bits);
      Text += "#if !__has_builtin(__builtin_ia32_" + Name +
              ")\n#error missing\n#endif\n";
      Text += std::string(type(Bits)) + " probe_" + Name +
              "(const void *p, unsigned *marker) {\n";
      Text += "_Static_assert(_Generic(__builtin_ia32_" + Name + "(p), " +
              type(Bits) + ": 1, default: 0));\n";
      Text += "*marker = 17;\n" + std::string(type(Bits)) +
              " result = __builtin_ia32_" + Name + "(p);\n";
      Text +=
          "__builtin_ia32_" + Name + "(p);\n*marker = 23;\nreturn result;\n}\n";
    }
  writeFile(Source, Text);
  for (const char *Target : {"x86_64-unknown-linux-gnu",
                             "x86_64-pc-windows-msvc", "x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"})
      for (const char *OutputKind : {"ir", "att", "intel"}) {
        SCOPED_TRACE(Target);
        SCOPED_TRACE(Opt);
        SCOPED_TRACE(OutputKind);
        const bool IR = std::string(OutputKind) == "ir";
        const auto Output = tmpFile(IR ? "far-pointer.ll" : "far-pointer.s");
        std::vector<std::string> Args = {std::string("--target=") + Target,
                                         Opt,
                                         "-fno-builtin-mimalloc",
                                         "-fno-lto",
                                         "-ffreestanding",
                                         "-mllvm",
                                         "-verify-machineinstrs",
                                         "-S",
                                         Source.string(),
                                         "-o",
                                         Output.string()};
        Args.push_back(IR ? "-emit-llvm" : std::string("-masm=") + OutputKind);
        const auto Result = ncc(Args);
        ASSERT_EQ(Result.exitCode, 0) << Result.err;
        const auto Emitted = readFile(Output);
        EXPECT_EQ(Emitted.find(IR ? " asm " : "#APP"), std::string::npos);
        for (const char *Op : Operations)
          for (unsigned Bits : {16, 32, 64}) {
            const auto Name = std::string(Op) + std::to_string(Bits);
            const auto Start = Emitted.find("probe_" + Name + (IR ? "(" : ":"));
            ASSERT_NE(Start, std::string::npos) << Emitted;
            const auto End = Emitted.find(IR ? "\n}" : "\tret", Start);
            ASSERT_NE(End, std::string::npos) << Emitted;
            const auto Body = Emitted.substr(Start, End - Start);
            const auto Mnemonic = std::string(Op) + (Bits == 16   ? "w"
                                                     : Bits == 32 ? "l"
                                                                  : "q");
            const auto Needle =
                IR ? "@llvm.x86." + Name + "(ptr"
                   : "\t" + (std::string(OutputKind) == "att" ? Mnemonic : Op) +
                         "\t";
            const auto First = Body.find(Needle);
            ASSERT_NE(First, std::string::npos) << Body;
            const auto Second = Body.find(Needle, First + 1);
            ASSERT_NE(Second, std::string::npos) << Body;
            EXPECT_EQ(Body.find(Needle, Second + 1), std::string::npos);
            if (IR) {
              EXPECT_LT(Body.find("store i32 17"), First);
              EXPECT_NE(Body.find("store i32 23", Second), std::string::npos);
              if (std::string(Opt) == "-O2")
                EXPECT_EQ(Body.find("load i"), std::string::npos);
            }
          }
      }
}

TEST_F(X86FarPointerTest, SignaturesRejectMissingExtraAndIntegerOperands) {
  const auto Source = tmpFile("far-pointer-invalid.c");
  for (const char *Op : Operations)
    for (unsigned Bits : {16, 32, 64})
      for (const char *Args : {"", "p, p", "42"}) {
        const auto Call = "__builtin_ia32_" + std::string(Op) +
                          std::to_string(Bits) + "(" + Args + ")";
        SCOPED_TRACE(Call);
        writeFile(Source, "void bad(const void *p) { " + Call + "; }\n");
        EXPECT_NE(ncc({"-Werror", "-fsyntax-only", Source.string()}).exitCode,
                  0);
      }
}

TEST_F(X86FarPointerTest, LongModeDoesNotAdvertiseLdsOrLes) {
  const auto Source = tmpFile("far-pointer-mode.c");
  writeFile(Source, R"(
#if __has_builtin(__builtin_ia32_lds16) || __has_builtin(__builtin_ia32_lds32) || \
    __has_builtin(__builtin_ia32_les16) || __has_builtin(__builtin_ia32_les32)
#error LDS and LES are invalid in long mode
#endif
int valid;
)");
  EXPECT_EQ(ncc({"-fsyntax-only", Source.string()}).exitCode, 0);
}
