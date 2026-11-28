
TEST_F(TranslateTest, CoreV2BitsetReferenceProxyCopyAliasRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-reference-proxy-copy-alias.cpp");
  const auto Output = tmpFile("bitset-reference-proxy-copy-alias.nc");
  writeFile(Source, R"r19bitref(#include <bitset>
int main(){std::bitset<129> b;auto x=b[63];auto y=x;
x=true;if(!bool(y))return 1;y=false;if(bool(x))return 2;
b[64]=true;x=b[64];if(!b.test(63)||!bool(y))return 3;
b[64]=false;if(!bool(x)||b.test(64))return 4;
b[128]=x;return b.count()!=2||!b.test(63)||!b.test(128);}
)r19bitref");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-reference-proxy-copy-alias" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetReferenceConstAccessRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-reference-const-access.cpp");
  const auto Output = tmpFile("bitset-reference-const-access.nc");
  writeFile(Source, R"r19bitref(#include <bitset>
int main(){std::bitset<129> b;b.set(0).set(64).set(128);
const auto& c=b;auto first=c[0];auto middle=c[64];bool last=c[128];
if(!bool(first)||!bool(middle)||!last||bool(c[63]))return 1;
b.reset(0).reset(64);return bool(c[0])||bool(c[64])||!bool(c[128]);}
)r19bitref");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-reference-const-access" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetReferenceAssignmentSequencingRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-reference-assignment-sequencing.cpp");
  const auto Output = tmpFile("bitset-reference-assignment-sequencing.nc");
  writeFile(Source, R"r19bitref(#include <bitset>
static std::bitset<65>* target;static int events;
std::bitset<65>& left(){events=events*10+1;return *target;}
std::bitset<65>& right(){events=events*10+2;return *target;}
int main(){std::bitset<65> b;target=&b;b.set(64);events=0;
left()[0]=right()[64];if(events!=21||!b.test(0)||b.count()!=2)return 1;
b.reset(0);events=0;left()[0].operator=(right()[64]);
if(events!=12||!b.test(0)||b.count()!=2)return 2;
b[0]=b[0];b[64]=false;return !b.test(0)||b.test(64);}
)r19bitref");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-reference-assignment-sequencing" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedUnsignedLongLongFullWidthRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-unsigned-unsigned-long-long-full-width.cpp");
  const auto Output = tmpFile("bitset-unsigned-unsigned-long-long-full-width.nc");
  writeFile(Source, R"r19bitconv(#include <bitset>
int main(){std::bitset<64> b(1ull<<63);b.set(0);
return b.to_ullong()!=((1ull<<63)|1ull);}
)r19bitconv");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-unsigned-unsigned-long-long-full-width" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedPartialWordRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-unsigned-partial-word.cpp");
  const auto Output = tmpFile("bitset-unsigned-partial-word.nc");
  writeFile(Source, R"r19bitconv(#include <bitset>
int main(){std::bitset<5> b(255);if(b.to_ulong()!=31ul)return 1;
b.reset(1);return b.to_ullong()!=29ull;}
)r19bitconv");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-unsigned-partial-word" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedConstAndTemporaryRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-unsigned-const-and-temporary.cpp");
  const auto Output = tmpFile("bitset-unsigned-const-and-temporary.nc");
  writeFile(Source, R"r19bitconv(#include <bitset>
int main(){const std::bitset<16> b(0xffeeddccu);
return b.to_ullong()!=0xddccull||std::bitset<10>(1023).to_ulong()!=1023ul;}
)r19bitconv");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-unsigned-const-and-temporary" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetBoundedMaskLastWordRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-bounded-mask-last-word.cpp");
  const auto Output = tmpFile("bitset-bounded-mask-last-word.nc");
  writeFile(Source, R"r19bitindex(#include <bitset>
int main(){std::bitset<128> b;std::size_t pos=383;
b.set(pos&127,true);if(!b.test(127))return 1;b.flip(pos&127);b.set(pos&127,false);
return b.any()||b.test(127);}
)r19bitindex");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-bounded-mask-last-word" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetBoundedUnsignedIntPromotionRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-bounded-unsigned-int-promotion.cpp");
  const auto Output = tmpFile("bitset-bounded-unsigned-int-promotion.nc");
  writeFile(Source, R"r19bitindex(#include <bitset>
int main(){std::bitset<65> b;unsigned pos=129;
b.flip(pos%65u);return b.count()!=1||!b.test(64);}
)r19bitindex");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-bounded-unsigned-int-promotion" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetBoundedReceiverBeforeArgumentsRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-bounded-receiver-before-arguments.cpp");
  const auto Output = tmpFile("bitset-bounded-receiver-before-arguments.nc");
  writeFile(Source, R"r19bitindex(#include <bitset>
static int calls;
std::bitset<129>& observe(std::bitset<129>& a){calls=calls*10+1;return a;}
std::size_t position(){calls=calls*10+2;return 128;}
int main(){std::bitset<129> a;observe(a).set(position()%129).flip(position()%129);
return a.any()||calls!=122;}
)r19bitindex");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-bounded-receiver-before-arguments" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetReferenceOwnedMutableMaskFieldRejected) {
  const auto Source = tmpFile("bitset-reference-owned-mutable-mask-field.cpp");
  writeFile(Source, R"r19bitref(#define __mask_ nct_foreign_mask
#include <bitset>
#undef __mask_
int main(){std::bitset<65> a;auto r=a[64];r=true;return !bool(r);}
)r19bitref");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetReferenceOwnedMutableCarrierRejected) {
  const auto Source = tmpFile("bitset-reference-owned-mutable-carrier.cpp");
  writeFile(Source, R"r19bitref(#define __bit_reference nct_foreign_mutable_reference
#include <bitset>
#undef __bit_reference
int main(){std::bitset<65> a;auto r=a[64];r=true;return !bool(r);}
)r19bitref");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetReferenceOwnedStorageBaseRejected) {
  const auto Source = tmpFile("bitset-reference-owned-storage-base.cpp");
  writeFile(Source, R"r19bitref(#define __bitset nct_foreign_bitset_storage
#include <bitset>
#undef __bitset
int main(){std::bitset<65> a;a[0]=true;return !bool(a[0]);}
)r19bitref");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetReferenceOwnedConstSubscriptRejected) {
  const auto Source = tmpFile("bitset-reference-owned-const-subscript.cpp");
  writeFile(Source, R"r19bitref(#include <bitset>
namespace std{
template<> constexpr auto bitset<65>::operator[](size_t) const
#if defined(_LIBCPP_ABI_BITSET_VECTOR_BOOL_CONST_SUBSCRIPT_RETURN_BOOL)
    -> bool
#else
    -> __const_reference
#endif
{return this->__make_ref(64);}
}
int main(){const std::bitset<65> a;return bool(a[0]);}
)r19bitref");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedWideUllongRejected) {
  const auto Source = tmpFile("bitset-unsigned-wide-ullong.cpp");
  writeFile(Source, R"r19bitconv(#include <bitset>
int main(){std::bitset<65> b(1);return b.to_ullong()!=1ull;}
)r19bitconv");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedSpecializedUllongRejected) {
  const auto Source = tmpFile("bitset-unsigned-specialized-ullong.cpp");
  writeFile(Source, R"r19bitconv(#include <bitset>
namespace std{template<> unsigned long long bitset<1>::to_ullong() const{return 7;}}
int main(){std::bitset<1> b;return b.to_ullong()!=7ull;}
)r19bitconv");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedInterceptBaseRejected) {
  const auto Source = tmpFile("bitset-unsigned-intercept-base.cpp");
  writeFile(Source, R"r19bitconv(#define __bitset nct_foreign_bits
#include <bitset>
#undef __bitset
int main(){std::bitset<32> b(2);return b.to_ullong()!=2ull;}
)r19bitconv");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedUnownedUllongRejected) {
  const auto Source = tmpFile("bitset-unsigned-unowned-ullong.cpp");
  writeFile(Source, R"r19bitconv(#define to_ullong nct_foreign_ullong
#include <bitset>
#undef to_ullong
int main(){std::bitset<16> b(2);return b.nct_foreign_ullong()!=2ull;}
)r19bitconv");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetBoundedPositionSignedRemainderRejected) {
  const auto Source = tmpFile("bitset-bounded-position-signed-remainder.cpp");
  writeFile(Source, R"r19bitindex(#include <bitset>
int main(){std::bitset<65> b;int i=-1;return b.test(i%65);}
)r19bitindex");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetBoundedPositionOversizedDivisorRejected) {
  const auto Source = tmpFile("bitset-bounded-position-oversized-divisor.cpp");
  writeFile(Source, R"r19bitindex(#include <bitset>
int main(){std::bitset<65> b;std::size_t i=1;b.reset(i%66);return 0;}
)r19bitindex");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetBoundedPositionEmptyBitsetRejected) {
  const auto Source = tmpFile("bitset-bounded-position-empty-bitset.cpp");
  writeFile(Source, R"r19bitindex(#include <bitset>
int main(){std::bitset<0> b;std::size_t i=2;return b.test(i%1);}
)r19bitindex");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetBoundedPositionOwnedTestBodyRejected) {
  const auto Source = tmpFile("bitset-bounded-position-owned-test-body.cpp");
  writeFile(Source, R"r19bitindex(#include <bitset>
namespace std{template<> bool bitset<65>::test(size_t) const{return false;}}
int main(){std::bitset<65> b;std::size_t i=2;return b.test(i%65);}
)r19bitindex");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetHashSingleWordRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-hash-single-word.cpp");
  const auto Output = tmpFile("bitset-hash-single-word.nc");
  writeFile(Source, R"r19bithash(#include <bitset>
int main(){std::hash<std::bitset<64>> hash;
const std::bitset<64> a(0x8899aabb01020304ull);const std::bitset<7> b(0xffull);
return hash(a)!=0x8899aabb01020304ull||std::hash<std::bitset<7>>{}(b)!=127;}
)r19bithash");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-hash-single-word" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetHashNonzeroTailRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-hash-nonzero-tail.cpp");
  const auto Output = tmpFile("bitset-hash-nonzero-tail.nc");
  writeFile(Source, R"r19bithash(#include <bitset>
int main(){std::bitset<193> a(2ull);
a.set(64).set(130).set(192);const std::hash<std::bitset<193>> hash;
if(hash.operator()(a)!=6)return 1;a.set(191);return hash(a)!=(6ull|(1ull<<63));}
)r19bithash");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-hash-nonzero-tail" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetHashCopiedCarrierRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-hash-copied-carrier.cpp");
  const auto Output = tmpFile("bitset-hash-copied-carrier.nc");
  writeFile(Source, R"r19bithash(#include <bitset>
using Hash=std::hash<std::bitset<65>>;
int main(){Hash a;Hash b(a);std::bitset<65> bits;bits.set(64);
if(b(bits)!=1)return 1;const Hash c(b);bits.flip(0);return c(bits)!=0;}
)r19bithash");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-hash-copied-carrier" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetLiteralTextCrossWordRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-literal-text-cross-word.cpp");
  const auto Output = tmpFile("bitset-literal-text-cross-word.nc");
  writeFile(Source, R"r19bitlit(#include <bitset>
int main(){std::bitset<129> a("100000000000000000000000000000000000000000000000000000000000000010000000000000000000000000000000000000000000000000000000000000001");
return a.count()!=3||!a.test(0)||!a.test(64)||!a.test(128);}
)r19bitlit");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-literal-text-cross-word" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetLiteralTextCustomCharactersRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-literal-text-custom-characters.cpp");
  const auto Output = tmpFile("bitset-literal-text-custom-characters.nc");
  writeFile(Source, R"r19bitlit(#include <bitset>
int main(){
std::bitset<7> a("AABA",4,'A','B');std::bitset<4> b("\xff\xfe",2,'\xfe','\xff');
return a.count()!=1||!a.test(1)||b.count()!=1||!b.test(1);}
)r19bitlit");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-literal-text-custom-characters" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetLiteralTextEmptyInputAndResultRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-literal-text-empty-input-and-result.cpp");
  const auto Output = tmpFile("bitset-literal-text-empty-input-and-result.nc");
  writeFile(Source, R"r19bitlit(#include <bitset>
int main(){std::bitset<0> a("0101");
std::bitset<65> b("");std::bitset<4> c("invalid",0);
return a.any()||b.any()||c.any()||!a.all();}
)r19bitlit");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-literal-text-empty-input-and-result" + Optimization);
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

TEST_F(TranslateTest, CoreV2BitsetBoolSwapBoolFirstRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-bool-swap-bool-first.cpp");
  const auto Output = tmpFile("bitset-bool-swap-bool-first.nc");
  writeFile(Source, R"r19bitmix(#include <bitset>
int main(){std::bitset<65> b(1);bool v=false;using std::swap;
swap(v,b[0]);return !v||b.any();}
)r19bitmix");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-bool-swap-bool-first" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetBoolSwapUnchangedOtherWordsRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-bool-swap-unchanged-other-words.cpp");
  const auto Output = tmpFile("bitset-bool-swap-unchanged-other-words.nc");
  writeFile(Source, R"r19bitmix(#include <bitset>
int main(){std::bitset<129> b;b.set(64).set(128);
bool a=false;bool c=true;using std::swap;swap(a,b[64]);swap(b[0],c);
return !a||c||b.count()!=2||!b.test(0)||b.test(64)||!b.test(128);}
)r19bitmix");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-bool-swap-unchanged-other-words" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetBoolSwapReadAfterCaptureRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-bool-swap-read-after-capture.cpp");
  const auto Output = tmpFile("bitset-bool-swap-read-after-capture.nc");
  writeFile(Source, R"r19bitmix(#include <bitset>
static std::bitset<65>* selected;
bool& set_bit(bool& v){selected->set(64);return v;}
int main(){std::bitset<65> b;selected=&b;bool v=false;using std::swap;
swap(set_bit(v),b[64]);return !v||b.any();}
)r19bitmix");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-bool-swap-read-after-capture" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetHashBitsetBaseRejected) {
  const auto Source = tmpFile("bitset-hash-bitset-base.cpp");
  writeFile(Source, R"r19bithash(#define __bitset nct_foreign_hash_base
#include <bitset>
#undef __bitset
int main(){std::bitset<65> bits(3);return std::hash<std::bitset<65>>{}(bits)!=3;}
)r19bithash");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetHashHashBaseRejected) {
  const auto Source = tmpFile("bitset-hash-hash-base.cpp");
  writeFile(Source, R"r19bithash(#define __unary_function_keep_layout_base nct_foreign_hash_carrier
#include <bitset>
#undef __unary_function_keep_layout_base
int main(){std::bitset<65> bits(3);return std::hash<std::bitset<65>>{}(bits)!=3;}
)r19bithash");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetHashLegacyBaseRejected) {
  const auto Source = tmpFile("bitset-hash-legacy-base.cpp");
  writeFile(Source, R"r19bithash(#define _LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION
#include <bitset>
int main(){std::bitset<65> bits(3);return std::hash<std::bitset<65>>{}(bits)!=3;}
)r19bithash");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetHashSpecializedEmptyOperatorRejected) {
  const auto Source = tmpFile("bitset-hash-specialized-empty-operator.cpp");
  writeFile(Source, R"r19bithash(#include <bitset>
namespace std{template<> size_t hash<bitset<0>>::operator()(const bitset<0>&) const noexcept{return 5;}}
int main(){std::bitset<0> a;return std::hash<std::bitset<0>>{}(a)!=5;}
)r19bithash");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetLiteralTextInvalidBeyondWidthRejected) {
  const auto Source = tmpFile("bitset-literal-text-invalid-beyond-width.cpp");
  writeFile(Source, R"r19bitlit(#include <bitset>
int main(){std::bitset<1> a("1x");return a.any();}
)r19bitlit");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetLiteralTextDynamicLengthRejected) {
  const auto Source = tmpFile("bitset-literal-text-dynamic-length.cpp");
  writeFile(Source, R"r19bitlit(#include <bitset>
int main(){std::size_t n=2;std::bitset<8> a("01",n);return a.any();}
)r19bitlit");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetLiteralTextWideCharacterRejected) {
  const auto Source = tmpFile("bitset-literal-text-wide-character.cpp");
  writeFile(Source, R"r19bitlit(#include <bitset>
int main(){std::bitset<8> a(u"01");return a.any();}
)r19bitlit");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetLiteralTextOwnedLengthRejected) {
  const auto Source = tmpFile("bitset-literal-text-owned-length.cpp");
  writeFile(Source, R"r19bitlit(#define __constexpr_strlen nct_foreign_text_length
#include <bitset>
#undef __constexpr_strlen
int main(){std::bitset<8> a("1010");return a.any();}
)r19bitlit");
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

TEST_F(TranslateTest, CoreV2BitsetBoolSwapGuardMaskRejected) {
  const auto Source = tmpFile("bitset-bool-swap-guard-mask.cpp");
  writeFile(Source, R"r19bitmix(#define __mask_ nct_alternate_mask
#include <bitset>
#undef __mask_
int main(){std::bitset<65> b;bool value=true;using std::swap;swap(b[64],value);return value;}
)r19bitmix");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetBoolSwapGuardReferenceCarrierRejected) {
  const auto Source = tmpFile("bitset-bool-swap-guard-reference-carrier.cpp");
  writeFile(Source, R"r19bitmix(#define __bit_reference nct_alternate_bit_reference
#include <bitset>
#undef __bit_reference
int main(){std::bitset<65> b;bool value=true;using std::swap;swap(b[64],value);return value;}
)r19bitmix");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetBoolSwapGuardStorageRejected) {
  const auto Source = tmpFile("bitset-bool-swap-guard-storage.cpp");
  writeFile(Source, R"r19bitmix(#define __first_ nct_alternate_first
#include <bitset>
#undef __first_
int main(){std::bitset<65> b;bool value=true;using std::swap;swap(b[64],value);return value;}
)r19bitmix");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetBoolSwapGuardOwnedSubscriptRejected) {
  const auto Source = tmpFile("bitset-bool-swap-guard-owned-subscript.cpp");
  writeFile(Source, R"r19bitmix(#include <bitset>
namespace std{template<> auto bitset<65>::operator[](size_t) -> reference{return this->__make_ref(0);}}
int main(){std::bitset<65> b;bool value=true;using std::swap;swap(value,b[64]);return value;}
)r19bitmix");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryMaxUnsignedWordRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-wide-temporary-max-unsigned-word.cpp");
  const auto Output = tmpFile("bitset-wide-temporary-max-unsigned-word.nc");
  writeFile(Source, R"r19bitwidechar(#include <bitset>
int main(){return std::bitset<129>{~0ULL}.to_ullong()!=~0ULL;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-wide-temporary-max-unsigned-word" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryArgumentCalledOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-wide-temporary-argument-called-once.cpp");
  const auto Output = tmpFile("bitset-wide-temporary-argument-called-once.nc");
  writeFile(Source, R"r19bitwidechar(#include <bitset>
static int calls;
unsigned long long word(){++calls;return 0x8000000000000000ULL;}
int main(){auto v=std::bitset<65>(word()).to_ullong();return v!=0x8000000000000000ULL||calls!=1;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-wide-temporary-argument-called-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryModulusFitRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-wide-temporary-modulus-fit.cpp");
  const auto Output = tmpFile("bitset-wide-temporary-modulus-fit.nc");
  writeFile(Source, R"r19bitwidechar(#include <bitset>
static int calls;
unsigned long long source(){++calls;return 9876543210ULL;}
int main(){auto v=std::bitset<65>(source()%257ULL).to_ulong();return v!=9876543210ULL%257ULL||calls!=1;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-wide-temporary-modulus-fit" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetValueSwapOneWordRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-value-swap-one-word.cpp");
  const auto Output = tmpFile("bitset-value-swap-one-word.nc");
  writeFile(Source, R"r19bitvalswap(#include <bitset>
#include <utility>
int main() {
  std::bitset<9> a(0x155), b(0x0fe);
  std::swap(a, b);
  return a.to_ulong() != 0x0fe || b.to_ulong() != 0x155;
}
)r19bitvalswap");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-value-swap-one-word" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetValueSwapExactTemplateRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-value-swap-exact-template.cpp");
  const auto Output = tmpFile("bitset-value-swap-exact-template.nc");
  writeFile(Source, R"r19bitvalswap(#include <bitset>
#include <utility>
int main() {
  std::bitset<65> a(1), b(2);
  b.set(64);
  std::swap<std::bitset<65>>(a, b);
  return a.count() != 2 || !a.test(64) || !a.test(1) || b.count() != 1 || !b.test(0);
}
)r19bitvalswap");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-value-swap-exact-template" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetValueSwapExistingProxyRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-value-swap-existing-proxy.cpp");
  const auto Output = tmpFile("bitset-value-swap-existing-proxy.nc");
  writeFile(Source, R"r19bitvalswap(#include <bitset>
#include <utility>
int main() {
  std::bitset<70> a, b;
  a.set(65);
  b.set(64);
  auto bit = a[65];
  std::swap(a, b);
  if (bit || !b.test(65)) return 1;
  bit.flip();
  return !a.test(65) || !a.test(64) || !b.test(65) || b.test(64);
}
)r19bitvalswap");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-value-swap-existing-proxy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryGuardSavedObjectRejected) {
  const auto Source = tmpFile("bitset-wide-temporary-guard-saved-object.cpp");
  writeFile(Source, R"r19bitwidechar(#include <bitset>
int main(){std::bitset<129> b(33);return b.to_ullong()!=33;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryGuardFactoryValueRejected) {
  const auto Source = tmpFile("bitset-wide-temporary-guard-factory-value.cpp");
  writeFile(Source, R"r19bitwidechar(#include <bitset>
std::bitset<129> make(){return std::bitset<129>{33};}
int main(){return make().to_ullong()!=33;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryGuardStorageRejected) {
  const auto Source = tmpFile("bitset-wide-temporary-guard-storage.cpp");
  writeFile(Source, R"r19bitwidechar(#define __first_ nct_alternate_first
#include <bitset>
#undef __first_
int main(){return std::bitset<129>{33}.to_ullong()!=33;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryGuardOverflowDeclarationRejected) {
  const auto Source = tmpFile("bitset-wide-temporary-guard-overflow-declaration.cpp");
  writeFile(Source, R"r19bitwidechar(#define __throw_overflow_error nct_alternate_throw_overflow_error
#include <bitset>
#undef __throw_overflow_error
int main(){return std::bitset<129>{33}.to_ullong()!=33;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryGuardCharBitRejected) {
  const auto Source = tmpFile("bitset-wide-temporary-guard-char-bit.cpp");
  writeFile(Source, R"r19bitwidechar(#include <climits>
#undef CHAR_BIT
#define CHAR_BIT 8
#include <bitset>
int main(){return std::bitset<129>{33}.to_ullong()!=33;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetValueSwapGuardSdkOperandRejected) {
  const auto Source = tmpFile("bitset-value-swap-guard-sdk-operand.cpp");
  writeFile(Source, R"r19bitvalswap(#define __x nct_swap_operand
#include <bitset>
#include <utility>
#undef __x
int main(){std::bitset<65> a(1),b(2);std::swap(a,b);return !a.test(1);}
)r19bitvalswap");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetValueSwapGuardIndirectCallRejected) {
  const auto Source = tmpFile("bitset-value-swap-guard-indirect-call.cpp");
  writeFile(Source, R"r19bitvalswap(#include <bitset>
#include <utility>
using Bits=std::bitset<65>;using Swap=void(*)(Bits&,Bits&) noexcept;
int main(){Bits a(1),b(2);Swap function=&std::swap<Bits>;function(a,b);return !a.test(1);}
)r19bitvalswap");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstLocalEmptyRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-const-local-empty.cpp");
  const auto Output = tmpFile("bitset-const-local-empty.nc");
  writeFile(Source, R"r19bitlocal(#include <bitset>
int main() {
  const std::bitset<129> bits;
  return bits.to_ulong() != 0UL || bits.to_ullong() != 0ULL;
}
)r19bitlocal");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-const-local-empty" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstLocalWideUnsignedArgumentRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-const-local-wide-unsigned-argument.cpp");
  const auto Output = tmpFile("bitset-const-local-wide-unsigned-argument.nc");
  writeFile(Source, R"r19bitlocal(#include <bitset>
unsigned long long value() { return ~0ULL; }
int main() {
  const std::bitset<129> bits(value());
  return bits.to_ullong() != ~0ULL;
}
)r19bitlocal");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-const-local-wide-unsigned-argument" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstLocalLiveSourceIndependenceRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-const-local-live-source-independence.cpp");
  const auto Output = tmpFile("bitset-const-local-live-source-independence.nc");
  writeFile(Source, R"r19bitlocal(#include <bitset>
int main() {
  unsigned long long value = 31;
  const std::bitset<65> bits(value & 0xffffffffULL);
  value = 73;
  return bits.to_ulong() != 31UL || value != 73;
}
)r19bitlocal");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-const-local-live-source-independence" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstantPositionNamespaceConstantRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-constant-position-namespace-constant.cpp");
  const auto Output = tmpFile("bitset-constant-position-namespace-constant.nc");
  writeFile(Source, R"r19bitice(#include <bitset>
constexpr std::size_t high = 64;
int main() {
  std::bitset<70> bits;
  bits.set(high);
  return !bits.test(high) || bits.count() != 1;
}
)r19bitice");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-constant-position-namespace-constant" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstantPositionEnumIndexRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-constant-position-enum-index.cpp");
  const auto Output = tmpFile("bitset-constant-position-enum-index.nc");
  writeFile(Source, R"r19bitice(#include <bitset>
enum Position : unsigned { boundary = 63 };
int main() {
  std::bitset<70> bits;
  bits.set(boundary).set(static_cast<unsigned>(boundary) + 1);
  return !bits.test(boundary) || !bits.test(64) || bits.count() != 2;
}
)r19bitice");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-constant-position-enum-index" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstantPositionConstexprFunctionRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-constant-position-constexpr-function.cpp");
  const auto Output = tmpFile("bitset-constant-position-constexpr-function.nc");
  writeFile(Source, R"r19bitice(#include <bitset>
constexpr std::size_t at(bool high) { return high ? 65 : 0; }
int main() {
  std::bitset<70> bits;
  bits.set(at(true));
  bits.set(at(false));
  bits.reset(at(true));
  return !bits.test(at(false)) || bits.test(at(true)) || bits.count() != 1;
}
)r19bitice");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-constant-position-constexpr-function" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetIndexTypeLastByteIndexRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-index-type-last-byte-index.cpp");
  const auto Output = tmpFile("bitset-index-type-last-byte-index.nc");
  writeFile(Source, R"r19bitwiden(#include <bitset>
void set(std::bitset<256>& bits, unsigned char pos) { bits.set(pos); }
int main() {
  std::bitset<256> bits;
  set(bits, 255);
  return !bits.test(255) || bits.count() != 1;
}
)r19bitwiden");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-index-type-last-byte-index" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetIndexTypeBooleanIndexRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-index-type-boolean-index.cpp");
  const auto Output = tmpFile("bitset-index-type-boolean-index.nc");
  writeFile(Source, R"r19bitwiden(#include <bitset>
bool set_and_test(std::bitset<2>& bits, bool position) {
  bits.set(position);
  return bits.test(position);
}
int main() {
  std::bitset<2> bits;
  if (!set_and_test(bits, true) || !set_and_test(bits, false)) return 1;
  return !bits.all();
}
)r19bitwiden");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-index-type-boolean-index" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetIndexTypeUnsignedPointerLoadRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-index-type-unsigned-pointer-load.cpp");
  const auto Output = tmpFile("bitset-index-type-unsigned-pointer-load.nc");
  writeFile(Source, R"r19bitwiden(#include <bitset>
bool test(const std::bitset<256>& bits, const unsigned char* pos) {
  return bits.test(*pos);
}
int main() {
  std::bitset<256> bits;
  bits.set(211);
  unsigned char p = 211;
  return !test(bits, &p);
}
)r19bitwiden");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-index-type-unsigned-pointer-load" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralDefaultDigitsRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-owned-literal-default-digits.cpp");
  const auto Output = tmpFile("bitset-owned-literal-default-digits.nc");
  writeFile(Source, R"r19bitowned(#include <bitset>
#include <string>
int main() {
  std::bitset<7> value{std::string("0010110")};
  return value.to_ulong() != 22 || value.count() != 3;
}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-owned-literal-default-digits" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralAllocatedStringCrossesWordRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-owned-literal-allocated-string-crosses-word.cpp");
  const auto Output = tmpFile("bitset-owned-literal-allocated-string-crosses-word.nc");
  writeFile(Source, R"r19bitowned(#include <bitset>
#include <string>
int main() {
  std::bitset<70> value{std::string("1000000000000000000000000000000000000000000000000000000000000000001001", 70)};
  return value.count() != 3 || !value.test(69) || !value.test(3) || !value.test(0);
}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-owned-literal-allocated-string-crosses-word" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralEmptyInputRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-owned-literal-empty-input.cpp");
  const auto Output = tmpFile("bitset-owned-literal-empty-input.nc");
  writeFile(Source, R"r19bitowned(#include <bitset>
#include <string>
int main() {
  std::bitset<0> empty{std::string{}};
  std::bitset<8> end{std::string("11"), 2};
  std::bitset<8> zero{std::string("invalid"), 0, 0};
  return empty.any() || !empty.all() || end.any() || zero.any();
}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-owned-literal-empty-input" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstexprTextPointerLocalRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-constexpr-text-pointer-local.cpp");
  const auto Output = tmpFile("bitset-constexpr-text-pointer-local.nc");
  writeFile(Source, R"r19bitctxptr(#include <bitset>
int main() {
  constexpr const char* text = "101101";
  return std::bitset<8>(text).to_ulong() != 45UL;
}
)r19bitctxptr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-constexpr-text-pointer-local" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstexprTextPointerFirstNullRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-constexpr-text-pointer-first-null.cpp");
  const auto Output = tmpFile("bitset-constexpr-text-pointer-first-null.nc");
  writeFile(Source, R"r19bitctxptr(#include <bitset>
int main() {
  constexpr const char* text = "101\0x1";
  return std::bitset<8>(text).to_ullong() != 5ULL;
}
)r19bitctxptr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-constexpr-text-pointer-first-null" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstexprTextPointerPointerTypeAliasRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-constexpr-text-pointer-pointer-type-alias.cpp");
  const auto Output = tmpFile("bitset-constexpr-text-pointer-pointer-type-alias.nc");
  writeFile(Source, R"r19bitctxptr(#include <bitset>
using Text = const char*;
int main() {
  constexpr Text text = "101";
  std::bitset<128> bits(text);
  return bits.count() != 2 || !bits.test(0) || !bits.test(2) || bits.test(64);
}
)r19bitctxptr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-constexpr-text-pointer-pointer-type-alias" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetWideSwapAndCheckedPositionsRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-composed-wide-swap-and-positions.cpp");
  writeFile(Source, R"r19bitcomposed(#include <bitset>

static unsigned calls;
unsigned char position() { ++calls; return 255; }
constexpr unsigned index() { return 64 - 1; }
int main() {
  const std::bitset<256> initial(1ULL << 63);
  std::bitset<256> left(initial);
  std::bitset<256> right(2);
  std::swap(left, right);
  left.set(position());
  return initial.to_ullong() != (1ULL << 63) || !right.test(index()) ||
         !left.test(255) || !left.test(1) || calls != 1;
}
)r19bitcomposed");
  const auto Output = tmpFile("bitset-composed-wide-swap-and-positions.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-composed-wide-swap-and-positions" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedConversionSelectsImmutableReceiver) {
  const auto Source = tmpFile("bitset-const-local-guard-conditional-receiver.cpp");
  writeFile(Source, R"r19bitlocal(#include <bitset>
using Bits = std::bitset<129>;
unsigned long long convert(bool select){const Bits a(1),b(2);return (select?a:b).to_ullong();}
int main(){return convert(true)!=1||convert(false)!=2;}
)r19bitlocal");
  const auto Output = tmpFile("bitset-const-local-guard-conditional-receiver.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-const-local-guard-conditional-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedConversionAcceptsStaticConstant) {
  const auto Source = tmpFile("bitset-const-local-guard-static-const.cpp");
  writeFile(Source, R"r19bitlocal(#include <bitset>
using Bits = std::bitset<129>;
int main(){static const Bits bits(2);return bits.to_ullong()!=2;}
)r19bitlocal");
  const auto Output = tmpFile("bitset-const-local-guard-static-const.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-const-local-guard-static-const" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstLocalGuardMutableLocalRejected) {
  const auto Source = tmpFile("bitset-const-local-guard-mutable-local.cpp");
  writeFile(Source, R"r19bitlocal(#include <bitset>
using Bits = std::bitset<129>;
int main(){Bits bits(1);bits.set(1);return bits.to_ullong()!=3;}
)r19bitlocal");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstLocalGuardConstParameterRejected) {
  const auto Source = tmpFile("bitset-const-local-guard-const-parameter.cpp");
  writeFile(Source, R"r19bitlocal(#include <bitset>
using Bits = std::bitset<129>;
unsigned long long convert(const Bits& bits){return bits.to_ullong();}
int main(){const Bits bits(2);return convert(bits)!=2;}
)r19bitlocal");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstantPositionGuardOnePastRejected) {
  const auto Source = tmpFile("bitset-constant-position-guard-one-past.cpp");
  writeFile(Source, R"r19bitice(#include <bitset>
int main(){std::bitset<65> b;constexpr unsigned p=64;b.flip(p+1);return 0;}
)r19bitice");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstantPositionGuardEmptyRejected) {
  const auto Source = tmpFile("bitset-constant-position-guard-empty.cpp");
  writeFile(Source, R"r19bitice(#include <bitset>
int main(){std::bitset<0> b;constexpr unsigned p=0;b.set(p);return 0;}
)r19bitice");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstantPositionGuardIndexWithEffectRejected) {
  const auto Source = tmpFile("bitset-constant-position-guard-index-with-effect.cpp");
  writeFile(Source, R"r19bitice(#include <bitset>
int main(){std::bitset<65> b;unsigned p=0;b.set((++p,32));return p!=1;}
)r19bitice");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstantPositionGuardSourceBodyRejected) {
  const auto Source = tmpFile("bitset-constant-position-guard-source-body.cpp");
  writeFile(Source, R"r19bitice(#include <bitset>
namespace std{template<> bool bitset<65>::test(size_t) const{return true;}}
int main(){std::bitset<65> b;constexpr unsigned p=2;return b.test(p);}
)r19bitice");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetIndexTypeGuardTooShortForByteRejected) {
  const auto Source = tmpFile("bitset-index-type-guard-too-short-for-byte.cpp");
  writeFile(Source, R"r19bitwiden(#include <bitset>
bool check(unsigned char p){std::bitset<255> bits;return bits.test(p);}
int main(){return check(2);}
)r19bitwiden");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetIndexTypeGuardOffsetAfterPromotionRejected) {
  const auto Source = tmpFile("bitset-index-type-guard-offset-after-promotion.cpp");
  writeFile(Source, R"r19bitwiden(#include <bitset>
bool check(unsigned char p){std::bitset<256> bits;return bits.test(p+1);}
int main(){return check(2);}
)r19bitwiden");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetIndexTypeGuardExplicitSizeConversionRejected) {
  const auto Source = tmpFile("bitset-index-type-guard-explicit-size-conversion.cpp");
  writeFile(Source, R"r19bitwiden(#include <bitset>
bool check(unsigned char p){std::bitset<256> bits;return bits.test(static_cast<std::size_t>(p));}
int main(){return check(2);}
)r19bitwiden");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetIndexTypeGuardOwnedSdkBodyRejected) {
  const auto Source = tmpFile("bitset-index-type-guard-owned-sdk-body.cpp");
  writeFile(Source, R"r19bitwiden(#include <bitset>
namespace std{template<> bool bitset<256>::test(size_t) const{return true;}}
bool check(unsigned char p){std::bitset<256> bits;return bits.test(p);}
int main(){return check(2);}
)r19bitwiden");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralGuardFactoryStringRejected) {
  const auto Source = tmpFile("bitset-owned-literal-guard-factory-string.cpp");
  writeFile(Source, R"r19bitowned(#include <bitset>
#include <string>
std::string text(){return std::string("101");}
int main(){return std::bitset<8>{text()}.any();}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralGuardRuntimeOffsetRejected) {
  const auto Source = tmpFile("bitset-owned-literal-guard-runtime-offset.cpp");
  writeFile(Source, R"r19bitowned(#include <bitset>
#include <string>
bool use(unsigned position){return std::bitset<8>{std::string("101"),position}.any();}
int main(){return use(1)!=true;}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralGuardNonliteralConstructorInputRejected) {
  const auto Source = tmpFile("bitset-owned-literal-guard-nonliteral-constructor-input.cpp");
  writeFile(Source, R"r19bitowned(#include <bitset>
#include <string>
bool use(const char* data){return std::bitset<8>{std::string(data)}.any();}
int main(){return use("101")!=true;}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralGuardSdkBodyRejected) {
  const auto Source = tmpFile("bitset-owned-literal-guard-sdk-body.cpp");
  writeFile(Source, R"r19bitowned(#define __init_from_string_view nct_alternate_init
#include <bitset>
#include <string>
#undef __init_from_string_view
int main(){return std::bitset<8>{std::string("101")}.to_ulong()!=5;}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstexprTextPointerGuardMutablePointerRejected) {
  const auto Source = tmpFile("bitset-constexpr-text-pointer-guard-mutable-pointer.cpp");
  writeFile(Source, R"r19bitctxptr(#include <bitset>
int main(){const char* p="1";p="0";return std::bitset<8>(p).any();}
)r19bitctxptr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstexprTextPointerGuardPointerArithmeticRejected) {
  const auto Source = tmpFile("bitset-constexpr-text-pointer-guard-pointer-arithmetic.cpp");
  writeFile(Source, R"r19bitctxptr(#include <bitset>
int main(){constexpr const char* p="01"+1;return std::bitset<8>(p).any();}
)r19bitctxptr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstexprTextPointerGuardIndirectInitializerRejected) {
  const auto Source = tmpFile("bitset-constexpr-text-pointer-guard-indirect-initializer.cpp");
  writeFile(Source, R"r19bitctxptr(#include <bitset>
constexpr const char* text="1";int main(){constexpr const char* p=text;return std::bitset<8>(p).any();}
)r19bitctxptr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstexprTextPointerGuardInvalidBeyondWidthRejected) {
  const auto Source = tmpFile("bitset-constexpr-text-pointer-guard-invalid-beyond-width.cpp");
  writeFile(Source, R"r19bitctxptr(#include <bitset>
int main(){constexpr const char* p="1?";return std::bitset<1>(p).any();}
)r19bitctxptr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetWideSwapAndOutOfBoundsPositionRejected) {
  const auto Source = tmpFile("bitset-composed-wide-swap-and-position-guard.cpp");
  writeFile(Source, R"r19bitcomposed(#include <bitset>

static unsigned calls;
unsigned char position() { ++calls; return 255; }
constexpr unsigned index() { return 64 - 1; }
int main() {
  const std::bitset<256> initial(1ULL << 63);
  std::bitset<256> left(initial);
  std::bitset<256> right(2);
  std::swap(left, right);
  left.set(position() + 1);
  return initial.to_ullong() != (1ULL << 63) || !right.test(index()) ||
         !left.test(255) || !left.test(1) || calls != 1;
}
)r19bitcomposed");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetTextWideConversionUnsignedLongLongWidthRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-text-wide-conversion-unsigned-long-long-width.cpp");
  const auto Output = tmpFile("bitset-text-wide-conversion-unsigned-long-long-width.nc");
  writeFile(Source, R"r19bittextwide(#include <bitset>
#include <string>
int main(){return std::bitset<128>("1000000000000000000000000000000000000000000000000000000000000001").to_ullong()!=0x8000000000000001ULL;}
)r19bittextwide");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-text-wide-conversion-unsigned-long-long-width" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetTextWideConversionOwningSubstringRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-text-wide-conversion-owning-substring.cpp");
  const auto Output = tmpFile("bitset-text-wide-conversion-owning-substring.nc");
  writeFile(Source, R"r19bittextwide(#include <bitset>
#include <string>
int main(){
  const std::bitset<129> value(std::string("skipxoo"),4,3,'o','x');
  return value.to_ulong()!=4UL;
}
)r19bittextwide");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-text-wide-conversion-owning-substring" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetTextWideConversionZeroUpperStorageRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-text-wide-conversion-zero-upper-storage.cpp");
  const auto Output = tmpFile("bitset-text-wide-conversion-zero-upper-storage.nc");
  writeFile(Source, R"r19bittextwide(#include <bitset>
#include <string>
int main(){
  const std::bitset<194> value("0000000000000000000000000000000000000000000000000000000000000000001000000000000000000000000000000000000000000000000000000000000001");
  return value.to_ullong()!=0x8000000000000001ULL;
}
)r19bittextwide");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-text-wide-conversion-zero-upper-storage" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetTextWideConversionAboveUnsignedLongLongRejected) {
  const auto Source = tmpFile("bitset-text-wide-conversion-above-unsigned-long-long.cpp");
  writeFile(Source, R"r19bittextwide(#include <bitset>
#include <string>
int main(){
  const std::bitset<129> value(std::string("10000000000000000000000000000000000000000000000000000000000000000"));
  return value.to_ullong()==0ULL;
}
)r19bittextwide");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetTextWideConversionMutableObjectRejected) {
  const auto Source = tmpFile("bitset-text-wide-conversion-mutable-object.cpp");
  writeFile(Source, R"r19bittextwide(#include <bitset>
#include <string>
int main(){std::bitset<129> value("1");value.set(128);return value.to_ullong()==0ULL;}
)r19bittextwide");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetTextWideConversionInvalidDigitInSelectedRangeRejected) {
  const auto Source = tmpFile("bitset-text-wide-conversion-invalid-digit-in-selected-range.cpp");
  writeFile(Source, R"r19bittextwide(#include <bitset>
#include <string>
int main(){return std::bitset<129>(std::string("101?")).to_ullong()==0ULL;}
)r19bittextwide");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantMetadataAlternativeRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-metadata-alternative.cpp");
  writeFile(Source, R"r19variant(#include <variant>
using V = std::variant<int,double>;
using R = std::variant_alternative_t<1,V>;
R answer(){return 3.5;}
int main(){return answer()!=3.5;}
)r19variant");
  const auto Output = tmpFile("variant-metadata-alternative.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-metadata-alternative" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMetadataNestedAlternativeRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-metadata-nested-alternative.cpp");
  writeFile(Source, R"r19variant(#include <variant>
using Inner = std::variant<int,float>;
using Outer = std::variant<Inner,char>;
using Selected = std::variant_alternative_t<0,Outer>;
int main(){return std::variant_size_v<Selected>!=2;}
)r19variant");
  const auto Output = tmpFile("variant-metadata-nested-alternative.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-metadata-nested-alternative" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMetadataWrittenIndexRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-metadata-written-index.cpp");
  writeFile(Source, R"r19variant(#include <variant>
using V = std::variant<int,float,unsigned long>;
constexpr unsigned last(){return 3-1;}
using R = std::variant_alternative_t<last(),V>;
int main(){R v=19UL;return v!=19UL;}
)r19variant");
  const auto Output = tmpFile("variant-metadata-written-index.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-metadata-written-index" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantLayoutReferenceAndConstRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-layout-reference-and-const.cpp");
  writeFile(Source, R"r19variantlayout(#include <variant>
using V = std::variant<long long, char>;
int main() { return sizeof(const V&) != sizeof(V) || alignof(const V&) != alignof(long long); }
)r19variantlayout");
  const auto Output = tmpFile("variant-layout-reference-and-const.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-layout-reference-and-const" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantLayoutDuplicateTypesRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-layout-duplicate-types.cpp");
  writeFile(Source, R"r19variantlayout(#include <variant>
using V = std::variant<double, int, double>;
int main() { return sizeof(V) != 2 * sizeof(double) || alignof(V) != alignof(double); }
)r19variantlayout");
  const auto Output = tmpFile("variant-layout-duplicate-types.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-layout-duplicate-types" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantSizeofRuntimeRecordRunsAtBothOptimizations) {
  const auto Source = tmpFile("variant-layout-runtime-record.cpp");
  writeFile(Source, R"r19variantlayout(#include <variant>
using V = std::variant<int, double>;
int main() { V value{9}; return sizeof(value) == 0; }
)r19variantlayout");
  const auto Output = tmpFile("variant-layout-runtime-record.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-layout-runtime-record" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMetadataVariantSizeStorageRejected) {
  const auto Source = tmpFile("variant-metadata-variant-size-storage.cpp");
  writeFile(Source, R"r19variant(#include <variant>
using V=std::variant<int,double>;
int main(){const auto *p=&std::variant_size_v<V>;return *p!=2;}
)r19variant");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantMetadataSourceSpecializationRejected) {
  const auto Source = tmpFile("variant-metadata-source-specialization.cpp");
  writeFile(Source, R"r19variant(#include <variant>
struct Record {};
namespace std { template<> struct variant_size<Record> : integral_constant<size_t,1> {}; }
int main(){return std::variant_size_v<Record>!=1;}
)r19variant");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantMetadataDirectVolatileAlternativeRejected) {
  const auto Source = tmpFile("variant-metadata-direct-volatile-alternative.cpp");
  writeFile(Source, R"r19variant(#include <variant>
int main() { return std::variant_size_v<std::variant<int, volatile int>> != 2; }
)r19variant");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantLayoutUnsupportedLargeFloatRejected) {
  const auto Source = tmpFile("variant-layout-unsupported-large-float.cpp");
  writeFile(Source, R"r19variantlayout(#include <variant>
using V = std::variant<int, long double>;
int main() { return sizeof(V) == 0; }
)r19variantlayout");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantObjectsSecondRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-second.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
extern "C" int check() {
  std::variant<int, double> value = 2.5;
  return value.index() != 1 || value.valueless_by_exception();
}
int main() { return check(); }
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-second.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-second" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsCopyRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-copy.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
extern "C" int check() {
  std::variant<int, double> source = 3.0;
  const std::variant<int, double> copy = source;
  return copy.index() != 1 || copy.valueless_by_exception();
}
int main() { return check(); }
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-copy.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsRefAndSideEffectRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-ref-and-side-effect.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
using Choice = std::variant<int, double>;
int attempts;
const Choice& once(const Choice& choice) { ++attempts; return choice; }
extern "C" int check() {
  Choice choice = 4.0;
  return once(choice).index() != 1 || attempts != 1 ||
         once(choice).valueless_by_exception() || attempts != 2;
}
int main() { return check(); }
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-ref-and-side-effect.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-ref-and-side-effect" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsGetIfAliasRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-get-if-alias.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
extern "C" int check() {
  std::variant<int, double> value = 3;
  auto *first = std::get_if<0>(&value);
  if (!first || std::get_if<1>(&value) || *first != 3) return 1;
  *first = 11;
  auto *again = std::get_if<int>(&value);
  return again != first || *again != 11;
}
int main() { return check(); }
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-get-if-alias.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-get-if-alias" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsGetIfDuplicateRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-get-if-duplicate.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
extern "C" int check() {
  std::variant<int, int> value;
  const auto *p = std::get_if<0>(&value);
  return !p || *p != 0 || std::get_if<1>(&value);
}
int main() { return check(); }
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-get-if-duplicate.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-get-if-duplicate" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsHoldsConstRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-holds-const.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
int main() {
  const std::variant<int, double> value = 3.0;
  return !std::holds_alternative<double>(value) || std::holds_alternative<int>(value);
}
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-holds-const.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-holds-const" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsMoveAndSelfAssignmentRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-move-and-self-assignment.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
int main() {
  using V = std::variant<int, double>;
  V original = 6;
  V copy = 3.0;
  copy = static_cast<V&&>(original);
  copy = copy;
  auto* result = std::get_if<int>(&copy);
  return !result || *result != 6 || !std::holds_alternative<int>(original);
}
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-move-and-self-assignment.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-move-and-self-assignment" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsConstGetByTypeRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-const-get-by-type.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
int main() {
  const std::variant<int, double> value{2.5};
  return std::get<double>(value) != 2.5;
}
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-const-get-by-type.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-const-get-by-type" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantPromotedIntegerConstructionRunsAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-unproved-conversion.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
extern "C" int check() {
  short original = 7;
  std::variant<int, double> value = original;
  return int(value.index());
}
int main() { return check(); }
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-unproved-conversion.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-unproved-conversion" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsAlteredDiscriminantRejected) {
  const auto Source = tmpFile("variant-objects-altered-discriminant.cpp");
  writeFile(Source, R"r19variantsclr(#define __index renamed_variant_discriminant
#include <variant>
#undef __index
extern "C" int check() { std::variant<int, double> value; return int(value.index()); }
int main() { return check(); }
)r19variantsclr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantObjectsConstGetInactiveRejected) {
  const auto Source = tmpFile("variant-objects-const-get-inactive.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
int main() {
  const std::variant<int, double> value{2.5};
  return std::get<0>(value);
}
)r19variantsclr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantObjectsConstGetAlteredHelperRejected) {
  const auto Source = tmpFile("variant-objects-const-get-altered-helper.cpp");
  writeFile(Source, R"r19variantsclr(#define __generic_get replaced_generic_get
#include <variant>
#undef __generic_get
int main() {
  const std::variant<int, double> value{7};
  return std::get<int>(value);
}
)r19variantsclr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantMonostateIdentityRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-monostate-identity.cpp");
  writeFile(Source, R"r19monostate(#include <variant>
#include <type_traits>
using M = std::monostate;
using V = std::variant<M, int>;
int main() {
  return !std::is_same_v<M, std::variant_alternative_t<0, V>> ||
         !std::is_same_v<const M, std::variant_alternative_t<0, const V>> ||
         std::variant_size_v<V> != 2;
}
)r19monostate");
  const auto Output = tmpFile("variant-monostate-identity.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-monostate-identity" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMonostateVariantLayoutRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-monostate-variant-layout.cpp");
  writeFile(Source, R"r19monostate(#include <variant>
using V = std::variant<std::monostate, int>;
using R = std::variant<unsigned char, int>;
int main() { return sizeof(V) != sizeof(R) || alignof(const V&) != alignof(R); }
)r19monostate");
  const auto Output = tmpFile("variant-monostate-variant-layout.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-monostate-variant-layout" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyLayoutDirectSizeAlignRunAtBothOptimizations) {
  const auto Source = tmpFile("any-layout-direct-size-align.cpp");
  writeFile(Source, R"r19any(#include <any>
int main() {
  return sizeof(std::any) != 4 * sizeof(void*) || alignof(std::any) != alignof(void*);
}
)r19any");
  const auto Output = tmpFile("any-layout-direct-size-align.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-layout-direct-size-align" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyLayoutWrappedPointerTypeRunAtBothOptimizations) {
  const auto Source = tmpFile("any-layout-wrapped-pointer-type.cpp");
  writeFile(Source, R"r19any(#include <any>
#include <type_traits>
using A = const std::any;
static_assert(std::is_same_v<A*, const std::any*>);
int main() { return sizeof(A) != 4 * sizeof(void*); }
)r19any");
  const auto Output = tmpFile("any-layout-wrapped-pointer-type.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-layout-wrapped-pointer-type" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeMapIdentityRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-map-identity.cpp");
  writeFile(Source, R"r19assoc(#include <map>
#include <type_traits>
using Container = std::map<int, int>;
int main() { return !std::is_same_v<Container, std::map<int, int>>; }
)r19assoc");
  const auto Output = tmpFile("associative-map-identity.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-map-identity" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeMapSizeofRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-map-sizeof.cpp");
  writeFile(Source, R"r19assoc(#include <map>
#include <type_traits>
using Container = std::map<int, int>;
int main() { return sizeof(Container) != 3 * sizeof(void*); }
)r19assoc");
  const auto Output = tmpFile("associative-map-sizeof.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-map-sizeof" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeSetIdentityRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-set-identity.cpp");
  writeFile(Source, R"r19assoc(#include <set>
#include <type_traits>
using Container = std::set<int>;
int main() { return !std::is_same_v<Container, std::set<int>>; }
)r19assoc");
  const auto Output = tmpFile("associative-set-identity.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-set-identity" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeSetSizeofRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-set-sizeof.cpp");
  writeFile(Source, R"r19assoc(#include <set>
#include <type_traits>
using Container = std::set<int>;
int main() { return sizeof(Container) != 3 * sizeof(void*); }
)r19assoc");
  const auto Output = tmpFile("associative-set-sizeof.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-set-sizeof" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeUnorderedSetIdentityRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-unordered_set-identity.cpp");
  writeFile(Source, R"r19assoc(#include <unordered_set>
#include <type_traits>
using Container = std::unordered_set<int>;
int main() { return !std::is_same_v<Container, std::unordered_set<int>>; }
)r19assoc");
  const auto Output = tmpFile("associative-unordered_set-identity.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-unordered_set-identity" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeUnorderedSetSizeofRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-unordered_set-sizeof.cpp");
  writeFile(Source, R"r19assoc(#include <unordered_set>
#include <type_traits>
using Container = std::unordered_set<int>;
int main() { return sizeof(Container) != 5 * sizeof(void*); }
)r19assoc");
  const auto Output = tmpFile("associative-unordered_set-sizeof.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-unordered_set-sizeof" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMonostateValueActiveEmptyRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-monostate-value-active-empty.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
using V = std::variant<std::monostate, int>;
int main() {
  V value;
  return value.index() != 0 || value.valueless_by_exception() ||
         !std::holds_alternative<std::monostate>(value) ||
         !std::get_if<0>(&value) || !std::get_if<std::monostate>(&value) ||
         std::get_if<1>(&value);
}
)r19varvalue");
  const auto Output = tmpFile("variant-monostate-value-active-empty.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-monostate-value-active-empty" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMonostateValueCopyAndAssignRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-monostate-value-copy-and-assign.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
using V = std::variant<std::monostate, int>;
int main() {
  V source;
  V copy(source);
  const V scalar(37);
  source = scalar;
  const auto* number = std::get_if<int>(&source);
  return !number || *number != 37 || std::get_if<0>(&source) != nullptr ||
         !std::get_if<0>(&copy) || copy.index() != 0;
}
)r19varvalue");
  const auto Output = tmpFile("variant-monostate-value-copy-and-assign.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-monostate-value-copy-and-assign" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantIndexConstructionRepeatedTypeRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-index-construction-repeated-type.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
int main() {
  std::variant<int, int> value(std::in_place_index<1>, 31);
  auto* selected = std::get_if<1>(&value);
  return value.index() != 1 || !selected || *selected != 31 || std::get_if<0>(&value);
}
)r19varvalue");
  const auto Output = tmpFile("variant-index-construction-repeated-type.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-index-construction-repeated-type" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantIndexConstructionArgumentEffectRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-index-construction-argument-effect.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
long payload;
long& next() { ++payload; return payload; }
int main() {
  const std::variant<int, long> value(std::in_place_index<1>, next());
  return payload != 1 || std::get<1>(value) != 1;
}
)r19varvalue");
  const auto Output = tmpFile("variant-index-construction-argument-effect.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-index-construction-argument-effect" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantTypeConstructionValueAndEffectRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-type-construction-value-and-effect.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
long count;
long& next() { ++count; return count; }
int main() {
  using Selected = long;
  const std::variant<int, Selected> value(std::in_place_type<Selected>, next());
  return count != 1 || value.index() != 1 || std::get<Selected>(value) != 1;
}
)r19varvalue");
  const auto Output = tmpFile("variant-type-construction-value-and-effect.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-type-construction-value-and-effect" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantTypeConstructionEmptyAlternativeRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-type-construction-empty-alternative.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
int main() {
  std::variant<int, std::monostate> value(std::in_place_type<std::monostate>);
  return value.index() != 1 || !std::holds_alternative<std::monostate>(value) ||
         std::get_if<int>(&value) != nullptr;
}
)r19varvalue");
  const auto Output = tmpFile("variant-type-construction-empty-alternative.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-type-construction-empty-alternative" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapStoresFloatMappedValues) {
  const auto Source = tmpFile("associative-map-custom-type-or-policy.cpp");
  writeFile(Source, R"r19assoc(#include <map>
#include <type_traits>
using Container = std::map<int, double>;
int main() { return sizeof(Container) == 0; }
)r19assoc");
  const auto Output = tmpFile("associative-map-custom-type-or-policy.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-map-custom-type-or-policy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnySizeofExpressionRunsAtBothOptimizations) {
  const auto Source = tmpFile("any-layout-unevaluated-object.cpp");
  writeFile(Source, R"r19any(#include <any>
int main() { return sizeof(std::any{}) == 0; }
)r19any");
  const auto Output = tmpFile("any-layout-unevaluated-object.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-layout-unevaluated-object" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMonostateSourceRedeclarationRejected) {
  const auto Source = tmpFile("variant-monostate-source-redeclaration.cpp");
  writeFile(Source, R"r19monostate(#include <variant>
_LIBCPP_BEGIN_NAMESPACE_STD
struct monostate;
_LIBCPP_END_NAMESPACE_STD
using V = std::variant<std::monostate, int>;
int main() { return sizeof(V) == 0; }
)r19monostate");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyLayoutVolatileTypeRejected) {
  const auto Source = tmpFile("any-layout-volatile-type.cpp");
  writeFile(Source, R"r19any(#include <any>
int main() { return sizeof(volatile std::any) == 0; }
)r19any");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AssociativeMapRenamedContainerStorageRejected) {
  const auto Source = tmpFile("associative-map-renamed-container-storage.cpp");
  writeFile(Source, R"r19assoc(#define __tree_ altered_owner_storage
#include <map>
#include <type_traits>
#undef __tree_
using Container = std::map<int, int>;
int main() { return sizeof(Container) == 0; }
)r19assoc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AssociativeSetRenamedContainerStorageRejected) {
  const auto Source = tmpFile("associative-set-renamed-container-storage.cpp");
  writeFile(Source, R"r19assoc(#define __tree_ altered_owner_storage
#include <set>
#include <type_traits>
#undef __tree_
using Container = std::set<int>;
int main() { return sizeof(Container) == 0; }
)r19assoc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AssociativeUnorderedSetCustomTypeOrPolicyRejected) {
  const auto Source = tmpFile("associative-unordered_set-custom-type-or-policy.cpp");
  writeFile(Source, R"r19assoc(#include <unordered_set>
#include <type_traits>
struct CustomHash { unsigned long long operator()(int key) const { return static_cast<unsigned int>(key); } };
using Container = std::unordered_set<int, CustomHash>;
int main() { return sizeof(Container) == 0; }
)r19assoc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AssociativeUnorderedSetRuntimeStorageGuardRejected) {
  const auto Source = tmpFile("associative-unordered_set-runtime-storage-guard.cpp");
  writeFile(Source, R"r19assoc(#include <unordered_set>
#include <type_traits>
using Container = std::unordered_set<int>;
int main() { Container value; return sizeof(value) == 0; }
)r19assoc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantMonostateValueForeignEmptyRejected) {
  const auto Source = tmpFile("variant-monostate-value-foreign-empty.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
struct Empty {};
int main() {
  std::variant<Empty, int> value;
  return value.index();
}
)r19varvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantIndexConstructionWrittenTagObjectRejected) {
  const auto Source = tmpFile("variant-index-construction-written-tag-object.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
int f() {
  std::variant<int, long> value(std::in_place_index_t<1>{}, 12L);
  return value.index();
}
)r19varvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantTypeConstructionWrittenTagObjectRejected) {
  const auto Source = tmpFile("variant-type-construction-written-tag-object.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
int f() {
  std::variant<int, long> value(std::in_place_type_t<long>{}, 19L);
  return value.index();
}
)r19varvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyEmptyDefaultLocalRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-default-local.cpp");
  writeFile(Source, R"r19any(#include <any>
int main() {
  std::any a;
  if (a.has_value()) return 1;
  a.reset();
  a.reset();
  return a.has_value();
}
)r19any");
  const auto Output = tmpFile("any-empty-default-local.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-default-local" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyTemporaryRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-temporary.cpp");
  writeFile(Source, R"r19any(#include <any>
int main() {
  return std::any{}.has_value();
}
)r19any");
  const auto Output = tmpFile("any-empty-temporary.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-temporary" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyArrayLifetimeRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-array-lifetime.cpp");
  writeFile(Source, R"r19any(#include <any>
int main() {
  std::any values[2];
  values[1].reset();
  return values[0].has_value() || values[1].has_value();
}
)r19any");
  const auto Output = tmpFile("any-empty-array-lifetime.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-array-lifetime" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyPointerCastMutableRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-pointer-cast-mutable.cpp");
  writeFile(Source, R"r19any(#include <any>
int main() {
  std::any value;
  int* payload = std::any_cast<int>(&value);
  return payload != nullptr || value.has_value();
}
)r19any");
  const auto Output = tmpFile("any-empty-pointer-cast-mutable.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-pointer-cast-mutable" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyPointerCastNullAndEvaluationRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-pointer-cast-null-and-evaluation.cpp");
  writeFile(Source, R"r19any(#include <any>
int calls;
std::any* choose(std::any* value) { ++calls; return value; }
int main() {
  std::any value;
  auto* first = std::any_cast<int>(choose(&value));
  auto* second = std::any_cast<int>(choose(nullptr));
  return first != nullptr || second != nullptr || calls != 2;
}
)r19any");
  const auto Output = tmpFile("any-empty-pointer-cast-null-and-evaluation.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-pointer-cast-null-and-evaluation" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyCopyMoveRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-copy-move.cpp");
  writeFile(Source, R"r19any(#include <any>
#include <utility>
int main() {
  std::any original;
  std::any moved(std::move(original));
  return original.has_value() || moved.has_value() || std::any_cast<int>(&moved);
}
)r19any");
  const auto Output = tmpFile("any-empty-copy-move.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-copy-move" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyAssignmentCopyIdentityRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-assignment-copy-identity.cpp");
  writeFile(Source, R"r19any(#include <any>
int main() {
  std::any target;
  const std::any source;
  return &(target = source) != &target || target.has_value() || source.has_value();
}
)r19any");
  const auto Output = tmpFile("any-empty-assignment-copy-identity.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-assignment-copy-identity" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyAssignmentExplicitAssignmentOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-assignment-explicit-assignment-order.cpp");
  writeFile(Source, R"r19any(#include <any>
int effects;
std::any* select(std::any* value, int number) { effects = effects * 10 + number; return value; }
int main() {
  std::any first, second;
  auto& result = select(&first, 1)->operator=(*select(&second, 2));
  return effects != 12 || &result != &first || first.has_value() || second.has_value();
}
)r19any");
  const auto Output = tmpFile("any-empty-assignment-explicit-assignment-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-assignment-explicit-assignment-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyAssignmentSwapSequencingRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-assignment-swap-sequencing.cpp");
  writeFile(Source, R"r19any(#include <any>
int effects;
std::any& select(std::any& value, int number) { effects = effects * 10 + number; return value; }
int main() {
  std::any first, second;
  select(first, 1).swap(select(second, 2));
  first.swap(first);
  return effects != 12 || first.has_value() || second.has_value();
}
)r19any");
  const auto Output = tmpFile("any-empty-assignment-swap-sequencing.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-assignment-swap-sequencing" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyAlteredResetRejected) {
  const auto Source = tmpFile("any-empty-altered-reset.cpp");
  writeFile(Source, R"r19any(#define reset changed_reset
#include <any>
#undef reset
int main() {
  std::any a;
  a.changed_reset();
  return a.has_value();
}
)r19any");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyEmptyPointerCastUserRedeclarationRejected) {
  const auto Source = tmpFile("any-empty-pointer-cast-user-redeclaration.cpp");
  writeFile(Source, R"r19any(#include <any>
_LIBCPP_BEGIN_NAMESPACE_STD
template <class T> add_pointer_t<T> any_cast(any*) noexcept;
_LIBCPP_END_NAMESPACE_STD
int f() {
  std::any value;
  return std::any_cast<int>(&value) != nullptr;
}
)r19any");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2MonostateComparisonAllSixRunAtBothOptimizations) {
  const auto Source = tmpFile("monostate-comparison-all-six.cpp");
  writeFile(Source, R"r19monocmp(#include <variant>
int main() {
  std::monostate a{};
  std::monostate b;
  return !(a == b) || a != b || a < b || a > b || !(a <= b) || !(a >= b);
}
)r19monocmp");
  const auto Output = tmpFile("monostate-comparison-all-six.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("monostate-comparison-all-six" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2MonostateComparisonExplicitFunctionRunAtBothOptimizations) {
  const auto Source = tmpFile("monostate-comparison-explicit-function.cpp");
  writeFile(Source, R"r19monocmp(#include <variant>
int main() {
  std::variant<int, std::monostate> value(std::in_place_index<1>);
  const std::monostate* mono = std::get_if<1>(&value);
  if (!mono)
    return 1;
  std::monostate second(*mono);
  return !std::operator==(*mono, second) || std::operator<(*mono, second);
}
)r19monocmp");
  const auto Output = tmpFile("monostate-comparison-explicit-function.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("monostate-comparison-explicit-function" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantEmplaceTypeAndRepeatedValueRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-emplace-type-and-repeated-value.cpp");
  writeFile(Source, R"r19emplace(#include <variant>
int main() {
  std::variant<int, long> value;
  const long source = 37;
  value.emplace<long>(source) += 5;
  auto* item = std::get_if<long>(&value);
  if (!item || *item != 42)
    return 1;
  long& slot = value.emplace<long>(9L);
  return value.index() != 1 || std::get_if<long>(&value) != &slot || slot != 9L;
}
)r19emplace");
  const auto Output = tmpFile("variant-emplace-type-and-repeated-value.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-emplace-type-and-repeated-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantEmplaceEmptyAlternativeRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-emplace-empty-alternative.cpp");
  writeFile(Source, R"r19emplace(#include <variant>
int main() {
  std::variant<int, std::monostate> value(13);
  std::monostate& slot = value.emplace<std::monostate>();
  return value.valueless_by_exception() || value.index() != 1 ||
         std::get_if<std::monostate>(&value) != &slot || std::get_if<int>(&value) != nullptr;
}
)r19emplace");
  const auto Output = tmpFile("variant-emplace-empty-alternative.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-emplace-empty-alternative" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantAggregateEmplaceAcceptsOriginalFunctionBody) {
  const auto Source = tmpFile("variant-emplace-source-owned-record.cpp");
  writeFile(Source, R"r19emplace(#include <variant>
struct Payload {
  int data;
};
int f() {
  std::variant<int, Payload> value;
  return value.emplace<1>().data;
}
)r19emplace");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
}

TEST_F(TranslateTest, CoreV2MonostateComparisonSdkAddressRejected) {
  const auto Source = tmpFile("monostate-comparison-sdk-address.cpp");
  writeFile(Source, R"r19monocmp(#include <variant>
int f() {
  using Compare = bool (*)(std::monostate, std::monostate) noexcept;
  auto call = static_cast<Compare>(&std::operator==);
  return call({}, {});
}
)r19monocmp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AssociativeMultimapIdentityRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-multimap-identity.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <type_traits>
using Container = std::multimap<int, int>;
int main() { return !std::is_same_v<Container, std::multimap<int, int>>; }
)r19containers");
  const auto Output = tmpFile("associative-multimap-identity.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-multimap-identity" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeMultimapSizeofRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-multimap-sizeof.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <type_traits>
using Container = std::multimap<int, int>;
int main() { return sizeof(Container) != 3 * sizeof(void*); }
)r19containers");
  const auto Output = tmpFile("associative-multimap-sizeof.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-multimap-sizeof" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeMultisetIdentityRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-multiset-identity.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <type_traits>
using Container = std::multiset<int>;
int main() { return !std::is_same_v<Container, std::multiset<int>>; }
)r19containers");
  const auto Output = tmpFile("associative-multiset-identity.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-multiset-identity" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeMultisetSizeofRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-multiset-sizeof.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <type_traits>
using Container = std::multiset<int>;
int main() { return sizeof(Container) != 3 * sizeof(void*); }
)r19containers");
  const auto Output = tmpFile("associative-multiset-sizeof.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-multiset-sizeof" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeUnorderedMultisetIdentityRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-unordered_multiset-identity.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
#include <type_traits>
using Container = std::unordered_multiset<int>;
int main() { return !std::is_same_v<Container, std::unordered_multiset<int>>; }
)r19containers");
  const auto Output = tmpFile("associative-unordered_multiset-identity.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-unordered_multiset-identity" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeUnorderedMultisetSizeofRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-unordered_multiset-sizeof.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
#include <type_traits>
using Container = std::unordered_multiset<int>;
int main() { return sizeof(Container) != 5 * sizeof(void*); }
)r19containers");
  const auto Output = tmpFile("associative-unordered_multiset-sizeof.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-unordered_multiset-sizeof" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeAdapterConstructionStackCopyDequeRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-adapter-construction-stack-copy-deque.cpp");
  writeFile(Source, R"r19containers(#include <stack>
#include <deque>
int main() {
  std::deque<int> original{2, 7, 11};
  std::stack<int> result(original);
  if (&result.top() == &original.back())
    return 1;
  original.back() = 13;
  if (result.size() != 3 || result.top() != 11 || original.back() != 13)
    return 2;
  result.pop();
  return result.top() != 7 || original.size() != 3;
}
)r19containers");
  const auto Output = tmpFile("deque-adapter-construction-stack-copy-deque.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-adapter-construction-stack-copy-deque" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeAdapterConstructionQueueMoveDequeReferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-adapter-construction-queue-move-deque-reference.cpp");
  writeFile(Source, R"r19containers(#include <queue>
#include <deque>
#include <utility>
int main() {
  std::deque<int> original{2, 7, 11};
  int* first = &original.front();
  std::queue<int> result(std::move(original));
  original.push_back(17);
  *first = 3;
  return &result.front() != first || result.front() != 3 || result.back() != 11 ||
         original.size() != 1 || original.back() != 17;
}
)r19containers");
  const auto Output = tmpFile("deque-adapter-construction-queue-move-deque-reference.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-adapter-construction-queue-move-deque-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapStoresFloatMappedValues) {
  const auto Source = tmpFile("associative-multimap-custom-type-or-policy.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <type_traits>
using Container = std::multimap<int, double>;
int main() { return sizeof(Container) == 0; }
)r19containers");
  const auto Output = tmpFile("associative-multimap-custom-type-or-policy.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-multimap-custom-type-or-policy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeMultimapRenamedContainerStorageRejected) {
  const auto Source = tmpFile("associative-multimap-renamed-container-storage.cpp");
  writeFile(Source, R"r19containers(#define __tree_ altered_owner_storage
#include <map>
#include <type_traits>
#undef __tree_
using Container = std::multimap<int, int>;
int main() { return sizeof(Container) == 0; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AssociativeMultisetRenamedContainerStorageRejected) {
  const auto Source = tmpFile("associative-multiset-renamed-container-storage.cpp");
  writeFile(Source, R"r19containers(#define __tree_ altered_owner_storage
#include <set>
#include <type_traits>
#undef __tree_
using Container = std::multiset<int>;
int main() { return sizeof(Container) == 0; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AssociativeUnorderedMultisetCustomTypeOrPolicyRejected) {
  const auto Source = tmpFile("associative-unordered_multiset-custom-type-or-policy.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
#include <type_traits>
struct CustomHash { unsigned long long operator()(int key) const { return static_cast<unsigned int>(key); } };
using Container = std::unordered_multiset<int, CustomHash>;
int main() { return sizeof(Container) == 0; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AssociativeUnorderedMultisetRuntimeStorageGuardRejected) {
  const auto Source = tmpFile("associative-unordered_multiset-runtime-storage-guard.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
#include <type_traits>
using Container = std::unordered_multiset<int>;
int main() { Container value; return sizeof(value) == 0; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeAdapterConstructionNonIntDequeRejected) {
  const auto Source = tmpFile("deque-adapter-construction-non-int-deque.cpp");
  writeFile(Source, R"r19containers(#include <queue>
#include <deque>
int f() {
  const std::deque<long> input{7L};
  std::queue<long> out(input);
  return static_cast<int>(out.front());
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantValueAssignmentSameAlternativeReferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-value-assignment-same-alternative-reference.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int main() {
  std::variant<int, long> value(25L);
  long* before = std::get_if<long>(&value);
  const long value_to_assign = 42L;
  auto& result = (value = value_to_assign);
  long* after = std::get_if<long>(&result);
  if (!before || before != after || *after != 42L || &result != &value)
    return 1;
  value = *after;
  return *before != 42L;
}
)r19containers");
  const auto Output = tmpFile("variant-value-assignment-same-alternative-reference.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-value-assignment-same-alternative-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantValueAssignmentExplicitMemberBindingOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-value-assignment-explicit-member-binding-order.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<int, long>;
int order;
Value* target(Value& value) { order = order * 10 + 1; return &value; }
long input() { order = order * 10 + 2; return 49L; }
int main() {
  Value value(4);
  auto& result = target(value)->operator=(input());
  auto* item = std::get_if<long>(&result);
  return order != 12 || &result != &value || !item || *item != 49L;
}
)r19containers");
  const auto Output = tmpFile("variant-value-assignment-explicit-member-binding-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-value-assignment-explicit-member-binding-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantZeroVisitFunctionRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-zero-visit-function.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int visitor() { return 11; }
int main() { return std::visit(visitor) != 11; }
)r19containers");
  const auto Output = tmpFile("variant-zero-visit-function.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-zero-visit-function" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantZeroVisitFactoryEvaluationRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-zero-visit-factory-evaluation.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Callback = int (*)();
int visitor() { return 19; }
Callback choose(int& count) { ++count; return visitor; }
int main() {
  int count = 0;
  const int result = std::visit(choose(count));
  return result != 19 || count != 1;
}
)r19containers");
  const auto Output = tmpFile("variant-zero-visit-factory-evaluation.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-zero-visit-factory-evaluation" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantValueAssignmentVolatileOperandRejected) {
  const auto Source = tmpFile("variant-value-assignment-volatile-operand.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int f() {
  std::variant<int, long> value;
  volatile long operand = 17;
  value = operand;
  return value.index();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantValueAssignmentNontrivialAlternativeRejected) {
  const auto Source = tmpFile("variant-value-assignment-nontrivial-alternative.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Payload {
  int data;
  Payload(int value) : data(value) {}
  ~Payload() { data = 0; }
};
int f() {
  std::variant<int, Payload> value;
  value = Payload(17);
  return value.index();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantZeroVisitRenamedTableProjectionRejected) {
  const auto Source = tmpFile("variant-zero-visit-renamed-table-projection.cpp");
  writeFile(Source, R"r19containers(#define __at changed_visitor_projection
#include <variant>
#undef __at
int visitor() { return 41; }
int main() { return std::visit(visitor) != 41; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantIntegralVisitConstAndIntegralConversionRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-integral-visit-const-and-integral-conversion.cpp");
  writeFile(Source, R"r19containers(#include <variant>
long widen(long value) { return value * 2L; }
int main() {
  const std::variant<int, short> first(std::in_place_index<0>, -19);
  const std::variant<int, short> second(std::in_place_index<1>, short(23));
  return std::visit(widen, first) != -38L || std::visit(&widen, second) != 46L;
}
)r19containers");
  const auto Output = tmpFile("variant-integral-visit-const-and-integral-conversion.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-integral-visit-const-and-integral-conversion" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantIntegralVisitVoidResultRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-integral-visit-void-result.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int total;
void read(int value) { total += value; }
int main() {
  std::variant<short, long> value(std::in_place_index<0>, short(4));
  std::visit(read, value);
  value.emplace<1>(34L);
  std::visit(&read, value);
  return total != 38;
}
)r19containers");
  const auto Output = tmpFile("variant-integral-visit-void-result.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-integral-visit-void-result" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantIntegralVisitRenamedGuardRejected) {
  const auto Source = tmpFile("variant-integral-visit-renamed-guard.cpp");
  writeFile(Source, R"r19containers(#define __throw_if_valueless changed_guard
#include <variant>
#undef __throw_if_valueless
int cb(int v) { return v; }
int main() {
  std::variant<short, int> value(std::in_place_index<1>, 27);
  return std::visit(cb, value) != 27;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyIntRuntimeReferenceAndMismatchRunAtBothOptimizations) {
  const auto Source = tmpFile("any-int-runtime-reference-and-mismatch.cpp");
  writeFile(Source, R"r19containers(#include <any>
int next(int& calls) { ++calls; return 19; }
int main() {
  int calls = 0;
  std::any value(next(calls));
  auto* p = std::any_cast<int>(&value);
  if (!p || *p != 19 || std::any_cast<long>(&value) || calls != 1)
    return 1;
  *p = 31;
  const std::any& readonly = value;
  const int* q = std::any_cast<int>(&readonly);
  return q != p || *q != 31 || !value.has_value();
}
)r19containers");
  const auto Output = tmpFile("any-int-runtime-reference-and-mismatch.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-int-runtime-reference-and-mismatch" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyIntCopyAndMoveConstructionRunAtBothOptimizations) {
  const auto Source = tmpFile("any-int-copy-and-move-construction.cpp");
  writeFile(Source, R"r19containers(#include <any>
#include <utility>
int main() {
  std::any first(27);
  std::any copy(first);
  std::any moved(std::move(first));
  if (first.has_value() || std::any_cast<int>(&first))
    return 1;
  auto* left = std::any_cast<int>(&copy);
  auto* right = std::any_cast<int>(&moved);
  if (!left || !right || left == right || *left != 27 || *right != 27)
    return 2;
  moved.reset();
  return moved.has_value() || std::any_cast<int>(&moved) || *left != 27;
}
)r19containers");
  const auto Output = tmpFile("any-int-copy-and-move-construction.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-int-copy-and-move-construction" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyDoubleConstructionAcceptsFunctionBody) {
  const auto Source = tmpFile("any-int-unsupported-floating-value.cpp");
  writeFile(Source, R"r19containers(#include <any>
int f() {
  std::any value(1.0);
  return value.has_value();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantTwoVisitMixedActiveIndicesRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-two-visit-mixed-active-indices.cpp");
  writeFile(Source, R"r19containers(#include <variant>
long combine(int a, long b) { return a * 100 + b; }
int main() {
  std::variant<short, int> left(std::in_place_index<0>, short(7));
  std::variant<long, int, short> right(std::in_place_index<2>, short(15));
  if (std::visit(combine, left, right) != 715)
    return 1;
  left.emplace<1>(25);
  right.emplace<0>(51L);
  if (std::visit(combine, left, right) != 2551)
    return 2;
  right.emplace<1>(-3);
  return std::visit(&combine, left, right) != 2497;
}
)r19containers");
  const auto Output = tmpFile("variant-two-visit-mixed-active-indices.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-two-visit-mixed-active-indices" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantTwoVisitAliasAndEffectsWithVoidResultRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-two-visit-alias-and-effects-with-void-result.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<short, int>;
using Visitor = void (*)(int,int);
int left_calls, right_calls, visitor_calls, invoked, seen;
void two(int a, int b) { ++invoked; seen = a * 10 + b; }
Visitor visitor() { ++visitor_calls; return &two; }
Value& left(Value& v) { ++left_calls; return v; }
Value& right(Value& v) { ++right_calls; return v; }
int main() {
  Value v(std::in_place_index<1>, 4);
  std::visit(visitor(), left(v), right(v));
  return seen != 44 || left_calls != 1 || right_calls != 1 || visitor_calls != 1 || invoked != 1;
}
)r19containers");
  const auto Output = tmpFile("variant-two-visit-alias-and-effects-with-void-result.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-two-visit-alias-and-effects-with-void-result" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantTwoVisitRenamedMatrixIndexRejected) {
  const auto Source = tmpFile("variant-two-visit-renamed-matrix-index.cpp");
  writeFile(Source, R"r19containers(#define __at changed_matrix_index
#include <variant>
#undef __at
int sub(int a, int b) { return a - b; }
int f(std::variant<int, short>& left, std::variant<int, short>& right) {
  return std::visit(sub, left, right);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2TupleBudgetDeep15ExecutionRunAtBothOptimizations) {
  const auto Source = tmpFile("tuple-budget-deep-15-execution.cpp");
  writeFile(Source, R"r19containers(#include <array>
#include <tuple>
#include <utility>
using T0 = int;
using T1 = std::tuple<T0>;
using T2 = std::tuple<T1>;
using T3 = std::tuple<T2>;
using T4 = std::tuple<T3>;
using T5 = std::tuple<T4>;
using T6 = std::tuple<T5>;
using T7 = std::tuple<T6>;
using T8 = std::tuple<T7>;
using T9 = std::tuple<T8>;
using T10 = std::tuple<T9>;
using T11 = std::tuple<T10>;
using T12 = std::tuple<T11>;
using T13 = std::tuple<T12>;
using T14 = std::tuple<T13>;
using T15 = std::tuple<T14>;
using Carrier = std::tuple<T1, T15>;
int main() {
  Carrier left{}, right{};
  std::get<0>(std::get<0>(left)) = 7;
  std::get<0>(std::get<0>(right)) = 19;
  std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<1>(left)))))))))))))))) = -23;
  std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<1>(right)))))))))))))))) = 31;
  left.swap(right);
  if (std::get<0>(std::get<0>(left)) != 19 ||
      std::get<0>(std::get<0>(right)) != 7)
    return 1;
  if (std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<1>(left)))))))))))))))) != 31 || std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<1>(right)))))))))))))))) != -23)
    return 2;
  return 0;
}
)r19containers");
  const auto Output = tmpFile("tuple-budget-deep-15-execution.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("tuple-budget-deep-15-execution" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyInPlaceValueInitRunAtBothOptimizations) {
  const auto Source = tmpFile("any-in-place-value-init.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  std::any a(std::in_place_type<int>);
  const auto *p = std::any_cast<int>(&a);
  return !a.has_value() || !p || *p != 0;
}
)r19containers");
  const auto Output = tmpFile("any-in-place-value-init.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-in-place-value-init" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmplaceIntegerReplacesAndReturnsReferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("any-emplace-integer-replaces-and-returns-reference.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  std::any a(12);
  int original = 21;
  int& item = a.emplace<int>(original);
  if (item != 21 || &item != std::any_cast<int>(&a))
    return 1;
  item = 32;
  if (*std::any_cast<int>(&a) != 32)
    return 2;
  int& zero = a.emplace<int>();
  return !a.has_value() || zero != 0 || &zero != std::any_cast<int>(&a);
}
)r19containers");
  const auto Output = tmpFile("any-emplace-integer-replaces-and-returns-reference.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-emplace-integer-replaces-and-returns-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantOwnedVisitConstBorrowRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-owned-visit-const-borrow.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Visitor {
  int weight;
  int operator()(int x) const & { return x * weight; }
};
int main() {
  const Visitor call{7};
  std::variant<short, int> input(std::in_place_index<0>, short(3));
  if (std::visit(call, input) != 21)
    return 1;
  input.emplace<1>(5);
  return std::visit(call, input) != 35;
}
)r19containers");
  const auto Output = tmpFile("variant-owned-visit-const-borrow.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-owned-visit-const-borrow" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantOwnedVisitVoidBorrowEvaluatedOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-owned-visit-void-borrow-evaluated-once.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Visitor {
  int observed;
  void operator()(int a, int b) & { observed = a * 10 + b; }
};
int selections;
Visitor& choose(Visitor& call) { ++selections; return call; }
int main() {
  Visitor call{0};
  std::variant<short, int> input(std::in_place_index<1>, 3);
  std::visit(choose(call), input, input);
  return selections != 1 || call.observed != 33;
}
)r19containers");
  const auto Output = tmpFile("variant-owned-visit-void-borrow-evaluated-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-owned-visit-void-borrow-evaluated-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmplaceLongAcceptsFunctionBody) {
  const auto Source = tmpFile("any-emplace-other-payload-guard.cpp");
  writeFile(Source, R"r19containers(#include <any>
void f(std::any& a) { a.emplace<long>(3L); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantOwnedVisitRenamedInvokeRejected) {
  const auto Source = tmpFile("variant-owned-visit-renamed-invoke.cpp");
  writeFile(Source, R"r19containers(#define __invoke renamed_invoke
#include <variant>
#undef __invoke
struct Visitor { int operator()(int a, int b) & { return a - b; } };
int check(Visitor& call, std::variant<short, int>& left, std::variant<short, int>& right) {
  return std::visit(call, left, right);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantOwnedVisitRenamedIndexRejected) {
  const auto Source = tmpFile("variant-owned-visit-renamed-index.cpp");
  writeFile(Source, R"r19containers(#define __at renamed_index
#include <variant>
#undef __at
struct Visitor { int operator()(int a, int b) & { return a - b; } };
int check(Visitor& call, std::variant<short, int>& left, std::variant<short, int>& right) {
  return std::visit(call, left, right);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyIntAssignReadsReferenceAfterReceiverRunAtBothOptimizations) {
  const auto Source = tmpFile("any-int-assign-reads-reference-after-receiver.cpp");
  writeFile(Source, R"r19containers(#include <any>
int source = 3;
int sequence, read_sequence, receiver_sequence;
int& input() { read_sequence = ++sequence; return source; }
std::any& receiver(std::any& a) {
  receiver_sequence = ++sequence;
  source = 19;
  return a;
}
int main() {
  std::any a(65);
  receiver(a) = input();
  if (read_sequence != 1 || receiver_sequence != 2)
    return 1;
  return *std::any_cast<int>(&a) != 19;
}
)r19containers");
  const auto Output = tmpFile("any-int-assign-reads-reference-after-receiver.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-int-assign-reads-reference-after-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyIntMakeAtReturnBoundaryRunAtBothOptimizations) {
  const auto Source = tmpFile("any-int-make-at-return-boundary.cpp");
  writeFile(Source, R"r19containers(#include <any>
int calls;
int next() { ++calls; return 47; }
std::any create() { return std::make_any<int>(next()); }
int main() {
  auto a = create();
  auto* value = std::any_cast<int>(&a);
  return calls != 1 || !value || *value != 47;
}
)r19containers");
  const auto Output = tmpFile("any-int-make-at-return-boundary.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-int-make-at-return-boundary" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyIntConstValueFromInPlaceConstructionRunAtBothOptimizations) {
  const auto Source = tmpFile("any-int-const-value-from-in-place-construction.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  int input = -43;
  const std::any a(std::in_place_type<int>, input);
  input = 11;
  return std::any_cast<int>(a) != -43;
}
)r19containers");
  const auto Output = tmpFile("any-int-const-value-from-in-place-construction.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-int-const-value-from-in-place-construction" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ScopedAllocatorSameTypeCopyRunAtBothOptimizations) {
  const auto Source = tmpFile("scoped-allocator-same-type-copy.cpp");
  writeFile(Source, R"r19containers(#include <memory>
#include <scoped_allocator>
using Alloc = std::scoped_allocator_adaptor<std::allocator<int>>;
int visits;
const Alloc& choose(const Alloc& allocation) { ++visits; return allocation; }
int main() {
  Alloc original;
  Alloc copy(choose(original));
  int* slot = copy.allocate(1);
  copy.construct(slot, 29);
  const int result = *slot;
  copy.destroy(slot);
  original.deallocate(slot, 1);
  return visits != 1 || result != 29;
}
)r19containers");
  const auto Output = tmpFile("scoped-allocator-same-type-copy.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("scoped-allocator-same-type-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ScopedAllocatorCopyAssignmentRunAtBothOptimizations) {
  const auto Source = tmpFile("scoped-allocator-copy-assignment.cpp");
  writeFile(Source, R"r19containers(#include <memory>
#include <scoped_allocator>
using Alloc = std::scoped_allocator_adaptor<std::allocator<int>>;
int visits;
const Alloc& choose(const Alloc& allocation) { ++visits; return allocation; }
int main() {
  Alloc first;
  Alloc second;
  Alloc& alias = (second = choose(first));
  return visits != 1 || &second != &alias;
}
)r19containers");
  const auto Output = tmpFile("scoped-allocator-copy-assignment.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("scoped-allocator-copy-assignment" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantThreeVisitConstOverloadedVisitorRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-three-visit-const-overloaded-visitor.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Encode {
  int offset;
  int operator()(int x, int y, short z) const & { return offset + x + 2*y + 3*z; }
  int operator()(int x, int y, int z) const & { return -offset + x + 2*y + 3*z; }
};
int main() {
  using Value = std::variant<short, int>;
  const Encode encode{50};
  Value a(std::in_place_index<1>, 2);
  const Value b(std::in_place_index<0>, short(3));
  Value c(std::in_place_index<0>, short(5));
  if (std::visit(encode, a, b, c) != 73)
    return 1;
  c.emplace<1>(7);
  return std::visit(encode, a, b, c) != -21;
}
)r19containers");
  const auto Output = tmpFile("variant-three-visit-const-overloaded-visitor.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-three-visit-const-overloaded-visitor" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyAssignLongAcceptsFunctionBody) {
  const auto Source = tmpFile("any-int-assign-other-payload-guard.cpp");
  writeFile(Source, R"r19containers(#include <any>
void f(std::any& a, long n) { a = n; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantVisitAcceptsOwnedTemporaryOperand) {
  const auto Source = tmpFile("variant-three-visit-temporary-operand.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<short, int>;
int combine(int a, int b, int c) { return a + b + c; }
int f(Value& a, Value& b) { return std::visit(combine, a, b, Value(2)); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyIntCastWithoutActiveStateEvidenceRejected) {
  const auto Source = tmpFile("any-int-cast-without-active-state-evidence.cpp");
  writeFile(Source, R"r19containers(#include <any>
int f(const std::any& a) { return std::any_cast<int>(a); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ScopedAllocatorInnerPackCopyGuardRejected) {
  const auto Source = tmpFile("scoped-allocator-inner-pack-copy-guard.cpp");
  writeFile(Source, R"r19containers(#include <memory>
#include <scoped_allocator>
using Alloc = std::scoped_allocator_adaptor<std::allocator<int>, std::allocator<int>>;
int check(const Alloc& source) {
  Alloc copy(source);
  return sizeof(copy);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantThreeVisitMatrixCapacityRejected) {
  const auto Source = tmpFile("variant-three-visit-matrix-capacity.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<signed char, short, int, long>;
using Wider = std::variant<signed char, short, int, long, bool>;
long combine(long a, long b, long c) { return a + b + c; }
long f(Value& a, Value& b, Wider& c) { return std::visit(combine, a, b, c); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantEqualityDuplicateTypesRetainIndexRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-equality-duplicate-types-retain-index.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int main() {
  std::variant<int, int> a(std::in_place_index<0>, 42);
  const std::variant<int, int> b(std::in_place_index<1>, 42);
  if (std::operator==(a, b) || !std::operator!=(a, b)) return 1;
  a.emplace<1>(42);
  return !std::operator==(a, b) || std::operator!=(a, b);
}
)r19containers");
  const auto Output = tmpFile("variant-equality-duplicate-types-retain-index.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-equality-duplicate-types-retain-index" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantEqualityAcceptsFloatingAlternative) {
  const auto Source = tmpFile("variant-equality-floating-alternative.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<int, double>;
bool equal(const Value& a, const Value& b) { return a == b; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantEqualityRenamedConversionPolicyRejected) {
  const auto Source = tmpFile("variant-equality-renamed-conversion-policy.cpp");
  writeFile(Source, R"r19containers(#define __convert_to_bool changed_comparison_policy
#include <variant>
#undef __convert_to_bool
using Value = std::variant<short, int>;
bool different(const Value& a, const Value& b) { return a != b; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantOrderingSignedValuesAndDuplicateTypesRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-ordering-signed-values-and-duplicate-types.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int main() {
  using Value = std::variant<int, int>;
  const Value lower_index(std::in_place_index<0>, 1);
  Value a(std::in_place_index<1>, -6);
  Value b(std::in_place_index<1>, 1);
  if (!(lower_index < a) || !(a < b) || !(b > a))
    return 1;
  b.emplace<1>(-9);
  return a <= b || !(a >= b);
}
)r19containers");
  const auto Output = tmpFile("variant-ordering-signed-values-and-duplicate-types.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-ordering-signed-values-and-duplicate-types" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantOrderingAcceptsFloatingAlternative) {
  const auto Source = tmpFile("variant-ordering-floating-alternative.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<int, double>;
bool compare(const Value& a, const Value& b) { return a >= b; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantOrderingRenamedPolicyRejected) {
  const auto Source = tmpFile("variant-ordering-renamed-policy.cpp");
  writeFile(Source, R"r19containers(#define less altered_variant_less
#include <variant>
#undef less
using Value = std::variant<short, int>;
bool compare(const Value& a, const Value& b) { return a < b; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantMonostateComparisonDistinctEmptyAlternativeIndexesRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-monostate-comparison-distinct-empty-alternative-indexes.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<std::monostate, int, std::monostate>;
int left_reads, right_reads;
const Value& left(const Value& v) { ++left_reads; return v; }
const Value& right(const Value& v) { ++right_reads; return v; }
int main() {
  const Value a, b(std::in_place_index<2>);
  if (!std::operator<(left(a), right(b))) return 1;
  if (std::operator==(left(a), right(b))) return 2;
  if (!std::operator>=(left(b), right(b))) return 3;
  return left_reads != 3 || right_reads != 3;
}
)r19containers");
  const auto Output = tmpFile("variant-monostate-comparison-distinct-empty-alternative-indexes.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-monostate-comparison-distinct-empty-alternative-indexes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMonostateComparisonRenamedSdkMonostateRejected) {
  const auto Source = tmpFile("variant-monostate-comparison-renamed-sdk-monostate.cpp");
  writeFile(Source, R"r19containers(#define monostate alternate_monostate
#include <variant>
#undef monostate
using Value = std::variant<std::alternate_monostate, int>;
bool compare(const Value& a, const Value& b) { return a == b; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantMonostateHashReceiverAndOperandOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-monostate-hash-receiver-and-operand-once.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Hash = std::hash<std::monostate>;
int receiver_calls, value_calls;
const Hash& get_hash(const Hash& h) { ++receiver_calls; return h; }
const std::monostate& get_value(const std::monostate& v) {
  ++value_calls;
  return v;
}
int main() {
  const Hash h{};
  const std::monostate value{};
  if (get_hash(h)(get_value(value)) != 66740831) return 1;
  return receiver_calls != 1 || value_calls != 1;
}
)r19containers");
  const auto Output = tmpFile("variant-monostate-hash-receiver-and-operand-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-monostate-hash-receiver-and-operand-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMonostateHashStandaloneMemberPointerRejected) {
  const auto Source = tmpFile("variant-monostate-hash-standalone-member-pointer.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int main() {
  auto op = &std::hash<std::monostate>::operator();
  return op == nullptr;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantVisitAlternativesMixedConstAndMutableBindingsRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-alternatives-mixed-const-and-mutable-bindings.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Empty = std::monostate;
using State = std::variant<Empty, int, Empty>;
using Count = std::variant<int, int>;
struct Visit {
  const Empty* expected;
  int calls;
  int operator()(const Empty& empty, int& count) & {
    ++calls;
    return count += &empty == expected ? 2 : 200;
  }
  int operator()(const int& value, int& count) & {
    ++calls;
    return count += value;
  }
};
int visitor_calls, state_calls, count_calls;
Visit& visitor(Visit& v) { ++visitor_calls; return v; }
const State& borrow(const State& v) { ++state_calls; return v; }
Count& borrow_count(Count& v) { ++count_calls; return v; }
int main() {
  const State left(std::in_place_index<2>);
  const State right(std::in_place_index<1>, 5);
  Count count(std::in_place_index<1>, 30);
  Visit use{&std::get<2>(left), 0};
  if (std::visit(visitor(use), borrow(left), borrow_count(count)) != 32 ||
      std::visit(use, right, count) != 37) return 1;
  const int* stored = std::get_if<1>(&count);
  return !stored || *stored != 37 || use.calls != 2 || visitor_calls != 1 ||
         state_calls != 1 || count_calls != 1;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-alternatives-mixed-const-and-mutable-bindings.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-alternatives-mixed-const-and-mutable-bindings" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitAlternativesExactPointerAndFloatingAlternativesRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-alternatives-exact-pointer-and-floating-alternatives.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Visit {
  int operator()(int* value) const & { return *value; }
  int operator()(const double& value) const & { return value == 1.5 ? 23 : 99; }
};
int main() {
  int x = 19;
  const Visit use{};
  const std::variant<int*, double> p(std::in_place_index<0>, &x);
  const std::variant<int*, double> d(std::in_place_index<1>, 1.5);
  return std::visit(use, p) != 19 || std::visit(use, d) != 23;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-alternatives-exact-pointer-and-floating-alternatives.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-alternatives-exact-pointer-and-floating-alternatives" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitAlternativesRenamedExhaustiveCheckRejected) {
  const auto Source = tmpFile("variant-visit-alternatives-renamed-exhaustive-check.cpp");
  writeFile(Source, R"r19containers(#define __std_visit_exhaustive_visitor_check changed_exhaustive_check
#include <variant>
#undef __std_visit_exhaustive_visitor_check
using State = std::variant<std::monostate, std::monostate>;
int read(const std::monostate&) { return 1; }
int f(const State& state) { return std::visit(read, state); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantVisitAlternativesVolatileReferenceRejected) {
  const auto Source = tmpFile("variant-visit-alternatives-volatile-reference.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int read(const volatile int& value) { return value; }
int f(std::variant<int>& state) { return std::visit(read, state); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedObserverQuerySetObserverSignaturesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-observer-query-set-observer-signatures.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <type_traits>
#include <utility>
using Container = std::set<int>;
int main() {
  using Size = decltype(std::declval<const Container&>().size());
  using Empty = decltype(std::declval<Container&>().empty());
  using Max = decltype(std::declval<const Container&>().max_size());
  return !std::is_same_v<Size, Container::size_type> ||
         !std::is_same_v<Max, Container::size_type> ||
         !std::is_same_v<Empty, bool> ||
         !noexcept(std::declval<const Container&>().size()) ||
         !noexcept(std::declval<Container&>().empty()) ||
         !noexcept(std::declval<const Container&>().max_size());
}
)r19containers");
  const auto Output = tmpFile("ordered-observer-query-set-observer-signatures.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-observer-query-set-observer-signatures" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedObserverQueryMultisetObserverSignaturesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-observer-query-multiset-observer-signatures.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <type_traits>
#include <utility>
using Container = std::multiset<int>;
int main() {
  using Size = decltype(std::declval<const Container&>().size());
  using Empty = decltype(std::declval<Container&>().empty());
  using Max = decltype(std::declval<const Container&>().max_size());
  return !std::is_same_v<Size, Container::size_type> ||
         !std::is_same_v<Max, Container::size_type> ||
         !std::is_same_v<Empty, bool> ||
         !noexcept(std::declval<const Container&>().size()) ||
         !noexcept(std::declval<Container&>().empty()) ||
         !noexcept(std::declval<const Container&>().max_size());
}
)r19containers");
  const auto Output = tmpFile("ordered-observer-query-multiset-observer-signatures.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-observer-query-multiset-observer-signatures" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedObserverQueryOverriddenKeyTypeRejected) {
  const auto Source = tmpFile("ordered-observer-query-overridden-key-type.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <utility>
using Other = std::map<long, int>;
int main() { return !noexcept(std::declval<const Other&>().size()); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2MonostateAdapterHashDirectInvokeRunAtBothOptimizations) {
  const auto Source = tmpFile("monostate-adapter-hash-direct-invoke.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <variant>
using M = std::monostate;
using H = std::hash<M>;
int hasher_calls, value_calls;
const H& get_hasher(const H& h) { ++hasher_calls; return h; }
const M& get_value(const M& m) { ++value_calls; return m; }
int main() {
  const H h{};
  const M m{};
  if (std::invoke(H{}, m) != 66740831) return 1;
  if (std::invoke(get_hasher(h), get_value(m)) != 66740831) return 2;
  return hasher_calls != 1 || value_calls != 1;
}
)r19containers");
  const auto Output = tmpFile("monostate-adapter-hash-direct-invoke.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("monostate-adapter-hash-direct-invoke" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2MonostateAdapterHashReferenceWrapperCallRunAtBothOptimizations) {
  const auto Source = tmpFile("monostate-adapter-hash-reference-wrapper-call.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <variant>
using M = std::monostate;
using H = std::hash<M>;
int calls;
const M& borrow(const M& m) { ++calls; return m; }
int main() {
  H h{};
  const M m{};
  auto ref = std::ref(h);
  auto cref = std::cref(h);
  return ref(borrow(m)) != 66740831 || cref(borrow(m)) != 66740831 ||
         calls != 2;
}
)r19containers");
  const auto Output = tmpFile("monostate-adapter-hash-reference-wrapper-call.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("monostate-adapter-hash-reference-wrapper-call" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2MonostateAdapterHashSourceConversionRejected) {
  const auto Source = tmpFile("monostate-adapter-hash-source-conversion.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <variant>
struct Source { operator std::monostate() const { return {}; } };
int main() { return std::invoke(std::hash<std::monostate>{}, Source{}) != 66740831; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2MonostateAdapterHashMemberAddressThroughInvokeRejected) {
  const auto Source = tmpFile("monostate-adapter-hash-member-address-through-invoke.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <variant>
using M = std::monostate;
using H = std::hash<M>;
int main() {
  const H h{};
  const M m{};
  auto pointer = &H::operator();
  return std::invoke(pointer, h, m) != 66740831;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantGuardedGetConstReferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-guarded-get-const-reference.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using V = std::variant<int, double>;
int read(const V& state) {
  if (std::holds_alternative<double>(state))
    return std::get<double>(state);
  return -1;
}
int main() {
  const V active{42.5};
  const V inactive{7};
  return read(active) != 42 || read(inactive) != -1;
}
)r19containers");
  const auto Output = tmpFile("variant-guarded-get-const-reference.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-guarded-get-const-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantGuardedGetRecheckAfterEmplaceRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-guarded-get-recheck-after-emplace.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using V = std::variant<std::monostate, long>;
int main() {
  V state;
  if (std::holds_alternative<std::monostate>(state)) {
    auto& empty = std::get<std::monostate>(state);
    if (&empty != std::get_if<0>(&state)) return 1;
  }
  state.emplace<1>(2L);
  if (std::holds_alternative<long>(state)) {
    long& item = std::get<1>(state);
    item *= 7;
    return item != 14 || std::get_if<0>(&state) != nullptr;
  }
  return 2;
}
)r19containers");
  const auto Output = tmpFile("variant-guarded-get-recheck-after-emplace.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-guarded-get-recheck-after-emplace" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantGuardedGetGuardForOtherAlternativeRejected) {
  const auto Source = tmpFile("variant-guarded-get-guard-for-other-alternative.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using V = std::variant<int, double>;
int read(V& state) {
  if (std::holds_alternative<double>(state)) return std::get<0>(state);
  return 0;
}
int main() { V state{2.5}; return read(state); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantGuardedGetFalsePathRejected) {
  const auto Source = tmpFile("variant-guarded-get-false-path.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using V = std::variant<int, double>;
bool read(V& state) {
  return std::holds_alternative<int>(state) || std::get<int>(state);
}
int main() { V state{2.5}; return read(state); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyUnsignedInlineInPlaceAndConstValueRunAtBothOptimizations) {
  const auto Source = tmpFile("any-unsigned-inline-in-place-and-const-value.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  const std::any value(std::in_place_type<unsigned>, 271u);
  auto const& payload = std::any_cast<const unsigned&>(value);
  if (&payload != std::any_cast<unsigned>(&value)) return 1;
  const std::any zero(std::in_place_type<unsigned>);
  return payload != 271u || std::any_cast<unsigned>(zero) != 0;
}
)r19containers");
  const auto Output = tmpFile("any-unsigned-inline-in-place-and-const-value.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-unsigned-inline-in-place-and-const-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyUnsignedInlineChangeIntegerKindRunAtBothOptimizations) {
  const auto Source = tmpFile("any-unsigned-inline-change-integer-kind.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  std::any value(-4);
  value = 14u;
  auto* active = std::any_cast<unsigned>(&value);
  if (!active || *active != 14u || std::any_cast<int>(&value)) return 1;
  int& integer = value.emplace<int>(6);
  if (&integer != std::any_cast<int>(&value) || std::any_cast<unsigned>(&value)) return 2;
  unsigned& natural = value.emplace<unsigned>();
  return &natural != std::any_cast<unsigned>(&value) || natural != 0u;
}
)r19containers");
  const auto Output = tmpFile("any-unsigned-inline-change-integer-kind.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-unsigned-inline-change-integer-kind" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyUnsignedInlineUnprovedSignedValueCastRejected) {
  const auto Source = tmpFile("any-unsigned-inline-unproved-signed-value-cast.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  const std::any value(5u);
  return std::any_cast<int>(value);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyUnsignedInlineRenamedUnsignedHandlerRejected) {
  const auto Source = tmpFile("any-unsigned-inline-renamed-unsigned-handler.cpp");
  writeFile(Source, R"r19containers(#define __create changed_unsigned_handler
#include <any>
#undef __create
int main() {
  std::any value(17u);
  return value.has_value();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantGetEarlyReturnReturnsOnDifferentIndexRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-get-early-return-returns-on-different-index.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using V = std::variant<int, int>;
int update(V& state) {
  if (state.index() != 1) { return -1; }
  int& value = std::get<1>(state);
  return ++value;
}
int main() {
  V wrong(std::in_place_index<0>, 4);
  V right(std::in_place_index<1>, 7);
  return update(wrong) != -1 || update(right) != 8 || *std::get_if<1>(&right) != 8;
}
)r19containers");
  const auto Output = tmpFile("variant-get-early-return-returns-on-different-index.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-get-early-return-returns-on-different-index" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantGetEarlyReturnScopeLocalAfterReturnRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-get-early-return-scope-local-after-return.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int main() {
  std::variant<std::monostate, int> state;
  if (!std::holds_alternative<std::monostate>(state)) return 1;
  auto& token = std::get<std::monostate>(state);
  return &token != std::get_if<0>(&state);
}
)r19containers");
  const auto Output = tmpFile("variant-get-early-return-scope-local-after-return.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-get-early-return-scope-local-after-return" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantGetEarlyReturnDestroyedIfInitializerRejected) {
  const auto Source = tmpFile("variant-get-early-return-destroyed-if-initializer.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using V = std::variant<int, double>;
struct Change {
  V& state;
  ~Change() { state.emplace<1>(3.5); }
};
int read(V& state) {
  if (Change change{state}; state.index() != 0) return -1;
  return std::get<0>(state);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantGetEarlyReturnReturnGuardOnOtherObjectRejected) {
  const auto Source = tmpFile("variant-get-early-return-return-guard-on-other-object.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using V = std::variant<int, double>;
int read(V& left, V& right) {
  if (!std::holds_alternative<int>(left)) return -1;
  return std::get<int>(right);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedEmptyOwnerConstMultimapRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-empty-owner-const-multimap.cpp");
  writeFile(Source, R"r19containers(#include <map>
unsigned long long empty_size(const std::multimap<int, int>& values) {
  return values.empty() ? values.size() : 1;
}
int main() {
  const std::multimap<int, int> values;
  return empty_size(values) != 0;
}
)r19containers");
  const auto Output = tmpFile("ordered-empty-owner-const-multimap.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-empty-owner-const-multimap" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedEmptyOwnerMultisetScopedOwnersRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-empty-owner-multiset-scoped-owners.cpp");
  writeFile(Source, R"r19containers(#include <set>
bool empty_at_exit(int flag) {
  std::multiset<int> outer;
  if (flag) {
    const std::multiset<int> inner;
    return inner.empty() && outer.size() == 0;
  }
  return outer.empty();
}
int main() {
  return !empty_at_exit(0) || !empty_at_exit(1);
}
)r19containers");
  const auto Output = tmpFile("ordered-empty-owner-multiset-scoped-owners.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-empty-owner-multiset-scoped-owners" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedEmptyOwnerMapCopyRunsAtBothOptimizations) {
  const auto Source = tmpFile("ordered-empty-owner-map-copy.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> values;
  std::map<int, int> copy(values);
  return !copy.empty();
}
)r19containers");
  const auto Output = tmpFile("ordered-empty-owner-map-copy.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-empty-owner-map-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedEmptyOwnerDifferentKeyRejected) {
  const auto Source = tmpFile("ordered-empty-owner-different-key.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<long long, int> values;
  return values.size() != 0;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMaxSizeConstMultimapRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-max-size-const-multimap.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  const std::map<int, int> unique;
  const std::multimap<int, int> repeated;
  return repeated.max_size() == 0 || repeated.max_size() != unique.max_size();
}
)r19containers");
  const auto Output = tmpFile("ordered-max-size-const-multimap.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-max-size-const-multimap" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMaxSizeMultisetRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-max-size-multiset.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  std::multiset<int> repeated;
  std::set<int> unique;
  return repeated.max_size() == 0 || repeated.max_size() != unique.max_size();
}
)r19containers");
  const auto Output = tmpFile("ordered-max-size-multiset.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-max-size-multiset" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMaxSizeDifferentElementRejected) {
  const auto Source = tmpFile("ordered-max-size-different-element.cpp");
  writeFile(Source, R"r19containers(#include <set>
using Values = std::set<short>;
auto bound(const Values& values) { return values.max_size(); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMaxSizeTemporaryRejected) {
  const auto Source = tmpFile("ordered-max-size-temporary.cpp");
  writeFile(Source, R"r19containers(#include <set>
auto bound() { return std::multiset<int>{}.max_size(); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyBoolInlineFactoryEmplaceResetRunAtBothOptimizations) {
  const auto Source = tmpFile("any-bool-inline-factory-emplace-reset.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  std::any value = std::make_any<bool>();
  auto* first = std::any_cast<bool>(&value);
  if (!first || *first) return 1;
  bool& stored = value.emplace<bool>(true);
  if (!stored || !value.has_value()) return 2;
  value.reset();
  return value.has_value() || std::any_cast<bool>(&value) != nullptr;
}
)r19containers");
  const auto Output = tmpFile("any-bool-inline-factory-emplace-reset.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-bool-inline-factory-emplace-reset" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyBoolInlineConstantActiveValueRunAtBothOptimizations) {
  const auto Source = tmpFile("any-bool-inline-constant-active-value.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  const std::any value{std::in_place_type<bool>, true};
  return std::any_cast<bool>(value) ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("any-bool-inline-constant-active-value.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-bool-inline-constant-active-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyBooleanMutableReferenceRunsAtBothOptimizations) {
  const auto Source = tmpFile("any-bool-inline-mutable-reference-cast.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  std::any value{true};
  bool& result = std::any_cast<bool&>(value);
  return result ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("any-bool-inline-mutable-reference-cast.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-bool-inline-mutable-reference-cast" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyBoolInlineDynamicValueTagRejected) {
  const auto Source = tmpFile("any-bool-inline-dynamic-value-tag.cpp");
  writeFile(Source, R"r19containers(#include <any>
bool read(const std::any& value) { return std::any_cast<bool>(value); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2EmptyHashSetConstMultisetRunAtBothOptimizations) {
  const auto Source = tmpFile("empty-hash-set-const-multiset.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::unordered_multiset<int> values;
  return !values.empty() || values.size() != 0 || values.bucket_count() != 0 || values.max_load_factor() != 1.0f;
}
)r19containers");
  const auto Output = tmpFile("empty-hash-set-const-multiset.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("empty-hash-set-const-multiset" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2EmptyHashSetScopedMultisetRunAtBothOptimizations) {
  const auto Source = tmpFile("empty-hash-set-scoped-multiset.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
bool probe(bool nested) {
  std::unordered_multiset<int> outer;
  if (nested) {
    const std::unordered_multiset<int> inner;
    return inner.empty() && outer.size() == 0;
  }
  return outer.empty();
}
int main() { return !probe(false) || !probe(true); }
)r19containers");
  const auto Output = tmpFile("empty-hash-set-scoped-multiset.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("empty-hash-set-scoped-multiset" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalScalarInsertRunAtBothOptimizations) {
  const auto Source = tmpFile("empty-hash-set-insert.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> values;
  values.insert(1);
  return values.size() != 1;
}
)r19containers");
  const auto Output = tmpFile("empty-hash-set-insert.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("empty-hash-set-insert" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2EmptyHashSetCustomHashRejected) {
  const auto Source = tmpFile("empty-hash-set-custom-hash.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct CustomHash { unsigned long long operator()(int value) const { return value; } };
int main() {
  std::unordered_set<int, CustomHash> values;
  return values.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyFloatInlineImmutableValueRunAtBothOptimizations) {
  const auto Source = tmpFile("any-float-inline-immutable-value.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  const std::any value(std::in_place_type<float>, 1.5f);
  float copied = std::any_cast<float>(value);
  const float& viewed = std::any_cast<const float&>(value);
  return copied != 1.5f || viewed != copied;
}
)r19containers");
  const auto Output = tmpFile("any-float-inline-immutable-value.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-float-inline-immutable-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyFloatInlineCopyMoveAndAssignRunAtBothOptimizations) {
  const auto Source = tmpFile("any-float-inline-copy-move-and-assign.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  std::any source{4.75f};
  std::any copy = source;
  std::any moved = static_cast<std::any&&>(copy);
  std::any target{false};
  target = moved;
  auto* a = std::any_cast<float>(&source);
  auto* b = std::any_cast<float>(&target);
  if (!a || !b || *a != 4.75f || *b != 4.75f) return 1;
  target = true;
  auto* flag = std::any_cast<bool>(&target);
  return !flag || !*flag || std::any_cast<float>(&target) != nullptr;
}
)r19containers");
  const auto Output = tmpFile("any-float-inline-copy-move-and-assign.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-float-inline-copy-move-and-assign" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyFloatInlineLongDoubleOwnerRejected) {
  const auto Source = tmpFile("any-float-inline-long-double-owner.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  auto value = std::make_any<long double>(1.0L);
  return !value.has_value();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyFloatInlineMismatchedImmutableValueRejected) {
  const auto Source = tmpFile("any-float-inline-mismatched-immutable-value.cpp");
  writeFile(Source, R"r19containers(#include <any>
float read() {
  const std::any value{1u};
  return std::any_cast<float>(value);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedEmptyLoadFactorEmptyMultisetRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-empty-load-factor-empty-multiset.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> value;
  float current = value.load_factor();
  return current != 0.0f || !value.empty() || value.size() != 0;
}
)r19containers");
  const auto Output = tmpFile("unordered-empty-load-factor-empty-multiset.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-empty-load-factor-empty-multiset" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedEmptyLoadFactorNestedScopeRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-empty-load-factor-nested-scope.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
float load(bool nested) {
  const std::unordered_multiset<int> outer;
  if (nested) {
    const std::unordered_multiset<int> inner;
    return inner.load_factor() + outer.load_factor();
  }
  return outer.load_factor();
}
int main() { return load(true) != 0.0f || load(false) != 0.0f; }
)r19containers");
  const auto Output = tmpFile("unordered-empty-load-factor-nested-scope.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-empty-load-factor-nested-scope" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalPointerLoadFactorRunsAtBothOptimizations) {
  const auto Source = tmpFile("unordered-empty-load-factor-pointer-receiver.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::unordered_multiset<int> value;
  const auto* pointer = &value;
  return pointer->load_factor() != 0.0f;
}
)r19containers");
  const auto Output = tmpFile("unordered-empty-load-factor-pointer-receiver.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-empty-load-factor-pointer-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedEmptyLoadFactorCustomHashRejected) {
  const auto Source = tmpFile("unordered-empty-load-factor-custom-hash.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct AlternateHash { unsigned long long operator()(int value) const { return value; } };
int main() {
  const std::unordered_set<int, AlternateHash> value;
  return value.load_factor() != 0.0f;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedEmptyCapacityMultisetMaxBucketsRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-empty-capacity-multiset-max-buckets.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  using Set = std::unordered_multiset<int>;
  Set values;
  Set::size_type maximum = values.max_bucket_count();
  return maximum != Set::size_type(-1) / 24 || values.bucket_count() != 0;
}
)r19containers");
  const auto Output = tmpFile("unordered-empty-capacity-multiset-max-buckets.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-empty-capacity-multiset-max-buckets" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedEmptyCapacityNestedMultisetRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-empty-capacity-nested-multiset.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
using Set = std::unordered_multiset<int>;
Set::size_type maximum(bool nested) {
  const Set outer;
  if (nested) {
    const Set inner;
    return inner.max_size() - outer.max_bucket_count();
  }
  return outer.max_size();
}
int main() { return maximum(true) != 0 || maximum(false) != Set::size_type(-1) / 24; }
)r19containers");
  const auto Output = tmpFile("unordered-empty-capacity-nested-multiset.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-empty-capacity-nested-multiset" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalPointerMaxBucketsRunsAtBothOptimizations) {
  const auto Source = tmpFile("unordered-empty-capacity-arrow-receiver.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::unordered_set<int> values;
  const auto* pointer = &values;
  return pointer->max_bucket_count() == 0;
}
)r19containers");
  const auto Output = tmpFile("unordered-empty-capacity-arrow-receiver.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-empty-capacity-arrow-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedEmptyCapacityNonIntKeyRejected) {
  const auto Source = tmpFile("unordered-empty-capacity-non-int-key.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::unordered_multiset<long> values;
  return values.max_bucket_count() == 0;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedLookupPoliciesMultisetEqualityValueRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-lookup-policies-multiset-equality-value.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> values;
  auto same = values.key_eq();
  return !same(-17, -17) || same(-17, 17) || values.key_eq()(8, 1);
}
)r19containers");
  const auto Output = tmpFile("unordered-lookup-policies-multiset-equality-value.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-lookup-policies-multiset-equality-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedLookupPoliciesEquivalentCopiesRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-lookup-policies-equivalent-copies.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::unordered_set<int> values;
  auto first = values.key_eq();
  auto second = first;
  return !second(1, 1) || first(1, 2) || !values.empty();
}
)r19containers");
  const auto Output = tmpFile("unordered-lookup-policies-equivalent-copies.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-lookup-policies-equivalent-copies" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalArrowObserverRunsAtBothOptimizations) {
  const auto Source = tmpFile("unordered-lookup-policies-arrow-receiver.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::unordered_set<int> values;
  const auto* pointer = &values;
  return !pointer->key_eq()(2, 2);
}
)r19containers");
  const auto Output = tmpFile("unordered-lookup-policies-arrow-receiver.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-lookup-policies-arrow-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedLookupPoliciesCustomEqualityRejected) {
  const auto Source = tmpFile("unordered-lookup-policies-custom-equality.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Equal { bool operator()(int first, int second) const { return first == second; } };
int main() {
  const std::unordered_multiset<int, std::hash<int>, Equal> values;
  return !values.key_eq()(1, 1);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedLoadLimitMultisetUpdateRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-load-limit-multiset-update.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> values;
  values.max_load_factor(2.0f);
  values.max_load_factor(0.5f);
  return values.max_load_factor() != 0.5f || values.bucket_count() != 0;
}
)r19containers");
  const auto Output = tmpFile("unordered-load-limit-multiset-update.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-load-limit-multiset-update" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedLoadLimitIndependentEmptyOwnersRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-load-limit-independent-empty-owners.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> first, second;
  first.max_load_factor(1.25f);
  const auto& view = first;
  return view.max_load_factor() != 1.25f || second.max_load_factor() != 1.0f;
}
)r19containers");
  const auto Output = tmpFile("unordered-load-limit-independent-empty-owners.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-load-limit-independent-empty-owners" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalPointerLoadSetterRunsAtBothOptimizations) {
  const auto Source = tmpFile("unordered-load-limit-arrow-receiver.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> values;
  auto* pointer = &values;
  pointer->max_load_factor(3.0f);
  return values.max_load_factor() != 3.0f;
}
)r19containers");
  const auto Output = tmpFile("unordered-load-limit-arrow-receiver.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-load-limit-arrow-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedLoadLimitNonIntKeyRejected) {
  const auto Source = tmpFile("unordered-load-limit-non-int-key.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<long> values;
  values.max_load_factor(3.0f);
  return values.max_load_factor() != 3.0f;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyWideInlineDoubleInPlaceDefaultRunAtBothOptimizations) {
  const auto Source = tmpFile("any-wide-inline-double-in-place-default.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  const std::any zero(std::in_place_type<double>);
  const double& borrowed = std::any_cast<const double&>(zero);
  return !zero.has_value() || borrowed != 0.0 || std::any_cast<double>(zero) != 0.0;
}
)r19containers");
  const auto Output = tmpFile("any-wide-inline-double-in-place-default.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-wide-inline-double-in-place-default" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyWideInlineWideUnsignedEmplaceRunAtBothOptimizations) {
  const auto Source = tmpFile("any-wide-inline-wide-unsigned-emplace.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  auto value = std::make_any<unsigned long long>(0xfe123456789abcdeULL);
  const auto* first = std::any_cast<unsigned long long>(&value);
  if (!first || *first != 0xfe123456789abcdeULL || std::any_cast<long long>(&value)) return 1;
  auto& second = value.emplace<unsigned long long>(0x8000000000000000ULL);
  if (second != 0x8000000000000000ULL || std::any_cast<unsigned long long>(&value) != &second) return 2;
  value.reset();
  return value.has_value() || std::any_cast<unsigned long long>(&value);
}
)r19containers");
  const auto Output = tmpFile("any-wide-inline-wide-unsigned-emplace.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-wide-inline-wide-unsigned-emplace" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyWideInlineWideInputOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("any-wide-inline-wide-input-once.cpp");
  writeFile(Source, R"r19containers(#include <any>
int steps;
unsigned long long input() {
  ++steps;
  return 0x0000000100000001ULL;
}
int main() {
  std::any value{input()};
  auto* saved = std::any_cast<unsigned long long>(&value);
  if (steps != 1 || !saved || *saved != 0x0000000100000001ULL) return 1;
  value = input();
  return steps != 2 || !std::any_cast<unsigned long long>(&value);
}
)r19containers");
  const auto Output = tmpFile("any-wide-inline-wide-input-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-wide-inline-wide-input-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyWideInlineUnknownWideValueRejected) {
  const auto Source = tmpFile("any-wide-inline-unknown-wide-value.cpp");
  writeFile(Source, R"r19containers(#include <any>
unsigned long long read(const std::any& value) {
  return std::any_cast<unsigned long long>(value);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyWideInlineRecordConversionRejected) {
  const auto Source = tmpFile("any-wide-inline-record-conversion.cpp");
  writeFile(Source, R"r19containers(#include <any>
struct Convertible {
  operator double() const noexcept { return 1.25; }
};
int main() {
  auto value = std::make_any<double>(Convertible{});
  return !value.has_value();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedComparatorMultisetConstValueRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-comparator-multiset-const-value.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  std::multiset<int> values;
  const auto& view = values;
  const auto cmp = view.value_comp();
  return !cmp(-1, 1) || cmp(4, 4);
}
)r19containers");
  const auto Output = tmpFile("ordered-comparator-multiset-const-value.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-comparator-multiset-const-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedComparatorMultimapKeyWrapperRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-comparator-multimap-key-wrapper.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  const std::multimap<int, int> values;
  auto cmp = values.key_comp();
  return !cmp(42, 100) || cmp(100, 42);
}
)r19containers");
  const auto Output = tmpFile("ordered-comparator-multimap-key-wrapper.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-comparator-multimap-key-wrapper" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedComparatorMapValueComparatorRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-comparator-map-value-comparator.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> values;
  auto cmp = values.value_comp();
  return !cmp({1, 4}, {2, 3});
}
)r19containers");
  const auto Output = tmpFile("ordered-comparator-map-value-comparator.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-comparator-map-value-comparator" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedComparatorNonDefaultKeyRejected) {
  const auto Source = tmpFile("ordered-comparator-non-default-key.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  std::multiset<long> values;
  return !values.value_comp()(0L, 1L);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedBucketStorageMultiReserveLimitRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-bucket-storage-multi-reserve-limit.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> values;
  values.max_load_factor(0.5f);
  values.reserve(5);
  if (values.bucket_count() != 11 || values.load_factor() != 0.0f) return 1;
  values.max_load_factor(2.0f);
  values.reserve(5);
    const auto& view = values;
    return view.bucket_count() != 3 || !view.empty() || view.bucket(13) != 1 || view.bucket_size(1) != 0;
}
)r19containers");
  const auto Output = tmpFile("unordered-bucket-storage-multi-reserve-limit.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-bucket-storage-multi-reserve-limit" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedBucketStorageBucketAllocationAndReleaseRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-bucket-storage-bucket-allocation-and-release.cpp");
  writeFile(Source, R"r19containers(using Size = decltype(sizeof(0));
extern "C" void* malloc(Size);
extern "C" void free(void*);
#include <unordered_set>
int armed, allocations, releases;
Size first_bytes, second_bytes;
void* operator new(Size bytes) {
  if (armed) {
    ++allocations;
    if (allocations == 1) first_bytes = bytes;
    else second_bytes = bytes;
  }
  return malloc(bytes);
}
void operator delete(void* pointer) noexcept {
  if (armed && pointer) ++releases;
  free(pointer);
}
void operator delete(void* pointer, Size) noexcept {
  if (armed && pointer) ++releases;
  free(pointer);
}
int main() {
  {
    std::unordered_multiset<int> values;
    armed = 1;
    values.rehash(4);
    values.rehash(7);
    values.rehash(7);
    if (allocations != 2 || releases != 1 ||
        first_bytes != 4 * sizeof(void*) || second_bytes != 7 * sizeof(void*)) return 1;
  }
  armed = 0;
  return releases != 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-bucket-storage-bucket-allocation-and-release.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-bucket-storage-bucket-allocation-and-release" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalPointerReserveRunsAtBothOptimizations) {
  const auto Source = tmpFile("unordered-bucket-storage-pointer-receiver.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_set<int> values; auto* ptr = &values; ptr->reserve(4); return values.size(); }
)r19containers");
  const auto Output = tmpFile("unordered-bucket-storage-pointer-receiver.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-bucket-storage-pointer-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedBucketStorageDifferentKeyRejected) {
  const auto Source = tmpFile("unordered-bucket-storage-different-key.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_multiset<long> values; values.reserve(4); return values.size(); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetValuesListInputOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-values-list-input-order.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int trace;
int mark(int n) { trace = 10 * trace + n; return -n; }
int main() {
  std::initializer_list<int> list{mark(2), mark(7), mark(1)};
  const std::unordered_set<int> values(list);
  return trace != 271 || values.count(-7) != 1 || values.count(-1) != 1 || values.size() != 3;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-values-list-input-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-values-list-input-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetValuesClearRetainsBucketCapacityRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-values-clear-retains-bucket-capacity.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> values{1, 4, 7, -3};
  const auto buckets = values.bucket_count();
  values.clear();
  if (values.bucket_count() != buckets || values.load_factor() != 0.0f) return 1;
  values.clear();
  return !values.empty() || values.size() != 0 || values.count(4) != 0;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-values-clear-retains-bucket-capacity.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-values-clear-retains-bucket-capacity" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalPointerSetCountRunsAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-values-arrow-count.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::unordered_set<int> values{3};
  auto* owner = &values;
  return owner->count(3) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-values-arrow-count.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-values-arrow-count" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetValuesCustomKeyEqualityRejected) {
  const auto Source = tmpFile("unordered-set-values-custom-key-equality.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Eq { bool operator()(int a, int b) const { return a == b; } };
int main() {
  std::unordered_set<int, std::hash<int>, Eq> values{1, 2};
  return values.count(1) != 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetModifiersReceiverBeforeModifierArgumentsRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-modifiers-receiver-before-modifier-arguments.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
using Set = std::unordered_set<int>;
int trace;
Set& receiver(Set& values) { trace = 10 * trace + 1; return values; }
int key(int digit) { trace = 10 * trace + digit; return -digit; }
int main() {
  Set values;
  receiver(values).insert({key(2), key(3), key(4)});
  if (trace != 1234 || values.size() != 3) return 1;
  trace = 0;
  if (receiver(values).erase(key(3)) != 1 || trace != 13) return 2;
  return values.count(-3) != 0 || values.count(-4) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-modifiers-receiver-before-modifier-arguments.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-modifiers-receiver-before-modifier-arguments" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetModifiersEraseReleasesOneNodeRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-modifiers-erase-releases-one-node.cpp");
  writeFile(Source, R"r19containers(using Size = decltype(sizeof(0));
extern "C" void* malloc(Size);
extern "C" void free(void*);
#include <unordered_set>
int armed, releases, allocations;
void* operator new(Size bytes) { if (armed) ++allocations; return malloc(bytes); }
void operator delete(void* pointer) noexcept { if (armed && pointer) ++releases; free(pointer); }
void operator delete(void* pointer, Size) noexcept { if (armed && pointer) ++releases; free(pointer); }
int main() {
  std::unordered_set<int> values{4, 8};
  armed = 1;
  values.insert({4, 8, 4});
  if (values.erase(9) || releases || allocations) return 1;
  if (values.erase(4) != 1 || values.erase(4) || releases != 1 || allocations) return 2;
  armed = 0;
  return values.size() != 1 || values.count(8) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-modifiers-erase-releases-one-node.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-modifiers-erase-releases-one-node" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalPointerSetListInsertAndEraseRunsAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-modifiers-pointer-receiver.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_set<int> values; auto* ptr = &values; ptr->insert({1, 2}); return ptr->erase(2); }
)r19containers");
  const auto Output = tmpFile("unordered-set-modifiers-pointer-receiver.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-modifiers-pointer-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 1) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetModifiersAlternateHashRejected) {
  const auto Source = tmpFile("unordered-set-modifiers-alternate-hash.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Hash { using Size = decltype(sizeof(0)); Size operator()(int value) const { return Size(value); } };
int main() { std::unordered_set<int, Hash> values; values.insert({1, 2}); return values.erase(2); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyNarrowInlineShortLifecycleRunAtBothOptimizations) {
  const auto Source = tmpFile("any-narrow-inline-short-lifecycle.cpp");
  writeFile(Source, R"r19containers(#include <any>
#include <utility>
int main() {
  auto original = std::make_any<short>(static_cast<short>(-32071));
  auto copy = original;
  std::any moved = std::move(copy);
  const auto& read = moved;
  auto* short_value = std::any_cast<short>(&read);
  if (copy.has_value() || !short_value || *short_value != -32071 || std::any_cast<int>(&read)) return 1;
  original.emplace<unsigned short>(static_cast<unsigned short>(65000));
  original.swap(moved);
  auto* wide = std::any_cast<unsigned short>(&moved);
  auto* narrow = std::any_cast<short>(&original);
  return !wide || *wide != 65000 || !narrow || *narrow != -32071 || std::any_cast<short>(&moved);
}
)r19containers");
  const auto Output = tmpFile("any-narrow-inline-short-lifecycle.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-narrow-inline-short-lifecycle" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyNarrowInlineUnsignedNarrowReferenceAndResetRunAtBothOptimizations) {
  const auto Source = tmpFile("any-narrow-inline-unsigned-narrow-reference-and-reset.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  std::any value;
  auto& word = value.emplace<unsigned short>(static_cast<unsigned short>(62000));
  word += 31;
  const auto* read = std::any_cast<unsigned short>(&value);
  if (!read || *read != 62031 || std::any_cast<unsigned int>(&value)) return 1;
  value.reset();
  return value.has_value() || std::any_cast<unsigned short>(&value);
}
)r19containers");
  const auto Output = tmpFile("any-narrow-inline-unsigned-narrow-reference-and-reset.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-narrow-inline-unsigned-narrow-reference-and-reset" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyNarrowMutableReferenceKeepsOriginalExitCode) {
  const auto Source = tmpFile("any-narrow-inline-mutable-narrow-value-cast.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() { std::any value = static_cast<short>(7); return std::any_cast<short&>(value); }
)r19containers");
  const auto Output = tmpFile("any-narrow-inline-mutable-narrow-value-cast.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-narrow-inline-mutable-narrow-value-cast" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 7) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyNarrowInlineNarrowTagMismatchValueCastRejected) {
  const auto Source = tmpFile("any-narrow-inline-narrow-tag-mismatch-value-cast.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() { std::any value = static_cast<unsigned char>(7); return std::any_cast<char>(value); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyLongInlineMakeAndCopyNativeLongRunAtBothOptimizations) {
  const auto Source = tmpFile("any-long-inline-make-and-copy-native-long.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  using Word = unsigned long;
  constexpr Word input = static_cast<Word>(0x90000000f0000007ULL);
  std::any original = std::make_any<Word>(input);
  std::any copy(original);
  original.reset();
  const std::any direct(input);
  if (original.has_value() || std::any_cast<Word>(direct) != input) return 1;
  const auto* saved = std::any_cast<Word>(&copy);
  return !saved || *saved != input;
}
)r19containers");
  const auto Output = tmpFile("any-long-inline-make-and-copy-native-long.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-long-inline-make-and-copy-native-long" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyLongInlineSwapNativeLongAndSignedWideRunAtBothOptimizations) {
  const auto Source = tmpFile("any-long-inline-swap-native-long-and-signed-wide.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  std::any left = 15L;
  std::any right = -19LL;
  left.swap(right);
  auto* native = std::any_cast<long>(&right);
  auto* wide = std::any_cast<long long>(&left);
  return !native || *native != 15L || !wide || *wide != -19LL ||
      std::any_cast<long>(&left) || std::any_cast<long long>(&right);
}
)r19containers");
  const auto Output = tmpFile("any-long-inline-swap-native-long-and-signed-wide.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-long-inline-swap-native-long-and-signed-wide" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyLongInlineSignedLongTagMismatchRejected) {
  const auto Source = tmpFile("any-long-inline-signed-long-tag-mismatch.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  std::any value = 3L;
  return std::any_cast<long long>(value) != 3LL;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyLongInlineVolatileNativeLongAccessRejected) {
  const auto Source = tmpFile("any-long-inline-volatile-native-long-access.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  std::any value = 7UL;
  return !std::any_cast<volatile unsigned long>(&value);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedMultisetValuesReservePreservesDuplicateKeysRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multiset-values-reserve-preserves-duplicate-keys.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> values{-3, -3, 10, 10, 10, 42};
  values.rehash(16);
  values.max_load_factor(0.5f);
  values.reserve(24);
  if (values.bucket_count() < 48 || values.count(-3) != 2 || values.count(10) != 3) return 1;
  const auto buckets = values.bucket_count();
  values.clear();
  return !values.empty() || values.count(10) != 0 || values.bucket_count() != buckets;
}
)r19containers");
  const auto Output = tmpFile("unordered-multiset-values-reserve-preserves-duplicate-keys.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multiset-values-reserve-preserves-duplicate-keys" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultisetValuesConstCountEvaluatesReceiverOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multiset-values-const-count-evaluates-receiver-once.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
using Bag = std::unordered_multiset<int>;
int calls;
const Bag& receiver(const Bag& values) { ++calls; return values; }
int main() {
  const Bag values{-2, 1, -2, 4, -2};
  if (receiver(values).count(-2) != 3 || calls != 1) return 1;
  return receiver(values).count(3) != 0 || calls != 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-multiset-values-const-count-evaluates-receiver-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multiset-values-const-count-evaluates-receiver-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalPointerMultisetCountRunsAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multiset-values-arrow-count.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_multiset<int> values{1, 1}; auto* p = &values; return p->count(1) != 2; }
)r19containers");
  const auto Output = tmpFile("unordered-multiset-values-arrow-count.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multiset-values-arrow-count" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultisetValuesAlternateHashRejected) {
  const auto Source = tmpFile("unordered-multiset-values-alternate-hash.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Hash { using Size = decltype(sizeof(0)); Size operator()(int key) const { return Size(key); } };
int main() { std::unordered_multiset<int, Hash> values{1, 1}; return values.count(1) != 2; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyCharacterInlineUtfTagTransitionRunAtBothOptimizations) {
  const auto Source = tmpFile("any-character-inline-utf-tag-transition.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  std::any value = u'\u03A9';
  const auto* short_code = std::any_cast<char16_t>(&value);
  if (!short_code || *short_code != u'\u03A9' || std::any_cast<char32_t>(&value)) return 1;
  value = U'\U0001F30F';
  const auto* wide_code = std::any_cast<char32_t>(&value);
  return !wide_code || *wide_code != U'\U0001F30F' || std::any_cast<char16_t>(&value) ||
      std::any_cast<unsigned int>(&value);
}
)r19containers");
  const auto Output = tmpFile("any-character-inline-utf-tag-transition.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-character-inline-utf-tag-transition" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyCharacterInlineClosedWideAndUtfValueCastsRunAtBothOptimizations) {
  const auto Source = tmpFile("any-character-inline-closed-wide-and-utf-value-casts.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  const std::any a = L'A';
  std::any b = U'B';
  return std::any_cast<wchar_t>(a) != L'A' || std::any_cast<char32_t>(b) != U'B';
}
)r19containers");
  const auto Output = tmpFile("any-character-inline-closed-wide-and-utf-value-casts.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-character-inline-closed-wide-and-utf-value-casts" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyCharacterInlineWrongUtfValueKindRejected) {
  const auto Source = tmpFile("any-character-inline-wrong-utf-value-kind.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() { const std::any value = U'A'; return std::any_cast<char16_t>(value) != u'A'; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyCharacterInlineVolatileUtfPointerRejected) {
  const auto Source = tmpFile("any-character-inline-volatile-utf-pointer.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() { std::any value = U'A'; return !std::any_cast<volatile char32_t>(&value); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedMultisetModifiersEraseRepairsCollidingBucketPredecessorsRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multiset-modifiers-erase-repairs-colliding-bucket-predecessors.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> values;
  values.rehash(8);
  values.insert({0, 8, 0, 1, 8, 16, 24, 8});
  const int key = 8;
  if (values.erase(key) != 3 || values.erase(key) != 0 || values.count(0) != 2 ||
      values.count(16) != 1 || values.count(24) != 1 || values.count(1) != 1 ||
      values.bucket_size(values.bucket(0)) != 4) return 1;
  if (values.erase(0) != 2 || values.erase(16) != 1 || values.erase(24) != 1 ||
      values.bucket_size(values.bucket(0)) != 0 || values.count(1) != 1) return 2;
  values.insert({24, 24});
  return values.size() != 3 || values.count(24) != 2 || values.count(1) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-multiset-modifiers-erase-repairs-colliding-bucket-predecessors.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multiset-modifiers-erase-repairs-colliding-bucket-predecessors" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultisetModifiersReceiverAndListSequencedOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multiset-modifiers-receiver-and-list-sequenced-once.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int trace = 0;
std::unordered_multiset<int>& receiver(std::unordered_multiset<int>& values) {
  trace = trace * 10 + 1;
  return values;
}
int argument(int event) { trace = trace * 10 + event; return 9; }
int main() {
  std::unordered_multiset<int> values;
  receiver(values).insert({argument(2), argument(3), argument(4)});
  if (trace != 1234 || values.count(9) != 3) return 1;
  trace = 0;
  const auto erased = receiver(values).erase(argument(5));
  return trace != 15 || erased != 3 || !values.empty();
}
)r19containers");
  const auto Output = tmpFile("unordered-multiset-modifiers-receiver-and-list-sequenced-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multiset-modifiers-receiver-and-list-sequenced-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalPointerMultisetListInsertAndEraseRunsAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multiset-modifiers-pointer-receiver.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_multiset<int> values; auto* p = &values; p->insert({1, 1}); return p->erase(1) != 2; }
)r19containers");
  const auto Output = tmpFile("unordered-multiset-modifiers-pointer-receiver.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multiset-modifiers-pointer-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultisetModifiersCustomAllowsDifferentKeyEqualityRejected) {
  const auto Source = tmpFile("unordered-multiset-modifiers-custom-allows-different-key-equality.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Hash { unsigned long operator()(int) const { return 0; } };
struct Equal { bool operator()(int a, int b) const { return (a % 2) == (b % 2); } };
int main() { std::unordered_multiset<int, Hash, Equal> values; values.insert({1, 3}); return values.erase(1) != 2; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetGlobalIteratorsConstFindAndMissingRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-global-iterators-const-find-and-missing.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::unordered_set<int> values{7, -5, 19};
  if (values.find(3) != values.cend()) return 1;
  auto it = values.find(-5);
  return it == values.end() || *it != -5;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-global-iterators-const-find-and-missing.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-global-iterators-const-find-and-missing" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetGlobalIteratorsEndpointsEvaluateReceiverOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-global-iterators-endpoints-evaluate-receiver-once.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
using Set = std::unordered_set<int>;
int trace;
const Set& receiver(const Set& values) { trace = trace * 10 + 1; return values; }
int key() { trace = trace * 10 + 2; return 6; }
int main() {
  const Set values{6};
  auto it = receiver(values).find(key());
  if (trace != 12 || it == values.cend() || *it != 6) return 1;
  trace = 0;
  return receiver(values).begin() == values.end() || trace != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-global-iterators-endpoints-evaluate-receiver-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-global-iterators-endpoints-evaluate-receiver-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalPointerFindRunsAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-global-iterators-find-pointer-receiver.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_set<int> values{5}; auto* p = &values; return *p->find(5) != 5; }
)r19containers");
  const auto Output = tmpFile("unordered-set-global-iterators-find-pointer-receiver.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-global-iterators-find-pointer-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetGlobalIteratorsCustomHashFindRejected) {
  const auto Source = tmpFile("unordered-set-global-iterators-custom-hash-find.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Hash { unsigned long operator()(int value) const { return static_cast<unsigned long>(value); } };
int main() { std::unordered_set<int, Hash> values{2}; return *values.find(2) != 2; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetScalarInsertionSetInsertionEvaluatesReceiverAndArgumentOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-scalar-insertion-set-insertion-evaluates-receiver-and-argument-once.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
using Set = std::unordered_set<int>;
int trace;
Set& receiver(Set& values) { trace = trace * 10 + 1; return values; }
int argument() { trace = trace * 10 + 2; return -7; }
int main() {
  Set values;
  if (!receiver(values).insert(argument()).second || trace != 12) return 1;
  return values.count(-7) != 1 || values.insert(-7).second;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-scalar-insertion-set-insertion-evaluates-receiver-and-argument-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-scalar-insertion-set-insertion-evaluates-receiver-and-argument-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetScalarInsertionMixedScalarAndListInsertRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-scalar-insertion-mixed-scalar-and-list-insert.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> values{3, 7, 3};
  values.insert({3, 7});
  auto added = values.insert(3);
  if (*added != 3 || values.count(3) != 4 || values.count(7) != 2) return 1;
  return values.erase(3) != 4 || values.count(7) != 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-scalar-insertion-mixed-scalar-and-list-insert.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-scalar-insertion-mixed-scalar-and-list-insert" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalPointerScalarInsertionRunsAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-scalar-insertion-pointer-receiver.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_set<int> values; auto* p = &values; return !p->insert(2).second; }
)r19containers");
  const auto Output = tmpFile("unordered-set-scalar-insertion-pointer-receiver.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-scalar-insertion-pointer-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetScalarInsertionDifferentKeyTypeRejected) {
  const auto Source = tmpFile("unordered-set-scalar-insertion-different-key-type.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_multiset<long> values; return *values.insert(2L) != 2; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetIteratorEraseMultisetErasesOnlyTheSelectedDuplicateRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-iterator-erase-multiset-erases-only-the-selected-duplicate.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> values{4, 4, 5, 4};
  auto position = values.find(4);
  auto successor = position;
  ++successor;
  if (values.erase(position) != successor || values.count(4) != 2) return 1;
  return values.size() != 3 || values.count(5) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-iterator-erase-multiset-erases-only-the-selected-duplicate.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-iterator-erase-multiset-erases-only-the-selected-duplicate" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetIteratorEraseEmptyAndEntireMultisetRangesRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-iterator-erase-empty-and-entire-multiset-ranges.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> values{1, -2, 1, -2};
  const auto end = values.end();
  if (values.erase(end, end) != end || values.size() != 4) return 1;
  if (values.erase(values.cbegin(), end) != end || !values.empty()) return 2;
  values.insert(7);
  return values.count(7) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-iterator-erase-empty-and-entire-multiset-ranges.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-iterator-erase-empty-and-entire-multiset-ranges" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetIteratorEraseDifferentKeyIteratorRejected) {
  const auto Source = tmpFile("unordered-set-iterator-erase-different-key-iterator.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_multiset<long> values{6L}; return values.erase(values.cbegin()) != values.end(); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetIteratorEraseCustomEqualityPositionRejected) {
  const auto Source = tmpFile("unordered-set-iterator-erase-custom-equality-position.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Equal { bool operator()(int a, int b) const { return a == b; } };
int main() {
  std::unordered_multiset<int, std::hash<int>, Equal> values{3, 3};
  const auto selected = values.cbegin();
  return values.erase(selected) == values.cend();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetEqualRangeAbsentKeyHasTwoEndpointsRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-equal-range-absent-key-has-two-endpoints.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::unordered_set<int> empty;
  const std::unordered_multiset<int> values{1, 1, 4};
  const auto none = empty.equal_range(1);
  auto missing = values.equal_range(2);
  return none.first != empty.cend() || none.second != empty.cend() ||
      missing.first != values.cend() || missing.second != values.cend();
}
)r19containers");
  const auto Output = tmpFile("unordered-set-equal-range-absent-key-has-two-endpoints.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-equal-range-absent-key-has-two-endpoints" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetEqualRangeReceiverAndKeyEvaluatedOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-equal-range-receiver-and-key-evaluated-once.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
using Bag = std::unordered_multiset<int>;
int trace;
const Bag& receiver(const Bag& values) { trace = trace * 10 + 1; return values; }
int key() { trace = trace * 10 + 2; return 6; }
int main() {
  const Bag values{6, 6};
  auto range = receiver(values).equal_range(key());
  if (trace != 12 || range.first == range.second) return 1;
  auto it = range.first;
  ++it;
  if (it == range.second || *it != 6) return 2;
  ++it;
  return it != range.second;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-equal-range-receiver-and-key-evaluated-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-equal-range-receiver-and-key-evaluated-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetEqualRangeDifferentKeyRejected) {
  const auto Source = tmpFile("unordered-set-equal-range-different-key.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_set<long> values{2}; return values.equal_range(2).first == values.end(); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetEqualRangeCustomEqualityRejected) {
  const auto Source = tmpFile("unordered-set-equal-range-custom-equality.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Equal { bool operator()(int a,int b) const { return a == b; } };
int main() { std::unordered_multiset<int, std::hash<int>, Equal> values{2, 2}; return values.equal_range(2).first == values.end(); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetLocalIteratorsConstBucketIterationSeesDuplicatesRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-local-iterators-const-bucket-iteration-sees-duplicates.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> values{4, 12, 4, 12, 4, 7};
  values.rehash(8);
  const auto& read = values;
  const auto bucket = read.bucket(4);
  int visited = 0, fours = 0;
  for (auto it = read.cbegin(bucket); it != read.cend(bucket);) {
    const int key = *it++;
    if (key != 4 && key != 12) return 1;
    fours += key == 4;
    ++visited;
  }
  return visited != 5 || fours != 3;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-local-iterators-const-bucket-iteration-sees-duplicates.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-local-iterators-const-bucket-iteration-sees-duplicates" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetLocalIteratorsReceiverAndBucketEvaluatedOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-local-iterators-receiver-and-bucket-evaluated-once.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
using Set = std::unordered_set<int>;
int trace;
const Set& receiver(const Set& values) { trace = trace * 10 + 1; return values; }
unsigned bucket() { trace = trace * 10 + 2; return 2; }
int main() {
  Set values{1};
  values.rehash(8);
  auto it = receiver(values).cbegin(bucket());
  return trace != 12 || it != values.cend(2);
}
)r19containers");
  const auto Output = tmpFile("unordered-set-local-iterators-receiver-and-bucket-evaluated-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-local-iterators-receiver-and-bucket-evaluated-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetLocalIteratorsDifferentKeyRejected) {
  const auto Source = tmpFile("unordered-set-local-iterators-different-key.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_set<long> values{3L}; return values.begin(0) != values.end(0); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetLocalIteratorsCustomEqualityRejected) {
  const auto Source = tmpFile("unordered-set-local-iterators-custom-equality.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Equal { bool operator()(int a, int b) const { return a == b; } };
int main() {
  std::unordered_multiset<int, std::hash<int>, Equal> values{1, 1};
  const auto bucket = values.bucket(1);
  return values.cbegin(bucket) == values.cend(bucket);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetEmplacementEquivalentHintBeforeExistingAfterGrowthRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-emplacement-equivalent-hint-before-existing-after-growth.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> values{3, 4};
  values.rehash(2);
  const auto hint = values.find(3);
  const int* existing = &*hint;
  auto added = values.emplace_hint(hint, 3);
  if (&*added == existing || values.count(3) != 2) return 1;
  auto successor = added;
  ++successor;
  if (successor == values.end() || &*successor != existing) return 2;
  const int key = 3;
  auto inserted = values.insert(successor, key);
  ++inserted;
  return inserted == values.end() || &*inserted != existing || values.count(3) != 3;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-emplacement-equivalent-hint-before-existing-after-growth.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-emplacement-equivalent-hint-before-existing-after-growth" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetEmplacementReceiverHintAndArgumentEvaluatedOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-emplacement-receiver-hint-and-argument-evaluated-once.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
using Set = std::unordered_set<int>;
int accesses, hints, keys;
Set& receiver(Set& values) { ++accesses; return values; }
Set::const_iterator hint(const Set& values) { hints += accesses; return values.cend(); }
int key() { keys += accesses; return 9; }
int main() {
  Set values{9};
  auto position = receiver(values).emplace_hint(hint(values), key());
  return accesses != 1 || hints != 1 || keys != 1 || *position != 9 || values.size() != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-emplacement-receiver-hint-and-argument-evaluated-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-emplacement-receiver-hint-and-argument-evaluated-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetEmplacementDifferentConstructorTypeRejected) {
  const auto Source = tmpFile("unordered-set-emplacement-different-constructor-type.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_set<int> values; short key=3; return *values.emplace(key).first != 3; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetEmplacementCustomHashRejected) {
  const auto Source = tmpFile("unordered-set-emplacement-custom-hash.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Hash { unsigned long operator()(int value) const { return static_cast<unsigned long>(value); } };
int main() { std::unordered_multiset<int, Hash> values; return *values.emplace_hint(values.cend(), 3) != 3; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetEqualitySwapMultisetEqualityChecksEachMultiplicityRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-equality-swap-multiset-equality-checks-each-multiplicity.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::unordered_multiset<int> a{1, 1, 9, 9, 9, 17};
  std::unordered_multiset<int> b{9, 1, 9, 17, 9, 1};
  b.rehash(8);
  if (a != b || !(a == b)) return 1;
  b.erase(b.find(9));
  b.insert(17);
  if (a == b || !(a != b) || a.size() != b.size()) return 2;
  b.erase(b.find(17));
  b.insert(9);
  return a != b;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-equality-swap-multiset-equality-checks-each-multiplicity.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-equality-swap-multiset-equality-checks-each-multiplicity" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetEqualitySwapQualifiedAndAdlMultisetSwapRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-equality-swap-qualified-and-adl-multiset-swap.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
using Bag = std::unordered_multiset<int>;
int seen;
Bag& pick(Bag& bag) { ++seen; return bag; }
int main() {
  Bag a{7, 7, 8}, b{2, 2};
  const int* saved = &*a.find(8);
  std::swap(pick(a), pick(b));
  if (seen != 2 || a.count(2) != 2 || b.count(7) != 2 || &*b.find(8) != saved) return 1;
  swap(a, b);
  return a.count(7) != 2 || &*a.find(8) != saved || b.count(2) != 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-equality-swap-qualified-and-adl-multiset-swap.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-equality-swap-qualified-and-adl-multiset-swap" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetEqualitySwapOtherKeyEqualityRejected) {
  const auto Source = tmpFile("unordered-set-equality-swap-other-key-equality.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_set<long> a{1L}, b{2L}; return a == b; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetEqualitySwapCustomPolicyFreeSwapRejected) {
  const auto Source = tmpFile("unordered-set-equality-swap-custom-policy-free-swap.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Hash { unsigned long operator()(int a) const { return static_cast<unsigned long>(a); } };
int main() { std::unordered_multiset<int, Hash> a{1, 1}, b{2}; std::swap(a, b); return 0; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetPointerRangeMultisetConstRangeRetainsCountsRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-pointer-range-multiset-const-range-retains-counts.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const int data[] = {4, 4, 12, 4, 12, -1};
  std::unordered_multiset<int> bag{4};
  bag.insert(data, data + 6);
  return bag.size() != 7 || bag.count(4) != 4 || bag.count(12) != 2 || bag.count(-1) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-pointer-range-multiset-const-range-retains-counts.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-pointer-range-multiset-const-range-retains-counts" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetPointerRangeReceiverAndBothEndpointsEvaluatedOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-pointer-range-receiver-and-both-endpoints-evaluated-once.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
using Bag = std::unordered_multiset<int>;
int accesses, firsts, lasts;
int data[] = {7, 7, 8, 8};
Bag& receiver(Bag& bag) { ++accesses; return bag; }
int* first() { firsts += accesses; return data + 1; }
int* last() { lasts += accesses; return data + 3; }
int main() {
  Bag bag;
  receiver(bag).insert(first(), last());
  return accesses != 1 || firsts != 1 || lasts != 1 || bag.size() != 2 ||
      bag.count(7) != 1 || bag.count(8) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-pointer-range-receiver-and-both-endpoints-evaluated-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-pointer-range-receiver-and-both-endpoints-evaluated-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetConvertsLongRangeValues) {
  const auto Source = tmpFile("unordered-set-pointer-range-converted-long-range.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { long data[] = {1L, 2L}; std::unordered_set<int> set; set.insert(data, data + 2); return set.empty(); }
)r19containers");
  const auto Output = tmpFile("unordered-set-pointer-range-converted-long-range.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-pointer-range-converted-long-range" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetPointerRangeCustomPolicyRangeRejected) {
  const auto Source = tmpFile("unordered-set-pointer-range-custom-policy-range.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Hash { unsigned long operator()(int key) const { return static_cast<unsigned long>(key); } };
int main() { int data[] = {3, 5}; std::unordered_set<int, Hash> set; set.insert(data, data + 2); return set.empty(); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetPointerConstructionMultisetConstructionRetainsDuplicatesRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-pointer-construction-multiset-construction-retains-duplicates.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const int data[] = {6, 6, 14, 6, -2};
  std::unordered_multiset<int> bag(data, data + 5);
  return bag.size() != 5 || bag.count(6) != 3 || bag.count(14) != 1 || bag.count(-2) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-pointer-construction-multiset-construction-retains-duplicates.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-pointer-construction-multiset-construction-retains-duplicates" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetPointerConstructionRangeArgumentsAreEvaluatedOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-pointer-construction-range-arguments-are-evaluated-once.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int first_calls, last_calls;
const int data[] = {9, -7, 9};
const int* first() { ++first_calls; return data; }
const int* last() { ++last_calls; return data + 3; }
int main() {
  std::unordered_multiset<int> bag(first(), last());
  return first_calls != 1 || last_calls != 1 || bag.size() != 3 || bag.count(9) != 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-pointer-construction-range-arguments-are-evaluated-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-pointer-construction-range-arguments-are-evaluated-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultisetConvertsShortConstructionRange) {
  const auto Source = tmpFile("unordered-set-pointer-construction-converted-short-range.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { short data[] = {1, 2}; std::unordered_multiset<int> bag(data, data + 2); return bag.empty(); }
)r19containers");
  const auto Output = tmpFile("unordered-set-pointer-construction-converted-short-range.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-pointer-construction-converted-short-range" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetPointerConstructionClassIteratorRangeRejected) {
  const auto Source = tmpFile("unordered-set-pointer-construction-class-iterator-range.cpp");
  writeFile(Source, R"r19containers(#include <list>
#include <unordered_set>
int main() { std::list<int> data{1, 2}; std::unordered_multiset<int> bag(data.begin(), data.end()); return bag.empty(); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetOwnerMoveEmptyMultisetConstructionKeepsReservationRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-owner-move-empty-multiset-construction-keeps-reservation.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
#include <utility>
int main() {
  std::unordered_multiset<int> source;
  source.rehash(23);
  source.max_load_factor(3.0f);
  const auto buckets = source.bucket_count();
  std::unordered_multiset<int> target(std::move(source));
  target.insert({2, 2, 5});
  return target.bucket_count() != buckets || target.count(2) != 2 ||
         target.max_load_factor() != 3.0f || !source.empty() || source.bucket_count() != 0;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-owner-move-empty-multiset-construction-keeps-reservation.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-owner-move-empty-multiset-construction-keeps-reservation" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOwnerMoveQualifiedAssignmentKeepsEmptySourceBucketsRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-owner-move-qualified-assignment-keeps-empty-source-buckets.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
#include <utility>
int main() {
  std::unordered_set<int> source;
  source.rehash(29);
  const auto buckets = source.bucket_count();
  std::unordered_set<int> target{4, 8, 12};
  auto* receiver = &target;
  auto& result = receiver->operator=(std::move(source));
  return &result != &target || !target.empty() || target.bucket_count() != buckets ||
         !source.empty() || source.bucket_count() != 0;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-owner-move-qualified-assignment-keeps-empty-source-buckets.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-owner-move-qualified-assignment-keeps-empty-source-buckets" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultisetOriginalAllocatorMoveRunsAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-owner-move-multiset-move-with-explicit-allocator.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
#include <utility>
int main() {
  std::unordered_multiset<int> a{8, 8};
  std::unordered_multiset<int> b(std::move(a), std::allocator<int>{});
  return b.size() != 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-owner-move-multiset-move-with-explicit-allocator.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-owner-move-multiset-move-with-explicit-allocator" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOwnerMoveCustomPolicyAssignmentRejected) {
  const auto Source = tmpFile("unordered-set-owner-move-custom-policy-assignment.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
#include <utility>
struct Hash { unsigned long operator()(int key) const { return static_cast<unsigned>(key); } };
int main() {
  std::unordered_set<int, Hash> a, b;
  a = std::move(b);
  return a.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetOwnerCopyMultisetCopyPreservesCountsAndIndependentEraseRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-owner-copy-multiset-copy-preserves-counts-and-independent-erase.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> source{8, 8, -3, 8, -3, 4};
  source.rehash(23);
  source.max_load_factor(2.0f);
  const std::unordered_multiset<int> copy(source);
  auto removed = source.erase(8);
  return removed != 3 || source.count(8) != 0 || copy.count(8) != 3 || copy.count(-3) != 2 ||
         copy.bucket_count() != 23 || copy.max_load_factor() != 2.0f;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-owner-copy-multiset-copy-preserves-counts-and-independent-erase.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-owner-copy-multiset-copy-preserves-counts-and-independent-erase" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOwnerCopyReferenceProducingSourceEvaluatedOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-owner-copy-reference-producing-source-evaluated-once.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
using Set = std::unordered_set<int>;
int accesses;
const Set& input(const Set& values) { ++accesses; return values; }
int main() {
  Set values{0, 6, 18};
  Set copy(input(values));
  values.clear();
  return accesses != 1 || !values.empty() || copy.size() != 3 || copy.count(6) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-owner-copy-reference-producing-source-evaluated-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-owner-copy-reference-producing-source-evaluated-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultisetOriginalAllocatorCopyRunsAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-owner-copy-explicit-allocator-multiset-copy.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> source{8, 8};
  std::unordered_multiset<int> copy(source, std::allocator<int>{});
  return copy.count(8) != 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-owner-copy-explicit-allocator-multiset-copy.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-owner-copy-explicit-allocator-multiset-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOwnerCopyCustomEqualityCopyRejected) {
  const auto Source = tmpFile("unordered-set-owner-copy-custom-equality-copy.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Equal { bool operator()(int left, int right) const { return left == right; } };
int main() {
  std::unordered_set<int, std::hash<int>, Equal> source;
  std::unordered_set<int, std::hash<int>, Equal> copy(source);
  return copy.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetCopyAssignmentMultisetCopyAssignmentGroupsCollidingDuplicatesRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-copy-assignment-multiset-copy-assignment-groups-colliding-duplicates.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> target{-1, -1};
  target.rehash(2);
  std::unordered_multiset<int> source{3, 5, 3, 7, 3, 5};
  source.max_load_factor(5.0f);
  target = source;
  auto range = target.equal_range(3);
  int groups = 0;
  for (auto it = range.first; it != range.second; ++it) {
    if (*it != 3) return 1;
    ++groups;
  }
  target.erase(5);
  return groups != 3 || target.count(5) != 0 || source.count(5) != 2 || target.count(-1) != 0 ||
         target.count(7) != 1 || target.max_load_factor() != 5.0f;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-copy-assignment-multiset-copy-assignment-groups-colliding-duplicates.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-copy-assignment-multiset-copy-assignment-groups-colliding-duplicates" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetCopyAssignmentSelfCopyPreservesAddressesAndEquivalentRangesRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-copy-assignment-self-copy-preserves-addresses-and-equivalent-ranges.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> values{8, -6, 8, 9};
  values.rehash(31);
  const auto buckets = values.bucket_count();
  const int* stored = &*values.find(-6);
  const auto& alias = values;
  values = alias;
  return values.size() != 4 || values.count(8) != 2 || values.bucket_count() != buckets ||
         &*values.find(-6) != stored;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-copy-assignment-self-copy-preserves-addresses-and-equivalent-ranges.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-copy-assignment-self-copy-preserves-addresses-and-equivalent-ranges" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetCopyAssignmentCustomHashCopyAssignmentRejected) {
  const auto Source = tmpFile("unordered-set-copy-assignment-custom-hash-copy-assignment.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Hash { unsigned long operator()(int value) const { return static_cast<unsigned>(value); } };
int main() {
  std::unordered_set<int, Hash> source, target;
  target = source;
  return target.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetCopyAssignmentLongKeyMultisetCopyAssignmentRejected) {
  const auto Source = tmpFile("unordered-set-copy-assignment-long-key-multiset-copy-assignment.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<long> source, target;
  target = source;
  return target.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetListAssignmentMultiListAssignmentGroupsCollidingDuplicatesRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-list-assignment-multi-list-assignment-groups-colliding-duplicates.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> values{-1, -3};
  values.rehash(2);
  values.max_load_factor(4.0f);
  values = {9, 7, 9, 5, 7, 9};
  auto group = values.equal_range(9);
  int count = 0;
  for (auto it = group.first; it != group.second; ++it) {
    if (*it != 9) return 1;
    ++count;
  }
  return values.size() != 6 || values.count(7) != 2 || values.count(-1) != 0 ||
         values.count(5) != 1 || count != 3 || values.bucket_count() != 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-list-assignment-multi-list-assignment-groups-colliding-duplicates.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-list-assignment-multi-list-assignment-groups-colliding-duplicates" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetListAssignmentListAssignmentEvaluatesBracedInputBeforeReceiverRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-list-assignment-list-assignment-evaluates-braced-input-before-receiver.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
using Set = std::unordered_set<int>;
int trace;
int input(int n) { trace = trace * 10 + n; return n; }
Set& receiver(Set& set) { trace = trace * 10 + 3; return set; }
int main() {
  Set values;
  receiver(values) = {input(1), input(2)};
  return trace != 123 || values.size() != 2 || values.count(1) != 1 || values.count(2) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-list-assignment-list-assignment-evaluates-braced-input-before-receiver.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-list-assignment-list-assignment-evaluates-braced-input-before-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetListAssignmentCustomEqualityMultiListAssignmentRejected) {
  const auto Source = tmpFile("unordered-set-list-assignment-custom-equality-multi-list-assignment.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Equal { bool operator()(int left, int right) const { return left == right; } };
int main() {
  std::unordered_multiset<int, std::hash<int>, Equal> values;
  values = {3, 3};
  return values.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetListAssignmentShortKeyMultiListAssignmentRejected) {
  const auto Source = tmpFile("unordered-set-list-assignment-short-key-multi-list-assignment.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<short> values;
  values = {5, 5};
  return values.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetIteratorRangeMultiInsertsOneElementFromAGlobalIteratorRangeRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-iterator-range-multi-inserts-one-element-from-a-global-iterator-range.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> source{11, 13};
  std::unordered_multiset<int> target{11, 13, 13};
  auto first = source.begin();
  const auto key = *first;
  const auto old_count = target.count(key);
  auto stop = first;
  ++stop;
  target.insert(first, stop);
  return target.size() != 4 || target.count(key) != old_count + 1 || source.size() != 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-iterator-range-multi-inserts-one-element-from-a-global-iterator-range.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-iterator-range-multi-inserts-one-element-from-a-global-iterator-range" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetIteratorRangeUniqueConstructsFromMultisetGlobalIteratorsRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-iterator-range-unique-constructs-from-multiset-global-iterators.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::unordered_multiset<int> source{3, 3, 7, -5, 7, 19};
  std::unordered_set<int> target(source.begin(), source.end());
  return target.size() != 4 || target.count(3) != 1 || target.count(7) != 1 ||
         target.count(-5) != 1 || target.count(19) != 1 || target.count(0) != 0;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-iterator-range-unique-constructs-from-multiset-global-iterators.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-iterator-range-unique-constructs-from-multiset-global-iterators" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetIteratorRangeEqualNonnullEndpointsFormAnEmptyIteratorRangeRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-iterator-range-equal-nonnull-endpoints-form-an-empty-iterator-range.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::unordered_set<int> source{5};
  const auto position = source.cbegin();
  std::unordered_set<int> target{7};
  target.insert(position, position);
  std::unordered_multiset<int> constructed(position, position);
  return !constructed.empty() || target.size() != 1 || target.count(7) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-iterator-range-equal-nonnull-endpoints-form-an-empty-iterator-range.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-iterator-range-equal-nonnull-endpoints-form-an-empty-iterator-range" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetIteratorRangeUnrelatedListIteratorConstructionRejected) {
  const auto Source = tmpFile("unordered-set-iterator-range-unrelated-list-iterator-construction.cpp");
  writeFile(Source, R"r19containers(#include <list>
#include <unordered_set>
int main() {
  const std::list<int> source{3, 3, 5};
  std::unordered_multiset<int> target(source.cbegin(), source.cend());
  return target.count(3) != 2;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetIteratorRangeCustomHashIteratorConstructionRejected) {
  const auto Source = tmpFile("unordered-set-iterator-range-custom-hash-iterator-construction.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Hash { unsigned long operator()(int value) const { return static_cast<unsigned>(value); } };
int main() {
  const std::unordered_set<int> source{1, 2};
  std::unordered_set<int, Hash> target(source.begin(), source.end());
  return target.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetMergeUniqueMergesOneCopyFromEachNewMultisetKeyRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-merge-unique-merges-one-copy-from-each-new-multiset-key.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> target{3};
  std::unordered_multiset<int> source{3, 5, 3, 5, 7, 7, 7};
  target.merge(source);
  return target.size() != 3 || target.count(5) != 1 || target.count(7) != 1 ||
         source.count(3) != 2 || source.count(5) != 1 || source.count(7) != 2 || source.size() != 5;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-merge-unique-merges-one-copy-from-each-new-multiset-key.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-merge-unique-merges-one-copy-from-each-new-multiset-key" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetMergeMultiMergeGroupsEquivalentKeysInCollidingBucketRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-merge-multi-merge-groups-equivalent-keys-in-colliding-bucket.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> target;
  target.rehash(2);
  target.max_load_factor(4.0f);
  target.insert({3, 7, 7});
  std::unordered_multiset<int> source{3, 5, 3, 7};
  target.merge(source);
  auto group = target.equal_range(3);
  int matches = 0;
  for (auto it = group.first; it != group.second; ++it) {
    if (*it != 3) return 1;
    ++matches;
  }
  return !source.empty() || target.size() != 7 || matches != 3 ||
         target.count(5) != 1 || target.count(7) != 3 || target.bucket_count() != 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-merge-multi-merge-groups-equivalent-keys-in-colliding-bucket.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-merge-multi-merge-groups-equivalent-keys-in-colliding-bucket" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetMergeCustomHashMergeDestinationRejected) {
  const auto Source = tmpFile("unordered-set-merge-custom-hash-merge-destination.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Hash { unsigned long operator()(int key) const { return static_cast<unsigned>(key); } };
int main() {
  std::unordered_set<int> source;
  std::unordered_multiset<int, Hash> target;
  target.merge(source);
  return target.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetMergeLongKeyMergeRejected) {
  const auto Source = tmpFile("unordered-set-merge-long-key-merge.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<long> source;
  std::unordered_multiset<long> target;
  target.merge(source);
  return target.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetObserversMultisetAllocatorEvaluatesArrowReceiverOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-observers-multiset-allocator-evaluates-arrow-receiver-once.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
using Set = std::unordered_multiset<int>;
const Set* observe(const Set& values, int& calls) { ++calls; return &values; }
int main() {
  const Set values{5, 5};
  int calls = 0;
  auto allocator = observe(values, calls)->get_allocator();
  auto* storage = allocator.allocate(2);
  const bool allocated = storage != nullptr;
  allocator.deallocate(storage, 2);
  return !allocated || calls != 1 || values.count(5) != 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-observers-multiset-allocator-evaluates-arrow-receiver-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-observers-multiset-allocator-evaluates-arrow-receiver-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetObserversKeyEqualityOnArrowReceiverComparesDefaultIntegerKeysRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-observers-key-equality-on-arrow-receiver-compares-default-integer-keys.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::unordered_multiset<int> values{3, 3, -1};
  const auto* receiver = &values;
  const auto equal = receiver->key_eq();
  return !equal(-1, -1) || equal(-1, 3) || values.count(3) != 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-observers-key-equality-on-arrow-receiver-compares-default-integer-keys.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-observers-key-equality-on-arrow-receiver-compares-default-integer-keys" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetObserversCustomEqualityArrowObserverRejected) {
  const auto Source = tmpFile("unordered-set-observers-custom-equality-arrow-observer.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Equal { bool operator()(int left, int right) const { return left == right; } };
int main() {
  const std::unordered_multiset<int, std::hash<int>, Equal> values;
  const auto* receiver = &values;
  return receiver->key_eq()(1, 2);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetObserversCustomHashAllocatorObserverRejected) {
  const auto Source = tmpFile("unordered-set-observers-custom-hash-allocator-observer.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Hash { unsigned long operator()(int key) const { return static_cast<unsigned>(key); } };
int main() {
  const std::unordered_multiset<int, Hash> values;
  auto allocator = values.get_allocator();
  auto* storage = allocator.allocate(1);
  allocator.deallocate(storage, 1);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetStorageConstructionMultisetExplicitHashEqualAndAllocatorBucketConstructionRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-storage-construction-multiset-explicit-hash-equal-and-allocator-bucket-construction.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::hash<int> hash;
  const std::equal_to<int> equal;
  const std::allocator<int> allocator;
  std::unordered_multiset<int> values(13u, hash, equal, allocator);
  values.insert(2);
  values.insert(2);
  return values.size() != 2 || values.count(2) != 2 || values.bucket_count() < 13;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-storage-construction-multiset-explicit-hash-equal-and-allocator-bucket-construction.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-storage-construction-multiset-explicit-hash-equal-and-allocator-bucket-construction" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetStorageConstructionMultisetAllocatorOnlyStartsEmptyAndOwnsInsertedKeysRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-storage-construction-multiset-allocator-only-starts-empty-and-owns-inserted-keys.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::allocator<int> allocator;
  std::unordered_multiset<int> values(allocator);
  if (!values.empty()) return 1;
  values.insert(7);
  return values.count(7) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-storage-construction-multiset-allocator-only-starts-empty-and-owns-inserted-keys.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-storage-construction-multiset-allocator-only-starts-empty-and-owns-inserted-keys" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetStorageConstructionMultisetMoveWithEqualDefaultAllocatorRetainsNodeAddressRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-storage-construction-multiset-move-with-equal-default-allocator-retains-node-address.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
#include <utility>
int main() {
  const std::allocator<int> allocator;
  std::unordered_multiset<int> source{31, 31, 5};
  const int* address = &*source.find(5);
  std::unordered_multiset<int> moved(std::move(source), allocator);
  return !source.empty() || moved.count(31) != 2 || &*moved.find(5) != address;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-storage-construction-multiset-move-with-equal-default-allocator-retains-node-address.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-storage-construction-multiset-move-with-equal-default-allocator-retains-node-address" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetStorageConstructionSetCustomHashBucketConstructionRejected) {
  const auto Source = tmpFile("unordered-set-storage-construction-set-custom-hash-bucket-construction.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Hash { unsigned long operator()(int key) const { return static_cast<unsigned>(key); } };
int main() {
  std::unordered_set<int, Hash> values(11u);
  return values.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetStorageConstructionUnapprovedKeyAllocatorConstructionRejected) {
  const auto Source = tmpFile("unordered-set-storage-construction-unapproved-key-allocator-construction.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::allocator<long> allocator;
  std::unordered_set<long> values(allocator);
  return values.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetNodeHandlesMultisetExtractsOneNodeFromConstIteratorRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-node-handles-multiset-extracts-one-node-from-const-iterator.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> values{5, 5, 19};
  const auto& source = values;
  auto* receiver = &values;
  auto node = receiver->extract(source.find(5));
  return !node || node.value() != 5 || values.count(5) != 1 || values.count(19) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-node-handles-multiset-extracts-one-node-from-const-iterator.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-node-handles-multiset-extracts-one-node-from-const-iterator" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetNodeHandlesSetAndMultisetHandleMovesReleaseThePreviousNodeRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-node-handles-set-and-multiset-handle-moves-release-the-previous-node.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
#include <utility>
int main() {
  std::unordered_set<int> unique{17};
  std::unordered_multiset<int> multi{31, 31};
  auto one = unique.extract(17);
  auto two = multi.extract(31);
  auto moved = std::move(one);
  if (one || !moved || moved.value() != 17) return 1;
  moved = std::move(two);
  return !two.empty() || !moved || moved.value() != 31 || multi.count(31) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-node-handles-set-and-multiset-handle-moves-release-the-previous-node.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-node-handles-set-and-multiset-handle-moves-release-the-previous-node" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetNodeHandlesUniqueDuplicateReturnsOwningNodeForRetryRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-node-handles-unique-duplicate-returns-owning-node-for-retry.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
#include <utility>
int main() {
  std::unordered_set<int> source{3, 5};
  std::unordered_set<int> target{3};
  auto node = source.extract(3);
  auto failed = target.insert(std::move(node));
  if (failed.inserted || node || !failed.node || failed.node.value() != 3 || *failed.position != 3) return 1;
  failed.node.value() = 29;
  auto added = target.insert(std::move(failed.node));
  return !added.inserted || added.node || failed.node || *added.position != 29 || target.count(29) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-node-handles-unique-duplicate-returns-owning-node-for-retry.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-node-handles-unique-duplicate-returns-owning-node-for-retry" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetNodeHandlesMultisetInsertNodePreservesStorageAndRehashesMutatedKeyRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-node-handles-multiset-insert-node-preserves-storage-and-rehashes-mutated-key.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
#include <utility>
int main() {
  std::unordered_set<int> source{7};
  std::unordered_multiset<int> target{17, 17};
  target.rehash(8);
  const int* address = &*source.find(7);
  auto node = source.extract(7);
  node.value() = 17;
  auto inserted = target.insert(std::move(node));
  return node || inserted == target.end() || &*inserted != address || *inserted != 17 ||
         target.count(17) != 3 || !source.empty();
}
)r19containers");
  const auto Output = tmpFile("unordered-set-node-handles-multiset-insert-node-preserves-storage-and-rehashes-mutated-key.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-node-handles-multiset-insert-node-preserves-storage-and-rehashes-mutated-key" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetNodeHandlesInsertingEmptyHandleReturnsEndInBothContainersRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-node-handles-inserting-empty-handle-returns-end-in-both-containers.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
#include <utility>
int main() {
  using Set = std::unordered_set<int>;
  Set unique{9};
  std::unordered_multiset<int> multi{9, 9};
  Set::node_type empty;
  auto result = unique.insert(std::move(empty));
  auto no_node = multi.insert(multi.end(), std::move(empty));
  return result.inserted || result.node || result.position != unique.end() || no_node != multi.end() ||
         unique.count(9) != 1 || multi.count(9) != 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-node-handles-inserting-empty-handle-returns-end-in-both-containers.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-node-handles-inserting-empty-handle-returns-end-in-both-containers" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetNodeHandlesOtherKeySetNodeExtractionRejected) {
  const auto Source = tmpFile("unordered-set-node-handles-other-key-set-node-extraction.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<long> values{5};
  auto node = values.extract(5);
  return node.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetNodeHandlesCustomHashUniqueHandleInsertRejected) {
  const auto Source = tmpFile("unordered-set-node-handles-custom-hash-unique-handle-insert.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
#include <utility>
struct Hash { unsigned long operator()(int value) const { return static_cast<unsigned>(value); } };
int main() {
  std::unordered_set<int> source{7};
  std::unordered_set<int, Hash> target;
  auto node = source.extract(7);
  return !target.insert(std::move(node)).inserted;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetBucketReceiversCapacityConstantsEvaluateTheConstPointerReceiverRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-bucket-receivers-capacity-constants-evaluate-the-const-pointer-receiver.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
using Set = std::unordered_set<int>;
const Set* observe(const Set& values, int& calls) { ++calls; return &values; }
int main() {
  const Set values{2, 3};
  int calls = 0;
  const auto keys = observe(values, calls)->max_size();
  const auto buckets = observe(values, calls)->max_bucket_count();
  return calls != 2 || keys < 2 || buckets < values.bucket_count();
}
)r19containers");
  const auto Output = tmpFile("unordered-set-bucket-receivers-capacity-constants-evaluate-the-const-pointer-receiver.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-bucket-receivers-capacity-constants-evaluate-the-const-pointer-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetBucketReceiversPointerReserveAndRehashEvaluateTheReceiverOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-bucket-receivers-pointer-reserve-and-rehash-evaluate-the-receiver-once.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
using Set = std::unordered_multiset<int>;
Set* observe(Set& values, int& calls) { ++calls; return &values; }
int main() {
  Set values{7, 7, 5};
  int calls = 0;
  observe(values, calls)->reserve(19);
  if (values.bucket_count() < 19) return 1;
  observe(values, calls)->rehash(37);
  return calls != 2 || values.bucket_count() < 37 || values.count(7) != 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-bucket-receivers-pointer-reserve-and-rehash-evaluate-the-receiver-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-bucket-receivers-pointer-reserve-and-rehash-evaluate-the-receiver-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetBucketReceiversPointerSetterPreservesTheLiveLoadLimitRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-bucket-receivers-pointer-setter-preserves-the-live-load-limit.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
using Set = std::unordered_set<int>;
Set* observe(Set& values, int& calls) { ++calls; return &values; }
int main() {
  Set values{1, 2};
  const auto load = values.load_factor();
  int calls = 0;
  observe(values, calls)->max_load_factor(load / 2);
  return calls != 1 || values.max_load_factor() != load || values.size() != 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-bucket-receivers-pointer-setter-preserves-the-live-load-limit.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-bucket-receivers-pointer-setter-preserves-the-live-load-limit" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetBucketReceiversCustomHashPointerRehashRejected) {
  const auto Source = tmpFile("unordered-set-bucket-receivers-custom-hash-pointer-rehash.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Hash { unsigned long operator()(int key) const { return static_cast<unsigned>(key); } };
int main() {
  std::unordered_multiset<int, Hash> values;
  auto* p = &values;
  p->rehash(5);
  return values.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetMemberReceiversPointerRangeEvaluatesBucketCountOnceBeforeInsertingRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-member-receivers-pointer-range-evaluates-bucket-count-once-before-inserting.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
static unsigned calls;
unsigned count() { ++calls; return 31; }
int main() {
  const int input[] = {7, 2, 7, 5};
  std::unordered_set<int> values(input, input + 4, count());
  return calls != 1 || values.bucket_count() < 31 || values.size() != 3 || values.count(7) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-member-receivers-pointer-range-evaluates-bucket-count-once-before-inserting.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-member-receivers-pointer-range-evaluates-bucket-count-once-before-inserting" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetMemberReceiversListWithBucketCountAndAllocatorCoalescesDuplicatesRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-member-receivers-list-with-bucket-count-and-allocator-coalesces-duplicates.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> values({2, 8, 2}, 17, std::allocator<int>{});
  return values.bucket_count() < 17 || values.size() != 2 || values.count(2) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-member-receivers-list-with-bucket-count-and-allocator-coalesces-duplicates.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-member-receivers-list-with-bucket-count-and-allocator-coalesces-duplicates" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetMemberReceiversEmptyRangeKeepsExplicitBucketStorageRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-member-receivers-empty-range-keeps-explicit-bucket-storage.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  int input = 5;
  std::unordered_multiset<int> values(&input, &input, 16, std::allocator<int>{});
  return !values.empty() || values.bucket_count() < 16;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-member-receivers-empty-range-keeps-explicit-bucket-storage.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-member-receivers-empty-range-keeps-explicit-bucket-storage" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetMemberReceiversConstPointerGlobalAndLocalIterationRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-member-receivers-const-pointer-global-and-local-iteration.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> values{3, 3, 7};
  values.rehash(16);
  const auto* pointer = &values;
  const auto group = pointer->equal_range(3);
  const auto bucket = values.bucket(3);
  unsigned global = 0, local = 0;
  for (auto it = group.first; it != group.second; ++it) global += *it;
  for (auto it = pointer->cbegin(bucket); it != pointer->cend(bucket); ++it) local += *it;
  return global != 6 || local != 6 || pointer->count(3) != 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-member-receivers-const-pointer-global-and-local-iteration.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-member-receivers-const-pointer-global-and-local-iteration" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetMemberReceiversPointerSwapRetainsElementAddressAndLiveBucketsRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-member-receivers-pointer-swap-retains-element-address-and-live-buckets.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> left{4, 5}, right{8};
  left.reserve(16);
  const int* address = &*left.find(5);
  auto* receiver = &left;
  receiver->swap(right);
  return receiver->count(8) != 1 || right.bucket_count() < 16 || &*right.find(5) != address;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-member-receivers-pointer-swap-retains-element-address-and-live-buckets.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-member-receivers-pointer-swap-retains-element-address-and-live-buckets" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetMemberReceiversBucketedListWithUnapprovedKeyTypeRejected) {
  const auto Source = tmpFile("unordered-set-member-receivers-bucketed-list-with-unapproved-key-type.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<long> values({3L, 7L}, 8);
  return values.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetMemberReceiversUnapprovedKeyPointerMemberRejected) {
  const auto Source = tmpFile("unordered-set-member-receivers-unapproved-key-pointer-member.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<long> values;
  auto* receiver = &values;
  receiver->insert(12L);
  return receiver->count(12L) != 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedMultimapOwnersListKeepsAllEquivalentKeysAndRemovesTheGroupRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multimap-owners-list-keeps-all-equivalent-keys-and-removes-the-group.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
int main() {
  std::unordered_multimap<int, int> values{{1, 8}, {1, 13}, {3, 3}, {1, 21}};
  if (values.size() != 4 || values.count(1) != 3 || values.count(3) != 1) return 1;
  return values.erase(1) != 3 || values.size() != 1 || values.count(1) != 0 || values.count(3) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-multimap-owners-list-keeps-all-equivalent-keys-and-removes-the-group.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multimap-owners-list-keeps-all-equivalent-keys-and-removes-the-group" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultimapOwnersBucketSizePreservesDuplicateMultiplicityAfterRehashRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multimap-owners-bucket-size-preserves-duplicate-multiplicity-after-rehash.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
int main() {
  std::unordered_multimap<int, int> pairs{{3, 13}, {3, 5}, {7, 8}};
  pairs.rehash(16);
  const auto bucket = pairs.bucket(3);
  return bucket >= pairs.bucket_count() || pairs.bucket_size(bucket) != 2 || pairs.count(7) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-multimap-owners-bucket-size-preserves-duplicate-multiplicity-after-rehash.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multimap-owners-bucket-size-preserves-duplicate-multiplicity-after-rehash" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultimapOwnersUnapprovedMappedTypeRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multimap-owners-unapproved-mapped-type.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
int main() {
  std::unordered_multimap<int, long> pairs;
  return pairs.count(1);
}
)r19containers");
  const auto Output = tmpFile("unordered-multimap-owners-unapproved-mapped-type.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multimap-owners-unapproved-mapped-type" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultimapIterationAndTransferIteratorFindAndTraverseWithUniqueOwnerInSameTranslationRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multimap-iteration-and-transfer-iterator-find-and-traverse-with-unique-owner-in-same-translation.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
int main() {
  std::unordered_map<int, int> unique{{4, 17}};
  std::unordered_multimap<int, int> multi{{4, 3}, {4, 9}, {7, 30}};
  auto first = multi.find(4);
  if (first == multi.end() || unique.begin()->second != 17) return 1;
  first->second += 2;
  int sum = 0;
  for (const auto& pair : multi) sum += pair.second;
  return sum != 44 || multi.find(9) != multi.end() || multi.count(4) != 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-multimap-iteration-and-transfer-iterator-find-and-traverse-with-unique-owner-in-same-translation.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multimap-iteration-and-transfer-iterator-find-and-traverse-with-unique-owner-in-same-translation" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultimapIterationAndTransferIteratorErasePositionAndRangeUnlinkOnlySelectedNodesRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multimap-iteration-and-transfer-iterator-erase-position-and-range-unlink-only-selected-nodes.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
int main() {
  std::unordered_multimap<int, int> values{{4, 2}, {4, 3}, {9, 17}, {4, 8}};
  auto group = values.equal_range(4);
  std::unordered_multimap<int, int>::const_iterator first = group.first;
  auto next = values.erase(first);
  if (next == group.second || next->first != 4 || values.count(4) != 2) return 1;
  const auto after = values.erase(next, group.second);
  return after != group.second || values.count(4) || values.size() != 1 || values.begin()->second != 17;
}
)r19containers");
  const auto Output = tmpFile("unordered-multimap-iteration-and-transfer-iterator-erase-position-and-range-unlink-only-selected-nodes.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multimap-iteration-and-transfer-iterator-erase-position-and-range-unlink-only-selected-nodes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultimapIterationAndTransferModifierEmplaceAndInsertReturnTheNewEquivalentNodeRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multimap-iteration-and-transfer-modifier-emplace-and-insert-return-the-new-equivalent-node.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
#include <utility>
int main() {
  std::unordered_multimap<int, int> values{{5, 13}};
  values.reserve(12);
  auto first = values.emplace(5, 8);
  const std::pair<const int, int> input(5, 21);
  auto second = values.insert(input);
  return first == second || first->first != 5 || first->second != 8 ||
         second->second != 21 || values.count(5) != 3;
}
)r19containers");
  const auto Output = tmpFile("unordered-multimap-iteration-and-transfer-modifier-emplace-and-insert-return-the-new-equivalent-node.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multimap-iteration-and-transfer-modifier-emplace-and-insert-return-the-new-equivalent-node" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultimapIterationAndTransferModifierIteratorInputConvertsBetweenCompatibleMappedOwnersRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multimap-iteration-and-transfer-modifier-iterator-input-converts-between-compatible-mapped-owners.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
int main() {
  std::unordered_map<int, int> source{{5, 2}, {3, 13}};
  std::unordered_multimap<int, int> values{{5, 3}};
  values.insert(source.cbegin(), source.cend());
  return values.count(5) != 2 || values.count(3) != 1 ||
         values.find(3)->second != 13 || source.size() != 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-multimap-iteration-and-transfer-modifier-iterator-input-converts-between-compatible-mapped-owners.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multimap-iteration-and-transfer-modifier-iterator-input-converts-between-compatible-mapped-owners" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultimapIterationAndTransferModifierMergeIntoUniqueLeavesEquivalentUntransferredNodesRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multimap-iteration-and-transfer-modifier-merge-into-unique-leaves-equivalent-untransferred-nodes.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
#include <utility>
int main() {
  std::unordered_multimap<int, int> source{{3, 13}, {3, 21}, {5, 8}, {5, 7}};
  std::unordered_map<int, int> target{{5, 100}};
  target.merge(std::move(source));
  return target.size() != 2 || target.find(5)->second != 100 ||
         source.size() != 3 || source.count(3) != 1 || source.count(5) != 2 ||
         target.find(3)->second + source.find(3)->second != 34;
}
)r19containers");
  const auto Output = tmpFile("unordered-multimap-iteration-and-transfer-modifier-merge-into-unique-leaves-equivalent-untransferred-nodes.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multimap-iteration-and-transfer-modifier-merge-into-unique-leaves-equivalent-untransferred-nodes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultimapIterationAndTransferIteratorCustomPolicyIteratorRejected) {
  const auto Source = tmpFile("unordered-multimap-iteration-and-transfer-iterator-custom-policy-iterator.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
struct Hash { unsigned operator()(int key) const { return static_cast<unsigned>(key); } };
int main() {
  std::unordered_multimap<int, int, Hash> values{{3, 5}};
  return values.find(3)->second;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedMultimapNodeHandlesExtractIteratorAndReinsertMutatedMappedValueRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multimap-node-handles-extract-iterator-and-reinsert-mutated-mapped-value.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
#include <utility>
int main() {
  std::unordered_multimap<int, int> values{{3, 5}, {3, 9}, {6, 12}};
  auto where = values.find(3);
  const int* address = &where->second;
  auto node = values.extract(where);
  if (node.empty() || values.count(3) != 1) return 1;
  node.key() = 6;
  node.mapped() = 17;
  const auto inserted = values.insert(std::move(node));
  return !node.empty() || inserted->first != 6 || inserted->second != 17 ||
         &inserted->second != address || values.count(6) != 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-multimap-node-handles-extract-iterator-and-reinsert-mutated-mapped-value.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multimap-node-handles-extract-iterator-and-reinsert-mutated-mapped-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultimapNodeHandlesEmptyExtractionAndInsertionReturnEndRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multimap-node-handles-empty-extraction-and-insertion-return-end.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
#include <utility>
int main() {
  std::unordered_multimap<int, int> values{{3, 11}};
  auto absent = values.extract(9);
  const auto inserted = values.insert(std::move(absent));
  return bool(absent) || inserted != values.end() || values.count(3) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-multimap-node-handles-empty-extraction-and-insertion-return-end.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multimap-node-handles-empty-extraction-and-insertion-return-end" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedPointerObserversMultimapPointerKeyComparatorEvaluatesReceiverRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-pointer-observers-multimap-pointer-key-comparator-evaluates-receiver.cpp");
  writeFile(Source, R"r19containers(#include <map>
using Table = std::multimap<int, int>;
const Table* observe(const Table& values, unsigned& calls) { ++calls; return &values; }
int main() {
  const Table values;
  unsigned calls = 0;
  auto compare = observe(values, calls)->key_comp();
  return calls != 1 || !compare(-2, 7) || compare(7, -2);
}
)r19containers");
  const auto Output = tmpFile("ordered-pointer-observers-multimap-pointer-key-comparator-evaluates-receiver.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-pointer-observers-multimap-pointer-key-comparator-evaluates-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultimapNodeHandlesMappedNodeWithUnapprovedValueTypeRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multimap-node-handles-mapped-node-with-unapproved-value-type.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
int main() {
  std::unordered_multimap<int, long> values{{3, 11L}};
  auto node = values.extract(3);
  return node.mapped() != 11L;
}
)r19containers");
  const auto Output = tmpFile("unordered-multimap-node-handles-mapped-node-with-unapproved-value-type.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multimap-node-handles-mapped-node-with-unapproved-value-type" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetUnsignedKeyLargeKeyBucketAndLocalIteratorRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-unsigned-key-large-key-bucket-and-local-iterator.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  constexpr unsigned high = ~0u;
  std::unordered_set<unsigned> values{high, 2u};
  values.rehash(7);
  const auto bucket = values.bucket(high);
  const auto* receiver = &values;
  if (bucket != high % values.bucket_count() || receiver->hash_function()(high) != high ||
      receiver->bucket_size(bucket) != 1u) return 1;
  auto where = receiver->cbegin(bucket);
  return where == receiver->cend(bucket) || *where != high || ++where != receiver->cend(bucket);
}
)r19containers");
  const auto Output = tmpFile("unordered-set-unsigned-key-large-key-bucket-and-local-iterator.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-unsigned-key-large-key-bucket-and-local-iterator" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetUnsignedKeyNodeTransferRetainsUnsignedPayloadAndAddressRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-unsigned-key-node-transfer-retains-unsigned-payload-and-address.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
#include <utility>
int main() {
  constexpr unsigned high = ~0u;
  std::unordered_set<unsigned> source{1u};
  auto where = source.find(1u);
  const auto* address = &*where;
  auto node = source.extract(where);
  node.value() = high;
  std::unordered_multiset<unsigned> target{high};
  auto inserted = target.insert(std::move(node));
  return !node.empty() || !source.empty() || &*inserted != address ||
         *inserted != high || target.count(high) != 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-unsigned-key-node-transfer-retains-unsigned-payload-and-address.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-unsigned-key-node-transfer-retains-unsigned-payload-and-address" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetListAndLookupListBalancesSortedKeysAndDeduplicatesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-list-and-lookup-list-balances-sorted-keys-and-deduplicates.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  const std::set<int> numbers{-13, -7, -4, -1, 0, 2, 3, 5, 7, 9, 10, 12, 7, 3};
  if (numbers.size() != 12 || numbers.empty()) return 1;
  for (int key = -13; key < 13; ++key) {
    const bool expected = key == -13 || key == -7 || key == -4 || key == -1 ||
                          key == 0 || key == 2 || key == 3 || key == 5 ||
                          key == 7 || key == 9 || key == 10 || key == 12;
    if (numbers.count(key) != unsigned(expected)) return 2;
  }
}
)r19containers");
  const auto Output = tmpFile("ordered-set-list-and-lookup-list-balances-sorted-keys-and-deduplicates.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-list-and-lookup-list-balances-sorted-keys-and-deduplicates" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetListAndLookupListDuplicatesReleaseEachOwnedNodeOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-list-and-lookup-list-duplicates-release-each-owned-node-once.cpp");
  writeFile(Source, R"r19containers(#include <cstddef>
#include <cstdlib>
#include <set>
unsigned allocating, releasing;
void* operator new(std::size_t bytes) { ++allocating; return std::malloc(bytes); }
void operator delete(void* p) noexcept { ++releasing; std::free(p); }
void operator delete(void* p, std::size_t) noexcept { ++releasing; std::free(p); }
int main() {
  {
    const std::set<int> values{3, 1, 7, 3, 9, 7};
    if (values.size() != 4 || allocating != 4 || releasing != 0) return 1;
  }
  return allocating != 4 || releasing != 4;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-list-and-lookup-list-duplicates-release-each-owned-node-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-list-and-lookup-list-duplicates-release-each-owned-node-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetInsertResultPointerInsertionUsesTheCheckedReceiverOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-insert-result-pointer-insertion-uses-the-checked-receiver-once.cpp");
  writeFile(Source, R"r19containers(#include <set>
using Set = std::set<int>;
Set* receiver(Set& values, int& calls) { ++calls; return &values; }
int main() {
  Set values{9, 7, 3};
  int calls = 0;
  auto result = receiver(values, calls)->insert(-6);
  return calls != 1 || !result.second || values.count(-6) != 1 || values.size() != 4;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-insert-result-pointer-insertion-uses-the-checked-receiver-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-insert-result-pointer-insertion-uses-the-checked-receiver-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetIteratorPointerEndpointsAndPostfixTraversalRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-iterator-pointer-endpoints-and-postfix-traversal.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  const std::set<int> values{9, -6, 3, 7};
  const auto* receiver = &values;
  auto last = receiver->cend();
  --last;
  auto previous = last--;
  auto first = receiver->cbegin();
  auto old = first++;
  return *last != 7 || *previous != 9 || *first != 3 || *old != -6;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-iterator-pointer-endpoints-and-postfix-traversal.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-iterator-pointer-endpoints-and-postfix-traversal" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetBoundsBoundsOnKeysAndGapsRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-bounds-bounds-on-keys-and-gaps.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  std::set<int> values{13, -6, 3, 0, 7, -6};
  auto exact = values.find(-6);
  auto missing = values.find(-5);
  auto after = values.upper_bound(3);
  auto at = values.lower_bound(3);
  auto gap = values.lower_bound(4);
  auto low = values.upper_bound(-9);
  auto end = values.end();
  auto high = values.lower_bound(14);
  return *exact != -6 || missing != end || *after != 7 || *at != 3 ||
         gap != after || low != exact || high != end;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-bounds-bounds-on-keys-and-gaps.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-bounds-bounds-on-keys-and-gaps" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetBoundsEmptyLookupEvaluatesReceiverAndKeyOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-bounds-empty-lookup-evaluates-receiver-and-key-once.cpp");
  writeFile(Source, R"r19containers(#include <set>
using Set = std::set<int>;
const Set* receiver(const Set& values, int& n) { ++n; return &values; }
int key(int& n) { ++n; return 4; }
int main() {
  const Set values;
  int owners = 0, keys = 0;
  auto result = receiver(values, owners)->lower_bound(key(keys));
  auto end = values.end();
  return result != end || owners != 1 || keys != 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-bounds-empty-lookup-evaluates-receiver-and-key-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-bounds-empty-lookup-evaluates-receiver-and-key-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetInsertResultUnapprovedCustomComparatorForScalarInsertionRejected) {
  const auto Source = tmpFile("ordered-set-insert-result-unapproved-custom-comparator-for-scalar-insertion.cpp");
  writeFile(Source, R"r19containers(#include <set>
struct Descending { bool operator()(int l, int r) const { return l > r; } };
int main() {
  std::set<int, Descending> values;
  return !values.insert(3).second;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedSetBoundsCustomOrderingDoesNotUseIntegerBoundsRejected) {
  const auto Source = tmpFile("ordered-set-bounds-custom-ordering-does-not-use-integer-bounds.cpp");
  writeFile(Source, R"r19containers(#include <set>
struct Descending { bool operator()(int left, int right) const { return left > right; } };
int main() {
  const std::set<int, Descending> values{2, 8};
  return *values.lower_bound(5);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedSetErasurePointerPositionAndRangeReturnTheRetainedSuccessorRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-erasure-pointer-position-and-range-return-the-retained-successor.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  std::set<int> values{4, 1, 7, 0, 2, 6, 9, 5, 3, 8};
  auto* receiver = &values;
  auto first = values.lower_bound(2);
  auto last = values.lower_bound(8);
  const int* retained = &*last;
  auto end_of_range = receiver->erase(first, last);
  if (end_of_range != last || &*end_of_range != retained || values.size() != 4) return 1;
  auto next = receiver->erase(last);
  if (*next != 9) return 2;
  auto end = values.end();
  auto after_last = receiver->erase(next);
  return after_last != end || values.size() != 2 || values.count(1) != 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-erasure-pointer-position-and-range-return-the-retained-successor.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-erasure-pointer-position-and-range-return-the-retained-successor" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetTransferMoveConstructionReparentsTheRootAndKeepsNodeAddressesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-transfer-move-construction-reparents-the-root-and-keeps-node-addresses.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <utility>
int main() {
  std::set<int> source{7, 3, 9, 5};
  auto first = source.begin();
  const int* node = &*first;
  std::set<int> result(std::move(source));
  if (!source.empty() || result.size() != 4 || &*first != node) return 1;
  auto last = result.end();
  --last;
  if (*first != 3 || *last != 9) return 2;
  ++last;
  auto end = result.end();
  return last != end || !source.insert(-4).second;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-transfer-move-construction-reparents-the-root-and-keeps-node-addresses.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-transfer-move-construction-reparents-the-root-and-keeps-node-addresses" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetTransferSwapTwoLiveTreesPreservesTheirNodeOrderAndIdentityRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-transfer-swap-two-live-trees-preserves-their-node-order-and-identity.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  std::set<int> left{2, 4};
  std::set<int> right{-1, 7, 9};
  auto two = left.begin();
  auto minus_one = right.begin();
  left.swap(right);
  ++two;
  ++minus_one;
  return left.size() != 3 || right.size() != 2 || *two != 4 || *minus_one != 7;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-transfer-swap-two-live-trees-preserves-their-node-order-and-identity.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-transfer-swap-two-live-trees-preserves-their-node-order-and-identity" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetEqualRangeConstPointerEqualRangeEvaluatesTheKeyOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-equal-range-const-pointer-equal-range-evaluates-the-key-once.cpp");
  writeFile(Source, R"r19containers(#include <set>
int get_key(int& calls) { ++calls; return 7; }
int main() {
  const std::set<int> keys{2, 7, 9};
  const auto* owner = &keys;
  int calls = 0;
  auto range = owner->equal_range(get_key(calls));
  return *range.first != 7 || *range.second != 9 || calls != 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-equal-range-const-pointer-equal-range-evaluates-the-key-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-equal-range-const-pointer-equal-range-evaluates-the-key-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultisetEquivalentInsertionFollowsExistingNodesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multiset-equivalent-insertion-follows-existing-nodes.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  std::multiset<int> values{9, -4, 9, 11, -4};
  auto first = values.lower_bound(9);
  auto second = first;
  ++second;
  auto added = values.insert(9);
  auto range = values.equal_range(9);
  if (values.count(9) != 3 || *added != 9 || range.first != first) return 1;
  ++second;
  if (second != added) return 2;
  ++added;
  return added != range.second || values.count(-4) != 2 || values.size() != 6;
}
)r19containers");
  const auto Output = tmpFile("ordered-multiset-equivalent-insertion-follows-existing-nodes.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multiset-equivalent-insertion-follows-existing-nodes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultisetSwapWithASetIteratorInTheSameTranslationRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multiset-swap-with-a-set-iterator-in-the-same-translation.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  std::set<int> unique{3, 3, 6};
  std::multiset<int> left{2, 9, 9};
  std::multiset<int> right;
  auto previous = left.begin();
  left.swap(right);
  auto edge = right.end();
  --edge;
  auto original = unique.begin();
  return !left.empty() || right.size() != 3 || unique.size() != 2 ||
         *previous != 2 || *edge != 9 || *original != 3;
}
)r19containers");
  const auto Output = tmpFile("ordered-multiset-swap-with-a-set-iterator-in-the-same-translation.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multiset-swap-with-a-set-iterator-in-the-same-translation" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetCopyConstructionMultisetDuplicatesAndIndependentLifetimeRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-copy-construction-multiset-duplicates-and-independent-lifetime.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  std::multiset<int> source{-5, 3, 3, 12, 3, -5};
  const std::multiset<int> copy(source);
  auto equivalent = copy.equal_range(3);
  if (copy.count(3) != 3 || &*equivalent.first == &*source.lower_bound(3)) return 1;
  source.clear();
  int seen = 0;
  for (auto current = equivalent.first; current != equivalent.second; ++current) {
    if (*current != 3) return 2;
    ++seen;
  }
  return seen != 3 || copy.size() != 6 || copy.count(-5) != 2 || !source.empty();
}
)r19containers");
  const auto Output = tmpFile("ordered-set-copy-construction-multiset-duplicates-and-independent-lifetime.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-copy-construction-multiset-duplicates-and-independent-lifetime" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetCopyAssignmentSetReusesOldNodeAllocationsAndReleasesOnlySurplusRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-copy-assignment-set-reuses-old-node-allocations-and-releases-only-surplus.cpp");
  writeFile(Source, R"r19containers(#include <cstddef>
#include <cstdlib>
#include <set>
unsigned allocations, releases;
void* operator new(std::size_t bytes) { ++allocations; return std::malloc(bytes); }
void operator delete(void* value) noexcept { ++releases; std::free(value); }
void operator delete(void* value, std::size_t) noexcept { ++releases; std::free(value); }
int main() {
  {
    std::set<int> target{-9, 4, 8, 13, 29, 73};
    const std::set<int> source{-3, 17, 18};
    auto& result = (target = source);
    if (&result != &target || target.size() != 3 || allocations != 9 || releases != 3) return 1;
    if (target.count(4) != 0 || target.count(17) != 1 || &*target.find(17) == &*source.find(17)) return 2;
    const auto& same = target;
    const int* address = &*target.begin();
    target = same;
    if (&*target.begin() != address || allocations != 9 || releases != 3) return 3;
  }
  return releases != allocations;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-copy-assignment-set-reuses-old-node-allocations-and-releases-only-surplus.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-copy-assignment-set-reuses-old-node-allocations-and-releases-only-surplus" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetCopyAssignmentAssignmentNotationAndExplicitMemberSequenceBothSidesOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-copy-assignment-assignment-notation-and-explicit-member-sequence-both-sides-once.cpp");
  writeFile(Source, R"r19containers(#include <set>
using Set = std::set<int>;
int trace;
Set* select(Set& value) { trace = trace * 10 + 2; return &value; }
const Set& input(const Set& value) { trace = trace * 10 + 1; return value; }
int main() {
  Set a{1, 3, 5};
  Set b{2, 4, 6};
  *select(a) = input(b);
  if (trace != 12 || a.count(4) != 1) return 1;
  trace = 0;
  auto& result = select(b)->operator=(input(a));
  return trace != 21 || &result != &b || b.size() != 3 || b.count(6) != 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-copy-assignment-assignment-notation-and-explicit-member-sequence-both-sides-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-copy-assignment-assignment-notation-and-explicit-member-sequence-both-sides-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetTransferTransparentComparatorDoesNotUseTheDefaultTransferRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-transfer-transparent-comparator-does-not-use-the-default-transfer.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <set>
#include <utility>
int main() {
  std::set<int, std::less<>> source;
  std::set<int, std::less<>> target(std::move(source));
  return target.empty() ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-transfer-transparent-comparator-does-not-use-the-default-transfer.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-transfer-transparent-comparator-does-not-use-the-default-transfer" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultisetCustomOrderingRemainsGuardedRejected) {
  const auto Source = tmpFile("ordered-multiset-custom-ordering-remains-guarded.cpp");
  writeFile(Source, R"r19containers(#include <set>
struct Descending { bool operator()(int l, int r) const { return l > r; } };
int main() {
  std::multiset<int, Descending> values{2, 2, 3};
  return values.count(2) != 2;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedSetCopyAssignmentCustomComparatorRemainsOutsideCheckedAssignmentRejected) {
  const auto Source = tmpFile("ordered-set-copy-assignment-custom-comparator-remains-outside-checked-assignment.cpp");
  writeFile(Source, R"r19containers(#include <set>
struct Descending { bool operator()(int a, int b) const { return a > b; } };
int main() {
  std::multiset<int, Descending> from{7, 2, 7}, to;
  to = from;
  return to.count(7) != 2;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapPointerReceiverAndKeyEvaluatedOnceInOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-pointer-receiver-and-key-evaluated-once-in-order.cpp");
  writeFile(Source, R"r19containers(#include <map>
using Map = std::map<int, int>;
int trace;
Map* select(Map& value) { trace = 10 * trace + 2; return &value; }
int key() { trace = 10 * trace + 1; return -18; }
int main() {
  Map map;
  int& value = select(map)->operator[](key());
  value = 19;
  return trace != 21 || map.size() != 1 || map[-18] != 19;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-pointer-receiver-and-key-evaluated-once-in-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-pointer-receiver-and-key-evaluated-once-in-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapContainsIntegerKeysAfterInsertionAndPreservesSizeRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-contains-integer-keys-after-insertion-and-preserves-size.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> map;
  map[-2147483647 - 1] = 1;
  map[-14] = 2;
  map[17] = 3;
  map[2147483647] = 4;
  const auto& view = map;
  return view.count(-2147483647 - 1) != 1 || view.count(-14) != 1 ||
         view.count(0) != 0 || view.count(2147483647) != 1 || view.size() != 4;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-contains-integer-keys-after-insertion-and-preserves-size.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-contains-integer-keys-after-insertion-and-preserves-size" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapInitializerListConstructsOrderedMapWithFirstMappedValueRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-initializer-list-constructs-ordered-map-with-first-mapped-value.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> values{{8, 71}, {-5, 9}, {8, 91}, {13, 23}, {-5, 11}};
  if (values.size() != 3 || values.count(-5) != 1 || values.count(0) != 0) return 1;
  return values[8] != 71 || values[-5] != 9 || values[13] != 23 || values.size() != 3;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-initializer-list-constructs-ordered-map-with-first-mapped-value.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-initializer-list-constructs-ordered-map-with-first-mapped-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapInitializerListDestroysEachMappedNodeOnlyOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-initializer-list-destroys-each-mapped-node-only-once.cpp");
  writeFile(Source, R"r19containers(#include <cstddef>
#include <cstdlib>
#include <map>
unsigned allocations, releases;
void* operator new(std::size_t bytes) { ++allocations; return std::malloc(bytes); }
void operator delete(void* value) noexcept { ++releases; std::free(value); }
void operator delete(void* value, std::size_t) noexcept { ++releases; std::free(value); }
int main() {
  {
    std::multimap<int, int> values{{6, 19}, {7, 21}, {6, 47}};
    if (values.size() != 3 || allocations != 3 || releases != 0) return 1;
  }
  return releases != 3;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-initializer-list-destroys-each-mapped-node-only-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-initializer-list-destroys-each-mapped-node-only-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapIteratorConstIteratorTraversesBoundsAndDecrementsEndRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-iterator-const-iterator-traverses-bounds-and-decrements-end.cpp");
  writeFile(Source, R"r19containers(#include <map>
using Map = std::map<int, int>;
int main() {
  const Map values{{-3, 4}, {0, 12}, {18, 21}, {81, 42}};
  const Map* view = &values;
  auto gap = view->lower_bound(-2);
  auto upper = view->upper_bound(18);
  auto last = view->cend();
  --last;
  if (gap->first != 0 || gap->second != 12 || upper != last || (*last).second != 42) return 1;
  auto previous = last--;
  auto first = view->cbegin();
  return previous->first != 81 || last->first != 18 || first->second != 4;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-iterator-const-iterator-traverses-bounds-and-decrements-end.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-iterator-const-iterator-traverses-bounds-and-decrements-end" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapErasureKeyEraseKeepsTheMappedAddressOfAnotherNodeRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-erasure-key-erase-keeps-the-mapped-address-of-another-node.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> values{{4, 51}, {8, 23}, {1, 11}, {6, 77}, {13, 2}};
  auto stable = values.find(6);
  int* address = &stable->second;
  if (values.erase(4) != 1 || values.erase(4) != 0 || values.size() != 4) return 1;
  auto again = values.find(6);
  return stable != again || &again->second != address || *address != 77;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-erasure-key-erase-keeps-the-mapped-address-of-another-node.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-erasure-key-erase-keeps-the-mapped-address-of-another-node" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapErasureMultimapErasesEveryDuplicateAndClearReseatsSentinelRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-erasure-multimap-erases-every-duplicate-and-clear-reseats-sentinel.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::multimap<int, int> values{{7, 14}, {8, 16}, {7, 29}, {9, 2}, {7, 36}};
  auto keep = values.find(8);
  int* stable = &keep->second;
  if (values.erase(7) != 3 || values.count(7) != 0 || values.size() != 2 ||
      &keep->second != stable || *stable != 16) return 1;
  values.clear();
  auto first = values.begin();
  auto end = values.end();
  return first != end || !values.empty();
}
)r19containers");
  const auto Output = tmpFile("ordered-map-erasure-multimap-erases-every-duplicate-and-clear-reseats-sentinel.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-erasure-multimap-erases-every-duplicate-and-clear-reseats-sentinel" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapInsertionMultimapInsertReturnsMappedNodeInDuplicateOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-insertion-multimap-insert-returns-mapped-node-in-duplicate-order.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <utility>
int main() {
  std::multimap<int, int> values{{7, 13}, {9, 1}};
  const std::pair<const int, int> payload{7, 17};
  auto inserted = values.insert(payload);
  auto bound = values.equal_range(7);
  ++bound.first;
  if (bound.first != inserted || inserted->second != 17) return 1;
  inserted->second = 31;
  auto next = inserted;
  ++next;
  return next != bound.second || inserted->second != 31 || values.count(7) != 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-insertion-multimap-insert-returns-mapped-node-in-duplicate-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-insertion-multimap-insert-returns-mapped-node-in-duplicate-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapScalarEmplacementTryEmplaceKeepsExistingValueAndDefaultsANewNodeRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-scalar-emplacement-try-emplace-keeps-existing-value-and-defaults-a-new-node.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> values;
  const int key = 19;
  const int value = 47;
  auto first = values.try_emplace(key, value);
  auto duplicate = values.try_emplace(19, 5);
  auto zero = values.try_emplace(-4);
  return !first.second || duplicate.second || !zero.second || first.first != duplicate.first ||
         first.first->second != 47 || zero.first->first != -4 || zero.first->second != 0 || values.size() != 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-scalar-emplacement-try-emplace-keeps-existing-value-and-defaults-a-new-node.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-scalar-emplacement-try-emplace-keeps-existing-value-and-defaults-a-new-node" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapScalarEmplacementTryEmplaceEvaluatesArgumentsOnADuplicateRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-scalar-emplacement-try-emplace-evaluates-arguments-on-a-duplicate.cpp");
  writeFile(Source, R"r19containers(#include <map>
int calls;
int& observe(int& value) {
  ++calls;
  return value;
}
int main() {
  std::map<int, int> values{{4, 17}};
  int key = 4;
  int value = 21;
  auto result = values.try_emplace(observe(key), observe(value));
  return calls != 2 || result.second || result.first->second != 17 || values.size() != 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-scalar-emplacement-try-emplace-evaluates-arguments-on-a-duplicate.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-scalar-emplacement-try-emplace-evaluates-arguments-on-a-duplicate" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapTransferMapMoveKeepsReferencesAndTraversesNewSentinelRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-transfer-map-move-keeps-references-and-traverses-new-sentinel.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <utility>
int main() {
  std::map<int, int> source{{7, 21}, {3, 14}, {-1, 6}};
  auto saved = source.find(3);
  int* address = &saved->second;
  std::map<int, int> dest(std::move(source));
  if (!source.empty() || dest.size() != 3 || &saved->second != address) return 1;
  ++saved;
  if (saved->first != 7 || saved->second != 21) return 2;
  ++saved;
  return saved != dest.end();
}
)r19containers");
  const auto Output = tmpFile("ordered-map-transfer-map-move-keeps-references-and-traverses-new-sentinel.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-transfer-map-move-keeps-references-and-traverses-new-sentinel" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapCopyAssignmentReplacesAndOwnsMappedValuesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-copy-assignment-replaces-and-owns-mapped-values.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> input{{4, 44}, {1, 11}};
  std::map<int, int> output{{9, 90}, {5, 50}, {2, 20}};
  auto *receiver = &output;
  auto &result = receiver->operator=(input);
  if (&result != receiver || output.size() != 2 || output.begin()->first != 1 ||
      output.find(4)->second != 44 || output.count(9) != 0)
    return 1;
  auto *value = &output.find(4)->second;
  auto &same = output;
  output = same;
  if (&output.find(4)->second != value)
    return 2;
  *value = 77;
  return input.find(4)->second == 44 && output.find(4)->second == 77 ? 0 : 3;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-copy-assignment-replaces-and-owns-mapped-values.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-copy-assignment-replaces-and-owns-mapped-values" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapCopyAssignmentPreservesOperandEvaluationOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-copy-assignment-preserves-operand-evaluation-order.cpp");
  writeFile(Source, R"r19containers(#include <map>
using Table = std::map<int, int>;
Table &source(Table *value, int *state) {
  *state = *state * 10 + 1;
  return *value;
}
Table &target(Table *value, int *state) {
  *state = *state * 10 + 2;
  return *value;
}
int main() {
  Table input{{1, 17}};
  Table output;
  int order = 0;
  target(&output, &order) = source(&input, &order);
  return order == 12 && output.size() == 1 && output.begin()->second == 17 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-copy-assignment-preserves-operand-evaluation-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-copy-assignment-preserves-operand-evaluation-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapIteratorConversionKeepsNodeAndConstViewRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-iterator-conversion-keeps-node-and-const-view.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> values{{4, 40}, {1, 10}};
  auto mutable_cursor = values.find(4);
  std::map<int, int>::const_iterator read_cursor = mutable_cursor;
  mutable_cursor->second = 44;
  const auto &view = values;
  if (read_cursor != view.find(4) || read_cursor->second != 44)
    return 1;
  ++read_cursor;
  return read_cursor == view.end() ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-iterator-conversion-keeps-node-and-const-view.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-iterator-conversion-keeps-node-and-const-view" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapIteratorConversionMixedEqualitySeesOwnerEndRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-iterator-conversion-mixed-equality-sees-owner-end.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> values{{1, 21}, {5, 25}};
  const auto &view = values;
  if (values.begin() != view.begin() || view.end() != values.end())
    return 1;
  auto cursor = values.begin();
  ++cursor;
  return cursor == view.find(5) && view.find(1) != cursor ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-iterator-conversion-mixed-equality-sees-owner-end.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-iterator-conversion-mixed-equality-sees-owner-end" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapCountsWithLongMappedValues) {
  const auto Source = tmpFile("ordered-map-unsupported-mapped-type-does-not-admit-count.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, long> map;
  return map.count(7) != 0;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-unsupported-mapped-type-does-not-admit-count.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-unsupported-mapped-type-does-not-admit-count" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapNonDefaultKeyOrderRemainsOutsideCheckedSubscriptRejected) {
  const auto Source = tmpFile("ordered-map-non-default-key-order-remains-outside-checked-subscript.cpp");
  writeFile(Source, R"r19containers(#include <map>
struct Descending { bool operator()(int a, int b) const { return a > b; } };
int main() {
  std::map<int, int, Descending> map;
  map[3] = 12;
  return map[3] != 12;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapIteratorCustomMapIteratorReceiverKeepsSourceGuardRejected) {
  const auto Source = tmpFile("ordered-map-iterator-custom-map-iterator-receiver-keeps-source-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
struct Descending { bool operator()(int a, int b) const { return a > b; } };
int main() {
  std::map<int, int, Descending> values{{2, 5}, {6, 7}};
  auto position = values.begin();
  return position->first != 6;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapTransferStatefulOrderedMapComparatorHasSeparateTransferSemanticsRejected) {
  const auto Source = tmpFile("ordered-map-transfer-stateful-ordered-map-comparator-has-separate-transfer-semantics.cpp");
  writeFile(Source, R"r19containers(#include <map>
struct Compare {
  bool reverse;
  bool operator()(int left, int right) const {
    return reverse ? right < left : left < right;
  }
};
int main() {
  std::map<int, int, Compare> left(Compare{true});
  std::map<int, int, Compare> right(Compare{false});
  left.swap(right);
  return left.size();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapIteratorConversionCustomComparatorRemainsUnsupportedRejected) {
  const auto Source = tmpFile("ordered-map-iterator-conversion-custom-comparator-remains-unsupported.cpp");
  writeFile(Source, R"r19containers(#include <map>
struct Reverse {
  bool operator()(int left, int right) const { return left > right; }
};
using Table = std::map<int, int, Reverse>;
bool copy_cursor(Table *values) {
  Table::const_iterator read_cursor = values->begin();
  return read_cursor == values->cend();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapMoveAssignmentEmptyMultimapRebindsSentinelRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-move-assignment-empty-multimap-rebinds-sentinel.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::multimap<int, int> input;
  std::multimap<int, int> output{{1, 17}, {1, 19}};
  auto *owner = &output;
  auto &result = owner->operator=(static_cast<std::multimap<int, int>&&>(input));
  if (&result != owner || !input.empty() || !output.empty() ||
      input.begin() != input.end() || output.begin() != output.end())
    return 1;
  std::multimap<int, int>::value_type value{2, 20};
  auto cursor = output.insert(value);
  return output.size() == 1 && cursor->second == 20 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-move-assignment-empty-multimap-rebinds-sentinel.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-move-assignment-empty-multimap-rebinds-sentinel" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapInsertListPreservesOldAndFirstNewMappedValuesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-insert-list-preserves-old-and-first-new-mapped-values.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> values{{2, 20}};
  auto *receiver = &values;
  receiver->insert({{2, 99}, {1, 10}, {1, 11}, {3, 30}});
  values.insert({});
  return values.size() == 3 && values.find(1)->second == 10 &&
         values.find(2)->second == 20 && values.find(3)->second == 30 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-insert-list-preserves-old-and-first-new-mapped-values.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-insert-list-preserves-old-and-first-new-mapped-values" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetInsertListEvaluatesReceiverAndListOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-insert-list-evaluates-receiver-and-list-once.cpp");
  writeFile(Source, R"r19containers(#include <set>
int item(int &state, int value) {
  state = state * 10 + value;
  return value;
}
std::set<int>* owner(std::set<int> &values, int &state) {
  state = state * 10 + 1;
  return &values;
}
int main() {
  std::set<int> values{1, 2};
  int state = 0;
  owner(values, state)->insert({item(state, 3), item(state, 2), item(state, 4)});
  values.insert({});
  return state == 1324 && values.size() == 4 && values.count(4) == 1 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-insert-list-evaluates-receiver-and-list-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-insert-list-evaluates-receiver-and-list-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultisetPointerRangeConstInputRetainsDuplicatesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multiset-pointer-range-const-input-retains-duplicates.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  const int input[] = {4, -2, 4, 5, 4};
  std::multiset<int> values(input, input + 2);
  auto *owner = &values;
  owner->insert(input + 2, input + 5);
  return values.size() == 5 && values.count(4) == 3 &&
         *values.begin() == -2 && values.count(5) == 1 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-multiset-pointer-range-const-input-retains-duplicates.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multiset-pointer-range-const-input-retains-duplicates" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapPointerRangeEvaluatesEndpointsOnceAndKeepsOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-pointer-range-evaluates-endpoints-once-and-keeps-order.cpp");
  writeFile(Source, R"r19containers(#include <map>
using Table = std::multimap<int, int>;
const Table::value_type *endpoint(const Table::value_type *where, int &calls) {
  ++calls;
  return where;
}
int main() {
  const Table::value_type input[] = {{2, 20}, {1, 11}, {2, 21}, {2, 22}};
  Table values(input, input + 1);
  int first_calls = 0;
  int last_calls = 0;
  values.insert(endpoint(input + 1, first_calls), endpoint(input + 4, last_calls));
  auto bounds = values.equal_range(2);
  auto cursor = bounds.first;
  if (first_calls != 1 || last_calls != 1 || values.size() != 4 ||
      cursor == bounds.second || cursor->second != 20) return 1;
  ++cursor;
  if (cursor == bounds.second || cursor->second != 21) return 2;
  ++cursor;
  if (cursor == bounds.second || cursor->second != 22) return 3;
  return ++cursor == bounds.second ? 0 : 4;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-pointer-range-evaluates-endpoints-once-and-keeps-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-pointer-range-evaluates-endpoints-once-and-keeps-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapMoveAssignmentCustomComparatorRemainsUnsupportedRejected) {
  const auto Source = tmpFile("ordered-map-move-assignment-custom-comparator-remains-unsupported.cpp");
  writeFile(Source, R"r19containers(#include <map>
struct Reverse {
  bool operator()(int left, int right) const { return left > right; }
};
using Table = std::map<int, int, Reverse>;
void transfer(Table *output, Table *input) { *output = static_cast<Table&&>(*input); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedSetPointerRangeCustomComparatorRemainsUnsupportedRejected) {
  const auto Source = tmpFile("ordered-set-pointer-range-custom-comparator-remains-unsupported.cpp");
  writeFile(Source, R"r19containers(#include <set>
struct Reverse {
  bool operator()(int a, int b) const { return a > b; }
};
void copy(const int *first, const int *last) {
  std::set<int, Reverse> values(first, last);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMultimapListAssignmentKeepsTheNewEquivalentOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-list-assignment-keeps-the-new-equivalent-order.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::multimap<int, int> values{{2, 20}, {2, 21}};
  auto *pointer = &values;
  auto &result = pointer->operator=({{2, 32}, {1, 17}, {2, 34}, {2, 31}});
  auto bounds = values.equal_range(2);
  auto cursor = bounds.first;
  if (&result != pointer || values.size() != 4 ||
      cursor == bounds.second || cursor->second != 32) return 1;
  ++cursor;
  if (cursor == bounds.second || cursor->second != 34) return 2;
  ++cursor;
  if (cursor == bounds.second || cursor->second != 31) return 3;
  return ++cursor == bounds.second && values.begin()->second == 17 ? 0 : 4;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-list-assignment-keeps-the-new-equivalent-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-list-assignment-keeps-the-new-equivalent-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultisetEmptyListAssignmentReusesAnEmptyOwnerAfterClearRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multiset-empty-list-assignment-reuses-an-empty-owner-after-clear.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  std::multiset<int> values{4, 3, 4};
  values = {};
  if (!values.empty() || values.begin() != values.end()) return 1;
  values = {5, 2, 5};
  auto cursor = values.end();
  --cursor;
  return values.size() == 3 && values.count(5) == 2 && *cursor == 5 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-multiset-empty-list-assignment-reuses-an-empty-owner-after-clear.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multiset-empty-list-assignment-reuses-an-empty-owner-after-clear" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetEmplaceDefaultDestroysARejectedZeroNodeRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-emplace-default-destroys-a-rejected-zero-node.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <cstddef>
#include <cstdlib>
static unsigned allocations;
static unsigned releases;
void *operator new(std::size_t bytes) {
  ++allocations;
  return std::malloc(bytes);
}
void operator delete(void *storage) noexcept {
  if (storage) ++releases;
  std::free(storage);
}
int main() {
  {
    std::set<int> values;
    auto first = values.emplace();
    auto duplicate = values.emplace();
    if (!first.second || duplicate.second || first.first != duplicate.first ||
        *first.first != 0 || values.size() != 1 || allocations != 2 || releases != 1) return 1;
  }
  return allocations == 2 && releases == 2 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-emplace-default-destroys-a-rejected-zero-node.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-emplace-default-destroys-a-rejected-zero-node" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetEmplaceConvertingKeyAcceptsFunctionBody) {
  const auto Source = tmpFile("ordered-set-emplace-converting-key-remains-unsupported.cpp");
  writeFile(Source, R"r19containers(#include <set>
auto append(std::set<int> &values, short key) { return values.emplace(key); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedSetMergeTransfersOriginalAddressesWithoutAllocatingRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-merge-transfers-original-addresses-without-allocating.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <cstddef>
#include <cstdlib>
static unsigned allocations;
static unsigned releases;
void *operator new(std::size_t count) {
  ++allocations;
  return std::malloc(count);
}
void operator delete(void *storage) noexcept {
  if (storage) ++releases;
  std::free(storage);
}
int main() {
  {
    std::set<int> output{4, 2};
    std::set<int> input{1, 4, 7, 6};
    const int *key = &*input.find(7);
    output.merge(input);
    if (allocations != 6 || releases || output.size() != 5 || input.size() != 1 ||
        &*output.find(7) != key || *input.begin() != 4 || *output.begin() != 1) return 1;
  }
  return allocations == 6 && releases == 6 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-merge-transfers-original-addresses-without-allocating.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-merge-transfers-original-addresses-without-allocating" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapMergeRvalueMapAppendsEquivalentNodeRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-merge-rvalue-map-appends-equivalent-node.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::multimap<int, int> output{{2, 20}, {2, 21}};
  std::map<int, int> input{{1, 11}, {2, 22}};
  auto *transferred = &input.find(2)->second;
  auto *owner = &output;
  owner->merge(static_cast<std::map<int, int>&&>(input));
  if (!input.empty() || input.begin() != input.end() || owner->size() != 4) return 1;
  auto range = output.equal_range(2);
  auto cursor = range.first;
  if (cursor->second != 20 || (++cursor)->second != 21 || (++cursor)->second != 22) return 2;
  if (&cursor->second != transferred || ++cursor != range.second) return 3;
  return output.begin()->second == 11 ? 0 : 4;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-merge-rvalue-map-appends-equivalent-node.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-merge-rvalue-map-appends-equivalent-node" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetComparisonConstPrefixAndAllRelationsRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-comparison-const-prefix-and-all-relations.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  const std::set<int> prefix{-4, 2};
  const std::set<int> longer{8, 2, -4};
  const std::set<int> equal{2, -4};
  const std::set<int> empty;
  if (!(empty < prefix && prefix < longer && longer > prefix && prefix <= longer &&
        longer >= prefix && prefix != longer)) return 1;
  return prefix == equal && !(prefix != equal) && prefix <= equal && prefix >= equal &&
         !(prefix < equal) && !(prefix > equal) ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-comparison-const-prefix-and-all-relations.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-comparison-const-prefix-and-all-relations" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapComparisonRetainsEquivalentNodeOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-comparison-retains-equivalent-node-order.cpp");
  writeFile(Source, R"r19containers(#include <map>
using Table = std::multimap<int, int>;
const Table &read(const Table &values, int &calls) { ++calls; return values; }
int main() {
  const Table left{{2, 10}, {2, 11}, {-3, 99}};
  const Table right{{2, 11}, {2, 10}, {-3, 99}};
  int first = 0;
  int second = 0;
  bool compare = read(left, first) < read(right, second);
  return compare && first == 1 && second == 1 && left != right && !(right <= left) ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-comparison-retains-equivalent-node-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-comparison-retains-equivalent-node-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapEmplaceIntDoesNotReplaceAnExistingValueRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-emplace-int-does-not-replace-an-existing-value.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <cstddef>
#include <cstdlib>
static unsigned allocations;
static unsigned releases;
void *operator new(std::size_t bytes) {
  ++allocations;
  return std::malloc(bytes);
}
void operator delete(void *storage) noexcept {
  if (storage) ++releases;
  std::free(storage);
}
int main() {
  {
    std::map<int, int> values;
    const int key = 3;
    auto first = values.emplace(key, 17);
    int mapped = 33;
    auto *receiver = &values;
    auto duplicate = receiver->emplace(3, mapped);
    if (!first.second || duplicate.second || first.first != duplicate.first ||
        duplicate.first->second != 17 || allocations != 1 || releases) return 1;
  }
  return allocations == 1 && releases == 1 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-emplace-int-does-not-replace-an-existing-value.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-emplace-int-does-not-replace-an-existing-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapEmplaceConvertingMappedArgumentAcceptsFunctionBody) {
  const auto Source = tmpFile("ordered-map-emplace-converting-mapped-argument-remains-unsupported.cpp");
  writeFile(Source, R"r19containers(#include <map>
void insert(std::map<int, int> &values) {
  short converted = 3;
  values.emplace(1, converted);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedComparisonCustomComparatorRemainsUnsupportedRejected) {
  const auto Source = tmpFile("ordered-comparison-custom-comparator-remains-unsupported.cpp");
  writeFile(Source, R"r19containers(#include <set>
struct Reverse {
  bool operator()(int left, int right) const { return left > right; }
};
bool equal(const std::set<int, Reverse> &a, const std::set<int, Reverse> &b) { return a == b; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMultisetRangeFromSetAndDuplicateIteratorsRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multiset-range-from-set-and-duplicate-iterators.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  const std::set<int> unique{2, 5, 9};
  std::multiset<int> output(unique.cbegin(), unique.cend());
  const std::multiset<int> input{5, 7, 5, 3};
  output.insert(input.begin(), input.end());
  output.insert(input.begin(), input.begin());
  return output.size() == 7 && *output.begin() == 2 &&
         output.count(5) == 3 && input.count(5) == 2 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-multiset-range-from-set-and-duplicate-iterators.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multiset-range-from-set-and-duplicate-iterators" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapIteratorRangeKeepsEquivalentOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-iterator-range-keeps-equivalent-order.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  const std::map<int, int> unique{{1, 7}, {3, 9}};
  std::multimap<int, int> output(unique.begin(), unique.end());
  std::multimap<int, int> incoming{{1, 8}, {2, 6}, {1, 4}};
  output.insert(incoming.begin(), incoming.end());
  auto range = output.equal_range(1);
  auto item = range.first;
  if (output.size() != 5 || item->second != 7)
    return 1;
  ++item;
  if (item->second != 8)
    return 2;
  ++item;
  if (item->second != 4)
    return 3;
  ++item;
  return item == range.second && incoming.count(1) == 2 ? 0 : 4;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-iterator-range-keeps-equivalent-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-iterator-range-keeps-equivalent-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultisetExplicitDefaultPoliciesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multiset-explicit-default-policies.cpp");
  writeFile(Source, R"r19containers(#include <set>
using Set = std::multiset<int>;

int main() {
  Set a{Set::key_compare{}};
  Set b{Set::allocator_type{}};
  Set c{Set::key_compare{}, Set::allocator_type{}};
  if (!a.empty() || !b.empty() || c.begin() != c.end())
    return 1;
  a.insert(3);
  a.insert(3);
  b.insert(1);
  c.insert(4);
  c.insert(2);
  return a.count(3) == 2 && *b.begin() == 1 && *c.begin() == 2 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-multiset-explicit-default-policies.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multiset-explicit-default-policies" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapExplicitDefaultPoliciesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-explicit-default-policies.cpp");
  writeFile(Source, R"r19containers(#include <map>
using Map = std::multimap<int, int>;

int main() {
  Map a{Map::key_compare{}};
  Map b{Map::allocator_type{}};
  Map c{Map::key_compare{}, Map::allocator_type{}};
  if (!a.empty() || !b.empty() || c.begin() != c.end())
    return 1;
  a.insert({{3, 4}, {3, 9}});
  b.insert({{2, 8}});
  c.insert({{5, 1}, {1, 7}});
  auto position = a.begin();
  if (position->second != 4)
    return 2;
  ++position;
  if (position->second != 9)
    return 2;
  return b.begin()->second == 8 && c.begin()->first == 1 && c.begin()->second == 7 ? 0 : 3;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-explicit-default-policies.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-explicit-default-policies" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultisetAllocatorObserverOnEmptyOwnerRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multiset-allocator-observer-on-empty-owner.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  using Set = std::multiset<int>;
  const Set values;
  auto allocator = values.get_allocator();
  return allocator == Set::allocator_type{} && values.empty() ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-multiset-allocator-observer-on-empty-owner.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multiset-allocator-observer-on-empty-owner" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapAllocatorObserverOnConstOwnerRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-allocator-observer-on-const-owner.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  using Map = std::multimap<int, int>;
  const Map values;
  return values.get_allocator() == Map::allocator_type{} && values.empty() ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-allocator-observer-on-const-owner.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-allocator-observer-on-const-owner" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedRangeReverseIteratorRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-range-reverse-iterator-guard.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  const std::set<int> input{1, 2, 3};
  std::set<int> result(input.rbegin(), input.rend());
  return result.size() == 3 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-range-reverse-iterator-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-range-reverse-iterator-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedAllocatorObserverCustomPolicyGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-allocator-observer-custom-policy-guard.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <functional>
int main() {
  const std::set<int, std::greater<int>> values{1, 2, 3};
  return values.get_allocator() == std::allocator<int>{} && *values.begin() == 3 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-allocator-observer-custom-policy-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-allocator-observer-custom-policy-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedExplicitStatefulPolicyGuardRejected) {
  const auto Source = tmpFile("ordered-explicit-stateful-policy-guard.cpp");
  writeFile(Source, R"r19containers(#include <set>
struct Compare {
  bool descending;
  bool operator()(int a, int b) const { return descending ? a > b : a < b; }
};

int main() {
  std::set<int, Compare> values(Compare{true});
  values.insert(1);
  values.insert(3);
  return *values.begin() == 3 ? 0 : 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMultisetExtractPositionAndMissingKeyRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multiset-extract-position-and-missing-key.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <utility>
int main() {
  std::multiset<int> values{4, 9, 4, 5};
  auto first = values.find(4);
  ++first;
  auto handle = values.extract(first);
  auto absent = values.extract(6);
  if (!handle || !absent.empty() || handle.value() != 4 || values.count(4) != 1)
    return 1;
  handle.swap(absent);
  return handle.empty() && absent.value() == 4 && values.size() == 3 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-multiset-extract-position-and-missing-key.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multiset-extract-position-and-missing-key" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapExtractKeepsEquivalentNodeOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-extract-keeps-equivalent-node-order.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  using Map = std::multimap<int, int>;
  Map values{{1, 3}, {4, 8}, {4, 9}, {4, 10}};
  auto node = values.extract(4);
  if (!node || node.key() != 4 || node.mapped() != 8)
    return 1;
  const Map &read_only = values;
  auto position = read_only.find(4);
  ++position;
  auto other = values.extract(position);
  return other.key() == 4 && other.mapped() == 10 &&
         other.get_allocator() == Map::allocator_type{} &&
         values.count(4) == 1 && values.find(4)->second == 9 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-extract-keeps-equivalent-node-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-extract-keeps-equivalent-node-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapNodeConflictReturnsOwnerWithMutableKeyRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-node-conflict-returns-owner-with-mutable-key.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <utility>
int main() {
  using Map = std::map<int, int>;
  Map source{{2, 21}}, destination{{2, 20}};
  auto node = source.extract(2);
  auto result = destination.insert(std::move(node));
  if (result.inserted || !node.empty() || result.node.empty() ||
      result.position->second != 20 || result.node.key() != 2 || result.node.mapped() != 21)
    return 1;
  result.node.key() = 4;
  auto inserted = destination.insert(std::move(result.node));
  return inserted.inserted && inserted.node.empty() && result.node.empty() &&
         inserted.position->first == 4 && destination[4] == 21 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-node-conflict-returns-owner-with-mutable-key.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-node-conflict-returns-owner-with-mutable-key" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapNodeInsertAppendsAnEquivalentNodeRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-node-insert-appends-an-equivalent-node.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <utility>
int main() {
  std::map<int, int> source{{2, 42}};
  std::multimap<int, int> destination{{2, 40}, {2, 41}};
  auto node = source.extract(2);
  auto position = destination.insert(std::move(node));
  auto first = destination.begin();
  if (!node.empty() || !source.empty() || first->second != 40)
    return 1;
  ++first;
  if (first->second != 41)
    return 2;
  ++first;
  return first == position && first->second == 42 && destination.count(2) == 3 ? 0 : 3;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-node-insert-appends-an-equivalent-node.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-node-insert-appends-an-equivalent-node" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedValueCompareIgnoresTheMappedValueRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-value-compare-ignores-the-mapped-value.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  using Map = std::map<int, int>;
  const Map values{{2, 1}};
  auto compare = values.value_comp();
  Map::value_type a{3, 900}, b{4, -900}, equal{3, -20};
  if (!compare(a, b) || compare(b, a) || compare(a, equal))
    return 1;
  return !compare(equal, a) && values.begin()->first == 2 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-value-compare-ignores-the-mapped-value.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-value-compare-ignores-the-mapped-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedValueCompareConstObjectUsesLiveInputsRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-value-compare-const-object-uses-live-inputs.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  using Map = std::multimap<int, int>;
  const Map values;
  const auto compare = values.value_comp();
  const Map::value_type low{-2, 47}, high{5, -47};
  return compare(low, high) && !compare(high, low) && !compare(low, low) ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-value-compare-const-object-uses-live-inputs.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-value-compare-const-object-uses-live-inputs" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapValueComparatorAcceptsLongMappedValues) {
  const auto Source = tmpFile("ordered-map-value-compare-other-mapped-type-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  using Map = std::map<int, long>;
  const Map values;
  return values.value_comp()(Map::value_type{1, 100L}, Map::value_type{2, -100L}) ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-value-compare-other-mapped-type-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-value-compare-other-mapped-type-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedExtractionCustomPolicyGuardRejected) {
  const auto Source = tmpFile("ordered-extraction-custom-policy-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
struct Compare {
  bool descending;
  bool operator()(int a, int b) const { return descending ? a > b : a < b; }
};
int main() {
  std::map<int, int, Compare> map({{1, 5}, {3, 7}}, Compare{true});
  auto node = map.extract(3);
  return node.key() == 3 && node.mapped() == 7 && map.size() == 1 ? 0 : 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapHintedNodeUsesConstCursorConversionRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-hinted-node-uses-const-cursor-conversion.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <utility>
int main() {
  std::map<int, int> source{{1, 11}, {4, 44}}, destination{{2, 22}, {5, 55}};
  auto first = source.extract(1);
  auto before = destination.find(2);
  auto one = destination.insert(before, std::move(first));
  if (one != destination.begin() || one->second != 11 || !first.empty())
    return 1;
  auto middle = source.extract(4);
  auto four = destination.insert(destination.find(5), std::move(middle));
  return four != destination.end() && four->first == 4 && four->second == 44 &&
         middle.empty() && source.empty() && destination.size() == 4 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-hinted-node-uses-const-cursor-conversion.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-hinted-node-uses-const-cursor-conversion" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapHintedFallbackPreservesEquivalentOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-hinted-fallback-preserves-equivalent-order.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <utility>
int main() {
  std::multimap<int, int> source{{2, 70}, {2, 80}};
  std::multimap<int, int> destination{{1, 10}, {2, 20}, {2, 21}, {5, 50}};
  auto left = source.extract(source.cbegin());
  auto low = destination.insert(destination.cbegin(), std::move(left));
  auto right = source.extract(source.cbegin());
  auto high = destination.insert(destination.cend(), std::move(right));
  auto cursor = destination.lower_bound(2);
  if (cursor != low || cursor->second != 70 || !left.empty() || !right.empty())
    return 1;
  if ((++cursor)->second != 20 || (++cursor)->second != 21)
    return 2;
  return ++cursor == high && cursor->second == 80 && destination.count(2) == 4 ? 0 : 3;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-hinted-fallback-preserves-equivalent-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-hinted-fallback-preserves-equivalent-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultisetHintedValueInsertsBeforeEquivalentCursorRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multiset-hinted-value-inserts-before-equivalent-cursor.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  std::multiset<int> values{1, 2, 2, 4};
  auto first = values.lower_bound(2);
  const int *saved = &*first;
  auto inserted = values.emplace_hint(first, 2);
  if (inserted == first || inserted != values.lower_bound(2) || &*first != saved)
    return 1;
  auto high = values.insert(values.cend(), 2);
  return ++high == values.find(4) && values.count(2) == 4 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-multiset-hinted-value-inserts-before-equivalent-cursor.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multiset-hinted-value-inserts-before-equivalent-cursor" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapHintedPairAndValuePreserveOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-hinted-pair-and-value-preserve-order.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  using Map = std::multimap<int, int>;
  Map values{{1, 10}, {2, 20}, {2, 21}, {4, 40}};
  Map::value_type written{2, 19};
  auto first = values.insert(values.cbegin(), written);
  auto last = values.emplace_hint(values.cend(), 2, 22);
  auto cursor = values.lower_bound(2);
  if (cursor != first || cursor->second != 19)
    return 1;
  if ((++cursor)->second != 20 || (++cursor)->second != 21)
    return 2;
  return ++cursor == last && cursor->second == 22 && values.count(2) == 4 ? 0 : 3;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-hinted-pair-and-value-preserve-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-hinted-pair-and-value-preserve-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultisetAllocatorMoveTransfersExistingAddressesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multiset-allocator-move-transfers-existing-addresses.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <utility>
int main() {
  using Set = std::multiset<int>;
  Set source{1, 2, 2, 9};
  const int *saved = &*source.begin();
  const Set::allocator_type allocator;
  Set moved(std::move(source), allocator);
  if (!source.empty() || moved.count(2) != 2 || &*moved.begin() != saved)
    return 1;
  source.insert(8);
  return source.size() == 1 && *source.begin() == 8 && moved.size() == 4 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-multiset-allocator-move-transfers-existing-addresses.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multiset-allocator-move-transfers-existing-addresses" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapAllocatorCopyAndMoveRetainEquivalentSequenceRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-allocator-copy-and-move-retain-equivalent-sequence.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <utility>
int main() {
  using Map = std::multimap<int, int>;
  const Map source{{2, 20}, {2, 21}, {5, 50}};
  const Map::allocator_type allocator;
  Map copy(source, allocator);
  if (copy != source || &*copy.begin() == &*source.begin())
    return 1;
  auto saved = &*copy.begin();
  Map moved(std::move(copy), allocator);
  auto first = moved.begin();
  if (!copy.empty() || &*first != saved || first->second != 20)
    return 2;
  return (++first)->second == 21 && moved.count(2) == 2 && source.size() == 3 ? 0 : 3;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-allocator-copy-and-move-retain-equivalent-sequence.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-allocator-copy-and-move-retain-equivalent-sequence" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetHintedInsertCustomPolicyGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-hinted-insert-custom-policy-guard.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <set>
int main() {
  std::set<int, std::greater<int>> values{2};
  auto inserted = values.insert(values.cbegin(), 3);
  return inserted == values.begin() && *inserted == 3 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-hinted-insert-custom-policy-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-hinted-insert-custom-policy-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapHintedEmplaceConvertingValueGuardRejected) {
  const auto Source = tmpFile("ordered-map-hinted-emplace-converting-value-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> values;
  short mapped = 14;
  auto inserted = values.emplace_hint(values.cend(), 1, mapped);
  return inserted->second == 14 ? 0 : 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMultisetConstReverseTraversalCountsEquivalentsRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multiset-const-reverse-traversal-counts-equivalents.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  const std::multiset<int> values{1, 3, 3, 7};
  auto it = values.crbegin();
  auto end = values.crend();
  int sum = 0;
  while (it != end) {
    sum = sum * 10 + *it;
    ++it;
  }
  const std::multiset<int> empty;
  return sum == 7331 && empty.rbegin() == empty.rend() ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-multiset-const-reverse-traversal-counts-equivalents.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multiset-const-reverse-traversal-counts-equivalents" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapReverseFactoryAndConstEndpointRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-reverse-factory-and-const-endpoint.cpp");
  writeFile(Source, R"r19containers(#include <iterator>
#include <map>
int main() {
  const std::multimap<int, int> values{{2, 20}, {2, 21}, {4, 40}};
  auto it = std::make_reverse_iterator(values.end());
  if (it != values.crbegin() || it->second != 40)
    return 1;
  ++it;
  if (it->second != 21)
    return 2;
  ++it;
  return it != values.crend() && it->second == 20 && it.base() != values.end() ? 0 : 3;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-reverse-factory-and-const-endpoint.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-reverse-factory-and-const-endpoint" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultisetReverseRangeInsertionKeepsEquivalentEntriesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multiset-reverse-range-insertion-keeps-equivalent-entries.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  const std::multiset<int> source{1, 2, 2, 3};
  std::multiset<int> values{2, 4};
  values.insert(source.crbegin(), source.crend());
  values.insert(source.crend(), source.crend());
  return values.size() == 6 && values.count(2) == 3 && values.count(3) == 1 &&
         *values.begin() == 1 && source.size() == 4 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-multiset-reverse-range-insertion-keeps-equivalent-entries.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multiset-reverse-range-insertion-keeps-equivalent-entries" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapReverseRangePresentsEquivalentsInReverseOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-reverse-range-presents-equivalents-in-reverse-order.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  const std::multimap<int, int> source{{2, 20}, {2, 21}, {4, 40}};
  std::multimap<int, int> values{{2, 19}};
  values.insert(source.crbegin(), source.crend());
  auto cursor = values.lower_bound(2);
  if (cursor->second != 19 || (++cursor)->second != 21 || (++cursor)->second != 20)
    return 1;
  return values.size() == 4 && values.count(2) == 3 && (++cursor)->second == 40 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-reverse-range-presents-equivalents-in-reverse-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-reverse-range-presents-equivalents-in-reverse-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapMixedReverseCursorsComparePositionsRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-mixed-reverse-cursors-compare-positions.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> values{{1, 10}, {2, 20}, {3, 30}};
  auto write = values.rbegin();
  auto read = values.crbegin();
  if (write != read || read != write)
    return 1;
  ++write;
  if (read == write || write == read)
    return 2;
  ++read;
  return read == write && read->second == 20 ? 0 : 3;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-mixed-reverse-cursors-compare-positions.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-mixed-reverse-cursors-compare-positions" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapCopiesConstReverseEndFromMutableRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-copies-const-reverse-end-from-mutable.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  using Map = std::map<int, int>;
  Map values;
  auto mutable_end = values.rend();
  const Map::const_reverse_iterator end(mutable_end);
  return end == values.crend() && end == values.rbegin() &&
         end.base() == values.cbegin() ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-copies-const-reverse-end-from-mutable.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-copies-const-reverse-end-from-mutable" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapConvertsReverseIteratorWithLongMappedValues) {
  const auto Source = tmpFile("ordered-mapped-reverse-conversion-other-mapped-storage-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  using Map = std::map<int, long>;
  Map values{{1, 10}, {5, 50}};
  auto it = values.rbegin();
  Map::const_reverse_iterator read = it;
  return read == it && read->second == 50 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-mapped-reverse-conversion-other-mapped-storage-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-mapped-reverse-conversion-other-mapped-storage-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMappedReverseConversionCustomOrderGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-mapped-reverse-conversion-custom-order-guard.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <map>
int main() {
  using Map = std::map<int, int, std::greater<int>>;
  Map values{{1, 10}, {5, 50}};
  Map::const_reverse_iterator it = values.rbegin();
  return it->first == 1 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-mapped-reverse-conversion-custom-order-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-mapped-reverse-conversion-custom-order-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapConstructsFromMutablePairPointersRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-constructs-from-mutable-pair-pointers.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::pair<int, int> input[]{{3, 30}, {1, 10}, {3, 31}};
  std::map<int, int> values(input, input + 3);
  input[0].second = 99;
  return values.size() == 2 && values.begin()->first == 1 &&
         values.find(3)->second == 30 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-constructs-from-mutable-pair-pointers.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-constructs-from-mutable-pair-pointers" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapConstructsFromConstPairPointersRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-constructs-from-const-pair-pointers.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  const std::pair<int, int> input[]{{4, 40}, {2, 20}, {2, 21}};
  std::multimap<int, int> values(input, input + 3);
  auto cursor = values.begin();
  if (cursor->first != 2 || cursor->second != 20 || (++cursor)->second != 21)
    return 1;
  return (++cursor)->first == 4 && values.size() == 3 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-constructs-from-const-pair-pointers.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-constructs-from-const-pair-pointers" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetEmplacementConvertsIntegerKeyBeforeLookupRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-emplacement-converts-integer-key-before-lookup.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  short signed_key = -3;
  unsigned char small_key = 9;
  std::set<int> values;
  auto left = values.emplace(signed_key);
  auto right = values.emplace(small_key);
  auto duplicate = values.emplace(static_cast<short>(small_key));
  return left.second && right.second && !duplicate.second &&
         duplicate.first == right.first && *left.first == -3 &&
         *right.first == 9 && values.size() == 2 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-emplacement-converts-integer-key-before-lookup.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-emplacement-converts-integer-key-before-lookup" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapPointerRangeDifferentMappedLeafGuardRejected) {
  const auto Source = tmpFile("ordered-map-pointer-range-different-mapped-leaf-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  const std::pair<int, long> values[]{{1, 7}};
  std::map<int, int> result(values, values + 1);
  return result.begin()->second == 7 ? 0 : 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapDefaultEmplacementInitializesBothFieldsRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-default-emplacement-initializes-both-fields.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> values;
  auto inserted = values.emplace();
  if (!inserted.second || inserted.first->first != 0 || inserted.first->second != 0)
    return 1;
  inserted.first->second = 17;
  auto duplicate = values.emplace();
  return !duplicate.second && duplicate.first == inserted.first &&
         duplicate.first->second == 17 && values.size() == 1 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-default-emplacement-initializes-both-fields.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-default-emplacement-initializes-both-fields" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapEmplacementOfExactPairsKeepsOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-emplacement-of-exact-pairs-keeps-order.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::multimap<int, int> values;
  const std::multimap<int, int>::value_type input{7, 70};
  auto first = values.emplace(input);
  auto second = values.emplace(std::make_pair(7, 71));
  auto cursor = values.begin();
  return values.size() == 2 && cursor == first && cursor->second == 70 &&
         ++cursor == second && cursor->second == 71 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-emplacement-of-exact-pairs-keeps-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-emplacement-of-exact-pairs-keeps-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapPiecewiseEmplacementReadsLiveReferenceElementsRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-piecewise-emplacement-reads-live-reference-elements.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <tuple>
int main() {
  int key = 7, value = 70;
  auto keys = std::forward_as_tuple(key);
  auto values = std::forward_as_tuple(value);
  value = 71;
  std::map<int, int> map;
  auto inserted = map.emplace(std::piecewise_construct, keys, values);
  value = 72;
  auto duplicate = map.emplace(std::piecewise_construct, keys, values);
  return inserted.second && !duplicate.second && duplicate.first == inserted.first &&
         inserted.first->first == 7 && inserted.first->second == 71 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-piecewise-emplacement-reads-live-reference-elements.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-piecewise-emplacement-reads-live-reference-elements" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapPiecewiseInsertsEquivalentValuesInOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-piecewise-inserts-equivalent-values-in-order.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <tuple>
int main() {
  std::multimap<int, int> map;
  auto i = map.emplace(std::piecewise_construct, std::tuple<int>{4}, std::tuple<int>{40});
  auto j = map.emplace(std::piecewise_construct, std::tuple<int>{4}, std::tuple<int>{41});
  return i->second == 40 && j->second == 41 && ++i == j && map.size() == 2 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-piecewise-inserts-equivalent-values-in-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-piecewise-inserts-equivalent-values-in-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedIteratorAdvanceWalksInBothDirectionsRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-iterator-advance-walks-in-both-directions.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <iterator>
int main() {
  std::set<int> values{1, 2, 4, 8};
  auto i = values.begin();
  std::advance(i, 2);
  if (*i != 4)
    return 1;
  std::advance(i, -1);
  std::advance(i, 0);
  return *i == 2 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-iterator-advance-walks-in-both-directions.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-iterator-advance-walks-in-both-directions" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedIteratorNextAndPrevCopyTheirInputRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-iterator-next-and-prev-copy-their-input.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <iterator>
int main() {
  const std::set<int> values{1, 2, 4, 8};
  auto first = values.begin();
  auto last = values.end();
  auto second = std::next(first);
  auto high = std::prev(last);
  auto middle = std::next(last, -2);
  return *first == 1 && last == values.end() && *second == 2 &&
         *high == 8 && *middle == 4 && *std::prev(second, -1) == 4 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-iterator-next-and-prev-copy-their-input.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-iterator-next-and-prev-copy-their-input" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetFreeBeginEndAndEmptyFollowOwnerRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-free-begin-end-and-empty-follow-owner.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <iterator>
int main() {
  std::set<int> values;
  if (!std::empty(values) || std::begin(values) != std::end(values))
    return 1;
  values.insert(2);
  values.insert(7);
  auto iterator = std::begin(values);
  auto finish = std::end(values);
  return !std::empty(values) && std::size(values) == 2 && *iterator++ == 2 &&
         *iterator++ == 7 && iterator == finish ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-free-begin-end-and-empty-follow-owner.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-free-begin-end-and-empty-follow-owner" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultisetFreeReverseAccessRetainsEquivalentValuesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multiset-free-reverse-access-retains-equivalent-values.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <iterator>
int main() {
  const std::multiset<int> values{2, 2, 4};
  auto r = std::rbegin(values);
  if (*r++ != 4 || *r++ != 2 || *r++ != 2)
    return 1;
  return r == std::rend(values) && std::size(values) == 3 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-multiset-free-reverse-access-retains-equivalent-values.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multiset-free-reverse-access-retains-equivalent-values" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedReverseAdvanceMutatesOnlyTheSelectedCursorRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-reverse-advance-mutates-only-the-selected-cursor.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <iterator>
int main() {
  std::set<int> values{1, 3, 5, 8};
  auto i = values.rbegin();
  const auto original = i;
  std::advance(i, 2);
  if (*i != 3 || *original != 8)
    return 1;
  std::advance(i, -1);
  std::advance(i, 0);
  return *i == 5 && *original == 8 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-reverse-advance-mutates-only-the-selected-cursor.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-reverse-advance-mutates-only-the-selected-cursor" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapReverseNextPrevPreserveSourceAndMappedReferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-reverse-next-prev-preserve-source-and-mapped-reference.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <iterator>
int main() {
  std::map<int, int> values{{1, 10}, {4, 40}, {7, 70}};
  auto end = values.rend();
  auto first = values.rbegin();
  auto middle = std::next(first);
  auto lowest = std::prev(end);
  middle->second = 42;
  return first->second == 70 && end == values.rend() && lowest->first == 1 &&
         std::prev(middle, -1) == lowest && std::next(lowest, -1)->second == 42 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-reverse-next-prev-preserve-source-and-mapped-reference.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-reverse-next-prev-preserve-source-and-mapped-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapHintedDefaultEmplacementPreservesDuplicateValueRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-hinted-default-emplacement-preserves-duplicate-value.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> values{{2, 20}, {9, 90}};
  auto first = values.emplace_hint(values.cbegin());
  if (first != values.begin() || first->first != 0 || first->second != 0)
    return 1;
  first->second = 17;
  auto duplicate = values.emplace_hint(values.cend());
  return duplicate == first && duplicate->second == 17 && values.size() == 3 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-hinted-default-emplacement-preserves-duplicate-value.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-hinted-default-emplacement-preserves-duplicate-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapHintedExactPairInsertsBeforeEquivalentHintRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-hinted-exact-pair-inserts-before-equivalent-hint.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::multimap<int, int> values{{2, 20}, {2, 21}, {5, 50}};
  const auto first = values.cbegin();
  std::multimap<int, int>::value_type pair{2, 19};
  auto inserted = values.emplace_hint(first, pair);
  auto next = inserted;
  return inserted == values.begin() && inserted->second == 19 &&
         ++next == first && next->second == 20 && values.size() == 4 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-hinted-exact-pair-inserts-before-equivalent-hint.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-hinted-exact-pair-inserts-before-equivalent-hint" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedWideMapSubscriptKeepsExistingValueAndZeroInitializesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-wide-map-subscript-keeps-existing-value-and-zero-initializes.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, long long> values{{1, 4398046511104LL}};
  long long &missing = values[2];
  if (missing != 0 || values[1] != 4398046511104LL)
    return 1;
  missing = -4398046511105LL;
  const auto &read = values;
  return read.find(2)->second == -4398046511105LL && values.size() == 2 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-wide-map-subscript-keeps-existing-value-and-zero-initializes.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-wide-map-subscript-keeps-existing-value-and-zero-initializes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedWideMapPointerRangeCopyAndNodeHandlePreserveLayoutRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-wide-map-pointer-range-copy-and-node-handle-preserve-layout.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::pair<int, long long> source[] = {{1, 4398046511104LL}, {4, 20LL}};
  std::map<int, long long> values(source, source + 2);
  auto copy = values;
  auto node = copy.extract(copy.begin());
  if (node.mapped() != 4398046511104LL || values.begin()->second != node.mapped())
    return 1;
  node.key() = 3;
  node.mapped() = -4398046511105LL;
  auto result = copy.insert(std::move(node));
  return result.inserted && result.position->first == 3 &&
         result.position->second == -4398046511105LL && values.size() == 2 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-wide-map-pointer-range-copy-and-node-handle-preserve-layout.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-wide-map-pointer-range-copy-and-node-handle-preserve-layout" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapAtReadsExistingConstMappedReferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-at-reads-existing-const-mapped-reference.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  const std::map<int, int> values{{3, 40}, {1, 20}};
  const int &result = values.at(1);
  return result == 20 && &result == &values.find(1)->second && values.size() == 2 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-at-reads-existing-const-mapped-reference.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-at-reads-existing-const-mapped-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapAtMappedReferenceRetainsWideTypeRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-at-mapped-reference-retains-wide-type.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  const std::map<int, long long> values{{-7, -4398046511105LL}, {8, 4398046511104LL}};
  const long long &result = values.at(-7);
  return result == -4398046511105LL && &result == &values.find(-7)->second ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-at-mapped-reference-retains-wide-type.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-at-mapped-reference-retains-wide-type" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapForwardDistanceWithLongMappedValues) {
  const auto Source = tmpFile("ordered-iterator-navigation-different-mapped-layout-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <iterator>
int main() {
  std::map<int, long> values{{2, 20}, {3, 30}};
  return std::distance(values.begin(), values.end()) == 2 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-iterator-navigation-different-mapped-layout-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-iterator-navigation-different-mapped-layout-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapReverseDistanceWithLongMappedValues) {
  const auto Source = tmpFile("ordered-reverse-navigation-different-mapped-layout-guard.cpp");
  writeFile(Source, R"r19containers(#include <iterator>
#include <map>
int main() {
  std::map<int, long> values{{1, 10}, {2, 20}};
  return std::distance(values.rbegin(), values.rend()) == 2 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-reverse-navigation-different-mapped-layout-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-reverse-navigation-different-mapped-layout-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapPairEmplacementCustomOrderGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-pair-emplacement-custom-order-guard.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <map>
int main() {
  std::map<int, int, std::greater<int>> values;
  return values.emplace(std::make_pair(2, 7)).second ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-pair-emplacement-custom-order-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-pair-emplacement-custom-order-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedIteratorNavigationCustomComparatorGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-iterator-navigation-custom-comparator-guard.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <functional>
#include <iterator>
int main() {
  std::set<int, std::greater<int>> values{2, 3};
  return *std::next(values.begin()) == 2 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-iterator-navigation-custom-comparator-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-iterator-navigation-custom-comparator-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedReverseNavigationCustomComparatorGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-reverse-navigation-custom-comparator-guard.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <functional>
#include <iterator>
int main() {
  std::set<int, std::greater<int>> values{1, 3};
  return *std::next(values.rbegin()) == 3 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-reverse-navigation-custom-comparator-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-reverse-navigation-custom-comparator-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapEmplacementPairWithDifferentMappedLeafGuardRejected) {
  const auto Source = tmpFile("ordered-map-emplacement-pair-with-different-mapped-leaf-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> values;
  return values.emplace(std::make_pair(2, 7L)).first->second == 7 ? 0 : 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapHintedPairDifferentMappedLeafGuardRejected) {
  const auto Source = tmpFile("ordered-map-hinted-pair-different-mapped-leaf-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> values;
  return values.emplace_hint(values.cend(), std::make_pair(2, 7L))->second == 7 ? 0 : 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapAtMissingStaticKeyRemainsThrowingRejected) {
  const auto Source = tmpFile("ordered-map-at-missing-static-key-remains-throwing.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  const std::map<int, int> values{{1, 2}};
  return values.at(3);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapLongLayoutFollowsTargetWidthRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-long-layout-follows-target-width.cpp");
  writeFile(Source, R"r19containers(#include <limits>
#include <map>
int main() {
  constexpr long maximum = std::numeric_limits<long>::max();
  constexpr long minimum = std::numeric_limits<long>::min();
  std::map<int, long> values;
  values.try_emplace(4, maximum);
  values.try_emplace(5, minimum);
  auto other = values;
  if (other[4] != maximum || other[5] != minimum || other[6] != 0L)
    return 1;
  auto node = other.extract(5);
  node.mapped() = maximum;
  if (!other.insert(std::move(node)).inserted)
    return 2;
  const auto copied = other.find(5);
  const auto original = values.find(5);
  return copied != other.end() && original != values.end() &&
         copied->second == maximum && original->second == minimum ? 0 : 3;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-long-layout-follows-target-width.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-long-layout-follows-target-width" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapUintNodeAndComparisonRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-uint-node-and-comparison.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, unsigned> left;
  left.try_emplace(4, 0xffffffffU);
  auto right = left;
  right.insert_or_assign(4, 0U);
  if (!(right < left) || right == left)
    return 1;
  auto node = left.extract(4);
  auto inserted = left.insert(std::move(node));
  return inserted.inserted && inserted.position->second == 0xffffffffU ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-uint-node-and-comparison.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-uint-node-and-comparison" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapUshortAndMapShortRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-ushort-and-map-short.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  const auto high = static_cast<unsigned short>(65535);
  std::multimap<int, unsigned short> values;
  values.emplace(4, high);
  values.emplace(4, static_cast<unsigned short>(23));
  auto copy = values;
  if (copy.count(4) != 2 || copy.begin()->second != high)
    return 1;
  copy.erase(copy.begin());
  if (copy.begin()->second != 23)
    return 2;
  std::map<int, short> negative;
  negative.try_emplace(1, static_cast<short>(-32768));
  return negative.find(1)->second < static_cast<short>(-1) ? 0 : 3;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-ushort-and-map-short.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-ushort-and-map-short" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ClimitsMatchesCxxIntegerLimitsRunAtBothOptimizations) {
  const auto Source = tmpFile("climits-matches-cxx-integer-limits.cpp");
  writeFile(Source, R"r19containers(#include <climits>
#include <limits>
static_assert(CHAR_BIT == std::numeric_limits<unsigned char>::digits);
static_assert(CHAR_MIN == std::numeric_limits<char>::min());
static_assert(CHAR_MAX == std::numeric_limits<char>::max());
static_assert(SCHAR_MIN == std::numeric_limits<signed char>::min());
static_assert(SCHAR_MAX == std::numeric_limits<signed char>::max());
static_assert(UCHAR_MAX == std::numeric_limits<unsigned char>::max());
static_assert(SHRT_MIN == std::numeric_limits<short>::min());
static_assert(SHRT_MAX == std::numeric_limits<short>::max());
static_assert(USHRT_MAX == std::numeric_limits<unsigned short>::max());
static_assert(INT_MIN == std::numeric_limits<int>::min());
static_assert(INT_MAX == std::numeric_limits<int>::max());
static_assert(UINT_MAX == std::numeric_limits<unsigned>::max());
int main() { return MB_LEN_MAX >= 1 ? 0 : 1; }
)r19containers");
  const auto Output = tmpFile("climits-matches-cxx-integer-limits.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("climits-matches-cxx-integer-limits" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapLongStoresClimitsExtremesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-long-stores-climits-extremes.cpp");
  writeFile(Source, R"r19containers(#include <climits>
#include <map>
int main() {
  std::map<int, long> values;
  values.try_emplace(4, LONG_MAX);
  values.try_emplace(5, LONG_MIN);
  auto copy = values;
  auto node = copy.extract(5);
  node.mapped() = LONG_MAX;
  auto place = copy.insert(std::move(node));
  return place.inserted && place.position->second == LONG_MAX &&
    values.find(5)->second == LONG_MIN ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-long-stores-climits-extremes.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-long-stores-climits-extremes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapAtFindTernaryOnConstReferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-at-find-ternary-on-const-reference.cpp");
  writeFile(Source, R"r19containers(#include <map>
int lookup(const std::map<int, int>& values, int key) {
  return values.find(key) != values.end() ? values.at(key) : -1;
}
int main() {
  const std::map<int, int> values{{2, 20}, {3, 31}};
  return lookup(values, 3) == 31 && lookup(values, 4) == -1 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-at-find-ternary-on-const-reference.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-at-find-ternary-on-const-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapAtIfPreservesWideMappedValueRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-at-if-preserves-wide-mapped-value.cpp");
  writeFile(Source, R"r19containers(#include <map>
long long lookup(int key) {
  const std::map<int, long long> values{{3, -4398046511105LL}};
  if (values.find(key) != values.end()) {
    return values.at(key);
  }
  return 0LL;
}
int main() { return lookup(3) == -4398046511105LL && lookup(4) == 0LL ? 0 : 1; }
)r19containers");
  const auto Output = tmpFile("ordered-map-at-if-preserves-wide-mapped-value.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-at-if-preserves-wide-mapped-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapFloatSubscriptCopyAndNodeRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-float-subscript-copy-and-node.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, float> values;
  values.try_emplace(1, 2.75f);
  auto copy = values;
  if (copy[2] != 0.0f || copy[1] != 2.75f)
    return 1;
  auto node = copy.extract(1);
  node.mapped() = -3.5f;
  auto placed = copy.insert(std::move(node));
  return placed.inserted && placed.position->second == -3.5f && values[1] == 2.75f ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-float-subscript-copy-and-node.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-float-subscript-copy-and-node" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapDoubleComparisonAdvancesPastUnorderedPairTypedInputRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-double-comparison-advances-past-unordered-pair-typed-input.cpp");
  writeFile(Source, R"r19containers(#include <limits>
#include <map>
int main() {
  const double quiet = std::numeric_limits<double>::quiet_NaN();
  const std::map<int, double> left{{1, quiet}, {2, -3.0}};
  const std::map<int, double> right{{1, 1.0}, {2, 4.0}};
  if (left == right || !(left < right) || right < left)
    return 1;
  auto copy = left;
  return copy == left ? 2 : 0;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-double-comparison-advances-past-unordered-pair-typed-input.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-double-comparison-advances-past-unordered-pair-typed-input" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapConstLongMappedStorageGuardRejected) {
  const auto Source = tmpFile("ordered-map-const-long-mapped-storage-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, const long> values;
  return values.try_emplace(1, 2L).second ? 0 : 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapQualifiedUintValueGuardRejected) {
  const auto Source = tmpFile("ordered-map-qualified-uint-value-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, const unsigned> values;
  return values.try_emplace(1, 5U).second ? 0 : 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ClimitsQuotedSourceIncludeIsRejectedRejected) {
  const auto Source = tmpFile("climits-quoted-source-include-is-rejected.cpp");
  writeFile(Source, R"r19containers(#include "climits"
int main() { return INT_MAX < 0; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapAtGuardDifferentKeyRejected) {
  const auto Source = tmpFile("ordered-map-at-guard-different-key.cpp");
  writeFile(Source, R"r19containers(#include <map>
int lookup(const std::map<int, int>& values, int key, int other) {
  return values.find(key) != values.end() ? values.at(other) : -1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapLongDoubleStorageGuardRejected) {
  const auto Source = tmpFile("ordered-map-long-double-storage-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, long double> values;
  return values.try_emplace(1, 1.5L).second ? 0 : 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2PairDeducedFromFloatingLiteralRunAtBothOptimizations) {
  const auto Source = tmpFile("pair-deduced-from-floating-literal.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  const auto mapped = 1.25;
  std::map<int, double> values{{1, mapped}};
  return values.begin()->second == 1.25 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("pair-deduced-from-floating-literal.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("pair-deduced-from-floating-literal" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PairExplicitTypeForNumericLimitsResultRunAtBothOptimizations) {
  const auto Source = tmpFile("pair-explicit-type-for-numeric-limits-result.cpp");
  writeFile(Source, R"r19containers(#include <limits>
#include <utility>
int main() {
  const double mapped = std::numeric_limits<double>::quiet_NaN();
  std::pair<const int, double> value{1, mapped};
  return value.second != value.second ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("pair-explicit-type-for-numeric-limits-result.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("pair-explicit-type-for-numeric-limits-result" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PairDeducedFromFloatNumericLimitsRunAtBothOptimizations) {
  const auto Source = tmpFile("pair-deduced-from-float-numeric-limits.cpp");
  writeFile(Source, R"r19containers(#include <limits>
#include <utility>
int main() {
  const auto mapped = std::numeric_limits<float>::lowest();
  std::pair<const int, float> value{1, mapped};
  return value.second == std::numeric_limits<float>::lowest() ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("pair-deduced-from-float-numeric-limits.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("pair-deduced-from-float-numeric-limits" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapAtAfterAbsentKeyReturnPreservesReferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-at-after-absent-key-return-preserves-reference.cpp");
  writeFile(Source, R"r19containers(#include <map>
int& lookup(std::map<int, int>& values, int key, int& fallback) {
  if (values.end() == values.find(key)) {
    return fallback;
  }
  return values.at(key);
}
int main() {
  std::map<int, int> values{{4, 2}};
  int fallback = -1;
  lookup(values, 4, fallback) = 87;
  lookup(values, 5, fallback) = 12;
  return values.find(4)->second == 87 && fallback == 12 && values.size() == 1 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-at-after-absent-key-return-preserves-reference.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-at-after-absent-key-return-preserves-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetGreaterBoundariesAndIterationRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-greater-boundaries-and-iteration.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  std::set<int, std::greater<int>> values{1, 3, 5};
  if (*values.begin() != 5 || *values.rbegin() != 1)
    return 1;
  if (*values.lower_bound(4) != 3 || *values.upper_bound(3) != 1)
    return 2;
  const auto both = values.equal_range(3);
  return *both.first == 3 && *both.second == 1 && values.find(2) == values.end() ? 0 : 3;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-greater-boundaries-and-iteration.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-greater-boundaries-and-iteration" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapGreaterCompareAndStorageRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-greater-compare-and-storage.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int, std::greater<int>> left;
  left.try_emplace(3, 31);
  left.try_emplace(1, 17);
  left[2] = 12;
  auto first = left.begin();
  auto second = first;
  ++second;
  if (!left.key_comp()(3, 2) || !left.value_comp()(*first, *second))
    return 1;
  std::map<int, int, std::greater<int>> right{{4, 41}, {0, 1}};
  return left < right && left[2] == 12 && left.begin()->first == 3 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-greater-compare-and-storage.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-greater-compare-and-storage" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMapLongLongPairRangeAndNodeRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-map-long-long-pair-range-and-node.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
#include <utility>
int main() {
  const std::pair<int, long long> input[]{{6, -4398046511123LL}, {5, 4398046511119LL}};
  std::unordered_map<int, long long> values;
  values.insert(input, input + 2);
  auto node = values.extract(5);
  node.mapped() += 61LL;
  auto result = values.insert(std::move(node));
  return result.inserted && result.position->second == 4398046511180LL &&
      values.find(6)->second == -4398046511123LL ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-map-long-long-pair-range-and-node.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-map-long-long-pair-range-and-node" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapTransparentGreaterExactSearchRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-transparent-greater-exact-search.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <map>
int main() {
  std::map<int, int, std::greater<>> values;
  values[1] = 21;
  values.try_emplace(4, 11);
  auto found = values.find(1);
  return values.begin()->first == 4 && found != values.end() && found->second == 21 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-transparent-greater-exact-search.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-transparent-greater-exact-search" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetTransparentWideFindAndBoundsRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-transparent-wide-find-and-bounds.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <set>
int main() {
  const std::set<int, std::less<>> values{-5, -1, 8};
  const long long above = 4294967295LL;
  const long long below = -4294967297LL;
  if (values.find(above) != values.end() || values.lower_bound(above) != values.end())
    return 1;
  if (values.upper_bound(below) != values.begin() || values.lower_bound(below) != values.begin())
    return 2;
  const long key = -1L;
  return values.find(key) != values.end() && *values.find(key) == -1 ? 0 : 3;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-transparent-wide-find-and-bounds.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-transparent-wide-find-and-bounds" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapTransparentSignedProbeCountRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-transparent-signed-probe-count.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <map>
int main() {
  const std::multimap<int, int, std::less<>> values{{-4, 3}, {7, 41}, {7, 53}, {11, 67}};
  const long query = 7;
  auto pair = values.equal_range(query);
  auto second = pair.first;
  ++second;
  return values.count(query) == 2 && values.count(4294967303LL) == 0 &&
      pair.first->second == 41 && second->second == 53 &&
      pair.second->first == 11 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-transparent-signed-probe-count.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-transparent-signed-probe-count" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapAtKeyChangesAfterGuardRejected) {
  const auto Source = tmpFile("ordered-map-at-key-changes-after-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
int lookup(const std::map<int, int>& values, int key) {
  if (values.find(key) == values.end()) return -1;
  ++key;
  return values.at(key);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapAtAfterGuardOfOtherContainerRejected) {
  const auto Source = tmpFile("ordered-map-at-after-guard-of-other-container.cpp");
  writeFile(Source, R"r19containers(#include <map>
int lookup(const std::map<int, int>& values, const std::map<int, int>& other, int key) {
  if (other.find(key) == other.end()) return -1;
  return values.at(key);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedSetCallerComparatorGuardRejected) {
  const auto Source = tmpFile("ordered-set-caller-comparator-guard.cpp");
  writeFile(Source, R"r19containers(#include <set>
struct Descending {
  int offset = 1;
  bool operator()(int a, int b) const { return (a ^ offset) > (b ^ offset); }
};
int main() {
  std::set<int, Descending> values{2, 3, 4};
  return *values.begin() != 4;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedSetMismatchedLessSpecializationGuardRejected) {
  const auto Source = tmpFile("ordered-set-mismatched-less-specialization-guard.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <set>
int main() {
  std::set<int, std::less<long>> values{4, 10};
  return values.count(4) != 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapTransparentVolatileProbeGuardRejected) {
  const auto Source = tmpFile("ordered-map-transparent-volatile-probe-guard.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <map>
int main() {
  const std::map<int, int, std::greater<>> values;
  volatile long query = 8L;
  return values.count(query) == 0 ? 0 : 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedMultimapDoubleMappedPermutationRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multimap-double-mapped-permutation.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
#include <utility>
int main() {
  const std::pair<int, double> input[]{{3, 1.5}, {3, 2.25}, {3, 1.5}, {4, -0.0}};
  const std::pair<int, double> output[]{{4, 0.0}, {3, 2.25}, {3, 1.5}, {3, 1.5}};
  std::unordered_multimap<int, double> first;
  first.insert(input, input + 4);
  std::unordered_multimap<int, double> second;
  second.insert(output, output + 4);
  if (!(first == second) || first.count(3) != 3)
    return 1;
  auto zeros = second.equal_range(4);
  zeros.first->second = 0.5;
  return first != second ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-multimap-double-mapped-permutation.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multimap-double-mapped-permutation" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMapShortAndBoolMappedStorageRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-map-short-and-bool-mapped-storage.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
#include <utility>
int main() {
  const std::pair<int, short> input[]{{-1, -2048}, {2, 4096}};
  std::unordered_map<int, short> numbers;
  numbers.insert(input, input + 2);
  std::unordered_map<int, bool> flags;
  flags.try_emplace(1, true);
  if (flags[2] || !flags[1])
    return 1;
  numbers[2] += 29;
  return numbers[-1] == -2048 && numbers[2] == 4125 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-map-short-and-bool-mapped-storage.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-map-short-and-bool-mapped-storage" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapReverseRangeBetweenStandardOrdersRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-reverse-range-between-standard-orders.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <map>
int main() {
  const std::map<int, long, std::greater<int>> high{{1, -1201340L}, {7, 98764L}};
  std::map<int, long> low(high.rbegin(), high.rend());
  auto first = low.begin();
  if (first == low.end() || first->first != 1 || first->second != -1201340L)
    return 1;
  ++first;
  return first != low.end() && first->first == 7 && first->second == 98764L ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-reverse-range-between-standard-orders.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-reverse-range-between-standard-orders" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultisetUnsignedFromSignedShortRangeRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multiset-unsigned-from-signed-short-range.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const short data[] = {-1, 6, -1, 6};
  std::unordered_multiset<unsigned> values(data, data + 4, 19);
  return values.size() == 4 && values.count(-1U) == 2 && values.count(6U) == 2 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-multiset-unsigned-from-signed-short-range.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multiset-unsigned-from-signed-short-range" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMapConstDoubleMappedPayloadGuardRejected) {
  const auto Source = tmpFile("unordered-map-const-double-mapped-payload-guard.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
int main() {
  std::unordered_map<int, const double> values;
  return values.try_emplace(4, 1.25).second ? 0 : 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetUncheckedFloatPointerRangeGuardRejected) {
  const auto Source = tmpFile("unordered-set-unchecked-float-pointer-range-guard.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const float values[] = {2.25f, -3.75f};
  std::unordered_set<int> set(values, values + 2);
  return set.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantDoubleRelationsQuietNanAndWidthRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-double-relations-quiet-nan-and-width.cpp");
  writeFile(Source, R"r19containers(#include <limits>
#include <variant>
using Value = std::variant<int, std::monostate, double>;

bool compare(const Value& nan, const Value& finite, const Value& adjacent,
             const Value& emptyValue) {
  return !(nan == finite) && nan != finite && !(nan < finite) &&
         !(nan <= finite) && !(nan > finite) && !(nan >= finite) &&
         finite < adjacent && adjacent > finite && finite != adjacent &&
         emptyValue < nan && nan > emptyValue;
}

int main() {
  const Value nan{std::numeric_limits<double>::quiet_NaN()};
  const Value finite{1.0}, adjacent{1.0000000000000002};
  const Value emptyValue{std::in_place_index<1>};
  return compare(nan, finite, adjacent, emptyValue) ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("variant-double-relations-quiet-nan-and-width.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-double-relations-quiet-nan-and-width" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantComparisonTemporaryOperandsEvaluateOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-comparison-temporary-operands-evaluate-once.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<int, long long>;

int next(int& calls, int value) {
  ++calls;
  return value;
}

int main() {
  int left = 0, right = 0;
  bool ordered = Value(next(left, -3)) < Value(next(right, 7));
  return ordered && left == 1 && right == 1 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("variant-comparison-temporary-operands-evaluate-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-comparison-temporary-operands-evaluate-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMonostateConvertingRvalueRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-monostate-converting-rvalue.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int main() {
  using Value = std::variant<int, std::monostate>;
  const Value value{std::monostate{}};
  return value.index() != 1 || !std::holds_alternative<std::monostate>(value) ||
         std::get_if<0>(&value) != nullptr || std::get_if<1>(&value) == nullptr;
}
)r19containers");
  const auto Output = tmpFile("variant-monostate-converting-rvalue.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-monostate-converting-rvalue" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMonostateInPlaceCopyByIndexAndTypeRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-monostate-in-place-copy-by-index-and-type.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int main() {
  using Value = std::variant<int, std::monostate>;
  const std::monostate source{};
  const Value left{std::in_place_index<1>, source};
  const Value right{std::in_place_type<std::monostate>, std::monostate{}};
  return left.index() != 1 || left != right;
}
)r19containers");
  const auto Output = tmpFile("variant-monostate-in-place-copy-by-index-and-type.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-monostate-in-place-copy-by-index-and-type" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantComparisonLongDoubleRemainsUnsupportedRejected) {
  const auto Source = tmpFile("variant-comparison-long-double-remains-unsupported.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<int, long double>;
bool equal(const Value& a, const Value& b) { return a == b; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantMonostateAssignmentSelectsAlternativeRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-monostate-assignment-selects-alternative.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int main() {
  using Value = std::variant<int, std::monostate>;
  Value value{45};
  Value& result = (value = std::monostate{});
  return &result != &value || value.index() != 1 || std::get_if<1>(&value) == nullptr;
}
)r19containers");
  const auto Output = tmpFile("variant-monostate-assignment-selects-alternative.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-monostate-assignment-selects-alternative" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMonostateAssignmentObeysOperandSequencingRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-monostate-assignment-obeys-operand-sequencing.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<int, std::monostate>;
Value& receiver(Value& value, int& trace) {
  trace = trace * 10 + 1;
  return value;
}
const std::monostate& operand(const std::monostate& value, int& trace) {
  trace = trace * 10 + 2;
  return value;
}
int main() {
  Value value{5};
  const std::monostate item{};
  int trace = 0;
  receiver(value, trace) = operand(item, trace);
  if (trace != 21 || value.index() != 1) return 1;
  trace = 0;
  receiver(value, trace).operator=(operand(item, trace));
  return trace != 12 || value.index() != 1;
}
)r19containers");
  const auto Output = tmpFile("variant-monostate-assignment-obeys-operand-sequencing.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-monostate-assignment-obeys-operand-sequencing" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantPointerEqualityDistinctObjectsAndNullRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-pointer-equality-distinct-objects-and-null.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<int, int*>;
int main() {
  int left = 3, right = 3;
  const Value a{&left}, alias{&left}, b{&right}, nil{static_cast<int*>(nullptr)}, scalar{0};
  return a != alias || !(a == alias) || b == a || nil == a || nil != nil || nil == scalar;
}
)r19containers");
  const auto Output = tmpFile("variant-pointer-equality-distinct-objects-and-null.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-pointer-equality-distinct-objects-and-null" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantPointerAndFloatEqualityPreservesPayloadTypesRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-pointer-and-float-equality-preserves-payload-types.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<float, const int*>;
bool equal(const Value& a, const Value& b) { return a == b && !(b != a); }
int main() {
  const int a = 8, b = 8;
  const Value first{&a}, alias{&a}, second{&b}, negative{-0.0f}, positive{0.0f};
  return !equal(first, alias) || equal(first, second) || !equal(negative, positive) || equal(first, positive);
}
)r19containers");
  const auto Output = tmpFile("variant-pointer-and-float-equality-preserves-payload-types.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-pointer-and-float-equality-preserves-payload-types" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantUncheckedPointerOrderingGuardRejected) {
  const auto Source = tmpFile("variant-unchecked-pointer-ordering-guard.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<int, int*>;
bool less(const Value& a, const Value& b) { return a < b; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantSwapDifferentTagsAndWidthsRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-swap-different-tags-and-widths.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int main() {
  using Value = std::variant<long long, double>;
  Value left{std::in_place_index<0>, -4294967301LL};
  Value right{std::in_place_index<1>, 4.25};
  std::swap(left, right);
  const auto* real = std::get_if<1>(&left);
  const auto* integer = std::get_if<0>(&right);
  return !real || !integer || *real != 4.25 || *integer != -4294967301LL;
}
)r19containers");
  const auto Output = tmpFile("variant-swap-different-tags-and-widths.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-swap-different-tags-and-widths" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantSwapPointerReceiverEvaluatesOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-swap-pointer-receiver-evaluates-once.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<int, unsigned>;
Value* receiver(Value* input, int& trace) {
  trace = trace * 10 + 1;
  return input;
}
Value& operand(Value& input, int& trace) {
  trace = trace * 10 + 2;
  return input;
}
int main() {
  Value left{3}, right{9U};
  int trace = 0;
  receiver(&left, trace)->swap(operand(right, trace));
  const auto* a = std::get_if<1>(&left);
  const auto* b = std::get_if<0>(&right);
  return trace != 12 || !a || !b || *a != 9U || *b != 3;
}
)r19containers");
  const auto Output = tmpFile("variant-swap-pointer-receiver-evaluates-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-swap-pointer-receiver-evaluates-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitFourReferenceArgumentsRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-four-reference-arguments.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Update {
  int calls;
  int operator()(int& a, int& b, int& c, int& d) {
    ++calls;
    d += a + b + c;
    return d;
  }
};
int main() {
  using Value = std::variant<int>;
  Value a{2}, b{3}, c{5}, d{7};
  Update change{0};
  int result = std::visit(change, a, b, c, d);
  auto* last = std::get_if<0>(&d);
  return change.calls != 1 || result != 17 || !last || *last != result;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-four-reference-arguments.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-four-reference-arguments" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyVoidPointerConstructionAndCopyRunAtBothOptimizations) {
  const auto Source = tmpFile("any-void-pointer-construction-and-copy.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  int value = 31;
  void* pointer = &value;
  std::any original{pointer};
  std::any copy{original};
  auto* first = std::any_cast<void*>(&original);
  auto* second = std::any_cast<void*>(&copy);
  if (!first || !second || first == second || *first != pointer || *second != pointer)
    return 1;
  *second = nullptr;
  return *first != pointer || std::any_cast<const void*>(&copy) != nullptr;
}
)r19containers");
  const auto Output = tmpFile("any-void-pointer-construction-and-copy.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-void-pointer-construction-and-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyNullptrPayloadAndNullVoidPointerRunAtBothOptimizations) {
  const auto Source = tmpFile("any-nullptr-payload-and-null-void-pointer.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  std::any value{static_cast<void*>(nullptr)};
  auto& item = value.emplace<decltype(nullptr)>();
  auto* null_value = std::any_cast<decltype(nullptr)>(&value);
  if (!value.has_value() || null_value != &item ||
      std::any_cast<void*>(&value) != nullptr) return 1;
  value.reset();
  return value.has_value() || std::any_cast<decltype(nullptr)>(&value) != nullptr;
}
)r19containers");
  const auto Output = tmpFile("any-nullptr-payload-and-null-void-pointer.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-nullptr-payload-and-null-void-pointer" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyIntPointerPayloadAcceptsFunctionBody) {
  const auto Source = tmpFile("any-unchecked-object-pointer-payload-guard.cpp");
  writeFile(Source, R"r19containers(#include <any>
void hold(int* input) { std::any value{input}; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantSwapStandaloneSdkAddressGuardRejected) {
  const auto Source = tmpFile("variant-swap-standalone-sdk-address-guard.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<int, unsigned>;
using Swap = void (*)(Value&, Value&) noexcept;
Swap get_swap() { return static_cast<Swap>(&std::swap); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyUncheckedFunctionPointerPayloadGuardRejected) {
  const auto Source = tmpFile("any-unchecked-function-pointer-payload-guard.cpp");
  writeFile(Source, R"r19containers(#include <any>
using Callback = void (*)();
void hold(Callback input) { std::any value{input}; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantSwapBuiltInPointerWithNullRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-swap-built-in-pointer-with-null.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int main() {
  const int count = 15;
  using Value = std::variant<int, const int*>;
  Value left{&count}, right{static_cast<const int*>(nullptr)};
  auto* first = std::get_if<1>(&left);
  auto* second = std::get_if<1>(&right);
  if (!first || !second) return 1;
  left.swap(right);
  return std::get_if<1>(&left) != first || std::get_if<1>(&right) != second ||
         *first != nullptr || *second != &count;
}
)r19containers");
  const auto Output = tmpFile("variant-swap-built-in-pointer-with-null.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-swap-built-in-pointer-with-null" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantSwapSelfWithDuplicateMonostateRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-swap-self-with-duplicate-monostate.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int main() {
  using Value = std::variant<std::monostate, std::monostate>;
  Value object{std::in_place_index<1>};
  auto* address = std::get_if<1>(&object);
  if (!address) return 1;
  object.swap(object);
  return object.index() != 1 || std::get_if<1>(&object) != address;
}
)r19containers");
  const auto Output = tmpFile("variant-swap-self-with-duplicate-monostate.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-swap-self-with-duplicate-monostate" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyConstIntAndBoolPointersSwapRunAtBothOptimizations) {
  const auto Source = tmpFile("any-const-int-and-bool-pointers-swap.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  const int number = 39;
  bool flag = false;
  const int* pointer = &number;
  std::any first{std::in_place_type<const int*>, pointer};
  std::any second{&flag};
  first.swap(second);
  auto* boolean = std::any_cast<bool*>(&first);
  auto* constant = std::any_cast<const int*>(&second);
  if (!boolean || !constant || *boolean != &flag || *constant != &number ||
      std::any_cast<int*>(&second) || std::any_cast<bool*>(&second))
    return 1;
  **boolean = true;
  return !flag;
}
)r19containers");
  const auto Output = tmpFile("any-const-int-and-bool-pointers-swap.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-const-int-and-bool-pointers-swap" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantSwapUncheckedAssociatedEnumGuardRejected) {
  const auto Source = tmpFile("variant-swap-unchecked-associated-enum-guard.cpp");
  writeFile(Source, R"r19containers(#include <variant>
namespace payload {
enum class Code { first, second };
void swap(Code& left, Code& right) noexcept { left = right; }
}
using Value = std::variant<int, payload::Code>;
void exchange(Value& left, Value& right) { left.swap(right); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyUncheckedVolatileIntPointerPayloadGuardRejected) {
  const auto Source = tmpFile("any-unchecked-volatile-int-pointer-payload-guard.cpp");
  writeFile(Source, R"r19containers(#include <any>
void hold(volatile int* input) { std::any value{input}; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantVisitTemporarySelectsOwnedVisitorCallRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-temporary-selects-owned-visitor-call.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Visitor {
  int bias;
  int operator()(int value) const { return value + bias; }
};
int main() {
  const Visitor visitor{17};
  using Value = std::variant<short, int>;
  return std::visit(visitor, Value{std::in_place_index<0>, static_cast<short>(4)}) != 21;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-temporary-selects-owned-visitor-call.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-temporary-selects-owned-visitor-call" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyActiveMutableReferenceWritesItsSlotRunAtBothOptimizations) {
  const auto Source = tmpFile("any-active-mutable-reference-writes-its-slot.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  std::any value{true};
  bool& active = std::any_cast<bool&>(value);
  bool* address = &active;
  active = false;
  return *address ? 1 : 0;
}
)r19containers");
  const auto Output = tmpFile("any-active-mutable-reference-writes-its-slot.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-active-mutable-reference-writes-its-slot" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyActiveWideReferenceIncrementsPayloadRunAtBothOptimizations) {
  const auto Source = tmpFile("any-active-wide-reference-increments-payload.cpp");
  writeFile(Source, R"r19containers(#include <any>
long long edit() {
  std::any value{5LL};
  return ++std::any_cast<long long&>(value);
}
int main() { return edit() != 6LL; }
)r19containers");
  const auto Output = tmpFile("any-active-wide-reference-increments-payload.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-active-wide-reference-increments-payload" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitAcceptsTheBoundReferenceCast) {
  const auto Source = tmpFile("variant-visit-nonowning-xvalue-operand-guard.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<int>;
int read(int value) { return value; }
int test(Value& input) { return std::visit(read, static_cast<Value&&>(input)); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyActiveMutableReferenceWrongTypeGuardRejected) {
  const auto Source = tmpFile("any-active-mutable-reference-wrong-type-guard.cpp");
  writeFile(Source, R"r19containers(#include <any>
int read() {
  std::any value{false};
  return std::any_cast<int&>(value);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantVisitMutableReferenceResultAliasesPayloadRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-mutable-reference-result-aliases-payload.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int& identity(int& value) { return value; }
int main() {
  std::variant<int> value{2};
  int& slot = std::visit(identity, value);
  auto* address = &slot;
  slot = 8;
  auto* active = std::get_if<0>(&value);
  return address != active || !active || *active != 8;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-mutable-reference-result-aliases-payload.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-mutable-reference-result-aliases-payload" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitRvalueReferenceResultPreservesIdentityRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-rvalue-reference-result-preserves-identity.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int&& select(int& value) { return static_cast<int&&>(value); }
int main() {
  std::variant<int> value{4};
  auto&& slot = std::visit(select, value);
  auto* active = std::get_if<0>(&value);
  slot = 12;
  return !active || &slot != active || *active != 12;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-rvalue-reference-result-preserves-identity.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-rvalue-reference-result-preserves-identity" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyRepeatedActiveCastsReferenceSameSlotRunAtBothOptimizations) {
  const auto Source = tmpFile("any-repeated-active-casts-reference-same-slot.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  std::any value{true};
  bool& slot = std::any_cast<bool&>(value);
  const bool& read = std::any_cast<const bool&>(value);
  slot = false;
  return &read != &slot || read;
}
)r19containers");
  const auto Output = tmpFile("any-repeated-active-casts-reference-same-slot.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-repeated-active-casts-reference-same-slot" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantTrivialAggregateDefaultInitializesMembersRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-trivial-aggregate-default-initializes-members.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Payload { int count; double amount; };
int main() {
  std::variant<Payload, int> value;
  auto* active = std::get_if<Payload>(&value);
  if (!active || active->count != 0 || active->amount != 0.0) return 1;
  active->count = 6;
  return !std::holds_alternative<Payload>(value) || active->count != 6;
}
)r19containers");
  const auto Output = tmpFile("variant-trivial-aggregate-default-initializes-members.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-trivial-aggregate-default-initializes-members" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantTrivialAggregateCopyPreservesActivePayloadRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-trivial-aggregate-copy-preserves-active-payload.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Payload { short code; int size; };
int main() {
  using Value = std::variant<int, Payload>;
  Payload payload{3, 16};
  Value original{std::in_place_index<1>, payload};
  const Value copy{original};
  auto* active = std::get_if<Payload>(&copy);
  return !active || active->code != 3 || active->size != 16;
}
)r19containers");
  const auto Output = tmpFile("variant-trivial-aggregate-copy-preserves-active-payload.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-trivial-aggregate-copy-preserves-active-payload" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitConstAggregateSelectsOverloadRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-const-aggregate-selects-overload.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Payload { int amount; };
struct Read {
  int operator()(const Payload& value) const { return value.amount; }
  int operator()(const int& value) const { return value; }
};
int main() {
  Payload initial{7};
  const std::variant<Payload, int> left{std::in_place_index<0>, initial}, right{4};
  const Read read{};
  return std::visit(read, left) != 7 || std::visit(read, right) != 4;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-const-aggregate-selects-overload.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-const-aggregate-selects-overload" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantAggregateEmplaceDefaultPayloadRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-aggregate-emplace-default-payload.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Payload { int count; double mark; };
int main() {
  std::variant<int, Payload> value{8};
  Payload& active = value.emplace<Payload>();
  if (active.count != 0 || active.mark != 0.0) return 1;
  active.count = 4;
  auto* checked = std::get_if<1>(&value);
  return !checked || &active != checked || checked->count != 4 || value.index() != 1;
}
)r19containers");
  const auto Output = tmpFile("variant-aggregate-emplace-default-payload.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-aggregate-emplace-default-payload" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitReferenceResultBorrowsTemporaryGuardRejected) {
  const auto Source = tmpFile("variant-visit-reference-result-borrows-temporary-guard.cpp");
  writeFile(Source, R"r19containers(#include <variant>
const int& identity(const int& value) { return value; }
const int& bad() {
  using Value = std::variant<int>;
  return std::visit(identity, static_cast<const Value&>(Value{5}));
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyRepeatedActiveCastsWithResetGuardRejected) {
  const auto Source = tmpFile("any-repeated-active-casts-with-reset-guard.cpp");
  writeFile(Source, R"r19containers(#include <any>
int read() {
  std::any value{3};
  int before = std::any_cast<int>(value);
  value.reset();
  return before + std::any_cast<int>(value);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyRepeatedActiveCastsWithWrongSiblingGuardRejected) {
  const auto Source = tmpFile("any-repeated-active-casts-with-wrong-sibling-guard.cpp");
  writeFile(Source, R"r19containers(#include <any>
int read() {
  std::any value{3};
  int first = std::any_cast<int>(value);
  return first + std::any_cast<bool>(value);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantNontrivialRecordInitializationGuardRejected) {
  const auto Source = tmpFile("variant-nontrivial-record-initialization-guard.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Payload {
  int code;
  Payload() noexcept : code(17) {}
};
int test() { std::variant<Payload, int> value; return int(value.index()); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantVisitAggregateImplicitCopyGuardRejected) {
  const auto Source = tmpFile("variant-visit-aggregate-implicit-copy-guard.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Payload { int count; };
int read(Payload value) { return value.count; }
int test(std::variant<Payload>& value) { return std::visit(read, value); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantEmplaceIntegerConversionReturnsSlotRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-emplace-integer-conversion-returns-slot.cpp");
  writeFile(Source, R"r19containers(#include <variant>
static int next(int& calls) { ++calls; return -12; }
int main() {
  int calls = 0;
  std::variant<int, long> value{3};
  long& slot = value.emplace<1>(next(calls));
  if (calls != 1 || slot != -12L || value.index() != 1) return 1;
  slot = -33L;
  const auto* checked = std::get_if<long>(&value);
  return !checked || checked != &slot || *checked != -33L;
}
)r19containers");
  const auto Output = tmpFile("variant-emplace-integer-conversion-returns-slot.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-emplace-integer-conversion-returns-slot" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyMovedLocalCastPreservesScalarRunAtBothOptimizations) {
  const auto Source = tmpFile("any-moved-local-cast-preserves-scalar.cpp");
  writeFile(Source, R"r19containers(#include <any>
#include <utility>
int main() {
  std::any local{23};
  int obtained = std::any_cast<int>(std::move(local));
  return obtained != 23 || std::any_cast<int>(local) != 23;
}
)r19containers");
  const auto Output = tmpFile("any-moved-local-cast-preserves-scalar.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-moved-local-cast-preserves-scalar" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyExplicitRvalueCastAliasesPointerPayloadRunAtBothOptimizations) {
  const auto Source = tmpFile("any-explicit-rvalue-cast-aliases-pointer-payload.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  int first = 5;
  int second = 8;
  std::any local{&first};
  int*&& pointer = std::any_cast<int*&&>(static_cast<std::any&&>(local));
  pointer = &second;
  return std::any_cast<int*>(local) != &second;
}
)r19containers");
  const auto Output = tmpFile("any-explicit-rvalue-cast-aliases-pointer-payload.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-explicit-rvalue-cast-aliases-pointer-payload" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnySizeofExpressionSkipsPayloadConstructionRunAtBothOptimizations) {
  const auto Source = tmpFile("any-sizeof-expression-skips-payload-construction.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  int calls = 0;
  const std::any owner;
  return sizeof(std::any{++calls}) != sizeof(owner) || sizeof(owner) != sizeof(std::any) || calls != 0;
}
)r19containers");
  const auto Output = tmpFile("any-sizeof-expression-skips-payload-construction.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-sizeof-expression-skips-payload-construction" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyFundamentalPointerKeepsConstBoolRunAtBothOptimizations) {
  const auto Source = tmpFile("any-fundamental-pointer-keeps-const-bool.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  const bool input = true;
  std::any local = std::make_any<const bool*>(&input);
  const auto* pointer = std::any_cast<const bool*>(&local);
  return !pointer || *pointer != &input || std::any_cast<bool*>(&local) != nullptr;
}
)r19containers");
  const auto Output = tmpFile("any-fundamental-pointer-keeps-const-bool.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-fundamental-pointer-keeps-const-bool" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyFundamentalPointerEmplaceDistinguishesLongRunAtBothOptimizations) {
  const auto Source = tmpFile("any-fundamental-pointer-emplace-distinguishes-long.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  long input = 12;
  std::any local;
  long*& placed = local.emplace<long*>(&input);
  long** pointer = std::any_cast<long*>(&local);
  return !pointer || pointer != &placed || *pointer != &input ||
         std::any_cast<long long*>(&local) != nullptr;
}
)r19containers");
  const auto Output = tmpFile("any-fundamental-pointer-emplace-distinguishes-long.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-fundamental-pointer-emplace-distinguishes-long" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyMovedLocalAfterResetGuardRejected) {
  const auto Source = tmpFile("any-moved-local-after-reset-guard.cpp");
  writeFile(Source, R"r19containers(#include <any>
#include <utility>
int read() {
  std::any local{23};
  local.reset();
  return std::any_cast<int>(std::move(local));
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyRvalueCastTemporaryReferenceGuardRejected) {
  const auto Source = tmpFile("any-rvalue-cast-temporary-reference-guard.cpp");
  writeFile(Source, R"r19containers(#include <any>
int&& read() {
  return std::any_cast<int&&>(std::any{23});
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedConversionSelectsOneImmutableReceiverRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-unsigned-conversion-selects-one-immutable-receiver.cpp");
  writeFile(Source, R"r19containers(#include <bitset>
static bool select(int& calls) { ++calls; return calls == 1; }
int main() {
  using Bits = std::bitset<129>;
  const Bits first(0x80000000ULL), second(7ULL);
  int calls = 0;
  unsigned long a = (select(calls) ? first : second).to_ulong();
  unsigned long b = (select(calls) ? first : second).to_ulong();
  return calls != 2 || a != 0x80000000UL || b != 7UL;
}
)r19containers");
  const auto Output = tmpFile("bitset-unsigned-conversion-selects-one-immutable-receiver.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-unsigned-conversion-selects-one-immutable-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitNonowningMoveConstOperandRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-nonowning-move-const-operand.cpp");
  writeFile(Source, R"r19containers(#include <utility>
#include <variant>
using Value = std::variant<int, short>;
int read(int value) { return value; }
int main() {
  const Value item(std::in_place_index<0>, 13);
  return std::visit(read, std::move(item)) != 13;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-nonowning-move-const-operand.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-nonowning-move-const-operand" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitUsesCheckedSourceXvalueReturn) {
  const auto Source = tmpFile("variant-visit-unverified-xvalue-returning-call.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<int>;
Value&& produce(Value& value) { return static_cast<Value&&>(value); }
int read(int value) { return value; }
int test(Value& item) { return std::visit(read, produce(item)); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedConversionCopiesSingleUseDefaultRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-unsigned-conversion-copies-single-use-default.cpp");
  writeFile(Source, R"r19containers(#include <bitset>
int main() {
  using Bits = std::bitset<129>;
  Bits input;
  const Bits copy = input;
  return copy.to_ullong() != 0;
}
)r19containers");
  const auto Output = tmpFile("bitset-unsigned-conversion-copies-single-use-default.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-unsigned-conversion-copies-single-use-default" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedConversionSelectsStaticConstantRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-unsigned-conversion-selects-static-constant.cpp");
  writeFile(Source, R"r19containers(#include <bitset>
bool select(int& calls) { return ++calls == 1; }
int main() {
  using Bits = std::bitset<129>;
  static const Bits first(6), second(7);
  int calls = 0;
  unsigned long long left = (select(calls) ? first : second).to_ullong();
  unsigned long long right = (select(calls) ? first : second).to_ullong();
  return calls != 2 || left != 6 || right != 7;
}
)r19containers");
  const auto Output = tmpFile("bitset-unsigned-conversion-selects-static-constant.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-unsigned-conversion-selects-static-constant" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedConversionCopyFromAliasedOwnerGuardRejected) {
  const auto Source = tmpFile("bitset-unsigned-conversion-copy-from-aliased-owner-guard.cpp");
  writeFile(Source, R"r19containers(#include <bitset>
int main() {
  using Bits = std::bitset<129>;
  Bits input(4);
  Bits* alias = &input;
  alias->set(90);
  const Bits copy = input;
  return copy.to_ullong() != 4;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedConversionMutableStaticBindingGuardRejected) {
  const auto Source = tmpFile("bitset-unsigned-conversion-mutable-static-binding-guard.cpp");
  writeFile(Source, R"r19containers(#include <bitset>
int main() {
  using Bits = std::bitset<129>;
  static Bits payload(1);
  payload.set(90);
  return payload.to_ullong() != 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedConversionConditionalConstMembersRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-unsigned-conversion-conditional-const-members.cpp");
  writeFile(Source, R"r19containers(#include <bitset>
struct First { const std::bitset<129> bits{5}; };
struct Second { const std::bitset<129> bits{9}; };
bool choose(int& reads) { ++reads; return reads == 1; }
int main() {
  First first;
  Second second;
  int reads = 0;
  const bool selected = choose(reads);
  return (selected ? first.bits : second.bits).to_ullong() != 5 || reads != 1;
}
)r19containers");
  const auto Output = tmpFile("bitset-unsigned-conversion-conditional-const-members.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-unsigned-conversion-conditional-const-members" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedConversionAliasedMemberOwnerGuardRejected) {
  const auto Source = tmpFile("bitset-unsigned-conversion-aliased-member-owner-guard.cpp");
  writeFile(Source, R"r19containers(#include <bitset>
struct State { const std::bitset<129> bits{2}; };
int main() {
  State state;
  const State* alias = &state;
  return alias == nullptr || state.bits.to_ullong() != 2;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantVisitMatrixUsesTemporaryVisitorRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-matrix-uses-temporary-visitor.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Difference {
  int bias;
  int operator()(int left, int right) const { return left - right + bias; }
};
int main() {
  using Value = std::variant<int, short>;
  Value left(13);
  const Value right(std::in_place_index<1>, short(5));
  return std::visit(Difference{2}, left, right) != 10;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-matrix-uses-temporary-visitor.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-matrix-uses-temporary-visitor" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitReferenceResultFromTemporaryVisitorGuardRejected) {
  const auto Source = tmpFile("variant-visit-reference-result-from-temporary-visitor-guard.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Read {
  int payload;
  const int& operator()(int) const { return payload; }
};
int f(std::variant<int, short>& value) {
  const int& result = std::visit(Read{4}, value);
  return result;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantVisitInstantiatesTwoTemplateArgumentsRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-instantiates-two-template-arguments.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Difference {
  template<class L, class R>
  int operator()(const L& left, const R& right) const {
    return int(left) - int(right) + int(sizeof(L)) - int(sizeof(R));
  }
};
int main() {
  using Item = std::variant<int, short>;
  Item left(18);
  const Item right(std::in_place_index<1>, short(6));
  Difference difference{};
  return std::visit(difference, left, right) != 12 + int(sizeof(int)) - int(sizeof(short));
}
)r19containers");
  const auto Output = tmpFile("variant-visit-instantiates-two-template-arguments.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-instantiates-two-template-arguments" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitCapturelessSelectedIntegralResultRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-captureless-selected-integral-result.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int main() {
  using Item = std::variant<int, short>;
  Item value(std::in_place_index<1>, short(4));
  return std::visit([](auto input) -> int { return int(input) * 3 + 1; }, value) != 13;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-captureless-selected-integral-result.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-captureless-selected-integral-result" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitCapturingLambdaGuardRejected) {
  const auto Source = tmpFile("variant-visit-capturing-lambda-guard.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int main() {
  std::variant<int, short> value(3);
  int offset = 2;
  return std::visit([offset](auto x) -> int { return int(x) + offset; }, value) != 5;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantVisitSourceCastPreservesLiveOwnerRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-source-cast-preserves-live-owner.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<int, short>;
Value&& forward_value(Value& value) { return static_cast<Value&&>(value); }
int read(int value) { return value; }
int main() {
  Value item(std::in_place_index<1>, short{7});
  int before = std::visit(read, forward_value(item));
  item.emplace<0>(11);
  return before != 7 || std::visit(read, item) != 11;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-source-cast-preserves-live-owner.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-source-cast-preserves-live-owner" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitSourceCastAndPinnedMoveRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-source-cast-and-pinned-move.cpp");
  writeFile(Source, R"r19containers(#include <utility>
#include <variant>
using Value = std::variant<int, short>;
Value&& forward_value(Value& value) { return static_cast<Value&&>((value)); }
int subtract(int left, int right) { return left - right; }
int main() {
  Value left(std::in_place_index<1>, short{17});
  Value right(5);
  return std::visit(subtract, forward_value(left), std::move(right)) != 12;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-source-cast-and-pinned-move.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-source-cast-and-pinned-move" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListMovedFromOwnerCanBeReusedRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-moved-from-owner-can-be-reused.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <utility>
int main() {
  std::forward_list<int> original{3, 5};
  std::forward_list<int> moved(std::move(original));
  original.push_front(9);
  moved.pop_front();
  return original.front() != 9 || moved.front() != 5;
}
)r19containers");
  const auto Output = tmpFile("forward-list-moved-from-owner-can-be-reused.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-moved-from-owner-can-be-reused" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListCopyConstructsIndependentNodes) {
  const auto Source = tmpFile("forward-list-copy-keeps-independent-owner-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> original{5, 7};
  std::forward_list<int> copied(original);
  return &copied.front() == &original.front();
}
)r19containers");
  const auto Output = tmpFile("forward-list-copy-keeps-independent-owner-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-copy-keeps-independent-owner-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongMovesOwners) {
  const auto Source = tmpFile("forward-list-move-other-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <utility>
int main() {
  std::forward_list<long> original{1L, 2L};
  std::forward_list<long> moved(std::move(original));
  return moved.front() != 1L;
}
)r19containers");
  const auto Output = tmpFile("forward-list-move-other-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-move-other-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitSourceCastOtherOwnerGuardRejected) {
  const auto Source = tmpFile("variant-visit-source-cast-other-owner-guard.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<int>;
Value other(9);
Value&& forward_value(Value&) { return static_cast<Value&&>(other); }
int read(int value) { return value; }
int test(Value& item) { return std::visit(read, forward_value(item)); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListMoveAssignmentEvaluationOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-move-assignment-evaluation-order.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <utility>

using List = std::forward_list<int>;

List &mark(List &owner, int &trace, int digit) {
  trace = trace * 10 + digit;
  return owner;
}

int main() {
  List from{4};
  List into{5};
  int trace = 0;
  mark(into, trace, 2) = std::move(mark(from, trace, 1));
  if (trace != 12 || !from.empty() || into.front() != 4)
    return 1;
  trace = 0;
  mark(from, trace, 2).operator=(std::move(mark(into, trace, 1)));
  return trace != 21 || !into.empty() || from.front() != 4;
}
)r19containers");
  const auto Output = tmpFile("forward-list-move-assignment-evaluation-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-move-assignment-evaluation-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListCopyReadsConstOwnerInOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-copy-reads-const-owner-in-order.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  const std::forward_list<int> source{3, 7, 11};
  std::forward_list<int> copy(source);
  auto cursor = copy.begin();
  if (*cursor != 3 || &*cursor == &source.front()) return 1;
  ++cursor;
  if (*cursor != 7) return 2;
  *cursor = 9;
  auto sourceCursor = source.begin();
  ++sourceCursor;
  if (*sourceCursor != 7) return 3;
  ++cursor;
  if (*cursor != 11) return 4;
  ++cursor;
  return cursor != copy.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-copy-reads-const-owner-in-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-copy-reads-const-owner-in-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListCopiesValuesByAssignment) {
  const auto Source = tmpFile("forward-list-copy-assignment-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> from{3};
  std::forward_list<int> into{8};
  into = from;
  return into.front() != 3;
}
)r19containers");
  const auto Output = tmpFile("forward-list-copy-assignment-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-copy-assignment-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListCopiesWithExplicitAllocator) {
  const auto Source = tmpFile("forward-list-copy-explicit-allocator-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <memory>
int main() {
  std::forward_list<int> source{2, 4};
  std::forward_list<int> copy(source, std::allocator<int>{});
  return copy.front() != 2;
}
)r19containers");
  const auto Output = tmpFile("forward-list-copy-explicit-allocator-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-copy-explicit-allocator-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongCopiesOwners) {
  const auto Source = tmpFile("forward-list-copy-other-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  const std::forward_list<long> source{7L};
  std::forward_list<long> copy(source);
  return copy.front() != 7L;
}
)r19containers");
  const auto Output = tmpFile("forward-list-copy-other-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-copy-other-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListCopyAssignmentReusesNodesAndAppendsRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-copy-assignment-reuses-nodes-and-appends.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  const std::forward_list<int> from{2, 5, 8, 11};
  std::forward_list<int> into{13, 17};
  int *first = &into.front();
  auto second = into.begin();
  ++second;
  int *next = &*second;
  auto &assigned = (into = from);
  if (&assigned != &into || &into.front() != first || into.front() != 2)
    return 1;
  if (&*second != next || *second != 5)
    return 2;
  auto cursor = second;
  ++cursor;
  if (*cursor != 8)
    return 3;
  ++cursor;
  if (*cursor != 11)
    return 4;
  ++cursor;
  if (cursor != into.end())
    return 5;
  *first = 7;
  return from.front() != 2;
}
)r19containers");
  const auto Output = tmpFile("forward-list-copy-assignment-reuses-nodes-and-appends.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-copy-assignment-reuses-nodes-and-appends" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListCopyAssignmentObeysCallOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-copy-assignment-obeys-call-order.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
using List = std::forward_list<int>;
List &mark(List &owner, int &trace, int digit) {
  trace = trace * 10 + digit;
  return owner;
}
int main() {
  List from{31};
  List into{37, 41};
  int trace = 0;
  mark(into, trace, 2) = mark(from, trace, 1);
  if (trace != 12 || into.front() != 31 || from.front() != 31)
    return 1;
  trace = 0;
  mark(from, trace, 2).operator=(mark(into, trace, 1));
  return trace != 21 || from.front() != 31 || into.front() != 31;
}
)r19containers");
  const auto Output = tmpFile("forward-list-copy-assignment-obeys-call-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-copy-assignment-obeys-call-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListReverseEmptyAndSingletonRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-reverse-empty-and-singleton.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> values;
  auto *owner = &values;
  owner->reverse();
  if (!values.empty())
    return 1;
  values.push_front(7);
  const int *original = &values.front();
  owner->reverse();
  auto second = values.begin();
  ++second;
  return &values.front() != original || values.front() != 7 || second != values.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-reverse-empty-and-singleton.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-reverse-empty-and-singleton" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListFreeSwapEvaluatesTheSelectedOwnersOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-free-swap-evaluates-the-selected-owners-once.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <utility>
using List = std::forward_list<int>;
List &select(List &list, int &hits) {
  ++hits;
  return list;
}
int main() {
  List first{11, 13};
  List second{17};
  const int *beforeFirst = &first.front();
  const int *beforeSecond = &second.front();
  int firstHits = 0;
  int secondHits = 0;
  std::swap(select(first, firstHits), select(second, secondHits));
  return firstHits != 1 || secondHits != 1 ||
         &first.front() != beforeSecond || &second.front() != beforeFirst;
}
)r19containers");
  const auto Output = tmpFile("forward-list-free-swap-evaluates-the-selected-owners-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-free-swap-evaluates-the-selected-owners-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongReversesNodes) {
  const auto Source = tmpFile("forward-list-reverse-other-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<long> values{3L, 5L};
  values.reverse();
  return values.front() != 5L;
}
)r19containers");
  const auto Output = tmpFile("forward-list-reverse-other-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-reverse-other-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListSpliceWholeInMiddleKeepsNodeIdentityRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-splice-whole-in-middle-keeps-node-identity.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> destination{2, 11};
  std::forward_list<int> source{5, 7};
  auto moved = source.begin();
  auto tail = destination.begin();
  ++tail;
  auto position = destination.begin();
  destination.splice_after(position, source);
  if (!source.empty() || destination.front() != 2)
    return 1;
  ++position;
  if (position != moved || *position != 5)
    return 2;
  ++position;
  if (*position != 7)
    return 3;
  ++position;
  if (position != tail || *position != 11)
    return 4;
  ++position;
  return position != destination.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-splice-whole-in-middle-keeps-node-identity.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-splice-whole-in-middle-keeps-node-identity" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListSpliceWholeEmptyEdgesRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-splice-whole-empty-edges.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> destination;
  std::forward_list<int> source;
  destination.splice_after(destination.cbefore_begin(), source);
  if (!destination.empty() || !source.empty())
    return 1;
  source.push_front(17);
  int *node = &source.front();
  destination.splice_after(destination.cbefore_begin(), source);
  if (&destination.front() != node || !source.empty())
    return 2;
  destination.splice_after(destination.begin(), source);
  return destination.front() != 17 || &destination.front() != node;
}
)r19containers");
  const auto Output = tmpFile("forward-list-splice-whole-empty-edges.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-splice-whole-empty-edges" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListSpliceRangeOverloadGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-splice-range-overload-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> source{5, 7};
  std::forward_list<int> destination{11};
  destination.splice_after(destination.before_begin(), source, source.before_begin(), source.end());
  return destination.front() != 5;
}
)r19containers");
  const auto Output = tmpFile("forward-list-splice-range-overload-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-splice-range-overload-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListEraseRangeReleasesAllAndReusesOwnerRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-erase-range-releases-all-and-reuses-owner.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> values{2, 3, 5};
  auto *receiver = &values;
  auto end = receiver->erase_after(values.cbefore_begin(), values.cend());
  if (end != values.end() || !values.empty())
    return 1;
  values.push_front(13);
  return values.empty() || values.front() != 13;
}
)r19containers");
  const auto Output = tmpFile("forward-list-erase-range-releases-all-and-reuses-owner.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-erase-range-releases-all-and-reuses-owner" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListEraseRangeAtHeadRetainsTheLiveTailRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-erase-range-at-head-retains-the-live-tail.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> values{2, 3, 5};
  auto last = values.begin();
  ++last;
  ++last;
  int *tail = &*last;
  auto result = values.erase_after(values.before_begin(), last);
  if (&values.front() != tail || &*result != tail || *result != 5)
    return 1;
  ++result;
  return result != values.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-erase-range-at-head-retains-the-live-tail.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-erase-range-at-head-retains-the-live-tail" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListSpliceRangeSameOwnerSuffixRetainsNodeAddressesRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-splice-range-same-owner-suffix-retains-node-addresses.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <utility>
int main() {
  std::forward_list<int> values{2, 3, 5, 7, 11};
  auto before = values.begin();
  auto first = before;
  ++first;
  auto last = first;
  ++last;
  ++last;
  auto tail = last;
  ++tail;
  auto *firstAddress = &*first;
  values.splice_after(before, values, before, last);
  values.splice_after(tail, std::move(values), before, last);
  auto remaining = before;
  ++remaining;
  if (remaining != last || *remaining != 7)
    return 1;
  ++tail;
  if (tail != first || &*tail != firstAddress || *tail != 3)
    return 2;
  ++tail;
  if (*tail != 5)
    return 3;
  ++tail;
  return tail != values.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-splice-range-same-owner-suffix-retains-node-addresses.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-splice-range-same-owner-suffix-retains-node-addresses" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListEmplaceFrontKeepsAliasedAndConstValuesRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-emplace-front-keeps-aliased-and-const-values.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> values{17};
  int *before = &values.front();
  int &copy = values.emplace_front(values.front());
  if (&copy == before || copy != 17 || *before != 17)
    return 1;
  copy = 19;
  const int value = 23;
  int &another = values.emplace_front(value);
  return another != 23 || value != 23 || copy != 19 || *before != 17;
}
)r19containers");
  const auto Output = tmpFile("forward-list-emplace-front-keeps-aliased-and-const-values.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-emplace-front-keeps-aliased-and-const-values" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongSplicesWholeRanges) {
  const auto Source = tmpFile("forward-list-splice-whole-other-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<long> source{5L};
  std::forward_list<long> destination;
  destination.splice_after(destination.before_begin(), source);
  return destination.front() != 5L;
}
)r19containers");
  const auto Output = tmpFile("forward-list-splice-whole-other-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-splice-whole-other-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongSplicesPartialRanges) {
  const auto Source = tmpFile("forward-list-splice-range-other-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<long> source{2L, 3L};
  std::forward_list<long> destination;
  destination.splice_after(destination.before_begin(), source, source.before_begin(), source.end());
  return destination.front() != 2L;
}
)r19containers");
  const auto Output = tmpFile("forward-list-splice-range-other-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-splice-range-other-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListEmplacesConvertedLong) {
  const auto Source = tmpFile("forward-list-emplace-converted-long-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> values;
  return values.emplace_front(4L) != 4;
}
)r19containers");
  const auto Output = tmpFile("forward-list-emplace-converted-long-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-emplace-converted-long-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListSpliceSingleSourceAllocatorGuardRejected) {
  const auto Source = tmpFile("forward-list-splice-single-source-allocator-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <memory>
template <class T> struct LocalAllocator : std::allocator<T> {
  using value_type = T;
  LocalAllocator() = default;
  template <class U> LocalAllocator(const LocalAllocator<U> &) {}
  template <class U> struct rebind { using other = LocalAllocator<U>; };
};
int main() {
  std::forward_list<int, LocalAllocator<int>> source{2, 3};
  std::forward_list<int, LocalAllocator<int>> destination;
  destination.splice_after(destination.before_begin(), source, source.before_begin());
  return destination.front() != 2;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListInitializerAssignmentReusesPrefixRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-initializer-assignment-reuses-prefix.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> into{13, 17};
  auto first = into.begin();
  auto second = first;
  ++second;
  int *first_address = &*first;
  int *second_address = &*second;
  auto &assigned = (into = {2, 5, 8, 11});
  if (&assigned != &into || &into.front() != first_address || into.front() != 2)
    return 1;
  if (&*second != second_address || *second != 5)
    return 2;
  auto cursor = second;
  if (*++cursor != 8 || *++cursor != 11 || ++cursor != into.end())
    return 3;
  into = {19};
  ++first;
  if (first != into.end() || &into.front() != first_address || into.front() != 19)
    return 4;
  into = {};
  return !into.empty();
}
)r19containers");
  const auto Output = tmpFile("forward-list-initializer-assignment-reuses-prefix.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-initializer-assignment-reuses-prefix" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListInitializerAssignmentObeysCallOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-initializer-assignment-obeys-call-order.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
using List = std::forward_list<int>;
List &select(List &owner, int &trace, int digit) {
  trace = trace * 10 + digit;
  return owner;
}
int value(int &trace, int digit) {
  trace = trace * 10 + digit;
  return digit;
}
int main() {
  List owner{3};
  int trace = 0;
  select(owner, trace, 2) = {value(trace, 1)};
  if (trace != 12 || owner.front() != 1)
    return 1;
  trace = 0;
  select(owner, trace, 2).operator=({value(trace, 1)});
  return trace != 21 || owner.front() != 1;
}
)r19containers");
  const auto Output = tmpFile("forward-list-initializer-assignment-obeys-call-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-initializer-assignment-obeys-call-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListAssignPointerRangeGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-assign-pointer-range-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> list;
  const int values[]{3, 5};
  list.assign(values, values + 2);
  return list.front() != 3;
}
)r19containers");
  const auto Output = tmpFile("forward-list-assign-pointer-range-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-assign-pointer-range-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListPointerAssignmentGrowsAndTruncatesRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-pointer-assignment-grows-and-truncates.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> list{19, 23};
  int *first = &list.front();
  int source[]{3, 5, 7, 11};
  list.assign(source + 1, source + 4);
  auto cursor = list.begin();
  if (&*cursor != first || *cursor != 5 || *++cursor != 7 || *++cursor != 11 || ++cursor != list.end())
    return 1;
  const int shorter[]{13, 17};
  list.assign(shorter, shorter + 1);
  cursor = list.begin();
  if (&*cursor != first || *cursor != 13 || ++cursor != list.end())
    return 2;
  list.assign(shorter + 1, shorter + 1);
  return !list.empty();
}
)r19containers");
  const auto Output = tmpFile("forward-list-pointer-assignment-grows-and-truncates.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-pointer-assignment-grows-and-truncates" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListInsertInitializerRangePreservesOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-insert-initializer-range-preserves-order.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <initializer_list>
int main() {
  std::forward_list<int> list{11};
  auto tail = list.begin();
  const std::initializer_list<int> values{3, 5, 7};
  auto last = list.insert_after(list.cbefore_begin(), values);
  auto cursor = list.begin();
  if (*cursor != 3 || *++cursor != 5 || ++cursor != last || *last != 7)
    return 1;
  if (++cursor != tail || *tail != 11 || list.insert_after(last, {}) != last)
    return 2;
  auto appended = list.insert_after(tail, {13, 17});
  return *appended != 17 || ++appended != list.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-insert-initializer-range-preserves-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-insert-initializer-range-preserves-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListResizeValueInitializesAndKeepsPrefixRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-resize-value-initializes-and-keeps-prefix.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> list{3, 5};
  int *first = &list.front();
  auto cursor = list.begin();
  ++cursor;
  int *second = &*cursor;
  list.resize(4);
  if (&list.front() != first || &*cursor != second || *cursor != 5)
    return 1;
  if (*++cursor != 0 || *++cursor != 0 || ++cursor != list.end())
    return 2;
  list.resize(2);
  cursor = list.begin();
  if (*cursor != 3 || *++cursor != 5 || ++cursor != list.end())
    return 3;
  list.resize(0);
  if (!list.empty())
    return 4;
  list.resize(1);
  cursor = list.begin();
  return list.front() != 0 || ++cursor != list.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-resize-value-initializes-and-keeps-prefix.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-resize-value-initializes-and-keeps-prefix" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListResizeCapturesReceiverBeforeCountRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-resize-captures-receiver-before-count.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
using List = std::forward_list<int>;
List *receiver(List &value, int &trace) {
  trace = trace * 10 + 1;
  return &value;
}
unsigned count(int &trace) {
  trace = trace * 10 + 2;
  return 2;
}
int main() {
  List list{7};
  int trace = 0;
  receiver(list, trace)->resize(count(trace));
  auto cursor = list.begin();
  return trace != 12 || *cursor != 7 || *++cursor != 0 || ++cursor != list.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-resize-captures-receiver-before-count.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-resize-captures-receiver-before-count" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListUniqueKeepsFirstInEachRunRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-unique-keeps-first-in-each-run.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> list{2, 2, 5, 2, 2, 7, 7};
  auto first = list.begin();
  auto second_run = first;
  ++second_run;
  ++second_run;
  auto third_run = second_run;
  ++third_run;
  auto last_run = third_run;
  ++last_run;
  ++last_run;
  list.unique();
  auto cursor = list.begin();
  if (cursor != first || *cursor != 2 || ++cursor != second_run || *cursor != 5)
    return 1;
  if (++cursor != third_run || *cursor != 2 || ++cursor != last_run || *cursor != 7)
    return 2;
  return ++cursor != list.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-unique-keeps-first-in-each-run.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-unique-keeps-first-in-each-run" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListAssignsFromCheckedIteratorRange) {
  const auto Source = tmpFile("forward-list-assign-other-input-iterator-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> source{3, 5};
  std::forward_list<int> list;
  list.assign(source.begin(), source.end());
  return list.front() != 3;
}
)r19containers");
  const auto Output = tmpFile("forward-list-assign-other-input-iterator-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-assign-other-input-iterator-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListRemovesWithFunctionPredicate) {
  const auto Source = tmpFile("forward-list-remove-predicate-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
bool matches(int value) { return value == 5; }
int main() {
  std::forward_list<int> list{5};
  list.remove_if(matches);
  return !list.empty();
}
)r19containers");
  const auto Output = tmpFile("forward-list-remove-predicate-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-remove-predicate-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongAssignsInitializerLists) {
  const auto Source = tmpFile("forward-list-initializer-assignment-other-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<long> list;
  list = {3L, 5L};
  return list.front() != 3L;
}
)r19containers");
  const auto Output = tmpFile("forward-list-initializer-assignment-other-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-initializer-assignment-other-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongResizesWithZeros) {
  const auto Source = tmpFile("forward-list-resize-default-other-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<long> list;
  list.resize(2);
  return list.front() != 0;
}
)r19containers");
  const auto Output = tmpFile("forward-list-resize-default-other-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-resize-default-other-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongRemovesDuplicateValues) {
  const auto Source = tmpFile("forward-list-filter-other-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<long> list{5L, 5L};
  list.unique();
  list.remove(5L);
  return !list.empty();
}
)r19containers");
  const auto Output = tmpFile("forward-list-filter-other-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-filter-other-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListInsertsConvertedPointerValues) {
  const auto Source = tmpFile("forward-list-insert-range-converted-pointer-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  long values[]{3, 5};
  std::forward_list<int> list;
  list.insert_after(list.before_begin(), values, values + 2);
  return list.front() != 3;
}
)r19containers");
  const auto Output = tmpFile("forward-list-insert-range-converted-pointer-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-insert-range-converted-pointer-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListDefaultMergeIsStableAcrossOwnersRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-default-merge-is-stable-across-owners.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> left{1, 3, 5, 9};
  std::forward_list<int> right{2, 3, 5, 7};
  auto left_three = left.begin();
  ++left_three;
  auto right_three = right.begin();
  ++right_three;
  auto left_five = left_three;
  auto right_five = right_three;
  ++left_five;
  ++right_five;
  left.merge(right);
  if (!right.empty())
    return 1;
  auto it = left.begin();
  if (*it != 1 || *++it != 2 || ++it != left_three || *it != 3)
    return 2;
  if (++it != right_three || *it != 3 || ++it != left_five || *it != 5)
    return 3;
  if (++it != right_five || *it != 5 || *++it != 7 || *++it != 9)
    return 4;
  return ++it != left.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-default-merge-is-stable-across-owners.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-default-merge-is-stable-across-owners" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListDefaultMergeEmptyAndTailEdgesRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-default-merge-empty-and-tail-edges.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> left;
  std::forward_list<int> right;
  left.merge(right);
  if (!left.empty() || !right.empty())
    return 1;
  left.push_front(13);
  auto original = left.begin();
  left.merge(right);
  if (left.begin() != original)
    return 2;
  right.push_front(7);
  right.push_front(3);
  auto transferred = right.begin();
  auto *owner = &left;
  owner->merge(right);
  auto cursor = left.begin();
  if (!right.empty() || cursor != transferred || *cursor != 3 || *++cursor != 7)
    return 3;
  return ++cursor != original || *cursor != 13 || ++cursor != left.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-default-merge-empty-and-tail-edges.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-default-merge-empty-and-tail-edges" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListDefaultSortOrdersSignedValuesRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-default-sort-orders-signed-values.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> list{21, 13, 0, -2, -7, -11, -13, -17, -19};
  const int expected[]{-19, -17, -13, -11, -7, -2, 0, 13, 21};
  auto *owner = &list;
  owner->sort();
  auto cursor = list.begin();
  for (int index = 0; index != 9; ++index, ++cursor)
    if (cursor == list.end() || *cursor != expected[index])
      return 1;
  return cursor != list.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-default-sort-orders-signed-values.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-default-sort-orders-signed-values" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListSortsWithFunctionComparison) {
  const auto Source = tmpFile("forward-list-sort-custom-function-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
bool greater(int left, int right) { return left > right; }
int main() {
  std::forward_list<int> list{1, 2};
  list.sort(greater);
  return list.front() != 2;
}
)r19containers");
  const auto Output = tmpFile("forward-list-sort-custom-function-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-sort-custom-function-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListMergesRvalueWithFunctionComparison) {
  const auto Source = tmpFile("forward-list-merge-rvalue-custom-comparator-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <utility>
bool greater(int a, int b) { return a > b; }
int main() {
  std::forward_list<int> left{3};
  std::forward_list<int> right{2};
  left.merge(std::move(right), greater);
  return left.front() != 3;
}
)r19containers");
  const auto Output = tmpFile("forward-list-merge-rvalue-custom-comparator-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-merge-rvalue-custom-comparator-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongSortsNodes) {
  const auto Source = tmpFile("forward-list-sort-other-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<long> list{2L, 1L};
  list.sort();
  return list.front() != 1L;
}
)r19containers");
  const auto Output = tmpFile("forward-list-sort-other-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-sort-other-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListCountConstructionValueInitializesRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-count-construction-value-initializes.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> list(4);
  auto cursor = list.begin();
  for (int index = 0; index != 4; ++index, ++cursor) {
    if (cursor == list.end() || *cursor != 0)
      return 1;
    *cursor = index + 1;
  }
  return cursor != list.end() || list.front() != 1;
}
)r19containers");
  const auto Output = tmpFile("forward-list-count-construction-value-initializes.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-count-construction-value-initializes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListCountConstructionWithDefaultAllocatorRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-count-construction-with-default-allocator.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <memory>
int main() {
  const std::allocator<int> allocator;
  std::forward_list<int> zeros(2, allocator);
  std::forward_list<int> values(3, 11, allocator);
  auto zero = zeros.begin();
  if (*zero != 0 || *++zero != 0 || ++zero != zeros.end())
    return 1;
  auto cursor = values.begin();
  if (*cursor != 11 || *++cursor != 11 || *++cursor != 11)
    return 2;
  return ++cursor != values.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-count-construction-with-default-allocator.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-count-construction-with-default-allocator" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListPointerRangeConstructionOwnsCopyRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-pointer-range-construction-owns-copy.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  int source[]{2, 3, 5, 7, 11};
  std::forward_list<int> list(source + 1, source + 4);
  if (list.empty() || &list.front() == source + 1)
    return 1;
  source[1] = 13;
  auto cursor = list.begin();
  if (*cursor != 3 || *++cursor != 5 || *++cursor != 7)
    return 2;
  return ++cursor != list.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-pointer-range-construction-owns-copy.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-pointer-range-construction-owns-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListPointerConstructionAcceptsEmptyRangeRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-pointer-construction-accepts-empty-range.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  const int *end = nullptr;
  std::forward_list<int> empty(end, end);
  if (!empty.empty())
    return 1;
  int source[]{3, 5};
  std::forward_list<int> one(source + 1, source + 2);
  auto cursor = one.begin();
  return *cursor != 5 || ++cursor != one.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-pointer-construction-accepts-empty-range.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-pointer-construction-accepts-empty-range" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongConstructsZeros) {
  const auto Source = tmpFile("forward-list-count-construction-other-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<long> list(3);
  return list.front() != 0L;
}
)r19containers");
  const auto Output = tmpFile("forward-list-count-construction-other-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-count-construction-other-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListConstructsConvertedPointerValues) {
  const auto Source = tmpFile("forward-list-pointer-construction-converting-elements-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  long source[]{3, 5};
  std::forward_list<int> list(source, source + 2);
  return list.front() != 3;
}
)r19containers");
  const auto Output = tmpFile("forward-list-pointer-construction-converting-elements-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-pointer-construction-converting-elements-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListEqualityDistinguishesPrefixAndEmptyRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-equality-distinguishes-prefix-and-empty.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> empty, another;
  std::forward_list<int> short_list{2};
  std::forward_list<int> long_list{2, 3};
  if (!(empty == another) || empty != another || empty == short_list || short_list == empty)
    return 1;
  return short_list == long_list || long_list == short_list ||
      !(short_list != long_list) || !(long_list != short_list);
}
)r19containers");
  const auto Output = tmpFile("forward-list-equality-distinguishes-prefix-and-empty.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-equality-distinguishes-prefix-and-empty" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListOrderedComparisonGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-ordered-comparison-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> left{3}, right{5};
  return !(left < right);
}
)r19containers");
  const auto Output = tmpFile("forward-list-ordered-comparison-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-ordered-comparison-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListOrderRecognizesPrefixesAndEmptyOwnersRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-order-recognizes-prefixes-and-empty-owners.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> empty, short_list{3, 5}, long_list{3, 5, -7};
  if (!(empty < short_list) || !(short_list < long_list) || !(long_list > short_list))
    return 1;
  return short_list < empty || long_list < short_list || empty < empty || !(empty <= empty);
}
)r19containers");
  const auto Output = tmpFile("forward-list-order-recognizes-prefixes-and-empty-owners.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-order-recognizes-prefixes-and-empty-owners" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListGetAllocatorReturnsDefaultAllocatorRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-get-allocator-returns-default-allocator.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <memory>
int main() {
  const std::forward_list<int> existing{3, 5};
  auto allocator = existing.get_allocator();
  const std::allocator<int> standard;
  std::forward_list<int> empty(allocator);
  return !(allocator == standard) || !empty.empty() || existing.front() != 3;
}
)r19containers");
  const auto Output = tmpFile("forward-list-get-allocator-returns-default-allocator.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-get-allocator-returns-default-allocator" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListObserversEvaluateReceiversOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-observers-evaluate-receivers-once.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <memory>
using List = std::forward_list<int>;
const List *observe(const List &list, int &count) {
  ++count;
  return &list;
}
int main() {
  List owner{3};
  int count = 0;
  if (observe(owner, count)->max_size() < 2 || count != 1)
    return 1;
  auto allocator = observe(owner, count)->get_allocator();
  const std::allocator<int> expected;
  return count != 2 || !(allocator == expected);
}
)r19containers");
  const auto Output = tmpFile("forward-list-observers-evaluate-receivers-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-observers-evaluate-receivers-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListAllocatorCopyOwnsSeparateNodesRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-allocator-copy-owns-separate-nodes.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <memory>
int main() {
  const std::forward_list<int> source{3, 5};
  const std::allocator<int> allocator;
  std::forward_list<int> copy(source, allocator);
  if (copy.empty() || &copy.front() == &source.front())
    return 1;
  copy.front() = 11;
  auto next = copy.begin();
  return source.front() != 3 || *++next != 5 || ++next != copy.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-allocator-copy-owns-separate-nodes.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-allocator-copy-owns-separate-nodes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListConstIteratorConstructionWithAllocatorRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-const-iterator-construction-with-allocator.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <memory>
int main() {
  const std::forward_list<int> from{2, 3, 5};
  const std::allocator<int> allocator;
  std::forward_list<int> into(from.cbegin(), from.cend(), allocator);
  auto cursor = into.begin();
  if (*cursor != 2 || *++cursor != 3 || *++cursor != 5)
    return 1;
  return ++cursor != into.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-const-iterator-construction-with-allocator.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-const-iterator-construction-with-allocator" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListIteratorAssignmentReusesNodesAndTruncatesRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-iterator-assignment-reuses-nodes-and-truncates.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  const std::forward_list<int> from{2, 3, 5, 7};
  std::forward_list<int> into{11, 13, 17};
  auto first = from.cbegin();
  ++first;
  auto end = first;
  ++end;
  ++end;
  auto kept = into.begin();
  ++kept;
  into.assign(first, end);
  auto cursor = into.begin();
  if (*cursor != 3 || *++cursor != 5 || cursor != kept || ++cursor != into.end())
    return 1;
  return *first != 3 || *end != 7;
}
)r19containers");
  const auto Output = tmpFile("forward-list-iterator-assignment-reuses-nodes-and-truncates.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-iterator-assignment-reuses-nodes-and-truncates" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongComparesEquality) {
  const auto Source = tmpFile("forward-list-equality-other-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<long> left{3}, right{3};
  return left != right;
}
)r19containers");
  const auto Output = tmpFile("forward-list-equality-other-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-equality-other-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongReturnsAllocators) {
  const auto Source = tmpFile("forward-list-get-allocator-other-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  const std::forward_list<long> list;
  (void)list.get_allocator();
  return 0;
}
)r19containers");
  const auto Output = tmpFile("forward-list-get-allocator-other-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-get-allocator-other-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongCopiesWithAllocators) {
  const auto Source = tmpFile("forward-list-allocator-copy-other-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <memory>
int main() {
  const std::forward_list<long> source{3};
  std::forward_list<long> copy(source, std::allocator<long>{});
  return copy.front() != 3;
}
)r19containers");
  const auto Output = tmpFile("forward-list-allocator-copy-other-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-allocator-copy-other-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListIndirectOwnerComparisonGuardRejected) {
  const auto Source = tmpFile("forward-list-indirect-owner-comparison-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <memory>
int main() {
  using List = std::forward_list<int>;
  List left{3}, right{3};
  auto equal = static_cast<bool (*)(const List &, const List &)>(&std::operator==<int, std::allocator<int>>);
  return !equal(left, right);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListIndirectMaxSizeGuardRejected) {
  const auto Source = tmpFile("forward-list-indirect-max-size-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  using List = std::forward_list<int>;
  const List list;
  auto maximum = &List::max_size;
  return (list.*maximum)() < 2;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListIteratorConstructionMoveWrapperGuardRejected) {
  const auto Source = tmpFile("forward-list-iterator-construction-move-wrapper-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <iterator>
int main() {
  std::forward_list<int> from{3, 5};
  std::forward_list<int> into(std::make_move_iterator(from.begin()), std::make_move_iterator(from.end()));
  return into.front() != 3;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListIteratorInsertionMoveCursorGuardRejected) {
  const auto Source = tmpFile("forward-list-iterator-insertion-move-cursor-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <iterator>
int main() {
  std::forward_list<int> from{3, 5}, into;
  into.insert_after(into.cbefore_begin(), std::make_move_iterator(from.begin()), std::make_move_iterator(from.end()));
  return into.front() != 3;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListRemovePredicateEvaluatesOnceInOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-remove-predicate-evaluates-once-in-order.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int trace;
int calls;
bool even(int value) {
  trace = trace * 10 + value;
  ++calls;
  return value % 2 == 0;
}
using Predicate = bool (*)(int);
int selections;
Predicate select() {
  ++selections;
  return even;
}
int main() {
  std::forward_list<int> values{2, 4, 3, 6, 5};
  auto it = values.begin();
  ++it;
  ++it;
  auto last = it;
  ++last;
  ++last;
  values.remove_if(select());
  if (selections != 1 || calls != 5 || trace != 24365)
    return 1;
  auto cursor = values.begin();
  return cursor != it || *cursor != 3 || ++cursor != last || *cursor != 5 || ++cursor != values.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-remove-predicate-evaluates-once-in-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-remove-predicate-evaluates-once-in-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListSortObservableFunctionGuardRejected) {
  const auto Source = tmpFile("forward-list-sort-observable-function-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int count;
bool compare(int a, int b) { ++count; return a < b; }
int main() {
  std::forward_list<int> owner{3, 1, 7};
  owner.sort(compare);
  return owner.front() != 1 || count == 0;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListUniquePredicateOtherFunctionSignatureGuardRejected) {
  const auto Source = tmpFile("forward-list-unique-predicate-other-function-signature-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
bool equal(long first, long next) { return first == next; }
int main() {
  std::forward_list<int> values{3, 3};
  values.unique(equal);
  return values.front() != 3;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListLongKeepsFullWidthThroughIteratorsAndCopyRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-long-keeps-full-width-through-iterators-and-copy.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <memory>
int main() {
  const long a = sizeof(long) == 8 ? static_cast<long>(4294967301LL) : 1879048197L;
  const long b = sizeof(long) == 8 ? static_cast<long>(-4294967303LL) : -1879048199L;
  const std::allocator<long> allocator;
  std::forward_list<long> source(allocator);
  source.push_front(b);
  source.push_front(a);
  auto tail = source.begin();
  ++tail;
  std::forward_list<long> copied(source);
  if (&copied.front() == &source.front() || copied.front() != a || *tail != b)
    return 1;
  const auto &values = copied;
  auto it = values.cbegin();
  if (*it != a || *++it != b || ++it != values.cend())
    return 2;
  copied.pop_front();
  if (copied.front() != b || source.front() != a || *tail != b)
    return 3;
  copied.clear();
  return !copied.empty() || source.front() != a;
}
)r19containers");
  const auto Output = tmpFile("forward-list-long-keeps-full-width-through-iterators-and-copy.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-long-keeps-full-width-through-iterators-and-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongSortMergeRemoveAndCompareRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-long-sort-merge-remove-and-compare.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  const long low = sizeof(long) == 8 ? static_cast<long>(-4294967287LL) : -17L;
  const long high = sizeof(long) == 8 ? static_cast<long>(4294967305LL) : 19L;
  std::forward_list<long> left{high, low, 9L};
  std::forward_list<long> right{low, high};
  auto high_left = &left.front();
  auto low_right = &right.front();
  left.sort();
  left.merge(right);
  if (!right.empty() || left.front() != low || &left.front() == low_right)
    return 1;
  left.unique();
  auto high_node = left.begin();
  ++high_node;
  ++high_node;
  if (*high_node != high || &*high_node != high_left)
    return 2;
  left.remove(9L);
  const std::forward_list<long> expected{low, high};
  const std::forward_list<long> greater{high};
  return left != expected || !(left < greater);
}
)r19containers");
  const auto Output = tmpFile("forward-list-long-sort-merge-remove-and-compare.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-long-sort-merge-remove-and-compare" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListConstructsUnsignedElements) {
  const auto Source = tmpFile("forward-list-unsigned-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<unsigned> values{3u, 5u};
  return values.front() != 3u;
}
)r19containers");
  const auto Output = tmpFile("forward-list-unsigned-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-unsigned-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongNarrowingOrderFunctionGuardRejected) {
  const auto Source = tmpFile("forward-list-long-narrowing-order-function-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
bool less(int left, int right) { return left < right; }
int main() {
  std::forward_list<long> result{2L, 1L};
  result.sort(&less);
  return result.front() != 1L;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListLongTypedDescendingMergeRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-long-typed-descending-merge.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <utility>
bool descending(long left, long right) { return right < left; }
int main() {
  const long high = sizeof(long) == 8 ? static_cast<long>(4294967301LL) : 21L;
  std::forward_list<long> left{high, 5L};
  std::forward_list<long> right{high, -3L};
  auto left_node = left.begin();
  auto right_node = right.begin();
  left.merge(std::move(right), &descending);
  auto item = left.begin();
  if (!right.empty() || item != left_node || *item++ != high)
    return 1;
  if (item != right_node || *item++ != high || *item++ != 5L)
    return 2;
  return *item++ != -3L || item != left.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-long-typed-descending-merge.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-long-typed-descending-merge" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongTypedAdjacentPredicateRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-long-typed-adjacent-predicate.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
bool parity(long left, long right) { return (left & 1L) == (right & 1L); }
int main() {
  const long high = sizeof(long) == 8 ? static_cast<long>(4294967301LL) : 21L;
  std::forward_list<long> list{high, 3L, 2L, 4L, 5L, 7L};
  list.unique(&parity);
  auto item = list.begin();
  return *item++ != high || *item++ != 2L || *item++ != 5L || item != list.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-long-typed-adjacent-predicate.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-long-typed-adjacent-predicate" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongNarrowingFilterFunctionGuardRejected) {
  const auto Source = tmpFile("forward-list-long-narrowing-filter-function-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
bool remove(int value) { return value == 1; }
int main() {
  std::forward_list<long> list{1L, 2L};
  list.remove_if(&remove);
  return list.front() != 2L;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListAssignConvertingRangeReusesAndExtendsNodesRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-assign-converting-range-reuses-and-extends-nodes.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  const short source[] = {-31, 36, 73};
  std::forward_list<int> list{9, 6};
  auto first = list.begin();
  auto second = first;
  ++second;
  list.assign(source, source + 3);
  auto item = list.begin();
  if (item != first || *item++ != -31 || item != second || *item++ != 36)
    return 1;
  return *item++ != 73 || item != list.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-assign-converting-range-reuses-and-extends-nodes.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-assign-converting-range-reuses-and-extends-nodes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListConvertingVolatilePointerRangeGuardRejected) {
  const auto Source = tmpFile("forward-list-converting-volatile-pointer-range-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  volatile short source[] = {1, 2};
  std::forward_list<long> list(source, source + 2);
  return list.front() != 1L;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListEmplaceAfterConvertsConstShortRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-emplace-after-converts-const-short.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  const short value = -320;
  std::forward_list<int> list{5, 6};
  auto selected = list.emplace_after(list.cbegin(), value);
  auto item = list.begin();
  if (*item++ != 5 || item != selected || *item++ != -320)
    return 1;
  return *item++ != 6 || item != list.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-emplace-after-converts-const-short.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-emplace-after-converts-const-short" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListEmplaceFrontConvertsMutableLongRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-emplace-front-converts-mutable-long.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  long value = -45L;
  std::forward_list<int> list;
  int &result = list.emplace_front(value);
  value = 19L;
  return &result != &list.front() || result != -45 || value != 19L;
}
)r19containers");
  const auto Output = tmpFile("forward-list-emplace-front-converts-mutable-long.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-emplace-front-converts-mutable-long" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListEmptyOrderObjectEvaluatedOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-empty-order-object-evaluated-once.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Greater { bool operator()(int left, int right) const { return right < left; } };
int calls = 0;
Greater get_order() { ++calls; return {}; }
int main() {
  std::forward_list<int> values{1, 8, 8, -3};
  auto first_eight = values.begin();
  ++first_eight;
  values.sort(get_order());
  auto item = values.begin();
  if (calls != 1 || item != first_eight || *item++ != 8 || *item++ != 8)
    return 1;
  return *item++ != 1 || *item++ != -3 || item != values.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-empty-order-object-evaluated-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-empty-order-object-evaluated-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListOrderObjectWithStateGuardRejected) {
  const auto Source = tmpFile("forward-list-order-object-with-state-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Order {
  bool descending;
  bool operator()(int a, int b) const { return descending ? a > b : a < b; }
};
int main() {
  std::forward_list<int> values{1, 2};
  values.sort(Order{true});
  return values.front() != 2;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListUnsignedSortAndOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-unsigned-sort-and-order.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  constexpr unsigned max = static_cast<unsigned>(-1);
  std::forward_list<unsigned> values{max, 1u, 3u, max};
  auto first_max = values.begin();
  values.sort();
  auto item = values.begin();
  const std::forward_list<unsigned> expected{1u, 3u, max, max};
  if (values != expected || *item++ != 1u || *item++ != 3u || item != first_max)
    return 1;
  if (*item++ != max || *item++ != max || item != values.end())
    return 2;
  const std::forward_list<unsigned> smaller{1u, 3u, max - 1u, max};
  return !(smaller < values) || values < smaller;
}
)r19containers");
  const auto Output = tmpFile("forward-list-unsigned-sort-and-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-unsigned-sort-and-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListUnsignedRangeAndEmplacementRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-unsigned-range-and-emplacement.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  const short original[] = {7, -1, -2, 13};
  std::forward_list<unsigned long> values(original + 1, original + 3);
  auto item = values.cbegin();
  if (*item++ != static_cast<unsigned long>(-1) ||
      *item++ != static_cast<unsigned long>(-2) || item != values.cend())
    return 1;
  const unsigned short inputs[] = {9, static_cast<unsigned short>(-1), 51, 33};
  values.assign(inputs + 1, inputs + 3);
  auto pos = values.emplace_after(values.cbegin(), inputs[3]);
  item = values.cbegin();
  if (*item++ != inputs[1] || item != pos || *item++ != 33UL || *item++ != 51UL || item != values.cend())
    return 2;
  return 0;
}
)r19containers");
  const auto Output = tmpFile("forward-list-unsigned-range-and-emplacement.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-unsigned-range-and-emplacement" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListUnsignedVolatilePointerRangeGuardRejected) {
  const auto Source = tmpFile("forward-list-unsigned-volatile-pointer-range-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  volatile unsigned short inputs[] = {4, 13};
  std::forward_list<unsigned> values;
  values.assign(inputs, inputs + 2);
  return values.front() != 4u;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListReverseEmptySingletonAndDoubleReversalRunAtBothOptimizations) {
  const auto Source = tmpFile("list-reverse-empty-singleton-and-double-reversal.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<int> values;
  values.reverse();
  if (!values.empty() || values.begin() != values.end())
    return 1;
  values.push_back(19);
  auto original = values.begin();
  values.reverse();
  if (values.begin() != original || values.front() != 19 || values.back() != 19)
    return 2;
  values.push_back(20);
  values.reverse();
  values.reverse();
  return values.size() != 2 || values.begin() != original || values.front() != 19 || values.back() != 20;
}
)r19containers");
  const auto Output = tmpFile("list-reverse-empty-singleton-and-double-reversal.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-reverse-empty-singleton-and-double-reversal" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListReverseOtherElementGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("list-reverse-other-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<long> values{6L, 9L};
  values.reverse();
  return values.front() != 9L;
}
)r19containers");
  const auto Output = tmpFile("list-reverse-other-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-reverse-other-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRangeEraseUnlinksBeforeReleaseRunAtBothOptimizations) {
  const auto Source = tmpFile("list-range-erase-unlinks-before-release.cpp");
  writeFile(Source, R"r19containers(using Size = decltype(sizeof(0));
extern "C" void* malloc(Size);
extern "C" void free(void*);
#include <new>
#include <list>
static std::list<int>* observed;
static int released, bad;
void* operator new(Size n) { return ::malloc(n); }
void operator delete(void* memory) noexcept {
  if (observed) {
    ++released;
    auto after = observed->begin();
    ++after;
    if (observed->front() != 10 || observed->back() != 50 || *after != 50 ||
        observed->size() != static_cast<Size>(5 - released))
      bad = 1;
  }
  ::free(memory);
}
int main() {
  std::list<int> values{10, 20, 30, 40, 50};
  auto first = values.cbegin();
  ++first;
  auto last = values.cend();
  --last;
  observed = &values;
  auto result = values.erase(first, last);
  observed = nullptr;
  return bad || released != 3 || *result != 50 || values.size() != 2;
}
)r19containers");
  const auto Output = tmpFile("list-range-erase-unlinks-before-release.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-range-erase-unlinks-before-release" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListSpliceWholeIntoMiddlePreservesAddressRunAtBothOptimizations) {
  const auto Source = tmpFile("list-splice-whole-into-middle-preserves-address.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<int> values{1, 7};
  std::list<int> source{3, 4, 5};
  auto moved = source.begin();
  ++moved;
  auto* address = &*moved;
  auto position = values.cbegin();
  ++position;
  auto end = values.end();
  values.splice(position, source);
  if (!source.empty() || source.begin() != source.end() || values.size() != 5 ||
      values.end() != end || values.front() != 1 || values.back() != 7)
    return 1;
  auto item = values.begin();
  if (*item++ != 1 || *item++ != 3 || item != moved || &*item != address ||
      *item++ != 4 || *item++ != 5 || *item++ != 7 || item != end)
    return 2;
  source.push_back(11);
  return source.front() != 11 || values.size() != 5;
}
)r19containers");
  const auto Output = tmpFile("list-splice-whole-into-middle-preserves-address.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-splice-whole-into-middle-preserves-address" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListSpliceEmptySourceLeavesAnExistingPositionRunAtBothOptimizations) {
  const auto Source = tmpFile("list-splice-empty-source-leaves-an-existing-position.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<int> values{9, 14};
  std::list<int> source;
  auto position = values.cbegin();
  ++position;
  const int* address = &*position;
  values.splice(position, source);
  return !source.empty() || values.size() != 2 || &values.back() != address || *position != 14;
}
)r19containers");
  const auto Output = tmpFile("list-splice-empty-source-leaves-an-existing-position.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-splice-empty-source-leaves-an-existing-position" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListSpliceSingleWithinOwnerAndAdjacentNoOpsRunAtBothOptimizations) {
  const auto Source = tmpFile("list-splice-single-within-owner-and-adjacent-no-ops.cpp");
  writeFile(Source, R"r19containers(#include <list>
#include <utility>
int main() {
  std::list<int> values{2, 3, 5, 7};
  auto moved = values.begin();
  ++moved;
  auto next = moved;
  ++next;
  const int* address = &*moved;
  values.splice(moved, values, moved);
  values.splice(next, values, moved);
  if (values.front() != 2 || values.back() != 7 || values.size() != 4 || *next != 5)
    return 1;
  auto after_first = values.begin();
  ++after_first;
  if (after_first != moved)
    return 2;
  values.splice(values.cend(), std::move(values), moved);
  auto tail = values.end();
  --tail;
  return values.size() != 4 || values.front() != 2 || values.back() != 3 ||
      tail != moved || &*tail != address || *--tail != 7;
}
)r19containers");
  const auto Output = tmpFile("list-splice-single-within-owner-and-adjacent-no-ops.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-splice-single-within-owner-and-adjacent-no-ops" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListSpliceRangeWithinOwnerAndEmptyRangeRunAtBothOptimizations) {
  const auto Source = tmpFile("list-splice-range-within-owner-and-empty-range.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<int> values{2, 3, 5, 7, 11};
  auto first = values.begin();
  auto after = first;
  ++after;
  ++after;
  auto end = values.end();
  const int* address = &*first;
  values.splice(after, values, first, after);
  values.splice(first, values, after, after);
  if (values.begin() != first || values.front() != 2 || values.back() != 11 || values.size() != 5)
    return 1;
  values.splice(end, values, first, after);
  auto check = values.begin();
  if (check != after || *check++ != 5 || *check++ != 7 || *check++ != 11 || check != first ||
      &*check != address || *check++ != 2 || *check++ != 3 || check != end)
    return 2;
  return values.size() != 5 || values.end() != end;
}
)r19containers");
  const auto Output = tmpFile("list-splice-range-within-owner-and-empty-range.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-splice-range-within-owner-and-empty-range" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRemoveValueAliasedToFirstNodeRunAtBothOptimizations) {
  const auto Source = tmpFile("list-remove-value-aliased-to-first-node.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<int> items{3, 3, 5, 3, 7, 3};
  auto kept = items.begin();
  ++kept;
  ++kept;
  const int* address = &*kept;
  const int& selected = items.front();
  items.remove(selected);
  if (items.size() != 2 || items.begin() != kept || &items.front() != address || items.front() != 5 || items.back() != 7)
    return 1;
  items.remove(11);
  if (items.size() != 2 || items.begin() != kept)
    return 2;
  std::list<int> equal{13, 13, 13};
  equal.remove(equal.back());
  equal.remove(13);
  return !equal.empty() || equal.begin() != equal.end();
}
)r19containers");
  const auto Output = tmpFile("list-remove-value-aliased-to-first-node.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-remove-value-aliased-to-first-node" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRemoveAndUniqueUnlinkBeforeFirstReleaseRunAtBothOptimizations) {
  const auto Source = tmpFile("list-remove-and-unique-unlink-before-first-release.cpp");
  writeFile(Source, R"r19containers(using Size = decltype(sizeof(0));
extern "C" void* malloc(Size);
extern "C" void free(void*);
#include <new>
#include <list>
static std::list<int>* observed;
static int expected_front, expected_back, released, bad;
void* operator new(Size n) { return ::malloc(n); }
void operator delete(void* p) noexcept {
  if (observed) {
    ++released;
    if (observed->size() != 2 || observed->front() != expected_front || observed->back() != expected_back) {
      bad = 1;
    } else {
      auto i = observed->begin();
      ++i;
      if (*i != expected_back || ++i != observed->end())
        bad = 1;
    }
  }
  ::free(p);
}
int main() {
  std::list<int> removed{3, 5, 3, 9, 3};
  expected_front = 5;
  expected_back = 9;
  observed = &removed;
  removed.remove(removed.front());
  observed = nullptr;
  if (bad || released != 3 || removed.size() != 2)
    return 1;
  std::list<int> unique{7, 7, 11, 11, 11};
  expected_front = 7;
  expected_back = 11;
  released = 0;
  observed = &unique;
  unique.unique();
  observed = nullptr;
  return bad || released != 3 || unique.size() != 2;
}
)r19containers");
  const auto Output = tmpFile("list-remove-and-unique-unlink-before-first-release.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-remove-and-unique-unlink-before-first-release" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListSortDefaultIsStableAndKeepsNodeIdentityRunAtBothOptimizations) {
  const auto Source = tmpFile("list-sort-default-is-stable-and-keeps-node-identity.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<int> items{-3, 7, -3, 1, 7, 0, -8};
  auto first = items.begin();
  auto second = first;
  ++second;
  auto third = second;
  ++third;
  const int* first_address = &*first;
  const int* second_address = &*second;
  auto end = items.end();
  items.sort();
  if (items.front() != -8 || items.back() != 7 || items.size() != 7 || items.end() != end)
    return 1;
  auto item = items.begin();
  ++item;
  if (item != first || &*item != first_address || *item++ != -3 || item != third ||
      *item++ != -3 || *item++ != 0 || *item++ != 1 || item != second ||
      &*item != second_address || *item++ != 7 || *item++ != 7 || item != end)
    return 2;
  return *--end != 7 || *--end != 7 || *--end != 1;
}
)r19containers");
  const auto Output = tmpFile("list-sort-default-is-stable-and-keeps-node-identity.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-sort-default-is-stable-and-keeps-node-identity" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListMergeKeepsLeftEqualsFirstAndReusesOwnersRunAtBothOptimizations) {
  const auto Source = tmpFile("list-merge-keeps-left-equals-first-and-reuses-owners.cpp");
  writeFile(Source, R"r19containers(#include <list>
#include <utility>
int main() {
  std::list<int> left{-7, 3, 7};
  std::list<int> right{-9, 3, 3, 11};
  auto first_equal = left.begin();
  ++first_equal;
  auto moved = right.begin();
  ++moved;
  const int* address = &*moved;
  left.merge(std::move(right));
  if (left.size() != 7 || !right.empty() || right.begin() != right.end())
    return 1;
  auto item = left.begin();
  if (*item++ != -9 || *item++ != -7 || item != first_equal || *item++ != 3 ||
      item != moved || &*item != address || *item++ != 3 || *item++ != 3 || *item++ != 7 ||
      *item++ != 11 || item != left.end())
    return 2;
  left.merge(left);
  right.merge(left);
  return !left.empty() || right.size() != 7 || right.front() != -9 || right.back() != 11;
}
)r19containers");
  const auto Output = tmpFile("list-merge-keeps-left-equals-first-and-reuses-owners.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-merge-keeps-left-equals-first-and-reuses-owners" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListOwnerRelationsSignedDifferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("list-owner-relations-signed-difference.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  const std::list<int> first{3, -5, 11};
  const std::list<int> second{3, 5, -11};
  return !(first < second) || !(second > first) || first >= second || second <= first ||
      !(first <= second) || !(second >= first) || first == second || !(first != second);
}
)r19containers");
  const auto Output = tmpFile("list-owner-relations-signed-difference.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-owner-relations-signed-difference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListOwnerRelationsEvaluateOperandsOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("list-owner-relations-evaluate-operands-once.cpp");
  writeFile(Source, R"r19containers(#include <list>
static int left_calls, right_calls;
using List = std::list<int>;
const List& left(const List& x) { ++left_calls; return x; }
const List& right(const List& x) { ++right_calls; return x; }
int main() {
  const List first{-11}, second{-3};
  if (!(left(first) < right(second)) || left_calls != 1 || right_calls != 1)
    return 1;
  if (std::operator==(left(first), right(second)) || left_calls != 2 || right_calls != 2)
    return 2;
  return 0;
}
)r19containers");
  const auto Output = tmpFile("list-owner-relations-evaluate-operands-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-owner-relations-evaluate-operands-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListResizeDefaultFillAndRepeatedCountRunAtBothOptimizations) {
  const auto Source = tmpFile("list-resize-default-fill-and-repeated-count.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<int> items{7, 13};
  auto first = items.begin();
  const int* address = &*first;
  auto end = items.end();
  items.resize(4);
  if (items.size() != 4 || items.begin() != first || items.end() != end || items.back() != 0)
    return 1;
  items.resize(6, -5);
  items.resize(6, 9);
  auto tail = items.end();
  if (*--tail != -5 || *--tail != -5 || *--tail != 0)
    return 2;
  items.resize(1);
  if (items.size() != 1 || items.begin() != first || &items.front() != address || items.back() != 7)
    return 3;
  items.resize(0, 11);
  return !items.empty() || items.begin() != end || items.end() != end;
}
)r19containers");
  const auto Output = tmpFile("list-resize-default-fill-and-repeated-count.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-resize-default-fill-and-repeated-count" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListResizeShrinkUnlinksBeforeReleasingTheTailRunAtBothOptimizations) {
  const auto Source = tmpFile("list-resize-shrink-unlinks-before-releasing-the-tail.cpp");
  writeFile(Source, R"r19containers(using Size = decltype(sizeof(0));
extern "C" void* malloc(Size);
extern "C" void free(void*);
#include <new>
#include <list>
static std::list<int>* observed;
static int deletes, bad;
void* operator new(Size bytes) { return ::malloc(bytes); }
void operator delete(void* p) noexcept {
  if (observed) {
    ++deletes;
    auto last = observed->end();
    if (observed->front() != 2 || *--last != 3 || observed->size() != static_cast<Size>(5 - deletes))
      bad = 1;
  }
  ::free(p);
}
int main() {
  std::list<int> items{2, 3, 5, 7, 11};
  auto kept = items.begin();
  ++kept;
  observed = &items;
  items.resize(2, 17);
  observed = nullptr;
  return bad || deletes != 3 || items.size() != 2 || &items.back() != &*kept;
}
)r19containers");
  const auto Output = tmpFile("list-resize-shrink-unlinks-before-releasing-the-tail.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-resize-shrink-unlinks-before-releasing-the-tail" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListLongStableOrderIteratorValuesAndCopyRunAtBothOptimizations) {
  const auto Source = tmpFile("list-long-stable-order-iterator-values-and-copy.cpp");
  writeFile(Source, R"r19containers(#include <list>
#include <utility>
int main() {
  constexpr long hi = sizeof(long) == 8 ? static_cast<long>(4294967301LL) : 1879048197L;
  constexpr long lo = sizeof(long) == 8 ? static_cast<long>(-4294967303LL) : -1879048199L;
  std::list<long> items{hi, lo, hi, 3L};
  auto first_hi = items.begin();
  const auto* address = first_hi.operator->();
  items.sort();
  if (items.front() != lo || items.back() != hi || items.size() != 4 || *address != hi)
    return 1;
  items.unique();
  auto last = items.end();
  if (--last != first_hi || last.operator->() != address || items.size() != 3)
    return 2;
  const std::list<long> copied(items);
  if (copied != items || &copied.back() == &items.back() || *copied.cbegin() != lo)
    return 3;
  items.reverse();
  std::list<long> moved(std::move(items));
  return !items.empty() || moved.begin() != first_hi || moved.front() != hi || moved.back() != lo;
}
)r19containers");
  const auto Output = tmpFile("list-long-stable-order-iterator-values-and-copy.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-long-stable-order-iterator-values-and-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListUnsignedLongResizeInsertAndSpliceRunAtBothOptimizations) {
  const auto Source = tmpFile("list-unsigned-long-resize-insert-and-splice.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  constexpr unsigned long maximum = static_cast<unsigned long>(-1);
  std::list<unsigned long> items{2UL};
  items.resize(4, maximum);
  items.resize(5);
  if (items.size() != 5 || items.front() != 2UL || items.back() != 0UL)
    return 1;
  auto moved = items.begin();
  ++moved;
  std::list<unsigned long> out{3UL};
  out.splice(out.cend(), items, moved);
  auto* address = &*moved;
  auto inserted = out.insert(out.cbegin(), maximum - 2UL);
  if (*inserted != maximum - 2UL || out.size() != 3 || items.size() != 4 || &out.back() != address)
    return 2;
  items.erase(items.cbegin(), items.cend());
  out.pop_front();
  return !items.empty() || out.front() != 3UL || out.back() != maximum;
}
)r19containers");
  const auto Output = tmpFile("list-unsigned-long-resize-insert-and-splice.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-unsigned-long-resize-insert-and-splice" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListObserverAllocatorConstructsATypedOwnerRunAtBothOptimizations) {
  const auto Source = tmpFile("list-observer-allocator-constructs-a-typed-owner.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  const std::list<unsigned long> first{3UL, 7UL};
  auto allocator = first.get_allocator();
  std::list<unsigned long> second(allocator);
  if (!second.empty() || first.size() != 2 || first.front() != 3UL)
    return 1;
  second.push_back(static_cast<unsigned long>(-1));
  return second.size() != 1 || second.back() != static_cast<unsigned long>(-1);
}
)r19containers");
  const auto Output = tmpFile("list-observer-allocator-constructs-a-typed-owner.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-observer-allocator-constructs-a-typed-owner" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListObserversEvaluateAPointerReceiverOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("list-observers-evaluate-a-pointer-receiver-once.cpp");
  writeFile(Source, R"r19containers(#include <list>
const std::list<int>* observe(const std::list<int>& items, int& calls) {
  ++calls;
  return &items;
}
int main() {
  const std::list<int> first{4, 7};
  int calls = 0;
  if (observe(first, calls)->max_size() <= 2 || calls != 1)
    return 1;
  std::list<int> second(observe(first, calls)->get_allocator());
  return calls != 2 || !second.empty() || first.size() != 2;
}
)r19containers");
  const auto Output = tmpFile("list-observers-evaluate-a-pointer-receiver-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-observers-evaluate-a-pointer-receiver-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListUniquePredicateComparesBeforeEachRunTransferRunAtBothOptimizations) {
  const auto Source = tmpFile("list-unique-predicate-compares-before-each-run-transfer.cpp");
  writeFile(Source, R"r19containers(#include <list>
std::list<long>* active;
int sizes;
bool same_parity(long left, long right) {
  sizes = sizes * 10 + static_cast<int>(active->size());
  return (left & 1L) == (right & 1L);
}
int main() {
  std::list<long> values{10L, 12L, 11L, 13L, 14L};
  active = &values;
  auto first = values.begin();
  auto second = first;
  ++second;
  ++second;
  values.unique(&same_parity);
  if (sizes != 5544 || values.size() != 3)
    return 1;
  auto item = values.begin();
  return item != first || *item++ != 10L || item != second || *item++ != 11L ||
         *item++ != 14L || item != values.end();
}
)r19containers");
  const auto Output = tmpFile("list-unique-predicate-compares-before-each-run-transfer.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-unique-predicate-compares-before-each-run-transfer" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListWideUnsignedMergePreservesHighBitAndStabilityRunAtBothOptimizations) {
  const auto Source = tmpFile("list-wide-unsigned-merge-preserves-high-bit-and-stability.cpp");
  writeFile(Source, R"r19containers(#include <list>
#include <utility>
int main() {
  const unsigned long long high = 0x8000000000000007ULL;
  const unsigned long long max = 0xffffffffffffffffULL;
  std::list<unsigned long long> left{3ULL, high};
  std::list<unsigned long long> right{high, max};
  auto left_high = left.end();
  --left_high;
  auto right_high = right.begin();
  auto allocation = left.get_allocator();
  std::list<unsigned long long> empty(allocation);
  left.merge(std::move(right));
  auto item = left.begin();
  if (!right.empty() || !empty.empty() || left.max_size() == 0 || *item++ != 3ULL ||
      item != left_high || *item++ != high || item != right_high || *item++ != high)
    return 1;
  return *item++ != max || item != left.end();
}
)r19containers");
  const auto Output = tmpFile("list-wide-unsigned-merge-preserves-high-bit-and-stability.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-wide-unsigned-merge-preserves-high-bit-and-stability" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListEmplaceAtPositionRetainsNeighborIteratorsRunAtBothOptimizations) {
  const auto Source = tmpFile("list-emplace-at-position-retains-neighbor-iterators.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<unsigned> values{5u, 9u};
  auto first = values.begin();
  auto last = values.end();
  --last;
  auto position = values.cbegin();
  ++position;
  auto inserted = values.emplace(position, 7u);
  auto zero = values.emplace(values.cend());
  auto item = values.begin();
  return item != first || *item++ != 5u || item != inserted || *item++ != 7u ||
         item != last || *item++ != 9u || item != zero || *item++ != 0u || item != values.end();
}
)r19containers");
  const auto Output = tmpFile("list-emplace-at-position-retains-neighbor-iterators.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-emplace-at-position-retains-neighbor-iterators" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListShortPayloadSortAndLvalueStoreRunAtBothOptimizations) {
  const auto Source = tmpFile("list-short-payload-sort-and-lvalue-store.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<short> values{31000, -30000, 2500};
  auto high = values.begin();
  values.sort();
  auto item = values.begin();
  if (*item++ != -30000 || *item++ != 2500 || item != high || *item != 31000)
    return 1;
  *item = -255;
  return values.back() != -255 || &*high != &values.back() || ++item != values.end();
}
)r19containers");
  const auto Output = tmpFile("list-short-payload-sort-and-lvalue-store.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-short-payload-sort-and-lvalue-store" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListCharacterPayloadsKeepDistinctTypeAndOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("list-character-payloads-keep-distinct-type-and-order.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<signed char> signed_values{-120, 100, -2};
  signed_values.sort();
  auto s = signed_values.begin();
  if (*s++ != -120 || *s++ != -2 || *s++ != 100 || s != signed_values.end())
    return 1;
  std::list<unsigned char> unsigned_values{250, 3, 128};
  unsigned_values.sort();
  auto u = unsigned_values.begin();
  if (*u++ != 3 || *u++ != 128 || *u++ != 250 || u != unsigned_values.end())
    return 2;
  std::list<char> plain{'x', 'y'};
  plain.reverse();
  return plain.front() != 'y' || plain.back() != 'x';
}
)r19containers");
  const auto Output = tmpFile("list-character-payloads-keep-distinct-type-and-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-character-payloads-keep-distinct-type-and-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListCountAssignmentCopiesExistingNodesBeforeAllocationRunAtBothOptimizations) {
  const auto Source = tmpFile("list-count-assignment-copies-existing-nodes-before-allocation.cpp");
  writeFile(Source, R"r19containers(using Size = decltype(sizeof(0));
extern "C" void* malloc(Size);
extern "C" void free(void*);
#include <new>
#include <list>
static std::list<long>* observed;
static long fill = 13L;
static int allocations, bad;
void* operator new(Size size) {
  if (observed) {
    ++allocations;
    if (observed->size() != 1 || observed->front() != 13L)
      bad = 1;
    fill = 25L;
  }
  return ::malloc(size);
}
void operator delete(void* p) noexcept { ::free(p); }
int main() {
  std::list<long> values{1L};
  observed = &values;
  values.assign(3, fill);
  observed = nullptr;
  auto item = values.begin();
  return bad || allocations != 2 || values.size() != 3 || *item++ != 13L ||
         *item++ != 25L || *item++ != 25L || item != values.end();
}
)r19containers");
  const auto Output = tmpFile("list-count-assignment-copies-existing-nodes-before-allocation.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-count-assignment-copies-existing-nodes-before-allocation" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRangeAssignmentGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("list-range-assignment-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  int values[]{2, 3, 5};
  std::list<int> list;
  list.assign(values, values + 3);
  return list.size() != 3 || list.back() != 5;
}
)r19containers");
  const auto Output = tmpFile("list-range-assignment-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-range-assignment-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListIteratorRangeOverwritesAndShrinksRunAtBothOptimizations) {
  const auto Source = tmpFile("list-iterator-range-overwrites-and-shrinks.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  const std::list<unsigned short> input{65000, 17, 60000};
  std::list<unsigned short> values{2, 3, 4};
  auto first = values.begin();
  auto second = first;
  ++second;
  auto start = input.cbegin();
  ++start;
  values.assign(start, input.cend());
  auto item = values.begin();
  if (values.size() != 2 || item != first || *item++ != 17 ||
      item != second || *item++ != 60000 || item != values.end())
    return 1;
  values.assign(input.cend(), input.cend());
  return !values.empty() || values.begin() != values.end();
}
)r19containers");
  const auto Output = tmpFile("list-iterator-range-overwrites-and-shrinks.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-iterator-range-overwrites-and-shrinks" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListConstructRawRangeLoadsAfterAllocationRunAtBothOptimizations) {
  const auto Source = tmpFile("list-construct-raw-range-loads-after-allocation.cpp");
  writeFile(Source, R"r19containers(using Size = decltype(sizeof(0));
extern "C" void* malloc(Size);
extern "C" void free(void*);
#include <new>
#include <list>
static signed char input[]{1, 2};
static int calls;
static bool observe;
void* operator new(Size bytes) {
  if (observe) {
    ++calls;
    input[0] = static_cast<signed char>(-90 - calls);
    input[1] = static_cast<signed char>(90 + calls);
  }
  return ::malloc(bytes);
}
void operator delete(void* memory) noexcept { ::free(memory); }
int main() {
  const signed char* first = input;
  observe = true;
  const std::list<long> values(first, first + 2);
  observe = false;
  return calls != 2 || values.size() != 2 || values.front() != -91L || values.back() != 92L;
}
)r19containers");
  const auto Output = tmpFile("list-construct-raw-range-loads-after-allocation.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-construct-raw-range-loads-after-allocation" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListAssignInitializerReusesPrefixAndClearsRunAtBothOptimizations) {
  const auto Source = tmpFile("list-assign-initializer-reuses-prefix-and-clears.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<int> values{1, 2, 3};
  auto first = values.begin();
  auto second = first;
  ++second;
  values.assign({6, -3});
  auto item = values.begin();
  if (values.size() != 2 || item != first || *item++ != 6 ||
      item != second || *item++ != -3 || item != values.end())
    return 1;
  values.assign({});
  return !values.empty() || values.begin() != values.end();
}
)r19containers");
  const auto Output = tmpFile("list-assign-initializer-reuses-prefix-and-clears.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-assign-initializer-reuses-prefix-and-clears" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListInitializerAssignmentCapturesRightSideFirstRunAtBothOptimizations) {
  const auto Source = tmpFile("list-initializer-assignment-captures-right-side-first.cpp");
  writeFile(Source, R"r19containers(#include <list>
int ordering;
std::list<long>& target(std::list<long>& value) {
  ordering = ordering * 10 + 2;
  return value;
}
long element(long result) {
  ordering = ordering * 10 + 1;
  return result;
}
int main() {
  std::list<long> values{5L};
  target(values) = {element(7L), element(-9L)};
  return ordering != 112 || values.size() != 2 || values.front() != 7L || values.back() != -9L;
}
)r19containers");
  const auto Output = tmpFile("list-initializer-assignment-captures-right-side-first.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-initializer-assignment-captures-right-side-first" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListInsertConvertingRangeDefersAllLinksRunAtBothOptimizations) {
  const auto Source = tmpFile("list-insert-converting-range-defers-all-links.cpp");
  writeFile(Source, R"r19containers(using Size = decltype(sizeof(0));
extern "C" void* malloc(Size);
extern "C" void free(void*);
#include <new>
#include <list>
static short input[]{-9, 10};
static std::list<long>* observed;
static int calls, bad;
void* operator new(Size size) {
  if (observed) {
    ++calls;
    if (observed->size() != 2 || observed->front() != 1L || observed->back() != 5L)
      bad = 1;
    input[0] = -11 - calls;
    input[1] = 20 + calls;
  }
  return ::malloc(size);
}
void operator delete(void* p) noexcept { ::free(p); }
int main() {
  std::list<long> values{1L, 5L};
  auto position = values.begin();
  ++position;
  const short* start = input;
  observed = &values;
  auto first = values.insert(position, start, start + 2);
  observed = nullptr;
  auto item = values.begin();
  ++item;
  return bad || calls != 2 || values.size() != 4 || item != first ||
         *item++ != -12L || *item++ != 22L || item != position;
}
)r19containers");
  const auto Output = tmpFile("list-insert-converting-range-defers-all-links.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-insert-converting-range-defers-all-links" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListInsertInitializerAtBeginAndEmptyAtEndRunAtBothOptimizations) {
  const auto Source = tmpFile("list-insert-initializer-at-begin-and-empty-at-end.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<int> values{19};
  auto old = values.begin();
  auto first = values.insert(old, {2, -7});
  auto item = values.begin();
  if (item != first || *item++ != 2 || *item++ != -7 || item != old || *item != 19)
    return 1;
  return values.insert(values.cend(), {}) != values.end() || values.size() != 3;
}
)r19containers");
  const auto Output = tmpFile("list-insert-initializer-at-begin-and-empty-at-end.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-insert-initializer-at-begin-and-empty-at-end" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListExplicitOrderMergeKeepsDestinationTiesFirstRunAtBothOptimizations) {
  const auto Source = tmpFile("list-explicit-order-merge-keeps-destination-ties-first.cpp");
  writeFile(Source, R"r19containers(#include <list>
#include <utility>
bool higher(long left, long right) { return left > right; }
int main() {
  std::list<long> destination{7L, 5L, -1L};
  std::list<long> source{7L, 6L, -8L};
  auto dst = destination.begin();
  auto src = source.begin();
  destination.merge(std::move(source), higher);
  auto item = destination.begin();
  return !source.empty() || destination.size() != 6 || item != dst || *item++ != 7L ||
         item != src || *item++ != 7L || *item++ != 6L || *item++ != 5L ||
         *item++ != -1L || *item++ != -8L || item != destination.end();
}
)r19containers");
  const auto Output = tmpFile("list-explicit-order-merge-keeps-destination-ties-first.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-explicit-order-merge-keeps-destination-ties-first" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListConstReverseIteratorEmptyAndTraversalRunAtBothOptimizations) {
  const auto Source = tmpFile("list-const-reverse-iterator-empty-and-traversal.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  const std::list<unsigned short> values{2, 5, 5, 7};
  auto it = values.crbegin();
  unsigned result = 0;
  while (it != values.crend()) {
    result = result * 10 + *it;
    ++it;
  }
  const std::list<unsigned short> empty;
  return result == 7552 && empty.rbegin() == empty.rend() && values.rbegin() == values.crbegin()
      && values.rend().base() == values.cbegin() ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("list-const-reverse-iterator-empty-and-traversal.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-const-reverse-iterator-empty-and-traversal" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListReverseIteratorDistanceSourceGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("list-reverse-iterator-distance-source-guard.cpp");
  writeFile(Source, R"r19containers(#include <iterator>
#include <list>
int main() {
  std::list<int> values{1, 2, 3};
  return std::distance(values.rbegin(), values.rend()) == 3 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("list-reverse-iterator-distance-source-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-reverse-iterator-distance-source-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListConstReverseNavigationDirectionsRunAtBothOptimizations) {
  const auto Source = tmpFile("list-const-reverse-navigation-directions.cpp");
  writeFile(Source, R"r19containers(#include <iterator>
#include <list>
int main() {
  const std::list<unsigned int> values{2u, 3u, 5u, 7u, 8u};
  auto head = values.crbegin();
  auto cursor = std::next(head, 2);
  if (*head != 8u || *cursor != 5u || std::distance(head, cursor) != 2)
    return 1;
  std::advance(cursor, -1);
  if (*cursor != 7u || std::prev(cursor) != head)
    return 2;
  std::advance(cursor, 3);
  return *cursor == 2u && std::next(cursor) == values.crend() ? 0 : 3;
}
)r19containers");
  const auto Output = tmpFile("list-const-reverse-navigation-directions.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-const-reverse-navigation-directions" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListFreeAccessEvaluatesReceiverOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("list-free-access-evaluates-receiver-once.cpp");
  writeFile(Source, R"r19containers(#include <iterator>
#include <list>
int calls;
std::list<long> &observe(std::list<long> &values) { ++calls; return values; }
int main() {
  std::list<long> values{3L, 8L};
  auto begin = std::begin(observe(values));
  auto end = std::end(observe(values));
  *begin = -4L;
  if (end != values.end() || values.front() != -4L)
    return 1;
  return std::size(observe(values)) == 2 && !std::empty(observe(values)) && calls == 4 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("list-free-access-evaluates-receiver-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-free-access-evaluates-receiver-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListFreeReverseAccessPreservesMutableAndEmptyEndpointsRunAtBothOptimizations) {
  const auto Source = tmpFile("list-free-reverse-access-preserves-mutable-and-empty-endpoints.cpp");
  writeFile(Source, R"r19containers(#include <iterator>
#include <list>
int main() {
  std::list<int> values{4, 7};
  auto from = std::rbegin(values);
  *from = 11;
  if (std::rend(values).base() != values.begin() || values.back() != 11)
    return 1;
  values.clear();
  return std::empty(values) && std::size(values) == 0 && std::begin(values) == std::end(values)
      && std::rbegin(values) == std::rend(values) ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("list-free-reverse-access-preserves-mutable-and-empty-endpoints.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-free-reverse-access-preserves-mutable-and-empty-endpoints" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListBoolRangeConversionAndReusedPrefixRunAtBothOptimizations) {
  const auto Source = tmpFile("list-bool-range-conversion-and-reused-prefix.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  int input[]{0, 5, -2};
  std::list<bool> values{true};
  bool *first = &values.front();
  values.assign(input, input + 3);
  if (values.size() != 3 || values.front() || &values.front() != first)
    return 1;
  auto item = values.begin();
  ++item;
  return *item && *++item && &*item == &values.back() ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("list-bool-range-conversion-and-reused-prefix.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-bool-range-conversion-and-reused-prefix" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListBoolReverseBaseAddressAndConstAccessRunAtBothOptimizations) {
  const auto Source = tmpFile("list-bool-reverse-base-address-and-const-access.cpp");
  writeFile(Source, R"r19containers(#include <iterator>
#include <list>
int main() {
  std::list<bool> values{false, true};
  auto last = std::rbegin(values);
  bool *element = last.operator->();
  *element = false;
  std::list<bool>::const_reverse_iterator checked(last);
  return checked.base() == values.cend() && *checked == false && &values.back() == element ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("list-bool-reverse-base-address-and-const-access.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-bool-reverse-base-address-and-const-access" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListTransparentGreaterMoveMergePreservesTiesRunAtBothOptimizations) {
  const auto Source = tmpFile("list-transparent-greater-move-merge-preserves-ties.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <list>
#include <utility>
int main() {
  const unsigned long long high = 4294967303ULL;
  std::list<unsigned long long> left{high, 7ULL}, right{high, 9ULL, 0ULL};
  auto left_first = left.begin();
  auto right_first = right.begin();
  left.merge(std::move(right), std::greater<>{});
  auto it = left.begin();
  return !right.empty() || it != left_first || *it++ != high || it != right_first ||
         *it++ != high || *it++ != 9ULL || *it++ != 7ULL || *it++ != 0ULL || it != left.end();
}
)r19containers");
  const auto Output = tmpFile("list-transparent-greater-move-merge-preserves-ties.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-transparent-greater-move-merge-preserves-ties" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListSortsWithSameTypeEmptyComparator) {
  const auto Source = tmpFile("list-standard-order-user-lookalike-guard.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <list>
struct greater {
  bool operator()(int left, int right) const { return left > right; }
};
int main() {
  std::list<int> values{1, 4};
  values.sort(greater{});
  return values.front() != 4;
}
)r19containers");
  const auto Output = tmpFile("list-standard-order-user-lookalike-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-standard-order-user-lookalike-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListAssignsDoubleInitializerAtBothOptimizations) {
  const auto Source = tmpFile("list-floating-initializer-assignment-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<double> values;
  values = {1.5, 2.25};
  return values.size() != 2;
}
)r19containers");
  const auto Output = tmpFile("list-floating-initializer-assignment-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-floating-initializer-assignment-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListReadsDoubleThroughFreeReverseAccess) {
  const auto Source = tmpFile("list-free-reverse-access-floating-payload-guard.cpp");
  writeFile(Source, R"r19containers(#include <iterator>
#include <list>
int main() {
  const std::list<double> values{2.0, 3.0};
  return *std::crbegin(values) == 3.0 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("list-free-reverse-access-floating-payload-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-free-reverse-access-floating-payload-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListProvidesFreeAccessToTrivialRecordElements) {
  const auto Source = tmpFile("list-free-access-record-payload-guard.cpp");
  writeFile(Source, R"r19containers(#include <iterator>
#include <list>
struct Record { int field; };
int main() {
  std::list<Record> values{Record{2}};
  return std::begin(values)->field == 2 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("list-free-access-record-payload-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-free-access-record-payload-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListIteratorNavigatesTrivialRecords) {
  const auto Source = tmpFile("list-iterator-non-scalar-navigation-guard.cpp");
  writeFile(Source, R"r19containers(#include <iterator>
#include <list>
struct Record { int value; };
int main() {
  std::list<Record> values{Record{1}, Record{3}};
  return std::next(values.begin())->value == 3 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("list-iterator-non-scalar-navigation-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-iterator-non-scalar-navigation-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListUserDefinedIntegerAllocatorGuardRejected) {
  const auto Source = tmpFile("list-user-defined-integer-allocator-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
#include <memory>
template <class T> struct LocalAllocator : std::allocator<T> {
  using value_type = T;
  template <class U> struct rebind { using other = LocalAllocator<U>; };
  LocalAllocator() noexcept = default;
  template <class U> LocalAllocator(const LocalAllocator<U>&) noexcept {}
};
int main() {
  std::list<long, LocalAllocator<long>> items{3L, 7L};
  items.reverse();
  return items.front() != 7L;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListRemoveIfNarrowingPredicateGuardRejected) {
  const auto Source = tmpFile("list-remove-if-narrowing-predicate-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
bool positive(int value) { return value > 0; }
int main() {
  std::list<long> items{0L, 1L};
  items.remove_if(&positive);
  return items.size() != 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListEmplaceUserConversionGuardRejected) {
  const auto Source = tmpFile("list-emplace-user-conversion-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Value {
  operator int() const { return 4; }
};
int main() {
  std::list<int> values;
  return values.emplace_back(Value{}) != 4;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListAssignRangeFloatingInputGuardRejected) {
  const auto Source = tmpFile("list-assign-range-floating-input-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  const double input[]{-1.5, 2.75};
  std::list<int> values;
  values.assign(input, input + 2);
  return values.size() != 2;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListInsertVolatilePointerRangeGuardRejected) {
  const auto Source = tmpFile("list-insert-volatile-pointer-range-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  volatile short source[]{9, 10};
  std::list<int> values;
  values.insert(values.cend(), source, source + 2);
  return values.size() != 2;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListOrderFunctionWithConvertingSignatureGuardRejected) {
  const auto Source = tmpFile("list-order-function-with-converting-signature-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
bool greater(long left, long right) { return left > right; }
int main() {
  std::list<int> values{2, 5, 7};
  values.sort(&greater);
  return values.front() != 7;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListEmptyOrderSignedWideSortPreservesNodesRunAtBothOptimizations) {
  const auto Source = tmpFile("list-empty-order-signed-wide-sort-preserves-nodes.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Descending {
  bool operator()(long long left, long long right) const { return right < left; }
};
int main() {
  const long long low = -4294967301LL;
  std::list<long long> values{low, 7LL, 7LL, -3LL};
  auto first_equal = values.begin();
  ++first_equal;
  const Descending compare{};
  values.sort(compare);
  auto it = values.begin();
  return it != first_equal || *it++ != 7LL || *it++ != 7LL || *it++ != -3LL ||
         *it++ != low || it != values.end();
}
)r19containers");
  const auto Output = tmpFile("list-empty-order-signed-wide-sort-preserves-nodes.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-empty-order-signed-wide-sort-preserves-nodes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListStandardGreaterTypedStableSortRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-standard-greater-typed-stable-sort.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <functional>
int main() {
  std::forward_list<short> values{-3, 12, 12, -8};
  auto first_equal = values.begin();
  ++first_equal;
  const std::greater<short> order{};
  values.sort(order);
  auto it = values.begin();
  return it != first_equal || *it++ != 12 || *it++ != 12 || *it++ != -3 ||
         *it++ != -8 || it != values.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-standard-greater-typed-stable-sort.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-standard-greater-typed-stable-sort" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListStandardLessLvalueMergeRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-standard-less-lvalue-merge.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <functional>
int main() {
  std::forward_list<long> left{-6L, 2L}, right{-3L, 4L};
  left.merge(right, std::less<long>{});
  auto it = left.begin();
  return !right.empty() || *it++ != -6L || *it++ != -3L || *it++ != 2L ||
         *it++ != 4L || it != left.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-standard-less-lvalue-merge.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-standard-less-lvalue-merge" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListUnsignedShortSortPreservesHighDuplicatesRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-unsigned-short-sort-preserves-high-duplicates.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  const unsigned short high = 65535;
  std::forward_list<unsigned short> values{high, 2, high, 3};
  auto first_high = values.begin();
  values.sort();
  auto it = values.begin();
  return *it++ != 2 || *it++ != 3 || it != first_high ||
         *it++ != high || *it++ != high || it != values.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-unsigned-short-sort-preserves-high-duplicates.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-unsigned-short-sort-preserves-high-duplicates" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListCharacterTypesPreserveWidthAndOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-character-types-preserve-width-and-order.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  const char edge = static_cast<char>(0xfe);
  std::forward_list<char> plain{edge, 'A'};
  std::forward_list<signed char> signed_values{63, -127};
  std::forward_list<unsigned char> unsigned_values{254, 10};
  plain.sort();
  signed_values.sort();
  unsigned_values.sort();
  auto plain_item = plain.begin();
  auto signed_item = signed_values.begin();
  auto unsigned_item = unsigned_values.begin();
  return *plain_item++ != (edge < 'A' ? edge : 'A') ||
         *plain_item++ != (edge < 'A' ? 'A' : edge) || plain_item != plain.end() ||
         *signed_item++ != -127 || *signed_item++ != 63 || signed_item != signed_values.end() ||
         *unsigned_item++ != 10 || *unsigned_item++ != 254 || unsigned_item != unsigned_values.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-character-types-preserve-width-and-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-character-types-preserve-width-and-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListUnsignedLongLongSortKeepsTopBitRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-unsigned-long-long-sort-keeps-top-bit.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  const unsigned long long upper = 9223372036854775817ULL;
  std::forward_list<unsigned long long> values{upper, 7ULL, upper, 0ULL};
  auto first_upper = values.begin();
  values.sort();
  auto it = values.begin();
  return *it++ != 0ULL || *it++ != 7ULL || it != first_upper ||
         *it++ != upper || *it++ != upper || it != values.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-unsigned-long-long-sort-keeps-top-bit.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-unsigned-long-long-sort-keeps-top-bit" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListEmptyOrderObservableCopyGuardRejected) {
  const auto Source = tmpFile("list-empty-order-observable-copy-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
#include <utility>
static int copies;
struct Compare {
  Compare() = default;
  Compare(const Compare&) { ++copies; }
  bool operator()(int left, int right) const { return left < right; }
};
int main() {
  std::list<int> left{1}, right{2};
  left.merge(std::move(right), Compare{});
  return copies != 1 || left.back() != 2 || !right.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListReferenceWrappedStandardOrderGuardRejected) {
  const auto Source = tmpFile("forward-list-reference-wrapped-standard-order-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <functional>
int main() {
  std::forward_list<long> values{4L, -1L, 6L};
  std::less<long> compare{};
  values.sort(std::ref(compare));
  return values.front() != -1L;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListEmplaceLoadsConvertingReferenceAfterAllocationRunAtBothOptimizations) {
  const auto Source = tmpFile("list-emplace-loads-converting-reference-after-allocation.cpp");
  writeFile(Source, R"r19containers(using Size = decltype(sizeof(0));
extern "C" void* malloc(Size);
extern "C" void free(void*);
#include <new>
#include <list>
static std::list<long long>* observed;
static int source = 13, allocations, previous_size;
void* operator new(Size count) {
  if (observed) {
    ++allocations;
    previous_size = static_cast<int>(observed->size());
    source = -17;
  }
  return ::malloc(count);
}
void operator delete(void* p) noexcept { ::free(p); }
int main() {
  std::list<long long> values{7LL};
  observed = &values;
  const int& input = source;
  auto& first = values.emplace_front(input);
  observed = nullptr;
  return allocations != 1 || previous_size != 1 || &first != &values.front() ||
         first != -17LL || values.size() != 2 || values.back() != 7LL;
}
)r19containers");
  const auto Output = tmpFile("list-emplace-loads-converting-reference-after-allocation.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-emplace-loads-converting-reference-after-allocation" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListFloatConstructAssignAndEmplaceSameTypeRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-float-construct-assign-and-emplace-same-type.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  float input[]{1.25f, -6.5f, 3.0f};
  std::forward_list<float> values(input, input + 3);
  if (values.front() != 1.25f)
    return 1;
  values.assign(input + 1, input + 3);
  float& first = values.emplace_front(input[0]);
  auto it = values.cbegin();
  return &first != &values.front() || *it++ != 1.25f || *it++ != -6.5f ||
         *it++ != 3.0f || it != values.cend();
}
)r19containers");
  const auto Output = tmpFile("forward-list-float-construct-assign-and-emplace-same-type.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-float-construct-assign-and-emplace-same-type" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListDoubleInitializerInsertRetainsPositionRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-double-initializer-insert-retains-position.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<double> values{1.125, 4.25};
  auto old = values.begin();
  auto last = values.insert_after(values.cbegin(), {2.25, 3.5});
  auto item = values.begin();
  return item != old || *item++ != 1.125 || *item++ != 2.25 ||
         item != last || *item++ != 3.5 || *item++ != 4.25 || item != values.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-double-initializer-insert-retains-position.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-double-initializer-insert-retains-position" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListEmplaceVolatileIntegerConversionGuardRejected) {
  const auto Source = tmpFile("list-emplace-volatile-integer-conversion-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<long> values{4L};
  volatile short value = -3;
  auto inserted = values.emplace(values.cbegin(), value);
  return *inserted != -3L || values.size() != 2;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListDoubleEmplaceVolatileArgumentGuardRejected) {
  const auto Source = tmpFile("forward-list-double-emplace-volatile-argument-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  volatile double input = -2.25;
  std::forward_list<double> values;
  return values.emplace_front(input) != -2.25;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListWideCharResizeAndMergeRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-wide-char-resize-and-merge.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<wchar_t> left{L'\u03bb'}, right{L'\u6c34'};
  const wchar_t* old = &right.front();
  left.merge(right);
  left.resize(3);
  auto value = left.cbegin();
  if (!right.empty() || *value++ != L'\u03bb' || &*value != old || *value++ != L'\u6c34')
    return 1;
  return *value++ != wchar_t{} || value != left.cend();
}
)r19containers");
  const auto Output = tmpFile("forward-list-wide-char-resize-and-merge.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-wide-char-resize-and-merge" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListUtf16KeepsStoredAddressRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-utf16-keeps-stored-address.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  const char16_t first = u'\ufffd';
  std::forward_list<char16_t> letters{u'\u03bb'};
  auto& added = letters.emplace_front(first);
  auto* saved = &added;
  letters.sort();
  auto value = letters.begin();
  if (*value++ != u'\u03bb' || &*value != saved || *value != first)
    return 1;
  *value++ = u'\u6c34';
  return value != letters.end() || added != u'\u6c34';
}
)r19containers");
  const auto Output = tmpFile("forward-list-utf16-keeps-stored-address.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-utf16-keeps-stored-address" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListUtf32SplicePreservesNodeRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-utf32-splice-preserves-node.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<char32_t> left{U'A'}, right{U'\U0001f642'};
  auto saved = right.begin();
  auto* address = &*saved;
  left.splice_after(left.cbefore_begin(), right);
  auto value = left.begin();
  return !right.empty() || value != saved || &*value != address ||
         *value++ != U'\U0001f642' || *value++ != U'A' || value != left.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-utf32-splice-preserves-node.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-utf32-splice-preserves-node" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListFixedWideEnumRangeAndComparisonRunAtBothOptimizations) {
  const auto Source = tmpFile("list-fixed-wide-enum-range-and-comparison.cpp");
  writeFile(Source, R"r19containers(#include <list>
enum class Tag : unsigned long long { empty = 0, high = 0x8000000000000000ULL };
int main() {
  const Tag input[] = {Tag::high, Tag::empty};
  std::list<Tag> values(input, input + 2);
  std::list<Tag> copy(values);
  values.remove(Tag::high);
  return values.size() != 1 || values.front() != Tag::empty ||
         copy.size() != 2 || copy.front() != Tag::high || !(values < copy);
}
)r19containers");
  const auto Output = tmpFile("list-fixed-wide-enum-range-and-comparison.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-fixed-wide-enum-range-and-comparison" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListFixedEnumMergePreservesStoredAddressRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-fixed-enum-merge-preserves-stored-address.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
enum class Rank : short { lowest = -15, next = 7, highest = 61 };
int main() {
  std::forward_list<Rank> left{Rank::lowest, Rank::highest}, right{Rank::next};
  Rank* original = &right.front();
  left.merge(right);
  auto item = left.begin();
  return !right.empty() || *item++ != Rank::lowest || &*item != original ||
         *item++ != Rank::next || *item++ != Rank::highest || item != left.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-fixed-enum-merge-preserves-stored-address.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-fixed-enum-merge-preserves-stored-address" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListWideCharVolatileEmplacementGuardRejected) {
  const auto Source = tmpFile("forward-list-wide-char-volatile-emplacement-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  volatile wchar_t input = L'\u03bb';
  std::forward_list<wchar_t> letters;
  return letters.emplace_front(input) != L'\u03bb';
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListFixedEnumCustomDefaultOrderGuardRejected) {
  const auto Source = tmpFile("forward-list-fixed-enum-custom-default-order-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
enum class Rank : int { lowest = -30, highest = 47 };
bool operator<(Rank left, Rank right) {
  return static_cast<int>(left) > static_cast<int>(right);
}
int main() {
  std::forward_list<Rank> ranks{Rank::lowest, Rank::highest};
  ranks.sort();
  return ranks.front() != Rank::highest;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListConstPointeeRangeCopiesPointerValuesRunAtBothOptimizations) {
  const auto Source = tmpFile("list-const-pointee-range-copies-pointer-values.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  const char data[] = {'a', 'b', 'c'};
  const char* input[] = {data, data + 2};
  std::list<const char*> pointers(input, input + 2);
  input[0] = data + 1;
  const auto& observed = pointers;
  auto cursor = observed.crbegin();
  if (*cursor != &data[2] || **cursor != 'c')
    return 1;
  ++cursor;
  return *cursor != &data[0] || **cursor != 'a' || pointers.size() != 2;
}
)r19containers");
  const auto Output = tmpFile("list-const-pointee-range-copies-pointer-values.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-const-pointee-range-copies-pointer-values" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListPointerMergeKeepsNodeAndPointeeRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-pointer-merge-keeps-node-and-pointee.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  double data[] = {1.5, 2.5, 3.5};
  std::forward_list<double*> left{data, data + 2}, right{data + 1};
  auto saved = right.begin();
  double** slot = &*saved;
  left.merge(right);
  auto item = left.begin();
  if (!right.empty() || *item++ != data || item != saved || &*item != slot)
    return 1;
  **item++ = 4.25;
  return *item++ != data + 2 || item != left.end() || data[1] != 4.25;
}
)r19containers");
  const auto Output = tmpFile("forward-list-pointer-merge-keeps-node-and-pointee.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-pointer-merge-keeps-node-and-pointee" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListVolatilePointeeElementGuardRejected) {
  const auto Source = tmpFile("forward-list-volatile-pointee-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  volatile int value = 5;
  std::forward_list<volatile int*> pointers{&value};
  return **pointers.begin() != 5;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListTrivialRecordRangeAndRelinkRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-trivial-record-range-and-relink.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry { unsigned short code; char group; };
int main() {
  Entry input[] = {{4, 'A'}, {7, 'B'}};
  std::forward_list<Entry> values(input, input + 2);
  auto cursor = values.begin();
  Entry* saved = &*cursor;
  values.reverse();
  input[0].code = 3;
  if (values.front().code != 7 || values.front().group != 'B')
    return 1;
  ++cursor;
  auto tail = values.begin();
  ++tail;
  return cursor != values.end() || &*tail != saved || tail->code != 4 || tail->group != 'A';
}
)r19containers");
  const auto Output = tmpFile("forward-list-trivial-record-range-and-relink.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-trivial-record-range-and-relink" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordNodeSizesAllocationAndClearRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-node-sizes-allocation-and-clear.cpp");
  writeFile(Source, R"r19containers(using Size = decltype(sizeof(0));
extern "C" void* malloc(Size);
extern "C" void free(void*);
#include <list>
struct Entry { long long first; long long second; int tag; };
static bool checking;
static Size node_bytes;
static int allocations, releases;
void* operator new(Size size) {
  if (checking) {
    node_bytes = size;
    ++allocations;
  }
  return ::malloc(size);
}
void operator delete(void* p) noexcept {
  if (checking) ++releases;
  ::free(p);
}
int main() {
  std::list<Entry> values;
  Entry input{1, 2, 19};
  checking = true;
  values.push_back(input);
  bool correct = values.back().first == 1 && values.back().second == 2 &&
                 values.back().tag == 19 && node_bytes == 40 && allocations == 1;
  values.clear();
  checking = false;
  return !correct || releases != 1;
}
)r19containers");
  const auto Output = tmpFile("list-record-node-sizes-allocation-and-clear.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-node-sizes-allocation-and-clear" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListWiderRecordValueInitAndSpliceRunAtBothOptimizations) {
  const auto Source = tmpFile("list-wider-record-value-init-and-splice.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry { int first; int second; int third; int fourth; int fifth; };
int main() {
  std::list<Entry> left, right;
  auto& value = right.emplace_back();
  if (value.first || value.second || value.third || value.fourth || value.fifth)
    return 1;
  value.fifth = 23;
  left.splice(left.begin(), right);
  return !right.empty() || left.size() != 1 || &left.front() != &value || left.front().fifth != 23;
}
)r19containers");
  const auto Output = tmpFile("list-wider-record-value-init-and-splice.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-wider-record-value-init-and-splice" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListMixedTrivialRecordRangeAndResizeRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-mixed-trivial-record-range-and-resize.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
enum class Choice : unsigned char { selected = 7 };
struct Entry { float score; const int* target; Choice option; };
int main() {
  const int first = 6, second = 17;
  Entry input[] = {{1.25f, &first, Choice::selected}, {2.5f, &second, Choice::selected}};
  std::forward_list<Entry> values;
  values.assign(input, input + 2);
  input[1].score = 9.0f;
  values.resize(3);
  auto item = values.cbegin();
  if (item == values.cend() || item->score != 1.25f || item->target != &first)
    return 1;
  ++item;
  if (item == values.cend() || item->score != 2.5f || item->target != &second ||
      item->option != Choice::selected)
    return 2;
  ++item;
  if (item == values.cend() || item->score != 0.0f || item->target != nullptr ||
      item->option != static_cast<Choice>(0))
    return 3;
  return ++item != values.cend();
}
)r19containers");
  const auto Output = tmpFile("forward-list-mixed-trivial-record-range-and-resize.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-mixed-trivial-record-range-and-resize" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListNestedTrivialRecordCopyAndEraseRunAtBothOptimizations) {
  const auto Source = tmpFile("list-nested-trivial-record-copy-and-erase.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Coordinates { int x; short y; };
struct Element { long long id; Coordinates position; };
int main() {
  std::list<Element> source{{17, {23, 31}}, {61, {41, 47}}};
  std::list<Element> copy(source);
  auto first = copy.begin();
  if (first->id != 17 || first->position.x != 23 || first->position.y != 31)
    return 1;
  ++first;
  Element* keep = &*first;
  copy.erase(copy.begin());
  first->position.x = 103;
  return copy.size() != 1 || &copy.front() != keep || source.back().position.x != 41 ||
         copy.front().id != 61 || copy.front().position.x != 103 || copy.front().position.y != 47;
}
)r19containers");
  const auto Output = tmpFile("list-nested-trivial-record-copy-and-erase.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-nested-trivial-record-copy-and-erase" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListNestedTrivialRecordSpliceStabilityRunAtBothOptimizations) {
  const auto Source = tmpFile("list-nested-trivial-record-splice-stability.cpp");
  writeFile(Source, R"r19containers(#include <list>
enum class Mode : unsigned char { selected = 3 };
struct Item { double cost; const int* marker; Mode mode; };
struct Element { int tag; Item value; };
int main() {
  const int marker = 13;
  std::list<Element> from{{17, {3.5, &marker, Mode::selected}}}, into;
  auto& value = from.front().value;
  into.splice(into.end(), from);
  return !from.empty() || into.size() != 1 || &into.front().value != &value ||
         value.cost != 3.5 || value.marker != &marker || *value.marker != 13 ||
         value.mode != Mode::selected || into.front().tag != 17;
}
)r19containers");
  const auto Output = tmpFile("list-nested-trivial-record-splice-stability.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-nested-trivial-record-splice-stability" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListRecordNestedAndMultidimensionalArraysRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-record-nested-and-multidimensional-arrays.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Item { float score; const int* pointer; };
struct Entry { Item items[2]; int bias[2][2]; };
int main() {
  const int marker = 31;
  Entry input{{{1.25f, &marker}, {2.5f, &marker}}, {{3, 5}, {7, 11}}};
  std::forward_list<Entry> values;
  values.push_front(input);
  values.resize(2);
  auto cursor = values.begin();
  if (cursor->items[1].score != 2.5f || cursor->items[0].pointer != &marker ||
      cursor->bias[1][0] != 7 || cursor->bias[1][1] != 11)
    return 1;
  ++cursor;
  return cursor->items[1].score != 0.0f || cursor->items[0].pointer != nullptr ||
         cursor->bias[1][1] != 0 || ++cursor != values.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-nested-and-multidimensional-arrays.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-nested-and-multidimensional-arrays" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListSortUsesSelectedRecordOperator) {
  const auto Source = tmpFile("list-trivial-record-custom-default-order-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry { int code; };
bool operator<(const Entry& left, const Entry& right) {
  return left.code > right.code;
}
int main() {
  std::list<Entry> values{Entry{1}, Entry{3}};
  values.sort();
  return values.front().code != 3;
}
)r19containers");
  const auto Output = tmpFile("list-trivial-record-custom-default-order-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-trivial-record-custom-default-order-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRunsNestedStoredRecordDestructors) {
  const auto Source = tmpFile("list-nested-nontrivial-record-destruction-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Inner { int value; ~Inner() { value = 0; } };
struct Element { int id; Inner value; };
int main() {
  std::list<Element> values;
  values.emplace_back().value.value = 5;
  return values.back().value.value != 5;
}
)r19containers");
  const auto Output = tmpFile("list-nested-nontrivial-record-destruction-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-nested-nontrivial-record-destruction-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListWiderRecordBitFieldGuardRejected) {
  const auto Source = tmpFile("forward-list-wider-record-bit-field-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry { long long first; unsigned second : 5; int third; };
int main() {
  std::forward_list<Entry> values{Entry{2, 3, 5}};
  return values.front().third != 5;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListRecordVolatileFloatingFieldGuardRejected) {
  const auto Source = tmpFile("list-record-volatile-floating-field-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry { int key; volatile double score; };
int main() {
  std::list<Entry> values;
  auto& item = values.emplace_back();
  item.score = 1.25;
  return item.score != 1.25;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListRecordLongDoubleFieldGuardRejected) {
  const auto Source = tmpFile("list-record-long-double-field-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry { int key; long double value; };
int main() {
  std::list<Entry> values{{9, 1.25L}};
  return values.front().value != 1.25L;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListNestedTrivialUnionRecordGuardRejected) {
  const auto Source = tmpFile("list-nested-trivial-union-record-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
union Inner { int first; unsigned second; };
struct Element { int tag; Inner value; };
int main() {
  std::list<Element> values{{7, Inner{11}}};
  return values.back().value.first != 11;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListRecordArrayOfOveralignedValuesGuardRejected) {
  const auto Source = tmpFile("forward-list-record-array-of-overaligned-values-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct alignas(16) Item { int value; };
struct Entry { Item items[2]; };
int main() {
  std::forward_list<Entry> values;
  values.emplace_front().items[1].value = 11;
  return values.front().items[1].value != 11;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListRecordRemoveIfBorrowsNodeValueRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-remove-if-borrows-node-value.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry { int code; int data[2]; };
const Entry* watched;
bool observed;
bool remove_value(const Entry& value) {
  if (value.code == 6) observed = &value == watched;
  return value.code % 2 == 0;
}
int main() {
  std::list<Entry> values{{3, {7, 11}}, {6, {13, 17}}, {9, {19, 23}}};
  auto saved = values.begin();
  auto removed = saved;
  ++removed;
  watched = &*removed;
  values.remove_if(&remove_value);
  return !observed || values.size() != 2 || &*saved != &values.front() ||
         values.front().data[1] != 11 || values.back().code != 9;
}
)r19containers");
  const auto Output = tmpFile("list-record-remove-if-borrows-node-value.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-remove-if-borrows-node-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListRecordRemoveIfKeepsNodeAddressesRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-record-remove-if-keeps-node-addresses.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Detail { float score; };
struct Entry { Detail data; int tag; };
bool bad_score(const Entry& value) { return value.data.score < 0.0f; }
int main() {
  std::forward_list<Entry> values{{{-1.5f}, 5}, {{2.0f}, 7}, {{3.5f}, 11}};
  auto kept = values.begin();
  ++kept;
  auto* address = &*kept;
  values.remove_if(bad_score);
  auto position = values.begin();
  if (&*position != address || position->tag != 7) return 1;
  ++position;
  return position->tag != 11 || ++position != values.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-remove-if-keeps-node-addresses.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-remove-if-keeps-node-addresses" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordFieldStableSortRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-field-stable-sort.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Item { short rank; short id; };
bool descending(const Item& left, const Item& right) { return right.rank < left.rank; }
int main() {
  std::list<Item> entries{{2, 11}, {4, 13}, {2, 17}, {3, 19}};
  auto first = entries.begin();
  const Item* address = &*first;
  entries.sort(&descending);
  auto item = entries.begin();
  if (item->id != 13 || (++item)->id != 19) return 1;
  return &*++item != address || item->id != 11 ||
         (++item)->id != 17 || ++item != entries.end();
}
)r19containers");
  const auto Output = tmpFile("list-record-field-stable-sort.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-field-stable-sort" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListRecordFieldStableSortRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-record-field-stable-sort.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Item { int rank; int id; };
bool ascending(const Item& left, const Item& right) { return left.rank < right.rank; }
int main() {
  std::forward_list<Item> entries{{4, 11}, {2, 13}, {4, 17}, {1, 19}};
  auto saved = entries.begin();
  const Item* address = &*saved;
  entries.sort(ascending);
  auto item = entries.begin();
  if (item->id != 19 || (++item)->id != 13) return 1;
  return &*++item != address || item->id != 11 ||
         (++item)->id != 17 || ++item != entries.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-field-stable-sort.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-field-stable-sort" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListDefaultRecordOrderStableMergeRunAtBothOptimizations) {
  const auto Source = tmpFile("list-default-record-order-stable-merge.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Item { int rank; int id; };
bool operator<(const Item& left, const Item& right) { return left.rank < right.rank; }
int main() {
  std::list<Item> into{{1, 11}, {4, 13}};
  std::list<Item> from{{2, 17}, {4, 19}};
  auto moved = from.begin();
  into.merge(from);
  auto item = into.begin();
  if (!from.empty() || into.size() != 4 || item->id != 11) return 1;
  if (++item != moved || item->id != 17) return 2;
  return (++item)->id != 13 || (++item)->id != 19 || ++item != into.end();
}
)r19containers");
  const auto Output = tmpFile("list-default-record-order-stable-merge.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-default-record-order-stable-merge" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListDefaultRecordOrderStableMergeRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-default-record-order-stable-merge.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Item { unsigned rank; short id; };
bool operator<(const Item& left, const Item& right) { return left.rank > right.rank; }
int main() {
  std::forward_list<Item> into{{7, 11}, {3, 13}};
  std::forward_list<Item> from{{5, 17}, {3, 19}};
  auto moved = from.begin();
  into.merge(from);
  auto item = into.begin();
  if (!from.empty() || item->id != 11 || ++item != moved || item->id != 17) return 1;
  return (++item)->id != 13 || (++item)->id != 19 || ++item != into.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-default-record-order-stable-merge.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-default-record-order-stable-merge" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListSortUsesRecordValueParameters) {
  const auto Source = tmpFile("list-record-by-value-field-comparator-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Item { int rank; };
bool less(Item left, Item right) { return left.rank < right.rank; }
int main() {
  std::list<Item> values{{7}, {2}};
  values.sort(&less);
  return values.front().rank != 2;
}
)r19containers");
  const auto Output = tmpFile("list-record-by-value-field-comparator-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-by-value-field-comparator-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordVolatileReferencePredicateGuardRejected) {
  const auto Source = tmpFile("list-record-volatile-reference-predicate-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry { int value; };
bool negative(const volatile Entry& value) { return value.value < 0; }
int main() {
  std::list<Entry> values{{-2}, {4}};
  values.remove_if(&negative);
  return values.front().value != 4;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListCapturingFieldComparatorGuardRejected) {
  const auto Source = tmpFile("forward-list-capturing-field-comparator-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Item { int rank; };
int main() {
  std::forward_list<Item> values{{7}, {2}};
  bool descending = true;
  values.sort([descending](const Item& left, const Item& right) {
    return descending ? left.rank > right.rank : left.rank < right.rank;
  });
  return values.front().rank != 7;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListRecordUniqueUsesEqualityOperatorRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-unique-uses-equality-operator.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry { short code; short tag; };
bool operator==(const Entry& left, const Entry& right) { return left.code == right.code; }
int main() {
  std::list<Entry> values{{2, 11}, {2, 13}, {5, 17}, {2, 19}};
  auto first = values.begin();
  values.unique();
  auto item = values.begin();
  if (values.size() != 3 || item != first || item->tag != 11) return 1;
  return (++item)->tag != 17 || (++item)->tag != 19 || ++item != values.end();
}
)r19containers");
  const auto Output = tmpFile("list-record-unique-uses-equality-operator.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-unique-uses-equality-operator" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListRecordUniqueUsesEqualityOperatorRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-record-unique-uses-equality-operator.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry { int code; int tag; };
bool operator==(const Entry& left, const Entry& right) { return left.code == right.code; }
int main() {
  std::forward_list<Entry> values{{2, 11}, {2, 13}, {5, 17}, {5, 19}};
  const auto* first = &values.front();
  values.unique();
  auto item = values.begin();
  if (&*item != first || item->tag != 11) return 1;
  return (++item)->tag != 17 || ++item != values.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-unique-uses-equality-operator.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-unique-uses-equality-operator" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordOwnerOrderingRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-owner-ordering.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry { int rank; int tag; };
bool operator<(const Entry& left, const Entry& right) { return left.rank > right.rank; }
int main() {
  const std::list<Entry> left{{9, 7}, {4, 5}}, right{{9, 2}, {2, 8}};
  const std::list<Entry> same{{9, 3}, {4, 6}}, prefix{{9, 0}};
  return !(left < right) || !(right > left) || right < left || left >= right ||
         !(prefix < left) || left < same || !(left <= same) || !(left >= same);
}
)r19containers");
  const auto Output = tmpFile("list-record-owner-ordering.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-owner-ordering" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListRecordOwnerOrderingRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-record-owner-ordering.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry { unsigned long long rank; int tag; };
bool operator<(const Entry& left, const Entry& right) { return left.rank < right.rank; }
int main() {
  const std::forward_list<Entry> left{{1, 7}, {4, 5}}, right{{1, 2}, {6, 8}};
  const std::forward_list<Entry> empty, same{{1, 3}, {4, 6}}, prefix{{1, 0}};
  return !(left < right) || !(right > left) || right < left || left >= right ||
         !(empty < left) || !(prefix < left) || left < same ||
         !(left <= same) || !(left >= same);
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-owner-ordering.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-owner-ordering" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListRecordEnumFieldOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-record-enum-field-order.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
enum class Rank : signed char { Early = -3, Late = 4 };
struct Entry { Rank rank; int tag; };
bool operator<(const Entry& left, const Entry& right) { return left.rank < right.rank; }
int main() {
  std::forward_list<Entry> left{{Rank::Late, 1}, {Rank::Early, 2}};
  std::forward_list<Entry> right{{Rank::Early, 3}, {Rank::Late, 4}};
  auto original = left.begin();
  left.sort();
  left.merge(right);
  auto item = left.begin();
  return !right.empty() || item->tag != 2 || (++item)->tag != 3 ||
         ++item != original || item->tag != 1 || (++item)->tag != 4 || ++item != left.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-enum-field-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-enum-field-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListRecordDoubleFieldEqualityRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-record-double-field-equality.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry { double key; int tag; };
bool operator==(const Entry& left, const Entry& right) { return left.key == right.key; }
int main() {
  std::forward_list<Entry> data{{-0.0, 7}, {0.0, 9}, {3.5, 5}, {3.5, 6}};
  data.unique();
  const std::forward_list<Entry> same{{0.0, 0}, {3.5, 0}};
  auto item = data.begin();
  return data != same || item->tag != 7 || (++item)->tag != 5 || ++item != data.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-double-field-equality.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-double-field-equality" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListOwnersUseConstRecordMemberEquality) {
  const auto Source = tmpFile("list-record-member-owner-equality-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry {
  int key;
  bool operator==(const Entry& other) const { return key == other.key; }
};
int main() { return std::list<Entry>{{1}} == std::list<Entry>{{2}}; }
)r19containers");
  const auto Output = tmpFile("list-record-member-owner-equality-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-member-owner-equality-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListOwnersUseCompoundRecordOrder) {
  const auto Source = tmpFile("forward-list-record-compound-owner-order-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry { int key; int tag; };
bool operator<(const Entry& left, const Entry& right) {
  return left.key < right.key || (left.key == right.key && left.tag < right.tag);
}
int main() { return std::forward_list<Entry>{{1, 2}} < std::forward_list<Entry>{{1, 3}}; }
)r19containers");
  const auto Output = tmpFile("forward-list-record-compound-owner-order-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-compound-owner-order-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 1) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListRecordPointerFieldOrderGuardRejected) {
  const auto Source = tmpFile("forward-list-record-pointer-field-order-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry { int* key; int tag; };
bool operator<(const Entry& left, const Entry& right) { return left.key < right.key; }
int main() {
  int keys[2]{};
  std::forward_list<Entry> data{{keys + 1, 2}, {keys, 1}};
  data.sort();
  return data.front().tag != 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListRecordLiteralDefaultInsertionRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-record-literal-default-insertion.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
enum class Tag : unsigned char { Default = 7 };
struct Entry { Tag tag = Tag::Default; bool state = true; const int* key = nullptr; };
int main() {
  std::forward_list<Entry> values(2);
  values.resize(3);
  auto p = values.emplace_after(values.before_begin());
  p->state = false;
  values.emplace_front();
  auto i = values.begin();
  if (i->tag != Tag::Default || !i->state || i->key || ++i != p || i->state) return 1;
  for (; i != values.end(); ++i)
    if (i->tag != Tag::Default || i->key) return 2;
  return 0;
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-literal-default-insertion.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-literal-default-insertion" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListRecordLiteralDefaultCopyRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-record-literal-default-copy.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry { int key = 7; double value = 0.5; };
int main() {
  Entry source[]{{3}, {2, 0.25}, {}};
  std::forward_list<Entry> values(source, source + 3);
  auto copy = values;
  values.clear();
  auto i = copy.begin();
  if (i->key != 3 || i->value != 0.5) return 1;
  if ((++i)->key != 2 || i->value != 0.25) return 2;
  return (++i)->key != 7 || i->value != 0.5 || ++i != copy.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-literal-default-copy.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-literal-default-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRunsDirectMemberDefaultCalls) {
  const auto Source = tmpFile("list-record-effectful-default-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int counter = 0;
int next() { return ++counter; }
struct Entry { int value = next(); };
int main() { std::list<Entry> values(2); return values.back().value != 2; }
)r19containers");
  const auto Output = tmpFile("list-record-effectful-default-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-effectful-default-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListRecordMemberOwnerEqualityRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-record-member-owner-equality.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry {
  unsigned key;
  int tag;
  bool operator==(const Entry& other) const { return other.key == this->key; }
};
int main() {
  const std::forward_list<Entry> left{{1, 6}, {2, 7}}, same{{1, 8}, {2, 9}};
  const std::forward_list<Entry> prefix{{1, 0}}, different{{1, 0}, {3, 0}};
  return !(left == same) || left != same || left == prefix || !(left != different);
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-member-owner-equality.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-member-owner-equality" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordMemberRemoveAndUniqueRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-member-remove-and-unique.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry {
  int key;
  int tag;
  bool operator==(const Entry& rhs) const { return key == rhs.key; }
};
int main() {
  std::list<Entry> values{{1, 7}, {2, 5}, {2, 6}, {1, 9}, {3, 8}};
  const auto& removed = values.front();
  auto kept = values.begin();
  ++kept;
  values.remove(removed);
  values.unique();
  return values.size() != 2 || values.begin() != kept || kept->tag != 5 || values.back().tag != 8;
}
)r19containers");
  const auto Output = tmpFile("list-record-member-remove-and-unique.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-member-remove-and-unique" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListRecordMemberRemoveAndUniqueRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-record-member-remove-and-unique.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry {
  short key;
  int tag;
  bool operator==(const Entry& rhs) const { return rhs.key == key; }
};
int main() {
  std::forward_list<Entry> data{{1, 7}, {2, 5}, {2, 6}, {1, 9}, {3, 8}};
  const auto& removed = data.front();
  auto kept = data.begin();
  ++kept;
  data.remove(removed);
  data.unique();
  auto it = data.begin();
  return it != kept || it->tag != 5 || (++it)->tag != 8 || ++it != data.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-member-remove-and-unique.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-member-remove-and-unique" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListSortUsesCompoundRecordMemberOrder) {
  const auto Source = tmpFile("forward-list-record-compound-member-sort-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry {
  int key;
  int tag;
  bool operator<(const Entry& rhs) const {
    return key < rhs.key || (key == rhs.key && tag < rhs.tag);
  }
};
int main() {
  std::forward_list<Entry> values{{1, 2}, {1, 1}};
  values.sort();
  return values.front().tag != 1;
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-compound-member-sort-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-compound-member-sort-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListNormalizesIntegralRecordRemoval) {
  const auto Source = tmpFile("list-record-integer-member-removal-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry {
  int key;
  int operator==(const Entry& rhs) const { return key == rhs.key; }
};
int main() {
  std::list<Entry> values{{2}, {3}};
  values.remove(Entry{2});
  return values.size() != 1;
}
)r19containers");
  const auto Output = tmpFile("list-record-integer-member-removal-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-integer-member-removal-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListRecordByValueStableMergeRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-record-by-value-stable-merge.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Row { short key; int marks[2]; };
bool before(Row left, Row right) { return left.key < right.key; }
int main() {
  std::forward_list<Row> left{{1, {5, 7}}, {3, {6, 8}}};
  std::forward_list<Row> right{{1, {2, 9}}, {4, {3, 10}}};
  auto saved = right.begin();
  left.merge(right, &before);
  auto it = left.begin();
  if (!right.empty() || it->marks[1] != 7) return 1;
  ++it;
  if (it != saved || it->marks[1] != 9) return 2;
  ++it;
  return it->key != 3 || it->marks[0] != 6;
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-by-value-stable-merge.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-by-value-stable-merge" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordEffectfulMemberOrderGuardRejected) {
  const auto Source = tmpFile("list-record-effectful-member-order-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int comparisons;
struct Entry {
  int rank;
  bool operator<(const Entry& other) const {
    return ++comparisons && rank < other.rank;
  }
};
int main() {
  std::list<Entry> values{{4}, {2}};
  values.sort();
  return values.front().rank != 2;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListRecordByValueEffectfulCopyGuardRejected) {
  const auto Source = tmpFile("forward-list-record-by-value-effectful-copy-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int copies;
struct Row {
  int key;
  Row(int value) : key(value) {}
  Row(const Row& other) : key(other.key) { ++copies; }
};
bool before(Row left, Row right) { return left.key < right.key; }
int main() {
  std::forward_list<Row> rows{{2}, {1}};
  rows.sort(&before);
  return copies < 2;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListRecordDestructorAfterSpliceAndEraseRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-destructor-after-splice-and-erase.cpp");
  writeFile(Source, R"r19containers(#include <list>
int trace;
struct Entry {
  int value;
  ~Entry() { trace = trace * 10 + value; }
};
int main() {
  {
    std::list<Entry> from;
    std::list<Entry> to;
    from.emplace_back().value = 2;
    from.emplace_back().value = 3;
    auto saved = from.begin();
    to.splice(to.begin(), from, saved);
    if (trace || saved != to.begin()) return 1;
    to.erase(saved);
    if (trace != 2 || !to.empty()) return 2;
  }
  return trace != 23;
}
)r19containers");
  const auto Output = tmpFile("list-record-destructor-after-splice-and-erase.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-destructor-after-splice-and-erase" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordPotentiallyThrowingDestructorGuardRejected) {
  const auto Source = tmpFile("list-record-potentially-throwing-destructor-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry {
  int value;
  ~Entry() noexcept(false) { value = 0; }
};
int main() {
  std::list<Entry> rows;
  rows.emplace_back().value = 9;
  return rows.back().value != 9;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListRecordDependentDefaultResizeRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-dependent-default-resize.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry { int seed = 3; int total = seed * 2; int delta = total - 1; };
int main() {
  std::list<Entry> rows(1);
  auto retained = rows.begin();
  retained->seed = 10;
  rows.resize(3);
  if (retained != rows.begin() || retained->seed != 10 || retained->delta != 5) return 1;
  return rows.back().seed != 3 || rows.back().total != 6 || rows.back().delta != 5;
}
)r19containers");
  const auto Output = tmpFile("list-record-dependent-default-resize.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-dependent-default-resize" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordDependentDefaultAggregateInputRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-dependent-default-aggregate-input.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry { int first = 3; int second = first + 4; };
int main() {
  Entry source[]{{9}, {}};
  std::list<Entry> rows(source, source + 2);
  return rows.front().second != 13 || rows.back().second != 7;
}
)r19containers");
  const auto Output = tmpFile("list-record-dependent-default-aggregate-input.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-dependent-default-aggregate-input" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListRecordIntegralUniqueResultRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-record-integral-unique-result.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry {
  int key;
  int id;
  unsigned char operator==(const Entry& other) const { return key == other.key; }
};
int main() {
  std::forward_list<Entry> values{{2, 5}, {2, 6}, {3, 7}, {3, 8}};
  values.unique();
  auto it = values.begin();
  if (it->id != 5 || (++it)->id != 7) return 1;
  return ++it != values.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-integral-unique-result.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-integral-unique-result" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordMutatingDefaultGuardRejected) {
  const auto Source = tmpFile("list-record-mutating-default-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry { int first = 4; int second = ++first; };
int main() {
  std::list<Entry> rows(2);
  return rows.front().first != 5 || rows.back().second != 5;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListRecordEffectfulIntegralComparisonResultGuardRejected) {
  const auto Source = tmpFile("list-record-effectful-integral-comparison-result-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int comparisons;
struct Entry {
  int key;
  int operator<(const Entry& other) const { return ++comparisons && key < other.key; }
};
int main() {
  std::list<Entry> values{{3}, {1}};
  values.sort();
  return values.front().key != 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListRecordDefaultCallEmplacementRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-record-default-call-emplacement.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int calls;
int next() { return ++calls; }
struct Entry { int id = next() + 4; int tag = next(); };
int main() {
  std::forward_list<Entry> rows(1);
  auto first = rows.begin();
  if (first->id != 5 || first->tag != 2) return 1;
  auto second = rows.emplace_after(first);
  rows.emplace_front();
  return calls != 6 || rows.front().id != 9 || rows.front().tag != 6 ||
         second->id != 7 || second->tag != 4 || first->id != 5;
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-default-call-emplacement.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-default-call-emplacement" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListRunsDirectCallsWithDefaultArgument) {
  const auto Source = tmpFile("forward-list-record-default-call-argument-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int next(int count = 1) { return count; }
struct Entry { int id = next(); };
int main() {
  std::forward_list<Entry> rows(1);
  return rows.front().id != 1;
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-default-call-argument-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-default-call-argument-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordDependentDefaultAndDestructionRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-dependent-default-and-destruction.cpp");
  writeFile(Source, R"r19containers(#include <list>
int seen;
struct Entry {
  int seed = 3;
  int tag = seed + 4;
  ~Entry() noexcept { seen = seen * 10 + tag; }
};
int main() {
  {
    std::list<Entry> rows(2);
    rows.front().tag = 8;
    rows.pop_front();
    if (seen != 8 || rows.front().tag != 7) return 1;
  }
  return seen != 87;
}
)r19containers");
  const auto Output = tmpFile("list-record-dependent-default-and-destruction.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-dependent-default-and-destruction" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordDefaultEmplacementSpliceAndDestructionRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-default-emplacement-splice-and-destruction.cpp");
  writeFile(Source, R"r19containers(#include <list>
int calls, released;
int next() { return ++calls; }
struct Entry {
  int tag = next() + 4;
  ~Entry() noexcept { released += tag; }
};
int main() {
  std::list<Entry> left, right;
  auto& item = left.emplace_back();
  right.splice(right.end(), left, left.begin());
  if (!left.empty() || &right.front() != &item || item.tag != 5 || calls != 1 || released) return 1;
  right.erase(right.begin());
  return released != 5 || calls != 1 || !right.empty();
}
)r19containers");
  const auto Output = tmpFile("list-record-default-emplacement-splice-and-destruction.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-default-emplacement-splice-and-destruction" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordFloatDefaultSortRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-float-default-sort.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry {
  int key;
  int id;
  float operator<(const Entry& other) const { return key < other.key; }
};
int main() {
  std::list<Entry> rows{{4, 0}, {2, 1}, {2, 2}, {1, 3}};
  const auto moved = rows.begin();
  rows.sort();
  auto item = rows.begin();
  if (item->id != 3 || (++item)->id != 1 || (++item)->id != 2) return 1;
  return ++item != moved || item->id != 0;
}
)r19containers");
  const auto Output = tmpFile("list-record-float-default-sort.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-float-default-sort" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordDoubleRemovalResultRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-double-removal-result.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry {
  int key;
  double operator==(const Entry& other) const { return key == other.key; }
};
int main() {
  std::list<Entry> rows{{1}, {2}, {3}, {2}};
  auto first = rows.begin();
  rows.remove(Entry{2});
  return rows.size() != 2 || rows.begin() != first || rows.back().key != 3;
}
)r19containers");
  const auto Output = tmpFile("list-record-double-removal-result.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-double-removal-result" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListConstructsAndDestroysNestedRecordsAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-nested-record-default-and-destruction-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int released;
struct Child { int value = 4; ~Child() noexcept { released += value; } };
struct Entry { int key = 2; Child child; };
int main() {
  std::forward_list<Entry> rows(1);
  rows.clear();
  return released != 4;
}
)r19containers");
  const auto Output = tmpFile("forward-list-nested-record-default-and-destruction-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-nested-record-default-and-destruction-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordEffectfulFloatComparisonResultGuardRejected) {
  const auto Source = tmpFile("list-record-effectful-float-comparison-result-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int comparisons;
struct Entry {
  int key;
  float operator<(const Entry& other) const { return ++comparisons && key < other.key; }
};
int main() {
  std::list<Entry> rows{{3}, {1}};
  rows.sort();
  return rows.front().key != 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListDefaultConstructionCountAndResizeRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-default-construction-count-and-resize.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int calls, destroyed;
int next() { return ++calls; }
struct Entry {
  int value = next();
  ~Entry() noexcept { destroyed += value; }
};
int main() {
  std::forward_list<Entry> empty(0);
  empty.resize(0);
  if (calls || destroyed || !empty.empty()) return 1;
  std::forward_list<Entry> rows(2);
  auto first = rows.begin();
  auto second = first;
  ++second;
  rows.resize(2);
  if (calls != 2 || first->value != 1 || second->value != 2) return 2;
  rows.resize(1);
  if (calls != 2 || destroyed != 2 || first != rows.begin()) return 3;
  rows.resize(3);
  rows.clear();
  return calls != 4 || destroyed != 10 || !rows.empty();
}
)r19containers");
  const auto Output = tmpFile("forward-list-default-construction-count-and-resize.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-default-construction-count-and-resize" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListDefaultEmplacementAndExplicitFillRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-default-emplacement-and-explicit-fill.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int calls;
int next() { return ++calls; }
struct Entry { int id = next(); int data = id + 20; };
int main() {
  Entry value{9};
  std::forward_list<Entry> rows(1, value);
  rows.resize(2, value);
  auto explicit_value = rows.begin();
  auto second = explicit_value;
  ++second;
  if (calls || explicit_value->data != 29 || second->data != 29) return 1;
  auto& first = rows.emplace_front();
  auto inserted = rows.emplace_after(explicit_value);
  return calls != 2 || first.data != 21 || explicit_value->data != 29 || inserted->data != 22;
}
)r19containers");
  const auto Output = tmpFile("forward-list-default-emplacement-and-explicit-fill.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-default-emplacement-and-explicit-fill" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListNodeDefaultWritingMemberGuardRejected) {
  const auto Source = tmpFile("forward-list-node-default-writing-member-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry { int id = 3; int previous = id++; };
int main() {
  std::forward_list<Entry> rows;
  rows.resize(2);
  return rows.front().previous != 3;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListRecordDefaultCallNestedScalarRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-record-default-call-nested-scalar.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int calls;
int offset(int amount) { return amount + ++calls; }
int tag(int id, bool odd = true) { return id * 10 + odd; }
struct Entry { int checksum = tag(offset(2)); };
int main() {
  std::forward_list<Entry> rows(2);
  auto first = rows.begin();
  auto second = first;
  ++second;
  if (calls != 2 || first->checksum != 31 || second->checksum != 41) return 1;
  rows.resize(2);
  auto third = rows.emplace_after(second);
  return calls != 3 || third->checksum != 51 || first != rows.begin();
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-default-call-nested-scalar.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-default-call-nested-scalar" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordDefaultCallReferenceArgumentGuardRejected) {
  const auto Source = tmpFile("list-record-default-call-reference-argument-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int change(int& value) { return ++value; }
struct Entry { int id = 1; int checksum = change(id); };
int main() {
  std::list<Entry> rows(1);
  return rows.front().id != 2 || rows.front().checksum != 2;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListRecordSelectedDefaultCallPerNodeRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-selected-default-call-per-node.cpp");
  writeFile(Source, R"r19containers(#include <list>
int calls;
int seed() { return ++calls; }
int tag(int value = seed()) { return value; }
struct Entry { int id = tag(); int checksum = id + 10; };
int main() {
  std::list<Entry> rows(2);
  auto first = rows.begin();
  if (calls != 2 || first->checksum != 11 || rows.back().checksum != 12) return 1;
  rows.resize(2);
  rows.resize(1);
  if (calls != 2 || first != rows.begin()) return 1;
  auto& last = rows.emplace_back();
  return calls != 3 || last.checksum != 13;
}
)r19containers");
  const auto Output = tmpFile("list-record-selected-default-call-per-node.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-selected-default-call-per-node" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordNestedSelectedDefaultArgumentsRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-nested-selected-default-arguments.cpp");
  writeFile(Source, R"r19containers(#include <list>
int calls;
int seed() { return ++calls; }
int code(int base = seed()) { return base * 10; }
int stamp(int value = code(), int suffix = code()) { return value + suffix; }
struct Entry { int checksum = stamp(); };
int main() {
  std::list<Entry> rows;
  rows.emplace_back();
  rows.emplace_front();
  if (calls != 4 || rows.front().checksum != 70 || rows.back().checksum != 30) return 1;
  return rows.emplace_back().checksum != 110 || calls != 6;
}
)r19containers");
  const auto Output = tmpFile("list-record-nested-selected-default-arguments.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-nested-selected-default-arguments" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListRecordSelectedDefaultReferenceGuardRejected) {
  const auto Source = tmpFile("forward-list-record-selected-default-reference-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int seed = 3;
int tag(const int& value = seed) { return value; }
struct Entry { int checksum = tag(); };
int main() {
  std::forward_list<Entry> rows(1);
  return rows.front().checksum != 3;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListNestedArrayDefaultAndDestroyOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-nested-array-default-and-destroy-order.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int sequence, released;
int next() { return ++sequence; }
struct Leaf {
  int ticket = next();
  ~Leaf() noexcept { released = released * 10 + ticket; }
};
struct Entry { Leaf fields[2]; int marker = next(); };
int main() {
  std::forward_list<Entry> rows;
  auto& item = rows.emplace_front();
  if (sequence != 3 || item.fields[0].ticket != 1 || item.fields[1].ticket != 2 ||
      item.marker != 3 || released) return 1;
  rows.clear();
  return released != 21;
}
)r19containers");
  const auto Output = tmpFile("forward-list-nested-array-default-and-destroy-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-nested-array-default-and-destroy-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListConstructsNestedScalarOnlyDefaultRecordAtBothOptimizations) {
  const auto Source = tmpFile("list-nested-user-provided-default-constructor-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Leaf { int value; Leaf() : value(7) {} };
struct Entry { Leaf field; };
int main() {
  std::list<Entry> rows(1);
  return rows.front().field.value != 7;
}
)r19containers");
  const auto Output = tmpFile("list-nested-user-provided-default-constructor-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-nested-user-provided-default-constructor-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListDirectRecordDefaultConstructorRunAtBothOptimizations) {
  const auto Source = tmpFile("list-direct-record-default-constructor.cpp");
  writeFile(Source, R"r19containers(#include <list>
int sequence;
struct Entry {
  int first, second;
  explicit Entry() noexcept : first(++sequence), second(first + 10) { second += 2; }
};
int main() {
  std::list<Entry> rows(2);
  if (sequence != 2 || rows.front().first != 1 || rows.back().second != 14) return 1;
  rows.resize(3);
  return sequence != 3 || rows.back().first != 3 || rows.back().second != 15;
}
)r19containers");
  const auto Output = tmpFile("list-direct-record-default-constructor.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-direct-record-default-constructor" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListUserDefaultConstructorFillCopyRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-user-default-constructor-fill-copy.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int sequence;
struct Entry {
  int value;
  Entry() noexcept;
};
Entry::Entry() noexcept : value(++sequence) {}
int main() {
  Entry seed;
  std::forward_list<Entry> rows(2, seed);
  if (sequence != 1 || rows.front().value != 1) return 1;
  rows.emplace_front();
  return sequence != 2 || rows.front().value != 2;
}
)r19containers");
  const auto Output = tmpFile("forward-list-user-default-constructor-fill-copy.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-user-default-constructor-fill-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListUserDefaultConstructorParameterGuardRejected) {
  const auto Source = tmpFile("list-user-default-constructor-parameter-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry { int value; explicit Entry(int n = 7) noexcept : value(n) {} };
int main() {
  std::list<Entry> rows(1);
  return rows.front().value != 7;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListPlainDefaultCtorEarlierScalarMemberRunAtBothOptimizations) {
  const auto Source = tmpFile("list-plain-default-ctor-earlier-scalar-member.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry {
  int key, derived;
  Entry() : key(4), derived(key * 3 + 1) {}
};
int main() {
  std::list<Entry> rows(2);
  auto& first = rows.emplace_front();
  if (rows.size() != 3 || first.key != 4 || first.derived != 13) return 1;
  rows.resize(4);
  return rows.back().derived != 13;
}
)r19containers");
  const auto Output = tmpFile("list-plain-default-ctor-earlier-scalar-member.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-plain-default-ctor-earlier-scalar-member" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListPlainDefaultCtorOutOfLineFillRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-plain-default-ctor-out-of-line-fill.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry {
  int value;
  Entry();
};
Entry::Entry() : value(7) {}
int main() {
  Entry seed;
  seed.value = 9;
  std::forward_list<Entry> rows(2, seed);
  if (rows.front().value != 9) return 1;
  rows.emplace_front();
  return rows.front().value != 7;
}
)r19containers");
  const auto Output = tmpFile("forward-list-plain-default-ctor-out-of-line-fill.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-plain-default-ctor-out-of-line-fill" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListPlainDefaultCtorInitializerCallGuardRejected) {
  const auto Source = tmpFile("forward-list-plain-default-ctor-initializer-call-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int value() { return 7; }
struct Entry {
  int field;
  Entry() : field(value()) {}
};
int main() {
  std::forward_list<Entry> rows(1);
  return rows.front().field != 7;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListTwoScalarEmplaceAfterRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-two-scalar-emplace-after.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry {
  int first, second;
  Entry() noexcept : first(0), second(0) {}
  Entry(int a, int b) noexcept : first(a), second(b) {}
};
int main() {
  std::forward_list<Entry> rows;
  const int b = 8;
  auto out = rows.emplace_after(rows.before_begin(), 3, b);
  auto next = out;
  ++next;
  return out != rows.begin() || out->first != 3 || out->second != 8 || next != rows.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-two-scalar-emplace-after.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-two-scalar-emplace-after" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListTwoScalarEmplaceWithDestructionRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-two-scalar-emplace-with-destruction.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int calls, removed;
int next() { return ++calls; }
struct Entry {
  int value;
  Entry() noexcept : value(0) {}
  Entry(int a, int b) noexcept : value(a + b) {}
  ~Entry() noexcept { removed += value; }
};
int main() {
  std::forward_list<Entry> rows;
  auto& value = rows.emplace_front(next(), 5);
  if (calls != 1 || value.value != 6 || removed) return 1;
  rows.pop_front();
  return calls != 1 || removed != 6;
}
)r19containers");
  const auto Output = tmpFile("forward-list-two-scalar-emplace-with-destruction.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-two-scalar-emplace-with-destruction" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListTwoEmplaceConversionGuardRejected) {
  const auto Source = tmpFile("forward-list-two-emplace-conversion-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry {
  int value;
  Entry() noexcept : value(0) {}
  Entry(long a, int b) noexcept : value(static_cast<int>(a) + b) {}
};
int main() {
  std::forward_list<Entry> rows;
  return rows.emplace_front(4, 3).value != 7;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
