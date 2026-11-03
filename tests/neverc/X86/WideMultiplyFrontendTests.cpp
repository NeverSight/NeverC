#include "NativeBuiltinFixture.h"

#include <iterator>
#include <regex>
#include <string>

class X86WideMultiplyTest : public X86NativeBuiltinTest {};

TEST_F(X86WideMultiplyTest, EvaluatesHighProductPointerOnce) {
  for (const char *Level : {"-O0", "-O2"})
    for (bool IsSigned : {false, true}) {
      const std::string Type = IsSigned ? "long long" : "unsigned long long";
      const std::string Name = IsSigned ? "_mul128" : "_umul128";
      SCOPED_TRACE(Name + Level);
      const auto Input = tmpFile("multiply-pointer.c");
      writeFile(Input, "#include <intrin.h>\n" + Type +
                           " *next_high_product(void);\n" + Type + " probe(" +
                           Type + " a," + Type + " b) { return " + Name +
                           "(a,b,next_high_product()); }\n");
      const auto Output = tmpFile("multiply-pointer.ll");
      const auto Result = ncc(
          {"--target=x86_64-pc-windows-msvc", "-std=c23", Level,
           "-ffreestanding", "-fno-lto", "-fno-builtin-mimalloc", "-S",
           "-emit-llvm", Input.string(), "-o", Output.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      const auto Text = readFile(Output);
      const std::regex Call("call[^\\n]*@next_high_product\\(");
      EXPECT_EQ(std::distance(std::sregex_iterator(Text.begin(), Text.end(), Call),
                              std::sregex_iterator()),
                1)
          << Text;
      EXPECT_NE(Text.find("store i64"), std::string::npos) << Text;
      EXPECT_NE(Text.find("ret i64"), std::string::npos) << Text;
    }
}
