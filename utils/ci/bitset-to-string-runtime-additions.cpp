
TEST_F(TranslateTest, CoreV2BitsetToStringShortDefaultDigitsRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-to-string-short-default-digits.cpp");
  const auto Output = tmpFile("bitset-to-string-short-default-digits.nc");
  writeFile(Source, R"r19bitstr(#include <bitset>
int main(){
std::bitset<6> b(33);const auto s=b.to_string();
return s.size()!=6||s[0]!='1'||s[1]!='0'||s[4]!='0'||s[5]!='1';}
)r19bitstr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-to-string-short-default-digits" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetToStringLongCrossWordRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-to-string-long-cross-word.cpp");
  const auto Output = tmpFile("bitset-to-string-long-cross-word.nc");
  writeFile(Source, R"r19bitstr(#include <bitset>
int main(){
std::bitset<129> b;b.set(0).set(64).set(128);
const std::string s=b.to_string('.', '*');
return s.size()!=129||s[0]!='*'||s[64]!='*'||s[128]!='*'||s[63]!='.'||s[127]!='.';}
)r19bitstr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-to-string-long-cross-word" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetToStringEmptyReceiverRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-to-string-empty-receiver.cpp");
  const auto Output = tmpFile("bitset-to-string-empty-receiver.nc");
  writeFile(Source, R"r19bitstr(#include <bitset>
static int calls;
const std::bitset<0>& observe(const std::bitset<0>& b){++calls;return b;}
int main(){std::bitset<0> b;const auto s=observe(b).to_string();
return !s.empty()||calls!=1;}
)r19bitstr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-to-string-empty-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetToStringEmbeddedNulRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-to-string-embedded-nul.cpp");
  const auto Output = tmpFile("bitset-to-string-embedded-nul.nc");
  writeFile(Source, R"r19bitstr(#include <bitset>
int main(){
std::bitset<12> b;b.set(1).set(10);
auto s=b.to_string<char>('\0','X');
return s.size()!=12||s[0]!='\0'||s[1]!='X'||s[2]!='\0'||s[10]!='X'||s[11]!='\0';}
)r19bitstr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-to-string-embedded-nul" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetToStringPersistentCopyRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-to-string-persistent-copy.cpp");
  const auto Output = tmpFile("bitset-to-string-persistent-copy.nc");
  writeFile(Source, R"r19bitstr(#include <bitset>
int main(){
std::bitset<65> b;b.set(64);std::string s;s=b.to_string();
b.reset().set(0);auto next=b.to_string();
return s.size()!=65||next.size()!=65||s[0]!='1'||s[64]!='0'||next[0]!='0'||next[64]!='1';}
)r19bitstr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-to-string-persistent-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetToStringDefaultReturnTemplateRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-to-string-default-return-template.cpp");
  const auto Output = tmpFile("bitset-to-string-default-return-template.nc");
  writeFile(Source, R"r19bitstr(#include <bitset>
static int events;
const std::bitset<65>& observe(const std::bitset<65>& b){events|=1;return b;}
char zero(){events|=(events&1)?2:8;return 'o';}
char one(){events|=(events&1)?4:16;return 'x';}
int main(){
std::bitset<65> b;b.set(0).set(64);
auto s=observe(b).to_string<char,std::char_traits<char>,std::allocator<char>>(zero(),one());
return events!=7||s.size()!=65||s[0]!='x'||s[1]!='o'||s[64]!='x';}
)r19bitstr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-to-string-default-return-template" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetToStringWideCharacterRejected) {
  const auto Source = tmpFile("bitset-to-string-wide-character.cpp");
  writeFile(Source, R"r19bitstr(#include <bitset>
int main(){std::bitset<8> b(2);return b.to_string<char16_t>().size()!=8;}
)r19bitstr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetToStringCustomTraitsRejected) {
  const auto Source = tmpFile("bitset-to-string-custom-traits.cpp");
  writeFile(Source, R"r19bitstr(#include <bitset>
struct Traits : std::char_traits<char>{};
int main(){std::bitset<8> b;return b.to_string<char,Traits>().size()!=8;}
)r19bitstr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetToStringCustomAllocatorRejected) {
  const auto Source = tmpFile("bitset-to-string-custom-allocator.cpp");
  writeFile(Source, R"r19bitstr(#include <bitset>
template<class T> struct CustomAllocator : std::allocator<T>{template<class U> struct rebind{using other=CustomAllocator<U>;};};
int main(){std::bitset<65> b;return b.to_string<char,std::char_traits<char>,CustomAllocator<char>>().size()!=65;}
)r19bitstr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetToStringExplicitUserImplementationRejected) {
  const auto Source = tmpFile("bitset-to-string-explicit-user-implementation.cpp");
  writeFile(Source, R"r19bitstr(#include <bitset>
namespace std{template<> template<> basic_string<char,char_traits<char>,allocator<char>> bitset<65>::to_string<char,char_traits<char>,allocator<char>>(char,char)const{return string(4,'x');}}
int main(){std::bitset<65> b;return b.to_string<char,std::char_traits<char>,std::allocator<char>>('0','1').size()!=4;}
)r19bitstr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetToStringOwnedRootNameRejected) {
  const auto Source = tmpFile("bitset-to-string-owned-root-name.cpp");
  writeFile(Source, R"r19bitstr(#define to_string nct_custom_to_string
#include <bitset>
#undef to_string
int main(){std::bitset<65> b(3);return b.nct_custom_to_string().size()!=65;}
)r19bitstr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetToStringOwnedBitsetStorageRejected) {
  const auto Source = tmpFile("bitset-to-string-owned-bitset-storage.cpp");
  writeFile(Source, R"r19bitstr(#define __first_ nct_custom_to_string_storage
#include <bitset>
#undef __first_
int main(){std::bitset<65> b(3);return b.to_string().size()!=65;}
)r19bitstr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetToStringOwnedBitReferenceRejected) {
  const auto Source = tmpFile("bitset-to-string-owned-bit-reference.cpp");
  writeFile(Source, R"r19bitstr(#define __make_ref nct_custom_string_bit_reference
#include <bitset>
#undef __make_ref
int main(){std::bitset<65> b(3);return b.to_string().size()!=65;}
)r19bitstr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetToStringOwnedStringTypeRejected) {
  const auto Source = tmpFile("bitset-to-string-owned-string-type.cpp");
  writeFile(Source, R"r19bitstr(#define basic_string nct_custom_result_string
#include <bitset>
#undef basic_string
int main(){std::bitset<65> b(3);return b.to_string().size()!=65;}
)r19bitstr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
