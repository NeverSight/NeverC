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
  bool Void;
};

constexpr VMXAPI APIs[] = {
    {"vmread", "field, (word_t *)memory", false},
    {"vmwrite", "field, prior", false},
    {"vmread_reg", "field, prior, &status", false},
    {"vmwrite_mem", "field, memory", false},
    {"invept", "field, memory", false},
    {"invvpid", "field, memory", false},
    {"vmclear", "memory", false},
    {"vmptrld", "memory", false},
    {"vmptrst", "memory", true},
    {"vmxon", "memory", false},
    {"vmxoff", "", true},
    {"vmxoff_status", "", false},
    {"vmlaunch", "", false},
    {"vmresume", "", false},
    {"vmread_address", "field, prior, ADDRESS_BITS, 0x64", false},
    {"vmwrite_mem_address", "field, prior, ADDRESS_BITS, 0x64", false},
    {"invept_address", "field, prior, ADDRESS_BITS, 0x64", false},
    {"invvpid_address", "field, prior, ADDRESS_BITS, 0x64", false},
    {"vmclear_address", "prior, ADDRESS_BITS, 0x65", false},
    {"vmptrld_address", "prior, ADDRESS_BITS, 0x65", false},
    {"vmptrst_address", "prior, ADDRESS_BITS, 0x65", true},
    {"vmxon_address", "prior, ADDRESS_BITS, 0x65", false},
};

} // namespace

class X86VMXFrontendTest : public X86NativeBuiltinTest {};

TEST_F(X86VMXFrontendTest, NativeInterfacesRetainCallsAndX64Types) {
  for (const char *Target : {"x86_64-unknown-linux-gnu",
                             "x86_64-pc-windows-msvc", "x86_64-apple-darwin"}) {
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(Target);
      SCOPED_TRACE(Opt);
      std::string Source = "typedef unsigned long long word_t;\n";
      Source += "#define ADDRESS_BITS 32\n";
      for (const auto &API : APIs) {
        const auto Name = std::string(API.Name);
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
                "__builtin_ia32_vmwrite(field, 3); *p = 34; }\n";
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
        auto Name = std::string(API.Name);
        std::replace(Name.begin(), Name.end(), '_', '.');
        const std::regex Calls("call [^@\\n]*@llvm\\.x86\\." + Name + "\\(");
        EXPECT_GE(
            std::distance(std::sregex_iterator(IR.begin(), IR.end(), Calls),
                          std::sregex_iterator()),
            2)
            << Name << '\n'
            << IR;
      }
      const auto Integer = "i64";
      const auto First = IR.find(std::string("store ") + Integer + " 17");
      const auto Call = IR.find("call i8 @llvm.x86.vmwrite", First);
      const auto Second =
          IR.find(std::string("store ") + Integer + " 34", Call);
      EXPECT_LT(First, Call);
      EXPECT_LT(Call, Second);
      EXPECT_NE(Second, std::string::npos);
      EXPECT_NE(IR.find("@llvm.x86.vmread(i64"), std::string::npos);
      EXPECT_NE(IR.find("call { i64, i8 } @llvm.x86.vmread.reg"),
                std::string::npos);
    }
  }
}

TEST_F(X86VMXFrontendTest,
       MicrosoftInterfacesUseNativeSizeTWithOrWithoutHeader) {
  for (bool Header : {false, true})
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(Header);
      SCOPED_TRACE(Opt);
      const auto Input = tmpFile("vmx_ms.c"), Output = tmpFile("vmx_ms.ll");
      writeFile(Input, std::string(Header ? "#include <intrin.h>\n" : "") + R"(
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
      std::vector<std::string> Arguments = {"--target=x86_64-pc-windows-msvc",
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
      const auto R = ncc(Arguments);
      ASSERT_EQ(R.exitCode, 0) << R.err;
      const auto IR = readFile(Output);
      EXPECT_EQ(IR.find(" asm "), std::string::npos);
      for (const char *Op : {"vmread", "vmwrite"})
        EXPECT_NE(IR.find(std::string("call i8 @llvm.x86.") + Op + "(i64"),
                  std::string::npos)
            << IR;
    }
}
