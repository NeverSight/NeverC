//===- VMXFrontendTests.cpp - Native VMX source widths --------------------===//
//
// NeverC Compiler
//
//===----------------------------------------------------------------------===//
/// \file
/// Keep VMCS word widths separate from m64 management and m128 invalidation.
//===----------------------------------------------------------------------===//

#include "NativeBuiltinFixture.h"

#include <algorithm>
#include <regex>

namespace {
struct VMXAPI {
  const char *Name;
  const char *Arguments;
  bool ModeSized;
  bool Void;
};

constexpr VMXAPI APIs[] = {
    {"vmread", "field, (word_t *)memory", true, false},
    {"vmwrite", "field, prior", true, false},
    {"vmread_reg", "field, prior, &status", true, false},
    {"vmwrite_mem", "field, memory", true, false},
    {"invept", "field, memory", true, false},
    {"invvpid", "field, memory", true, false},
    {"vmclear", "memory", false, false},
    {"vmptrld", "memory", false, false},
    {"vmptrst", "memory", false, true},
    {"vmxon", "memory", false, false},
    {"vmxoff", "", false, true},
    {"vmxoff_status", "", false, false},
    {"vmlaunch", "", false, false},
    {"vmresume", "", false, false},
    {"vmread_address", "field, prior, ADDRESS_BITS, 0x64", true, false},
    {"vmwrite_mem_address", "field, prior, ADDRESS_BITS, 0x64", true, false},
    {"invept_address", "field, prior, ADDRESS_BITS, 0x64", true, false},
    {"invvpid_address", "field, prior, ADDRESS_BITS, 0x64", true, false},
    {"vmclear_address", "prior, ADDRESS_BITS, 0x65", true, false},
    {"vmptrld_address", "prior, ADDRESS_BITS, 0x65", true, false},
    {"vmptrst_address", "prior, ADDRESS_BITS, 0x65", true, true},
    {"vmxon_address", "prior, ADDRESS_BITS, 0x65", true, false},
};

std::string nativeName(const VMXAPI &API, bool Wide) {
  return std::string(API.Name) + (API.ModeSized && !Wide ? "32" : "");
}
} // namespace

class X86VMXFrontendTest : public X86NativeBuiltinTest {};

TEST_F(X86VMXFrontendTest, NativeInterfacesRetainCallsAndCodeModeTypes) {
  for (const char *Target :
       {"i386-unknown-linux-gnu", "x86_64-unknown-linux-gnu",
        "x86_64-pc-windows-msvc", "x86_64-apple-darwin"}) {
    const bool Wide = std::string(Target).rfind("x86_64", 0) == 0;
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(Target);
      SCOPED_TRACE(Opt);
      std::string Source = Wide ? "typedef unsigned long long word_t;\n"
                                : "typedef unsigned word_t;\n";
      Source += "#define ADDRESS_BITS " + std::string(Wide ? "32\n" : "16\n");
      for (const auto &API : APIs) {
        const auto Name = nativeName(API, Wide);
        const auto Call = "__builtin_ia32_" + Name + "(" + API.Arguments + ")";
        Source += "#if !__has_builtin(__builtin_ia32_" + Name +
                  ")\n#error Missing VMX builtin\n#endif\n";
        Source +=
            "word_t live_" + Name +
            "(word_t field, word_t prior, void *memory) {\n"
            "unsigned char status;\n" +
            (API.Void ? Call + "; return prior;" : "return " + Call + ";") +
            "\n}\n";
        Source += "void dead_" + Name +
                  "(word_t field, word_t prior, void *memory) {\n"
                  "unsigned char status; (void)" +
                  Call + ";\n}\n";
      }
      Source += "void ordered(word_t field, word_t *p) { *p = 17; "
                "__builtin_ia32_vmwrite" +
                std::string(Wide ? "" : "32") + "(field, 3); *p = 34; }\n";
      const auto Input = tmpFile("vmx.c"), Output = tmpFile("vmx.ll");
      writeFile(Input, Source);
      const auto R =
          ncc({std::string("--target=") + Target, Opt, "-ffreestanding",
               "-fno-builtin-mimalloc", "-fno-lto", "-Werror", "-S",
               "-emit-llvm", Input.string(), "-o", Output.string()});
      ASSERT_EQ(R.exitCode, 0) << R.err;
      const auto IR = readFile(Output);
      EXPECT_EQ(IR.find(" asm "), std::string::npos) << IR;
      for (const auto &API : APIs) {
        auto Name = nativeName(API, Wide);
        std::replace(Name.begin(), Name.end(), '_', '.');
        const std::regex Calls("call [^@\\n]*@llvm\\.x86\\." + Name + "\\(");
        EXPECT_GE(
            std::distance(std::sregex_iterator(IR.begin(), IR.end(), Calls),
                          std::sregex_iterator()),
            2)
            << Name << '\n'
            << IR;
      }
      const auto Integer = Wide ? "i64" : "i32";
      const auto First = IR.find(std::string("store ") + Integer + " 17");
      const auto Call = IR.find("call i8 @llvm.x86.vmwrite", First);
      const auto Second =
          IR.find(std::string("store ") + Integer + " 34", Call);
      EXPECT_LT(First, Call);
      EXPECT_LT(Call, Second);
      EXPECT_NE(Second, std::string::npos);
      EXPECT_NE(IR.find(std::string("@llvm.x86.vmread") + (Wide ? "" : "32") +
                        "(" + Integer),
                std::string::npos);
      EXPECT_NE(IR.find(std::string("call { ") + Integer +
                        ", i8 } @llvm.x86.vmread.reg" + (Wide ? "" : "32")),
                std::string::npos);
    }
  }
}

