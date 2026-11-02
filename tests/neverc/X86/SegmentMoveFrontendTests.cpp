//===- SegmentMoveFrontendTests.cpp - Native selector moves --------------===//
//
// NeverC Compiler
//
//===----------------------------------------------------------------------===//

#include "../NeverCTestFixture.h"

#include <array>
#include <string>
#include <vector>

class X86SegmentMoveTest : public NeverCTest {};

namespace {
constexpr std::array<const char *, 4> Operations = {"readseg", "readseg_mem",
                                                    "writeseg", "writeseg_mem"};
constexpr std::array<const char *, 6> Segments = {"es", "cs", "ss",
                                                  "ds", "fs", "gs"};
} // namespace

TEST_F(X86SegmentMoveTest, NativeIdentityMemoryFormsAndRepeatedEffects) {
  const auto Source = tmpFile("segment-moves.c");
  std::string Text;
  for (unsigned Kind = 0; Kind < Operations.size(); ++Kind) {
    const std::string Op = Operations[Kind];
    Text += "#if !__has_builtin(__builtin_ia32_" + Op +
            ")\n#error missing builtin\n#endif\n";
    for (unsigned Segment = 0; Segment < Segments.size(); ++Segment) {
      if (Kind >= 2 && Segment == 1)
        continue;
      const std::string Args =
          std::to_string(Segment) + (Kind == 0   ? ""
                                     : Kind == 2 ? ", value"
                                                 : ", p");
      const std::string Call = "__builtin_ia32_" + Op + "(" + Args + ")";
      Text += std::string(Kind == 0 ? "unsigned short" : "void") + " probe_" +
              Op + "_" + Segments[Segment] +
              "(void *p, unsigned short value, unsigned *marker) {\n";
      if (Kind == 0)
        Text += "_Static_assert(_Generic(" + Call +
                ", unsigned short: 1, default: 0));\n";
      Text += "*marker = 17;\n";
      Text += (Kind == 0 ? "unsigned short result = " : "") + Call + ";\n";
      Text += Call + ";\n*marker = 23;\n";
      Text += Kind == 0 ? "return result;\n}\n" : "}\n";
    }
  }
  writeFile(Source, Text);
  for (const char *Target : {"x86_64-unknown-linux-gnu",
                             "x86_64-pc-windows-msvc", "x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"})
      for (const char *Format : {"ir", "att", "intel"}) {
        SCOPED_TRACE(Target);
        SCOPED_TRACE(Opt);
        SCOPED_TRACE(Format);
        const bool IR = std::string(Format) == "ir";
        const auto Output = tmpFile(IR ? "segments.ll" : "segments.s");
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
        Args.push_back(IR ? "-emit-llvm" : std::string("-masm=") + Format);
        const auto Result = ncc(Args);
        ASSERT_EQ(Result.exitCode, 0) << Result.err;
        const auto Emitted = readFile(Output);
        EXPECT_EQ(Emitted.find(IR ? " asm " : "#APP"), std::string::npos);
        for (unsigned Kind = 0; Kind < Operations.size(); ++Kind)
          for (unsigned Segment = 0; Segment < Segments.size(); ++Segment) {
            if (Kind >= 2 && Segment == 1)
              continue;
            const auto Name =
                std::string(Operations[Kind]) + "_" + Segments[Segment];
            const auto Start = Emitted.find("probe_" + Name + (IR ? "(" : ":"));
            ASSERT_NE(Start, std::string::npos) << Emitted;
            const auto End = Emitted.find(IR ? "\n}" : "\tret", Start);
            ASSERT_NE(End, std::string::npos) << Emitted;
            const auto Body = Emitted.substr(Start, End - Start);
            std::string Native = Operations[Kind];
            if (const auto At = Native.find('_'); At != std::string::npos)
              Native[At] = '.';
            const std::string SegmentName =
                (std::string(Format) == "att" ? "%" : "") +
                std::string(Segments[Segment]);
            const auto Needle =
                IR ? "@llvm.x86." + Native + "(i32 " + std::to_string(Segment)
                   : ((Kind < 2) == (std::string(Format) == "att")
                          ? SegmentName + ","
                          : ", " + SegmentName + "\n");
            const auto First = Body.find(Needle);
            ASSERT_NE(First, std::string::npos) << Body;
            const auto Second = Body.find(Needle, First + 1);
            ASSERT_NE(Second, std::string::npos) << Body;
            EXPECT_EQ(Body.find(Needle, Second + 1), std::string::npos) << Body;
            if (IR) {
              EXPECT_LT(Body.find("store i32 17"), First);
              EXPECT_NE(Body.find("store i32 23", Second), std::string::npos);
              if (std::string(Opt) == "-O2")
                EXPECT_EQ(Body.find("load i16"), std::string::npos);
            }
          }
      }
}

TEST_F(X86SegmentMoveTest, InvalidIndicesAndCodeSegmentWritesAreDiagnosed) {
  const auto Source = tmpFile("bad-segment.c");
  for (unsigned Kind = 0; Kind < Operations.size(); ++Kind)
    for (const char *Index : {"-1", "6", "number", "1"}) {
      if (Kind < 2 && std::string(Index) == "1")
        continue;
      const auto Call = std::string("__builtin_ia32_") + Operations[Kind] +
                        "(" + Index +
                        (Kind == 0   ? ""
                         : Kind == 2 ? ", 0"
                                     : ", p") +
                        ")";
      SCOPED_TRACE(Call);
      writeFile(Source,
                "void bad(unsigned number, void *p) { " + Call + "; }\n");
      const auto Result = ncc({"-Werror", "-fsyntax-only", Source.string()});
      EXPECT_NE(Result.exitCode, 0);
      if (std::string(Index) == "1")
        EXPECT_NE(Result.err.find("MOV cannot write the CS segment register"),
                  std::string::npos)
            << Result.err;
    }
  for (const char *Call :
       {"readseg()", "readseg(0, 0)", "writeseg(0)", "writeseg(0, 0, 0)",
        "readseg_mem(0, 42)", "writeseg_mem(0, 42)"}) {
    SCOPED_TRACE(Call);
    writeFile(Source,
              std::string("void bad(void) { __builtin_ia32_") + Call + "; }\n");
    EXPECT_NE(ncc({"-Werror", "-fsyntax-only", Source.string()}).exitCode, 0);
  }
}