TEST_F(X86VMXFrontendTest,
       MicrosoftInterfacesUseNativeSizeTWithOrWithoutHeader) {
  for (bool Wide : {false, true})
    for (bool Header : {false, true})
      for (const char *Opt : {"-O0", "-O2"}) {
        SCOPED_TRACE(Wide);
        SCOPED_TRACE(Header);
        SCOPED_TRACE(Opt);
        const auto Input = tmpFile("vmx_ms.c"), Output = tmpFile("vmx_ms.ll");
        writeFile(Input,
                  std::string(Header ? "#include <intrin.h>\n" : "") + R"(
          unsigned char read_vmcs(__SIZE_TYPE__ field, __SIZE_TYPE__ *out) {
            return __vmx_vmread(field, out);
          }
          unsigned char write_vmcs(__SIZE_TYPE__ field, __SIZE_TYPE__ value) {
            return __vmx_vmwrite(field, value);
          }
          unsigned char manage(unsigned long long *p) {
            __vmx_off(); __vmx_vmptrst(p);
            return __vmx_vmclear(p) ^ __vmx_vmptrld(p) ^ __vmx_on(p) ^
                   __vmx_vmlaunch() ^ __vmx_vmresume();
          }
        )");
        std::vector<std::string> Arguments = {
            Wide ? "--target=x86_64-pc-windows-msvc"
                 : "--target=i386-unknown-linux-gnu",
            "-fms-extensions",
            Opt,
            "-ffreestanding",
            "-fno-builtin-mimalloc",
            "-fno-lto",
            "-Werror",
            "-S",
            "-emit-llvm",
            Input.string(),
            "-o",
            Output.string()};
        if (!Wide)
          Arguments.emplace_back("-D_MSC_VER=1930");
        const auto R = ncc(Arguments);
        ASSERT_EQ(R.exitCode, 0) << R.err;
        const auto IR = readFile(Output);
        EXPECT_EQ(IR.find(" asm "), std::string::npos);
        for (const char *Op : {"vmread", "vmwrite"})
          EXPECT_NE(IR.find(std::string("call i8 @llvm.x86.") + Op +
                            (Wide ? "(i64" : "32(i32")),
                    std::string::npos)
              << IR;
      }
}

TEST_F(X86VMXFrontendTest, RejectsWrongCodeModeAndInvalidAddressControls) {
  const auto Input = tmpFile("vmx_invalid.c");
  for (const auto &API : APIs) {
    if (!API.ModeSized)
      continue;
    writeFile(Input,
              "void probe(unsigned field, unsigned prior, void *memory) {\n"
              "unsigned char status;\n#define ADDRESS_BITS 16\n"
              "typedef unsigned word_t;\n(void)__builtin_ia32_" +
                  nativeName(API, false) + "(" + API.Arguments + "); }");
    const auto R = ncc(
        {"--target=x86_64-unknown-linux-gnu", "-fsyntax-only", Input.string()});
    EXPECT_EQ(R.exitCode, 1) << R.err;
    EXPECT_NE(R.err.find("requires a 32-bit x86 target"), std::string::npos)
        << R.err;
  }
  for (const char *Controls :
       {"64, 0", "8, 0", "16, 0x40", "width, 0", "32, prefix"}) {
    writeFile(Input, "void probe(unsigned width, unsigned prefix) { "
                     "__builtin_ia32_vmread_address32(0, 0, " +
                         std::string(Controls) + "); }");
    const auto R = ncc(
        {"--target=i386-unknown-linux-gnu", "-fsyntax-only", Input.string()});
    EXPECT_EQ(R.exitCode, 1) << R.err;
  }
  for (const char *Call :
       {"__builtin_ia32_vmread32(0)", "__builtin_ia32_vmwrite32(0, 0, 0)",
        "__builtin_ia32_vmread_reg32(0, 0)",
        "__builtin_ia32_vmptrst_address32(0, 32)"}) {
    writeFile(Input, "void probe(void) { " + std::string(Call) + "; }");
    const auto R = ncc(
        {"--target=i386-unknown-linux-gnu", "-fsyntax-only", Input.string()});
    EXPECT_EQ(R.exitCode, 1) << R.err;
  }
}
