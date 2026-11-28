
TEST_F(TranslateTest, CoreV2BitsetReferenceMaskedWordsRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-reference-masked-words.cpp");
  const auto Output = tmpFile("bitset-reference-masked-words.nc");
  writeFile(Source, R"r19bitref(#include <bitset>
int main(){std::bitset<65> b;auto lo=b[0];auto hi=b[64];
lo=true;hi=true;if(b.count()!=2||!bool(lo)||!bool(hi)||~lo)return 1;
lo=false;hi.flip();return b.any()||bool(lo)||bool(hi)||!~lo;}
)r19bitref");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-reference-masked-words" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetReferenceAssignmentReferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-reference-assignment-reference.cpp");
  const auto Output = tmpFile("bitset-reference-assignment-reference.nc");
  writeFile(Source, R"r19bitref(#include <bitset>
int main(){std::bitset<129> b;auto a=b[1];auto c=b[65];c=true;
auto& r=(a=c);r=false;if(b.test(1)||!b.test(65))return 1;
auto& s=(a=true);s.flip();if(bool(a)||b.count()!=1)return 2;
a=a;if(bool(a))return 3;c=c;return !bool(c)||b.count()!=1;}
)r19bitref");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-reference-assignment-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetReferenceSwapAliasesRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-reference-swap-aliases.cpp");
  const auto Output = tmpFile("bitset-reference-swap-aliases.nc");
  writeFile(Source, R"r19bitref(#include <bitset>
int main(){std::bitset<65> a;std::bitset<129> b;a.set(0);b.set(64);
using std::swap;swap(a[0],b[128]);if(a.any()||b.count()!=2||!b.test(128))return 1;
a.set(64);swap(a[63],b[64]);if(a.count()!=2||b.count()!=1||!a.test(63))return 2;
swap(a[0],a[64]);swap(a[63],a[63]);return a.count()!=2||!a.test(0)||a.test(64)||!a.test(63)||!b.test(128);}
)r19bitref");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-reference-swap-aliases" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedEmptyReceiverRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-unsigned-empty-receiver.cpp");
  const auto Output = tmpFile("bitset-unsigned-empty-receiver.nc");
  writeFile(Source, R"r19bitconv(#include <bitset>
static int calls;
std::bitset<0>& observe(std::bitset<0>& b){++calls;return b;}
int main(){std::bitset<0> b;auto l=observe(b).to_ulong();auto ll=observe(b).to_ullong();
return l!=0ul||ll!=0ull||calls!=2;}
)r19bitconv");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-unsigned-empty-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedUnsignedLongPortableWidthRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-unsigned-unsigned-long-portable-width.cpp");
  const auto Output = tmpFile("bitset-unsigned-unsigned-long-portable-width.nc");
  writeFile(Source, R"r19bitconv(#include <bitset>
int main(){const std::bitset<32> b(~0ull);
return b.to_ulong()!=0xfffffffful;}
)r19bitconv");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-unsigned-unsigned-long-portable-width" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedValueSnapshotRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-unsigned-value-snapshot.cpp");
  const auto Output = tmpFile("bitset-unsigned-value-snapshot.nc");
  writeFile(Source, R"r19bitconv(#include <bitset>
int main(){std::bitset<32> b(8);auto first=b.to_ullong();
b.set(1);auto second=b.to_ulong();b.flip(1);
return first!=8ull||second!=10ul||b.to_ullong()!=8ull;}
)r19bitconv");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-unsigned-value-snapshot" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetBoundedModuloWordBoundaryRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-bounded-modulo-word-boundary.cpp");
  const auto Output = tmpFile("bitset-bounded-modulo-word-boundary.nc");
  writeFile(Source, R"r19bitindex(#include <bitset>
int main(){std::bitset<129> b;std::size_t pos=193;
b.set(pos%129);return b.count()!=1||!b.test(pos%129)||!b.test(64);}
)r19bitindex");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-bounded-modulo-word-boundary" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetBoundedWrappedSizeRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-bounded-wrapped-size.cpp");
  const auto Output = tmpFile("bitset-bounded-wrapped-size.nc");
  writeFile(Source, R"r19bitindex(#include <bitset>
int main(){std::bitset<65> b;std::size_t pos=~std::size_t(0);
b.set(pos%65);if(!b.test(pos%65))return 1;
b.reset(pos%65);return b.any();}
)r19bitindex");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-bounded-wrapped-size" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetBoundedSubexpressionOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-bounded-subexpression-once.cpp");
  const auto Output = tmpFile("bitset-bounded-subexpression-once.nc");
  writeFile(Source, R"r19bitindex(#include <bitset>
static int calls;
std::size_t position(){++calls;return 129;}
int main(){std::bitset<65> b;b.set(position()%65,true);bool on=b.test(position()%65);
b.reset(position()%65);return !on||calls!=3||b.any();}
)r19bitindex");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-bounded-subexpression-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetReferenceOwnedMutableSegmentFieldRejected) {
  const auto Source = tmpFile("bitset-reference-owned-mutable-segment-field.cpp");
  writeFile(Source, R"r19bitref(#define __seg_ nct_foreign_segment
#include <bitset>
#undef __seg_
int main(){std::bitset<65> a;auto r=a[64];r=true;return !bool(r);}
)r19bitref");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetReferenceOwnedReferenceConstructionRejected) {
  const auto Source = tmpFile("bitset-reference-owned-reference-construction.cpp");
  writeFile(Source, R"r19bitref(#define __make_ref nct_foreign_make_ref
#include <bitset>
#undef __make_ref
int main(){std::bitset<65> a;auto r=a[64];r=true;return !bool(r);}
)r19bitref");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetReferenceOwnedConstCarrierRejected) {
  const auto Source = tmpFile("bitset-reference-owned-const-carrier.cpp");
  writeFile(Source, R"r19bitref(#define __bit_const_reference nct_foreign_const_reference
#include <bitset>
#undef __bit_const_reference
int main(){const std::bitset<65> a;return bool(a[0]);}
)r19bitref");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetReferenceOwnedMutableSubscriptRejected) {
  const auto Source = tmpFile("bitset-reference-owned-mutable-subscript.cpp");
  writeFile(Source, R"r19bitref(#include <bitset>
namespace std{template<> auto bitset<65>::operator[](size_t) -> reference{return this->__make_ref(64);}}
int main(){std::bitset<65> a;a[0]=true;return !bool(a[0]);}
)r19bitref");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedWideUlongRejected) {
  const auto Source = tmpFile("bitset-unsigned-wide-ulong.cpp");
  writeFile(Source, R"r19bitconv(#include <bitset>
int main(){std::bitset<65> b(1);return b.to_ulong()!=1ul;}
)r19bitconv");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedSpecializedUlongRejected) {
  const auto Source = tmpFile("bitset-unsigned-specialized-ulong.cpp");
  writeFile(Source, R"r19bitconv(#include <bitset>
namespace std{template<> unsigned long bitset<1>::to_ulong() const{return 7;}}
int main(){std::bitset<1> b;return b.to_ulong()!=7ul;}
)r19bitconv");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedInterceptStorageRejected) {
  const auto Source = tmpFile("bitset-unsigned-intercept-storage.cpp");
  writeFile(Source, R"r19bitconv(#define __first_ nct_foreign_word
#include <bitset>
#undef __first_
int main(){std::bitset<32> b(2);return b.to_ulong()!=2ul;}
)r19bitconv");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedUnownedUlongRejected) {
  const auto Source = tmpFile("bitset-unsigned-unowned-ulong.cpp");
  writeFile(Source, R"r19bitconv(#define to_ulong nct_foreign_ulong
#include <bitset>
#undef to_ulong
int main(){std::bitset<16> b(2);return b.nct_foreign_ulong()!=2ul;}
)r19bitconv");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetBoundedPositionUnsignedWithoutProofRejected) {
  const auto Source = tmpFile("bitset-bounded-position-unsigned-without-proof.cpp");
  writeFile(Source, R"r19bitindex(#include <bitset>
int main(){std::bitset<65> b;std::size_t i=1;return b.test(i);}
)r19bitindex");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetBoundedPositionOversizedMaskRejected) {
  const auto Source = tmpFile("bitset-bounded-position-oversized-mask.cpp");
  writeFile(Source, R"r19bitindex(#include <bitset>
int main(){std::bitset<65> b;std::size_t i=1;b.set(i&127);return 0;}
)r19bitindex");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetBoundedPositionVariableDivisorRejected) {
  const auto Source = tmpFile("bitset-bounded-position-variable-divisor.cpp");
  writeFile(Source, R"r19bitindex(#include <bitset>
int main(){std::bitset<65> b;std::size_t i=1,d=2;b.flip(i%d);return 0;}
)r19bitindex");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetBoundedPositionBoundaryLiteralRejected) {
  const auto Source = tmpFile("bitset-bounded-position-boundary-literal.cpp");
  writeFile(Source, R"r19bitindex(#include <bitset>
int main(){std::bitset<65> b;b.flip(65);return 0;}
)r19bitindex");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetHashZeroSizeRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-hash-zero-size.cpp");
  const auto Output = tmpFile("bitset-hash-zero-size.nc");
  writeFile(Source, R"r19bithash(#include <bitset>
static int calls;
const std::bitset<0>& source(const std::bitset<0>& value){++calls;return value;}
int main(){std::bitset<0> value;std::hash<std::bitset<0>> hash;
return hash(source(value))!=0||calls!=1;}
)r19bithash");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-hash-zero-size" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetHashXorCancellationRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-hash-xor-cancellation.cpp");
  const auto Output = tmpFile("bitset-hash-xor-cancellation.nc");
  writeFile(Source, R"r19bithash(#include <bitset>
int main(){std::hash<std::bitset<129>> hash;
std::bitset<129> a;a.set(0).set(64).set(128);if(hash(a)!=1)return 1;
a.reset(128);if(hash(a)!=0)return 2;a.set(127);return hash(a)!=(1ull<<63);}
)r19bithash");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-hash-xor-cancellation" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetHashReceiverOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-hash-receiver-once.cpp");
  const auto Output = tmpFile("bitset-hash-receiver-once.nc");
  writeFile(Source, R"r19bithash(#include <bitset>
using Hash=std::hash<std::bitset<65>>;
static int order;
Hash* caller(Hash& hash){order=order*10+1;return &hash;}
const std::bitset<65>& operand(const std::bitset<65>& value){order=order*10+2;return value;}
int main(){Hash hash;std::bitset<65> a(3);a.set(64);
return caller(hash)->operator()(operand(a))!=2||order!=12;}
)r19bithash");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-hash-receiver-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetLiteralTextDefaultAndTruncationRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-literal-text-default-and-truncation.cpp");
  const auto Output = tmpFile("bitset-literal-text-default-and-truncation.nc");
  writeFile(Source, R"r19bitlit(#include <bitset>
int main(){const std::bitset<3> a("1011");
return a.count()!=2||!a.test(0)||a.test(1)||!a.test(2);}
)r19bitlit");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-literal-text-default-and-truncation" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetLiteralTextBoundedLengthAndNulRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-literal-text-bounded-length-and-nul.cpp");
  const auto Output = tmpFile("bitset-literal-text-bounded-length-and-nul.nc");
  writeFile(Source, R"r19bitlit(#include <bitset>
int main(){
std::bitset<16> a("101bad",3);std::bitset<16> b("01\0bad",7);
return a.count()!=2||!a.test(2)||!a.test(0)||b.count()!=1||!b.test(0);}
)r19bitlit");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-literal-text-bounded-length-and-nul" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetLiteralTextEqualCharactersRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-literal-text-equal-characters.cpp");
  const auto Output = tmpFile("bitset-literal-text-equal-characters.nc");
  writeFile(Source, R"r19bitlit(#include <bitset>
int main(){std::bitset<5> a("xx",2,'x','x');
return a.count()!=2||!a.test(0)||!a.test(1)||a.test(2);}
)r19bitlit");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-literal-text-equal-characters" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

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

TEST_F(TranslateTest, CoreV2BitsetBoolSwapProxyFirstRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-bool-swap-proxy-first.cpp");
  const auto Output = tmpFile("bitset-bool-swap-proxy-first.nc");
  writeFile(Source, R"r19bitmix(#include <bitset>
int main(){std::bitset<65> b;bool v=true;using std::swap;
swap(b[64],v);return v||!b.test(64)||b.count()!=1;}
)r19bitmix");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-bool-swap-proxy-first" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetBoolSwapCopiedAliasRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-bool-swap-copied-alias.cpp");
  const auto Output = tmpFile("bitset-bool-swap-copied-alias.nc");
  writeFile(Source, R"r19bitmix(#include <bitset>
int main(){std::bitset<129> b;auto original=b[128];auto alias=original;
bool v=true;using std::swap;swap(alias,v);if(v||!bool(original))return 1;
swap(v,original);return !v||b.any()||bool(alias);}
)r19bitmix");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-bool-swap-copied-alias" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetBoolSwapCaptureOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-bool-swap-capture-once.cpp");
  const auto Output = tmpFile("bitset-bool-swap-capture-once.nc");
  writeFile(Source, R"r19bitmix(#include <bitset>
static int events;
std::bitset<65>& observe(std::bitset<65>& b){events+=10;return b;}
bool& reference(bool& v){++events;return v;}
int main(){std::bitset<65> b;b.set(63);bool v=false;using std::swap;
swap(observe(b)[63],reference(v));return !v||b.any()||events!=11;}
)r19bitmix");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-bool-swap-capture-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetHashWordStorageRejected) {
  const auto Source = tmpFile("bitset-hash-word-storage.cpp");
  writeFile(Source, R"r19bithash(#define __first_ nct_foreign_hash_storage
#include <bitset>
#undef __first_
int main(){std::bitset<65> bits(3);return std::hash<std::bitset<65>>{}(bits)!=3;}
)r19bithash");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetHashHashImplementationRejected) {
  const auto Source = tmpFile("bitset-hash-hash-implementation.cpp");
  writeFile(Source, R"r19bithash(#define __hash_code nct_foreign_hash_code
#include <bitset>
#undef __hash_code
int main(){std::bitset<65> bits(3);return std::hash<std::bitset<65>>{}(bits)!=3;}
)r19bithash");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetHashHashCarrierRejected) {
  const auto Source = tmpFile("bitset-hash-hash-carrier.cpp");
  writeFile(Source, R"r19bithash(#define hash nct_foreign_hash
#include <bitset>
#undef hash
int main(){std::bitset<65> bits(3);return std::nct_foreign_hash<std::bitset<65>>{}(bits)!=3;}
)r19bithash");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetHashSpecializedOperatorRejected) {
  const auto Source = tmpFile("bitset-hash-specialized-operator.cpp");
  writeFile(Source, R"r19bithash(#include <bitset>
namespace std{template<> size_t hash<bitset<65>>::operator()(const bitset<65>&) const noexcept{return 5;}}
int main(){std::bitset<65> bits(3);return std::hash<std::bitset<65>>{}(bits)!=3;}
)r19bithash");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetLiteralTextInvalidDigitRejected) {
  const auto Source = tmpFile("bitset-literal-text-invalid-digit.cpp");
  writeFile(Source, R"r19bitlit(#include <bitset>
int main(){std::bitset<8> a("10x");return a.any();}
)r19bitlit");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetLiteralTextDynamicPointerRejected) {
  const auto Source = tmpFile("bitset-literal-text-dynamic-pointer.cpp");
  writeFile(Source, R"r19bitlit(#include <bitset>
int main(){const char* p="01";std::bitset<8> a(p);return a.any();}
)r19bitlit");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetLiteralTextDynamicDigitRejected) {
  const auto Source = tmpFile("bitset-literal-text-dynamic-digit.cpp");
  writeFile(Source, R"r19bitlit(#include <bitset>
int main(){char digit='x';std::bitset<8> a("ox",2,'o',digit);return a.any();}
)r19bitlit");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetLiteralTextOwnedDecoderRejected) {
  const auto Source = tmpFile("bitset-literal-text-owned-decoder.cpp");
  writeFile(Source, R"r19bitlit(#define __init_from_string_view nct_foreign_text_init
#include <bitset>
#undef __init_from_string_view
int main(){std::bitset<8> a("01");return a.any();}
)r19bitlit");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
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

TEST_F(TranslateTest, CoreV2BitsetBoolSwapGuardSegmentRejected) {
  const auto Source = tmpFile("bitset-bool-swap-guard-segment.cpp");
  writeFile(Source, R"r19bitmix(#define __seg_ nct_alternate_seg
#include <bitset>
#undef __seg_
int main(){std::bitset<65> b;bool value=true;using std::swap;swap(b[64],value);return value;}
)r19bitmix");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetBoolSwapGuardMakeReferenceRejected) {
  const auto Source = tmpFile("bitset-bool-swap-guard-make-reference.cpp");
  writeFile(Source, R"r19bitmix(#define __make_ref nct_alternate_make_ref
#include <bitset>
#undef __make_ref
int main(){std::bitset<65> b;bool value=true;using std::swap;swap(b[64],value);return value;}
)r19bitmix");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetBoolSwapGuardStorageBaseRejected) {
  const auto Source = tmpFile("bitset-bool-swap-guard-storage-base.cpp");
  writeFile(Source, R"r19bitmix(#define __bitset nct_alternate_bitset
#include <bitset>
#undef __bitset
int main(){std::bitset<65> b;bool value=true;using std::swap;swap(b[64],value);return value;}
)r19bitmix");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetBoolSwapGuardOverloadNameRejected) {
  const auto Source = tmpFile("bitset-bool-swap-guard-overload-name.cpp");
  writeFile(Source, R"r19bitmix(#define swap nct_alternate_swap
#include <bitset>
#undef swap
int main(){std::bitset<65> b;bool value=true;using std::nct_alternate_swap;nct_alternate_swap(b[64],value);return value;}
)r19bitmix");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryEmptyMultiwordRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-wide-temporary-empty-multiword.cpp");
  const auto Output = tmpFile("bitset-wide-temporary-empty-multiword.nc");
  writeFile(Source, R"r19bitwidechar(#include <bitset>
int main(){return std::bitset<129>{}.to_ulong()!=0UL;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-wide-temporary-empty-multiword" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryKnownNarrowResultRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-wide-temporary-known-narrow-result.cpp");
  const auto Output = tmpFile("bitset-wide-temporary-known-narrow-result.nc");
  writeFile(Source, R"r19bitwidechar(#include <bitset>
int main(){return std::bitset<65>(0xfedcba98ULL).to_ulong()!=0xfedcba98UL;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-wide-temporary-known-narrow-result" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryMaskFitRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-wide-temporary-mask-fit.cpp");
  const auto Output = tmpFile("bitset-wide-temporary-mask-fit.nc");
  writeFile(Source, R"r19bitwidechar(#include <bitset>
static int calls;
unsigned long long word(){++calls;return 0x1020304005060708ULL;}
int main(){auto v=std::bitset<129>(word()&0xffffffffULL).to_ulong();return v!=0x05060708UL||calls!=1;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-wide-temporary-mask-fit" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetValueSwapEmptyRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-value-swap-empty.cpp");
  const auto Output = tmpFile("bitset-value-swap-empty.nc");
  writeFile(Source, R"r19bitvalswap(#include <bitset>
#include <utility>
int main() {
  std::bitset<0> a, b;
  std::swap(a, b);
  return a.size() != 0 || !b.none();
}
)r19bitvalswap");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-value-swap-empty" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetValueSwapMultiwordRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-value-swap-multiword.cpp");
  const auto Output = tmpFile("bitset-value-swap-multiword.nc");
  writeFile(Source, R"r19bitvalswap(#include <bitset>
#include <utility>
int main() {
  std::bitset<129> a, b;
  a.set(128);
  b.set(64).set(0);
  std::swap(a, b);
  return a.count() != 2 || !a.test(64) || !a.test(0) || b.count() != 1 || !b.test(128);
}
)r19bitvalswap");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-value-swap-multiword" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetValueSwapSelfAliasAndEvaluationRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-value-swap-self-alias-and-evaluation.cpp");
  const auto Output = tmpFile("bitset-value-swap-self-alias-and-evaluation.nc");
  writeFile(Source, R"r19bitvalswap(#include <bitset>
#include <utility>
using Bits = std::bitset<65>;
static unsigned left_calls, right_calls;
Bits& left(Bits& value) { ++left_calls; return value; }
Bits& right(Bits& value) { ++right_calls; return value; }
int main() {
  Bits value(1);
  value.set(64);
  std::swap(left(value), right(value));
  return value.count() != 2 || !value.test(0) || !value.test(64) ||
         left_calls != 1 || right_calls != 1;
}
)r19bitvalswap");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-value-swap-self-alias-and-evaluation" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryCopyOfSingleUseObject) {
  const auto Source = tmpFile("bitset-wide-temporary-guard-copy-construction.cpp");
  const auto Output = tmpFile("bitset-wide-temporary-guard-copy-construction.nc");
  writeFile(Source, R"r19bitwidechar(#include <bitset>
int main(){std::bitset<129> b(33);return std::bitset<129>{b}.to_ullong()!=33;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-wide-temporary-guard-copy-construction" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryGuardOverflowingWordRejected) {
  const auto Source = tmpFile("bitset-wide-temporary-guard-overflowing-word.cpp");
  writeFile(Source, R"r19bitwidechar(#include <bitset>
int main(){std::bitset<129> b(33);b.set(128);return b.to_ullong()!=33;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryGuardIndirectReceiverRejected) {
  const auto Source = tmpFile("bitset-wide-temporary-guard-indirect-receiver.cpp");
  writeFile(Source, R"r19bitwidechar(#include <bitset>
int main(){auto* b=new std::bitset<129>{33};auto v=b->to_ullong();delete b;return v!=33;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryGuardRangeDependencyRejected) {
  const auto Source = tmpFile("bitset-wide-temporary-guard-range-dependency.cpp");
  writeFile(Source, R"r19bitwidechar(#define __make_iter nct_alternate_make_iter
#include <bitset>
#undef __make_iter
int main(){return std::bitset<129>{33}.to_ullong()!=33;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryGuardCtzMacroRejected) {
  const auto Source = tmpFile("bitset-wide-temporary-guard-ctz-macro.cpp");
  writeFile(Source, R"r19bitwidechar(int alternate_l(unsigned long value) { return value ? __builtin_ctzl(value) : 0; }
int alternate_ll(unsigned long long value) { return value ? __builtin_ctzll(value) : 0; }
#define __builtin_ctzl alternate_l
#define __builtin_ctzll alternate_ll
#include <bitset>
#undef __builtin_ctzll
#undef __builtin_ctzl
int main() { return std::bitset<129>{33}.to_ullong() != 33; }
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryGuardCompilerCharBitRejected) {
  const auto Source = tmpFile("bitset-wide-temporary-guard-compiler-char-bit.cpp");
  writeFile(Source, R"r19bitwidechar(#include <climits>
#undef __CHAR_BIT__
#define __CHAR_BIT__ 8
#include <bitset>
int main(){return std::bitset<129>{33}.to_ullong()!=33;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetValueSwapGuardSdkTemporaryRejected) {
  const auto Source = tmpFile("bitset-value-swap-guard-sdk-temporary.cpp");
  writeFile(Source, R"r19bitvalswap(#define __t nct_swap_temporary
#include <bitset>
#include <utility>
#undef __t
int main(){std::bitset<65> a(1),b(2);std::swap(a,b);return !a.test(1);}
)r19bitvalswap");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetValueSwapGuardCastCalleeRejected) {
  const auto Source = tmpFile("bitset-value-swap-guard-cast-callee.cpp");
  writeFile(Source, R"r19bitvalswap(#include <bitset>
#include <utility>
using Bits=std::bitset<65>;using Swap=void(*)(Bits&,Bits&) noexcept;
int main(){Bits a(1),b(2);static_cast<Swap>(std::swap<Bits>)(a,b);return !a.test(1);}
)r19bitvalswap");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstLocalSingleWordFitRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-const-local-single-word-fit.cpp");
  const auto Output = tmpFile("bitset-const-local-single-word-fit.nc");
  writeFile(Source, R"r19bitlocal(#include <bitset>
int main() {
  const std::bitset<65> bits{0xfedcba98ULL};
  return bits.to_ulong() != 0xfedcba98UL;
}
)r19bitlocal");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-const-local-single-word-fit" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstLocalOneTimeInitializationRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-const-local-one-time-initialization.cpp");
  const auto Output = tmpFile("bitset-const-local-one-time-initialization.nc");
  writeFile(Source, R"r19bitlocal(#include <bitset>
static unsigned calls;
unsigned long long next() { ++calls; return 0xabcdef0123ULL; }
int main() {
  const std::bitset<129> first(next() & 0xffffffffULL), second(next() % 257ULL);
  const auto a = first.to_ulong();
  const auto b = second.to_ulong();
  return a != 0xcdef0123UL || b != 0xabcdef0123ULL % 257ULL || calls != 2;
}
)r19bitlocal");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-const-local-one-time-initialization" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstLocalConstAliasTypeRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-const-local-const-alias-type.cpp");
  const auto Output = tmpFile("bitset-const-local-const-alias-type.nc");
  writeFile(Source, R"r19bitlocal(#include <bitset>
using Bits = std::bitset<192>;
using Frozen = const Bits;
int main() {
  Frozen bits{123456789ULL};
  return bits.to_ulong() != 123456789UL || bits.to_ullong() != 123456789ULL;
}
)r19bitlocal");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-const-local-const-alias-type" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstantPositionArithmeticRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-constant-position-arithmetic.cpp");
  const auto Output = tmpFile("bitset-constant-position-arithmetic.nc");
  writeFile(Source, R"r19bitice(#include <bitset>
int main() {
  std::bitset<70> bits;
  bits.set(1u << 6);
  bits.flip(35 + 30);
  bits.reset(32 * 2);
  return bits.test(64) || !bits.test(35 + 30) || bits.count() != 1;
}
)r19bitice");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-constant-position-arithmetic" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstantPositionTypeSizeIndexRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-constant-position-type-size-index.cpp");
  const auto Output = tmpFile("bitset-constant-position-type-size-index.nc");
  writeFile(Source, R"r19bitice(#include <bitset>
int main() {
  constexpr std::size_t last = sizeof(unsigned long long) * 8 - 1;
  std::bitset<65> bits;
  bits.flip(last).set(last + 1);
  return !bits.test(last) || !bits.test(last + 1) || bits.count() != 2;
}
)r19bitice");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-constant-position-type-size-index" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstantPositionReceiverAndValueRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-constant-position-receiver-and-value.cpp");
  const auto Output = tmpFile("bitset-constant-position-receiver-and-value.nc");
  writeFile(Source, R"r19bitice(#include <bitset>
static unsigned phase;
std::bitset<70>& receiver(std::bitset<70>& bits) { ++phase; return bits; }
bool value() { phase = phase == 1 ? 2 : 99; return true; }
int main() {
  std::bitset<70> bits;
  receiver(bits).set(2u * 33u, value());
  return phase != 2 || !bits.test(66) || bits.count() != 1;
}
)r19bitice");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-constant-position-receiver-and-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetIndexTypeWordBoundariesRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-index-type-word-boundaries.cpp");
  const auto Output = tmpFile("bitset-index-type-word-boundaries.nc");
  writeFile(Source, R"r19bitwiden(#include <bitset>
void update(std::bitset<257>& bits, unsigned char a, unsigned char b) {
  bits.set(a).flip(b);
}
int main() {
  std::bitset<257> bits;
  update(bits, 63, 64);
  return !bits.test(63) || !bits.test(64) || bits.count() != 2;
}
)r19bitwiden");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-index-type-word-boundaries" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetIndexTypeResetAndFlipRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-index-type-reset-and-flip.cpp");
  const auto Output = tmpFile("bitset-index-type-reset-and-flip.nc");
  writeFile(Source, R"r19bitwiden(#include <bitset>
void change(std::bitset<256>& bits, unsigned char pos) {
  bits.flip(pos).reset(pos);
}
int main() {
  std::bitset<256> bits;
  bits.set(128);
  change(bits, 127);
  change(bits, 128);
  return bits.any();
}
)r19bitwiden");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-index-type-reset-and-flip" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetIndexTypeIndexCalledOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-index-type-index-called-once.cpp");
  const auto Output = tmpFile("bitset-index-type-index-called-once.nc");
  writeFile(Source, R"r19bitwiden(#include <bitset>
static unsigned calls;
unsigned char position() { ++calls; return 201; }
int main() {
  std::bitset<256> bits;
  bits.set(position());
  return calls != 1 || !bits.test(201) || bits.count() != 1;
}
)r19bitwiden");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-index-type-index-called-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralOffsetAndCountRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-owned-literal-offset-and-count.cpp");
  const auto Output = tmpFile("bitset-owned-literal-offset-and-count.nc");
  writeFile(Source, R"r19bitowned(#include <bitset>
#include <string>
int main() {
  std::bitset<8> value{std::string("prefixyxxymore"), 6, 4, 'x', 'y'};
  return value.to_ulong() != 9 || value.count() != 2;
}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-owned-literal-offset-and-count" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralZeroAsADigitRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-owned-literal-zero-as-a-digit.cpp");
  const auto Output = tmpFile("bitset-owned-literal-zero-as-a-digit.nc");
  writeFile(Source, R"r19bitowned(#include <bitset>
#include <string>
int main() {
  std::bitset<6> value{std::string("A\0A?", 4), 0, 3, 'A', '\0'};
  return value.to_ulong() != 2 || value.count() != 1;
}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-owned-literal-zero-as-a-digit" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralEqualDigitsRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-owned-literal-equal-digits.cpp");
  const auto Output = tmpFile("bitset-owned-literal-equal-digits.nc");
  writeFile(Source, R"r19bitowned(#include <bitset>
#include <string>
int main() {
  std::bitset<4> value{std::string("zzz"), 0, 3, 'z', 'z'};
  return value.to_ulong() != 7 || value.count() != 3;
}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-owned-literal-equal-digits" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstexprTextPointerNamespaceRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-constexpr-text-pointer-namespace.cpp");
  const auto Output = tmpFile("bitset-constexpr-text-pointer-namespace.nc");
  writeFile(Source, R"r19bitctxptr(#include <bitset>
constexpr const char* text = "0110";
bool test() { return std::bitset<8>(text).test(2); }
int main() { return !test() || std::bitset<8>(text).count() != 2; }
)r19bitctxptr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-constexpr-text-pointer-namespace" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstexprTextPointerCustomDigitsRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-constexpr-text-pointer-custom-digits.cpp");
  const auto Output = tmpFile("bitset-constexpr-text-pointer-custom-digits.nc");
  writeFile(Source, R"r19bitctxptr(#include <bitset>
int main() {
  constexpr const char* text = "xoo123";
  return std::bitset<6>(text, 3, 'o', 'x').to_ulong() != 4UL;
}
)r19bitctxptr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-constexpr-text-pointer-custom-digits" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstexprTextPointerZeroCountRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-constexpr-text-pointer-zero-count.cpp");
  const auto Output = tmpFile("bitset-constexpr-text-pointer-zero-count.nc");
  writeFile(Source, R"r19bitctxptr(#include <bitset>
int main() {
  constexpr const char* text = "not a binary string";
  return std::bitset<80>(text, 0).any();
}
)r19bitctxptr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-constexpr-text-pointer-zero-count" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedConversionAcceptsExtendedTemporary) {
  const auto Source = tmpFile("bitset-const-local-guard-const-reference.cpp");
  writeFile(Source, R"r19bitlocal(#include <bitset>
using Bits = std::bitset<129>;
int main(){const Bits& bits=Bits(2);return bits.to_ullong()!=2;}
)r19bitlocal");
  const auto Output = tmpFile("bitset-const-local-guard-const-reference.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-const-local-guard-const-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedConversionCopiesSingleUseLocal) {
  const auto Source = tmpFile("bitset-const-local-guard-copy-initialized.cpp");
  writeFile(Source, R"r19bitlocal(#include <bitset>
using Bits = std::bitset<129>;
int main(){Bits a(3);const Bits b=a;return b.to_ullong()!=3;}
)r19bitlocal");
  const auto Output = tmpFile("bitset-const-local-guard-copy-initialized.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-const-local-guard-copy-initialized" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedConversionAcceptsConstDataMember) {
  const auto Source = tmpFile("bitset-const-local-guard-const-member.cpp");
  writeFile(Source, R"r19bitlocal(#include <bitset>
using Bits = std::bitset<129>;
struct State{const Bits bits{2};};
int main(){State state;return state.bits.to_ullong()!=2;}
)r19bitlocal");
  const auto Output = tmpFile("bitset-const-local-guard-const-member.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-const-local-guard-const-member" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstLocalGuardAliasToMutableRejected) {
  const auto Source = tmpFile("bitset-const-local-guard-alias-to-mutable.cpp");
  writeFile(Source, R"r19bitlocal(#include <bitset>
using Bits = std::bitset<129>;
int main(){Bits bits(1);const Bits& r=bits;bits.set(1);return r.to_ullong()!=3;}
)r19bitlocal");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstantPositionGuardNegativeRejected) {
  const auto Source = tmpFile("bitset-constant-position-guard-negative.cpp");
  writeFile(Source, R"r19bitice(#include <bitset>
int main(){std::bitset<65> b;constexpr int p=-1;return b.test(p);}
)r19bitice");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstantPositionGuardConvertedTooLargeRejected) {
  const auto Source = tmpFile("bitset-constant-position-guard-converted-too-large.cpp");
  writeFile(Source, R"r19bitice(#include <bitset>
int main(){std::bitset<65> b;return b.test(~std::size_t{0});}
)r19bitice");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstantPositionGuardConstantContextOnlyRejected) {
  const auto Source = tmpFile("bitset-constant-position-guard-constant-context-only.cpp");
  writeFile(Source, R"r19bitice(#include <bitset>
constexpr unsigned at(){return __builtin_is_constant_evaluated()?0:65;}
int main(){std::bitset<65> b;return b.test(at());}
)r19bitice");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstantPositionGuardConstexprCallWithRuntimeInputRejected) {
  const auto Source = tmpFile("bitset-constant-position-guard-constexpr-call-with-runtime-input.cpp");
  writeFile(Source, R"r19bitice(#include <bitset>
constexpr unsigned at(unsigned p){return p;}
bool check(unsigned p){std::bitset<65> b;return b.test(at(p));}
int main(){return check(2);}
)r19bitice");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetIndexTypeGuardSignedByteRejected) {
  const auto Source = tmpFile("bitset-index-type-guard-signed-byte.cpp");
  writeFile(Source, R"r19bitwiden(#include <bitset>
bool check(signed char p){std::bitset<256> bits;return bits.test(p);}
int main(){return check(2);}
)r19bitwiden");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetIndexTypeGuardTooShortForBoolRejected) {
  const auto Source = tmpFile("bitset-index-type-guard-too-short-for-bool.cpp");
  writeFile(Source, R"r19bitwiden(#include <bitset>
bool check(bool p){std::bitset<1> bits;return bits.test(p);}
int main(){return check(false);}
)r19bitwiden");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetIndexTypeGuardWideUnsignedRejected) {
  const auto Source = tmpFile("bitset-index-type-guard-wide-unsigned.cpp");
  writeFile(Source, R"r19bitwiden(#include <bitset>
bool check(unsigned p){std::bitset<256> bits;return bits.test(p);}
int main(){return check(2);}
)r19bitwiden");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetIndexTypeGuardEnumOperandRejected) {
  const auto Source = tmpFile("bitset-index-type-guard-enum-operand.cpp");
  writeFile(Source, R"r19bitwiden(#include <bitset>
enum Position:unsigned char {p=2};
bool check(Position i){std::bitset<256> bits;return bits.test(i);}
int main(){return check(p);}
)r19bitwiden");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralGuardNamedStringRejected) {
  const auto Source = tmpFile("bitset-owned-literal-guard-named-string.cpp");
  writeFile(Source, R"r19bitowned(#include <bitset>
#include <string>
int main(){std::string text("101");return std::bitset<8>{text}.any();}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralGuardOutOfRangeOffsetRejected) {
  const auto Source = tmpFile("bitset-owned-literal-guard-out-of-range-offset.cpp");
  writeFile(Source, R"r19bitowned(#include <bitset>
#include <string>
int main(){return std::bitset<8>{std::string("101"),4}.any();}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralGuardInvalidSelectedDigitRejected) {
  const auto Source = tmpFile("bitset-owned-literal-guard-invalid-selected-digit.cpp");
  writeFile(Source, R"r19bitowned(#include <bitset>
#include <string>
int main(){return std::bitset<1>{std::string("1x"),0,2}.any();}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralGuardNonCharTraitsRejected) {
  const auto Source = tmpFile("bitset-owned-literal-guard-non-char-traits.cpp");
  writeFile(Source, R"r19bitowned(#include <bitset>
#include <string>
int main(){return std::bitset<8>{std::u16string(u"101")}.any();}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralGuardRepresentationQueryRejected) {
  const auto Source = tmpFile("bitset-owned-literal-guard-representation-query.cpp");
  writeFile(Source, R"r19bitowned(#define __builtin_constant_p(...) 0
#include <bitset>
#include <string>
#undef __builtin_constant_p
int main(){return std::bitset<8>{std::string("101")}.to_ulong()!=5;}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstexprTextPointerGuardMutableGlobalRejected) {
  const auto Source = tmpFile("bitset-constexpr-text-pointer-guard-mutable-global.cpp");
  writeFile(Source, R"r19bitctxptr(#include <bitset>
const char* p="1";int main(){return std::bitset<8>(p).any();}
)r19bitctxptr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstexprTextPointerGuardNullRejected) {
  const auto Source = tmpFile("bitset-constexpr-text-pointer-guard-null.cpp");
  writeFile(Source, R"r19bitctxptr(#include <bitset>
int main(){constexpr const char* p=nullptr;return std::bitset<8>(p).any();}
)r19bitctxptr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstexprTextPointerGuardQualifiedWithoutConstexprRejected) {
  const auto Source = tmpFile("bitset-constexpr-text-pointer-guard-qualified-without-constexpr.cpp");
  writeFile(Source, R"r19bitctxptr(#include <bitset>
int main(){const char* const p="1";return std::bitset<8>(p).any();}
)r19bitctxptr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstexprTextPointerGuardOwnedDecoderRejected) {
  const auto Source = tmpFile("bitset-constexpr-text-pointer-guard-owned-decoder.cpp");
  writeFile(Source, R"r19bitctxptr(#define __init_from_string_view nct_foreign_text_init
#include <bitset>
#undef __init_from_string_view
int main(){constexpr const char* p="01";return std::bitset<8>(p).any();}
)r19bitctxptr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetTextWideConversionUnsignedLongWidthRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-text-wide-conversion-unsigned-long-width.cpp");
  const auto Output = tmpFile("bitset-text-wide-conversion-unsigned-long-width.nc");
  writeFile(Source, R"r19bittextwide(#include <bitset>
#include <string>
int main(){return std::bitset<128>("11111111111111111111111111111111").to_ulong()!=0xffffffffUL;}
)r19bittextwide");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-text-wide-conversion-unsigned-long-width" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetTextWideConversionConstexprPointerRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-text-wide-conversion-constexpr-pointer.cpp");
  const auto Output = tmpFile("bitset-text-wide-conversion-constexpr-pointer.nc");
  writeFile(Source, R"r19bittextwide(#include <bitset>
#include <string>
constexpr const char* text="00100111";
int main(){return std::bitset<257>(text).to_ullong()!=39ULL;}
)r19bittextwide");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-text-wide-conversion-constexpr-pointer" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetTextWideConversionEmptyOwningRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-text-wide-conversion-empty-owning.cpp");
  const auto Output = tmpFile("bitset-text-wide-conversion-empty-owning.nc");
  writeFile(Source, R"r19bittextwide(#include <bitset>
#include <string>
int main(){return std::bitset<129>(std::string()).to_ulong()!=0UL;}
)r19bittextwide");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-text-wide-conversion-empty-owning" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetTextWideConversionAboveUnsignedLongRejected) {
  const auto Source = tmpFile("bitset-text-wide-conversion-above-unsigned-long.cpp");
  writeFile(Source, R"r19bittextwide(#include <bitset>
#include <string>
int main(){return std::bitset<128>("10000000000000000000000000000000000000000000000000000000000000000").to_ulong()==0UL;}
)r19bittextwide");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetTextWideConversionHighWordFromCustomDigitsRejected) {
  const auto Source = tmpFile("bitset-text-wide-conversion-high-word-from-custom-digits.cpp");
  writeFile(Source, R"r19bittextwide(#include <bitset>
#include <string>
int main(){return std::bitset<129>("xoooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooo",65,'o','x').to_ullong()==0ULL;}
)r19bittextwide");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetTextWideConversionRuntimeTextRejected) {
  const auto Source = tmpFile("bitset-text-wide-conversion-runtime-text.cpp");
  writeFile(Source, R"r19bittextwide(#include <bitset>
#include <string>
int main(int argc,char** argv){
  return argc==2 && std::bitset<129>(argv[1]).to_ullong()==0ULL;
}
)r19bittextwide");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantMetadataSizeAndSentinelRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-metadata-size-and-sentinel.cpp");
  writeFile(Source, R"r19variant(#include <variant>
using V = std::variant<int,double,unsigned long long>;
int main() {
  return std::variant_size_v<V> != 3 ||
         std::variant_npos != static_cast<decltype(sizeof(int))>(-1);
}
)r19variant");
  const auto Output = tmpFile("variant-metadata-size-and-sentinel.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-metadata-size-and-sentinel" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMetadataConstAlternativeRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-metadata-const-alternative.cpp");
  writeFile(Source, R"r19variant(#include <variant>
#include <type_traits>
using V = std::variant<int,int,float>;
using R = std::variant_alternative_t<1,const V>;
static_assert(std::is_same_v<R,const int>);
int main(){R value=17;return value!=17;}
)r19variant");
  const auto Output = tmpFile("variant-metadata-const-alternative.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-metadata-const-alternative" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMetadataSizeQualificationRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-metadata-size-qualification.cpp");
  writeFile(Source, R"r19variant(#include <variant>
using V = std::variant<int,char>;
int main(){return std::variant_size_v<const V>!=2;}
)r19variant");
  const auto Output = tmpFile("variant-metadata-size-qualification.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-metadata-size-qualification" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantLayoutMixedScalarRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-layout-mixed-scalar.cpp");
  writeFile(Source, R"r19variantlayout(#include <variant>
using V = std::variant<int, double>;
int main() { return sizeof(V) != 2 * sizeof(double) || alignof(V) != alignof(double); }
)r19variantlayout");
  const auto Output = tmpFile("variant-layout-mixed-scalar.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-layout-mixed-scalar" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantLayoutPointerAlternativeRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-layout-pointer-alternative.cpp");
  writeFile(Source, R"r19variantlayout(#include <variant>
using V = std::variant<int*, bool>;
int main() { return sizeof(V) != 2 * sizeof(int*) || alignof(V) != alignof(int*); }
)r19variantlayout");
  const auto Output = tmpFile("variant-layout-pointer-alternative.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-layout-pointer-alternative" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantLayoutDeclarationIndexRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-layout-declaration-index.cpp");
  writeFile(Source, R"r19variantlayout(#include <variant>
using V = std::variant<int, double>;
constexpr auto Width = sizeof(V);
int main() { unsigned char bytes[Width]{}; return sizeof(bytes) != 2 * sizeof(double); }
)r19variantlayout");
  const auto Output = tmpFile("variant-layout-declaration-index.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-layout-declaration-index" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMetadataRuntimeValueRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-metadata-runtime-value.cpp");
  writeFile(Source, R"r19variant(#include <variant>
using V = std::variant<int,double>;
int main(){V value(7);return value.index();}
)r19variant");
  const auto Output = tmpFile("variant-metadata-runtime-value.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-metadata-runtime-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMetadataSentinelStorageRejected) {
  const auto Source = tmpFile("variant-metadata-sentinel-storage.cpp");
  writeFile(Source, R"r19variant(#include <variant>
int main(){auto p=&std::variant_npos;return *p==0;}
)r19variant");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantMetadataVolatileAlternativeRejected) {
  const auto Source = tmpFile("variant-metadata-volatile-alternative.cpp");
  writeFile(Source, R"r19variant(#include <variant>
using V = std::variant<int,volatile int>;
int main(){return std::variant_size_v<V>!=2;}
)r19variant");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantLayoutAlteredIndexMemberRejected) {
  const auto Source = tmpFile("variant-layout-altered-index-member.cpp");
  writeFile(Source, R"r19variantlayout(#define __index substituted_index
#include <variant>
#undef __index
using V = std::variant<int, double>;
int main() { return sizeof(V) == 0; }
)r19variantlayout");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantObjectsDefaultRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-default.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
extern "C" int check() {
  std::variant<int, double> value;
  return value.index() != 0 || value.valueless_by_exception();
}
int main() { return check(); }
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-default.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-default" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsThirdLvalueRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-third-lvalue.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
extern "C" int check() {
  int value = 7;
  const std::variant<char, double, int> item = value;
  return item.index() != 2 || item.valueless_by_exception();
}
int main() { return check(); }
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-third-lvalue.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-third-lvalue" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsDefaultPointerRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-default-pointer.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
extern "C" int check() {
  std::variant<int*, bool> value{};
  return (&value)->index() != 0 || (&value)->valueless_by_exception();
}
int main() { return check(); }
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-default-pointer.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-default-pointer" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsTemporaryRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-temporary.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
extern "C" int check() {
  return std::variant<int, double>(1).index() != 0 ||
         std::variant<int, double>(2.5).valueless_by_exception();
}
int main() { return check(); }
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-temporary.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-temporary" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsGetIfConstRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-get-if-const.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
extern "C" int check() {
  using Choice = std::variant<int, double>;
  const Choice value = 2.5;
  const auto *p = std::get_if<double>(&value);
  const Choice *empty = nullptr;
  return !p || *p != 2.5 || std::get_if<0>(&value) ||
         std::get_if<1>(empty) || std::get_if<int>(empty);
}
int main() { return check(); }
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-get-if-const.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-get-if-const" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsGetIfCopyAndEvaluateOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-get-if-copy-and-evaluate-once.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
using Choice = std::variant<char, double, int>;
int attempts;
Choice *once(Choice *owner) { ++attempts; return owner; }
extern "C" int check() {
  Choice value = 42;
  Choice copy = value;
  auto *p = std::get_if<2>(once(&copy));
  if (!p || *p != 42 || attempts != 1) return 2;
  *p = 19;
  auto *original = std::get_if<2>(&value);
  return !original || original == p || *original != 42 || *p != 19;
}
int main() { return check(); }
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-get-if-copy-and-evaluate-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-get-if-copy-and-evaluate-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsCopyChangingAlternativeRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-copy-changing-alternative.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
int main() {
  std::variant<int, double> first = 3;
  const std::variant<int, double> second = 9.0;
  auto& receiver = (first = second);
  auto* payload = std::get_if<1>(&first);
  return &receiver != &first || !payload || *payload != 9.0 || std::get_if<0>(&first);
}
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-copy-changing-alternative.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-copy-changing-alternative" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsConstGetByIndexRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-const-get-by-index.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
int main() {
  const std::variant<int, double> value{5};
  const auto &item = std::get<0>(value);
  return item != 5;
}
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-const-get-by-index.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-const-get-by-index" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantAggregateAlternativeRunsAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-class-alternative.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
struct Payload { int field; };
extern "C" int check() { std::variant<Payload, int> value; return int(value.index()); }
int main() { return check(); }
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-class-alternative.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-class-alternative" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsConstAlternativeRejected) {
  const auto Source = tmpFile("variant-objects-const-alternative.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
extern "C" int check() {
  std::variant<const int, double> value;
  return int(value.index());
}
int main() { return check(); }
)r19variantsclr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantObjectsUncheckedGetRejected) {
  const auto Source = tmpFile("variant-objects-unchecked-get.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
extern "C" int check() {
  std::variant<int, double> value;
  return std::get<0>(value);
}
int main() { return check(); }
)r19variantsclr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantObjectsConstGetMutableAliasRejected) {
  const auto Source = tmpFile("variant-objects-const-get-mutable-alias.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
int main() {
  std::variant<int, double> value{3};
  const auto &alias = value;
  value = std::variant<int, double>{2.5};
  return std::get<0>(alias);
}
)r19variantsclr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantObjectsGetIfReplacedAddressBodyRejected) {
  const auto Source = tmpFile("variant-objects-get-if-replaced-address-body.cpp");
  writeFile(Source, R"r19variantsclr(#define __builtin_addressof(x) (&(x))
#include <variant>
#undef __builtin_addressof
int main() {
  std::variant<int, double> value = 3;
  const int *item = std::get_if<0>(&value);
  return !item || *item != 3;
}
)r19variantsclr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantMonostateStandaloneLayoutRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-monostate-standalone-layout.cpp");
  writeFile(Source, R"r19monostate(#include <variant>
using M = std::monostate;
int main() { return sizeof(M) != 1 || alignof(const M&) != 1 || sizeof(M[5]) != 5; }
)r19monostate");
  const auto Output = tmpFile("variant-monostate-standalone-layout.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-monostate-standalone-layout" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMonostateMonostateOnlyVariantRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-monostate-monostate-only-variant.cpp");
  writeFile(Source, R"r19monostate(#include <variant>
using V = std::variant<std::monostate, std::monostate>;
using R = std::variant<unsigned char, unsigned char>;
int main() { return sizeof(V) != sizeof(R) || alignof(V) != alignof(R); }
)r19monostate");
  const auto Output = tmpFile("variant-monostate-monostate-only-variant.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-monostate-monostate-only-variant" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyLayoutConstReferenceSizeRunAtBothOptimizations) {
  const auto Source = tmpFile("any-layout-const-reference-size.cpp");
  writeFile(Source, R"r19any(#include <any>
using A = std::any;
int main() { return sizeof(const A&) != sizeof(A) || alignof(const A&) != alignof(void*); }
)r19any");
  const auto Output = tmpFile("any-layout-const-reference-size.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-layout-const-reference-size" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyLayoutConstantArrayBoundRunAtBothOptimizations) {
  const auto Source = tmpFile("any-layout-constant-array-bound.cpp");
  writeFile(Source, R"r19any(#include <any>
constexpr auto Count = sizeof(std::any);
int main() { unsigned char bytes[Count]{}; return sizeof(bytes) != 4 * sizeof(void*); }
)r19any");
  const auto Output = tmpFile("any-layout-constant-array-bound.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-layout-constant-array-bound" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeMapAliasesRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-map-aliases.cpp");
  writeFile(Source, R"r19assoc(#include <map>
#include <type_traits>
using Container = std::map<int, int>;
int main() { return !std::is_same_v<Container::key_type, int> || !std::is_same_v<Container::value_type, std::pair<const int, int>>; }
)r19assoc");
  const auto Output = tmpFile("associative-map-aliases.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-map-aliases" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeMapAlignofRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-map-alignof.cpp");
  writeFile(Source, R"r19assoc(#include <map>
#include <type_traits>
using Container = std::map<int, int>;
int main() { return alignof(const Container&) != alignof(void*); }
)r19assoc");
  const auto Output = tmpFile("associative-map-alignof.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-map-alignof" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeSetAliasesRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-set-aliases.cpp");
  writeFile(Source, R"r19assoc(#include <set>
#include <type_traits>
using Container = std::set<int>;
int main() { return !std::is_same_v<Container::key_type, int> || !std::is_same_v<Container::value_type, int>; }
)r19assoc");
  const auto Output = tmpFile("associative-set-aliases.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-set-aliases" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeSetAlignofRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-set-alignof.cpp");
  writeFile(Source, R"r19assoc(#include <set>
#include <type_traits>
using Container = std::set<int>;
int main() { return alignof(const Container&) != alignof(void*); }
)r19assoc");
  const auto Output = tmpFile("associative-set-alignof.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-set-alignof" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeUnorderedSetAliasesRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-unordered_set-aliases.cpp");
  writeFile(Source, R"r19assoc(#include <unordered_set>
#include <type_traits>
using Container = std::unordered_set<int>;
int main() { return !std::is_same_v<Container::key_type, int> || !std::is_same_v<Container::value_type, int>; }
)r19assoc");
  const auto Output = tmpFile("associative-unordered_set-aliases.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-unordered_set-aliases" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeUnorderedSetAlignofRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-unordered_set-alignof.cpp");
  writeFile(Source, R"r19assoc(#include <unordered_set>
#include <type_traits>
using Container = std::unordered_set<int>;
int main() { return alignof(const Container&) != alignof(void*); }
)r19assoc");
  const auto Output = tmpFile("associative-unordered_set-alignof.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-unordered_set-alignof" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMonostateValueConstAddressRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-monostate-value-const-address.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
using V = std::variant<std::monostate, int>;
int main() {
  const V value;
  const V* absent = nullptr;
  const auto* first = std::get_if<std::monostate>(&value);
  return first == nullptr || first != &std::get<std::monostate>(value) ||
         std::get_if<0>(absent) != nullptr || std::get_if<1>(&value) != nullptr;
}
)r19varvalue");
  const auto Output = tmpFile("variant-monostate-value-const-address.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-monostate-value-const-address" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMonostateValueDuplicateEmptyRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-monostate-value-duplicate-empty.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
using V = std::variant<std::monostate, std::monostate>;
int main() {
  V value;
  return value.index() != 0 || !std::get_if<0>(&value) || std::get_if<1>(&value);
}
)r19varvalue");
  const auto Output = tmpFile("variant-monostate-value-duplicate-empty.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-monostate-value-duplicate-empty" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantIndexConstructionZeroInitializedRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-index-construction-zero-initialized.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
int main() {
  std::variant<int, long> value(std::in_place_index<1>);
  auto* selected = std::get_if<1>(&value);
  return value.index() != 1 || !selected || *selected != 0;
}
)r19varvalue");
  const auto Output = tmpFile("variant-index-construction-zero-initialized.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-index-construction-zero-initialized" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantIndexConstructionEmptyNonfirstRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-index-construction-empty-nonfirst.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
int main() {
  std::variant<int, std::monostate> value(std::in_place_index<1>);
  return !std::holds_alternative<std::monostate>(value) || !std::get_if<1>(&value) ||
         std::get_if<0>(&value) != nullptr;
}
)r19varvalue");
  const auto Output = tmpFile("variant-index-construction-empty-nonfirst.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-index-construction-empty-nonfirst" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantTypeConstructionValueInitializePointerRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-type-construction-value-initialize-pointer.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
int main() {
  std::variant<int, int*> value(std::in_place_type<int*>);
  auto** selected = std::get_if<int*>(&value);
  return value.index() != 1 || selected == nullptr || *selected != nullptr;
}
)r19varvalue");
  const auto Output = tmpFile("variant-type-construction-value-initialize-pointer.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-type-construction-value-initialize-pointer" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantTypeConstructionZeroIndexRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-type-construction-zero-index.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
int main() {
  std::variant<unsigned, long> value(std::in_place_type<unsigned>, 19u);
  auto* selected = std::get_if<unsigned>(&value);
  return value.index() != 0 || !selected || *selected != 19u;
}
)r19varvalue");
  const auto Output = tmpFile("variant-type-construction-zero-index.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-type-construction-zero-index" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeSetCustomTypeOrPolicyRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-set-custom-type-or-policy.cpp");
  writeFile(Source, R"r19assoc(#include <set>
#include <type_traits>
using Container = std::set<int, std::greater<int>>;
int main() { return sizeof(Container) == 0; }
)r19assoc");
  const auto Output = tmpFile("associative-set-custom-type-or-policy.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-set-custom-type-or-policy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyLayoutRuntimeConstructionRunAtBothOptimizations) {
  const auto Source = tmpFile("any-layout-runtime-construction.cpp");
  writeFile(Source, R"r19any(#include <any>
int main() { std::any value{7}; return !value.has_value(); }
)r19any");
  const auto Output = tmpFile("any-layout-runtime-construction.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-layout-runtime-construction" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMonostateVolatileLayoutRejected) {
  const auto Source = tmpFile("variant-monostate-volatile-layout.cpp");
  writeFile(Source, R"r19monostate(#include <variant>
using M = volatile std::monostate;
int main() { return sizeof(M) == 0; }
)r19monostate");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyLayoutAlteredHandlerFieldRejected) {
  const auto Source = tmpFile("any-layout-altered-handler-field.cpp");
  writeFile(Source, R"r19any(#define __h_ altered_handler
#include <any>
#undef __h_
int main() { return sizeof(std::any) == 0; }
)r19any");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AssociativeMapRuntimeStorageGuardRejected) {
  const auto Source = tmpFile("associative-map-runtime-storage-guard.cpp");
  writeFile(Source, R"r19assoc(#include <map>
#include <type_traits>
using Container = std::map<int, int>;
int main() { Container value; return sizeof(value) == 0; }
)r19assoc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AssociativeSetRuntimeStorageGuardRejected) {
  const auto Source = tmpFile("associative-set-runtime-storage-guard.cpp");
  writeFile(Source, R"r19assoc(#include <set>
#include <type_traits>
using Container = std::set<int>;
int main() { Container value; return sizeof(value) == 0; }
)r19assoc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AssociativeUnorderedSetRenamedContainerStorageRejected) {
  const auto Source = tmpFile("associative-unordered_set-renamed-container-storage.cpp");
  writeFile(Source, R"r19assoc(#define __table_ altered_owner_storage
#include <unordered_set>
#include <type_traits>
#undef __table_
using Container = std::unordered_set<int>;
int main() { return sizeof(Container) == 0; }
)r19assoc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AssociativeUnorderedMapNonDefaultKeyRejected) {
  const auto Source = tmpFile("associative-unordered-map-non-default-key.cpp");
  writeFile(Source, R"r19assoc(#include <unordered_map>
using Map = std::unordered_map<long long, int>;
int main() { return sizeof(Map) == 0; }
)r19assoc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantMonostateValueRuntimeRedeclarationRejected) {
  const auto Source = tmpFile("variant-monostate-value-runtime-redeclaration.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
_LIBCPP_BEGIN_NAMESPACE_STD
struct monostate;
_LIBCPP_END_NAMESPACE_STD
int main() {
  std::variant<std::monostate, int> value;
  return value.index();
}
)r19varvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantIndexConstructionEscapingTagRejected) {
  const auto Source = tmpFile("variant-index-construction-escaping-tag.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
int f() {
  const auto* tag = &std::in_place_index<1>;
  return tag != nullptr;
}
)r19varvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantTypeConstructionEffectfulTagRejected) {
  const auto Source = tmpFile("variant-type-construction-effectful-tag.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
int effects;
std::in_place_type_t<long> select_type() {
  ++effects;
  return std::in_place_type<long>;
}
int f() {
  std::variant<int, long> value(select_type(), 19L);
  return value.index();
}
)r19varvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyEmptyReceiverEvaluationRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-receiver-evaluation.cpp");
  writeFile(Source, R"r19any(#include <any>
int visits;
std::any* receive(std::any* p) { ++visits; return p; }
int main() {
  std::any a;
  receive(&a)->reset();
  const std::any* p = receive(&a);
  return visits != 2 || p->has_value();
}
)r19any");
  const auto Output = tmpFile("any-empty-receiver-evaluation.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-receiver-evaluation" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyMemberLifetimeRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-member-lifetime.cpp");
  writeFile(Source, R"r19any(#include <any>
struct Owner { std::any any; int marker; };
int main() {
  Owner object{{}, 2};
  object.any.reset();
  return object.marker != 2 || object.any.has_value();
}
)r19any");
  const auto Output = tmpFile("any-empty-member-lifetime.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-member-lifetime" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyUnprovedCopyRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-unproved-copy.cpp");
  writeFile(Source, R"r19any(#include <any>
int main() {
  std::any a;
  std::any b(a);
  return b.has_value();
}
)r19any");
  const auto Output = tmpFile("any-empty-unproved-copy.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-unproved-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyPointerCastConstRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-pointer-cast-const.cpp");
  writeFile(Source, R"r19any(#include <any>
#include <type_traits>
int main() {
  const std::any value;
  using T = unsigned;
  const auto* pointer = std::any_cast<T>(&value);
  static_assert(std::is_same_v<decltype(pointer), const T*>);
  return pointer != nullptr || std::any_cast<const T>(&value) != nullptr;
}
)r19any");
  const auto Output = tmpFile("any-empty-pointer-cast-const.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-pointer-cast-const" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyPointerCastPointerTypeRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-pointer-cast-pointer-type.cpp");
  writeFile(Source, R"r19any(#include <any>
int main() {
  const std::any* value = nullptr;
  int* const* pointer = std::any_cast<int*>(value);
  return pointer != nullptr;
}
)r19any");
  const auto Output = tmpFile("any-empty-pointer-cast-pointer-type.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-pointer-cast-pointer-type" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyCopyCopyEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-copy-copy-effects.cpp");
  writeFile(Source, R"r19any(#include <any>
int calls;
const std::any& observe(const std::any& value) { ++calls; return value; }
int main() {
  std::any original;
  std::any copy(observe(original));
  return calls != 1 || copy.has_value();
}
)r19any");
  const auto Output = tmpFile("any-empty-copy-copy-effects.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-copy-copy-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyAssignmentOperatorNotationOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-assignment-operator-notation-order.cpp");
  writeFile(Source, R"r19any(#include <any>
int effects;
std::any& select(std::any& value, int number) { effects = effects * 10 + number; return value; }
int main() {
  std::any first, second;
  auto& result = (select(first, 1) = select(second, 2));
  return effects != 21 || &result != &first || first.has_value() || second.has_value();
}
)r19any");
  const auto Output = tmpFile("any-empty-assignment-operator-notation-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-assignment-operator-notation-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyAssignmentMoveAndSelfRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-assignment-move-and-self.cpp");
  writeFile(Source, R"r19any(#include <any>
#include <utility>
int main() {
  std::any first, second;
  return &(first = std::move(second)) != &first || &(first = std::move(first)) != &first ||
         first.has_value() || second.has_value();
}
)r19any");
  const auto Output = tmpFile("any-empty-assignment-move-and-self.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-assignment-move-and-self" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyAlteredObserverRejected) {
  const auto Source = tmpFile("any-empty-altered-observer.cpp");
  writeFile(Source, R"r19any(#define has_value changed_observer
#include <any>
#undef has_value
int main() {
  std::any a;
  return a.changed_observer();
}
)r19any");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyEmptyPointerCastValueFormRejected) {
  const auto Source = tmpFile("any-empty-pointer-cast-value-form.cpp");
  writeFile(Source, R"r19any(#include <any>
int f() {
  std::any value;
  return std::any_cast<int>(value);
}
)r19any");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyEmptyAssignmentRenamedSwapRejected) {
  const auto Source = tmpFile("any-empty-assignment-renamed-swap.cpp");
  writeFile(Source, R"r19any(#define swap changed_swap
#include <any>
#undef swap
int f() {
  std::any first, second;
  first = second;
  return first.has_value();
}
)r19any");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2MonostateComparisonOperandEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("monostate-comparison-operand-effects.cpp");
  writeFile(Source, R"r19monocmp(#include <variant>
std::monostate touch(int& count) { ++count; return {}; }
int main() {
  int left = 0, right = 0;
  bool equal = touch(left) == touch(right);
  bool unequal = touch(left) != touch(right);
  return !equal || unequal || left != 2 || right != 2;
}
)r19monocmp");
  const auto Output = tmpFile("monostate-comparison-operand-effects.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("monostate-comparison-operand-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantEmplaceIndexAndReferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-emplace-index-and-reference.cpp");
  writeFile(Source, R"r19emplace(#include <variant>
int main() {
  std::variant<int, long> value(std::in_place_index<1>, 29L);
  int& slot = value.emplace<0>(12);
  slot += 9;
  auto* item = std::get_if<0>(&value);
  return value.index() != 0 || std::holds_alternative<long>(value) ||
         item != &slot || *item != 21;
}
)r19emplace");
  const auto Output = tmpFile("variant-emplace-index-and-reference.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-emplace-index-and-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantEmplacePointerZeroRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-emplace-pointer-zero.cpp");
  writeFile(Source, R"r19emplace(#include <variant>
int main() {
  int number = 1;
  std::variant<int*, long> value(std::in_place_index<0>, &number);
  auto& slot = value.emplace<int*>();
  return slot != nullptr || value.index() != 0 || std::get_if<int*>(&value) != &slot;
}
)r19emplace");
  const auto Output = tmpFile("variant-emplace-pointer-zero.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-emplace-pointer-zero" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantEmplaceReceiverBeforeInputRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-emplace-receiver-before-input.cpp");
  writeFile(Source, R"r19emplace(#include <variant>
using Value = std::variant<int, long>;
int order;
Value* receiver(Value& v) { order = order * 10 + 1; return &v; }
long input() { order = order * 10 + 2; return 43L; }
int main() {
  Value value(7);
  long* slot = &receiver(value)->emplace<1>(input());
  return order != 12 || value.index() != 1 || std::get_if<1>(&value) != slot || *slot != 43L;
}
)r19emplace");
  const auto Output = tmpFile("variant-emplace-receiver-before-input.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-emplace-receiver-before-input" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantEmplaceAcceptsIntegerWidening) {
  const auto Source = tmpFile("variant-emplace-widening-operand.cpp");
  writeFile(Source, R"r19emplace(#include <variant>
int f() {
  std::variant<int, long> value;
  return value.emplace<1>(11);
}
)r19emplace");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantEmplaceVolatileOperandRejected) {
  const auto Source = tmpFile("variant-emplace-volatile-operand.cpp");
  writeFile(Source, R"r19emplace(#include <variant>
int f() {
  std::variant<int, long> value;
  volatile long source = 11;
  return value.emplace<long>(source);
}
)r19emplace");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AssociativeMultimapAliasesRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-multimap-aliases.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <type_traits>
using Container = std::multimap<int, int>;
int main() { return !std::is_same_v<Container::key_type, int> || !std::is_same_v<Container::value_type, std::pair<const int, int>>; }
)r19containers");
  const auto Output = tmpFile("associative-multimap-aliases.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-multimap-aliases" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeMultimapAlignofRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-multimap-alignof.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <type_traits>
using Container = std::multimap<int, int>;
int main() { return alignof(const Container&) != alignof(void*); }
)r19containers");
  const auto Output = tmpFile("associative-multimap-alignof.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-multimap-alignof" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeMultisetAliasesRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-multiset-aliases.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <type_traits>
using Container = std::multiset<int>;
int main() { return !std::is_same_v<Container::key_type, int> || !std::is_same_v<Container::value_type, int>; }
)r19containers");
  const auto Output = tmpFile("associative-multiset-aliases.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-multiset-aliases" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeMultisetAlignofRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-multiset-alignof.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <type_traits>
using Container = std::multiset<int>;
int main() { return alignof(const Container&) != alignof(void*); }
)r19containers");
  const auto Output = tmpFile("associative-multiset-alignof.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-multiset-alignof" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeUnorderedMultisetAliasesRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-unordered_multiset-aliases.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
#include <type_traits>
using Container = std::unordered_multiset<int>;
int main() { return !std::is_same_v<Container::key_type, int> || !std::is_same_v<Container::value_type, int>; }
)r19containers");
  const auto Output = tmpFile("associative-unordered_multiset-aliases.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-unordered_multiset-aliases" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeUnorderedMultisetAlignofRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-unordered_multiset-alignof.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
#include <type_traits>
using Container = std::unordered_multiset<int>;
int main() { return alignof(const Container&) != alignof(void*); }
)r19containers");
  const auto Output = tmpFile("associative-unordered_multiset-alignof.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-unordered_multiset-alignof" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeAdapterConstructionQueueCopyConstDequeRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-adapter-construction-queue-copy-const-deque.cpp");
  writeFile(Source, R"r19containers(#include <queue>
#include <deque>
int main() {
  const std::deque<int> original{2, 7, 11};
  std::queue<int> result(original);
  if (&result.front() == &original.front() || result.front() != 2)
    return 1;
  result.front() = 13;
  result.pop();
  return result.front() != 7 || result.back() != 11 || original.front() != 2;
}
)r19containers");
  const auto Output = tmpFile("deque-adapter-construction-queue-copy-const-deque.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-adapter-construction-queue-copy-const-deque" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeAdapterConstructionStackMoveDequeOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-adapter-construction-stack-move-deque-once.cpp");
  writeFile(Source, R"r19containers(#include <stack>
#include <deque>
int calls;
std::deque<int> contents() {
  ++calls;
  return std::deque<int>{2, 7, 11};
}
int main() {
  std::stack<int> result(contents());
  if (calls != 1 || result.top() != 11)
    return 1;
  result.pop();
  return result.top() != 7;
}
)r19containers");
  const auto Output = tmpFile("deque-adapter-construction-stack-move-deque-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-adapter-construction-stack-move-deque-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeMultisetCustomTypeOrPolicyRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-multiset-custom-type-or-policy.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <type_traits>
using Container = std::multiset<int, std::greater<int>>;
int main() { return sizeof(Container) == 0; }
)r19containers");
  const auto Output = tmpFile("associative-multiset-custom-type-or-policy.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-multiset-custom-type-or-policy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeMultimapRuntimeStorageGuardRejected) {
  const auto Source = tmpFile("associative-multimap-runtime-storage-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <type_traits>
using Container = std::multimap<int, int>;
int main() { Container value; return sizeof(value) == 0; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AssociativeMultisetRuntimeStorageGuardRejected) {
  const auto Source = tmpFile("associative-multiset-runtime-storage-guard.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <type_traits>
using Container = std::multiset<int>;
int main() { Container value; return sizeof(value) == 0; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AssociativeUnorderedMultisetRenamedContainerStorageRejected) {
  const auto Source = tmpFile("associative-unordered_multiset-renamed-container-storage.cpp");
  writeFile(Source, R"r19containers(#define __table_ altered_owner_storage
#include <unordered_set>
#include <type_traits>
#undef __table_
using Container = std::unordered_multiset<int>;
int main() { return sizeof(Container) == 0; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeAdapterConstructionVectorBackedStackRejected) {
  const auto Source = tmpFile("deque-adapter-construction-vector-backed-stack.cpp");
  writeFile(Source, R"r19containers(#include <stack>
#include <vector>
int f() {
  const std::vector<int> input{7};
  std::stack<int, std::vector<int>> out(input);
  return out.top();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantValueAssignmentSwitchAndChainedResultRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-value-assignment-switch-and-chained-result.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int main() {
  std::variant<int, long> value(3), other(77L);
  (value = 34L) = other;
  auto* item = std::get_if<long>(&value);
  return value.index() != 1 || !item || *item != 77L || std::get_if<int>(&value);
}
)r19containers");
  const auto Output = tmpFile("variant-value-assignment-switch-and-chained-result.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-value-assignment-switch-and-chained-result" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantValueAssignmentAssignmentSyntaxBindingOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-value-assignment-assignment-syntax-binding-order.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<int, long>;
int order;
long next_value = 3;
long& input() { order = order * 10 + 1; return next_value; }
Value& target(Value& value) { order = order * 10 + 2; next_value = 41; return value; }
int main() {
  Value value(17);
  target(value) = input();
  auto* item = std::get_if<long>(&value);
  return order != 12 || !item || *item != 41L;
}
)r19containers");
  const auto Output = tmpFile("variant-value-assignment-assignment-syntax-binding-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-value-assignment-assignment-syntax-binding-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantValueAssignmentPointerAndResetSelectionRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-value-assignment-pointer-and-reset-selection.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int main() {
  int memory = 3;
  std::variant<int, int*> value(12);
  value = &memory;
  auto* item = std::get_if<int*>(&value);
  if (!item || *item != &memory || value.valueless_by_exception())
    return 1;
  value = 7;
  auto* integer = std::get_if<int>(&value);
  return !integer || *integer != 7 || std::get_if<int*>(&value);
}
)r19containers");
  const auto Output = tmpFile("variant-value-assignment-pointer-and-reset-selection.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-value-assignment-pointer-and-reset-selection" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantZeroVisitPointerReferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-zero-visit-pointer-reference.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int first() { return 12; }
int second() { return 21; }
int main() {
  int (*visitor)() = first;
  const int a = std::visit(visitor);
  visitor = second;
  return a != 12 || std::visit(visitor) != 21;
}
)r19containers");
  const auto Output = tmpFile("variant-zero-visit-pointer-reference.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-zero-visit-pointer-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantZeroVisitVoidResultRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-zero-visit-void-result.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int calls;
void visit_body() { ++calls; }
int main() {
  std::visit(visit_body);
  std::visit(&visit_body);
  return calls != 2;
}
)r19containers");
  const auto Output = tmpFile("variant-zero-visit-void-result.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-zero-visit-void-result" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantValueAssignmentUserDefinedConversionRejected) {
  const auto Source = tmpFile("variant-value-assignment-user-defined-conversion.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Input { operator int() const { return 17; } };
int f() {
  std::variant<int, long> value;
  value = Input{};
  return value.index();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantZeroVisitRenamedValueWrapperRejected) {
  const auto Source = tmpFile("variant-zero-visit-renamed-value-wrapper.cpp");
  writeFile(Source, R"r19containers(#define __make_value_visitor changed_visitor_factory
#include <variant>
#undef __make_value_visitor
int visitor() { return 31; }
int main() { return std::visit(visitor) != 31; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantIntegralVisitActiveIndexChangesRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-integral-visit-active-index-changes.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int add(int value) { return value + 3; }
int main() {
  std::variant<short, int> value(std::in_place_index<0>, short(7));
  if (std::visit(add, value) != 10)
    return 1;
  value.emplace<1>(45);
  return std::visit(add, value) != 48;
}
)r19containers");
  const auto Output = tmpFile("variant-integral-visit-active-index-changes.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-integral-visit-active-index-changes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantIntegralVisitCallbackAndReceiverEvaluationRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-integral-visit-callback-and-receiver-evaluation.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<int, short>;
using Callback = int (*)(int);
int visits, receivers, selected;
int body(int value) { ++visits; return value - 2; }
Callback select() { ++selected; return body; }
Value& receive(Value& value) { ++receivers; return value; }
int main() {
  Value value(std::in_place_index<1>, short(7));
  const int result = std::visit(select(), receive(value));
  return result != 5 || visits != 1 || receivers != 1 || selected != 1;
}
)r19containers");
  const auto Output = tmpFile("variant-integral-visit-callback-and-receiver-evaluation.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-integral-visit-callback-and-receiver-evaluation" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantIntegralVisitUsesCapturelessGenericVisitor) {
  const auto Source = tmpFile("variant-integral-visit-generic-visitor.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int f(std::variant<int, short>& value) {
  return std::visit([](auto v) -> int { return int(v); }, value);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantIntegralVisitRenamedBaseSelectionRejected) {
  const auto Source = tmpFile("variant-integral-visit-renamed-base-selection.cpp");
  writeFile(Source, R"r19containers(#define __as_base changed_base_selection
#include <variant>
#undef __as_base
int cb(int v) { return v; }
int main() {
  std::variant<short, int> value(std::in_place_index<1>, 28);
  return std::visit(cb, value) != 28;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyIntCopyValueFromLvalueRunAtBothOptimizations) {
  const auto Source = tmpFile("any-int-copy-value-from-lvalue.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  int item = -17;
  const int& source = item;
  std::any v(source);
  item = 29;
  auto* p = std::any_cast<int>(&v);
  return !p || *p != -17;
}
)r19containers");
  const auto Output = tmpFile("any-int-copy-value-from-lvalue.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-int-copy-value-from-lvalue" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyIntTransferSwapAndResetRunAtBothOptimizations) {
  const auto Source = tmpFile("any-int-transfer-swap-and-reset.cpp");
  writeFile(Source, R"r19containers(#include <any>
#include <utility>
int main() {
  std::any first(8), second(11), vacant;
  first.swap(second);
  second.swap(vacant);
  first = first;
  first = std::move(first);
  if (*std::any_cast<int>(&first) != 11 || second.has_value() || *std::any_cast<int>(&vacant) != 8)
    return 1;
  vacant = first;
  first = std::move(second);
  return first.has_value() || second.has_value() || *std::any_cast<int>(&vacant) != 11;
}
)r19containers");
  const auto Output = tmpFile("any-int-transfer-swap-and-reset.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-int-transfer-swap-and-reset" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyIntRenamedPinnedStorageHandlerRejected) {
  const auto Source = tmpFile("any-int-renamed-pinned-storage-handler.cpp");
  writeFile(Source, R"r19containers(#define __create changed_inline_creation
#include <any>
#undef __create
int f() {
  std::any value(17);
  return value.has_value();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantTwoVisitConstOperandAndMutationRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-two-visit-const-operand-and-mutation.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int observe(short x, int y) { return x - y; }
int main() {
  const std::variant<int, short> a(std::in_place_index<1>, short(3));
  std::variant<short, int> b(std::in_place_index<1>, 28);
  if (std::visit(&observe, a, b) != -25)
    return 1;
  b.emplace<0>(short(-1));
  return std::visit(observe, a, b) != 4;
}
)r19containers");
  const auto Output = tmpFile("variant-two-visit-const-operand-and-mutation.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-two-visit-const-operand-and-mutation" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantTwoVisitUsesCapturelessGenericVisitor) {
  const auto Source = tmpFile("variant-two-visit-generic-visitor.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int f(std::variant<int, short>& left, std::variant<int, short>& right) {
  return std::visit([](auto a, auto b) -> int { return a - b; }, left, right);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantTwoVisitRenamedMatrixDispatchRejected) {
  const auto Source = tmpFile("variant-two-visit-renamed-matrix-dispatch.cpp");
  writeFile(Source, R"r19containers(#define __dispatch changed_matrix_dispatch
#include <variant>
#undef __dispatch
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

TEST_F(TranslateTest, CoreV2TupleBudgetDeep16ExecutionRunAtBothOptimizations) {
  const auto Source = tmpFile("tuple-budget-deep-16-execution.cpp");
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
using T16 = std::tuple<T15>;
using Carrier = std::tuple<T1, T16>;
int main() {
  Carrier left{}, right{};
  std::get<0>(std::get<0>(left)) = 7;
  std::get<0>(std::get<0>(right)) = 19;
  std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<1>(left))))))))))))))))) = -23;
  std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<1>(right))))))))))))))))) = 31;
  std::swap(left, right);
  if (std::get<0>(std::get<0>(left)) != 19 ||
      std::get<0>(std::get<0>(right)) != 7)
    return 1;
  if (std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<1>(left))))))))))))))))) != 31 || std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<0>(std::get<1>(right))))))))))))))))) != -23)
    return 2;
  return 0;
}
)r19containers");
  const auto Output = tmpFile("tuple-budget-deep-16-execution.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("tuple-budget-deep-16-execution" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyInPlaceEvaluatesArgumentOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("any-in-place-evaluates-argument-once.cpp");
  writeFile(Source, R"r19containers(#include <any>
int calls;
int value() { ++calls; return 27; }
int main() {
  std::any a(std::in_place_type<int>, value());
  const auto *p = std::any_cast<int>(&a);
  return calls != 1 || !p || *p != 27;
}
)r19containers");
  const auto Output = tmpFile("any-in-place-evaluates-argument-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-in-place-evaluates-argument-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmplaceIntegerEvaluatesArrowBeforeArgumentRunAtBothOptimizations) {
  const auto Source = tmpFile("any-emplace-integer-evaluates-arrow-before-argument.cpp");
  writeFile(Source, R"r19containers(#include <any>
int receivers, inputs, order;
std::any* receiver(std::any& a) { ++receivers; return &a; }
int input() { ++inputs; order = receivers; return 89; }
int main() {
  std::any a;
  int& item = receiver(a)->emplace<int>(input());
  if (receivers != 1 || inputs != 1 || order != 1 || item != 89)
    return 1;
  item = 39;
  return *std::any_cast<int>(&a) != 39;
}
)r19containers");
  const auto Output = tmpFile("any-emplace-integer-evaluates-arrow-before-argument.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-emplace-integer-evaluates-arrow-before-argument" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantOwnedVisitOverloadByActiveAlternativeRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-owned-visit-overload-by-active-alternative.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Visitor {
  int seen;
  int operator()(short a, int b) & { ++seen; return a * 100 + b; }
  int operator()(int a, int b) & { ++seen; return a * 1000 + b; }
};
int main() {
  Visitor call{0};
  std::variant<short, int> left(std::in_place_index<0>, short(8));
  std::variant<bool, int> right(std::in_place_index<1>, 23);
  if (std::visit(call, left, right) != 823 || call.seen != 1)
    return 1;
  left.emplace<1>(9);
  right.emplace<0>(true);
  return std::visit(call, left, right) != 9001 || call.seen != 2;
}
)r19containers");
  const auto Output = tmpFile("variant-owned-visit-overload-by-active-alternative.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-owned-visit-overload-by-active-alternative" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyInPlaceDefaultDoubleAcceptsFunctionBody) {
  const auto Source = tmpFile("any-in-place-other-payload-guard.cpp");
  writeFile(Source, R"r19containers(#include <any>
bool f() {
  std::any a(std::in_place_type<double>);
  return a.has_value();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyInPlaceRenamedHandlerInstallationGuardRejected) {
  const auto Source = tmpFile("any-in-place-renamed-handler-installation-guard.cpp");
  writeFile(Source, R"r19containers(#define __create changed_handler_installation
#include <any>
#undef __create
bool f() {
  std::any a(std::in_place_type<int>, 51);
  return a.has_value();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantOwnedVisitRenamedDispatchRejected) {
  const auto Source = tmpFile("variant-owned-visit-renamed-dispatch.cpp");
  writeFile(Source, R"r19containers(#define __dispatch renamed_dispatch
#include <variant>
#undef __dispatch
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

TEST_F(TranslateTest, CoreV2AnyIntAssignAndReturnReceiverRunAtBothOptimizations) {
  const auto Source = tmpFile("any-int-assign-and-return-receiver.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  std::any a;
  std::any& result = (a = 23);
  auto* first = std::any_cast<int>(&a);
  if (&result != &a || !first || *first != 23)
    return 1;
  const int value = -51;
  return &a.operator=(value) != &a || *std::any_cast<int>(&a) != -51;
}
)r19containers");
  const auto Output = tmpFile("any-int-assign-and-return-receiver.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-int-assign-and-return-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyIntMakeZeroInDestinationRunAtBothOptimizations) {
  const auto Source = tmpFile("any-int-make-zero-in-destination.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  const std::any a = std::make_any<int>();
  auto* value = std::any_cast<const int>(&a);
  return !a.has_value() || !value || *value != 0;
}
)r19containers");
  const auto Output = tmpFile("any-int-make-zero-in-destination.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-int-make-zero-in-destination" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyIntConstValueAndBorrowedReferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("any-int-const-value-and-borrowed-reference.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  const std::any a(17);
  const int& reference = std::any_cast<const int&>(a);
  if (&reference != std::any_cast<int>(&a))
    return 1;
  int result = std::any_cast<int>(a);
  return result != 17 || reference != 17;
}
)r19containers");
  const auto Output = tmpFile("any-int-const-value-and-borrowed-reference.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-int-const-value-and-borrowed-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyIntMakeAndAssignWithCopyRunAtBothOptimizations) {
  const auto Source = tmpFile("any-int-make-and-assign-with-copy.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  auto a = std::make_any<int>(68);
  auto b = a;
  a = 9;
  return *std::any_cast<int>(&a) != 9 || *std::any_cast<int>(&b) != 68;
}
)r19containers");
  const auto Output = tmpFile("any-int-make-and-assign-with-copy.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-int-make-and-assign-with-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ScopedAllocatorSameTypeMoveRunAtBothOptimizations) {
  const auto Source = tmpFile("scoped-allocator-same-type-move.cpp");
  writeFile(Source, R"r19containers(#include <memory>
#include <scoped_allocator>
using Alloc = std::scoped_allocator_adaptor<std::allocator<int>>;
int visits;
Alloc&& choose(Alloc& allocation) { ++visits; return static_cast<Alloc&&>(allocation); }
int main() {
  Alloc original;
  Alloc moved(choose(original));
  int* slot = moved.allocate(1);
  original.construct(slot, 37);
  int value = *slot;
  original.destroy(slot);
  moved.deallocate(slot, 1);
  return visits != 1 || value != 37;
}
)r19containers");
  const auto Output = tmpFile("scoped-allocator-same-type-move.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("scoped-allocator-same-type-move" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantThreeVisitFunctionAndPointerRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-three-visit-function-and-pointer.cpp");
  writeFile(Source, R"r19containers(#include <variant>
long combine(int a, long b, int c) { return a * 100L + b * 10L + c; }
int main() {
  std::variant<short, int> a(std::in_place_index<0>, short(7));
  std::variant<long, int> b(std::in_place_index<1>, 4);
  const std::variant<short, int> c(std::in_place_index<1>, 2);
  if (std::visit(combine, a, b, c) != 742)
    return 1;
  a.emplace<1>(2);
  b.emplace<0>(5L);
  return std::visit(&combine, a, b, c) != 252;
}
)r19containers");
  const auto Output = tmpFile("variant-three-visit-function-and-pointer.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-three-visit-function-and-pointer" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantThreeVisitAliasedOperandsAndVoidRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-three-visit-aliased-operands-and-void.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<short, int>;
using Visitor = void (*)(int, int, int);
int first_calls, second_calls, third_calls, visitor_calls, invoked, seen;
void combine(int a, int b, int c) { ++invoked; seen = a * 100 + b * 10 + c; }
Visitor visitor() { ++visitor_calls; return &combine; }
Value& first(Value& v) { ++first_calls; return v; }
Value& second(Value& v) { ++second_calls; return v; }
const Value& third(const Value& v) { ++third_calls; return v; }
int main() {
  Value v(std::in_place_index<1>, 4);
  std::visit(visitor(), first(v), second(v), third(v));
  return seen != 444 || first_calls != 1 || second_calls != 1 ||
         third_calls != 1 || visitor_calls != 1 || invoked != 1;
}
)r19containers");
  const auto Output = tmpFile("variant-three-visit-aliased-operands-and-void.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-three-visit-aliased-operands-and-void" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitAcceptsFourOperands) {
  const auto Source = tmpFile("variant-three-visit-four-operands.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<short, int>;
int combine(int a, int b, int c, int d) { return a + b + c + d; }
int f(Value& a, Value& b, Value& c, Value& d) { return std::visit(combine, a, b, c, d); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyIntMakeRenamedInstallationGuardRejected) {
  const auto Source = tmpFile("any-int-make-renamed-installation-guard.cpp");
  writeFile(Source, R"r19containers(#define __create changed_int_installation
#include <any>
#undef __create
bool f() {
  auto a = std::make_any<int>(7);
  return a.has_value();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ScopedAllocatorRenamedStorageCopyGuardRejected) {
  const auto Source = tmpFile("scoped-allocator-renamed-storage-copy-guard.cpp");
  writeFile(Source, R"r19containers(#include <memory>
#define __scoped_allocator_storage changed_scoped_storage
#include <scoped_allocator>
#undef __scoped_allocator_storage
using Alloc = std::scoped_allocator_adaptor<std::allocator<int>>;
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

TEST_F(TranslateTest, CoreV2VariantThreeVisitRenamedValuelessGuardRejected) {
  const auto Source = tmpFile("variant-three-visit-renamed-valueless-guard.cpp");
  writeFile(Source, R"r19containers(#define __throw_if_valueless renamed_valueless_guard
#include <variant>
#undef __throw_if_valueless
using Value = std::variant<short, int>;
int combine(int a, int b, int c) { return a + b + c; }
int f(Value& a, Value& b, Value& c) { return std::visit(combine, a, b, c); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantEqualityMatchingAndDifferentLiveAlternativesRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-equality-matching-and-different-live-alternatives.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int main() {
  std::variant<short, int> a(std::in_place_index<0>, short(-3));
  std::variant<short, int> b(std::in_place_index<0>, short(-3));
  if (!(a == b) || a != b) return 1;
  b.emplace<0>(short(-4));
  if (a == b || !(a != b)) return 2;
  b.emplace<1>(-3);
  if (a == b || !(a != b)) return 3;
  a.emplace<1>(-3);
  return !(a == b) || a != b;
}
)r19containers");
  const auto Output = tmpFile("variant-equality-matching-and-different-live-alternatives.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-equality-matching-and-different-live-alternatives" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantEqualityAliasedOperandsEvaluateOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-equality-aliased-operands-evaluate-once.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<short, int>;
int left_calls, right_calls;
const Value& left(const Value& v) { ++left_calls; return v; }
const Value& right(const Value& v) { ++right_calls; return v; }
int main() {
  const Value v(std::in_place_index<1>, 9);
  const Value w(std::in_place_index<1>, 8);
  if (!(left(v) == right(v))) return 1;
  if (!(left(v) != right(w))) return 2;
  return left_calls != 2 || right_calls != 2;
}
)r19containers");
  const auto Output = tmpFile("variant-equality-aliased-operands-evaluate-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-equality-aliased-operands-evaluate-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantEqualityRenamedDiagonalVisitorRejected) {
  const auto Source = tmpFile("variant-equality-renamed-diagonal-visitor.cpp");
  writeFile(Source, R"r19containers(#define __visit_value_at changed_diagonal_visitor
#include <variant>
#undef __visit_value_at
using Value = std::variant<short, int>;
bool equal(const Value& a, const Value& b) { return a == b; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantOrderingIndexBeforeValueRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-ordering-index-before-value.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int main() {
  using Value = std::variant<int, short>;
  Value left(std::in_place_index<0>, 1000);
  Value right(std::in_place_index<1>, short(-9));
  if (!(left < right) || !(left <= right) || left > right || left >= right)
    return 1;
  if (right < left || right <= left || !(right > left) || !(right >= left))
    return 2;
  left.emplace<1>(short(-9));
  return left < right || left > right || !(left <= right) || !(left >= right);
}
)r19containers");
  const auto Output = tmpFile("variant-ordering-index-before-value.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-ordering-index-before-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantOrderingOnceOnlyConstOperandsRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-ordering-once-only-const-operands.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<short, int>;
int left_reads, right_reads;
const Value& left(const Value& v) { ++left_reads; return v; }
const Value& right(const Value& v) { ++right_reads; return v; }
int main() {
  const Value a(std::in_place_index<0>, short(3));
  const Value b(std::in_place_index<0>, short(7));
  if (!std::operator<=(left(a), right(b))) return 1;
  if (std::operator>(left(a), right(a))) return 2;
  return left_reads != 2 || right_reads != 2;
}
)r19containers");
  const auto Output = tmpFile("variant-ordering-once-only-const-operands.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-ordering-once-only-const-operands" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantOrderingAcceptsTemporaryOperand) {
  const auto Source = tmpFile("variant-ordering-temporary-operand.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<short, int>;
bool compare(const Value& a) { return a <= Value(3); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantMonostateComparisonSameIndexHasNoPayloadOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-monostate-comparison-same-index-has-no-payload-order.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<std::monostate, int>;
int main() {
  const Value a, b;
  if (!(a == b) || a != b || a < b || a > b || !(a <= b) || !(a >= b))
    return 1;
  const Value c(std::in_place_index<1>, -9), d(std::in_place_index<1>, 3);
  if (!(a < c) || !(c > a) || a == c || !(a != c)) return 2;
  return !(c < d) || !(d >= c) || c == d;
}
)r19containers");
  const auto Output = tmpFile("variant-monostate-comparison-same-index-has-no-payload-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-monostate-comparison-same-index-has-no-payload-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMonostateComparisonReplacementEmptyClassRejected) {
  const auto Source = tmpFile("variant-monostate-comparison-replacement-empty-class.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Empty {};
constexpr bool operator<(Empty, Empty) noexcept { return true; }
using Value = std::variant<Empty, int>;
bool compare(const Value& a, const Value& b) { return a < b; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantMonostateHashTemporaryRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-monostate-hash-temporary.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int main() {
  std::monostate value{};
  return std::hash<std::monostate>{}(value) != 66740831;
}
)r19containers");
  const auto Output = tmpFile("variant-monostate-hash-temporary.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-monostate-hash-temporary" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMonostateHashAlternateSdkSpellingRejected) {
  const auto Source = tmpFile("variant-monostate-hash-alternate-sdk-spelling.cpp");
  writeFile(Source, R"r19containers(#define monostate alternate_monostate
#include <variant>
#undef monostate
int main() {
  std::alternate_monostate value{};
  return std::hash<std::alternate_monostate>{}(value) != 66740831;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantVisitAlternativesFunctionReferenceAliasRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-alternatives-function-reference-alias.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using State = std::variant<int, int>;
int* seen;
int bump(int& value) { seen = &value; return ++value; }
int main() {
  State state(std::in_place_index<1>, 40);
  int* active = std::get_if<1>(&state);
  if (!active || std::visit(bump, state) != 41 || seen != active || *active != 41) return 1;
  auto callback = &bump;
  return std::visit(callback, state) != 42 || state.index() != 1 ||
         *active != 42 || seen != active;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-alternatives-function-reference-alias.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-alternatives-function-reference-alias" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitAlternativesMonostateValueCopyRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-alternatives-monostate-value-copy.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Empty = std::monostate;
using State = std::variant<Empty, Empty>;
int calls;
int use(Empty lhs, const Empty& rhs) {
  ++calls;
  return &lhs != &rhs ? 41 : 0;
}
int main() {
  const State source(std::in_place_index<1>);
  auto callback = &use;
  return std::visit(callback, source, source) != 41 || calls != 1 ||
         source.index() != 1;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-alternatives-monostate-value-copy.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-alternatives-monostate-value-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitReferenceResultAcceptsFunctionBody) {
  const auto Source = tmpFile("variant-visit-alternatives-reference-result.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int& forward(int& value) { return value; }
int& f(std::variant<int>& state) { return std::visit(forward, state); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantVisitAlternativesReferenceToConvertedTemporaryRejected) {
  const auto Source = tmpFile("variant-visit-alternatives-reference-to-converted-temporary.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int read(const double& value) { return value != 0; }
int f(std::variant<int>& state) { return std::visit(read, state); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedObserverQueryMapObserverSignaturesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-observer-query-map-observer-signatures.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <type_traits>
#include <utility>
using Container = std::map<int, int>;
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
  const auto Output = tmpFile("ordered-observer-query-map-observer-signatures.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-observer-query-map-observer-signatures" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedObserverQueryMultimapObserverSignaturesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-observer-query-multimap-observer-signatures.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <type_traits>
#include <utility>
using Container = std::multimap<int, int>;
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
  const auto Output = tmpFile("ordered-observer-query-multimap-observer-signatures.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-observer-query-multimap-observer-signatures" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedEmptyOwnerOriginalMapRunsAtBothOptimizations) {
  const auto Source = tmpFile("ordered-observer-query-observer-does-not-admit-storage.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() { std::map<int, int> values; return values.empty() ? 0 : 1; }
)r19containers");
  const auto Output = tmpFile("ordered-observer-query-observer-does-not-admit-storage.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-observer-query-observer-does-not-admit-storage" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedObserverQueryRenamedSdkMemberRejected) {
  const auto Source = tmpFile("ordered-observer-query-renamed-sdk-member.cpp");
  writeFile(Source, R"r19containers(#define size changed_set_size
#include <set>
#undef size
#include <utility>
using Set = std::multiset<int>;
int main() { return !noexcept(std::declval<const Set&>().changed_set_size()); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2MonostateAdapterHashTupleOfReferencesRunAtBothOptimizations) {
  const auto Source = tmpFile("monostate-adapter-hash-tuple-of-references.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <tuple>
#include <variant>
using M = std::monostate;
using Args = std::tuple<const M&>;
int calls;
const M& borrow(const M& m) { ++calls; return m; }
int main() {
  const M m{};
  const Args args = std::tie(borrow(m));
  return std::apply(std::hash<M>{}, args) != 66740831 || calls != 1;
}
)r19containers");
  const auto Output = tmpFile("monostate-adapter-hash-tuple-of-references.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("monostate-adapter-hash-tuple-of-references" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2MonostateAdapterHashWrappedHasherThroughInvokeRunAtBothOptimizations) {
  const auto Source = tmpFile("monostate-adapter-hash-wrapped-hasher-through-invoke.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <variant>
using M = std::monostate;
using H = std::hash<M>;
int calls;
const M& borrow(const M& m) { ++calls; return m; }
int main() {
  const H h{};
  const M m{};
  auto ref = std::cref(h);
  return std::invoke(ref, borrow(m)) != 66740831 || calls != 1;
}
)r19containers");
  const auto Output = tmpFile("monostate-adapter-hash-wrapped-hasher-through-invoke.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("monostate-adapter-hash-wrapped-hasher-through-invoke" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2MonostateAdapterHashRenamedSdkMonostateRejected) {
  const auto Source = tmpFile("monostate-adapter-hash-renamed-sdk-monostate.cpp");
  writeFile(Source, R"r19containers(#define monostate changed_monostate
#include <variant>
#undef monostate
#include <functional>
int main() {
  std::changed_monostate m{};
  return std::invoke(std::hash<std::changed_monostate>{}, m) != 66740831;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantGuardedGetMutableReferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-guarded-get-mutable-reference.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using V = std::variant<int, int>;
int increment(V& state) {
  if (state.index() == 1) {
    int& item = (std::get<1>(state));
    item += 3;
    return &item == std::get_if<1>(&state) ? item : -2;
  }
  return -1;
}
int main() {
  V active(std::in_place_index<1>, 4);
  V inactive(std::in_place_index<0>, 2);
  return increment(active) != 7 || increment(inactive) != -1 ||
         *std::get_if<1>(&active) != 7 || *std::get_if<0>(&inactive) != 2;
}
)r19containers");
  const auto Output = tmpFile("variant-guarded-get-mutable-reference.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-guarded-get-mutable-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantGuardedGetShortCircuitRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-guarded-get-short-circuit.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using V = std::variant<int, double>;
bool read(V& state) {
  return std::holds_alternative<int>(state) && std::get<int>(state);
}
int main() {
  V zero{0};
  V nonzero{6};
  V inactive{4.5};
  return read(zero) || !read(nonzero) || read(inactive);
}
)r19containers");
  const auto Output = tmpFile("variant-guarded-get-short-circuit.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-guarded-get-short-circuit" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantGuardedGetGuardForOtherObjectRejected) {
  const auto Source = tmpFile("variant-guarded-get-guard-for-other-object.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using V = std::variant<int, double>;
int read(V& a, V& b) {
  if (std::holds_alternative<int>(a)) return std::get<int>(b);
  return 0;
}
int main() { V a{1}; V b{2.5}; return read(a, b); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantGuardedGetChangedAfterGuardRejected) {
  const auto Source = tmpFile("variant-guarded-get-changed-after-guard.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using V = std::variant<int, double>;
int read(V& state) {
  if (std::holds_alternative<int>(state)) {
    state.emplace<1>(2.5);
    return std::get<0>(state);
  }
  return 0;
}
int main() { V state{1}; return read(state); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyUnsignedInlineConstructorAndTypedPointerRunAtBothOptimizations) {
  const auto Source = tmpFile("any-unsigned-inline-constructor-and-typed-pointer.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  std::any value(~0u);
  if (std::any_cast<int>(&value) != nullptr) return 1;
  auto* payload = std::any_cast<unsigned>(&value);
  if (!payload || *payload != ~0u) return 2;
  *payload = 23u;
  const std::any& view = value;
  auto* read = std::any_cast<unsigned>(&view);
  return !read || read != payload || *read != 23u;
}
)r19containers");
  const auto Output = tmpFile("any-unsigned-inline-constructor-and-typed-pointer.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-unsigned-inline-constructor-and-typed-pointer" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyUnsignedInlineFactoryCopyMoveAndResetRunAtBothOptimizations) {
  const auto Source = tmpFile("any-unsigned-inline-factory-copy-move-and-reset.cpp");
  writeFile(Source, R"r19containers(#include <any>
#include <utility>
int main() {
  auto first = std::make_any<unsigned>(37u);
  std::any copy(first);
  std::any moved(std::move(first));
  if (first.has_value() || !copy.has_value() || !moved.has_value()) return 1;
  auto* a = std::any_cast<unsigned>(&copy);
  auto* b = std::any_cast<unsigned>(&moved);
  if (!a || !b || a == b || *a != 37u || *b != 37u) return 2;
  copy.reset();
  return std::any_cast<unsigned>(&copy) != nullptr || *b != 37u;
}
)r19containers");
  const auto Output = tmpFile("any-unsigned-inline-factory-copy-move-and-reset.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-unsigned-inline-factory-copy-move-and-reset" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyUnsignedLongMakeRetainsValueAtBothOptimizations) {
  const auto Source = tmpFile("any-unsigned-inline-unsigned-long-is-not-unsigned-int.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  auto value = std::make_any<unsigned long>(17ul);
  return value.has_value();
}
)r19containers");
  const auto Output = tmpFile("any-unsigned-inline-unsigned-long-is-not-unsigned-int.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-unsigned-inline-unsigned-long-is-not-unsigned-int" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 1) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyUnsignedInlineUnprovedUnsignedValueCastRejected) {
  const auto Source = tmpFile("any-unsigned-inline-unproved-unsigned-value-cast.cpp");
  writeFile(Source, R"r19containers(#include <any>
unsigned read_value(const std::any& value) { return std::any_cast<unsigned>(value); }
int main() { return 0; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantGetEarlyReturnReturnsOnDifferentTypeRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-get-early-return-returns-on-different-type.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using V = std::variant<int, double>;
int read(const V& state) {
  if (!std::holds_alternative<int>(state)) return -1;
  return std::get<int>(state);
}
int main() {
  const V normal{18};
  const V different{3.5};
  return read(normal) != 18 || read(different) != -1;
}
)r19containers");
  const auto Output = tmpFile("variant-get-early-return-returns-on-different-type.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-get-early-return-returns-on-different-type" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantGetEarlyReturnCheckedElseBranchRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-get-early-return-checked-else-branch.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using V = std::variant<std::monostate, long>;
long read(V& state) {
  if (state.index() != 1) return -1;
  else {
    auto& hold = std::get<1>(state);
    hold += 3;
    return hold;
  }
}
int main() {
  V inactive;
  V active{6L};
  return read(inactive) != -1 || read(active) != 9L;
}
)r19containers");
  const auto Output = tmpFile("variant-get-early-return-checked-else-branch.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-get-early-return-checked-else-branch" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantGetEarlyReturnFailingBranchFallsThroughRejected) {
  const auto Source = tmpFile("variant-get-early-return-failing-branch-falls-through.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int read(std::variant<int, double>& state) {
  if (state.index() != 0) { int elsewhere = 3; }
  return std::get<0>(state);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantGetEarlyReturnInterveningMutationRejected) {
  const auto Source = tmpFile("variant-get-early-return-intervening-mutation.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int read(std::variant<int, double>& state) {
  if (state.index() != 0) return -1;
  state.emplace<1>(7.5);
  return std::get<0>(state);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedEmptyOwnerMapObserversRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-empty-owner-map-observers.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> values;
  return !values.empty() || values.size() != 0;
}
)r19containers");
  const auto Output = tmpFile("ordered-empty-owner-map-observers.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-empty-owner-map-observers" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedEmptyOwnerSetReceiverOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-empty-owner-set-receiver-once.cpp");
  writeFile(Source, R"r19containers(#include <set>
const std::set<int>& selected(int& calls, const std::set<int>& values) {
  ++calls;
  return values;
}
int main() {
  std::set<int> values;
  int calls = 0;
  bool empty = selected(calls, values).empty();
  auto count = selected(calls, values).size();
  return !empty || count || calls != 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-empty-owner-set-receiver-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-empty-owner-set-receiver-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetOriginalInsertionRunsAtBothOptimizations) {
  const auto Source = tmpFile("ordered-empty-owner-set-insert.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  std::set<int> values;
  values.insert(9);
  return values.size() != 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-empty-owner-set-insert.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-empty-owner-set-insert" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedEmptyOwnerCustomComparatorRejected) {
  const auto Source = tmpFile("ordered-empty-owner-custom-comparator.cpp");
  writeFile(Source, R"r19containers(#include <set>
struct Reverse { bool operator()(int lhs, int rhs) const { return lhs > rhs; } };
int main() {
  std::set<int, Reverse> values;
  return values.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMaxSizeMapRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-max-size-map.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> values;
  auto limit = values.max_size();
  return limit <= values.size() || limit > 9223372036854775807ULL;
}
)r19containers");
  const auto Output = tmpFile("ordered-max-size-map.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-max-size-map" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMaxSizeSetReceiverOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-max-size-set-receiver-once.cpp");
  writeFile(Source, R"r19containers(#include <set>
const std::set<int>& select(const std::set<int>& values, int& calls) {
  ++calls;
  return values;
}
int main() {
  std::set<int> values;
  int calls = 0;
  auto limit = select(values, calls).max_size();
  return calls != 1 || limit <= values.size() || limit > 9223372036854775807ULL;
}
)r19containers");
  const auto Output = tmpFile("ordered-max-size-set-receiver-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-max-size-set-receiver-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMaxSizeMutableTreesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-max-size-mutable-trees.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::multimap<int, int> values;
  values.emplace(1, 2);
  return values.max_size() == 0;
}
)r19containers");
  const auto Output = tmpFile("ordered-max-size-mutable-trees.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-max-size-mutable-trees" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMaxSizeCustomComparatorRejected) {
  const auto Source = tmpFile("ordered-max-size-custom-comparator.cpp");
  writeFile(Source, R"r19containers(#include <map>
struct Reverse { bool operator()(int a, int b) const { return a > b; } };
using Values = std::map<int, int, Reverse>;
auto bound(const Values& values) { return values.max_size(); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyBoolInlineTypedMutablePointerRunAtBothOptimizations) {
  const auto Source = tmpFile("any-bool-inline-typed-mutable-pointer.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  std::any value{true};
  auto* flag = std::any_cast<bool>(&value);
  if (!flag || !*flag || std::any_cast<int>(&value)) return 1;
  *flag = false;
  const auto& view = value;
  auto* result = std::any_cast<const bool>(&view);
  return !result || *result || std::any_cast<unsigned>(&view) != nullptr;
}
)r19containers");
  const auto Output = tmpFile("any-bool-inline-typed-mutable-pointer.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-bool-inline-typed-mutable-pointer" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyBoolInlineCopyMoveAndAssignmentRunAtBothOptimizations) {
  const auto Source = tmpFile("any-bool-inline-copy-move-and-assignment.cpp");
  writeFile(Source, R"r19containers(#include <any>
#include <utility>
int main() {
  std::any left{true};
  std::any right{7u};
  left.swap(right);
  std::any transferred(std::move(right));
  if (right.has_value() || !std::any_cast<unsigned>(&left)) return 1;
  const auto* first = std::any_cast<bool>(&transferred);
  if (!first || !*first) return 2;
  left = false;
  transferred = left;
  const auto* copied = std::any_cast<bool>(&transferred);
  return !copied || *copied || std::any_cast<unsigned>(&left) != nullptr;
}
)r19containers");
  const auto Output = tmpFile("any-bool-inline-copy-move-and-assignment.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-bool-inline-copy-move-and-assignment" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyBooleanPointerPayloadRunsAtBothOptimizations) {
  const auto Source = tmpFile("any-bool-inline-pointer-payload.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  bool truth = true;
  const std::any value{&truth};
  return !value.has_value();
}
)r19containers");
  const auto Output = tmpFile("any-bool-inline-pointer-payload.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-bool-inline-pointer-payload" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyBoolInlineValueMismatchRejected) {
  const auto Source = tmpFile("any-bool-inline-value-mismatch.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  const std::any value{1};
  return std::any_cast<bool>(value) ? 0 : 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2EmptyHashSetSetSizeRunAtBothOptimizations) {
  const auto Source = tmpFile("empty-hash-set-set-size.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> values;
  return !values.empty() || values.size() != 0 || values.bucket_count() != 0;
}
)r19containers");
  const auto Output = tmpFile("empty-hash-set-set-size.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("empty-hash-set-set-size" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2EmptyHashSetReceiverOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("empty-hash-set-receiver-once.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
const std::unordered_set<int>& read(int& calls, const std::unordered_set<int>& values) {
  ++calls;
  return values;
}
int main() {
  std::unordered_set<int> values;
  int calls = 0;
  auto limit = read(calls, values).max_load_factor();
  return calls != 1 || limit != 1.0f;
}
)r19containers");
  const auto Output = tmpFile("empty-hash-set-receiver-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("empty-hash-set-receiver-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2EmptyHashSetLoadFactorSetterRunAtBothOptimizations) {
  const auto Source = tmpFile("empty-hash-set-load-factor-setter.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> values;
  values.max_load_factor(2.0f);
  return values.max_load_factor() != 2.0f;
}
)r19containers");
  const auto Output = tmpFile("empty-hash-set-load-factor-setter.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("empty-hash-set-load-factor-setter" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalEmptyCopyRunsAtBothOptimizations) {
  const auto Source = tmpFile("empty-hash-set-copy.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::unordered_multiset<int> source;
  std::unordered_multiset<int> copy(source);
  return copy.empty();
}
)r19containers");
  const auto Output = tmpFile("empty-hash-set-copy.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("empty-hash-set-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 1) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyFloatInlinePointerWriteRunAtBothOptimizations) {
  const auto Source = tmpFile("any-float-inline-pointer-write.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  std::any value{1.25f};
  float* slot = std::any_cast<float>(&value);
  if (!slot || *slot != 1.25f || std::any_cast<double>(&value)) return 1;
  *slot = 2.5f;
  const std::any* view = &value;
  const float* saved = std::any_cast<float>(view);
  return !saved || *saved != 2.5f || std::any_cast<bool>(&value);
}
)r19containers");
  const auto Output = tmpFile("any-float-inline-pointer-write.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-float-inline-pointer-write" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyFloatInlineFactoryEmplaceResetRunAtBothOptimizations) {
  const auto Source = tmpFile("any-float-inline-factory-emplace-reset.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  auto value = std::make_any<float>(3.25f);
  auto* first = std::any_cast<float>(&value);
  if (!first || *first != 3.25f) return 1;
  value.emplace<unsigned int>(7u);
  if (std::any_cast<float>(&value)) return 2;
  float& second = value.emplace<float>(-0.5f);
  if (second != -0.5f || std::any_cast<unsigned int>(&value)) return 3;
  value.reset();
  return value.has_value() || std::any_cast<float>(&value) != nullptr;
}
)r19containers");
  const auto Output = tmpFile("any-float-inline-factory-emplace-reset.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-float-inline-factory-emplace-reset" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyFloatMutableReferenceAcceptsFunctionBody) {
  const auto Source = tmpFile("any-float-inline-mutable-reference.cpp");
  writeFile(Source, R"r19containers(#include <any>
float edit() {
  std::any value{1.5f};
  return ++std::any_cast<float&>(value);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyFloatInlineUnknownActiveValueRejected) {
  const auto Source = tmpFile("any-float-inline-unknown-active-value.cpp");
  writeFile(Source, R"r19containers(#include <any>
float read(const std::any& value) { return std::any_cast<float>(value); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedEmptyLoadFactorEmptySetRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-empty-load-factor-empty-set.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::unordered_set<int> value;
  return value.load_factor() != 0.0f || value.bucket_count() != 0 ||
         value.max_load_factor() != 1.0f;
}
)r19containers");
  const auto Output = tmpFile("unordered-empty-load-factor-empty-set.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-empty-load-factor-empty-set" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedEmptyLoadFactorSingleReceiverEvaluationRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-empty-load-factor-single-receiver-evaluation.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
const std::unordered_set<int>& observe(int& calls, const std::unordered_set<int>& value) {
  ++calls;
  return value;
}
int main() {
  const std::unordered_set<int> value;
  int calls = 0;
  float load = observe(calls, value).load_factor();
  return calls != 1 || load != 0.0f;
}
)r19containers");
  const auto Output = tmpFile("unordered-empty-load-factor-single-receiver-evaluation.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-empty-load-factor-single-receiver-evaluation" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedEmptyLoadFactorRehashRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-empty-load-factor-rehash.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> value;
  value.rehash(7);
  return value.load_factor() != 0.0f;
}
)r19containers");
  const auto Output = tmpFile("unordered-empty-load-factor-rehash.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-empty-load-factor-rehash" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalUnsignedLoadFactor) {
  const auto Source = tmpFile("unordered-empty-load-factor-different-key.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::unordered_multiset<unsigned int> value;
  return value.load_factor() != 0.0f;
}
)r19containers");
  const auto Output = tmpFile("unordered-empty-load-factor-different-key.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-empty-load-factor-different-key" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedEmptyCapacitySetMaxSizeRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-empty-capacity-set-max-size.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  using Set = std::unordered_set<int>;
  const Set values;
  Set::size_type maximum = values.max_size();
  return maximum != Set::size_type(-1) / 24 || !values.empty();
}
)r19containers");
  const auto Output = tmpFile("unordered-empty-capacity-set-max-size.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-empty-capacity-set-max-size" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedEmptyCapacitySingleReceiverEvaluationRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-empty-capacity-single-receiver-evaluation.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
using Set = std::unordered_set<int>;
const Set& observe(int& calls, const Set& values) {
  ++calls;
  return values;
}
int main() {
  const Set values;
  int calls = 0;
  Set::size_type maximum = observe(calls, values).max_bucket_count();
  return calls != 1 || maximum != values.max_size();
}
)r19containers");
  const auto Output = tmpFile("unordered-empty-capacity-single-receiver-evaluation.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-empty-capacity-single-receiver-evaluation" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultisetNonemptyMaxSizeRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-empty-capacity-nonempty-owner.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::unordered_multiset<int> values{3, 3};
  return values.max_size() == 0;
}
)r19containers");
  const auto Output = tmpFile("unordered-empty-capacity-nonempty-owner.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-empty-capacity-nonempty-owner" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedEmptyCapacityCustomHashRejected) {
  const auto Source = tmpFile("unordered-empty-capacity-custom-hash.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Hash { unsigned long long operator()(int value) const { return value; } };
int main() {
  const std::unordered_set<int, Hash> values;
  return values.max_size() == 0;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedLookupPoliciesSetHashValueRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-lookup-policies-set-hash-value.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::unordered_set<int> values;
  auto hash = values.hash_function();
  return hash(-7) != std::hash<int>{}(-7) || hash(9) != std::hash<int>{}(9);
}
)r19containers");
  const auto Output = tmpFile("unordered-lookup-policies-set-hash-value.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-lookup-policies-set-hash-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedLookupPoliciesHashReceiverEvaluatedOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-lookup-policies-hash-receiver-evaluated-once.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
using Set = std::unordered_multiset<int>;
const Set& read(int& calls, const Set& values) {
  ++calls;
  return values;
}
int main() {
  const Set values;
  int calls = 0;
  auto hash = read(calls, values).hash_function();
  return calls != 1 || hash(23) != std::hash<int>{}(23);
}
)r19containers");
  const auto Output = tmpFile("unordered-lookup-policies-hash-receiver-evaluated-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-lookup-policies-hash-receiver-evaluated-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultisetNonemptyEqualityRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-lookup-policies-nonempty-owner.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::unordered_multiset<int> values{1, 1};
  return !values.key_eq()(1, 1);
}
)r19containers");
  const auto Output = tmpFile("unordered-lookup-policies-nonempty-owner.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-lookup-policies-nonempty-owner" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedLookupPoliciesCustomHashRejected) {
  const auto Source = tmpFile("unordered-lookup-policies-custom-hash.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Hash { unsigned long long operator()(int value) const { return value + 1; } };
int main() {
  const std::unordered_set<int, Hash> values;
  return values.hash_function()(0) != 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedLoadLimitSetUpdateRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-load-limit-set-update.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> values;
  if (values.max_load_factor() != 1.0f) return 1;
  values.max_load_factor(2);
  return values.max_load_factor() != 2.0f || values.load_factor() != 0.0f;
}
)r19containers");
  const auto Output = tmpFile("unordered-load-limit-set-update.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-load-limit-set-update" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedLoadLimitReceiverBeforeArgumentRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-load-limit-receiver-before-argument.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
using Set = std::unordered_set<int>;
Set& observe(Set& values, int& trace) {
  trace = trace * 10 + 1;
  return values;
}
float requested(int& trace) {
  trace = trace * 10 + 2;
  return 1.75f;
}
int main() {
  Set values;
  int trace = 0;
  observe(values, trace).max_load_factor(requested(trace));
  return trace != 12 || values.max_load_factor() != 1.75f;
}
)r19containers");
  const auto Output = tmpFile("unordered-load-limit-receiver-before-argument.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-load-limit-receiver-before-argument" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultisetNonemptyLoadLimitRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-load-limit-nonempty-owner.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> values{3, 3};
  values.max_load_factor(3.0f);
  return values.max_load_factor() != 3.0f;
}
)r19containers");
  const auto Output = tmpFile("unordered-load-limit-nonempty-owner.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-load-limit-nonempty-owner" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedLoadLimitCustomHashRejected) {
  const auto Source = tmpFile("unordered-load-limit-custom-hash.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Hash { unsigned long long operator()(int value) const { return value; } };
int main() {
  std::unordered_set<int, Hash> values;
  values.max_load_factor(3.0f);
  return values.max_load_factor() != 3.0f;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyWideInlineDoublePointerPrecisionRunAtBothOptimizations) {
  const auto Source = tmpFile("any-wide-inline-double-pointer-precision.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  std::any value{1.0000000000000002};
  const std::any* view = &value;
  const double* exact = std::any_cast<double>(view);
  if (!exact || *exact != 1.0000000000000002 || std::any_cast<float>(view)) return 1;
  double* writable = std::any_cast<double>(&value);
  if (!writable || writable != exact) return 2;
  *writable = -2.25;
  return *exact != -2.25 || std::any_cast<long long>(view) != nullptr;
}
)r19containers");
  const auto Output = tmpFile("any-wide-inline-double-pointer-precision.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-wide-inline-double-pointer-precision" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyWideInlineWideSignedValueMoveRunAtBothOptimizations) {
  const auto Source = tmpFile("any-wide-inline-wide-signed-value-move.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  const std::any value{(-9223372036854775807LL - 1)};
  auto copied = value;
  auto moved = static_cast<std::any&&>(copied);
  auto* slot = std::any_cast<long long>(&moved);
  return copied.has_value() || !slot || *slot != (-9223372036854775807LL - 1) ||
      std::any_cast<long long>(value) != *slot || std::any_cast<unsigned long long>(&moved);
}
)r19containers");
  const auto Output = tmpFile("any-wide-inline-wide-signed-value-move.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-wide-inline-wide-signed-value-move" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyWideInlineWideTagsAndSwapRunAtBothOptimizations) {
  const auto Source = tmpFile("any-wide-inline-wide-tags-and-swap.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  std::any a{7LL}, b{7ULL};
  a.swap(b);
  const auto* u = std::any_cast<unsigned long long>(&a);
  const auto* s = std::any_cast<long long>(&b);
  if (!u || !s || *u != 7ULL || *s != 7LL || std::any_cast<long long>(&a)) return 1;
  b = 7.0;
  auto* d = std::any_cast<double>(&b);
  if (!d || *d != 7.0 || std::any_cast<long long>(&b)) return 2;
  a = 7u;
  return !std::any_cast<unsigned int>(&a) || std::any_cast<unsigned long long>(&a);
}
)r19containers");
  const auto Output = tmpFile("any-wide-inline-wide-tags-and-swap.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-wide-inline-wide-tags-and-swap" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyWideMutableReferenceAcceptsFunctionBody) {
  const auto Source = tmpFile("any-wide-inline-mutable-wide-reference.cpp");
  writeFile(Source, R"r19containers(#include <any>
long long edit() {
  std::any value{5LL};
  return ++std::any_cast<long long&>(value);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyWideInlineMismatchedWideValueRejected) {
  const auto Source = tmpFile("any-wide-inline-mismatched-wide-value.cpp");
  writeFile(Source, R"r19containers(#include <any>
double read() {
  const std::any value{1.0f};
  return std::any_cast<double>(value);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedComparatorSetKeyAndValueRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-comparator-set-key-and-value.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  const std::set<int> values;
  auto keys = values.key_comp();
  auto elements = values.value_comp();
  return !keys(-7, 3) || keys(3, -7) || !elements(0, 1) || elements(1, 1);
}
)r19containers");
  const auto Output = tmpFile("ordered-comparator-set-key-and-value.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-comparator-set-key-and-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedComparatorMapKeyWrapperRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-comparator-map-key-wrapper.cpp");
  writeFile(Source, R"r19containers(#include <map>
using Map = std::map<int, int>;
const Map& observe(const Map& values, int& calls) {
  ++calls;
  return values;
}
int main() {
  const Map values;
  int calls = 0;
  auto cmp = observe(values, calls).key_comp();
  return calls != 1 || !cmp(-8, 2) || cmp(2, 2);
}
)r19containers");
  const auto Output = tmpFile("ordered-comparator-map-key-wrapper.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-comparator-map-key-wrapper" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedComparatorOriginalArrowReceiverRunsAtBothOptimizations) {
  const auto Source = tmpFile("ordered-comparator-arrow-receiver.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::multimap<int, int> values;
  const auto* pointer = &values;
  auto cmp = pointer->key_comp();
  return !cmp(1, 2);
}
)r19containers");
  const auto Output = tmpFile("ordered-comparator-arrow-receiver.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-comparator-arrow-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedComparatorCustomComparatorRejected) {
  const auto Source = tmpFile("ordered-comparator-custom-comparator.cpp");
  writeFile(Source, R"r19containers(#include <set>
struct Greater { bool operator()(int a, int b) const { return a > b; } };
int main() {
  std::set<int, Greater> values;
  auto cmp = values.key_comp();
  return !cmp(2, 1);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedBucketStorageUniqueRehashRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-bucket-storage-unique-rehash.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> values;
  values.rehash(7);
  if (values.bucket_count() != 7 || values.load_factor() != 0.0f) return 1;
    if (values.bucket(10) != 3 || values.bucket_size(3) != 0) return 2;
  values.rehash(1);
    if (values.bucket_count() != 2 || !values.empty()) return 3;
  values.rehash(0);
  return values.bucket_count() != 0 || values.size() != 0;
}
)r19containers");
  const auto Output = tmpFile("unordered-bucket-storage-unique-rehash.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-bucket-storage-unique-rehash" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedBucketStorageResizeArgumentsOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-bucket-storage-resize-arguments-once.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
using Set = std::unordered_set<int>;
using Size = decltype(sizeof(0));
int trace;
Set& owner(Set& value) { trace = trace * 10 + 1; return value; }
Size count() { trace = trace * 10 + 2; return 4; }
int main() {
  Set values;
  owner(values).reserve(count());
  return trace != 12 || values.bucket_count() != 4;
}
)r19containers");
  const auto Output = tmpFile("unordered-bucket-storage-resize-arguments-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-bucket-storage-resize-arguments-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultisetNonemptyRehashRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-bucket-storage-nonempty-owner.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_multiset<int> values{1, 3}; values.rehash(4); return values.size(); }
)r19containers");
  const auto Output = tmpFile("unordered-bucket-storage-nonempty-owner.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-bucket-storage-nonempty-owner" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 2) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedBucketStorageCustomHasherRejected) {
  const auto Source = tmpFile("unordered-bucket-storage-custom-hasher.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Hash { using Size = decltype(sizeof(0)); Size operator()(int value) const { return Size(value); } };
int main() { std::unordered_set<int, Hash> values; values.rehash(4); return values.size(); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetValuesBracesDuplicatesAndCountRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-values-braces-duplicates-and-count.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> values{3, 1, 3, -2};
  return values.size() != 3 || values.count(3) != 1 || values.count(-2) != 1 ||
      values.count(8) != 0 || values.empty();
}
)r19containers");
  const auto Output = tmpFile("unordered-set-values-braces-duplicates-and-count.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-values-braces-duplicates-and-count" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetValuesNonemptyRehashAndBucketsRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-values-nonempty-rehash-and-buckets.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> values{2, 10, 3, 11, 2};
  values.rehash(8);
  const auto b = values.bucket(10);
  if (values.bucket_count() != 8 || values.bucket_size(b) != 2 || values.load_factor() != 0.5f) return 1;
  values.reserve(30);
  return values.bucket_count() < 30 || values.count(3) != 1 || values.count(11) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-values-nonempty-rehash-and-buckets.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-values-nonempty-rehash-and-buckets" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultisetRetainsDuplicateKeysRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-values-multiple-key-owner.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_multiset<int> values{1, 1}; return values.size() != 2; }
)r19containers");
  const auto Output = tmpFile("unordered-set-values-multiple-key-owner.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-values-multiple-key-owner" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetValuesDifferentKeyListRejected) {
  const auto Source = tmpFile("unordered-set-values-different-key-list.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_set<long> values{1, 2}; return values.size() != 2; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetModifiersEraseCollisionsRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-modifiers-erase-collisions.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> values;
  values.rehash(8);
  values.insert({2, 10, 18, 3});
  if (values.erase(10) != 1 || values.erase(10) != 0) return 1;
  if (values.bucket_size(values.bucket(2)) != 2 || values.count(18) != 1) return 2;
  if (values.erase(2) != 1 || values.erase(18) != 1 || values.bucket_size(2) != 0) return 3;
  return values.bucket_count() != 8 || values.size() != 1 || values.count(3) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-modifiers-erase-collisions.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-modifiers-erase-collisions" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetModifiersInsertAfterClearRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-modifiers-insert-after-clear.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> values{4, 9, 12};
  values.rehash(16);
  values.clear();
  std::initializer_list<int> input{5, -2, 5};
  values.insert(input);
  values.insert({});
  return values.size() != 2 || values.count(5) != 1 || values.count(-2) != 1 ||
      values.count(4) != 0 || values.bucket_count() != 16;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-modifiers-insert-after-clear.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-modifiers-insert-after-clear" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultisetEraseOriginalEmptyRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-modifiers-multi-key-erase.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_multiset<int> values; return values.erase(2); }
)r19containers");
  const auto Output = tmpFile("unordered-set-modifiers-multi-key-erase.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-modifiers-multi-key-erase" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetModifiersDifferentKeyRejected) {
  const auto Source = tmpFile("unordered-set-modifiers-different-key.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_set<long> values; values.insert({1, 2}); return values.erase(2); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyNarrowInlineDistinctCharTagsRunAtBothOptimizations) {
  const auto Source = tmpFile("any-narrow-inline-distinct-char-tags.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  std::any value = static_cast<char>(39);
  auto* plain = std::any_cast<char>(&value);
  if (!plain || *plain != 39 || std::any_cast<signed char>(&value) || std::any_cast<unsigned char>(&value)) return 1;
  value = static_cast<signed char>(-17);
  auto* signed_value = std::any_cast<signed char>(&value);
  if (!signed_value || *signed_value != -17 || std::any_cast<char>(&value)) return 2;
  value = static_cast<unsigned char>(247);
  auto* unsigned_value = std::any_cast<unsigned char>(&value);
  return !unsigned_value || *unsigned_value != 247 || std::any_cast<signed char>(&value);
}
)r19containers");
  const auto Output = tmpFile("any-narrow-inline-distinct-char-tags.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-narrow-inline-distinct-char-tags" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyNarrowInlineSignedNarrowValueCastsRunAtBothOptimizations) {
  const auto Source = tmpFile("any-narrow-inline-signed-narrow-value-casts.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  std::any byte(static_cast<signed char>(-103));
  std::any word(static_cast<short>(-31249));
  return std::any_cast<signed char>(byte) != -103 || std::any_cast<short>(word) != -31249;
}
)r19containers");
  const auto Output = tmpFile("any-narrow-inline-signed-narrow-value-casts.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-narrow-inline-signed-narrow-value-casts" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyChar16OriginalConstructionRunAtBothOptimizations) {
  const auto Source = tmpFile("any-narrow-inline-unicode-payload.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() { std::any value = u'x'; return !value.has_value(); }
)r19containers");
  const auto Output = tmpFile("any-narrow-inline-unicode-payload.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-narrow-inline-unicode-payload" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyNarrowInlineDifferentSmallRecordRejected) {
  const auto Source = tmpFile("any-narrow-inline-different-small-record.cpp");
  writeFile(Source, R"r19containers(#include <any>
struct Tiny { unsigned char value; };
int main() { std::any value = Tiny{1}; return !value.has_value(); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyLongInlineDistinctNativeLongTagsRunAtBothOptimizations) {
  const auto Source = tmpFile("any-long-inline-distinct-native-long-tags.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  const long input = sizeof(long) == 8 ? static_cast<long>(-4000000007LL) : -2000000007L;
  std::any value = input;
  const auto* number = std::any_cast<long>(&value);
  if (!number || *number != input || std::any_cast<int>(&value) ||
      std::any_cast<long long>(&value) || std::any_cast<unsigned long>(&value)) return 1;
  const unsigned long bits = static_cast<unsigned long>(0xaedcb987f6543210ULL);
  value = bits;
  auto* word = std::any_cast<unsigned long>(&value);
  return !word || *word != bits || std::any_cast<unsigned int>(&value) ||
      std::any_cast<unsigned long long>(&value);
}
)r19containers");
  const auto Output = tmpFile("any-long-inline-distinct-native-long-tags.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-long-inline-distinct-native-long-tags" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyLongInlineLongEmplaceAndMoveRunAtBothOptimizations) {
  const auto Source = tmpFile("any-long-inline-long-emplace-and-move.cpp");
  writeFile(Source, R"r19containers(#include <any>
#include <utility>
int main() {
  std::any value;
  auto& n = value.emplace<long>();
  n = -19L;
  std::any receiver;
  receiver = std::move(value);
  const auto* moved = std::any_cast<long>(&receiver);
  return value.has_value() || !moved || *moved != -19L;
}
)r19containers");
  const auto Output = tmpFile("any-long-inline-long-emplace-and-move.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-long-inline-long-emplace-and-move" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyLongMutableReferenceRunsAtBothOptimizations) {
  const auto Source = tmpFile("any-long-inline-mutable-long-reference-cast.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  std::any value = 7L;
  return std::any_cast<long&>(value) != 7L;
}
)r19containers");
  const auto Output = tmpFile("any-long-inline-mutable-long-reference-cast.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-long-inline-mutable-long-reference-cast" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyLongInlineUnsignedLongTagMismatchRejected) {
  const auto Source = tmpFile("any-long-inline-unsigned-long-tag-mismatch.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  std::any value = 7UL;
  return std::any_cast<unsigned int>(value) != 7U;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedMultisetValuesCollisionBucketsPreserveMultiplicityRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multiset-values-collision-buckets-preserve-multiplicity.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> values{2, 10, 2, 18, 10, 2, 3};
  values.rehash(8);
  if (values.count(2) != 3 || values.count(10) != 2 || values.count(18) != 1) return 1;
  if (values.bucket_size(values.bucket(2)) != 6 || values.bucket_size(values.bucket(3)) != 1) return 2;
  values.rehash(32);
  return values.size() != 7 || values.bucket_size(values.bucket(10)) != 2 || values.count(0) != 0;
}
)r19containers");
  const auto Output = tmpFile("unordered-multiset-values-collision-buckets-preserve-multiplicity.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multiset-values-collision-buckets-preserve-multiplicity" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultisetValuesReleaseEachDuplicateNodeRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multiset-values-release-each-duplicate-node.cpp");
  writeFile(Source, R"r19containers(using Size = decltype(sizeof(0));
extern "C" void* malloc(Size);
extern "C" void free(void*);
#include <unordered_set>
int armed, releases;
void* operator new(Size bytes) { return malloc(bytes); }
void operator delete(void* ptr) noexcept { if (armed && ptr) ++releases; free(ptr); }
void operator delete(void* ptr, Size) noexcept { if (armed && ptr) ++releases; free(ptr); }
int main() {
  std::unordered_multiset<int> values{4, 4, 4, 8};
  armed = 1;
  values.clear();
  armed = 0;
  return releases != 4 || !values.empty();
}
)r19containers");
  const auto Output = tmpFile("unordered-multiset-values-release-each-duplicate-node.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multiset-values-release-each-duplicate-node" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultisetOriginalScalarInsertRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multiset-values-scalar-insert.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_multiset<int> values{1, 1}; values.insert(2); return values.size() != 3; }
)r19containers");
  const auto Output = tmpFile("unordered-multiset-values-scalar-insert.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multiset-values-scalar-insert" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultisetOriginalUnsignedValues) {
  const auto Source = tmpFile("unordered-multiset-values-different-key.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_multiset<unsigned> values{1u, 1u}; return values.count(1u) != 2; }
)r19containers");
  const auto Output = tmpFile("unordered-multiset-values-different-key.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multiset-values-different-key" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyCharacterInlineWideTagDistinctFromIntegersRunAtBothOptimizations) {
  const auto Source = tmpFile("any-character-inline-wide-tag-distinct-from-integers.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  std::any value = L'G';
  const auto* read = std::any_cast<wchar_t>(&value);
  return !read || *read != L'G' || std::any_cast<int>(&value) ||
      std::any_cast<unsigned short>(&value) || std::any_cast<char16_t>(&value);
}
)r19containers");
  const auto Output = tmpFile("any-character-inline-wide-tag-distinct-from-integers.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-character-inline-wide-tag-distinct-from-integers" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyCharacterInlineUtfPayloadCopyAndSwapRunAtBothOptimizations) {
  const auto Source = tmpFile("any-character-inline-utf-payload-copy-and-swap.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  auto input = std::make_any<char16_t>(u'X');
  std::any copy(input);
  input.emplace<char32_t>(U'Y');
  input.swap(copy);
  const auto* short_code = std::any_cast<char16_t>(&input);
  const auto* wide_code = std::any_cast<char32_t>(&copy);
  return !short_code || *short_code != u'X' || !wide_code || *wide_code != U'Y';
}
)r19containers");
  const auto Output = tmpFile("any-character-inline-utf-payload-copy-and-swap.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-character-inline-utf-payload-copy-and-swap" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyUtfMutableReferenceRunsAtBothOptimizations) {
  const auto Source = tmpFile("any-character-inline-mutable-utf-reference.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() { std::any value = u'A'; return std::any_cast<char16_t&>(value) != u'A'; }
)r19containers");
  const auto Output = tmpFile("any-character-inline-mutable-utf-reference.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-character-inline-mutable-utf-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyCharacterInlineAggregateWithUtfMemberRejected) {
  const auto Source = tmpFile("any-character-inline-aggregate-with-utf-member.cpp");
  writeFile(Source, R"r19containers(#include <any>
struct Code { char16_t value; };
int main() { std::any value = Code{u'A'}; return !value.has_value(); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedMultisetModifiersInsertListRetainsEachDuplicateRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multiset-modifiers-insert-list-retains-each-duplicate.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> values{3, 7, 3};
  values.insert({3, 7, 7, -4});
  values.insert({});
  return values.size() != 7 || values.count(3) != 3 || values.count(7) != 3 || values.count(-4) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-multiset-modifiers-insert-list-retains-each-duplicate.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multiset-modifiers-insert-list-retains-each-duplicate" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultisetModifiersRehashAfterErasingGroupsRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multiset-modifiers-rehash-after-erasing-groups.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> values{-1, 2, -1, 3, -1, 2};
  values.rehash(64);
  if (values.erase(-1) != 3 || values.count(-1) || values.size() != 3) return 1;
  values.reserve(257);
  values.insert({2, -1, 3, 2});
  if (values.count(2) != 4 || values.count(3) != 2 || values.count(-1) != 1) return 2;
  if (values.erase(2) != 4 || values.erase(3) != 2 || values.erase(-1) != 1 || !values.empty()) return 3;
  values.insert({-2, -2});
  values.clear();
  return !values.empty() || values.erase(-2) != 0;
}
)r19containers");
  const auto Output = tmpFile("unordered-multiset-modifiers-rehash-after-erasing-groups.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multiset-modifiers-rehash-after-erasing-groups" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultisetModifiersEraseOneFreePerDuplicateRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multiset-modifiers-erase-one-free-per-duplicate.cpp");
  writeFile(Source, R"r19containers(using Size = decltype(sizeof(0));
extern "C" void* malloc(Size);
extern "C" void free(void*);
#include <unordered_set>
int armed, frees;
void* operator new(Size n) { return malloc(n); }
void operator delete(void* p) noexcept { if (armed && p) ++frees; free(p); }
void operator delete(void* p, Size) noexcept { if (armed && p) ++frees; free(p); }
int main() {
  std::unordered_multiset<int> values;
  values.rehash(16);
  values.insert({2, 2, 3, 2});
  armed = 1;
  if (values.erase(2) != 3 || frees != 3 || values.erase(2) != 0 || frees != 3) return 1;
  values.clear();
  armed = 0;
  return frees != 4 || !values.empty();
}
)r19containers");
  const auto Output = tmpFile("unordered-multiset-modifiers-erase-one-free-per-duplicate.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multiset-modifiers-erase-one-free-per-duplicate" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultisetModifiersDifferentKeyListRejected) {
  const auto Source = tmpFile("unordered-multiset-modifiers-different-key-list.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_multiset<long> values; values.insert({1L, 1L}); return values.erase(1L); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetGlobalIteratorsForwardWalkRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-global-iterators-forward-walk.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> values{-3, 2, 9, -3};
  auto it = values.begin();
  const auto end = values.end();
  int sum = 0, visited = 0;
  while (it != end) { sum += *it++; ++visited; }
  return sum != 8 || visited != 3;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-global-iterators-forward-walk.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-global-iterators-forward-walk" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetGlobalIteratorsDuplicateTraversalRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-global-iterators-duplicate-traversal.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> values{3, 5, 3, 7, 5};
  const auto& read = values;
  unsigned seen = 0;
  for (auto it = read.cbegin(); it != read.cend(); ++it) if (*it == 5) ++seen;
  return seen != 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-global-iterators-duplicate-traversal.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-global-iterators-duplicate-traversal" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalLocalBucketRunsAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-global-iterators-local-bucket-iterator.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_set<int> values{5}; return values.begin(0) == values.end(0); }
)r19containers");
  const auto Output = tmpFile("unordered-set-global-iterators-local-bucket-iterator.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-global-iterators-local-bucket-iterator" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 1) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetGlobalIteratorsDifferentKeyIteratorRejected) {
  const auto Source = tmpFile("unordered-set-global-iterators-different-key-iterator.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_set<long> values{2L}; return *values.begin() != 2; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetScalarInsertionSetReturnsPositionAndInsertionFlagRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-scalar-insertion-set-returns-position-and-insertion-flag.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> values;
  values.reserve(8);
  auto first = values.insert(11);
  if (!first.second || first.first == values.end() || *first.first != 11) return 1;
  const int key = 11;
  auto second = values.insert(key);
  return second.second || second.first != first.first || second.first != values.find(key) || values.size() != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-scalar-insertion-set-returns-position-and-insertion-flag.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-scalar-insertion-set-returns-position-and-insertion-flag" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetScalarInsertionMultisetReturnsTheNewNodeRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-scalar-insertion-multiset-returns-the-new-node.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> values;
  values.reserve(8);
  const int key = -4;
  auto first = values.insert(key);
  auto second = values.insert(-4);
  return first == second || first == values.end() || second == values.end() ||
      *first != -4 || *second != -4 || values.count(-4) != 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-scalar-insertion-multiset-returns-the-new-node.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-scalar-insertion-multiset-returns-the-new-node" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalHintedInsertRunsAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-scalar-insertion-insert-with-hint.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_set<int> values; return *values.insert(values.cend(), 2) != 2; }
)r19containers");
  const auto Output = tmpFile("unordered-set-scalar-insertion-insert-with-hint.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-scalar-insertion-insert-with-hint" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetScalarInsertionCustomHashRejected) {
  const auto Source = tmpFile("unordered-set-scalar-insertion-custom-hash.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Hash { unsigned long operator()(int value) const { return static_cast<unsigned long>(value); } };
int main() { std::unordered_set<int, Hash> values; return !values.insert(2).second; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetIteratorErasePositionReturnsSuccessorRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-iterator-erase-position-returns-successor.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> values{2, 5, 13};
  const auto position = values.find(5);
  auto successor = position;
  ++successor;
  const auto returned = values.erase(position);
  return returned != successor || values.size() != 2 || values.count(5) || values.count(13) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-iterator-erase-position-returns-successor.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-iterator-erase-position-returns-successor" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetIteratorEraseHalfOpenRangeReturnsLastRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-iterator-erase-half-open-range-returns-last.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> values{0, 8, 1, 16};
  values.rehash(8);
  auto first = values.begin();
  auto last = first;
  const int removed = *last;
  ++last;
  const int kept = *last;
  auto next = values.erase(first, last);
  if (next != last || *next != kept || values.count(removed)) return 1;
  return values.count(kept) != 1 || values.size() != 3;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-iterator-erase-half-open-range-returns-last.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-iterator-erase-half-open-range-returns-last" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalPointerErasePositionRunsAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-iterator-erase-pointer-receiver.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_set<int> values{5}; auto* p=&values; return p->erase(values.cbegin()) != values.end(); }
)r19containers");
  const auto Output = tmpFile("unordered-set-iterator-erase-pointer-receiver.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-iterator-erase-pointer-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetIteratorEraseCustomHashRangeRejected) {
  const auto Source = tmpFile("unordered-set-iterator-erase-custom-hash-range.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Hash { unsigned long operator()(int k) const { return static_cast<unsigned long>(k); } };
int main() { std::unordered_set<int, Hash> values{1, 2}; return values.erase(values.cbegin(), values.cend()) != values.end(); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetEqualRangeUniqueRangeHasOneElementRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-equal-range-unique-range-has-one-element.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> values{2, 4, -7};
  auto range = values.equal_range(-7);
  if (range.first != values.find(-7) || range.first == range.second || *range.first != -7) return 1;
  auto successor = range.first;
  ++successor;
  return successor != range.second;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-equal-range-unique-range-has-one-element.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-equal-range-unique-range-has-one-element" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetEqualRangeDuplicateRangePreservesCollidingGroupsRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-equal-range-duplicate-range-preserves-colliding-groups.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> values{3, 11, 3, 19, 3, 11};
  values.rehash(8);
  const auto& read = values;
  auto range = read.equal_range(3);
  unsigned seen = 0;
  for (auto it = range.first; it != range.second; ++it) {
    if (*it != 3) return 1;
    ++seen;
  }
  return seen != 3 || values.count(11) != 2 || values.count(19) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-equal-range-duplicate-range-preserves-colliding-groups.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-equal-range-duplicate-range-preserves-colliding-groups" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalPointerEqualRangeRunsAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-equal-range-pointer-receiver.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_multiset<int> values{2, 2}; auto* p=&values; return p->equal_range(2).first == values.end(); }
)r19containers");
  const auto Output = tmpFile("unordered-set-equal-range-pointer-receiver.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-equal-range-pointer-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetEqualRangeCustomHashRejected) {
  const auto Source = tmpFile("unordered-set-equal-range-custom-hash.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Hash { unsigned long operator()(int k) const { return static_cast<unsigned long>(k); } };
int main() { std::unordered_set<int, Hash> values{2}; return values.equal_range(2).first == values.end(); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetLocalIteratorsBucketBoundaryKeepsCollisionsRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-local-iterators-bucket-boundary-keeps-collisions.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> values{0, 8, 1, 16, 17};
  values.rehash(8);
  const auto bucket = values.bucket(8);
  unsigned visited = 0;
  int sum = 0;
  for (auto it = values.begin(bucket); it != values.end(bucket); ++it) {
    sum += *it;
    ++visited;
  }
  return visited != 3 || sum != 24 || values.bucket_size(bucket) != 3;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-local-iterators-bucket-boundary-keeps-collisions.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-local-iterators-bucket-boundary-keeps-collisions" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetLocalIteratorsEmptyBucketAndCopiedLocalEndRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-local-iterators-empty-bucket-and-copied-local-end.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> values{1, 3};
  values.rehash(8);
  const auto& read = values;
  auto end = read.end(2);
  auto copy = end;
  return copy != read.begin(2) || end != read.cend(2);
}
)r19containers");
  const auto Output = tmpFile("unordered-set-local-iterators-empty-bucket-and-copied-local-end.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-local-iterators-empty-bucket-and-copied-local-end" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalPointerLocalIterationRunsAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-local-iterators-pointer-receiver.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_set<int> values{3}; auto* p = &values; return p->begin(0) != p->end(0); }
)r19containers");
  const auto Output = tmpFile("unordered-set-local-iterators-pointer-receiver.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-local-iterators-pointer-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetLocalIteratorsCustomHashRejected) {
  const auto Source = tmpFile("unordered-set-local-iterators-custom-hash.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Hash { unsigned long operator()(int k) const { return static_cast<unsigned long>(k); } };
int main() { std::unordered_multiset<int, Hash> values{3,3}; return values.begin(0) != values.end(0); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetEmplacementUniqueEmplacementReturnsResultRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-emplacement-unique-emplacement-returns-result.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> values;
  int key = -4;
  auto added = values.emplace(key);
  if (!added.second || *added.first != -4) return 1;
  const int equal = -4;
  auto existing = values.emplace(equal);
  if (existing.second || existing.first != added.first) return 2;
  auto zero = values.emplace();
  const auto* original = &*zero.first;
  auto again = values.emplace();
  return !zero.second || again.second || *again.first != 0 || &*again.first != original || values.size() != 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-emplacement-unique-emplacement-returns-result.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-emplacement-unique-emplacement-returns-result" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetEmplacementHintedValueInitializationAndUnrelatedHintRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-emplacement-hinted-value-initialization-and-unrelated-hint.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> set{6};
  auto zero = set.emplace_hint(set.cend());
  if (*zero != 0 || set.size() != 2) return 1;
  std::unordered_multiset<int> values{1, 2};
  const int value = 2;
  auto other = values.emplace_hint(values.find(1), value);
  if (*other != 2 || values.count(2) != 2) return 2;
  auto default_value = values.emplace();
  return *default_value != 0 || values.count(0) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-emplacement-hinted-value-initialization-and-unrelated-hint.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-emplacement-hinted-value-initialization-and-unrelated-hint" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalPointerEmplaceRunsAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-emplacement-pointer-receiver.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_set<int> values; auto* p=&values; return !p->emplace(3).second; }
)r19containers");
  const auto Output = tmpFile("unordered-set-emplacement-pointer-receiver.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-emplacement-pointer-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetEmplacementDifferentKeyTypeRejected) {
  const auto Source = tmpFile("unordered-set-emplacement-different-key-type.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_multiset<long> values; return *values.emplace(3L) != 3L; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetEqualitySwapUniqueEqualityIgnoresBucketsAndOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-equality-swap-unique-equality-ignores-buckets-and-order.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> a{1, -1, 0, 13}, b{13, 0, -1, 1};
  a.rehash(31);
  if (!(a == b) || a != b) return 1;
  b.erase(13);
  b.insert(14);
  return a == b || !(a != b);
}
)r19containers");
  const auto Output = tmpFile("unordered-set-equality-swap-unique-equality-ignores-buckets-and-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-equality-swap-unique-equality-ignores-buckets-and-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetEqualitySwapMemberSwapPreservesStorageAndElementAddressesRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-equality-swap-member-swap-preserves-storage-and-element-addresses.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> left{4, 5}, right;
  left.rehash(31);
  left.max_load_factor(.5f);
  const auto buckets = left.bucket_count();
  const int* saved = &*left.find(5);
  left.swap(right);
  if (!left.empty() || right.count(4) != 1 || &*right.find(5) != saved) return 1;
  if (right.bucket_count() != buckets || right.max_load_factor() != .5f) return 2;
  right.swap(right);
  return right.count(4) != 1 || &*right.find(5) != saved;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-equality-swap-member-swap-preserves-storage-and-element-addresses.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-equality-swap-member-swap-preserves-storage-and-element-addresses" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalPointerSwapRunsAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-equality-swap-pointer-member-swap.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { std::unordered_set<int> a{1}, b{2}; auto* p = &a; p->swap(b); return 0; }
)r19containers");
  const auto Output = tmpFile("unordered-set-equality-swap-pointer-member-swap.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-equality-swap-pointer-member-swap" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetEqualitySwapCustomPolicyEqualityRejected) {
  const auto Source = tmpFile("unordered-set-equality-swap-custom-policy-equality.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Equal { bool operator()(int a, int b) const { return a == b; } };
int main() { std::unordered_multiset<int, std::hash<int>, Equal> a{1, 1}, b{2}; return a != b; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetPointerRangeUniqueRangeInsertsDistinctKeysRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-pointer-range-unique-range-inserts-distinct-keys.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  int data[] = {3, 11, 3, -1, 19, 0, 11};
  std::unordered_set<int> set{11};
  set.rehash(2);
  set.insert(data, data + 7);
  return set.size() != 5 || set.count(3) != 1 || set.count(-1) != 1 || set.count(19) != 1 || set.count(0) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-pointer-range-unique-range-inserts-distinct-keys.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-pointer-range-unique-range-inserts-distinct-keys" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetPointerRangeEmptyRangeKeepsBucketsAndValuesRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-pointer-range-empty-range-keeps-buckets-and-values.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const int data[] = {123, 456, 789};
  std::unordered_set<int> set{5, 9};
  const auto buckets = set.bucket_count();
  set.insert(data + 1, data + 1);
  return set.size() != 2 || set.count(5) != 1 || set.bucket_count() != buckets;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-pointer-range-empty-range-keeps-buckets-and-values.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-pointer-range-empty-range-keeps-buckets-and-values" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalPointerRangeInsertionRunsAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-pointer-range-pointer-receiver.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() { const int data[] = {3, 3}; std::unordered_multiset<int> bag; auto* ptr=&bag; ptr->insert(data, data + 2); return bag.empty(); }
)r19containers");
  const auto Output = tmpFile("unordered-set-pointer-range-pointer-receiver.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-pointer-range-pointer-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetPointerRangeClassIteratorRangeRejected) {
  const auto Source = tmpFile("unordered-set-pointer-range-class-iterator-range.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
#include <list>
int main() { std::list<int> data{1, 1}; std::unordered_multiset<int> bag; bag.insert(data.begin(), data.end()); return bag.empty(); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetPointerConstructionUniqueRangeConstructionDeduplicatesRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-pointer-construction-unique-range-construction-deduplicates.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  int data[] = {4, 12, 4, -3, 20, 12};
  const std::unordered_set<int> set(data, data + 6);
  return set.size() != 4 || set.count(4) != 1 || set.count(-3) != 1 || set.count(20) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-pointer-construction-unique-range-construction-deduplicates.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-pointer-construction-unique-range-construction-deduplicates" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetPointerConstructionEmptyRangeUsesEmptyDefaultStateRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-pointer-construction-empty-range-uses-empty-default-state.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  int data[] = {1, 2};
  std::unordered_set<int> set(data + 1, data + 1);
  std::unordered_multiset<int> bag(data, data);
  return !set.empty() || !bag.empty() || set.bucket_count() != 0 || bag.bucket_count() != 0;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-pointer-construction-empty-range-uses-empty-default-state.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-pointer-construction-empty-range-uses-empty-default-state" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalBucketedPointerRangeConstructionRunsAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-pointer-construction-explicit-bucket-count-and-policies.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  int data[] = {1, 2};
  std::unordered_set<int> set(data, data + 2, 31, std::hash<int>{}, std::equal_to<int>{});
  return set.empty();
}
)r19containers");
  const auto Output = tmpFile("unordered-set-pointer-construction-explicit-bucket-count-and-policies.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-pointer-construction-explicit-bucket-count-and-policies" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetPointerConstructionCustomHashRangeRejected) {
  const auto Source = tmpFile("unordered-set-pointer-construction-custom-hash-range.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Hash { unsigned long operator()(int value) const { return static_cast<unsigned long>(value); } };
int main() { int data[] = {1, 2}; std::unordered_set<int, Hash> set(data, data + 2); return set.empty(); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetOwnerMoveUniqueConstructionTransfersNodesRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-owner-move-unique-construction-transfers-nodes.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
#include <utility>
int main() {
  std::unordered_set<int> source{3, -7, 22};
  source.rehash(19);
  source.max_load_factor(2.0f);
  const auto buckets = source.bucket_count();
  const int* key = &*source.find(-7);
  std::unordered_set<int> target(std::move(source));
  if (!source.empty() || source.bucket_count() != 0 || source.max_load_factor() != 2.0f)
    return 1;
  source.insert(44);
  return target.size() != 3 || target.bucket_count() != buckets || target.max_load_factor() != 2.0f ||
         &*target.find(-7) != key || source.count(44) != 1 || target.count(44) != 0;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-owner-move-unique-construction-transfers-nodes.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-owner-move-unique-construction-transfers-nodes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOwnerMoveMultisetAssignmentTransfersDuplicatesAndReturnsReceiverRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-owner-move-multiset-assignment-transfers-duplicates-and-returns-receiver.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
#include <utility>
using Bag = std::unordered_multiset<int>;
int trace;
Bag& left(Bag& item) { trace = trace * 10 + 2; return item; }
Bag&& right(Bag& item) { trace = trace * 10 + 1; return std::move(item); }
int main() {
  Bag source{3, 3, 9, 3};
  Bag target{71, 71};
  const int* key = &*source.find(9);
  Bag& result = (left(target) = right(source));
  return trace != 12 || &result != &target || target.size() != 4 || target.count(3) != 3 ||
         target.count(71) != 0 || &*target.find(9) != key || !source.empty();
}
)r19containers");
  const auto Output = tmpFile("unordered-set-owner-move-multiset-assignment-transfers-duplicates-and-returns-receiver.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-owner-move-multiset-assignment-transfers-duplicates-and-returns-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalAllocatorMoveRunsAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-owner-move-explicit-allocator-construction.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
#include <utility>
int main() {
  std::unordered_set<int> source{1};
  std::unordered_set<int> target(std::move(source), std::allocator<int>{});
  return target.empty();
}
)r19containers");
  const auto Output = tmpFile("unordered-set-owner-move-explicit-allocator-construction.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-owner-move-explicit-allocator-construction" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalUnsignedMoveConstruction) {
  const auto Source = tmpFile("unordered-set-owner-move-other-key-construction.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
#include <utility>
int main() {
  std::unordered_multiset<unsigned> source;
  std::unordered_multiset<unsigned> target(std::move(source));
  return target.empty();
}
)r19containers");
  const auto Output = tmpFile("unordered-set-owner-move-other-key-construction.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-owner-move-other-key-construction" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 1) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOwnerCopyUniqueConstSourceOwnsDistinctNodesRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-owner-copy-unique-const-source-owns-distinct-nodes.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> values{5, 11, -7};
  values.rehash(29);
  values.max_load_factor(2.5f);
  const auto& source = values;
  std::unordered_set<int> copy(source);
  return copy.size() != 3 || copy.count(-7) != 1 || copy.bucket_count() != source.bucket_count() ||
         copy.max_load_factor() != 2.5f || &*copy.find(-7) == &*source.find(-7);
}
)r19containers");
  const auto Output = tmpFile("unordered-set-owner-copy-unique-const-source-owns-distinct-nodes.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-owner-copy-unique-const-source-owns-distinct-nodes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOwnerCopyReservedEmptyCopyKeepsBucketPolicyRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-owner-copy-reserved-empty-copy-keeps-bucket-policy.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> source;
  source.rehash(31);
  source.max_load_factor(3.5f);
  std::unordered_multiset<int> copy(source);
  copy.insert({-1, -1});
  return !source.empty() || copy.count(-1) != 2 || copy.bucket_count() != source.bucket_count() ||
         copy.max_load_factor() != 3.5f;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-owner-copy-reserved-empty-copy-keeps-bucket-policy.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-owner-copy-reserved-empty-copy-keeps-bucket-policy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalAllocatorCopyRunsAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-owner-copy-explicit-allocator-copy.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> source{1};
  std::unordered_set<int> copy(source, std::allocator<int>{});
  return copy.empty();
}
)r19containers");
  const auto Output = tmpFile("unordered-set-owner-copy-explicit-allocator-copy.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-owner-copy-explicit-allocator-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOwnerCopyOtherKeyCopyRejected) {
  const auto Source = tmpFile("unordered-set-owner-copy-other-key-copy.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<long> source;
  std::unordered_multiset<long> copy(source);
  return copy.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetCopyAssignmentUniqueCopyAssignmentReusesDestinationBucketsRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-copy-assignment-unique-copy-assignment-reuses-destination-buckets.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> target{-5, -7, -11, -13};
  target.rehash(53);
  const auto buckets = target.bucket_count();
  std::unordered_set<int> source{100, 18, 6};
  source.max_load_factor(2.0f);
  auto& result = (target = source);
  return &result != &target || target.size() != 3 || target.count(-5) != 0 || target.count(18) != 1 ||
         target.bucket_count() != buckets || target.max_load_factor() != source.max_load_factor() ||
         &*target.find(18) == &*source.find(18);
}
)r19containers");
  const auto Output = tmpFile("unordered-set-copy-assignment-unique-copy-assignment-reuses-destination-buckets.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-copy-assignment-unique-copy-assignment-reuses-destination-buckets" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetCopyAssignmentExplicitCopyAssignmentEvaluatesReceiverBeforeSourceRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-copy-assignment-explicit-copy-assignment-evaluates-receiver-before-source.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
using Set = std::unordered_set<int>;
int trace;
Set* receiver(Set& set) { trace = trace * 10 + 2; return &set; }
const Set& input(const Set& set) { trace = trace * 10 + 1; return set; }
int main() {
  Set source{19, 29};
  Set target{23};
  auto& result = receiver(target)->operator=(input(source));
  return trace != 21 || &result != &target || target.count(19) != 1 || target.count(23) != 0;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-copy-assignment-explicit-copy-assignment-evaluates-receiver-before-source.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-copy-assignment-explicit-copy-assignment-evaluates-receiver-before-source" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalUnsignedCopyAssignment) {
  const auto Source = tmpFile("unordered-set-copy-assignment-unsigned-key-copy-assignment.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<unsigned> source, target;
  target = source;
  return target.empty();
}
)r19containers");
  const auto Output = tmpFile("unordered-set-copy-assignment-unsigned-key-copy-assignment.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-copy-assignment-unsigned-key-copy-assignment" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 1) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetCopyAssignmentCustomEqualityMultisetCopyAssignmentRejected) {
  const auto Source = tmpFile("unordered-set-copy-assignment-custom-equality-multiset-copy-assignment.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Equal { bool operator()(int left, int right) const { return left == right; } };
int main() {
  std::unordered_multiset<int, std::hash<int>, Equal> source, target;
  target = source;
  return target.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetListAssignmentUniqueListAssignmentKeepsBucketsAndLoadFactorRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-list-assignment-unique-list-assignment-keeps-buckets-and-load-factor.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> values{-2, -4, -6, -8};
  values.rehash(31);
  values.max_load_factor(2.0f);
  const auto buckets = values.bucket_count();
  auto& result = (values = {3, 34, 3, 7, 34, 5});
  return &result != &values || values.size() != 4 || values.count(-2) != 0 ||
         values.count(3) != 1 || values.count(34) != 1 || values.count(5) != 1 ||
         values.bucket_count() != buckets || values.max_load_factor() != 2.0f;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-list-assignment-unique-list-assignment-keeps-buckets-and-load-factor.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-list-assignment-unique-list-assignment-keeps-buckets-and-load-factor" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetListAssignmentExplicitEmptyListAssignmentClearsReservedStorageRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-list-assignment-explicit-empty-list-assignment-clears-reserved-storage.cpp");
  writeFile(Source, R"r19containers(#include <initializer_list>
#include <unordered_set>
int main() {
  std::unordered_set<int> values{11, 23};
  values.rehash(31);
  auto* receiver = &values;
  auto& result = receiver->operator=(std::initializer_list<int>{});
  return &result != &values || !values.empty() || values.bucket_count() != 31;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-list-assignment-explicit-empty-list-assignment-clears-reserved-storage.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-list-assignment-explicit-empty-list-assignment-clears-reserved-storage" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetListAssignmentCustomHashListAssignmentRejected) {
  const auto Source = tmpFile("unordered-set-list-assignment-custom-hash-list-assignment.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Hash { unsigned long operator()(int value) const { return static_cast<unsigned>(value); } };
int main() {
  std::unordered_set<int, Hash> values;
  values = {2, 2};
  return values.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetListAssignmentLongKeyListAssignmentRejected) {
  const auto Source = tmpFile("unordered-set-list-assignment-long-key-list-assignment.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<long> values;
  values = {4L, 4L};
  return values.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetIteratorRangeUniqueInsertsConstMultisetIteratorRangeRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-iterator-range-unique-inserts-const-multiset-iterator-range.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::unordered_multiset<int> source{13, 3, 13, 5, 13, 5, 7};
  std::unordered_set<int> target{19, 5};
  target.insert(source.cbegin(), source.cend());
  return target.size() != 5 || target.count(13) != 1 || target.count(3) != 1 ||
         target.count(7) != 1 || target.count(19) != 1 || source.count(13) != 3;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-iterator-range-unique-inserts-const-multiset-iterator-range.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-iterator-range-unique-inserts-const-multiset-iterator-range" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetIteratorRangeMultiInsertsOneLocalBucketWithDuplicatesRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-iterator-range-multi-inserts-one-local-bucket-with-duplicates.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> source;
  source.rehash(2);
  source.max_load_factor(4.0f);
  source.insert({3, 3, 7, 8});
  const auto bucket = source.bucket(3);
  std::unordered_multiset<int> target{3};
  target.insert(source.cbegin(bucket), source.cend(bucket));
  return target.size() != 4 || target.count(3) != 3 || target.count(7) != 1 || target.count(8) != 0;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-iterator-range-multi-inserts-one-local-bucket-with-duplicates.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-iterator-range-multi-inserts-one-local-bucket-with-duplicates" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetIteratorRangeMultiConstructsFromLocalIteratorsRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-iterator-range-multi-constructs-from-local-iterators.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> source;
  source.rehash(2);
  source.max_load_factor(4.0f);
  source.insert({23, 23, 27, 24});
  const auto bucket = source.bucket(23);
  std::unordered_multiset<int> target(source.begin(bucket), source.end(bucket));
  return target.size() != 3 || target.count(23) != 2 || target.count(27) != 1 || target.count(24) != 0;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-iterator-range-multi-constructs-from-local-iterators.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-iterator-range-multi-constructs-from-local-iterators" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetIteratorRangeUnrelatedVectorIteratorInsertionRejected) {
  const auto Source = tmpFile("unordered-set-iterator-range-unrelated-vector-iterator-insertion.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
#include <vector>
int main() {
  const std::vector<int> source{3, 5};
  std::unordered_set<int> target;
  target.insert(source.begin(), source.end());
  return target.count(3) != 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetIteratorRangeUnsignedKeyIteratorInsertionRejected) {
  const auto Source = tmpFile("unordered-set-iterator-range-unsigned-key-iterator-insertion.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::unordered_set<unsigned> source{5u, 7u};
  std::unordered_set<int> target;
  target.insert(source.begin(), source.end());
  return target.count(5) != 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetMergeUniqueMergePreservesNodeAndRejectedDuplicateAddressesRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-merge-unique-merge-preserves-node-and-rejected-duplicate-addresses.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> target{5, 7};
  std::unordered_set<int> source{5, 13, 19};
  const int* moved = &*source.find(13);
  const int* duplicate = &*source.find(5);
  target.merge(source);
  return target.size() != 4 || source.size() != 1 || &*target.find(13) != moved ||
         &*source.find(5) != duplicate || target.count(19) != 1 || source.count(19) != 0;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-merge-unique-merge-preserves-node-and-rejected-duplicate-addresses.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-merge-unique-merge-preserves-node-and-rejected-duplicate-addresses" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetMergeMultiMergesRvalueUniqueOwnerWithoutReallocatingNodesRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-merge-multi-merges-rvalue-unique-owner-without-reallocating-nodes.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
#include <utility>
int main() {
  std::unordered_multiset<int> target{9, 9};
  std::unordered_set<int> source{9, 23, -1};
  const int* moved = &*source.find(23);
  auto* receiver = &target;
  receiver->merge(std::move(source));
  return !source.empty() || target.size() != 5 || target.count(9) != 3 ||
         target.count(-1) != 1 || &*target.find(23) != moved;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-merge-multi-merges-rvalue-unique-owner-without-reallocating-nodes.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-merge-multi-merges-rvalue-unique-owner-without-reallocating-nodes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetMergeCustomHashMergeSourceRejected) {
  const auto Source = tmpFile("unordered-set-merge-custom-hash-merge-source.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Hash { unsigned long operator()(int key) const { return static_cast<unsigned>(key); } };
int main() {
  std::unordered_set<int, Hash> source;
  std::unordered_set<int> target;
  target.merge(source);
  return target.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetMergeCustomEqualityMergeDestinationRejected) {
  const auto Source = tmpFile("unordered-set-merge-custom-equality-merge-destination.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Equal { bool operator()(int left, int right) const { return left == right; } };
int main() {
  std::unordered_multiset<int> source;
  std::unordered_set<int, std::hash<int>, Equal> target;
  target.merge(source);
  return target.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetObserversSetAllocatorAllocatesAndReleasesIntegerStorageRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-observers-set-allocator-allocates-and-releases-integer-storage.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::unordered_set<int> values{9, 17};
  auto allocator = values.get_allocator();
  auto* storage = allocator.allocate(1);
  const bool allocated = storage != nullptr;
  allocator.deallocate(storage, 1);
  return !allocated || values.count(9) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-observers-set-allocator-allocates-and-releases-integer-storage.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-observers-set-allocator-allocates-and-releases-integer-storage" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetObserversHashFunctionOnArrowReceiverUsesPinnedSignedHashRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-observers-hash-function-on-arrow-receiver-uses-pinned-signed-hash.cpp");
  writeFile(Source, R"r19containers(#include <cstddef>
#include <unordered_set>
int main() {
  const std::unordered_set<int> values{-9, 7};
  const auto* receiver = &values;
  const auto hash = receiver->hash_function();
  return hash(-9) != static_cast<std::size_t>(-9) || hash(7) != 7u || values.count(-9) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-observers-hash-function-on-arrow-receiver-uses-pinned-signed-hash.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-observers-hash-function-on-arrow-receiver-uses-pinned-signed-hash" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetObserversCustomHashArrowObserverRejected) {
  const auto Source = tmpFile("unordered-set-observers-custom-hash-arrow-observer.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Hash { unsigned long operator()(int key) const { return static_cast<unsigned>(key); } };
int main() {
  const std::unordered_set<int, Hash> values;
  const auto* receiver = &values;
  return receiver->hash_function()(7) != 7u;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetObserversDifferentKeyAllocatorObserverRejected) {
  const auto Source = tmpFile("unordered-set-observers-different-key-allocator-observer.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::unordered_set<long> values;
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

TEST_F(TranslateTest, CoreV2UnorderedSetStorageConstructionSetBucketCountDefaultPoliciesEvaluateCountOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-storage-construction-set-bucket-count-default-policies-evaluate-count-once.cpp");
  writeFile(Source, R"r19containers(#include <cstddef>
#include <unordered_set>
std::size_t buckets(int& calls) { ++calls; return 17; }
int main() {
  int calls = 0;
  std::unordered_set<int> values(buckets(calls));
  return calls != 1 || !values.empty() || values.bucket_count() < 17 || values.max_load_factor() != 1.0f;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-storage-construction-set-bucket-count-default-policies-evaluate-count-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-storage-construction-set-bucket-count-default-policies-evaluate-count-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetStorageConstructionSetBucketDelegationWithDefaultAllocatorEvaluatesArgumentOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-storage-construction-set-bucket-delegation-with-default-allocator-evaluates-argument-once.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
using Alloc = std::allocator<int>;
const Alloc& observe(const Alloc& allocator, int& calls) { ++calls; return allocator; }
int main() {
  const Alloc allocator;
  int calls = 0;
  std::unordered_set<int> values(11u, observe(allocator, calls));
  return calls != 1 || !values.empty() || values.bucket_count() < 11;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-storage-construction-set-bucket-delegation-with-default-allocator-evaluates-argument-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-storage-construction-set-bucket-delegation-with-default-allocator-evaluates-argument-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetStorageConstructionSetCopyWithDefaultAllocatorOwnsADistinctNodeRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-storage-construction-set-copy-with-default-allocator-owns-a-distinct-node.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::allocator<int> allocator;
  const std::unordered_set<int> source{17, 23};
  std::unordered_set<int> copy(source, allocator);
  return copy.size() != 2 || &*copy.find(17) == &*source.find(17) || copy.count(23) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-storage-construction-set-copy-with-default-allocator-owns-a-distinct-node.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-storage-construction-set-copy-with-default-allocator-owns-a-distinct-node" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalUnsignedBucketConstruction) {
  const auto Source = tmpFile("unordered-set-storage-construction-unapproved-key-bucket-construction.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<unsigned> values(7u, std::hash<unsigned>{}, std::equal_to<unsigned>{});
  return values.empty();
}
)r19containers");
  const auto Output = tmpFile("unordered-set-storage-construction-unapproved-key-bucket-construction.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-storage-construction-unapproved-key-bucket-construction" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 1) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetStorageConstructionMultisetCustomEqualityBucketConstructionRejected) {
  const auto Source = tmpFile("unordered-set-storage-construction-multiset-custom-equality-bucket-construction.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Equal { bool operator()(int first, int second) const { return first == second; } };
int main() {
  std::unordered_multiset<int, std::hash<int>, Equal> values(11u);
  return values.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetNodeHandlesUniqueExtractsKeyAndMutatesDetachedValueRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-node-handles-unique-extracts-key-and-mutates-detached-value.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_set<int> values{3, 11};
  auto node = values.extract(11);
  if (!node || node.empty() || values.count(11)) return 1;
  node.value() = -5;
  const auto& observed = node;
  return observed.value() != -5 || values.count(3) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-node-handles-unique-extracts-key-and-mutates-detached-value.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-node-handles-unique-extracts-key-and-mutates-detached-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetNodeHandlesDefaultAndMissingKeyHandlesStayEmptyRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-node-handles-default-and-missing-key-handles-stay-empty.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  using Set = std::unordered_set<int>;
  Set values{7};
  Set::node_type initial;
  const auto absent = values.extract(-9);
  return !initial.empty() || bool(initial) || !absent.empty() || bool(absent) || values.count(7) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-node-handles-default-and-missing-key-handles-stay-empty.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-node-handles-default-and-missing-key-handles-stay-empty" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetNodeHandlesSwappedNodeRetainsAllocatorAndValueRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-node-handles-swapped-node-retains-allocator-and-value.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  using Set = std::unordered_multiset<int>;
  Set values{21};
  auto extracted = values.extract(21);
  Set::node_type other;
  extracted.swap(other);
  if (!extracted.empty() || !other || other.value() != 21) return 1;
  auto allocator = other.get_allocator();
  auto* storage = allocator.allocate(1);
  const bool allocated = storage != nullptr;
  allocator.deallocate(storage, 1);
  return !allocated;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-node-handles-swapped-node-retains-allocator-and-value.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-node-handles-swapped-node-retains-allocator-and-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetNodeHandlesUniqueHintedDuplicateRetainsTheSuppliedHandleRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-node-handles-unique-hinted-duplicate-retains-the-supplied-handle.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
#include <utility>
int main() {
  std::unordered_set<int> source{5};
  std::unordered_set<int> target{5};
  auto node = source.extract(5);
  auto where = target.insert(target.end(), std::move(node));
  return !node || node.value() != 5 || where == target.end() || *where != 5 || target.size() != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-node-handles-unique-hinted-duplicate-retains-the-supplied-handle.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-node-handles-unique-hinted-duplicate-retains-the-supplied-handle" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetNodeHandlesMultisetHintInsertsTheTransferredNodeWithItsEquivalentsRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-node-handles-multiset-hint-inserts-the-transferred-node-with-its-equivalents.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
#include <utility>
int main() {
  std::unordered_multiset<int> source{5, 5};
  std::unordered_multiset<int> target{5, 5, 13};
  target.rehash(16);
  auto it = source.find(5);
  const int* address = &*it;
  auto node = source.extract(it);
  auto added = target.insert(target.find(5), std::move(node));
  return node || &*added != address || target.count(5) != 3 || target.count(13) != 1 || source.count(5) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-node-handles-multiset-hint-inserts-the-transferred-node-with-its-equivalents.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-node-handles-multiset-hint-inserts-the-transferred-node-with-its-equivalents" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetOriginalUnsignedEmptyNodeHandle) {
  const auto Source = tmpFile("unordered-set-node-handles-other-key-default-node-handle.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<unsigned>::node_type empty;
  return empty.empty();
}
)r19containers");
  const auto Output = tmpFile("unordered-set-node-handles-other-key-default-node-handle.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-node-handles-other-key-default-node-handle" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 1) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetNodeHandlesCustomPolicyMultisetNodeExtractionRejected) {
  const auto Source = tmpFile("unordered-set-node-handles-custom-policy-multiset-node-extraction.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Equal { bool operator()(int left, int right) const { return left == right; } };
int main() {
  std::unordered_multiset<int, std::hash<int>, Equal> values{5, 5};
  auto node = values.extract(values.find(5));
  return node.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetNodeHandlesCustomEqualityMultisetHandleInsertRejected) {
  const auto Source = tmpFile("unordered-set-node-handles-custom-equality-multiset-handle-insert.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
#include <utility>
struct Equal { bool operator()(int left, int right) const { return left == right; } };
int main() {
  std::unordered_set<int> source{7};
  std::unordered_multiset<int, std::hash<int>, Equal> target;
  auto node = source.extract(7);
  return target.insert(std::move(node)) == target.end();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetBucketReceiversPointerCapacityAndLoadObserversUseTheLiveTableRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-bucket-receivers-pointer-capacity-and-load-observers-use-the-live-table.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::unordered_multiset<int> values{3, 3, 7};
  const auto* p = &values;
  return p->empty() || p->size() != 3 || p->bucket_count() != values.bucket_count() ||
         p->load_factor() != values.load_factor() || p->max_load_factor() != 1.0f;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-bucket-receivers-pointer-capacity-and-load-observers-use-the-live-table.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-bucket-receivers-pointer-capacity-and-load-observers-use-the-live-table" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetBucketReceiversPointerBucketAndSizeFindAnEquivalentKeyGroupRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-bucket-receivers-pointer-bucket-and-size-find-an-equivalent-key-group.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> values{1, 1, 2};
  values.rehash(8);
  const auto* p = &values;
  const auto bucket = p->bucket(1);
  return bucket >= p->bucket_count() || p->bucket_size(bucket) != 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-bucket-receivers-pointer-bucket-and-size-find-an-equivalent-key-group.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-bucket-receivers-pointer-bucket-and-size-find-an-equivalent-key-group" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetBucketReceiversOtherKeyPointerCapacityObserverRejected) {
  const auto Source = tmpFile("unordered-set-bucket-receivers-other-key-pointer-capacity-observer.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const std::unordered_set<long> values;
  const auto* p = &values;
  return p->max_bucket_count() == 0;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetBucketReceiversCustomEqualityPointerLoadLimitRejected) {
  const auto Source = tmpFile("unordered-set-bucket-receivers-custom-equality-pointer-load-limit.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Equal { bool operator()(int x, int y) const { return x == y; } };
int main() {
  std::unordered_set<int, std::hash<int>, Equal> values;
  auto* p = &values;
  p->max_load_factor(2.0f);
  return p->size();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetMemberReceiversIteratorRangeWithPinnedPoliciesRetainsEquivalentKeysRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-member-receivers-iterator-range-with-pinned-policies-retains-equivalent-keys.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> source{5, 5, 6};
  std::unordered_multiset<int> values(source.cbegin(), source.cend(), 32, std::hash<int>{},
      std::equal_to<int>{}, std::allocator<int>{});
  source.clear();
  return values.bucket_count() < 32 || values.size() != 3 || values.count(5) != 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-member-receivers-iterator-range-with-pinned-policies-retains-equivalent-keys.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-member-receivers-iterator-range-with-pinned-policies-retains-equivalent-keys" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetMemberReceiversListWithAllPoliciesPreservesDuplicateKeyGroupRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-member-receivers-list-with-all-policies-preserves-duplicate-key-group.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> values({7, 3, 7}, 11, std::hash<int>{},
      std::equal_to<int>{}, std::allocator<int>{});
  return values.bucket_count() < 11 || values.size() != 3 || values.count(7) != 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-member-receivers-list-with-all-policies-preserves-duplicate-key-group.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-member-receivers-list-with-all-policies-preserves-duplicate-key-group" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetMemberReceiversMemberPointerExpressionsEvaluateOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-member-receivers-member-pointer-expressions-evaluate-once.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
using Set = std::unordered_set<int>;
Set* table(Set& values, unsigned& calls) { ++calls; return &values; }
int main() {
  Set values{3};
  unsigned calls = 0;
  if (!table(values, calls)->emplace(4).second) return 1;
  auto found = table(values, calls)->find(3);
  if (found == values.end() || *found != 3) return 2;
  table(values, calls)->erase(found);
  return calls != 3 || values.size() != 1 || values.count(4) != 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-member-receivers-member-pointer-expressions-evaluate-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-member-receivers-member-pointer-expressions-evaluate-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetMemberReceiversPointerRangeInsertionAndClearPreserveKeyMultiplicityRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-member-receivers-pointer-range-insertion-and-clear-preserve-key-multiplicity.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  std::unordered_multiset<int> source{4, 4, 8}, target{4};
  auto* receiver = &target;
  receiver->insert(source.cbegin(), source.cend());
  if (receiver->count(4) != 3 || receiver->erase(8) != 1) return 1;
  receiver->clear();
  return !target.empty() || source.count(4) != 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-member-receivers-pointer-range-insertion-and-clear-preserve-key-multiplicity.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-member-receivers-pointer-range-insertion-and-clear-preserve-key-multiplicity" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetMemberReceiversRangeWithUserHashRejected) {
  const auto Source = tmpFile("unordered-set-member-receivers-range-with-user-hash.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Hash { unsigned operator()(int n) const { return static_cast<unsigned>(n); } };
int main() {
  int input = 3;
  std::unordered_set<int, Hash> values(&input, &input + 1, 16, Hash{});
  return values.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetMemberReceiversUserHashPointerMemberRejected) {
  const auto Source = tmpFile("unordered-set-member-receivers-user-hash-pointer-member.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Hash { unsigned operator()(int key) const { return static_cast<unsigned>(key); } };
int main() {
  std::unordered_set<int, Hash> values;
  auto* receiver = &values;
  return receiver->find(1) != receiver->end();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedMultimapOwnersDefaultConstructionAndCapacityRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multimap-owners-default-construction-and-capacity.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
int main() {
  std::unordered_multimap<int, int> values;
  auto* receiver = &values;
  receiver->reserve(11);
  return !receiver->empty() || values.size() != 0 || values.bucket_count() < 11 || values.count(-1) != 0;
}
)r19containers");
  const auto Output = tmpFile("unordered-multimap-owners-default-construction-and-capacity.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multimap-owners-default-construction-and-capacity" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultimapOwnersCopyOwnsDistinctNodesAfterSourceClearRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multimap-owners-copy-owns-distinct-nodes-after-source-clear.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
int main() {
  using Map = std::unordered_multimap<int, int>;
  Map source{{-4, 13}, {-4, 14}, {6, 7}};
  source.max_load_factor(2.0f);
  source.rehash(17);
  Map copy(source);
  source.clear();
  return !source.empty() || copy.count(-4) != 2 || copy.count(6) != 1 ||
         copy.bucket_count() < 17 || copy.max_load_factor() != 2.0f;
}
)r19containers");
  const auto Output = tmpFile("unordered-multimap-owners-copy-owns-distinct-nodes-after-source-clear.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multimap-owners-copy-owns-distinct-nodes-after-source-clear" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultimapOwnersComparisonRequiresMappedValueMultiplicitiesRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multimap-owners-comparison-requires-mapped-value-multiplicities.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
int main() {
  const std::unordered_multimap<int, int> left{{1, 3}, {1, 5}, {1, 5}, {7, 9}};
  const std::unordered_multimap<int, int> permuted{{7, 9}, {1, 5}, {1, 3}, {1, 5}};
  const std::unordered_multimap<int, int> different{{1, 3}, {1, 3}, {1, 5}, {7, 9}};
  return left != permuted || left == different;
}
)r19containers");
  const auto Output = tmpFile("unordered-multimap-owners-comparison-requires-mapped-value-multiplicities.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multimap-owners-comparison-requires-mapped-value-multiplicities" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultimapOwnersCustomHashOwnerRejected) {
  const auto Source = tmpFile("unordered-multimap-owners-custom-hash-owner.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
struct Hash { unsigned long operator()(int key) const { return static_cast<unsigned>(key); } };
int main() {
  std::unordered_multimap<int, int, Hash> pairs;
  return pairs.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedMultimapIterationAndTransferIteratorConstEqualRangeReachesTheEndOfTheGroupRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multimap-iteration-and-transfer-iterator-const-equal-range-reaches-the-end-of-the-group.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
int main() {
  const std::unordered_multimap<int, int> values{{-1, 2}, {3, 9}, {-1, 5}, {-1, 8}};
  auto group = values.equal_range(-1);
  int count = 0, sum = 0;
  for (auto it = group.first; it != group.second; ++it) {
    if (it->first != -1) return 1;
    ++count;
    sum += it->second;
  }
  auto missing = values.equal_range(77);
  return count != 3 || sum != 15 || missing.first != values.cend() || missing.second != values.cend();
}
)r19containers");
  const auto Output = tmpFile("unordered-multimap-iteration-and-transfer-iterator-const-equal-range-reaches-the-end-of-the-group.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multimap-iteration-and-transfer-iterator-const-equal-range-reaches-the-end-of-the-group" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultimapIterationAndTransferIteratorLocalConstIterationSeesTheMatchingBucketRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multimap-iteration-and-transfer-iterator-local-const-iteration-sees-the-matching-bucket.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
int main() {
  std::unordered_multimap<int, int> values{{3, 5}, {3, 12}, {8, 7}};
  values.rehash(16);
  const auto index = values.bucket(3);
  const auto& view = values;
  int count = 0, sum = 0;
  for (auto it = view.cbegin(index); it != view.cend(index); it++) {
    if (it->first != 3) return 1;
    ++count;
    sum += it->second;
  }
  return count != 2 || view.bucket_size(index) != 2 || sum != 17;
}
)r19containers");
  const auto Output = tmpFile("unordered-multimap-iteration-and-transfer-iterator-local-const-iteration-sees-the-matching-bucket.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multimap-iteration-and-transfer-iterator-local-const-iteration-sees-the-matching-bucket" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultimapIterationAndTransferModifierHintedInsertionKeepsEquivalentKeysContiguousRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multimap-iteration-and-transfer-modifier-hinted-insertion-keeps-equivalent-keys-contiguous.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
int main() {
  std::unordered_multimap<int, int> values{{5, 2}, {5, 3}, {3, 11}};
  auto next = values.emplace_hint(values.find(5), 5, 7);
  const auto group = values.equal_range(5);
  int sum = 0, count = 0;
  for (auto it = group.first; it != group.second; ++it) { sum += it->second; ++count; }
  return next->first != 5 || next->second != 7 || sum != 12 || count != 3;
}
)r19containers");
  const auto Output = tmpFile("unordered-multimap-iteration-and-transfer-modifier-hinted-insertion-keeps-equivalent-keys-contiguous.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multimap-iteration-and-transfer-modifier-hinted-insertion-keeps-equivalent-keys-contiguous" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultimapIterationAndTransferModifierMergeIntoMultiPreservesPayloadAddressesRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multimap-iteration-and-transfer-modifier-merge-into-multi-preserves-payload-addresses.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
int main() {
  std::unordered_map<int, int> source{{5, 13}, {6, 21}};
  std::unordered_multimap<int, int> target{{5, 3}, {5, 8}};
  const int* payload = &source.find(6)->second;
  target.merge(source);
  return !source.empty() || target.count(5) != 3 || target.size() != 4 ||
         &target.find(6)->second != payload;
}
)r19containers");
  const auto Output = tmpFile("unordered-multimap-iteration-and-transfer-modifier-merge-into-multi-preserves-payload-addresses.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multimap-iteration-and-transfer-modifier-merge-into-multi-preserves-payload-addresses" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultimapIterationAndTransferIteratorOtherMappedTypeIteratorRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multimap-iteration-and-transfer-iterator-other-mapped-type-iterator.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
int main() {
  const std::unordered_multimap<int, long> values{{3, 5L}};
  return values.begin()->second == 5L;
}
)r19containers");
  const auto Output = tmpFile("unordered-multimap-iteration-and-transfer-iterator-other-mapped-type-iterator.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multimap-iteration-and-transfer-iterator-other-mapped-type-iterator" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 1) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMultimapIterationAndTransferModifierMergeFromUserHashPolicyRejected) {
  const auto Source = tmpFile("unordered-multimap-iteration-and-transfer-modifier-merge-from-user-hash-policy.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
struct Hash { unsigned operator()(int key) const { return static_cast<unsigned>(key); } };
int main() {
  std::unordered_map<int, int, Hash> source;
  std::unordered_multimap<int, int> target;
  target.merge(source);
  return target.size();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedMultimapNodeHandlesRejectedUniqueInsertionReturnsHandleForMultimapRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-multimap-node-handles-rejected-unique-insertion-returns-handle-for-multimap.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
#include <utility>
int main() {
  std::unordered_multimap<int, int> source{{3, 17}, {3, 22}};
  std::unordered_map<int, int> unique{{3, 5}};
  auto node = source.extract(3);
  const auto payload = node.mapped();
  auto rejected = unique.insert(std::move(node));
  if (rejected.inserted || rejected.position->second != 5 ||
      rejected.node.empty() || rejected.node.key() != 3 || rejected.node.mapped() != payload) return 1;
  auto inserted = source.insert(source.find(3), std::move(rejected.node));
  return !rejected.node.empty() || inserted->second != payload || source.count(3) != 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-multimap-node-handles-rejected-unique-insertion-returns-handle-for-multimap.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-multimap-node-handles-rejected-unique-insertion-returns-handle-for-multimap" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedPointerObserversMapSizeEmptyAndMaximumRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-pointer-observers-map-size-empty-and-maximum.cpp");
  writeFile(Source, R"r19containers(#include <map>
using Table = std::map<int, int>;
const Table* observe(const Table& values, unsigned& calls) { ++calls; return &values; }
int main() {
  const Table values;
  unsigned calls = 0;
  const auto maximum = observe(values, calls)->max_size();
  const auto* pointer = &values;
  return calls != 1 || maximum != values.max_size() || !pointer->empty() || pointer->size() != 0;
}
)r19containers");
  const auto Output = tmpFile("ordered-pointer-observers-map-size-empty-and-maximum.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-pointer-observers-map-size-empty-and-maximum" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedPointerObserversMultisetPointerValueComparatorRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-pointer-observers-multiset-pointer-value-comparator.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  const std::multiset<int> values;
  const auto* pointer = &values;
  return !pointer->value_comp()(3, 7) || pointer->key_comp()(3, 3) || !pointer->empty();
}
)r19containers");
  const auto Output = tmpFile("ordered-pointer-observers-multiset-pointer-value-comparator.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-pointer-observers-multiset-pointer-value-comparator" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedPointerObserversCustomOrderedComparePointerRejected) {
  const auto Source = tmpFile("ordered-pointer-observers-custom-ordered-compare-pointer.cpp");
  writeFile(Source, R"r19containers(#include <map>
struct Greater { bool operator()(int a, int b) const { return a > b; } };
int main() {
  const std::map<int, int, Greater> values;
  return (&values)->key_comp()(1, 2);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedSetUnsignedKeyBucketedRangesAndCopyUseUnsignedKeyRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-unsigned-key-bucketed-ranges-and-copy-use-unsigned-key.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  constexpr unsigned high = ~0u;
  const unsigned input[] = {high, 1u, high};
  std::unordered_multiset<unsigned> source(input, input + 3, 7);
  std::unordered_multiset<unsigned> target;
  target = source;
  auto range = target.equal_range(high);
  if (range.first == target.end() || *range.first != high || ++range.first == range.second) return 1;
  const auto original = target.size();
  if (target.erase(high) != 2 || target.size() + 2 != original || source.count(high) != 2) return 1;
  target = {2u, high};
  return target.count(high) != 1 || *target.emplace(0u) != 0u;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-unsigned-key-bucketed-ranges-and-copy-use-unsigned-key.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-unsigned-key-bucketed-ranges-and-copy-use-unsigned-key" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetUnsignedKeyAlternateUnsignedHashRemainsUnsupportedRejected) {
  const auto Source = tmpFile("unordered-set-unsigned-key-alternate-unsigned-hash-remains-unsupported.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
struct Hash { unsigned long operator()(unsigned n) const { return n & 1u; } };
int main() {
  std::unordered_set<unsigned, Hash> values{4u};
  return values.count(4u) != 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedSetListAndLookupPointerCountAfterLeftAndRightRotationsRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-list-and-lookup-pointer-count-after-left-and-right-rotations.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <functional>
int main() {
  const std::set<int> values({8, 14, 13, 4, 5, 3, 20, 19}, std::less<int>{});
  const auto* receiver = &values;
  return receiver->count(5) != 1 || receiver->count(20) != 1 ||
         receiver->count(8) != 1 || receiver->count(7) != 0 || receiver->size() != 8;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-list-and-lookup-pointer-count-after-left-and-right-rotations.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-list-and-lookup-pointer-count-after-left-and-right-rotations" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetInsertResultInsertionResultDistinguishesADuplicateKeyRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-insert-result-insertion-result-distinguishes-a-duplicate-key.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  std::set<int> values;
  int key = -9;
  auto added = values.insert(key);
  auto duplicate = values.insert(key);
  auto other = values.insert(3);
  return !added.second || duplicate.second || !other.second || values.size() != 2 ||
         values.count(-9) != 1 || values.count(0) != 0;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-insert-result-insertion-result-distinguishes-a-duplicate-key.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-insert-result-insertion-result-distinguishes-a-duplicate-key" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetIteratorOrderedRangeAndInsertionPreserveExistingPositionRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-iterator-ordered-range-and-insertion-preserve-existing-position.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  std::set<int> values{9, -6, 3, 7, 3};
  auto state = values.insert(-2);
  if (!state.second || *state.first != -2) return 1;
  int previous = -8, count = 0;
  for (const int value : values) {
    if (value <= previous) return 2;
    previous = value;
    ++count;
  }
  return previous != 9 || count != 5;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-iterator-ordered-range-and-insertion-preserve-existing-position.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-iterator-ordered-range-and-insertion-preserve-existing-position" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetIteratorEmptyIteratorsUseTheSameOwnerSentinelRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-iterator-empty-iterators-use-the-same-owner-sentinel.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  const std::set<int> values;
  return values.begin() != values.end();
}
)r19containers");
  const auto Output = tmpFile("ordered-set-iterator-empty-iterators-use-the-same-owner-sentinel.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-iterator-empty-iterators-use-the-same-owner-sentinel" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetBoundsConstPointerLookupMatchesExtremeKeysRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-bounds-const-pointer-lookup-matches-extreme-keys.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  const std::set<int> values{(-2147483647 - 1), -2, 0, 2147483647};
  const auto* owner = &values;
  auto first = owner->begin();
  auto last = owner->find(2147483647);
  auto low = owner->lower_bound((-2147483647 - 1));
  auto high = owner->upper_bound(2147483647);
  auto end = owner->end();
  return low != first || high != end || *last != 2147483647;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-bounds-const-pointer-lookup-matches-extreme-keys.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-bounds-const-pointer-lookup-matches-extreme-keys" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetListAndLookupCustomOrderKeepsExistingSourceGuardRejected) {
  const auto Source = tmpFile("ordered-set-list-and-lookup-custom-order-keeps-existing-source-guard.cpp");
  writeFile(Source, R"r19containers(#include <set>
struct Descending { bool operator()(int a, int b) const { return a > b; } };
int main() {
  std::set<int, Descending> values{2, 4};
  return values.count(2) != 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedSetIteratorStatefulOrderingRemainsOutsideTheCheckedIteratorTypeRejected) {
  const auto Source = tmpFile("ordered-set-iterator-stateful-ordering-remains-outside-the-checked-iterator-type.cpp");
  writeFile(Source, R"r19containers(#include <set>
struct Comparator { int delta; bool operator()(int a, int b) const { return a + delta < b; } };
int main() {
  const std::set<int, Comparator> values({1, 2}, Comparator{0});
  return *values.begin() != 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedSetErasureKeyEraseKeepsOtherNodeAddressesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-erasure-key-erase-keeps-other-node-addresses.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  std::set<int> values{4, 1, 7, 0, 2, 6, 9, 5, 3, 8};
  auto six = values.find(6);
  const int* original = &*six;
  if (values.erase(4) != 1 || values.erase(4) != 0 || values.size() != 9) return 1;
  auto position = values.find(6);
  if (&*position != original || *six != 6) return 2;
  int previous = -1;
  for (int key : values) {
    if (key <= previous || key == 4) return 3;
    previous = key;
  }
  return previous != 9;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-erasure-key-erase-keeps-other-node-addresses.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-erasure-key-erase-keeps-other-node-addresses" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetErasureClearResetsOwnershipForReinsertionRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-erasure-clear-resets-ownership-for-reinsertion.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  std::set<int> values{6, 1, 9, 5, 7, 8};
  values.clear();
  values.clear();
  auto begin = values.begin();
  auto end = values.end();
  if (begin != end || !values.empty() || values.size() != 0) return 1;
  auto added = values.insert(-3);
  if (!added.second || *added.first != -3) return 2;
  auto result = values.erase(added.first);
  auto empty = values.end();
  return result != empty || !values.empty();
}
)r19containers");
  const auto Output = tmpFile("ordered-set-erasure-clear-resets-ownership-for-reinsertion.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-erasure-clear-resets-ownership-for-reinsertion" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetTransferSwapThroughPointerRebindsEmptyAndNonemptySentinelsRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-transfer-swap-through-pointer-rebinds-empty-and-nonempty-sentinels.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  std::set<int> left{4, 2, 6};
  std::set<int> right;
  auto retained = left.begin();
  const int* original = &*retained;
  auto* owner = &left;
  owner->swap(right);
  auto left_first = left.begin();
  auto left_end = left.end();
  if (left_first != left_end || right.size() != 3 || &*retained != original) return 1;
  right.swap(right);
  auto past_last = right.end();
  --past_last;
  if (*retained != 2 || *past_last != 6) return 2;
  ++past_last;
  auto end = right.end();
  return past_last != end;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-transfer-swap-through-pointer-rebinds-empty-and-nonempty-sentinels.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-transfer-swap-through-pointer-rebinds-empty-and-nonempty-sentinels" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetEqualRangeFoundAndAbsentKeysUseTheSameTreeRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-equal-range-found-and-absent-keys-use-the-same-tree.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  std::set<int> keys{7, -5, 3, 11, 16};
  auto found = keys.equal_range(3);
  auto gap = keys.equal_range(5);
  if (*found.first != 3 || *found.second != 7 || gap.first != gap.second ||
      gap.first != found.second) return 1;
  auto high = keys.equal_range(17);
  auto end = keys.end();
  return high.first != end || high.second != end;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-equal-range-found-and-absent-keys-use-the-same-tree.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-equal-range-found-and-absent-keys-use-the-same-tree" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetEqualRangeEmptyEqualRangeUsesBothOwnerSentinelsRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-equal-range-empty-equal-range-uses-both-owner-sentinels.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  std::set<int> keys;
  auto range = keys.equal_range(8);
  auto end = keys.end();
  if (range.first != end || range.second != end) return 1;
  keys.insert(8);
  auto after = keys.equal_range(8);
  return *after.first != 8 || after.second != end;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-equal-range-empty-equal-range-uses-both-owner-sentinels.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-equal-range-empty-equal-range-uses-both-owner-sentinels" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultisetKeyErasureRemovesAllMatchesAndPreservesOtherNodesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multiset-key-erasure-removes-all-matches-and-preserves-other-nodes.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  std::multiset<int> values{9, 9, -4, -1, 9, -1, 4, 16};
  auto survivor = values.find(4);
  const int* address = &*survivor;
  auto* owner = &values;
  if (owner->erase(9) != 3 || owner->count(9) != 0 || owner->erase(9) != 0) return 1;
  if (values.count(-1) != 2 || values.size() != 5 || &*survivor != address) return 2;
  owner->clear();
  return !owner->empty();
}
)r19containers");
  const auto Output = tmpFile("ordered-multiset-key-erasure-removes-all-matches-and-preserves-other-nodes.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multiset-key-erasure-removes-all-matches-and-preserves-other-nodes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetCopyConstructionSetIndependentNodesAndSortedCopyRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-copy-construction-set-independent-nodes-and-sorted-copy.cpp");
  writeFile(Source, R"r19containers(#include <set>
using Set = std::set<int>;
int visits;
const Set& read(const Set& value) { ++visits; return value; }
int main() {
  const Set source{5, -2, 3, 1, 9, 1};
  const int* original = &*source.find(3);
  Set copy(read(source));
  if (visits != 1 || copy.size() != 5 || &*copy.find(3) == original) return 1;
  auto position = copy.begin();
  const int expected[]{-2, 1, 3, 5, 9};
  for (int key : expected) {
    if (position == copy.end() || *position != key) return 2;
    ++position;
  }
  copy.erase(3);
  return position != copy.end() || copy.count(3) != 0 || &*source.find(3) != original;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-copy-construction-set-independent-nodes-and-sorted-copy.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-copy-construction-set-independent-nodes-and-sorted-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetCopyConstructionEmptyOwnersGetDistinctSentinelsAndNoAllocationsRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-copy-construction-empty-owners-get-distinct-sentinels-and-no-allocations.cpp");
  writeFile(Source, R"r19containers(#include <cstddef>
#include <cstdlib>
#include <set>
unsigned allocations, releases;
void* operator new(std::size_t bytes) { ++allocations; return std::malloc(bytes); }
void operator delete(void* value) noexcept { ++releases; std::free(value); }
void operator delete(void* value, std::size_t) noexcept { ++releases; std::free(value); }
int main() {
  {
    std::set<int> empty;
    std::set<int> copy(empty);
    if (copy.begin() != copy.end() || allocations != 0) return 1;
    copy.insert(17);
    if (!empty.empty() || allocations != 1) return 2;
    std::set<int> second(copy);
    if (allocations != 2 || second.count(17) != 1 || &*second.begin() == &*copy.begin()) return 3;
  }
  return releases != 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-copy-construction-empty-owners-get-distinct-sentinels-and-no-allocations.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-copy-construction-empty-owners-get-distinct-sentinels-and-no-allocations" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetCopyAssignmentMultisetReplacesEmptyAndShorterInputsRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-copy-assignment-multiset-replaces-empty-and-shorter-inputs.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  std::multiset<int> target{2};
  const std::multiset<int> source{5, 5, 11, 5, -3, -3};
  target = source;
  if (target.size() != 6 || target.count(5) != 3 || target.count(-3) != 2 || target.count(2)) return 1;
  std::multiset<int> empty;
  target = empty;
  return !target.empty() || target.begin() != target.end() || source.count(5) != 3;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-copy-assignment-multiset-replaces-empty-and-shorter-inputs.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-copy-assignment-multiset-replaces-empty-and-shorter-inputs" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetErasureCustomOrderingDoesNotUseIntegerErasureRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-erasure-custom-ordering-does-not-use-integer-erasure.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <set>
int main() {
  std::set<int, std::greater<int>> values;
  return values.erase(1) != 0;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-erasure-custom-ordering-does-not-use-integer-erasure.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-erasure-custom-ordering-does-not-use-integer-erasure" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetEqualRangeTransparentComparatorOverloadRemainsGuardedRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-equal-range-transparent-comparator-overload-remains-guarded.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <set>
int main() {
  const std::set<int, std::less<>> keys;
  const long query = 3;
  auto range = keys.equal_range(query);
  return range.first == range.second ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-equal-range-transparent-comparator-overload-remains-guarded.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-equal-range-transparent-comparator-overload-remains-guarded" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetCopyConstructionCustomComparatorRemainsOutsideCheckedCopyRejected) {
  const auto Source = tmpFile("ordered-set-copy-construction-custom-comparator-remains-outside-checked-copy.cpp");
  writeFile(Source, R"r19containers(#include <set>
struct Descending { bool operator()(int a, int b) const { return a > b; } };
int main() {
  const std::set<int, Descending> source{1, 3, 2};
  std::set<int, Descending> copy(source);
  return *copy.begin() != 3;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapDefaultMappedValueAndStableReferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-default-mapped-value-and-stable-reference.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> values;
  const int key = -13;
  int& original = values[key];
  if (original != 0 || values.size() != 1) return 1;
  original = 29;
  values[81] = 17;
  values[23] = 5;
  values[-40] = -8;
  values[-7] = 88;
  return values.size() != 5 || &values[key] != &original ||
         values[key] != 29 || values[23] != 5 || values[-7] != 88;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-default-mapped-value-and-stable-reference.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-default-mapped-value-and-stable-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapRepeatedSubscriptAllocatesAndDestroysOneNodePerKeyRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-repeated-subscript-allocates-and-destroys-one-node-per-key.cpp");
  writeFile(Source, R"r19containers(#include <cstddef>
#include <cstdlib>
#include <map>
unsigned allocations, releases;
void* operator new(std::size_t bytes) { ++allocations; return std::malloc(bytes); }
void operator delete(void* value) noexcept { ++releases; std::free(value); }
void operator delete(void* value, std::size_t) noexcept { ++releases; std::free(value); }
int main() {
  {
    std::map<int, int> map;
    map[8] = 63;
    map[-6] = 11;
    map[8] = 29;
    if (allocations != 2 || releases != 0 || map[8] != 29 || map[-6] != 11) return 1;
  }
  return releases != 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-repeated-subscript-allocates-and-destroys-one-node-per-key.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-repeated-subscript-allocates-and-destroys-one-node-per-key" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapConstPointerCountEvaluatesReceiverAndKeyOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-const-pointer-count-evaluates-receiver-and-key-once.cpp");
  writeFile(Source, R"r19containers(#include <map>
using Map = std::map<int, int>;
int trace;
const Map* get(const Map& map) { trace = trace * 10 + 4; return &map; }
int key() { trace = trace * 10 + 7; return -11; }
int main() {
  Map values;
  if (get(values)->count(key()) != 0 || trace != 47) return 1;
  trace = 0;
  values[-11] = 38;
  return get(values)->count(key()) != 1 || trace != 47 || values.size() != 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-const-pointer-count-evaluates-receiver-and-key-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-const-pointer-count-evaluates-receiver-and-key-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapMultimapInitializerListKeepsDuplicatesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-multimap-initializer-list-keeps-duplicates.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  const std::multimap<int, int> values{{9, 22}, {-7, 11}, {9, 17}, {9, 36}, {-7, 57}};
  return values.size() != 5 || values.count(9) != 3 || values.count(-7) != 2 || values.count(0) != 0;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-multimap-initializer-list-keeps-duplicates.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-multimap-initializer-list-keeps-duplicates" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapIteratorFindReturnsAMutableMappedLvalueRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-iterator-find-returns-a-mutable-mapped-lvalue.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> values{{17, 3}, {-8, 29}, {5, 51}};
  auto node = values.find(-8);
  auto end = values.end();
  if (node == end || node->first != -8 || node->second != 29) return 1;
  auto& pair = *node;
  pair.second = 39;
  values[23] = 41;
  auto missing = values.find(0);
  return values[-8] != 39 || &pair.second != &values[-8] || missing != end;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-iterator-find-returns-a-mutable-mapped-lvalue.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-iterator-find-returns-a-mutable-mapped-lvalue" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapIteratorMultimapEqualRangePreservesMappedDuplicateOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-iterator-multimap-equal-range-preserves-mapped-duplicate-order.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  const std::multimap<int, int> values{{6, 3}, {4, 7}, {6, 8}, {8, 5}, {6, 12}};
  auto range = values.equal_range(6);
  int seen = 0;
  for (auto item = range.first; item != range.second; ++item) {
    if (item->first != 6) return 1;
    seen = 100 * seen + item->second;
  }
  return seen != 30812 || range.second->first != 8;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-iterator-multimap-equal-range-preserves-mapped-duplicate-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-iterator-multimap-equal-range-preserves-mapped-duplicate-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapErasureEraseConstIteratorSpanReturnsMutableSuccessorRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-erasure-erase-const-iterator-span-returns-mutable-successor.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> values{{8, 1}, {2, 5}, {5, 9}, {14, 11}};
  auto first = values.cbegin();
  const auto& view = values;
  auto last = view.lower_bound(8);
  auto next = values.erase(first, last);
  next->second = 22;
  auto after = values.erase(next);
  return values.size() != 1 || after->first != 14 || after->second != 11;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-erasure-erase-const-iterator-span-returns-mutable-successor.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-erasure-erase-const-iterator-span-returns-mutable-successor" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapInsertionMapInsertResultKeepsFirstValueOnADuplicateRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-insertion-map-insert-result-keeps-first-value-on-a-duplicate.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <utility>
int main() {
  std::map<int, int> values;
  const std::pair<const int, int> one{19, 7};
  auto inserted = values.insert(one);
  auto duplicate = values.insert(std::pair<const int, int>{19, 71});
  auto other = values.insert(std::pair<const int, int>{-5, 33});
  return !inserted.second || duplicate.second || !other.second || inserted.first != duplicate.first ||
         inserted.first->second != 7 || other.first->first != -5 || values.size() != 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-insertion-map-insert-result-keeps-first-value-on-a-duplicate.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-insertion-map-insert-result-keeps-first-value-on-a-duplicate" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapInsertionMapPointerCallEvaluatesOwnerAndInputOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-insertion-map-pointer-call-evaluates-owner-and-input-once.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <utility>
using Map = std::map<int, int>;
using Pair = std::pair<const int, int>;
int trace;
Map* owner(Map& map) { trace = trace * 10 + 1; return &map; }
const Pair& input(const Pair& pair) { trace = trace * 10 + 2; return pair; }
int main() {
  Map values;
  const Pair pair{8, 20};
  auto result = owner(values)->insert(input(pair));
  return trace != 12 || !result.second || result.first->second != 20 || values.size() != 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-insertion-map-pointer-call-evaluates-owner-and-input-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-insertion-map-pointer-call-evaluates-owner-and-input-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapScalarEmplacementInsertOrAssignReusesNodeAndReportsInsertionRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-scalar-emplacement-insert-or-assign-reuses-node-and-reports-insertion.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> values;
  int key = 7;
  int value = 15;
  auto first = values.insert_or_assign(key, value);
  const int* const address = &first.first->second;
  auto existing = values.insert_or_assign(7, 31);
  auto second = values.insert_or_assign(-5, *address);
  return !first.second || existing.second || !second.second || first.first != existing.first ||
         address != &existing.first->second || *address != 31 || second.first->second != 31 || values.size() != 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-scalar-emplacement-insert-or-assign-reuses-node-and-reports-insertion.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-scalar-emplacement-insert-or-assign-reuses-node-and-reports-insertion" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapTransferMapCopyCreatesIndependentMappedObjectsRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-transfer-map-copy-creates-independent-mapped-objects.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> values{{7, 21}, {3, 14}, {-1, 6}};
  const std::map<int, int>& view = values;
  std::map<int, int> copy(view);
  auto first = copy.begin();
  auto node = copy.find(3);
  if (copy.size() != 3 || first->first != -1 || first->second != 6 ||
      node->second != 14 || &node->second == &values.find(3)->second) return 1;
  node->second = 19;
  values.clear();
  return !values.empty() || copy.size() != 3 || node->second != 19;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-transfer-map-copy-creates-independent-mapped-objects.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-transfer-map-copy-creates-independent-mapped-objects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapTransferMultimapSwapPreservesEquivalentNodeOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-transfer-multimap-swap-preserves-equivalent-node-order.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::multimap<int, int> left{{5, 10}, {5, 11}, {9, 90}};
  std::multimap<int, int> right;
  auto saved = left.begin();
  int* address = &saved->second;
  left.swap(right);
  right.swap(right);
  if (!left.empty() || right.count(5) != 2 || &saved->second != address) return 1;
  ++saved;
  return saved->first != 5 || saved->second != 11;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-transfer-multimap-swap-preserves-equivalent-node-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-transfer-multimap-swap-preserves-equivalent-node-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapCopyAssignmentMultimapRetainsDuplicatesAndClearsOldNodesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-copy-assignment-multimap-retains-duplicates-and-clears-old-nodes.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::multimap<int, int> input{{2, 20}, {1, 10}, {2, 21}, {2, 22}};
  std::multimap<int, int> output{{7, 70}};
  output = input;
  auto range = output.equal_range(2);
  auto it = range.first;
  if (it == range.second || it->second != 20)
    return 1;
  ++it;
  if (it == range.second || it->second != 21)
    return 2;
  ++it;
  if (it == range.second || it->second != 22)
    return 3;
  ++it;
  if (it != range.second || output.size() != 4 || output.count(7) != 0)
    return 4;
  std::multimap<int, int> empty;
  output = empty;
  return output.empty() && output.begin() == output.end() && input.count(2) == 3 ? 0 : 5;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-copy-assignment-multimap-retains-duplicates-and-clears-old-nodes.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-copy-assignment-multimap-retains-duplicates-and-clears-old-nodes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapAndSetCursorsKeepIndependentSentinelsRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-and-set-cursors-keep-independent-sentinels.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <set>
int main() {
  std::map<int, int> mapped{{1, 9}, {3, 7}};
  std::set<int> values{1, 4};
  const auto &map_view = mapped;
  const auto &set_view = values;
  auto map_it = map_view.end();
  auto set_it = set_view.end();
  --map_it;
  --set_it;
  return map_it->first == 3 && map_it->second == 7 && *set_it == 4 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-and-set-cursors-keep-independent-sentinels.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-and-set-cursors-keep-independent-sentinels" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapIteratorConversionMultimapMatchesDuplicatePositionRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-iterator-conversion-multimap-matches-duplicate-position.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::multimap<int, int> values{{2, 17}, {2, 19}, {6, 31}};
  auto mutable_cursor = values.begin();
  ++mutable_cursor;
  std::multimap<int, int>::const_iterator read_cursor(mutable_cursor);
  if (read_cursor->first != 2 || read_cursor->second != 19)
    return 1;
  ++read_cursor;
  return read_cursor->first == 6 && read_cursor->second == 31 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-iterator-conversion-multimap-matches-duplicate-position.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-iterator-conversion-multimap-matches-duplicate-position" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapScalarEmplacementMappedConversionRunsAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-scalar-emplacement-mapped-forwarding-conversions-require-their-own-proof.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> values;
  short value = 41;
  return !values.try_emplace(5, value).second;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-scalar-emplacement-mapped-forwarding-conversions-require-their-own-proof.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-scalar-emplacement-mapped-forwarding-conversions-require-their-own-proof" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapErasesWithLongMappedValues) {
  const auto Source = tmpFile("ordered-map-erasure-unsupported-mapped-storage-keeps-mutation-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, long> values;
  values.clear();
  return values.erase(7) != 0;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-erasure-unsupported-mapped-storage-keeps-mutation-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-erasure-unsupported-mapped-storage-keeps-mutation-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapStatefulPolicyRemainsOutsideCheckedMappedOwnerRejected) {
  const auto Source = tmpFile("ordered-map-stateful-policy-remains-outside-checked-mapped-owner.cpp");
  writeFile(Source, R"r19containers(#include <map>
struct Comparator {
  bool reverse;
  bool operator()(int a, int b) const { return reverse ? a > b : a < b; }
};
int main() {
  std::map<int, int, Comparator> values({{4, 17}, {7, 13}}, Comparator{true});
  return values.count(4) != 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapInsertionStatefulComparatorInsertionRemainsOutsideCheckedLayoutRejected) {
  const auto Source = tmpFile("ordered-map-insertion-stateful-comparator-insertion-remains-outside-checked-layout.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <utility>
struct Descending {
  bool active;
  bool operator()(int a, int b) const { return active ? a > b : a < b; }
};
int main() {
  std::map<int, int, Descending> values(Descending{true});
  const std::pair<const int, int> payload{7, 12};
  return !values.insert(payload).second;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapCopyAssignmentCustomComparatorRemainsUnsupportedRejected) {
  const auto Source = tmpFile("ordered-map-copy-assignment-custom-comparator-remains-unsupported.cpp");
  writeFile(Source, R"r19containers(#include <map>
struct Reverse {
  bool operator()(int left, int right) const { return left > right; }
};
using Table = std::map<int, int, Reverse>;
void copy_map(Table *output, const Table *input) { *output = *input; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapMoveAssignmentReleasesOldStorageAndKeepsIncomingNodeRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-move-assignment-releases-old-storage-and-keeps-incoming-node.cpp");
  writeFile(Source, R"r19containers(#include <map>
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
    std::map<int, int> input{{5, 50}, {3, 30}};
    std::map<int, int> output{{1, 10}, {4, 40}};
    auto *address = &input.find(5)->second;
    auto &result = (output = static_cast<std::map<int, int>&&>(input));
    if (&result != &output || allocations != 4 || releases != 2 ||
        output.size() != 2 || input.begin() != input.end())
      return 1;
    auto cursor = output.end();
    --cursor;
    if (cursor->first != 5 || &cursor->second != address || *address != 50)
      return 2;
  }
  return allocations == 4 && releases == 4 ? 0 : 3;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-move-assignment-releases-old-storage-and-keeps-incoming-node.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-move-assignment-releases-old-storage-and-keeps-incoming-node" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetMoveAssignmentKeepsIncomingKeyAddressRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-move-assignment-keeps-incoming-key-address.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  std::set<int> input{8, 2};
  std::set<int> output{1, 5, 7};
  const auto *key = &*input.find(8);
  output = static_cast<std::set<int>&&>(input);
  if (!input.empty() || input.begin() != input.end() || output.size() != 2)
    return 1;
  auto cursor = output.end();
  --cursor;
  return &*cursor == key && *key == 8 && output.count(7) == 0 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-move-assignment-keeps-incoming-key-address.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-move-assignment-keeps-incoming-key-address" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapInsertListMultimapKeepsDuplicateInsertionOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-insert-list-multimap-keeps-duplicate-insertion-order.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::multimap<int, int> values{{2, 20}};
  values.insert({{2, 21}, {1, 11}, {2, 22}});
  auto range = values.equal_range(2);
  auto it = range.first;
  if (it == range.second || it->second != 20) return 1;
  ++it;
  if (it == range.second || it->second != 21) return 2;
  ++it;
  if (it == range.second || it->second != 22) return 3;
  ++it;
  return it == range.second && values.size() == 4 ? 0 : 4;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-insert-list-multimap-keeps-duplicate-insertion-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-insert-list-multimap-keeps-duplicate-insertion-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetPointerRangeConstructionAndEmptyInsertRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-pointer-range-construction-and-empty-insert.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  int input[] = {5, -1, 5, 3};
  std::set<int> values(input, input + 4);
  const int *empty = nullptr;
  values.insert(empty, empty);
  auto cursor = values.begin();
  if (values.size() != 3 || *cursor++ != -1 || *cursor++ != 3) return 1;
  return *cursor == 5 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-pointer-range-construction-and-empty-insert.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-pointer-range-construction-and-empty-insert" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapPointerRangeUsesFirstValueAndCopiesStorageRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-pointer-range-uses-first-value-and-copies-storage.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int>::value_type input[] = {{2, 20}, {2, 99}, {1, 10}, {3, 30}};
  std::map<int, int> values(input, input + 2);
  const auto *first = input + 1;
  const auto *last = input + 4;
  values.insert(first, last);
  input[0].second = 100;
  return values.size() == 3 && values.find(2)->second == 20 &&
         values.find(1)->second == 10 && values.find(3)->second == 30 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-pointer-range-uses-first-value-and-copies-storage.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-pointer-range-uses-first-value-and-copies-storage" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapPointerRangeConvertingPairAcceptsFunctionBody) {
  const auto Source = tmpFile("ordered-map-pointer-range-converting-pair-remains-unsupported.cpp");
  writeFile(Source, R"r19containers(#include <map>
void insert(std::map<int, int> &values, const std::pair<int, int> *first,
            const std::pair<int, int> *last) { values.insert(first, last); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapInsertListCustomComparatorRemainsUnsupportedRejected) {
  const auto Source = tmpFile("ordered-map-insert-list-custom-comparator-remains-unsupported.cpp");
  writeFile(Source, R"r19containers(#include <map>
struct Reverse {
  bool operator()(int a, int b) const { return a > b; }
};
void insert(std::map<int, int, Reverse> &values) { values.insert({{1, 2}}); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapListAssignmentReusesOnlySuccessfulUniqueInsertionsRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-list-assignment-reuses-only-successful-unique-insertions.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <cstddef>
#include <cstdlib>
static unsigned allocations;
static unsigned releases;
void *operator new(std::size_t bytes) {
  ++allocations;
  return std::malloc(bytes);
}
void operator delete(void *ptr) noexcept {
  if (ptr) ++releases;
  std::free(ptr);
}
int main() {
  {
    std::map<int, int> values{{1, 10}, {2, 20}, {3, 30}};
    auto &result = (values = {{7, 70}, {7, 99}, {5, 50}});
    if (&result != &values || allocations != 3 || releases != 1 ||
        values.size() != 2 || values.count(2) || values.find(7)->second != 70) return 1;
    values = {{4, 40}, {5, 54}, {6, 60}, {8, 80}};
    if (allocations != 5 || releases != 1 || values.size() != 4 ||
        values.find(5)->second != 54) return 2;
  }
  return allocations == 5 && releases == 5 ? 0 : 3;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-list-assignment-reuses-only-successful-unique-insertions.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-list-assignment-reuses-only-successful-unique-insertions" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetListAssignmentPreservesOperatorAndMemberEvaluationOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-list-assignment-preserves-operator-and-member-evaluation-order.cpp");
  writeFile(Source, R"r19containers(#include <set>
int item(int &state, int value) {
  state = state * 10 + value;
  return value;
}
std::set<int> &owner(std::set<int> &values, int &state) {
  state = state * 10 + 1;
  return values;
}
int main() {
  std::set<int> values{0};
  int state = 0;
  auto &result = (owner(values, state) = {item(state, 3), item(state, 2)});
  if (&result != &values || state != 321 || values.count(0) || values.size() != 2) return 1;
  state = 0;
  owner(values, state).operator=({item(state, 7), item(state, 4)});
  return state == 174 && *values.begin() == 4 && values.size() == 2 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-list-assignment-preserves-operator-and-member-evaluation-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-list-assignment-preserves-operator-and-member-evaluation-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetEmplaceExactIntReusesTheExistingKeyWithoutAllocatingRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-emplace-exact-int-reuses-the-existing-key-without-allocating.cpp");
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
    int key = 9;
    const int duplicate = 9;
    int calls = 0;
    auto first = values.emplace(key);
    auto found = values.emplace(duplicate);
    auto other = values.emplace(++calls + 2);
    if (!first.second || found.second || first.first != found.first || !other.second ||
        *first.first != 9 || *other.first != 3 || calls != 1 ||
        allocations != 2 || releases != 0) return 1;
  }
  return allocations == 2 && releases == 2 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-emplace-exact-int-reuses-the-existing-key-without-allocating.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-emplace-exact-int-reuses-the-existing-key-without-allocating" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultisetEmplaceDefaultAndRvaluePreserveEquivalentOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multiset-emplace-default-and-rvalue-preserve-equivalent-order.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  std::multiset<int> values;
  auto *receiver = &values;
  auto first = receiver->emplace();
  auto second = values.emplace(0);
  auto third = values.emplace(0);
  values.emplace(-4);
  auto range = values.equal_range(0);
  auto cursor = range.first;
  if (cursor != first || *values.begin() != -4 || values.size() != 4) return 1;
  if (++cursor != second || ++cursor != third) return 2;
  return ++cursor == range.second ? 0 : 3;
}
)r19containers");
  const auto Output = tmpFile("ordered-multiset-emplace-default-and-rvalue-preserve-equivalent-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multiset-emplace-default-and-rvalue-preserve-equivalent-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapListAssignmentCustomComparatorRemainsUnsupportedRejected) {
  const auto Source = tmpFile("ordered-map-list-assignment-custom-comparator-remains-unsupported.cpp");
  writeFile(Source, R"r19containers(#include <map>
struct Reverse {
  bool operator()(int lhs, int rhs) const { return lhs > rhs; }
};
void assign(std::map<int, int, Reverse> &values) { values = {{1, 7}, {1, 8}}; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapMergeMultimapKeepsConflictsInTheSourceRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-merge-multimap-keeps-conflicts-in-the-source.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> output{{2, 20}};
  std::multimap<int, int> input{{1, 11}, {1, 12}, {2, 99}, {3, 33}};
  const int *transferred = &input.find(1)->second;
  output.merge(input);
  if (output.size() != 3 || input.size() != 2 ||
      output.find(1)->second != 11 || &output.find(1)->second != transferred) return 1;
  return input.find(1)->second == 12 && input.find(2)->second == 99 &&
         output.find(2)->second == 20 && output.find(3)->second == 33 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-merge-multimap-keeps-conflicts-in-the-source.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-merge-multimap-keeps-conflicts-in-the-source" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetSelfMergePreservesItsNodesAndSizeRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-self-merge-preserves-its-nodes-and-size.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  std::set<int> values{3, 4, 5};
  const int *identity = &*values.find(4);
  values.merge(values);
  return values.size() == 3 && &*values.find(4) == identity && *values.begin() == 3 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-self-merge-preserves-its-nodes-and-size.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-self-merge-preserves-its-nodes-and-size" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapComparisonUsesMappedValuesAfterEqualKeysRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-comparison-uses-mapped-values-after-equal-keys.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  const std::map<int, int> left{{3, 20}, {-1, 53}};
  const std::map<int, int> right{{3, 21}, {-1, 53}};
  const std::map<int, int> other{{4, -200}, {-1, 53}};
  return left != right && left < right && right < other && other > left &&
         !(left == right) && left >= left ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-comparison-uses-mapped-values-after-equal-keys.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-comparison-uses-mapped-values-after-equal-keys" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultisetComparisonDistinguishesDuplicateCountsRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multiset-comparison-distinguishes-duplicate-counts.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  const std::multiset<int> left{3, -2, 3};
  const std::multiset<int> right{-2, 3, 4};
  const std::multiset<int> equal{3, 3, -2};
  return left == equal && left != right && left < right &&
         right >= left && !(right <= left) ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-multiset-comparison-distinguishes-duplicate-counts.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multiset-comparison-distinguishes-duplicate-counts" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapEmplaceIntKeepsEquivalentInsertionOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-emplace-int-keeps-equivalent-insertion-order.cpp");
  writeFile(Source, R"r19containers(#include <map>
int bounded(int value, int &calls) { ++calls; return value; }
int main() {
  std::multimap<int, int> values{{2, 20}, {-1, 11}};
  int key_calls = 0;
  int mapped_calls = 0;
  auto added = values.emplace(bounded(2, key_calls), bounded(21, mapped_calls));
  auto next = values.emplace(2, 22);
  auto range = values.equal_range(2);
  auto cursor = range.first;
  if (key_calls != 1 || mapped_calls != 1 || cursor->second != 20) return 1;
  if (++cursor != added || cursor->second != 21) return 2;
  if (++cursor != next || cursor->second != 22) return 3;
  return ++cursor == range.second && values.size() == 4 ? 0 : 4;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-emplace-int-keeps-equivalent-insertion-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-emplace-int-keeps-equivalent-insertion-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapMergeCustomComparatorRemainsUnsupportedRejected) {
  const auto Source = tmpFile("ordered-map-merge-custom-comparator-remains-unsupported.cpp");
  writeFile(Source, R"r19containers(#include <map>
struct Reverse {
  bool operator()(int a, int b) const { return a > b; }
};
void merge(std::map<int, int> &output, std::map<int, int, Reverse> &input) { output.merge(input); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedSetIteratorRangeCopiesASubrangeRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-iterator-range-copies-a-subrange.cpp");
  writeFile(Source, R"r19containers(#include <set>
using Set = std::set<int>;

static Set::iterator endpoint(const Set &other, int key, int &calls) {
  ++calls;
  return other.find(key);
}

int main() {
  const Set source{7, 1, 6, 3, 9};
  int calls = 0;
  Set output(endpoint(source, 3, calls), endpoint(source, 7, calls));
  if (calls != 2 || output.size() != 2 || *output.begin() != 3 || output.count(6) != 1)
    return 1;
  output.insert(source.begin(), source.end());
  return output.size() == 5 && *output.begin() == 1 && source.size() == 5 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-iterator-range-copies-a-subrange.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-iterator-range-copies-a-subrange" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapMutableAndConstIteratorRangesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-mutable-and-const-iterator-ranges.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> input{{1, 10}, {2, 20}, {6, 60}};
  auto first = input.find(2);
  std::map<int, int> output(first, input.end());
  const std::multimap<int, int> extra{{2, 21}, {3, 30}, {3, 31}, {4, 40}};
  output.insert(extra.cbegin(), extra.cend());
  if (output.size() != 4 || output.count(1) || output[2] != 20 || output[3] != 30 ||
      output[4] != 40 || output[6] != 60 || first->second != 20)
    return 1;
  input[2] = 200;
  return output[2] == 20 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-mutable-and-const-iterator-ranges.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-mutable-and-const-iterator-ranges" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetExplicitDefaultPoliciesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-explicit-default-policies.cpp");
  writeFile(Source, R"r19containers(#include <set>
using Set = std::set<int>;

static const Set::key_compare &use_compare(const Set::key_compare &policy, int &calls) {
  ++calls;
  return policy;
}

static const Set::allocator_type &use_allocator(const Set::allocator_type &policy, int &calls) {
  ++calls;
  return policy;
}

int main() {
  const Set::key_compare compare{};
  const Set::allocator_type alloc{};
  int comparisons = 0, allocations = 0;
  Set a(use_compare(compare, comparisons));
  Set b(use_allocator(alloc, allocations));
  Set c{use_compare(compare, comparisons), use_allocator(alloc, allocations)};
  if (comparisons != 2 || allocations != 2 || !a.empty() || b.begin() != b.end() || !c.empty())
    return 1;
  a.insert(4);
  b.insert(2);
  c.insert(3);
  c.insert(1);
  return *a.begin() == 4 && *b.begin() == 2 && *c.begin() == 1 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-explicit-default-policies.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-explicit-default-policies" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapExplicitDefaultPoliciesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-explicit-default-policies.cpp");
  writeFile(Source, R"r19containers(#include <map>
using Map = std::map<int, int>;

static const Map::key_compare &use_compare(const Map::key_compare &policy, int &calls) {
  ++calls;
  return policy;
}

static const Map::allocator_type &use_allocator(const Map::allocator_type &policy, int &calls) {
  ++calls;
  return policy;
}

int main() {
  const Map::key_compare compare{};
  const Map::allocator_type alloc{};
  int comparisons = 0, allocations = 0;
  Map a(use_compare(compare, comparisons));
  Map b(use_allocator(alloc, allocations));
  Map c{use_compare(compare, comparisons), use_allocator(alloc, allocations)};
  if (comparisons != 2 || allocations != 2 || !a.empty() || b.begin() != b.end() || !c.empty())
    return 1;
  a[4] = 5;
  b[2] = 8;
  c[3] = 1;
  c[1] = 9;
  return a.begin()->second == 5 && b.begin()->second == 8 &&
         c.begin()->first == 1 && c.begin()->second == 9 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-explicit-default-policies.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-explicit-default-policies" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetAllocatorObserverEvaluatesReceiverRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-allocator-observer-evaluates-receiver.cpp");
  writeFile(Source, R"r19containers(#include <set>
using Set = std::set<int>;

static const Set *observe(const Set *owner, int &calls) {
  ++calls;
  return owner;
}

int main() {
  const Set values{2, 1, 4};
  int calls = 0;
  const auto allocator = observe(&values, calls)->get_allocator();
  return calls == 1 && allocator == Set::allocator_type{} && values.size() == 3 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-allocator-observer-evaluates-receiver.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-allocator-observer-evaluates-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapAllocatorObserverRebindsToPublicValueRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-allocator-observer-rebinds-to-public-value.cpp");
  writeFile(Source, R"r19containers(#include <map>
using Map = std::map<int, int>;

static const Map &observe(const Map &owner, int &calls) {
  ++calls;
  return owner;
}

int main() {
  Map values{{2, 7}};
  int calls = 0;
  const auto allocator = observe(values, calls).get_allocator();
  return calls == 1 && allocator == Map::allocator_type{} &&
         values.begin()->second == 7 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-allocator-observer-rebinds-to-public-value.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-allocator-observer-rebinds-to-public-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedAllocatorCopyConstructionRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-allocator-copy-construction-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  using Map = std::map<int, int>;
  Map source;
  source[2] = 1;
  Map copy(source, Map::allocator_type{});
  return copy.size() == 1 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-allocator-copy-construction-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-allocator-copy-construction-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapGetsAllocatorWithLongMappedValues) {
  const auto Source = tmpFile("ordered-allocator-observer-other-mapped-type-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  using Map = std::map<int, long>;
  const Map values{{1, 7}};
  return values.get_allocator() == Map::allocator_type{} && values.begin()->second == 7 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-allocator-observer-other-mapped-type-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-allocator-observer-other-mapped-type-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedRangeForeignCursorGuardRejected) {
  const auto Source = tmpFile("ordered-range-foreign-cursor-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <iterator>
int main() {
  std::map<int, int> input{{2, 8}, {4, 9}};
  std::map<int, int> output;
  output.insert(std::make_move_iterator(input.begin()), std::make_move_iterator(input.end()));
  return output.size() == 2 ? 0 : 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedSetExtractKeepsDetachedNodeUntilHandleDestructionRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-extract-keeps-detached-node-until-handle-destruction.cpp");
  writeFile(Source, R"r19containers(#include <cstddef>
#include <cstdlib>
#include <set>
#include <utility>
unsigned allocated, released;
void *operator new(std::size_t size) { ++allocated; return std::malloc(size); }
void operator delete(void *p) noexcept { ++released; std::free(p); }
void operator delete(void *p, std::size_t) noexcept { ++released; std::free(p); }

int main() {
  using Set = std::set<int>;
  {
    Set source{7, 1, 3};
    if (allocated != 3 || released != 0)
      return 1;
    {
      Set::node_type node;
      node = source.extract(3);
      if (!node || node.value() != 3 || source.count(3) || source.size() != 2)
        return 2;
      node.value() = 9;
      Set::node_type owner(std::move(node));
      if (!node.empty() || owner.value() != 9 || allocated != 3 || released != 0)
        return 3;
    }
    if (released != 1 || *source.begin() != 1)
      return 4;
  }
  return allocated == 3 && released == 3 ? 0 : 5;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-extract-keeps-detached-node-until-handle-destruction.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-extract-keeps-detached-node-until-handle-destruction" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapExtractAllowsDetachedKeyAndValueChangesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-extract-allows-detached-key-and-value-changes.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  using Map = std::map<int, int>;
  Map values{{1, 10}, {2, 20}, {3, 30}};
  auto node = values.extract(2);
  if (node.empty() || node.key() != 2 || node.mapped() != 20 || values.count(2))
    return 1;
  node.key() = 9;
  node.mapped() = 90;
  auto position = values.find(3);
  auto other = values.extract(position);
  return node.key() == 9 && node.mapped() == 90 &&
         other.key() == 3 && other.mapped() == 30 && values.size() == 1 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-extract-allows-detached-key-and-value-changes.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-extract-allows-detached-key-and-value-changes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetNodeInsertionKeepsAddressAndReturnsEndForEmptyRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-node-insertion-keeps-address-and-returns-end-for-empty.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <utility>
int main() {
  using Set = std::set<int>;
  Set source{1, 4}, destination{3};
  auto position = source.find(4);
  const int *address = &*position;
  auto node = source.extract(position);
  auto result = destination.insert(std::move(node));
  if (!result.inserted || !node.empty() || !result.node.empty() ||
      result.position == destination.end() || &*result.position != address)
    return 1;
  Set::node_type absent;
  auto skipped = destination.insert(std::move(absent));
  return !skipped.inserted && skipped.node.empty() &&
         skipped.position == destination.end() && destination.size() == 2 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-node-insertion-keeps-address-and-returns-end-for-empty.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-node-insertion-keeps-address-and-returns-end-for-empty" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultisetNodeInsertionTransfersBetweenOwnerKindsRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multiset-node-insertion-transfers-between-owner-kinds.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <utility>
int main() {
  std::set<int> source{5};
  std::multiset<int> destination{2, 5, 5};
  auto node = source.extract(5);
  auto position = destination.insert(std::move(node));
  if (position == destination.end() || *position != 5 || !node.empty() ||
      !source.empty() || destination.count(5) != 3)
    return 1;
  auto missing = source.extract(5);
  return destination.insert(std::move(missing)) == destination.end() ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-multiset-node-insertion-transfers-between-owner-kinds.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multiset-node-insertion-transfers-between-owner-kinds" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedNodeCaseRetainsReinsertionGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-node-case-retains-reinsertion-guard.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <utility>
int main() {
  std::set<int> source{1, 4}, destination{3};
  auto node = source.extract(4);
  auto result = destination.insert(std::move(node));
  return result.inserted && node.empty() && *result.position == 4 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-node-case-retains-reinsertion-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-node-case-retains-reinsertion-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapValueCompareCanBeCalledDirectlyRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-value-compare-can-be-called-directly.cpp");
  writeFile(Source, R"r19containers(#include <map>
using Map = std::multimap<int, int>;
static const Map *owner(const Map *value, int &count) {
  ++count;
  return value;
}

int main() {
  const Map values{{3, 8}, {3, 9}, {4, 7}};
  auto first = values.begin();
  auto last = values.find(4);
  int count = 0;
  return owner(&values, count)->value_comp()(*first, *last) && count == 1 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-value-compare-can-be-called-directly.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-value-compare-can-be-called-directly" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedNodeHintedInsertionRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-node-hinted-insertion-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <utility>
int main() {
  std::multimap<int, int> source{{2, 10}}, destination{{2, 20}};
  auto node = source.extract(2);
  auto inserted = destination.insert(destination.cbegin(), std::move(node));
  return node.empty() && inserted == destination.begin() && inserted->second == 10 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-node-hinted-insertion-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-node-hinted-insertion-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapValueCompareCustomPolicyGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-value-compare-custom-policy-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <functional>
int main() {
  using Map = std::map<int, int, std::greater<int>>;
  const Map values;
  auto compare = values.value_comp();
  return compare(Map::value_type{2, 10}, Map::value_type{1, 20}) ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-value-compare-custom-policy-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-value-compare-custom-policy-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetHintedNodeConflictLeavesTheHandleRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-hinted-node-conflict-leaves-the-handle.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <utility>
using Set = std::set<int>;
static Set *observe(Set *owner, int &calls) {
  ++calls;
  return owner;
}
int main() {
  Set source{2, 4, 6}, destination{2, 3, 5};
  auto conflict = source.extract(2);
  auto same = destination.insert(destination.cbegin(), std::move(conflict));
  if (same != destination.begin() || conflict.empty() || conflict.value() != 2)
    return 1;
  auto adjacent = source.extract(4);
  auto four = destination.insert(destination.find(3), std::move(adjacent));
  if (four == destination.end() || *four != 4 || !adjacent.empty())
    return 2;
  auto distant = source.extract(6);
  auto six = destination.insert(destination.cbegin(), std::move(distant));
  if (six == destination.end() || *six != 6 || !distant.empty() || destination.size() != 5)
    return 3;
  Set::node_type empty;
  int calls = 0;
  auto missing = destination.insert(observe(&destination, calls)->cend(), std::move(empty));
  return missing == destination.end() && calls == 1 && empty.empty() ? 0 : 4;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-hinted-node-conflict-leaves-the-handle.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-hinted-node-conflict-leaves-the-handle" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultisetHintedNodePrecedesAnEquivalentCursorRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multiset-hinted-node-precedes-an-equivalent-cursor.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <utility>
int main() {
  std::multiset<int> source{2}, destination{1, 2, 2, 3};
  const int *saved = &*source.begin();
  auto node = source.extract(source.cbegin());
  auto hint = destination.lower_bound(2);
  auto result = destination.insert(hint, std::move(node));
  return result != hint && result == destination.lower_bound(2) &&
         &*result == saved && node.empty() && destination.count(2) == 3 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-multiset-hinted-node-precedes-an-equivalent-cursor.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multiset-hinted-node-precedes-an-equivalent-cursor" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetHintedValueInsertionAndEmplacementRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-hinted-value-insertion-and-emplacement.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  std::set<int> values{1, 3};
  const int key = 2;
  auto middle = values.insert(values.find(3), key);
  auto same = values.emplace_hint(values.cend(), 2);
  if (same != middle || *same != 2 || values.size() != 3)
    return 1;
  auto zero = values.emplace_hint(values.cbegin());
  auto duplicate = values.emplace_hint(values.cend());
  return zero == values.begin() && duplicate == zero && *zero == 0 && values.size() == 4 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-hinted-value-insertion-and-emplacement.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-hinted-value-insertion-and-emplacement" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapHintedTryEmplaceAndInsertOrAssignRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-hinted-try-emplace-and-insert-or-assign.cpp");
  writeFile(Source, R"r19containers(#include <map>
static int observe(int &calls, int value) {
  ++calls;
  return value;
}
int main() {
  std::map<int, int> values{{2, 20}, {5, 50}};
  int calls = 0;
  auto existing = values.find(2);
  auto kept = values.try_emplace(values.cend(), 2, observe(calls, 99));
  if (kept != existing || kept->second != 20 || calls != 1)
    return 1;
  const int three = 3;
  auto created = values.try_emplace(values.find(5), three);
  if (created->second != 0 || created->first != 3)
    return 2;
  auto assigned = values.insert_or_assign(values.cbegin(), three, observe(calls, 31));
  auto inserted = values.insert_or_assign(values.cend(), 7, 70);
  return assigned == created && assigned->second == 31 && inserted->second == 70 &&
         calls == 2 && values.size() == 4 ? 0 : 3;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-hinted-try-emplace-and-insert-or-assign.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-hinted-try-emplace-and-insert-or-assign" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetAllocatorCopyEvaluatesAllocatorAndOwnsNewNodesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-allocator-copy-evaluates-allocator-and-owns-new-nodes.cpp");
  writeFile(Source, R"r19containers(#include <set>
using Set = std::set<int>;
static const Set::allocator_type *observe(const Set::allocator_type *a, int &calls) {
  ++calls;
  return a;
}
int main() {
  const Set source{2, 5, 7};
  const Set::allocator_type allocator;
  int calls = 0;
  Set copy(source, *observe(&allocator, calls));
  if (calls != 1 || copy != source || &*copy.begin() == &*source.begin())
    return 1;
  copy.erase(5);
  return source.count(5) == 1 && copy.count(5) == 0 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-allocator-copy-evaluates-allocator-and-owns-new-nodes.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-allocator-copy-evaluates-allocator-and-owns-new-nodes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapAllocatorMoveKeepsPayloadAndNodeRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-allocator-move-keeps-payload-and-node.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <utility>
int main() {
  using Map = std::map<int, int>;
  Map source{{2, 20}, {5, 50}};
  const Map::value_type *saved = &*source.begin();
  Map moved(std::move(source), Map::allocator_type{});
  if (!source.empty() || moved.size() != 2 || &*moved.begin() != saved)
    return 1;
  moved[2] = 21;
  return moved.find(2)->second == 21 && moved.find(5)->second == 50 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-allocator-move-keeps-payload-and-node.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-allocator-move-keeps-payload-and-node" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedHintedNodeCustomPolicyGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-hinted-node-custom-policy-guard.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <map>
#include <utility>
int main() {
  std::map<int, int, std::greater<int>> source{{2, 20}}, destination{{1, 10}};
  auto node = source.extract(2);
  auto position = destination.insert(destination.cbegin(), std::move(node));
  return node.empty() && position->first == 2 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-hinted-node-custom-policy-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-hinted-node-custom-policy-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedAllocatorExtendedConstructorCustomCompareGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-allocator-extended-constructor-custom-compare-guard.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <map>
int main() {
  using Map = std::map<int, int, std::greater<int>>;
  Map source{{2, 20}};
  Map copy(source, Map::allocator_type{});
  return copy.size() == 1 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-allocator-extended-constructor-custom-compare-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-allocator-extended-constructor-custom-compare-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetReverseEndpointsAndPostfixStepRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-reverse-endpoints-and-postfix-step.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  std::set<int> values{1, 3, 7};
  auto it = values.rbegin();
  auto end = values.rend();
  if (it.base() != values.end() || end.base() != values.begin() || *it != 7)
    return 1;
  auto top = it++;
  if (*top != 7 || *it != 3 || top == it)
    return 2;
  --it;
  return it == top && *it == 7 ? 0 : 3;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-reverse-endpoints-and-postfix-step.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-reverse-endpoints-and-postfix-step" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapReverseArrowMutatesSelectedNodeRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-reverse-arrow-mutates-selected-node.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> values{{2, 20}, {5, 50}, {7, 70}};
  auto top = values.rbegin();
  auto second = top;
  ++second;
  second->second = 51;
  if (top->first != 7 || (*second).first != 5)
    return 1;
  auto old = second--;
  return old->second == 51 && second == top && values.find(5)->second == 51 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-reverse-arrow-mutates-selected-node.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-reverse-arrow-mutates-selected-node" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetReverseRangeUsesHalfOpenEndpointsRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-reverse-range-uses-half-open-endpoints.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  const std::set<int> source{1, 2, 3, 4};
  auto first = source.rbegin();
  ++first;
  auto last = first;
  ++last;
  ++last;
  std::set<int> values(first, last);
  return values.size() == 2 && *values.begin() == 2 && values.count(3) == 1 &&
         values.count(1) == 0 && values.count(4) == 0 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-reverse-range-uses-half-open-endpoints.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-reverse-range-uses-half-open-endpoints" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapReverseRangeRetainsExistingMappedValuesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-reverse-range-retains-existing-mapped-values.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> source{{1, 10}, {3, 30}, {5, 50}};
  std::map<int, int> copy(source.rbegin(), source.rend());
  std::map<int, int> values{{3, 99}};
  values.insert(copy.crbegin(), copy.crend());
  return values.size() == 3 && values.find(1)->second == 10 && values.find(3)->second == 99 &&
         values.find(5)->second == 50 && copy.find(3)->second == 30 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-reverse-range-retains-existing-mapped-values.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-reverse-range-retains-existing-mapped-values" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapReverseCursorConvertsToConstRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-reverse-cursor-converts-to-const.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  using Map = std::map<int, int>;
  Map values{{1, 10}, {5, 50}};
  auto mutable_cursor = values.rbegin();
  Map::const_reverse_iterator read = mutable_cursor;
  mutable_cursor->second = 51;
  return read->second == 51 && read == values.crbegin() &&
         read.base() == values.cend() ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-reverse-cursor-converts-to-const.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-reverse-cursor-converts-to-const" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapAssignsMutableReversePositionToConstRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-assigns-mutable-reverse-position-to-const.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  using Map = std::multimap<int, int>;
  Map values{{2, 20}, {2, 21}, {5, 50}};
  auto from = values.rbegin();
  ++from;
  Map::const_reverse_iterator to = values.crend();
  to = from;
  if (to != from || to->second != 21)
    return 1;
  ++from;
  return to != from && from->second == 20 && to->second == 21 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-assigns-mutable-reverse-position-to-const.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-assigns-mutable-reverse-position-to-const" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapTraversesInReverseWithLongMappedValues) {
  const auto Source = tmpFile("ordered-map-reverse-cursor-different-mapped-storage-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  const std::map<int, long> values{{2, 20}};
  auto it = values.crbegin();
  return it->second == 20 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-reverse-cursor-different-mapped-storage-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-reverse-cursor-different-mapped-storage-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedReverseCursorCustomPolicyGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-reverse-cursor-custom-policy-guard.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <set>
int main() {
  const std::set<int, std::greater<int>> values{2, 5};
  auto it = values.rbegin();
  return *it == 2 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-reverse-cursor-custom-policy-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-reverse-cursor-custom-policy-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedReverseRangeAcrossStandardComparators) {
  const auto Source = tmpFile("ordered-reverse-range-custom-policy-guard.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <set>
int main() {
  const std::set<int, std::greater<int>> source{1, 2, 3};
  std::set<int> values(source.rbegin(), source.rend());
  return values.size() == 3 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-reverse-range-custom-policy-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-reverse-range-custom-policy-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapInsertsConstPairPointerSubrangeRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-inserts-const-pair-pointer-subrange.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  const std::pair<int, int> input[]{{1, 10}, {2, 20}, {3, 30}, {4, 40}};
  std::map<int, int> values{{2, 99}};
  values.insert(input + 1, input + 3);
  return values.size() == 2 && values.find(2)->second == 99 &&
         values.find(3)->second == 30 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-inserts-const-pair-pointer-subrange.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-inserts-const-pair-pointer-subrange" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapInsertsMutablePairPointersRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-inserts-mutable-pair-pointers.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::pair<int, int> input[]{{2, 20}, {2, 21}};
  std::multimap<int, int> values{{2, 19}};
  values.insert(input, input);
  values.insert(input, input + 2);
  auto cursor = values.lower_bound(2);
  return values.size() == 3 && cursor->second == 19 &&
         (++cursor)->second == 20 && (++cursor)->second == 21 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-inserts-mutable-pair-pointers.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-inserts-mutable-pair-pointers" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapEmplacementConvertsKeysAndMappedIntegersRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-emplacement-converts-keys-and-mapped-integers.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> values;
  signed char key = 3;
  short mapped = -7;
  auto inserted = values.emplace(key, mapped);
  if (!inserted.second || inserted.first->second != -7)
    return 1;
  auto duplicate = values.try_emplace(3, static_cast<unsigned char>(10));
  if (duplicate.second || duplicate.first->second != -7)
    return 2;
  auto assigned = values.insert_or_assign(3, static_cast<unsigned short>(11));
  std::multimap<int, int> duplicates;
  auto first = duplicates.emplace(key, mapped);
  auto second = duplicates.emplace(key, static_cast<unsigned char>(8));
  return !assigned.second && assigned.first->second == 11 && values.size() == 1 &&
         first->second == -7 && second->second == 8 &&
         duplicates.count(3) == 2 ? 0 : 3;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-emplacement-converts-keys-and-mapped-integers.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-emplacement-converts-keys-and-mapped-integers" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedEmplacementUserDefinedKeyConversionGuardRejected) {
  const auto Source = tmpFile("ordered-emplacement-user-defined-key-conversion-guard.cpp");
  writeFile(Source, R"r19containers(#include <set>
struct Key {
  operator int() const { return 3; }
};
int main() {
  std::set<int> values;
  return values.emplace(Key{}).second ? 0 : 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapEmplacementCopiesExactIntegerPairRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-emplacement-copies-exact-integer-pair.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  using Map = std::map<int, int>;
  Map values;
  Map::value_type first{3, 30};
  auto inserted = values.emplace(first);
  first.second = 31;
  const std::pair<int, int> second{2, 20};
  auto plain = values.emplace(second);
  auto duplicate = values.emplace(std::make_pair(3, 99));
  return inserted.second && plain.second && !duplicate.second &&
         duplicate.first == inserted.first && inserted.first->second == 30 &&
         values.begin()->first == 2 && values.size() == 2 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-emplacement-copies-exact-integer-pair.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-emplacement-copies-exact-integer-pair" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapDefaultEmplacementPreservesExistingZeroKeyRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-default-emplacement-preserves-existing-zero-key.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::multimap<int, int> values{{0, 9}, {1, 10}};
  auto created = values.emplace();
  auto cursor = values.begin();
  return values.size() == 3 && cursor->second == 9 &&
         ++cursor == created && cursor->first == 0 && cursor->second == 0 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-default-emplacement-preserves-existing-zero-key.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-default-emplacement-preserves-existing-zero-key" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapPiecewiseEmptyKeyAndValueTuplesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-piecewise-empty-key-and-value-tuples.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <tuple>
int main() {
  std::map<int, int> map;
  auto first = map.emplace(std::piecewise_construct, std::tuple<>{}, std::tuple<int>{12});
  auto next = map.emplace(std::piecewise_construct, std::tuple<int>{2}, std::tuple<>{});
  return first.second && next.second && first.first->first == 0 &&
         first.first->second == 12 && next.first->second == 0 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-piecewise-empty-key-and-value-tuples.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-piecewise-empty-key-and-value-tuples" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapPiecewiseConstructsBeforeUniqueConflictRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-piecewise-constructs-before-unique-conflict.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <tuple>
int main() {
  std::map<int, int> map{{3, 30}};
  int key = 3;
  int value = 31;
  auto result = map.emplace(std::piecewise_construct, std::forward_as_tuple(key),
                            std::forward_as_tuple(value));
  return !result.second && result.first->second == 30 && map.size() == 1 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-piecewise-constructs-before-unique-conflict.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-piecewise-constructs-before-unique-conflict" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapDistanceCountsConstIteratorsAndEmptyRangesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-distance-counts-const-iterators-and-empty-ranges.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <iterator>
int main() {
  const std::map<int, int> values{{1, 10}, {3, 30}, {8, 80}};
  return std::distance(values.cbegin(), values.cend()) == 3 &&
         std::distance(values.cend(), values.cend()) == 0 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-distance-counts-const-iterators-and-empty-ranges.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-distance-counts-const-iterators-and-empty-ranges" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapNextRetainsEquivalentPositionRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-next-retains-equivalent-position.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <iterator>
int main() {
  std::multimap<int, int> values{{2, 20}, {2, 21}, {2, 22}, {3, 30}};
  auto first = values.cbegin();
  auto third = std::next(first, 2);
  auto second = std::prev(third);
  return std::distance(first, third) == 2 && first->second == 20 &&
         second->second == 21 && third->second == 22 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-next-retains-equivalent-position.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-next-retains-equivalent-position" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapFreeBeginKeepsMappedMutabilityRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-free-begin-keeps-mapped-mutability.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <iterator>
int main() {
  std::map<int, int> map{{2, 20}, {4, 40}};
  auto write = std::begin(map);
  write->second = 21;
  auto read = std::cbegin(map);
  auto last = std::cend(map);
  return read->first == 2 && read->second == 21 && ++read != last &&
         read->second == 40 && ++read == last ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-free-begin-keeps-mapped-mutability.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-free-begin-keeps-mapped-mutability" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapFreeConstReverseAccessUsesOwnerEndpointsRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-free-const-reverse-access-uses-owner-endpoints.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <iterator>
int main() {
  std::multimap<int, int> map{{1, 10}, {2, 20}, {2, 21}};
  auto r = std::crbegin(map);
  auto e = std::crend(map);
  return r.base() == std::cend(map) && r->second == 21 && (++r)->second == 20 &&
         (++r)->second == 10 && ++r == e && e.base() == std::cbegin(map) ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-free-const-reverse-access-uses-owner-endpoints.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-free-const-reverse-access-uses-owner-endpoints" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapReverseDistanceKeepsEquivalentPositionsRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-reverse-distance-keeps-equivalent-positions.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <iterator>
int main() {
  const std::multimap<int, int> values{{1, 10}, {2, 20}, {2, 21}};
  auto first = values.crbegin();
  auto last = values.crend();
  const std::multimap<int, int> empty;
  return std::distance(first, last) == 3 && std::distance(last, last) == 0 &&
         std::distance(empty.rbegin(), empty.rend()) == 0 && first->second == 21 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-reverse-distance-keeps-equivalent-positions.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-reverse-distance-keeps-equivalent-positions" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultisetReverseAdvanceCanReachAndLeaveEndRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multiset-reverse-advance-can-reach-and-leave-end.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <iterator>
int main() {
  const std::multiset<int> values{2, 2, 5};
  auto cursor = values.rbegin();
  std::advance(cursor, 3);
  if (cursor != values.rend())
    return 1;
  std::advance(cursor, -2);
  return *cursor == 2 && std::distance(values.rbegin(), cursor) == 1 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-multiset-reverse-advance-can-reach-and-leave-end.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multiset-reverse-advance-can-reach-and-leave-end" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapHintedExactPairPreservesExistingMappedValueRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-hinted-exact-pair-preserves-existing-mapped-value.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int> values{{1, 10}, {4, 40}};
  const std::pair<int, int> pair{3, 30};
  auto first = values.emplace_hint(values.find(4), pair);
  auto duplicate = values.emplace_hint(values.cbegin(), std::make_pair(3, 99));
  return first == duplicate && first->first == 3 && first->second == 30 &&
         (++first)->first == 4 && values.size() == 3 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-hinted-exact-pair-preserves-existing-mapped-value.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-hinted-exact-pair-preserves-existing-mapped-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapHintedPiecewisePositionsValueAndKeepsEquivalentsRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-hinted-piecewise-positions-value-and-keeps-equivalents.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <tuple>
int main() {
  std::multimap<int, int> values{{2, 20}, {2, 21}, {5, 50}};
  auto hint = values.cbegin();
  ++hint;
  auto inserted = values.emplace_hint(hint, std::piecewise_construct,
      std::forward_as_tuple(2), std::forward_as_tuple(19));
  auto before = inserted;
  auto after = inserted;
  return inserted->second == 19 && (--before)->second == 20 &&
         ++after == hint && after->second == 21 && values.size() == 4 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-hinted-piecewise-positions-value-and-keeps-equivalents.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-hinted-piecewise-positions-value-and-keeps-equivalents" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedWideMultimapPiecewiseAndHintedPairKeep64BitValuesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-wide-multimap-piecewise-and-hinted-pair-keep-64-bit-values.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <tuple>
int main() {
  std::multimap<int, long long> values;
  values.emplace(std::piecewise_construct, std::forward_as_tuple(2),
                 std::forward_as_tuple(4398046511104LL));
  auto first = values.emplace_hint(values.cbegin(), std::make_pair(2, -4398046511105LL));
  auto second = first;
  return first == values.begin() && first->second == -4398046511105LL &&
         (++second)->second == 4398046511104LL && values.size() == 2 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-wide-multimap-piecewise-and-hinted-pair-keep-64-bit-values.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-wide-multimap-piecewise-and-hinted-pair-keep-64-bit-values" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedWideMapComparisonAndAssignmentUseFullSignedValueRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-wide-map-comparison-and-assignment-use-full-signed-value.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, long long> first;
  first.try_emplace(2, 4398046511104LL);
  auto second = first;
  second.insert_or_assign(2, -4398046511104LL);
  if (!(second < first) || second == first || first.begin()->second != 4398046511104LL)
    return 1;
  second = first;
  return second == first && second.begin()->second == 4398046511104LL ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-wide-map-comparison-and-assignment-use-full-signed-value.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-wide-map-comparison-and-assignment-use-full-signed-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapAtProvesKeyInLaterInitializerEntryRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-at-proves-key-in-later-initializer-entry.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  const std::map<int, int> values{{6, 4}, {-3, 8}, {2, 2}};
  constexpr int key = -3;
  return values.at(key) == 8 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-at-proves-key-in-later-initializer-entry.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-at-proves-key-in-later-initializer-entry" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapAtDuplicateInitializerKeepsFirstValueRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-at-duplicate-initializer-keeps-first-value.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  const std::map<int, int> values{{4, 41}, {4, 16}, {5, 19}};
  return values.size() == 2 && values.at(4) == 41 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-at-duplicate-initializer-keeps-first-value.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-at-duplicate-initializer-keeps-first-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapFreeSizeWithLongMappedValues) {
  const auto Source = tmpFile("ordered-free-access-other-mapped-storage-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <iterator>
int main() {
  std::map<int, long> map{{1, 10}};
  return std::size(map) == 1 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-free-access-other-mapped-storage-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-free-access-other-mapped-storage-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapStoresDoubleMappedValues) {
  const auto Source = tmpFile("ordered-wide-mapped-floating-storage-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, double> values{{1, 2.0}};
  return values[1] == 2.0 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-wide-mapped-floating-storage-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-wide-mapped-floating-storage-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapPiecewiseCustomPolicyGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-piecewise-custom-policy-guard.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <map>
#include <tuple>
int main() {
  std::map<int, int, std::greater<int>> map;
  return map.emplace(std::piecewise_construct, std::tuple<int>{1}, std::tuple<int>{2}).second ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-piecewise-custom-policy-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-piecewise-custom-policy-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedFreeAccessCustomComparatorGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-free-access-custom-comparator-guard.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <functional>
#include <iterator>
int main() {
  std::set<int, std::greater<int>> values{1, 2};
  return *std::begin(values) == 2 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-free-access-custom-comparator-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-free-access-custom-comparator-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapHintedPiecewiseCustomPolicyGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-hinted-piecewise-custom-policy-guard.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <map>
#include <tuple>
int main() {
  std::map<int, int, std::greater<int>> values;
  return values.emplace_hint(values.cend(), std::piecewise_construct,
      std::forward_as_tuple(2), std::forward_as_tuple(7))->second == 7 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-hinted-piecewise-custom-policy-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-hinted-piecewise-custom-policy-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapPiecewiseNarrowTupleLeafGuardRejected) {
  const auto Source = tmpFile("ordered-map-piecewise-narrow-tuple-leaf-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <tuple>
int main() {
  std::map<int, int> map;
  return map.emplace(std::piecewise_construct, std::tuple<int>{1},
                     std::tuple<short>{2}).second ? 0 : 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedWideMapOtherPairLeafGuardRejected) {
  const auto Source = tmpFile("ordered-wide-map-other-pair-leaf-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, long long> values;
  return values.emplace(std::make_pair(1, 2.0)).second ? 0 : 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapAtReferenceReceiverDoesNotProvePresenceRejected) {
  const auto Source = tmpFile("ordered-map-at-reference-receiver-does-not-prove-presence.cpp");
  writeFile(Source, R"r19containers(#include <map>
int read(const std::map<int, int> &values) {
  return values.at(3);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapUnsignedLongLongComparesAboveSignedMaximumRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-unsigned-long-long-compares-above-signed-maximum.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  constexpr unsigned long long high = 18446744073709551615ULL;
  std::map<int, unsigned long long> left;
  left.emplace(3, high);
  auto right = left;
  right.insert_or_assign(3, 0ULL);
  if (!(right < left) || left < right || right == left)
    return 1;
  auto node = left.extract(3);
  right.erase(3);
  auto placed = right.insert(std::move(node));
  return placed.inserted && placed.position->second == high && right.size() == 1 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-unsigned-long-long-compares-above-signed-maximum.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-unsigned-long-long-compares-above-signed-maximum" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapUlongAndSizeTRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-ulong-and-size-t.cpp");
  writeFile(Source, R"r19containers(#include <cstddef>
#include <limits>
#include <map>
int main() {
  constexpr auto limit = std::numeric_limits<unsigned long>::max();
  std::map<int, unsigned long> values;
  values.try_emplace(1, limit);
  auto copy = values;
  if (copy[2] != 0UL || copy.find(1)->second != limit)
    return 1;
  std::map<int, std::size_t> sizes;
  auto item = sizes.try_emplace(1, sizeof(unsigned long));
  return item.second && item.first->second == sizeof(unsigned long) ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-ulong-and-size-t.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-ulong-and-size-t" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapBoolAndCharacterValuesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-bool-and-character-values.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, bool> flags;
  flags.try_emplace(2, true);
  if (flags[1] || !flags[2])
    return 1;
  std::map<int, char> letters;
  letters.try_emplace(1, 'x');
  std::map<int, signed char> signed_bytes;
  signed_bytes.try_emplace(1, static_cast<signed char>(-7));
  std::map<int, unsigned char> bytes;
  bytes.try_emplace(1, static_cast<unsigned char>(241));
  return letters[1] == 'x' && signed_bytes[1] == -7 && bytes[1] == 241 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-bool-and-character-values.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-bool-and-character-values" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ClimitsLongWidthFollowsTargetRunAtBothOptimizations) {
  const auto Source = tmpFile("climits-long-width-follows-target.cpp");
  writeFile(Source, R"r19containers(#include <climits>
static_assert(LONG_MIN + LONG_MAX == -1L);
static_assert(ULONG_MAX == 2UL * LONG_MAX + 1UL);
static_assert(LLONG_MIN + LLONG_MAX == -1LL);
static_assert(ULLONG_MAX == 18446744073709551615ULL);
int main() {
  return (sizeof(long) == 4 ? LONG_MAX == 2147483647LL
                            : LONG_MAX == 9223372036854775807LL) ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("climits-long-width-follows-target.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("climits-long-width-follows-target" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapWideUnsignedUsesClimitsMaximumRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-wide-unsigned-uses-climits-maximum.cpp");
  writeFile(Source, R"r19containers(#include <climits>
#include <map>
int main() {
  std::map<int, unsigned long long> values;
  values.emplace(1, ULLONG_MAX);
  auto other = values;
  other.insert_or_assign(1, 0ULL);
  return other < values && values.find(1)->second == ULLONG_MAX ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-wide-unsigned-uses-climits-maximum.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-wide-unsigned-uses-climits-maximum" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapAtIfReturnsMutableReferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-at-if-returns-mutable-reference.cpp");
  writeFile(Source, R"r19containers(#include <map>
int& lookup(std::map<int, int>& values, int key, int& fallback) {
  if (values.find(key) != values.end())
    return values.at(key);
  return fallback;
}
int main() {
  std::map<int, int> values{{2, 20}};
  int fallback = -1;
  lookup(values, 2, fallback) = 91;
  lookup(values, 3, fallback) = 14;
  return values.size() == 1 && values.find(2)->second == 91 && fallback == 14 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-at-if-returns-mutable-reference.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-at-if-returns-mutable-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapAtMutableTernaryDoesNotInsertRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-at-mutable-ternary-does-not-insert.cpp");
  writeFile(Source, R"r19containers(#include <map>
int lookup(int key) {
  std::map<int, int> values{{2, 20}};
  int result = values.find(key) != values.end() ? values.at(key) : -1;
  return values.size() == 1 ? result : -2;
}
int main() { return lookup(2) == 20 && lookup(5) == -1 ? 0 : 1; }
)r19containers");
  const auto Output = tmpFile("ordered-map-at-mutable-ternary-does-not-insert.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-at-mutable-ternary-does-not-insert" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultimapDoublePiecewiseAndEqualKeysRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multimap-double-piecewise-and-equal-keys.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <tuple>
int main() {
  std::multimap<int, double> values;
  values.emplace(std::piecewise_construct, std::forward_as_tuple(1), std::forward_as_tuple(-2.5));
  values.emplace(1, 1.25);
  auto copy = values;
  auto first = copy.begin();
  if (copy.count(1) != 2 || first->second != -2.5)
    return 1;
  ++first;
  return first->second == 1.25 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-multimap-double-piecewise-and-equal-keys.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multimap-double-piecewise-and-equal-keys" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapDoubleComparisonAdvancesPastUnorderedPairRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-double-comparison-advances-past-unordered-pair.cpp");
  writeFile(Source, R"r19containers(#include <limits>
#include <map>
int main() {
  const auto quiet = std::numeric_limits<double>::quiet_NaN();
  const std::map<int, double> left{{1, quiet}, {2, -3.0}};
  const std::map<int, double> right{{1, 1.0}, {2, 4.0}};
  if (left == right || !(left < right) || right < left)
    return 1;
  auto copy = left;
  return copy == left ? 2 : 0;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-double-comparison-advances-past-unordered-pair.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-double-comparison-advances-past-unordered-pair" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapUnsignedLongLongDifferentPairLeafGuardRejected) {
  const auto Source = tmpFile("ordered-map-unsigned-long-long-different-pair-leaf-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, unsigned long long> values;
  return values.emplace(std::make_pair(1, 2.0)).second ? 0 : 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapEnumMappedValueGuardRejected) {
  const auto Source = tmpFile("ordered-map-enum-mapped-value-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
enum class Flag : unsigned { ready = 1 };
int main() {
  std::map<int, Flag> values;
  return values.try_emplace(1, Flag::ready).second ? 0 : 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2LimitsResourceHeaderDirectSourceIncludeIsRejectedRejected) {
  const auto Source = tmpFile("limits-resource-header-direct-source-include-is-rejected.cpp");
  writeFile(Source, R"r19containers(#include <limits.h>
int main() { return INT_MAX < 0; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapAtGuardDifferentOwnerRejected) {
  const auto Source = tmpFile("ordered-map-at-guard-different-owner.cpp");
  writeFile(Source, R"r19containers(#include <map>
int lookup(const std::map<int, int>& values, const std::map<int, int>& other, int key) {
  return values.find(key) != values.end() ? other.at(key) : -1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapConstDoubleStorageGuardRejected) {
  const auto Source = tmpFile("ordered-map-const-double-storage-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, const double> values;
  return values.try_emplace(1, 2.0).second ? 0 : 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2PairDeducedFromNumericLimitsResultRunAtBothOptimizations) {
  const auto Source = tmpFile("pair-deduced-from-numeric-limits-result.cpp");
  writeFile(Source, R"r19containers(#include <limits>
#include <utility>
int main() {
  const auto mapped = std::numeric_limits<double>::quiet_NaN();
  std::pair<const int, double> value{1, mapped};
  return value.second != value.second ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("pair-deduced-from-numeric-limits-result.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("pair-deduced-from-numeric-limits-result" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PairDeducedFromSourceFunctionResultRunAtBothOptimizations) {
  const auto Source = tmpFile("pair-deduced-from-source-function-result.cpp");
  writeFile(Source, R"r19containers(#include <utility>
double init() { return -1.25; }
int main() {
  const auto mapped = init();
  std::pair<const int, double> value{1, mapped};
  return value.second == -1.25 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("pair-deduced-from-source-function-result.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("pair-deduced-from-source-function-result" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapAtAfterAbsentKeyReturnRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-at-after-absent-key-return.cpp");
  writeFile(Source, R"r19containers(#include <map>
int lookup(const std::map<int, int>& values, int key) {
  if (values.find(key) == values.end()) return -1;
  return values.at(key);
}
int main() {
  const std::map<int, int> values{{3, 71}};
  return lookup(values, 3) == 71 && lookup(values, 4) == -1 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-at-after-absent-key-return.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-at-after-absent-key-return" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapAtPresentElseBranchRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-at-present-else-branch.cpp");
  writeFile(Source, R"r19containers(#include <map>
int lookup(const std::map<int, int>& values, int key) {
  if (values.find(key) == values.end())
    return -1;
  else
    return values.at(key);
}
int main() {
  const std::map<int, int> values{{2, 64}};
  return lookup(values, 2) == 64 && lookup(values, 3) == -1 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-at-present-else-branch.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-at-present-else-branch" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultisetGreaterHintsAndDuplicatesRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multiset-greater-hints-and-duplicates.cpp");
  writeFile(Source, R"r19containers(#include <set>
int main() {
  std::multiset<int, std::greater<int>> values{2, 3, 4};
  auto hint = values.find(3);
  values.emplace_hint(hint, 3);
  values.insert(values.cbegin(), 5);
  if (*values.begin() != 5 || values.count(3) != 2)
    return 1;
  if (values.erase(3) != 2 || values.count(3) != 0)
    return 2;
  return *values.lower_bound(3) == 2 ? 0 : 3;
}
)r19containers");
  const auto Output = tmpFile("ordered-multiset-greater-hints-and-duplicates.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multiset-greater-hints-and-duplicates" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMapLongSubscriptAssignAndCopyRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-map-long-subscript-assign-and-copy.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
int main() {
  std::unordered_map<int, long> values;
  values.try_emplace(1, -7102431L);
  values.insert_or_assign(2, 42011071L);
  values[2] += 93L;
  auto copy = values;
  values.clear();
  if (copy[1] != -7102431L || copy[2] != 42011164L || values.size() != 0)
    return 1;
  return copy[3] == 0L && copy.size() == 3 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-map-long-subscript-assign-and-copy.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-map-long-subscript-assign-and-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetTransparentLessExactSearchRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-transparent-less-exact-search.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <set>
int main() {
  std::set<int, std::less<>> values{4, 10};
  const int key = 4;
  auto found = values.find(key);
  return found != values.end() && *found == 4 && *values.lower_bound(7) == 10 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-transparent-less-exact-search.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-transparent-less-exact-search" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMultisetTransparentGreaterEqualRangeRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-multiset-transparent-greater-equal-range.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <set>
int main() {
  const std::multiset<int, std::greater<>> values{5, 3, 3, 1};
  const int key = 3;
  auto both = values.equal_range(key);
  auto next = both.first;
  ++next;
  return *both.first == 3 && *next == 3 && *both.second == 1 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("ordered-multiset-transparent-greater-equal-range.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-multiset-transparent-greater-equal-range" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapTransparentWideDescendingRangeRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-transparent-wide-descending-range.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <map>
int main() {
  const std::map<int, int, std::greater<>> values{{9, 27}, {-1, 10}, {-3, 12}};
  auto above = values.equal_range(4294967295LL);
  auto below = values.equal_range(-4294967297LL);
  auto exact = values.equal_range(-1LL);
  if (above.first != values.begin() || above.second != above.first ||
      below.first != values.end() || below.second != values.end())
    return 1;
  return values.count(4294967295LL) == 0 && values.count(-1LL) == 1 &&
      exact.first->second == 10 && exact.second->first == -3 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-transparent-wide-descending-range.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-transparent-wide-descending-range" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PairDeducedVolatileNumericLimitsGuardRejected) {
  const auto Source = tmpFile("pair-deduced-volatile-numeric-limits-guard.cpp");
  writeFile(Source, R"r19containers(#include <limits>
#include <utility>
int main() {
  volatile auto mapped = std::numeric_limits<double>::quiet_NaN();
  std::pair<const int, double> value{1, mapped};
  return value.second != value.second ? 0 : 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapAtFallthroughIsAbsentRejected) {
  const auto Source = tmpFile("ordered-map-at-fallthrough-is-absent.cpp");
  writeFile(Source, R"r19containers(#include <map>
int lookup(const std::map<int, int>& values, int key) {
  if (values.find(key) != values.end()) return -1;
  return values.at(key);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedMapGreaterOnDifferentKeyTypeGuardRejected) {
  const auto Source = tmpFile("ordered-map-greater-on-different-key-type-guard.cpp");
  writeFile(Source, R"r19containers(#include <map>
int main() {
  std::map<int, int, std::greater<long>> values;
  values.try_emplace(3, 5);
  return values.begin()->second != 5;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedMapQualifiedLongStorageGuardRejected) {
  const auto Source = tmpFile("unordered-map-qualified-long-storage-guard.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
int main() {
  std::unordered_map<int, const long> values;
  return values.try_emplace(3, 5L).second ? 0 : 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2OrderedSetTransparentUnsignedProbeGuardRejected) {
  const auto Source = tmpFile("ordered-set-transparent-unsigned-probe-guard.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <set>
int main() {
  const std::set<int, std::less<>> values;
  return values.find(8ULL) == values.end() ? 0 : 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2UnorderedMapFloatInsertionZeroAndCopyRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-map-float-insertion-zero-and-copy.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
int main() {
  std::unordered_map<int, float> first;
  first.try_emplace(4, 1.25f);
  first[4] += 2.0f;
  first.insert_or_assign(5, -0.0f);
  auto second = first;
  second[5] = 0.0f;
  if (!(first == second) || second[4] != 3.25f)
    return 1;
  return second[6] == 0.0f && second.size() == 3 && first.size() == 2 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("unordered-map-float-insertion-zero-and-copy.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-map-float-insertion-zero-and-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMapUnsignedLongLongNodeRoundTripRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-map-unsigned-long-long-node-round-trip.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
#include <utility>
int main() {
  std::unordered_map<int, unsigned long long> values;
  values.try_emplace(8, 0xfedcba9876543210ULL);
  auto node = values.extract(8);
  node.mapped() += 31ULL;
  auto result = values.insert(std::move(node));
  return result.inserted && result.position->second == 0xfedcba987654322fULL &&
      values[8] > 0x8000000000000000ULL ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-map-unsigned-long-long-node-round-trip.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-map-unsigned-long-long-node-round-trip" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedSetForwardRangeBetweenStandardOrdersRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-set-forward-range-between-standard-orders.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <set>
int main() {
  const std::set<int, std::greater<>> high{5, 2, 7};
  std::set<int, std::less<>> low{3};
  auto selected = high.find(5);
  if (selected == high.end() || *selected != 5)
    return 1;
  low.insert(high.begin(), high.end());
  auto second = low.begin();
  ++second;
  return low.size() == 4 && *low.begin() == 2 && *second == 3 && *high.begin() == 7 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-set-forward-range-between-standard-orders.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-set-forward-range-between-standard-orders" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OrderedMapSharedCursorAcrossComparatorSpecializationsRunAtBothOptimizations) {
  const auto Source = tmpFile("ordered-map-shared-cursor-across-comparator-specializations.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <iterator>
#include <map>
int main() {
  std::map<int, int, std::greater<int>> high{{1, 10}, {2, 20}, {3, 30}};
  const std::map<int, int> low{{1, 11}, {2, 21}, {3, 31}};
  auto desc = std::next(high.begin());
  auto asc = std::next(low.rbegin());
  if (desc->first != 2 || asc->first != 2)
    return 1;
  desc->second += 7;
  return high.find(2)->second == 27 && asc->second == 21 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("ordered-map-shared-cursor-across-comparator-specializations.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("ordered-map-shared-cursor-across-comparator-specializations" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedSetLongRangeInsertionWithDuplicateNegativeKeyRunAtBothOptimizations) {
  const auto Source = tmpFile("unordered-set-long-range-insertion-with-duplicate-negative-key.cpp");
  writeFile(Source, R"r19containers(#include <unordered_set>
int main() {
  const long values[] = {-19001903L, 130227L, -19001903L};
  std::unordered_set<int> set{-11};
  set.insert(values, values + 3);
  return set.size() == 3 && set.count(-19001903) == 1 && set.count(130227) == 1 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("unordered-set-long-range-insertion-with-duplicate-negative-key.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("unordered-set-long-range-insertion-with-duplicate-negative-key" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UnorderedMapLongDoubleMappedPayloadGuardRejected) {
  const auto Source = tmpFile("unordered-map-long-double-mapped-payload-guard.cpp");
  writeFile(Source, R"r19containers(#include <unordered_map>
int main() {
  std::unordered_map<int, long double> values;
  return values.try_emplace(4, 1.25L).second ? 0 : 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantFloatRelationsAndSignedZeroRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-float-relations-and-signed-zero.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<float, int>;

bool compare(const Value& lower, const Value& higher, const Value& zero,
             const Value& negativeZero, const Value& integer) {
  return lower < higher && lower <= higher && higher > lower &&
         higher >= lower && lower != higher && !(higher == lower) &&
         zero == negativeZero && !(zero != negativeZero) &&
         higher < integer && integer > higher && higher != integer;
}

int main() {
  const Value lower{-3.5f}, higher{5.25f}, zero{0.0f}, negativeZero{-0.0f}, integer{-99};
  return compare(lower, higher, zero, negativeZero, integer) ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("variant-float-relations-and-signed-zero.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-float-relations-and-signed-zero" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantComparisonLiveAndTemporaryOperandsRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-comparison-live-and-temporary-operands.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<short, int>;

bool compare(const Value& value) {
  return Value(4) < value && value >= Value(9) && value == Value(9) &&
         Value(short(90)) < value && Value(short(9)) != value;
}

int main() { return compare(Value(9)) ? 0 : 1; }
)r19containers");
  const auto Output = tmpFile("variant-comparison-live-and-temporary-operands.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-comparison-live-and-temporary-operands" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantComparisonTemporaryMonostateAndWidthRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-comparison-temporary-monostate-and-width.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<std::monostate, long long>;

bool compare(const Value& value) {
  return value > Value{} && Value{} == Value{} &&
         Value(-8589934593LL) < value && value == Value(8589934593LL);
}

int main() {
  const Value value(8589934593LL);
  return compare(value) ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("variant-comparison-temporary-monostate-and-width.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-comparison-temporary-monostate-and-width" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMonostateCopyEvaluatesInputOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-monostate-copy-evaluates-input-once.cpp");
  writeFile(Source, R"r19containers(#include <variant>
const std::monostate& read(const std::monostate& source, int& calls) {
  ++calls;
  return source;
}
int main() {
  const std::monostate source{};
  int calls = 0;
  const std::variant<int, std::monostate> value = read(source, calls);
  return calls != 1 || value.index() != 1 || value.valueless_by_exception();
}
)r19containers");
  const auto Output = tmpFile("variant-monostate-copy-evaluates-input-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-monostate-copy-evaluates-input-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMonostateEmplaceReturnsActiveAlternativeRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-monostate-emplace-returns-active-alternative.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int main() {
  std::variant<int, std::monostate> value{25};
  const std::monostate source{};
  auto& inserted = value.emplace<1>(source);
  return value.index() != 1 || &inserted != std::get_if<std::monostate>(&value);
}
)r19containers");
  const auto Output = tmpFile("variant-monostate-emplace-returns-active-alternative.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-monostate-emplace-returns-active-alternative" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantComparisonEscapingReferenceGuardRejected) {
  const auto Source = tmpFile("variant-comparison-escaping-reference-guard.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<short, int>;
const int& escape() { return std::get<1>(Value(3)); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantMonostateAssignmentFromActiveAlternativeRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-monostate-assignment-from-active-alternative.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int main() {
  using Value = std::variant<int, std::monostate>;
  Value value{std::in_place_index<1>};
  const auto* item = std::get_if<1>(&value);
  if (!item) return 1;
  value = *item;
  return value.index() != 1 || std::get_if<1>(&value) != item;
}
)r19containers");
  const auto Output = tmpFile("variant-monostate-assignment-from-active-alternative.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-monostate-assignment-from-active-alternative" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMonostateAssignmentPointerReceiverRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-monostate-assignment-pointer-receiver.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<int, std::monostate>;
Value& assign(Value* value, const std::monostate& input) {
  return value->operator=(input);
}
int main() {
  Value value{6};
  const std::monostate input{};
  return &assign(&value, input) != &value || !std::holds_alternative<std::monostate>(value);
}
)r19containers");
  const auto Output = tmpFile("variant-monostate-assignment-pointer-receiver.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-monostate-assignment-pointer-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantPointerEqualityComparesTheActiveIndexRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-pointer-equality-compares-the-active-index.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<short, const int*, const int*>;
int main() {
  const int value = 7;
  const Value a{std::in_place_index<1>, &value};
  const Value b{std::in_place_index<2>, &value};
  const Value alias{std::in_place_index<2>, &value};
  return a == b || !(a != b) || b != alias;
}
)r19containers");
  const auto Output = tmpFile("variant-pointer-equality-compares-the-active-index.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-pointer-equality-compares-the-active-index" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVoidPointerEqualityRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-void-pointer-equality.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<int, void*>;
bool compare(const Value& left, const Value& right) {
  return std::operator==(left, right) && !std::operator!=(right, left);
}
int main() {
  int first = 0, second = 0;
  const Value a{static_cast<void*>(&first)}, b{static_cast<void*>(&first)}, c{static_cast<void*>(&second)};
  return !compare(a, b) || compare(a, c);
}
)r19containers");
  const auto Output = tmpFile("variant-void-pointer-equality.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-void-pointer-equality" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantSwapSameAlternativeKeepsObjectAddressesRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-swap-same-alternative-keeps-object-addresses.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int main() {
  using Value = std::variant<int, unsigned>;
  Value left{8}, right{14};
  auto* first = std::get_if<0>(&left);
  auto* second = std::get_if<0>(&right);
  if (!first || !second) return 1;
  left.swap(right);
  return std::get_if<0>(&left) != first || std::get_if<0>(&right) != second ||
         *first != 14 || *second != 8;
}
)r19containers");
  const auto Output = tmpFile("variant-swap-same-alternative-keeps-object-addresses.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-swap-same-alternative-keeps-object-addresses" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantSwapSelfWithDuplicateAlternativesRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-swap-self-with-duplicate-alternatives.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int main() {
  using Value = std::variant<int, int>;
  Value value{std::in_place_index<1>, -18};
  auto* old = std::get_if<1>(&value);
  if (!old) return 1;
  value.swap(value);
  return value.index() != 1 || std::get_if<1>(&value) != old || *old != -18;
}
)r19containers");
  const auto Output = tmpFile("variant-swap-self-with-duplicate-alternatives.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-swap-self-with-duplicate-alternatives" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitFourNonuniformAlternativesRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-four-nonuniform-alternatives.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<short, int>;
using Wide = std::variant<int, long, long long>;
long long select_sum(long long a, long long b, long long c, long long d) {
  return a - 2*b + 3*c - 4*d;
}
int main() {
  Value a{static_cast<short>(9)}, b{-16}, c{static_cast<short>(5)};
  Wide d{4294967299LL};
  return std::visit(select_sum, a, b, c, d) != -17179869140LL;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-four-nonuniform-alternatives.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-four-nonuniform-alternatives" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitSixAtMatrixEntryLimitRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-six-at-matrix-entry-limit.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<short, int>;
int pack(int a, int b, int c, int d, int e, int f) {
  return a*100000 + b*10000 + c*1000 + d*100 + e*10 + f;
}
int main() {
  const Value a{static_cast<short>(9)}, b{2}, c{static_cast<short>(8)}, d{3},
              e{static_cast<short>(7)}, f{4};
  return std::visit(pack, a, b, c, d, e, f) != 928374;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-six-at-matrix-entry-limit.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-six-at-matrix-entry-limit" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyConstVoidPointerMutableSlotRunAtBothOptimizations) {
  const auto Source = tmpFile("any-const-void-pointer-mutable-slot.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  const int first = 41;
  const int second = 62;
  const void* initial = &first;
  std::any object{std::in_place_type<const void*>, initial};
  auto* slot = std::any_cast<const void*>(&object);
  if (!slot || *slot != initial || std::any_cast<void*>(&object)) return 1;
  *slot = &second;
  const std::any& view = object;
  const auto* read = std::any_cast<const void*>(&view);
  return read != slot || *read != &second;
}
)r19containers");
  const auto Output = tmpFile("any-const-void-pointer-mutable-slot.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-const-void-pointer-mutable-slot" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyVoidPointerAssignmentAndSwapTypeRunAtBothOptimizations) {
  const auto Source = tmpFile("any-void-pointer-assignment-and-swap-type.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  int value = 7;
  void* pointer = &value;
  std::any left;
  std::any right{nullptr};
  std::any& assigned = (left = pointer);
  if (&assigned != &left) return 1;
  left.swap(right);
  auto* got = std::any_cast<void*>(&right);
  return !got || *got != pointer || !std::any_cast<decltype(nullptr)>(&left) ||
         std::any_cast<void*>(&left) != nullptr;
}
)r19containers");
  const auto Output = tmpFile("any-void-pointer-assignment-and-swap-type.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-void-pointer-assignment-and-swap-type" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantSwapUnsupportedCustomAlternativeGuardRejected) {
  const auto Source = tmpFile("variant-swap-unsupported-custom-alternative-guard.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Payload { int n; };
void swap(Payload& lhs, Payload& rhs) noexcept {
  lhs.n += rhs.n;
}
void exchange(std::variant<int, Payload>& lhs, std::variant<int, Payload>& rhs) {
  lhs.swap(rhs);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantVisitFourAboveMatrixEntryLimitRejected) {
  const auto Source = tmpFile("variant-visit-four-above-matrix-entry-limit.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<short, int, long>;
long add(long a, long b, long c, long d) { return a + b + c + d; }
long combined(const Value& a, const Value& b, const Value& c, const Value& d) {
  return std::visit(add, a, b, c, d);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantSwapMonostateAndIntegerRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-swap-monostate-and-integer.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int main() {
  using Value = std::variant<std::monostate, int>;
  Value empty, number{33};
  empty.swap(number);
  const auto* count = std::get_if<1>(&empty);
  if (!count || *count != 33 || !std::get_if<0>(&number)) return 1;
  std::swap(empty, number);
  count = std::get_if<1>(&number);
  return !std::get_if<0>(&empty) || !count || *count != 33;
}
)r19containers");
  const auto Output = tmpFile("variant-swap-monostate-and-integer.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-swap-monostate-and-integer" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantSwapNullptrAndVoidPointerRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-swap-nullptr-and-void-pointer.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int main() {
  int data = 82;
  using Value = std::variant<void*, decltype(nullptr)>;
  Value left{std::in_place_index<0>, &data};
  Value right{std::in_place_index<1>};
  std::swap(left, right);
  auto* pointer = std::get_if<0>(&right);
  return !std::get_if<1>(&left) || !pointer || *pointer != &data;
}
)r19containers");
  const auto Output = tmpFile("variant-swap-nullptr-and-void-pointer.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-swap-nullptr-and-void-pointer" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyIntPointerMutableSlotAndCvIdentityRunAtBothOptimizations) {
  const auto Source = tmpFile("any-int-pointer-mutable-slot-and-cv-identity.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  int first = 18, second = 52;
  std::any value{&first};
  auto* slot = std::any_cast<int*>(&value);
  if (!slot || *slot != &first || std::any_cast<const int*>(&value) ||
      std::any_cast<void*>(&value))
    return 1;
  *slot = &second;
  const std::any& read = value;
  const auto* address = std::any_cast<int*>(&read);
  return address != slot || *address != &second;
}
)r19containers");
  const auto Output = tmpFile("any-int-pointer-mutable-slot-and-cv-identity.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-int-pointer-mutable-slot-and-cv-identity" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantSwapUncheckedAssociatedPointerGuardRejected) {
  const auto Source = tmpFile("variant-swap-unchecked-associated-pointer-guard.cpp");
  writeFile(Source, R"r19containers(#include <variant>
namespace payload {
struct Item { int n; };
void swap(Item*& left, Item*& right) noexcept { left = right; }
}
using Value = std::variant<int, payload::Item*>;
void exchange(Value& left, Value& right) { left.swap(right); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyUncheckedRecordPointerPayloadGuardRejected) {
  const auto Source = tmpFile("any-unchecked-record-pointer-payload-guard.cpp");
  writeFile(Source, R"r19containers(#include <any>
struct Payload { int data; };
void hold(Payload* input) { std::any value{input}; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantVisitTemporaryEvaluatesItsInitializerOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-temporary-evaluates-its-initializer-once.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<short, int>;
int combine(int first, int second, int third) {
  return first * 100 + second * 10 + third;
}
int payload(int& calls) {
  ++calls;
  return 5;
}
int main() {
  Value a{std::in_place_index<0>, static_cast<short>(2)}, b{3};
  int calls = 0;
  const auto result = std::visit(combine, a, b, Value{payload(calls)});
  return result != 235 || calls != 1;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-temporary-evaluates-its-initializer-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-temporary-evaluates-its-initializer-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitTemporaryWithMutableLiveOperandRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-temporary-with-mutable-live-operand.cpp");
  writeFile(Source, R"r19containers(#include <variant>
void remember(int& output, int value) { output = value; }
int main() {
  std::variant<int> output{1};
  using Input = std::variant<short, int>;
  std::visit(remember, output, Input{6});
  auto* result = std::get_if<0>(&output);
  return !result || *result != 6;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-temporary-with-mutable-live-operand.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-temporary-with-mutable-live-operand" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyActiveFloatReferenceIncrementsPayloadRunAtBothOptimizations) {
  const auto Source = tmpFile("any-active-float-reference-increments-payload.cpp");
  writeFile(Source, R"r19containers(#include <any>
float edit() {
  std::any value{1.5f};
  return ++std::any_cast<float&>(value);
}
int main() { return edit() != 2.5f; }
)r19containers");
  const auto Output = tmpFile("any-active-float-reference-increments-payload.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-active-float-reference-increments-payload" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyActiveUtfReferenceWritesPayloadRunAtBothOptimizations) {
  const auto Source = tmpFile("any-active-utf-reference-writes-payload.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  std::any value = u'A';
  char16_t& active = std::any_cast<char16_t&>(value);
  auto* address = &active;
  active = u'Z';
  return *address != u'Z';
}
)r19containers");
  const auto Output = tmpFile("any-active-utf-reference-writes-payload.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-active-utf-reference-writes-payload" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitTemporaryReferenceParameterGuardRejected) {
  const auto Source = tmpFile("variant-visit-temporary-reference-parameter-guard.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int read(const int& value) { return value; }
int test() { return std::visit(read, std::variant<int>{7}); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyActiveMutableReferenceAliasedResetGuardRejected) {
  const auto Source = tmpFile("any-active-mutable-reference-aliased-reset-guard.cpp");
  writeFile(Source, R"r19containers(#include <any>
int read() {
  std::any value{true};
  std::any& alias = value;
  alias.reset();
  return std::any_cast<bool&>(value);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantVisitConstReferenceResultSelectsOverloadRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-const-reference-result-selects-overload.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Visitor {
  double backing;
  const double& operator()(const double& value) const { return value; }
  const double& operator()(const int&) const { return backing; }
};
int main() {
  using Value = std::variant<double, int>;
  const Value direct{3.5}, fallback{7};
  const Visitor visitor{8.5};
  auto* active = std::get_if<0>(&direct);
  const double& selected = std::visit(visitor, direct);
  const double& other = std::visit(visitor, fallback);
  return !active || &selected != active || &other != &visitor.backing || selected != 3.5 || other != 8.5;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-const-reference-result-selects-overload.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-const-reference-result-selects-overload" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyRepeatedActiveValueAndReferenceCastsRunAtBothOptimizations) {
  const auto Source = tmpFile("any-repeated-active-value-and-reference-casts.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  std::any value{4};
  int before = std::any_cast<int>(value);
  int& slot = std::any_cast<int&>(value);
  slot = 9;
  return before != 4 || std::any_cast<int>(value) != 9;
}
)r19containers");
  const auto Output = tmpFile("any-repeated-active-value-and-reference-casts.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-repeated-active-value-and-reference-casts" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyRepeatedActiveCastInLoopRunAtBothOptimizations) {
  const auto Source = tmpFile("any-repeated-active-cast-in-loop.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  std::any value{3L};
  for (int index = 0; index < 2; ++index) ++std::any_cast<long&>(value);
  return std::any_cast<long>(value) != 5L;
}
)r19containers");
  const auto Output = tmpFile("any-repeated-active-cast-in-loop.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-repeated-active-cast-in-loop" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantTrivialAggregateInPlaceCopyPreservesMembersRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-trivial-aggregate-in-place-copy-preserves-members.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Payload { int count; double amount; };
int main() {
  Payload original{7, 4.5};
  std::variant<int, Payload> value{std::in_place_type<Payload>, original};
  original.count = 9;
  auto* active = std::get_if<1>(&value);
  return !active || active->count != 7 || active->amount != 4.5 || value.index() != 1;
}
)r19containers");
  const auto Output = tmpFile("variant-trivial-aggregate-in-place-copy-preserves-members.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-trivial-aggregate-in-place-copy-preserves-members" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitAggregateMutablePayloadRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-aggregate-mutable-payload.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Payload { int count; double weight; };
void increase(Payload& payload) { payload.count += 3; }
int main() {
  std::variant<Payload> value;
  std::visit(increase, value);
  auto* active = std::get_if<0>(&value);
  return !active || active->count != 3 || active->weight != 0.0;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-aggregate-mutable-payload.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-aggregate-mutable-payload" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitMultipleLiveAggregatePayloadsRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-multiple-live-aggregate-payloads.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Input { int amount; };
struct Output { int value; int calls; };
int put(const Input& input, Output& output) {
  ++output.calls;
  output.value += input.amount;
  return output.value;
}
int main() {
  const Input input{5};
  const std::variant<Input> left{std::in_place_index<0>, input};
  std::variant<Output> right;
  int result = std::visit(put, left, right);
  const auto* active = std::get_if<0>(&right);
  return !active || result != 5 || active->value != 5 || active->calls != 1;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-multiple-live-aggregate-payloads.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-multiple-live-aggregate-payloads" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantAggregateEmplaceCopiesPayloadRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-aggregate-emplace-copies-payload.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Payload { short code; int amount; };
int main() {
  Payload initial{5, 11};
  std::variant<int, Payload> value{8};
  Payload& active = value.emplace<1>(initial);
  initial.amount = 20;
  const auto* checked = std::get_if<Payload>(&value);
  return !checked || checked != &active || checked->code != 5 || checked->amount != 11;
}
)r19containers");
  const auto Output = tmpFile("variant-aggregate-emplace-copies-payload.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-aggregate-emplace-copies-payload" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitVolatileReferenceResultGuardRejected) {
  const auto Source = tmpFile("variant-visit-volatile-reference-result-guard.cpp");
  writeFile(Source, R"r19containers(#include <variant>
volatile int& borrow(int& value) { return value; }
volatile int& bad(std::variant<int>& value) { return std::visit(borrow, value); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyRepeatedActiveCastsWithRetaggingGuardRejected) {
  const auto Source = tmpFile("any-repeated-active-casts-with-retagging-guard.cpp");
  writeFile(Source, R"r19containers(#include <any>
int read() {
  std::any value{3};
  int before = std::any_cast<int>(value);
  value = std::any{true};
  return before + std::any_cast<int>(value);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantUncheckedBitFieldRecordAlternativeGuardRejected) {
  const auto Source = tmpFile("variant-unchecked-bit-field-record-alternative-guard.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Payload { unsigned code : 3; };
int test() { std::variant<Payload, int> value; return int(value.index()); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantVisitAggregateTemporaryReferenceGuardRejected) {
  const auto Source = tmpFile("variant-visit-aggregate-temporary-reference-guard.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Payload { int count; };
int read(const Payload& value) { return value.count; }
int test() { return std::visit(read, std::variant<Payload>{}); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantVisitVolatileAggregateReferenceGuardRejected) {
  const auto Source = tmpFile("variant-visit-volatile-aggregate-reference-guard.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Payload { int count; };
int read(volatile Payload& value) { return value.count; }
int test(std::variant<Payload>& value) { return std::visit(read, value); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantInPlaceUnsignedToWiderSignedRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-in-place-unsigned-to-wider-signed.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int main() {
  unsigned int input = 4294967295u;
  std::variant<int, long long> value{std::in_place_type<long long>, input};
  const auto* stored = std::get_if<1>(&value);
  return !stored || *stored != 4294967295LL || value.index() != 1;
}
)r19containers");
  const auto Output = tmpFile("variant-in-place-unsigned-to-wider-signed.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-in-place-unsigned-to-wider-signed" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyRvalueReferenceCastAliasesPayloadRunAtBothOptimizations) {
  const auto Source = tmpFile("any-rvalue-reference-cast-aliases-payload.cpp");
  writeFile(Source, R"r19containers(#include <any>
#include <utility>
int main() {
  std::any local{23};
  int&& payload = std::any_cast<int&&>(std::move(local));
  payload = 44;
  return std::any_cast<int>(local) != 44;
}
)r19containers");
  const auto Output = tmpFile("any-rvalue-reference-cast-aliases-payload.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-rvalue-reference-cast-aliases-payload" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantSizeofExpressionSkipsSideEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-sizeof-expression-skips-side-effects.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int main() {
  int calls = 0;
  using Value = std::variant<int, long>;
  const Value owner{17};
  return sizeof((++calls, owner)) != sizeof(Value) || calls != 0 || owner.index() != 0;
}
)r19containers");
  const auto Output = tmpFile("variant-sizeof-expression-skips-side-effects.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-sizeof-expression-skips-side-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyFundamentalPointerCopyKeepsDoubleTypeRunAtBothOptimizations) {
  const auto Source = tmpFile("any-fundamental-pointer-copy-keeps-double-type.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  double first = 2.5;
  std::any local{&first};
  std::any copied = local;
  double** pointer = std::any_cast<double*>(&copied);
  if (!pointer || *pointer != &first) return 1;
  return std::any_cast<const double*>(&copied) != nullptr ||
         std::any_cast<float*>(&copied) != nullptr;
}
)r19containers");
  const auto Output = tmpFile("any-fundamental-pointer-copy-keeps-double-type.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-fundamental-pointer-copy-keeps-double-type" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyFundamentalPointerDistinguishesUtfAndIntegersRunAtBothOptimizations) {
  const auto Source = tmpFile("any-fundamental-pointer-distinguishes-utf-and-integers.cpp");
  writeFile(Source, R"r19containers(#include <any>
int main() {
  char16_t input = u'Q';
  std::any local{&input};
  char16_t** pointer = std::any_cast<char16_t*>(&local);
  return !pointer || *pointer != &input || std::any_cast<unsigned short*>(&local) != nullptr;
}
)r19containers");
  const auto Output = tmpFile("any-fundamental-pointer-distinguishes-utf-and-integers.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-fundamental-pointer-distinguishes-utf-and-integers" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantEmplaceLossyIntegerConversionGuardRejected) {
  const auto Source = tmpFile("variant-emplace-lossy-integer-conversion-guard.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int narrow(int input) {
  std::variant<int, signed char> value{1};
  return value.emplace<1>(input);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyRvalueCastUnprovedCallerOwnedTypeGuardRejected) {
  const auto Source = tmpFile("any-rvalue-cast-unproved-caller-owned-type-guard.cpp");
  writeFile(Source, R"r19containers(#include <any>
#include <utility>
int read(std::any& value) {
  return std::any_cast<int>(std::move(value));
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedConversionThroughConstCopyAndAliasRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-unsigned-conversion-through-const-copy-and-alias.cpp");
  writeFile(Source, R"r19containers(#include <bitset>
int main() {
  using Bits = std::bitset<129>;
  const Bits initial(5);
  const Bits copied(initial);
  const Bits& alias = copied;
  return alias.to_ullong() != 5ULL;
}
)r19containers");
  const auto Output = tmpFile("bitset-unsigned-conversion-through-const-copy-and-alias.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-unsigned-conversion-through-const-copy-and-alias" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitNonowningStaticCastPreservesOwnerRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-nonowning-static-cast-preserves-owner.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<int, short>;
int read(int value) { return value; }
int main() {
  Value item(std::in_place_index<1>, short{7});
  int before = std::visit(read, static_cast<Value&&>(item));
  item.emplace<0>(11);
  return before != 7 || std::visit(read, item) != 11;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-nonowning-static-cast-preserves-owner.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-nonowning-static-cast-preserves-owner" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitMixedBoundAndXvalueOperandsRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-mixed-bound-and-xvalue-operands.cpp");
  writeFile(Source, R"r19containers(#include <utility>
#include <variant>
using Value = std::variant<int, short>;
int subtract(int left, int right) { return left - right; }
int main() {
  Value left(std::in_place_index<1>, short{17});
  Value right(std::in_place_index<0>, 5);
  return std::visit(subtract, left, std::move(right)) != 12;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-mixed-bound-and-xvalue-operands.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-mixed-bound-and-xvalue-operands" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedConversionCopiesSingleUseLocalRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-unsigned-conversion-copies-single-use-local.cpp");
  writeFile(Source, R"r19containers(#include <bitset>
int main() {
  using Bits = std::bitset<129>;
  Bits input(0x80000000ULL);
  const Bits first(input);
  const Bits second(first);
  return second.to_ulong() != 0x80000000UL;
}
)r19containers");
  const auto Output = tmpFile("bitset-unsigned-conversion-copies-single-use-local.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-unsigned-conversion-copies-single-use-local" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedConversionStaticConstantFitsUnsignedLongRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-unsigned-conversion-static-constant-fits-unsigned-long.cpp");
  writeFile(Source, R"r19containers(#include <bitset>
int main() {
  using Bits = std::bitset<129>;
  static const Bits payload(0x80000000ULL);
  return payload.to_ulong() != 0x80000000UL;
}
)r19containers");
  const auto Output = tmpFile("bitset-unsigned-conversion-static-constant-fits-unsigned-long.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-unsigned-conversion-static-constant-fits-unsigned-long" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedConversionCopyAfterMutationGuardRejected) {
  const auto Source = tmpFile("bitset-unsigned-conversion-copy-after-mutation-guard.cpp");
  writeFile(Source, R"r19containers(#include <bitset>
int main() {
  using Bits = std::bitset<129>;
  Bits input(4);
  input.set(90);
  const Bits copy = input;
  return copy.to_ullong() != 4;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedConversionDynamicStaticInitializationGuardRejected) {
  const auto Source = tmpFile("bitset-unsigned-conversion-dynamic-static-initialization-guard.cpp");
  writeFile(Source, R"r19containers(#include <bitset>
unsigned long long initial_value() { return 1; }
int main() {
  using Bits = std::bitset<129>;
  static const Bits payload(initial_value());
  return payload.to_ullong() != 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedConversionDefaultMemberInAggregateListRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-unsigned-conversion-default-member-in-aggregate-list.cpp");
  writeFile(Source, R"r19containers(#include <bitset>
struct State { const std::bitset<129> bits{0x80000000ULL}; };
int main() {
  const State state{};
  return state.bits.to_ulong() != 0x80000000UL;
}
)r19containers");
  const auto Output = tmpFile("bitset-unsigned-conversion-default-member-in-aggregate-list.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-unsigned-conversion-default-member-in-aggregate-list" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetUnsignedConversionExplicitMemberInitializerGuardRejected) {
  const auto Source = tmpFile("bitset-unsigned-conversion-explicit-member-initializer-guard.cpp");
  writeFile(Source, R"r19containers(#include <bitset>
using Bits = std::bitset<129>;
struct State { const Bits bits{2}; };
int main() {
  State state{Bits{4}};
  return state.bits.to_ullong() != 4;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantVisitConstructsTemporaryVisitorOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-constructs-temporary-visitor-once.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Add {
  int bias;
  int operator()(int value) const { return value + bias; }
};
int next_bias(int& calls) { ++calls; return calls + 3; }
int main() {
  std::variant<int, short> value(std::in_place_index<1>, short(6));
  int calls = 0;
  int output = std::visit(Add{next_bias(calls)}, value);
  return output != 10 || calls != 1;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-constructs-temporary-visitor-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-constructs-temporary-visitor-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitTemporaryVisitorVoidResultRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-temporary-visitor-void-result.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct SetValue {
  int* out;
  void operator()(int value) const { *out = value; }
};
int main() {
  std::variant<int, short> value(std::in_place_index<1>, short(9));
  int output = 0;
  std::visit(SetValue{&output}, value);
  return output != 9;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-temporary-visitor-void-result.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-temporary-visitor-void-result" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitInstantiatesSelectedVisitorOperatorRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-instantiates-selected-visitor-operator.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Measure {
  template<class T> int operator()(T value) const { return 10 * int(sizeof(T)) + int(value); }
};
int main() {
  std::variant<int, short> value(std::in_place_index<1>, short(5));
  const Measure measure{};
  return std::visit(measure, value) != 10 * int(sizeof(short)) + 5;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-instantiates-selected-visitor-operator.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-instantiates-selected-visitor-operator" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitInstantiatesVoidVisitorOperatorRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-instantiates-void-visitor-operator.cpp");
  writeFile(Source, R"r19containers(#include <variant>
struct Write {
  int* output;
  template<class T> void operator()(const T& value) const { *output = int(value); }
};
int main() {
  std::variant<int, short> value(std::in_place_index<1>, short(11));
  int output = 0;
  const Write write{&output};
  std::visit(write, value);
  return output != 11;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-instantiates-void-visitor-operator.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-instantiates-void-visitor-operator" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitCapturelessDispatchesTwoSelectedIndicesRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-captureless-dispatches-two-selected-indices.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int main() {
  using Item = std::variant<int, short>;
  const Item left(std::in_place_index<1>, short(17));
  Item right(6);
  return std::visit([](auto a, auto b) -> int { return int(a) - int(b); }, left, right) != 11;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-captureless-dispatches-two-selected-indices.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-captureless-dispatches-two-selected-indices" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitCapturelessNonparameterSourceGuardRejected) {
  const auto Source = tmpFile("variant-visit-captureless-nonparameter-source-guard.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int bias = 2;
int main() {
  std::variant<int, short> value(3);
  return std::visit([](auto x) -> int { return int(x) + bias; }, value) != 5;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantVisitSourceCastBorrowsConstOwnerRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-visit-source-cast-borrows-const-owner.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<int, short>;
const Value&& view_value(const Value& value) { return static_cast<const Value&&>(value); }
int read(int value) { return value + 2; }
int main() {
  const Value item(std::in_place_index<0>, 13);
  return std::visit(read, view_value(item)) != 15;
}
)r19containers");
  const auto Output = tmpFile("variant-visit-source-cast-borrows-const-owner.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-visit-source-cast-borrows-const-owner" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListMovePreservesNodeReferencesRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-move-preserves-node-references.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <utility>
int main() {
  std::forward_list<int> source{4, 7};
  int *first = &source.front();
  auto current = source.begin();
  ++current;
  int *second = &*current;
  std::forward_list<int> destination(std::move(source));
  if (!source.empty() || &destination.front() != first) return 1;
  auto next = destination.begin();
  ++next;
  return &*next != second || *first != 4 || *second != 7;
}
)r19containers");
  const auto Output = tmpFile("forward-list-move-preserves-node-references.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-move-preserves-node-references" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListMoveEmptyOwnerRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-move-empty-owner.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <utility>
int main() {
  std::forward_list<int> original;
  std::forward_list<int> moved(std::move(original));
  if (!original.empty() || !moved.empty()) return 1;
  moved.push_front(6);
  return moved.front() != 6 || !original.empty();
}
)r19containers");
  const auto Output = tmpFile("forward-list-move-empty-owner.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-move-empty-owner" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListMovesWithExplicitAllocator) {
  const auto Source = tmpFile("forward-list-move-allocator-overload-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <memory>
#include <utility>
int main() {
  std::forward_list<int> original{2, 4};
  std::forward_list<int> moved(std::move(original), std::allocator<int>{});
  return moved.front() != 2;
}
)r19containers");
  const auto Output = tmpFile("forward-list-move-allocator-overload-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-move-allocator-overload-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantVisitSourceCastEffectfulBodyGuardRejected) {
  const auto Source = tmpFile("variant-visit-source-cast-effectful-body-guard.cpp");
  writeFile(Source, R"r19containers(#include <variant>
using Value = std::variant<int>;
Value&& forward_value(Value& value) {
  value.emplace<0>(9);
  return static_cast<Value&&>(value);
}
int read(int value) { return value; }
int test(Value& item) { return std::visit(read, forward_value(item)); }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListMoveAssignmentRetainsNodesRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-move-assignment-retains-nodes.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <utility>

int main() {
  std::forward_list<int> input{3, 4};
  std::forward_list<int> output{9, 8};
  const int *first = &input.front();
  auto &assigned = (output = std::move(input));
  if (&assigned != &output || &output.front() != first || !input.empty())
    return 1;
  auto cursor = output.begin();
  ++cursor;
  if (*cursor != 4)
    return 2;
  input.push_front(7);
  return input.front() != 7 || output.front() != 3;
}
)r19containers");
  const auto Output = tmpFile("forward-list-move-assignment-retains-nodes.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-move-assignment-retains-nodes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListMoveAssignmentEmptySourceRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-move-assignment-empty-source.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <utility>

int main() {
  std::forward_list<int> from;
  std::forward_list<int> into{9, 10};
  into = std::move(from);
  if (!into.empty() || !from.empty())
    return 1;
  into.push_front(11);
  return into.front() != 11 || !from.empty();
}
)r19containers");
  const auto Output = tmpFile("forward-list-move-assignment-empty-source.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-move-assignment-empty-source" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListCopyEmptyIndependentOwnerRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-copy-empty-independent-owner.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> source;
  std::forward_list<int> copy(source);
  if (!source.empty() || !copy.empty()) return 1;
  source.push_front(13);
  copy.push_front(17);
  return source.front() != 13 || copy.front() != 17;
}
)r19containers");
  const auto Output = tmpFile("forward-list-copy-empty-independent-owner.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-copy-empty-independent-owner" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListConstXvalueSelectsCopyAssignment) {
  const auto Source = tmpFile("forward-list-move-assignment-const-source-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <utility>
int main() {
  const std::forward_list<int> from{3};
  std::forward_list<int> into{8};
  into = std::move(from);
  return into.front() != 3;
}
)r19containers");
  const auto Output = tmpFile("forward-list-move-assignment-const-source-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-move-assignment-const-source-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongAssignsMovedOwners) {
  const auto Source = tmpFile("forward-list-move-assignment-non-int-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <utility>
int main() {
  std::forward_list<long> from{3};
  std::forward_list<long> into;
  into = std::move(from);
  return into.front() != 3;
}
)r19containers");
  const auto Output = tmpFile("forward-list-move-assignment-non-int-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-move-assignment-non-int-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListCopySourceAllocatorGuardRejected) {
  const auto Source = tmpFile("forward-list-copy-source-allocator-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <memory>
template <class T> struct LocalAllocator : std::allocator<T> {
  using value_type = T;
  LocalAllocator() = default;
  template <class U> LocalAllocator(const LocalAllocator<U> &) {}
  template <class U> struct rebind { using other = LocalAllocator<U>; };
};
int main() {
  std::forward_list<int, LocalAllocator<int>> source{7};
  std::forward_list<int, LocalAllocator<int>> copy(source);
  return copy.front() != 7;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListCopyAssignmentTruncatesAndHandlesSelfRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-copy-assignment-truncates-and-handles-self.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> into{13, 17, 19};
  const std::forward_list<int> one{23};
  int *first = &into.front();
  const auto &alias = into;
  if (&into.operator=(alias) != &into || &into.front() != first)
    return 1;
  into = one;
  auto cursor = into.begin();
  if (&*cursor != first || *cursor != 23)
    return 2;
  ++cursor;
  if (cursor != into.end())
    return 3;
  const std::forward_list<int> empty;
  into = empty;
  if (!into.empty())
    return 4;
  into = one;
  return into.empty() || into.front() != 23;
}
)r19containers");
  const auto Output = tmpFile("forward-list-copy-assignment-truncates-and-handles-self.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-copy-assignment-truncates-and-handles-self" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListReversePreservesNodeAddressesRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-reverse-preserves-node-addresses.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> values{3, 5, 7};
  auto first = values.begin();
  auto second = first;
  ++second;
  auto third = second;
  ++third;
  values.reverse();
  auto current = values.begin();
  if (&*current != &*third || *current != 7)
    return 1;
  ++current;
  if (&*current != &*second || *current != 5)
    return 2;
  ++current;
  if (&*current != &*first || *current != 3)
    return 3;
  ++current;
  return current != values.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-reverse-preserves-node-addresses.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-reverse-preserves-node-addresses" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListMemberSwapKeepsIteratorsAndReceiverSentinelRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-member-swap-keeps-iterators-and-receiver-sentinel.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> source{3, 5};
  std::forward_list<int> target;
  auto original = source.begin();
  auto before = target.before_begin();
  auto *receiver = &target;
  receiver->swap(source);
  if (!source.empty() || &target.front() != &*original)
    return 1;
  ++before;
  if (before != original)
    return 2;
  auto &alias = target;
  target.swap(alias);
  return &target.front() != &*original || target.front() != 3;
}
)r19containers");
  const auto Output = tmpFile("forward-list-member-swap-keeps-iterators-and-receiver-sentinel.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-member-swap-keeps-iterators-and-receiver-sentinel" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongCopyAssignsOwners) {
  const auto Source = tmpFile("forward-list-copy-assignment-unapproved-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<long> from{5};
  std::forward_list<long> into{7};
  into = from;
  return into.front() != 5;
}
)r19containers");
  const auto Output = tmpFile("forward-list-copy-assignment-unapproved-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-copy-assignment-unapproved-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListFreeSwapSourceAllocatorGuardRejected) {
  const auto Source = tmpFile("forward-list-free-swap-source-allocator-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <memory>
#include <utility>
template <class T> struct LocalAllocator : std::allocator<T> {
  using value_type = T;
  LocalAllocator() = default;
  template <class U> LocalAllocator(const LocalAllocator<U> &) {}
  template <class U> struct rebind { using other = LocalAllocator<U>; };
};
int main() {
  std::forward_list<int, LocalAllocator<int>> first{5};
  std::forward_list<int, LocalAllocator<int>> second{7};
  std::swap(first, second);
  return first.front() != 7;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListSpliceWholeRvalueAppendsWithoutDestroyingSourceRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-splice-whole-rvalue-appends-without-destroying-source.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <utility>
int main() {
  std::forward_list<int> destination{3};
  std::forward_list<int> source{5, 7};
  const int *first = &source.front();
  auto *receiver = &destination;
  receiver->splice_after(destination.cbegin(), std::move(source));
  if (!source.empty() || destination.front() != 3)
    return 1;
  auto cursor = destination.begin();
  ++cursor;
  if (&*cursor != first || *cursor != 5)
    return 2;
  ++cursor;
  if (*cursor != 7)
    return 3;
  source.push_front(13);
  return source.front() != 13 || *first != 5;
}
)r19containers");
  const auto Output = tmpFile("forward-list-splice-whole-rvalue-appends-without-destroying-source.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-splice-whole-rvalue-appends-without-destroying-source" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListSpliceSingleNodeOverloadGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-splice-single-node-overload-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> source{5, 7};
  std::forward_list<int> destination{11};
  destination.splice_after(destination.before_begin(), source, source.before_begin());
  return destination.front() != 5;
}
)r19containers");
  const auto Output = tmpFile("forward-list-splice-single-node-overload-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-splice-single-node-overload-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListEraseRangePreservesPredecessorAndSuffixRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-erase-range-preserves-predecessor-and-suffix.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> values{2, 3, 5, 7, 11};
  auto first = values.begin();
  auto last = values.cbegin();
  ++last;
  ++last;
  ++last;
  const int *retained = &*last;
  auto result = values.erase_after(first, last);
  if (&*result != retained || *result != 7 || *first != 2)
    return 1;
  ++first;
  if (first != result)
    return 2;
  ++result;
  if (*result != 11)
    return 3;
  ++result;
  return result != values.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-erase-range-preserves-predecessor-and-suffix.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-erase-range-preserves-predecessor-and-suffix" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListEraseRangeWithAdjacentBoundsIsEmptyRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-erase-range-with-adjacent-bounds-is-empty.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> values{3, 5};
  auto first = values.begin();
  auto next = first;
  ++next;
  const int *oldNext = &*next;
  auto result = values.erase_after(first, next);
  return result != next || &*result != oldNext || values.front() != 3;
}
)r19containers");
  const auto Output = tmpFile("forward-list-erase-range-with-adjacent-bounds-is-empty.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-erase-range-with-adjacent-bounds-is-empty" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListSpliceSingleSameOwnerPreservesOverlapRulesRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-splice-single-same-owner-preserves-overlap-rules.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <utility>
int main() {
  std::forward_list<int> values{2, 5, 7, 11};
  auto before = values.begin();
  auto moving = before;
  ++moving;
  auto tail = moving;
  ++tail;
  ++tail;
  values.splice_after(before, values, before);
  values.splice_after(moving, values, before);
  if (*before != 2 || *moving != 5)
    return 1;
  values.splice_after(tail, std::move(values), before);
  auto after = before;
  ++after;
  if (*after != 7)
    return 2;
  ++tail;
  if (tail != moving || *tail != 5)
    return 3;
  ++tail;
  return tail != values.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-splice-single-same-owner-preserves-overlap-rules.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-splice-single-same-owner-preserves-overlap-rules" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListEmplaceFrontDefaultReturnsStoredIntRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-emplace-front-default-returns-stored-int.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> values;
  int &front = values.emplace_front();
  if (&front != &values.front() || front != 0)
    return 1;
  front = 5;
  return values.front() != 5;
}
)r19containers");
  const auto Output = tmpFile("forward-list-emplace-front-default-returns-stored-int.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-emplace-front-default-returns-stored-int" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListEmplaceAfterZeroAndRvalueReturnExactCursorRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-emplace-after-zero-and-rvalue-return-exact-cursor.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> values{17};
  auto zero = values.emplace_after(values.cbefore_begin());
  if (zero != values.begin() || *zero != 0)
    return 1;
  auto first = values.emplace_after(zero, 3 + 4);
  if (*first != 7)
    return 2;
  *first = 11;
  ++zero;
  if (zero != first || *first != 11)
    return 3;
  ++first;
  if (*first != 17)
    return 4;
  ++first;
  return first != values.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-emplace-after-zero-and-rvalue-return-exact-cursor.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-emplace-after-zero-and-rvalue-return-exact-cursor" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongErasesRanges) {
  const auto Source = tmpFile("forward-list-erase-range-other-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<long> values{2L, 3L};
  auto result = values.erase_after(values.cbefore_begin(), values.cend());
  return result != values.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-erase-range-other-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-erase-range-other-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongEmplacesValues) {
  const auto Source = tmpFile("forward-list-emplace-other-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<long> values;
  return values.emplace_front(7L) != 7L;
}
)r19containers");
  const auto Output = tmpFile("forward-list-emplace-other-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-emplace-other-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListEraseRangeSourceAllocatorGuardRejected) {
  const auto Source = tmpFile("forward-list-erase-range-source-allocator-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <memory>
template <class T> struct LocalAllocator : std::allocator<T> {
  using value_type = T;
  LocalAllocator() = default;
  template <class U> LocalAllocator(const LocalAllocator<U> &) {}
  template <class U> struct rebind { using other = LocalAllocator<U>; };
};
int main() {
  std::forward_list<int, LocalAllocator<int>> values{2, 3};
  auto result = values.erase_after(values.cbefore_begin(), values.cend());
  return result != values.end();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListEmplaceUserConversionGuardRejected) {
  const auto Source = tmpFile("forward-list-emplace-user-conversion-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Value { operator int() const { return 9; } };
int main() {
  std::forward_list<int> values;
  auto result = values.emplace_after(values.cbefore_begin(), Value{});
  return *result != 9;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListInitializerAssignCopiesNamedCarrierRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-initializer-assign-copies-named-carrier.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <initializer_list>
int main() {
  const std::initializer_list<int> source{3, 5, 7};
  std::forward_list<int> into;
  into.assign(source);
  if (into.empty() || into.front() != 3)
    return 1;
  int *first = &into.front();
  into.assign({11, 13});
  auto cursor = into.begin();
  if (&*cursor != first || *cursor != 11 || *++cursor != 13 || ++cursor != into.end())
    return 2;
  if (*source.begin() != 3)
    return 3;
  into.assign({});
  return !into.empty();
}
)r19containers");
  const auto Output = tmpFile("forward-list-initializer-assign-copies-named-carrier.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-initializer-assign-copies-named-carrier" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListAssignCountGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-assign-count-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> list;
  list.assign(3, 5);
  return list.front() != 5;
}
)r19containers");
  const auto Output = tmpFile("forward-list-assign-count-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-assign-count-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListCountAssignmentReadsAliasedValueBeforeErasureRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-count-assignment-reads-aliased-value-before-erasure.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> list{3, 5, 7};
  int *first = &list.front();
  auto tail = list.begin();
  ++tail;
  ++tail;
  list.assign(2, *tail);
  auto cursor = list.begin();
  if (&*cursor != first || *cursor != 7 || *++cursor != 7 || ++cursor != list.end())
    return 1;
  list.assign(4, list.front());
  cursor = list.begin();
  for (int index = 0; index != 4; ++index, ++cursor)
    if (cursor == list.end() || *cursor != 7)
      return 2;
  if (cursor != list.end() || &list.front() != first)
    return 3;
  list.assign(0, list.front());
  return !list.empty();
}
)r19containers");
  const auto Output = tmpFile("forward-list-count-assignment-reads-aliased-value-before-erasure.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-count-assignment-reads-aliased-value-before-erasure" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListInsertCountReturnsLastOrPositionRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-insert-count-returns-last-or-position.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> list{3, 7};
  auto position = list.begin();
  auto old_tail = position;
  ++old_tail;
  int *head = &list.front();
  const int &value = *head;
  auto last = list.insert_after(position, 3, value);
  auto cursor = position;
  if (&list.front() != head || *++cursor != 3 || *++cursor != 3 || ++cursor != last || *last != 3)
    return 1;
  if (++cursor != old_tail || *old_tail != 7)
    return 2;
  return list.insert_after(position, 0, value) != position;
}
)r19containers");
  const auto Output = tmpFile("forward-list-insert-count-returns-last-or-position.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-insert-count-returns-last-or-position" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListInsertPointerRangeReturnsExactIteratorRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-insert-pointer-range-returns-exact-iterator.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> list{7};
  auto head = list.begin();
  int values[]{11, 13, 17, 19};
  const int *first = values + 1;
  auto last = list.insert_after(head, first, first + 2);
  auto cursor = head;
  if (*++cursor != 13 || ++cursor != last || *last != 17)
    return 1;
  if (list.insert_after(last, values, values) != last)
    return 2;
  return ++cursor != list.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-insert-pointer-range-returns-exact-iterator.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-insert-pointer-range-returns-exact-iterator" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListResizeFillPreservesExistingValuesRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-resize-fill-preserves-existing-values.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> list{3, 5};
  int *first = &list.front();
  auto held = list.begin();
  ++held;
  const int &fill = *held;
  list.resize(4, fill);
  auto cursor = list.begin();
  if (&*cursor != first || *cursor != 3 || ++cursor != held)
    return 1;
  if (*++cursor != 5 || *++cursor != 5 || ++cursor != list.end())
    return 2;
  list.resize(1, fill);
  cursor = list.begin();
  return &*cursor != first || *cursor != 3 || ++cursor != list.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-resize-fill-preserves-existing-values.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-resize-fill-preserves-existing-values" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListRemoveAcceptsAnAliasedValueRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-remove-accepts-an-aliased-value.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> list{5, 7, 5, 5, 11, 5};
  const int &removed = list.front();
  auto first_held = list.begin();
  ++first_held;
  auto last_held = first_held;
  ++last_held;
  ++last_held;
  ++last_held;
  list.remove(removed);
  auto cursor = list.begin();
  if (cursor != first_held || *cursor != 7 || ++cursor != last_held || *cursor != 11)
    return 1;
  return ++cursor != list.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-remove-accepts-an-aliased-value.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-remove-accepts-an-aliased-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListFilterAllNoneAndEmptyRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-filter-all-none-and-empty.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> list;
  list.remove(5);
  list.unique();
  list.push_front(7);
  list.push_front(7);
  int *first = &list.front();
  list.remove(3);
  if (&list.front() != first)
    return 1;
  list.unique();
  auto cursor = list.begin();
  if (&*cursor != first || *cursor != 7 || ++cursor != list.end())
    return 2;
  list.remove(7);
  return !list.empty();
}
)r19containers");
  const auto Output = tmpFile("forward-list-filter-all-none-and-empty.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-filter-all-none-and-empty" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListInsertsFromCheckedIteratorRange) {
  const auto Source = tmpFile("forward-list-insert-range-other-iterator-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> source{3, 5};
  std::forward_list<int> list;
  list.insert_after(list.before_begin(), source.begin(), source.end());
  return list.front() != 3;
}
)r19containers");
  const auto Output = tmpFile("forward-list-insert-range-other-iterator-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-insert-range-other-iterator-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListKeepsOneWithFunctionPredicate) {
  const auto Source = tmpFile("forward-list-unique-custom-predicate-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
bool equal(int left, int right) { return left == right; }
int main() {
  std::forward_list<int> list{3, 3};
  list.unique(equal);
  return list.front() != 3;
}
)r19containers");
  const auto Output = tmpFile("forward-list-unique-custom-predicate-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-unique-custom-predicate-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongInsertsRepeatedValues) {
  const auto Source = tmpFile("forward-list-insert-count-other-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<long> list;
  list.insert_after(list.before_begin(), 3, 5L);
  return list.front() != 5L;
}
)r19containers");
  const auto Output = tmpFile("forward-list-insert-count-other-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-insert-count-other-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongResizesWithValues) {
  const auto Source = tmpFile("forward-list-resize-fill-other-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<long> list;
  list.resize(2, 3L);
  return list.front() != 3L;
}
)r19containers");
  const auto Output = tmpFile("forward-list-resize-fill-other-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-resize-fill-other-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListAssignsConvertedPointerValues) {
  const auto Source = tmpFile("forward-list-assign-converted-pointer-elements-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  long values[]{3, 5};
  std::forward_list<int> list;
  list.assign(values, values + 2);
  return list.front() != 3;
}
)r19containers");
  const auto Output = tmpFile("forward-list-assign-converted-pointer-elements-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-assign-converted-pointer-elements-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListResizeIndirectMemberGuardRejected) {
  const auto Source = tmpFile("forward-list-resize-indirect-member-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
using List = std::forward_list<int>;
int main() {
  List list;
  auto resize = static_cast<void (List::*)(List::size_type)>(&List::resize);
  (list.*resize)(2);
  return list.front() != 0;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListDefaultMergeRvalueAndSelfRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-default-merge-rvalue-and-self.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <utility>
int main() {
  std::forward_list<int> left;
  std::forward_list<int> right{3, 7};
  auto before = right.begin();
  int *node = &*before;
  left.merge(std::move(right));
  if (!right.empty() || left.begin() != before || &left.front() != node)
    return 1;
  auto &alias = left;
  left.merge(alias);
  if (&left.front() != node)
    return 2;
  right.push_front(11);
  return *before != 3 || right.front() != 11;
}
)r19containers");
  const auto Output = tmpFile("forward-list-default-merge-rvalue-and-self.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-default-merge-rvalue-and-self" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListDefaultSortPreservesDuplicateOrderAndAddressesRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-default-sort-preserves-duplicate-order-and-addresses.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> list{4, 2, 4, 1, 2, 4, 3};
  auto four1 = list.begin();
  auto two1 = four1;
  ++two1;
  auto four2 = two1;
  ++four2;
  auto one = four2;
  ++one;
  auto two2 = one;
  ++two2;
  auto four3 = two2;
  ++four3;
  auto three = four3;
  ++three;
  list.sort();
  auto cursor = list.begin();
  if (cursor != one || *cursor != 1 || ++cursor != two1 || *cursor != 2)
    return 1;
  if (++cursor != two2 || *cursor != 2 || ++cursor != three || *cursor != 3)
    return 2;
  if (++cursor != four1 || *cursor != 4 || ++cursor != four2 || *cursor != 4)
    return 3;
  return ++cursor != four3 || *cursor != 4 || ++cursor != list.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-default-sort-preserves-duplicate-order-and-addresses.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-default-sort-preserves-duplicate-order-and-addresses" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListDefaultSortEmptySingletonAndSortedRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-default-sort-empty-singleton-and-sorted.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> list;
  list.sort();
  if (!list.empty())
    return 1;
  list.push_front(7);
  int *address = &list.front();
  list.sort();
  if (&list.front() != address)
    return 2;
  list.push_front(3);
  auto first = list.begin();
  list.sort();
  list.sort();
  auto next = list.begin();
  return next != first || *next != 3 || &*++next != address || ++next != list.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-default-sort-empty-singleton-and-sorted.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-default-sort-empty-singleton-and-sorted" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListMergesWithFunctionComparison) {
  const auto Source = tmpFile("forward-list-merge-custom-comparator-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
bool less(int a, int b) { return a < b; }
int main() {
  std::forward_list<int> left{1};
  std::forward_list<int> right{2};
  left.merge(right, less);
  return left.front() != 1;
}
)r19containers");
  const auto Output = tmpFile("forward-list-merge-custom-comparator-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-merge-custom-comparator-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongMergesNodes) {
  const auto Source = tmpFile("forward-list-merge-other-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<long> left{1L};
  std::forward_list<long> right{2L};
  left.merge(right);
  return left.front() != 1L;
}
)r19containers");
  const auto Output = tmpFile("forward-list-merge-other-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-merge-other-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListSortsWithAnEmptyComparator) {
  const auto Source = tmpFile("forward-list-sort-custom-object-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Greater {
  bool operator()(int left, int right) const { return left > right; }
};
int main() {
  std::forward_list<int> list{1, 2};
  list.sort(Greater{});
  return list.front() != 2;
}
)r19containers");
  const auto Output = tmpFile("forward-list-sort-custom-object-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-sort-custom-object-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListFillConstructionCopiesValueRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-fill-construction-copies-value.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  int value = 7;
  std::forward_list<int> list(3, value);
  std::forward_list<int> empty(0, value);
  value = 13;
  auto cursor = list.begin();
  for (int index = 0; index != 3; ++index, ++cursor)
    if (cursor == list.end() || *cursor != 7)
      return 1;
  return cursor != list.end() || !empty.empty() || value != 13;
}
)r19containers");
  const auto Output = tmpFile("forward-list-fill-construction-copies-value.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-fill-construction-copies-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListPointerConstructionGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-pointer-construction-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  const int values[]{3, 5};
  std::forward_list<int> list(values, values + 2);
  return list.front() != 3;
}
)r19containers");
  const auto Output = tmpFile("forward-list-pointer-construction-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-pointer-construction-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListPointerConstructionWithDefaultAllocatorRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-pointer-construction-with-default-allocator.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <memory>
int main() {
  const int source[]{3, 5, 7, 11};
  const std::allocator<int> allocator;
  std::forward_list<int> list(source, source + 4, allocator);
  auto cursor = list.begin();
  for (int index = 0; index != 4; ++index, ++cursor)
    if (cursor == list.end() || *cursor != source[index] || &*cursor == source + index)
      return 1;
  return cursor != list.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-pointer-construction-with-default-allocator.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-pointer-construction-with-default-allocator" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListConstructsFromCheckedIteratorRange) {
  const auto Source = tmpFile("forward-list-iterator-construction-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> source{3, 5};
  std::forward_list<int> list(source.begin(), source.end());
  return list.front() != 3;
}
)r19containers");
  const auto Output = tmpFile("forward-list-iterator-construction-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-iterator-construction-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongConstructsRepeatedValues) {
  const auto Source = tmpFile("forward-list-fill-construction-other-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<long> list(2, 7L);
  return list.front() != 7L;
}
)r19containers");
  const auto Output = tmpFile("forward-list-fill-construction-other-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-fill-construction-other-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListEqualityUsesValuesRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-equality-uses-values.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  const std::forward_list<int> one{3, 5, -7};
  std::forward_list<int> two{3, 5, -7};
  if (&one.front() == &two.front() || !(one == two) || one != two)
    return 1;
  ++two.front();
  return one == two || !(one != two);
}
)r19containers");
  const auto Output = tmpFile("forward-list-equality-uses-values.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-equality-uses-values" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListEqualityEvaluatesBothOperandsOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-equality-evaluates-both-operands-once.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
using List = std::forward_list<int>;
const List &observe(const List &list, int &count) {
  ++count;
  return list;
}
int main() {
  List left{3, 5}, right{3, 5};
  int a = 0, b = 0;
  return !(observe(left, a) == observe(right, b)) || a != 1 || b != 1;
}
)r19containers");
  const auto Output = tmpFile("forward-list-equality-evaluates-both-operands-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-equality-evaluates-both-operands-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListOrderUsesTheFirstSignedDifferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-order-uses-the-first-signed-difference.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  const std::forward_list<int> left{3, -5, 11};
  const std::forward_list<int> right{3, 5, -11};
  return !(left < right) || right < left || !(right > left) ||
      !(left <= right) || !(right >= left) || left >= right || right <= left;
}
)r19containers");
  const auto Output = tmpFile("forward-list-order-uses-the-first-signed-difference.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-order-uses-the-first-signed-difference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListOrderAgreesForEqualIndependentOwnersRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-order-agrees-for-equal-independent-owners.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  const std::forward_list<int> left{2, 3, 5};
  const std::forward_list<int> right{2, 3, 5};
  return &left.front() == &right.front() || left < right || left > right ||
      !(left <= right) || !(left >= right) || !(left == right);
}
)r19containers");
  const auto Output = tmpFile("forward-list-order-agrees-for-equal-independent-owners.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-order-agrees-for-equal-independent-owners" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListMaxSizeMatchesPinnedNodeCapacityRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-max-size-matches-pinned-node-capacity.cpp");
  writeFile(Source, R"r19containers(#include <cstddef>
#include <forward_list>
int main() {
  std::forward_list<int> empty;
  const std::forward_list<int> populated{3, 5};
  const std::size_t capacity = static_cast<std::size_t>(-1) / 16;
  return empty.max_size() != capacity || populated.max_size() != capacity;
}
)r19containers");
  const auto Output = tmpFile("forward-list-max-size-matches-pinned-node-capacity.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-max-size-matches-pinned-node-capacity" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListAllocatorMoveStealsTheOriginalChainRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-allocator-move-steals-the-original-chain.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <memory>
#include <utility>
int main() {
  std::forward_list<int> source{3, 5, 7};
  auto first = source.begin();
  const std::allocator<int> allocator;
  std::forward_list<int> target(std::move(source), allocator);
  if (!source.empty() || target.begin() != first || *++first != 5)
    return 1;
  source.push_front(11);
  return source.front() != 11 || target.front() != 3 || *++first != 7;
}
)r19containers");
  const auto Output = tmpFile("forward-list-allocator-move-steals-the-original-chain.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-allocator-move-steals-the-original-chain" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListIteratorConstructionStopsAtInteriorBoundRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-iterator-construction-stops-at-interior-bound.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> from{2, 3, 5, 7, 11};
  auto first = from.begin();
  ++first;
  auto last = first;
  ++last;
  ++last;
  std::forward_list<int> into(first, last);
  if (into.empty() || &into.front() == &*first)
    return 1;
  *first = 13;
  auto cursor = into.begin();
  if (*cursor != 3 || *++cursor != 5)
    return 2;
  return ++cursor != into.end() || *last != 7;
}
)r19containers");
  const auto Output = tmpFile("forward-list-iterator-construction-stops-at-interior-bound.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-iterator-construction-stops-at-interior-bound" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListIteratorConstructionAcceptsEmptyBoundsRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-iterator-construction-accepts-empty-bounds.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<int> from{3};
  auto cursor = from.begin();
  std::forward_list<int> first(cursor, cursor);
  std::forward_list<int> second(from.cend(), from.cend());
  return !first.empty() || !second.empty() || from.front() != 3;
}
)r19containers");
  const auto Output = tmpFile("forward-list-iterator-construction-accepts-empty-bounds.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-iterator-construction-accepts-empty-bounds" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListIteratorInsertionReturnsFinalNodeAndPositionRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-iterator-insertion-returns-final-node-and-position.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  const std::forward_list<int> from{2, 3, 5, 7};
  std::forward_list<int> into{11, 13};
  auto first = from.cbegin();
  ++first;
  auto end = first;
  ++end;
  ++end;
  auto position = into.begin();
  if (into.insert_after(position, first, first) != position)
    return 1;
  auto final = into.insert_after(position, first, end);
  auto cursor = position;
  if (*++cursor != 3 || &*cursor == &*first || *++cursor != 5 || cursor != final)
    return 2;
  return *++cursor != 13 || ++cursor != into.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-iterator-insertion-returns-final-node-and-position.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-iterator-insertion-returns-final-node-and-position" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongComparesOrder) {
  const auto Source = tmpFile("forward-list-order-other-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<long> left{3}, right{5};
  return !(left < right);
}
)r19containers");
  const auto Output = tmpFile("forward-list-order-other-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-order-other-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongReportsMaxSize) {
  const auto Source = tmpFile("forward-list-max-size-other-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  const std::forward_list<long> list;
  return list.max_size() < 2;
}
)r19containers");
  const auto Output = tmpFile("forward-list-max-size-other-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-max-size-other-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongMovesWithAllocators) {
  const auto Source = tmpFile("forward-list-allocator-move-other-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <memory>
#include <utility>
int main() {
  std::forward_list<long> source{3};
  std::forward_list<long> target(std::move(source), std::allocator<long>{});
  return target.front() != 3;
}
)r19containers");
  const auto Output = tmpFile("forward-list-allocator-move-other-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-allocator-move-other-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListIndirectOrderGuardRejected) {
  const auto Source = tmpFile("forward-list-indirect-order-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <memory>
int main() {
  using List = std::forward_list<int>;
  const List left{3}, right{5};
  auto less = static_cast<bool (*)(const List &, const List &)>(&std::operator< <int, std::allocator<int>>);
  return !less(left, right);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListIteratorConstructionForeignIteratorGuardRejected) {
  const auto Source = tmpFile("forward-list-iterator-construction-foreign-iterator-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <list>
int main() {
  std::list<int> from{3, 5};
  std::forward_list<int> into(from.begin(), from.end());
  return into.front() != 3;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListIteratorAssignmentForeignOwnerGuardRejected) {
  const auto Source = tmpFile("forward-list-iterator-assignment-foreign-owner-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <list>
int main() {
  std::list<int> from{3, 5};
  std::forward_list<int> into;
  into.assign(from.begin(), from.end());
  return into.front() != 3;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListOrderFunctionKeepsStabilityAndNodesRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-order-function-keeps-stability-and-nodes.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
bool descending(int a, int b) { return a > b; }
int main() {
  std::forward_list<int> left{1, 7, 3, 3}, right{5, 3};
  auto first_three = left.begin();
  ++first_three;
  ++first_three;
  auto next_three = first_three;
  ++next_three;
  auto other_three = right.begin();
  ++other_three;
  left.sort(&descending);
  left.merge(right, descending);
  auto it = left.begin();
  if (!right.empty() || *it != 7 || *++it != 5 || ++it != first_three)
    return 1;
  if (++it != next_three || ++it != other_three || *++it != 1)
    return 2;
  return ++it != left.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-order-function-keeps-stability-and-nodes.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-order-function-keeps-stability-and-nodes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListUniquePredicateKeepsFirstAndComparesAdjacentRunsRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-unique-predicate-keeps-first-and-compares-adjacent-runs.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int trace;
int calls;
bool same_parity(int first, int next) {
  trace = 100 * trace + 10 * first + next;
  ++calls;
  return first % 2 == next % 2;
}
int main() {
  std::forward_list<int> values{2, 4, 5, 7, 6};
  auto first = values.begin();
  auto second = first;
  ++second;
  ++second;
  auto third = second;
  ++third;
  ++third;
  values.unique(same_parity);
  if (calls != 4 || trace != 24255756)
    return 1;
  auto it = values.begin();
  return it != first || *it != 2 || ++it != second || *it != 5 ||
         ++it != third || *it != 6 || ++it != values.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-unique-predicate-keeps-first-and-compares-adjacent-runs.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-unique-predicate-keeps-first-and-compares-adjacent-runs" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListMergeOtherFunctionSignatureGuardRejected) {
  const auto Source = tmpFile("forward-list-merge-other-function-signature-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
bool compare(long a, long b) { return a < b; }
int main() {
  std::forward_list<int> left{1, 7}, right{3, 5};
  left.merge(right, compare);
  return !right.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListRemovePredicateObjectGuardRejected) {
  const auto Source = tmpFile("forward-list-remove-predicate-object-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Matches {
  int value;
  bool operator()(int input) const { return value == input; }
};
int main() {
  std::forward_list<int> values{2, 3, 5};
  values.remove_if(Matches{2});
  return values.front() != 3;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListLongAssignAndInsertExactRangesRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-long-assign-and-insert-exact-ranges.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  const long low = sizeof(long) == 8 ? static_cast<long>(-4294967297LL) : -17L;
  const long high = sizeof(long) == 8 ? static_cast<long>(4294967305LL) : 19L;
  const long data[] = {high, 0L, low};
  std::forward_list<long> from(data, data + 3);
  std::forward_list<long> into(2, 7L);
  auto original = &into.front();
  into.assign(from.cbegin(), from.cend());
  if (&into.front() != original || into.front() != high)
    return 1;
  auto inserted = into.insert_after(into.cbefore_begin(), data + 1, data + 3);
  auto iter = into.cbegin();
  if (*iter++ != 0L || *iter != low || &*iter != &*inserted)
    return 2;
  if (*++iter != high || *++iter != 0L || *++iter != low || ++iter != into.cend())
    return 3;
  return from.front() != high;
}
)r19containers");
  const auto Output = tmpFile("forward-list-long-assign-and-insert-exact-ranges.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-long-assign-and-insert-exact-ranges" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListConstructsLongsFromIntPointers) {
  const auto Source = tmpFile("forward-list-long-converting-pointer-range-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  int values[] = {1, 2, 3};
  std::forward_list<long> result(values, values + 3);
  return result.front() != 1L;
}
)r19containers");
  const auto Output = tmpFile("forward-list-long-converting-pointer-range-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-long-converting-pointer-range-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongSourceAllocatorGuardRejected) {
  const auto Source = tmpFile("forward-list-long-source-allocator-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <memory>
template <class T> struct LocalAllocator : std::allocator<T> {
  using value_type = T;
  template <class U> struct rebind { using other = LocalAllocator<U>; };
};
int main() {
  std::forward_list<long, LocalAllocator<long>> values{3L};
  return values.front() != 3L;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListLongTypedDescendingSortRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-long-typed-descending-sort.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
bool descending(long left, long right) { return left > right; }
int main() {
  const long high = sizeof(long) == 8 ? static_cast<long>(4294967301LL) : 21L;
  std::forward_list<long> list{1L, -3L, high, high};
  auto first_high = list.begin();
  ++first_high;
  ++first_high;
  list.sort(descending);
  auto item = list.begin();
  if (item != first_high || *item++ != high || *item++ != high)
    return 1;
  return *item++ != 1L || *item++ != -3L || item != list.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-long-typed-descending-sort.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-long-typed-descending-sort" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongTypedRemovePredicateRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-long-typed-remove-predicate.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
bool above(long v) { return v > 10L; }
int main() {
  const long high = sizeof(long) == 8 ? static_cast<long>(4294967301LL) : 21L;
  std::forward_list<long> list{1L, high, high, -3L};
  auto first = list.begin();
  bool (*predicate)(long) = above;
  list.remove_if(predicate);
  auto item = list.begin();
  return item != first || *item++ != 1L || *item++ != -3L || item != list.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-long-typed-remove-predicate.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-long-typed-remove-predicate" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListIntWideningOrderFunctionGuardRejected) {
  const auto Source = tmpFile("forward-list-int-widening-order-function-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
bool less(long left, long right) { return left < right; }
int main() {
  std::forward_list<int> list{2, 1};
  list.sort(&less);
  return list.front() != 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListConstructFromSignedShortRangeRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-construct-from-signed-short-range.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  const short source[] = {-300, 12, 123, -22};
  const std::forward_list<long> list(source + 1, source + 4);
  auto item = list.begin();
  return *item++ != 12L || *item++ != 123L || *item++ != -22L || item != list.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-construct-from-signed-short-range.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-construct-from-signed-short-range" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListInsertConvertingConstLongRangeRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-insert-converting-const-long-range.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  const long source[] = {-31L, 3L, 81L, -99L};
  std::forward_list<int> list{5, 6};
  auto result = list.insert_after(list.cbegin(), source, source + 3);
  auto item = list.begin();
  if (*item++ != 5 || *item++ != -31 || *item++ != 3 || &*item != &*result)
    return 1;
  return *item++ != 81 || *item++ != 6 || item != list.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-insert-converting-const-long-range.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-insert-converting-const-long-range" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListConvertingFloatingPointerRangeGuardRejected) {
  const auto Source = tmpFile("forward-list-converting-floating-pointer-range-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  float source[] = {1.25f, 2.75f};
  std::forward_list<int> list(source, source + 2);
  return list.front() != 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListLongEmplaceFrontPreservesSourceEvaluationRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-long-emplace-front-preserves-source-evaluation.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int calls = 0;
int next() { ++calls; return -289; }
int main() {
  std::forward_list<long> list{9L};
  long &result = list.emplace_front(next());
  if (calls != 1 || &result != &list.front() || result != -289L)
    return 1;
  result = 700L;
  auto item = list.cbegin();
  return *item++ != 700L || *item++ != 9L || item != list.cend();
}
)r19containers");
  const auto Output = tmpFile("forward-list-long-emplace-front-preserves-source-evaluation.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-long-emplace-front-preserves-source-evaluation" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListEmplaceVolatileShortGuardRejected) {
  const auto Source = tmpFile("forward-list-emplace-volatile-short-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  volatile short value = 1;
  std::forward_list<long> list;
  list.emplace_front(value);
  return list.front() != 1L;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListEmptyLongOrderObjectMergesEqualNodesRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-empty-long-order-object-merges-equal-nodes.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <utility>
struct Ascending { bool operator()(long left, long right) const { return right > left; } };
int main() {
  const long low = sizeof(long) == 8 ? static_cast<long>(-4294967289LL) : -17L;
  std::forward_list<long> left{low, 9L};
  std::forward_list<long> right{low, 12L};
  auto left_node = left.begin();
  auto right_node = right.begin();
  left.merge(std::move(right), Ascending{});
  auto item = left.begin();
  if (!right.empty() || item != left_node || *item++ != low)
    return 1;
  if (item != right_node || *item++ != low)
    return 2;
  return *item++ != 9L || *item++ != 12L || item != left.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-empty-long-order-object-merges-equal-nodes.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-empty-long-order-object-merges-equal-nodes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListOrderObjectEffectGuardRejected) {
  const auto Source = tmpFile("forward-list-order-object-effect-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int calls = 0;
struct Order {
  bool operator()(int a, int b) const { ++calls; return a < b; }
};
int main() {
  std::forward_list<int> values{2, 1};
  values.sort(Order{});
  return calls == 0 || values.front() != 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListUnsignedLongMergeAndCopyRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-unsigned-long-merge-and-copy.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <utility>
int main() {
  constexpr unsigned long max = static_cast<unsigned long>(-1);
  constexpr unsigned long high = max / 2 + 1;
  std::forward_list<unsigned long> left{1UL, high};
  std::forward_list<unsigned long> right{high, max};
  auto left_high = left.begin();
  ++left_high;
  auto right_high = right.begin();
  left.merge(std::move(right));
  auto item = left.begin();
  if (!right.empty() || *item++ != 1UL || item != left_high || *item++ != high)
    return 1;
  if (item != right_high || *item++ != high || *item++ != max || item != left.end())
    return 2;
  auto copy = left;
  left.front() = 3UL;
  right.push_front(7UL);
  return copy.front() != 1UL || left.front() != 3UL || right.front() != 7UL;
}
)r19containers");
  const auto Output = tmpFile("forward-list-unsigned-long-merge-and-copy.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-unsigned-long-merge-and-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListSignedOwnerFromUnsignedPointerRangeRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-signed-owner-from-unsigned-pointer-range.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  const unsigned long inputs[] = {12UL, 2147483666UL, 19UL, 24UL};
  std::forward_list<int> list{6, 7};
  const auto tail = list.insert_after(list.cbegin(), inputs + 1, inputs + 3);
  auto item = list.cbegin();
  if (*item++ != 6 || *item++ != static_cast<int>(inputs[1]) || item != tail)
    return 1;
  return *item++ != 19 || *item++ != 7 || item != list.cend();
}
)r19containers");
  const auto Output = tmpFile("forward-list-signed-owner-from-unsigned-pointer-range.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-signed-owner-from-unsigned-pointer-range" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListReverseRetainsIteratorsReferencesAndSizeRunAtBothOptimizations) {
  const auto Source = tmpFile("list-reverse-retains-iterators-references-and-size.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<int> values{11, 5, 17, -2};
  auto first = values.begin();
  auto middle = first;
  ++middle;
  int* address = &*middle;
  auto end = values.end();
  values.reverse();
  auto item = values.begin();
  if (values.size() != 4 || values.end() != end || values.front() != -2 || values.back() != 11)
    return 1;
  if (*item++ != -2 || *item++ != 17 || item != middle || &*item != address)
    return 2;
  *address = 31;
  if (*item++ != 31 || item != first || *item++ != 11 || item != end)
    return 3;
  return *--end != 11;
}
)r19containers");
  const auto Output = tmpFile("list-reverse-retains-iterators-references-and-size.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-reverse-retains-iterators-references-and-size" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListReverseEvaluatesPointerReceiverOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("list-reverse-evaluates-pointer-receiver-once.cpp");
  writeFile(Source, R"r19containers(#include <list>
std::list<int>* lookup(std::list<int>& values, int& calls) {
  ++calls;
  return &values;
}
int main() {
  std::list<int> values{5, 7, 8};
  int calls = 0;
  lookup(values, calls)->reverse();
  if (calls != 1 || values.front() != 8 || values.back() != 5)
    return 1;
  values.pop_front();
  auto item = values.begin();
  return values.size() != 2 || *item++ != 7 || *item++ != 5 || item != values.end();
}
)r19containers");
  const auto Output = tmpFile("list-reverse-evaluates-pointer-receiver-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-reverse-evaluates-pointer-receiver-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRangeErasePreservesBoundaryAndEmptyRangeRunAtBothOptimizations) {
  const auto Source = tmpFile("list-range-erase-preserves-boundary-and-empty-range.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<int> values{2, 5, 7, 11, 13};
  auto first = values.cbegin();
  ++first;
  auto last = values.cend();
  --last;
  const int* last_address = &*last;
  auto end = values.end();
  auto item = values.erase(first, last);
  if (&*item != last_address || values.size() != 2 || values.front() != 2 || values.end() != end)
    return 1;
  auto unchanged = values.erase(last, last);
  if (unchanged != item || values.size() != 2)
    return 2;
  return values.erase(values.cbegin(), values.cend()) != end || !values.empty();
}
)r19containers");
  const auto Output = tmpFile("list-range-erase-preserves-boundary-and-empty-range.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-range-erase-preserves-boundary-and-empty-range" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRangeEraseOtherElementGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("list-range-erase-other-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<long> values{5L, -7L};
  return values.erase(values.cbegin(), values.cend()) != values.end();
}
)r19containers");
  const auto Output = tmpFile("list-range-erase-other-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-range-erase-other-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListSpliceRvalueIntoEmptyOwnerRunAtBothOptimizations) {
  const auto Source = tmpFile("list-splice-rvalue-into-empty-owner.cpp");
  writeFile(Source, R"r19containers(#include <list>
#include <utility>
int main() {
  std::list<int> values;
  std::list<int> source{13, 21};
  auto first = source.begin();
  values.splice(values.cend(), std::move(source));
  if (!source.empty() || values.size() != 2 || values.begin() != first ||
      values.front() != 13 || values.back() != 21)
    return 1;
  source.splice(source.cend(), std::move(values));
  return !values.empty() || source.size() != 2 || source.begin() != first || source.back() != 21;
}
)r19containers");
  const auto Output = tmpFile("list-splice-rvalue-into-empty-owner.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-splice-rvalue-into-empty-owner" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListSpliceSingleTransfersAddressAndCountRunAtBothOptimizations) {
  const auto Source = tmpFile("list-splice-single-transfers-address-and-count.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<int> values{2, 5};
  std::list<int> source{3, 7};
  auto position = values.cbegin();
  ++position;
  auto moved = source.begin();
  const int* address = &*moved;
  values.splice(position, source, moved);
  if (source.size() != 1 || source.front() != 7 || source.back() != 7 || values.size() != 3)
    return 1;
  auto check = values.begin();
  ++check;
  if (check != moved || &*check != address || *check != 3)
    return 2;
  ++check;
  return check != position || *check != 5;
}
)r19containers");
  const auto Output = tmpFile("list-splice-single-transfers-address-and-count.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-splice-single-transfers-address-and-count" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListSpliceRvalueRangeKeepsBothOwnerBoundariesRunAtBothOptimizations) {
  const auto Source = tmpFile("list-splice-rvalue-range-keeps-both-owner-boundaries.cpp");
  writeFile(Source, R"r19containers(#include <list>
#include <utility>
int main() {
  std::list<int> values{2, 11};
  std::list<int> source{3, 5, 7, 13};
  auto first = source.begin();
  ++first;
  auto after = source.end();
  --after;
  auto position = values.end();
  --position;
  const int* address = &*first;
  values.splice(position, std::move(source), first, after);
  if (source.size() != 2 || source.front() != 3 || source.back() != 13 ||
      values.size() != 4 || values.front() != 2 || values.back() != 11)
    return 1;
  auto check = values.begin();
  ++check;
  if (check != first || &*check != address || *check++ != 5 || *check++ != 7 || check != position)
    return 2;
  return *--after != 3;
}
)r19containers");
  const auto Output = tmpFile("list-splice-rvalue-range-keeps-both-owner-boundaries.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-splice-rvalue-range-keeps-both-owner-boundaries" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListSpliceOtherElementGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("list-splice-other-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<long> values{3, 5};
  std::list<long> source{7};
  values.splice(values.cbegin(), source, source.cbegin());
  return values.front() != 7;
}
)r19containers");
  const auto Output = tmpFile("list-splice-other-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-splice-other-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListUniqueKeepsFirstNodeOfEachRunRunAtBothOptimizations) {
  const auto Source = tmpFile("list-unique-keeps-first-node-of-each-run.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<int> empty;
  empty.unique();
  std::list<int> single{17};
  auto only = single.begin();
  single.unique();
  if (!empty.empty() || single.size() != 1 || single.begin() != only)
    return 1;
  std::list<int> items{3, 3, 7, 3, 3, 3, 5, 5, 3};
  auto first = items.begin();
  auto later = items.end();
  --later;
  auto end = items.end();
  items.unique();
  if (items.size() != 5 || items.begin() != first || items.end() != end)
    return 2;
  auto current = items.begin();
  if (*current++ != 3 || *current++ != 7 || *current++ != 3 || *current++ != 5 || current != later ||
      *current++ != 3 || current != end)
    return 3;
  return *--later != 5;
}
)r19containers");
  const auto Output = tmpFile("list-unique-keeps-first-node-of-each-run.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-unique-keeps-first-node-of-each-run" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListUniqueUserPredicateGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("list-unique-user-predicate-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
bool same(int left, int right) { return left == right; }
int main() {
  std::list<int> items{3, 3, 5};
  items.unique(same);
  return items.size() != 2;
}
)r19containers");
  const auto Output = tmpFile("list-unique-user-predicate-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-unique-user-predicate-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListSortDefaultEmptySingletonAndTwoNodesRunAtBothOptimizations) {
  const auto Source = tmpFile("list-sort-default-empty-singleton-and-two-nodes.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<int> empty;
  empty.sort();
  std::list<int> single{24};
  auto one = single.begin();
  single.sort();
  if (!empty.empty() || single.size() != 1 || single.begin() != one || single.front() != 24)
    return 1;
  std::list<int> pair{7, 3};
  auto old_first = pair.begin();
  auto end = pair.end();
  pair.sort();
  pair.sort();
  auto last = pair.end();
  --last;
  return pair.size() != 2 || pair.front() != 3 || pair.back() != 7 || last != old_first || pair.end() != end;
}
)r19containers");
  const auto Output = tmpFile("list-sort-default-empty-singleton-and-two-nodes.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-sort-default-empty-singleton-and-two-nodes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListSortExplicitOrderGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("list-sort-explicit-order-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
bool descending(int left, int right) { return left > right; }
int main() {
  std::list<int> items{3, 7, 5};
  items.sort(descending);
  return items.front() != 7;
}
)r19containers");
  const auto Output = tmpFile("list-sort-explicit-order-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-sort-explicit-order-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListOwnerRelationsPrefixAndEmptyRunAtBothOptimizations) {
  const auto Source = tmpFile("list-owner-relations-prefix-and-empty.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<int> empty, empty_again, first{3, 5}, second{3, 5, -7}, same{3, 5};
  if (!(empty == empty_again) || empty != empty_again || empty < empty || !(empty >= empty))
    return 1;
  if (!(empty < first) || !(first < second) || second < first || first == second)
    return 2;
  if (!(first == same) || first != same || first < same || first > same ||
      !(first <= same) || !(first >= same))
    return 3;
  return &first.front() == &same.front();
}
)r19containers");
  const auto Output = tmpFile("list-owner-relations-prefix-and-empty.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-owner-relations-prefix-and-empty" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListOwnerRelationsOtherElementGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("list-owner-relations-other-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  const std::list<long> first{3L, 5L}, second{7L, 9L};
  return !(first < second) || first == second;
}
)r19containers");
  const auto Output = tmpFile("list-owner-relations-other-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-owner-relations-other-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListResizeGrowthKeepsChainDetachedAndReadsFillAfterAllocationRunAtBothOptimizations) {
  const auto Source = tmpFile("list-resize-growth-keeps-chain-detached-and-reads-fill-after-allocation.cpp");
  writeFile(Source, R"r19containers(using Size = decltype(sizeof(0));
extern "C" void* malloc(Size);
extern "C" void free(void*);
#include <new>
#include <list>
static std::list<int>* observed;
static int calls, bad;
void* operator new(Size bytes) {
  if (observed) {
    ++calls;
    if (observed->size() != 2 || observed->back() != 20)
      bad = 1;
    ++observed->front();
  }
  return ::malloc(bytes);
}
void operator delete(void* p) noexcept { ::free(p); }
int main() {
  std::list<int> items{10, 20};
  observed = &items;
  items.resize(5, items.front());
  observed = nullptr;
  if (calls != 3 || bad || items.size() != 5)
    return 1;
  auto it = items.begin();
  return *it++ != 13 || *it++ != 20 || *it++ != 11 || *it++ != 12 || *it++ != 13 || it != items.end();
}
)r19containers");
  const auto Output = tmpFile("list-resize-growth-keeps-chain-detached-and-reads-fill-after-allocation.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-resize-growth-keeps-chain-detached-and-reads-fill-after-allocation" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListResizeOtherElementGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("list-resize-other-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<long> items{3L, 5L};
  items.resize(4, 7L);
  return items.back() != 7L;
}
)r19containers");
  const auto Output = tmpFile("list-resize-other-element-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-resize-other-element-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListUnsignedMergesAndComparesInTheUnsignedDomainRunAtBothOptimizations) {
  const auto Source = tmpFile("list-unsigned-merges-and-compares-in-the-unsigned-domain.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  constexpr unsigned maximum = static_cast<unsigned>(-1);
  std::list<unsigned> lhs{1u, maximum}, rhs{2u, maximum};
  auto moved = rhs.cbegin();
  auto* moved_address = &*moved;
  lhs.merge(rhs);
  const std::list<unsigned> prefix{1u, 2u};
  if (!rhs.empty() || lhs.size() != 4 || lhs.back() != maximum || !(prefix < lhs) || lhs < prefix)
    return 1;
  const std::list<unsigned> smaller{1u, 2u, maximum - 1u, maximum};
  if (!(smaller < lhs) || lhs < smaller)
    return 2;
  lhs.remove(lhs.front());
  return lhs.cbegin() != moved || &lhs.front() != moved_address || lhs.front() != 2u || lhs.size() != 3;
}
)r19containers");
  const auto Output = tmpFile("list-unsigned-merges-and-compares-in-the-unsigned-domain.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-unsigned-merges-and-compares-in-the-unsigned-domain" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListLongLongGuardRunAtBothOptimizations) {
  const auto Source = tmpFile("list-long-long-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<long long> items{3LL, 5LL};
  items.push_back(7LL);
  return items.back() != 7LL;
}
)r19containers");
  const auto Output = tmpFile("list-long-long-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-long-long-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListMaxSizeUsesTheCheckedNodeWidthRunAtBothOptimizations) {
  const auto Source = tmpFile("list-max-size-uses-the-checked-node-width.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  using Size = decltype(sizeof(0));
  const Size maximum = static_cast<Size>(-1) / 24;
  const std::list<int> empty;
  const std::list<long> first{3L, 5L, 7L};
  const std::list<unsigned long> second{9UL, 11UL};
  return empty.max_size() != maximum || first.max_size() != maximum || second.max_size() != maximum;
}
)r19containers");
  const auto Output = tmpFile("list-max-size-uses-the-checked-node-width.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-max-size-uses-the-checked-node-width" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRemoveIfChecksEachElementBeforeSplicingARunRunAtBothOptimizations) {
  const auto Source = tmpFile("list-remove-if-checks-each-element-before-splicing-a-run.cpp");
  writeFile(Source, R"r19containers(#include <list>
std::list<unsigned>* active;
int sequence, sizes, selections;
bool even(unsigned value) {
  sequence = sequence * 10 + static_cast<int>(value);
  sizes = sizes * 10 + static_cast<int>(active->size());
  return value % 2u == 0u;
}
using Predicate = bool (*)(unsigned);
Predicate select() {
  ++selections;
  return &even;
}
int main() {
  std::list<unsigned> values{2u, 4u, 3u, 6u, 5u};
  active = &values;
  auto first = values.begin();
  ++first;
  ++first;
  auto last = values.end();
  --last;
  values.remove_if(select());
  if (sequence != 24365 || sizes != 55533 || selections != 1 || values.size() != 2)
    return 1;
  auto item = values.begin();
  return item != first || *item++ != 3u || item != last || *item++ != 5u || item != values.end();
}
)r19containers");
  const auto Output = tmpFile("list-remove-if-checks-each-element-before-splicing-a-run.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-remove-if-checks-each-element-before-splicing-a-run" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListWideSignedSortAndResizeKeepFullValuesRunAtBothOptimizations) {
  const auto Source = tmpFile("list-wide-signed-sort-and-resize-keep-full-values.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  const long long low = -5000000001LL;
  const long long high = 7000000003LL;
  std::list<long long> values{high, low, high, 2LL};
  auto first = values.begin();
  auto low_node = first;
  ++low_node;
  values.sort();
  auto item = values.begin();
  if (item != low_node || *item++ != low || *item++ != 2LL || item != first || *item++ != high)
    return 1;
  values.resize(6, values.back());
  return values.size() != 6 || values.front() != low || values.back() != high || *first != high;
}
)r19containers");
  const auto Output = tmpFile("list-wide-signed-sort-and-resize-keep-full-values.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-wide-signed-sort-and-resize-keep-full-values" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListEmplaceEndsReturnMutableReferencesRunAtBothOptimizations) {
  const auto Source = tmpFile("list-emplace-ends-return-mutable-references.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  const long long high = 7000000003LL;
  std::list<long long> values{13LL};
  auto middle = values.begin();
  auto end = values.end();
  long long& first = values.emplace_front();
  if (first != 0 || &first != &values.front())
    return 1;
  long long& last = values.emplace_back(high);
  first = -17LL;
  if (&last != &values.back() || last != high || values.size() != 3)
    return 2;
  auto item = values.begin();
  return *item++ != -17LL || item != middle || *item++ != 13LL ||
         *item++ != high || item != end || end != values.end();
}
)r19containers");
  const auto Output = tmpFile("list-emplace-ends-return-mutable-references.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-emplace-ends-return-mutable-references" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListEmplaceReadsReferenceAfterAllocationRunAtBothOptimizations) {
  const auto Source = tmpFile("list-emplace-reads-reference-after-allocation.cpp");
  writeFile(Source, R"r19containers(using Size = decltype(sizeof(0));
extern "C" void* malloc(Size);
extern "C" void free(void*);
#include <new>
#include <list>
static std::list<int>* observed;
static int value = 9, calls, seen_size;
void* operator new(Size count) {
  if (observed) {
    ++calls;
    seen_size = static_cast<int>(observed->size());
    value = 27;
  }
  return ::malloc(count);
}
void operator delete(void* p) noexcept { ::free(p); }
int main() {
  std::list<int> values{3};
  observed = &values;
  const int& fill = value;
  int& item = values.emplace_back(fill);
  observed = nullptr;
  return calls != 1 || seen_size != 1 || item != 27 || values.size() != 2 || &item != &values.back();
}
)r19containers");
  const auto Output = tmpFile("list-emplace-reads-reference-after-allocation.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-emplace-reads-reference-after-allocation" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListUnsignedShortPayloadKeepsHighBitRunAtBothOptimizations) {
  const auto Source = tmpFile("list-unsigned-short-payload-keeps-high-bit.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<unsigned short> left{9, 33000};
  std::list<unsigned short> right{33000, 65535};
  auto first_moved = right.begin();
  left.merge(right);
  auto item = left.begin();
  if (!right.empty() || *item++ != 9 || *item++ != 33000 ||
      item != first_moved || *item++ != 33000 || *item++ != 65535)
    return 1;
  left.resize(6, left.back());
  return left.back() != 65535 || left.size() != 6;
}
)r19containers");
  const auto Output = tmpFile("list-unsigned-short-payload-keeps-high-bit.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-unsigned-short-payload-keeps-high-bit" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListCountAssignmentShrinksAndOverwritesRunAtBothOptimizations) {
  const auto Source = tmpFile("list-count-assignment-shrinks-and-overwrites.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<int> values{1, 2, 3, 4};
  values.assign(2, -5);
  auto current = values.begin();
  return values.size() != 2 || *current++ != -5 || *current++ != -5 || current != values.end();
}
)r19containers");
  const auto Output = tmpFile("list-count-assignment-shrinks-and-overwrites.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-count-assignment-shrinks-and-overwrites" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListCountAssignmentZeroAndWideRegrowthRunAtBothOptimizations) {
  const auto Source = tmpFile("list-count-assignment-zero-and-wide-regrowth.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  const long long wide = -7000000009LL;
  std::list<long long> values{9LL, 13LL};
  values.assign(0, wide);
  if (!values.empty() || values.begin() != values.end())
    return 1;
  values.assign(3, wide);
  auto item = values.begin();
  return values.size() != 3 || *item++ != wide || *item++ != wide ||
         *item++ != wide || item != values.end();
}
)r19containers");
  const auto Output = tmpFile("list-count-assignment-zero-and-wide-regrowth.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-count-assignment-zero-and-wide-regrowth" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListConvertingRangeRetainsNodesAndDefersAppendsRunAtBothOptimizations) {
  const auto Source = tmpFile("list-converting-range-retains-nodes-and-defers-appends.cpp");
  writeFile(Source, R"r19containers(using Size = decltype(sizeof(0));
extern "C" void* malloc(Size);
extern "C" void free(void*);
#include <new>
#include <list>
static short input[]{-9, 10, -20};
static std::list<long long>* observed;
static int allocations, bad;
void* operator new(Size size) {
  if (observed) {
    ++allocations;
    if (observed->size() != 1 || observed->front() != -9LL)
      bad = 1;
    input[1] = 13 + allocations;
    input[2] = -23 - allocations;
  }
  return ::malloc(size);
}
void operator delete(void* p) noexcept { ::free(p); }
int main() {
  std::list<long long> values{3LL};
  auto first = values.begin();
  const short* data = input;
  observed = &values;
  values.assign(data, data + 3);
  observed = nullptr;
  auto item = values.begin();
  return bad || allocations != 2 || values.size() != 3 || item != first ||
         *item++ != -9LL || *item++ != 14LL || *item++ != -25LL || item != values.end();
}
)r19containers");
  const auto Output = tmpFile("list-converting-range-retains-nodes-and-defers-appends.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-converting-range-retains-nodes-and-defers-appends" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListConstructByCountValueAndDefaultAllocatorRunAtBothOptimizations) {
  const auto Source = tmpFile("list-construct-by-count-value-and-default-allocator.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  const std::allocator<long> alloc;
  const std::list<long> zero(0, alloc);
  const std::list<long> defaults(2, alloc);
  const std::list<unsigned long long> filled(3, 18446744073709551607ULL);
  auto cursor = filled.cbegin();
  return !zero.empty() || defaults.size() != 2 || defaults.front() || defaults.back() ||
         filled.size() != 3 || *cursor++ != 18446744073709551607ULL ||
         *cursor++ != 18446744073709551607ULL || *cursor++ != 18446744073709551607ULL ||
         cursor != filled.cend();
}
)r19containers");
  const auto Output = tmpFile("list-construct-by-count-value-and-default-allocator.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-construct-by-count-value-and-default-allocator" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListConstructFromConstIteratorsAndExplicitAllocatorRunAtBothOptimizations) {
  const auto Source = tmpFile("list-construct-from-const-iterators-and-explicit-allocator.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  const std::list<unsigned short> original{2, 65000, 4};
  auto start = original.cbegin();
  ++start;
  const std::allocator<unsigned short> alloc;
  const std::list<unsigned short> values(start, original.cend(), alloc);
  return original.size() != 3 || values.size() != 2 || values.front() != 65000 ||
         values.back() != 4 || &values.front() == &*start;
}
)r19containers");
  const auto Output = tmpFile("list-construct-from-const-iterators-and-explicit-allocator.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-construct-from-const-iterators-and-explicit-allocator" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListOperatorAssignmentByInitializerRetainsOwnerRunAtBothOptimizations) {
  const auto Source = tmpFile("list-operator-assignment-by-initializer-retains-owner.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<unsigned long long> values{11ULL};
  auto first = values.begin();
  auto& result = (values = {18446744073709551607ULL, 18ULL, 19ULL});
  auto item = values.begin();
  return &result != &values || values.size() != 3 || item != first ||
         *item++ != 18446744073709551607ULL || *item++ != 18ULL ||
         *item++ != 19ULL || item != values.end();
}
)r19containers");
  const auto Output = tmpFile("list-operator-assignment-by-initializer-retains-owner.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-operator-assignment-by-initializer-retains-owner" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListInsertCountValueAndEmptyRangeReturnPositionRunAtBothOptimizations) {
  const auto Source = tmpFile("list-insert-count-value-and-empty-range-return-position.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<unsigned long long> values{1ULL, 5ULL};
  auto before = values.begin();
  auto position = before;
  ++position;
  if (values.insert(position, 0, 9ULL) != position)
    return 1;
  auto first = values.insert(position, 2, 18446744073709551607ULL);
  auto item = values.begin();
  return values.size() != 4 || item != before || *item++ != 1ULL || item != first ||
         *item++ != 18446744073709551607ULL || *item++ != 18446744073709551607ULL ||
         item != position || *item++ != 5ULL || item != values.end();
}
)r19containers");
  const auto Output = tmpFile("list-insert-count-value-and-empty-range-return-position.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-insert-count-value-and-empty-range-return-position" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListInsertConstIteratorRangeKeepsSourceNodesRunAtBothOptimizations) {
  const auto Source = tmpFile("list-insert-const-iterator-range-keeps-source-nodes.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  const std::list<unsigned short> from{60000, 65000};
  std::list<unsigned short> values{1};
  auto first = values.insert(values.cend(), from.cbegin(), from.cend());
  auto next = first;
  return from.size() != 2 || values.size() != 3 || &*first == &from.front() ||
         *next++ != 60000 || *next++ != 65000 || next != values.end();
}
)r19containers");
  const auto Output = tmpFile("list-insert-const-iterator-range-keeps-source-nodes.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-insert-const-iterator-range-keeps-source-nodes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListExplicitOrderWideSortPreservesEquivalentNodesRunAtBothOptimizations) {
  const auto Source = tmpFile("list-explicit-order-wide-sort-preserves-equivalent-nodes.cpp");
  writeFile(Source, R"r19containers(#include <list>
bool higher(unsigned long long left, unsigned long long right) { return right < left; }
int main() {
  const auto high = 18446744073709551607ULL;
  std::list<unsigned long long> values{2ULL, high, 1ULL, high, 6ULL};
  auto early = values.begin();
  ++early;
  auto late = early;
  ++late;
  ++late;
  values.sort(&higher);
  auto item = values.begin();
  return item != early || *item++ != high || item != late || *item++ != high ||
         *item++ != 6ULL || *item++ != 2ULL || *item++ != 1ULL || item != values.end();
}
)r19containers");
  const auto Output = tmpFile("list-explicit-order-wide-sort-preserves-equivalent-nodes.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-explicit-order-wide-sort-preserves-equivalent-nodes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListReverseIteratorBaseAndNodeIdentityRunAtBothOptimizations) {
  const auto Source = tmpFile("list-reverse-iterator-base-and-node-identity.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<long> values{3L, -4L, 8L};
  auto top = values.rbegin();
  auto end = values.rend();
  if (top.base() != values.end() || end.base() != values.begin() || *top != 8L)
    return 1;
  *top = 12L;
  auto item = top;
  auto previous = item++;
  if (previous != top || *item != -4L || values.back() != 12L)
    return 2;
  ++item;
  auto old = item--;
  --item;
  return *old == 3L && item == top ? 0 : 3;
}
)r19containers");
  const auto Output = tmpFile("list-reverse-iterator-base-and-node-identity.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-reverse-iterator-base-and-node-identity" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListReverseIteratorFactoryAndConstConversionRunAtBothOptimizations) {
  const auto Source = tmpFile("list-reverse-iterator-factory-and-const-conversion.cpp");
  writeFile(Source, R"r19containers(#include <iterator>
#include <list>
int main() {
  std::list<int> values{1, 4, 9};
  auto from = std::make_reverse_iterator(values.end());
  std::reverse_iterator<std::list<int>::iterator> first(values.end());
  std::list<int>::const_reverse_iterator converted(from), assigned;
  assigned = first;
  if (first != converted || converted != assigned || first.base() != values.end())
    return 1;
  int *last = first.operator->();
  *last = 11;
  return *converted == 11 && values.back() == 11 ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("list-reverse-iterator-factory-and-const-conversion.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-reverse-iterator-factory-and-const-conversion" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListIteratorSignedAdvanceAndDefaultNavigationRunAtBothOptimizations) {
  const auto Source = tmpFile("list-iterator-signed-advance-and-default-navigation.cpp");
  writeFile(Source, R"r19containers(#include <iterator>
#include <list>
int main() {
  std::list<long> values{-2L, 3L, 6L, 8L};
  auto last = std::prev(values.end());
  auto second = std::next(values.begin());
  if (*last != 8L || *second != 3L || std::distance(second, values.end()) != 3)
    return 1;
  std::advance(last, -2);
  if (last != second || std::distance(last, second) != 0)
    return 2;
  std::advance(last, 2);
  return *last == 8L && *std::prev(last, 2) == 3L ? 0 : 3;
}
)r19containers");
  const auto Output = tmpFile("list-iterator-signed-advance-and-default-navigation.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-iterator-signed-advance-and-default-navigation" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListIteratorNavigationEvaluatesDistanceOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("list-iterator-navigation-evaluates-distance-once.cpp");
  writeFile(Source, R"r19containers(#include <iterator>
#include <list>
int calls;
int distance_value() { ++calls; return 1; }
int main() {
  std::list<int> values{1, 2, 3};
  auto cursor = values.begin();
  std::advance(cursor, distance_value());
  auto first = std::prev(cursor, distance_value());
  return *cursor == 2 && *first == 1 && calls == 2 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("list-iterator-navigation-evaluates-distance-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-iterator-navigation-evaluates-distance-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListFreeConstAccessMatchesMemberBaseRunAtBothOptimizations) {
  const auto Source = tmpFile("list-free-const-access-matches-member-base.cpp");
  writeFile(Source, R"r19containers(#include <iterator>
#include <list>
int main() {
  const std::list<short> values{2, 5, 9};
  auto first = std::cbegin(values);
  auto last = std::cend(values);
  auto top = std::crbegin(values);
  auto end = std::crend(values);
  return first == values.begin() && last == values.end() && *top == 9 &&
      top.base() == last && end.base() == first && *std::next(top) == 5 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("list-free-const-access-matches-member-base.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-free-const-access-matches-member-base" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListBoolAddressableReferencesAndNodeIdentityRunAtBothOptimizations) {
  const auto Source = tmpFile("list-bool-addressable-references-and-node-identity.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<bool> values{true, false, true};
  auto it = values.begin();
  bool *first = &*it;
  *first = false;
  ++it;
  if (values.front() || first == &*it)
    return 1;
  values.insert(values.end(), true);
  values.reverse();
  return values.size() == 4 && values.front() && !values.back() && &values.back() == first ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("list-bool-addressable-references-and-node-identity.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-bool-addressable-references-and-node-identity" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListBoolStableOrderAndUniqueRunAtBothOptimizations) {
  const auto Source = tmpFile("list-bool-stable-order-and-unique.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<bool> values{true, false, true, false};
  auto first_true = values.begin();
  auto first_false = first_true;
  ++first_false;
  values.sort();
  if (values.begin() != first_false || *first_false || !*first_true)
    return 1;
  values.unique();
  auto item = values.begin();
  ++item;
  return values.size() == 2 && values.begin() == first_false && item == first_true ? 0 : 2;
}
)r19containers");
  const auto Output = tmpFile("list-bool-stable-order-and-unique.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-bool-stable-order-and-unique" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListStandardGreaterKeepsSignedNodeOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("list-standard-greater-keeps-signed-node-order.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <list>
int main() {
  const long maximum = sizeof(long) == 8 ? static_cast<long>(4294967301LL) : 19L;
  std::list<long> values{-3L, maximum, maximum, 4L};
  auto original = values.begin();
  ++original;
  values.sort(std::greater<long>{});
  auto it = values.begin();
  return it != original || *it++ != maximum || *it++ != maximum || *it++ != 4L ||
         *it++ != -3L || it != values.end();
}
)r19containers");
  const auto Output = tmpFile("list-standard-greater-keeps-signed-node-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-standard-greater-keeps-signed-node-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListExplicitStandardLessOrdersLvalueMergeRunAtBothOptimizations) {
  const auto Source = tmpFile("list-explicit-standard-less-orders-lvalue-merge.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <list>
int main() {
  std::list<int> left{-3, 6}, right{-2, 4};
  const std::less<int> order{};
  left.merge(right, order);
  auto item = left.begin();
  return !right.empty() || *item++ != -3 || *item++ != -2 || *item++ != 4 || *item++ != 6;
}
)r19containers");
  const auto Output = tmpFile("list-explicit-standard-less-orders-lvalue-merge.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-explicit-standard-less-orders-lvalue-merge" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListEmplacesWithIntegerConversionAtBothOptimizations) {
  const auto Source = tmpFile("list-emplace-converting-long-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<int> values;
  const long value = 9;
  return values.emplace_front(value) != 9;
}
)r19containers");
  const auto Output = tmpFile("list-emplace-converting-long-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-emplace-converting-long-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListStoresDoubleElementsAtBothOptimizations) {
  const auto Source = tmpFile("list-floating-payload-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<double> values{3.0, 5.0};
  values.push_back(7.0);
  return values.back() != 7.0;
}
)r19containers");
  const auto Output = tmpFile("list-floating-payload-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-floating-payload-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListStoresAFixedUnderlyingEnum) {
  const auto Source = tmpFile("list-user-enum-payload-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
enum Code : unsigned char { First = 1, Second = 2 };
int main() {
  std::list<Code> values{First};
  values.push_back(Second);
  return values.back() != Second;
}
)r19containers");
  const auto Output = tmpFile("list-user-enum-payload-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-user-enum-payload-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListReverseIteratorTraversesTrivialRecords) {
  const auto Source = tmpFile("list-reverse-iterator-unsupported-payload-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Record { int member; };
int main() {
  const std::list<Record> values{Record{2}, Record{5}};
  return values.rbegin()->member == 5 ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("list-reverse-iterator-unsupported-payload-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-reverse-iterator-unsupported-payload-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListElementsUseRecordBoolConversion) {
  const auto Source = tmpFile("list-bool-user-proxy-payload-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Proxy { bool value; operator bool() const { return value; } };
int main() {
  std::list<Proxy> values{Proxy{true}, Proxy{false}};
  return values.front() && !values.back() ? 0 : 1;
}
)r19containers");
  const auto Output = tmpFile("list-bool-user-proxy-payload-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-bool-user-proxy-payload-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListIndirectCapacityGuardRejected) {
  const auto Source = tmpFile("list-indirect-capacity-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  using List = std::list<int>;
  const List items{3, 7};
  const auto method = &List::max_size;
  return (items.*method)() <= 2;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListRemoveIfStatefulObjectGuardRejected) {
  const auto Source = tmpFile("list-remove-if-stateful-object-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Matches {
  unsigned value;
  bool operator()(unsigned item) const { return item == value; }
};
int main() {
  std::list<unsigned> items{1u, 2u};
  items.remove_if(Matches{2u});
  return items.front() != 1u;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListAssignRangeVolatileInputGuardRejected) {
  const auto Source = tmpFile("list-assign-range-volatile-input-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  volatile unsigned int input[]{1, 2};
  std::list<unsigned int> values;
  values.assign(input, input + 2);
  return values.size() != 2;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListConstructRangeVolatileInputGuardRejected) {
  const auto Source = tmpFile("list-construct-range-volatile-input-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  volatile short source[]{3, 4};
  const std::list<int> values(source, source + 2);
  return values.size() != 2;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListOrderFunctionWithObservableBodyGuardRejected) {
  const auto Source = tmpFile("list-order-function-with-observable-body-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int calls;
bool greater(int left, int right) {
  ++calls;
  return left > right;
}
int main() {
  std::list<int> values{2, 5, 7};
  values.sort(&greater);
  return calls == 0 || values.front() != 7;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListStandardOrderWidthConversionGuardRejected) {
  const auto Source = tmpFile("list-standard-order-width-conversion-guard.cpp");
  writeFile(Source, R"r19containers(#include <functional>
#include <list>
int main() {
  std::list<long> values{2L, 5L};
  values.sort(std::greater<int>{});
  return values.front() != 5L;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListEmptyOrderRvalueMergeCopiesAndKeepsEqualNodesRunAtBothOptimizations) {
  const auto Source = tmpFile("list-empty-order-rvalue-merge-copies-and-keeps-equal-nodes.cpp");
  writeFile(Source, R"r19containers(#include <list>
#include <utility>
struct Ascending {
  bool operator()(short left, short right) const { return left < right; }
};
int main() {
  std::list<short> left{-4, 2}, right{-4, 1};
  auto left_first = left.begin();
  auto right_first = right.begin();
  left.merge(std::move(right), Ascending{});
  auto it = left.begin();
  return !right.empty() || it != left_first || *it++ != -4 || it != right_first ||
         *it++ != -4 || *it++ != 1 || *it++ != 2 || it != left.end();
}
)r19containers");
  const auto Output = tmpFile("list-empty-order-rvalue-merge-copies-and-keeps-equal-nodes.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-empty-order-rvalue-merge-copies-and-keeps-equal-nodes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListTransparentGreaterMoveMergePreservesTiesRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-transparent-greater-move-merge-preserves-ties.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <functional>
#include <utility>
int main() {
  const unsigned long high = static_cast<unsigned long>(-1) / 2 + 1;
  std::forward_list<unsigned long> left{high, 7UL}, right{high, 9UL, 0UL};
  auto left_first = left.begin();
  auto right_first = right.begin();
  left.merge(std::move(right), std::greater<>{});
  auto it = left.begin();
  return !right.empty() || it != left_first || *it++ != high || it != right_first ||
         *it++ != high || *it++ != 9UL || *it++ != 7UL || *it++ != 0UL || it != left.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-transparent-greater-move-merge-preserves-ties.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-transparent-greater-move-merge-preserves-ties" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListShortReferenceCopyAndConvertingEmplacementRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-short-reference-copy-and-converting-emplacement.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  const long input[] = {-30000L, 7L, 32760L};
  std::forward_list<short> values(input, input + 3);
  const short* first = &values.front();
  auto copy = values;
  auto inserted = values.emplace_after(values.cbegin(), input[2]);
  if (&values.front() != first || *inserted != 32760 || copy.front() != -30000)
    return 1;
  *inserted = -32768;
  auto it = values.cbegin();
  return *it++ != -30000 || it != inserted || *it++ != -32768 ||
         *it++ != 7 || *it++ != 32760 || it != values.cend();
}
)r19containers");
  const auto Output = tmpFile("forward-list-short-reference-copy-and-converting-emplacement.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-short-reference-copy-and-converting-emplacement" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListBoolFrontRemainsAddressableAfterSortingRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-bool-front-remains-addressable-after-sorting.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<bool> values{true, false};
  bool* first = &values.front();
  values.insert_after(values.cbegin(), true);
  *first = false;
  values.sort();
  auto it = values.begin();
  return &*it != first || *it++ || *it++ || !*it++ || it != values.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-bool-front-remains-addressable-after-sorting.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-bool-front-remains-addressable-after-sorting" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListLongLongMergeKeepsLargeNegativeDuplicatesRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-long-long-merge-keeps-large-negative-duplicates.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  const long long low = -4294967301LL;
  std::forward_list<long long> left{low, 7LL}, right{low, 1LL};
  auto left_first = left.begin();
  auto right_first = right.begin();
  left.merge(right);
  auto it = left.begin();
  return !right.empty() || it != left_first || *it++ != low || it != right_first ||
         *it++ != low || *it++ != 1LL || *it++ != 7LL || it != left.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-long-long-merge-keeps-large-negative-duplicates.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-long-long-merge-keeps-large-negative-duplicates" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListEmptyOrderObservableBodyGuardRejected) {
  const auto Source = tmpFile("list-empty-order-observable-body-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
static int calls;
struct Compare {
  bool operator()(int left, int right) const {
    ++calls;
    return left < right;
  }
};
int main() {
  std::list<int> values{4, 1};
  values.sort(Compare{});
  return calls == 0 || values.front() != 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListStandardOrderSignednessConversionGuardRejected) {
  const auto Source = tmpFile("forward-list-standard-order-signedness-conversion-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#include <functional>
int main() {
  std::forward_list<int> values{-4, 1};
  values.sort(std::greater<unsigned int>{});
  return values.front() != -4;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListEmplaceConversionKeepsPositionAndUnsignedResidueRunAtBothOptimizations) {
  const auto Source = tmpFile("list-emplace-conversion-keeps-position-and-unsigned-residue.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<unsigned short> values{7, 9};
  auto first = values.begin();
  auto position = values.cbegin();
  ++position;
  const signed char negative = -2;
  auto added = values.emplace(position, negative);
  unsigned short& back = values.emplace_back(65539UL);
  auto item = values.begin();
  return values.size() != 4 || item != first || *item++ != 7 || item != added ||
         *item++ != 65534 || *item++ != 9 || &*item != &back || *item++ != 3 || item != values.end();
}
)r19containers");
  const auto Output = tmpFile("list-emplace-conversion-keeps-position-and-unsigned-residue.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-emplace-conversion-keeps-position-and-unsigned-residue" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListFloatStableMergeRetainsTheOriginalNodesRunAtBothOptimizations) {
  const auto Source = tmpFile("list-float-stable-merge-retains-the-original-nodes.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<float> left{-1.25f, 2.5f}, right{-1.25f, 9.75f};
  auto first = left.cbegin();
  auto second = right.cbegin();
  left.merge(right);
  auto it = left.cbegin();
  return !right.empty() || it != first || *it++ != -1.25f || it != second ||
         *it++ != -1.25f || *it++ != 2.5f || *it++ != 9.75f || it != left.cend();
}
)r19containers");
  const auto Output = tmpFile("list-float-stable-merge-retains-the-original-nodes.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-float-stable-merge-retains-the-original-nodes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListDoubleStableSortKeepsFractionalValuesRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-double-stable-sort-keeps-fractional-values.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  std::forward_list<double> values{4.125, -0.25, 4.125, -6.5};
  auto first_equal = values.begin();
  values.sort();
  auto it = values.begin();
  return *it++ != -6.5 || *it++ != -0.25 || it != first_equal ||
         *it++ != 4.125 || *it++ != 4.125 || it != values.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-double-stable-sort-keeps-fractional-values.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-double-stable-sort-keeps-fractional-values" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListEmplaceFloatingToIntegerGuardRejected) {
  const auto Source = tmpFile("list-emplace-floating-to-integer-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  std::list<int> values;
  const double value = -3.25;
  return values.emplace_back(value) != -3;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListDoubleRangeDoesNotAdmitFloatConversionRejected) {
  const auto Source = tmpFile("list-double-range-does-not-admit-float-conversion.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  const float data[]{2.25f, 3.0f};
  std::list<double> values(data, data + 2);
  return values.front() != 2.25;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListWideCharNativeSignednessAndIteratorIdentityRunAtBothOptimizations) {
  const auto Source = tmpFile("list-wide-char-native-signedness-and-iterator-identity.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  static_assert(sizeof(wchar_t) == 2 || sizeof(wchar_t) == 4);
  const wchar_t high_or_negative = static_cast<wchar_t>(-2);
  std::list<wchar_t> letters{L'\u03bb', high_or_negative};
  auto saved = letters.begin();
  auto* original = &*saved;
  letters.sort();
  const wchar_t first = high_or_negative < L'\u03bb' ? high_or_negative : L'\u03bb';
  const wchar_t second = high_or_negative < L'\u03bb' ? L'\u03bb' : high_or_negative;
  return letters.front() != first || letters.back() != second || &*saved != original || *saved != L'\u03bb';
}
)r19containers");
  const auto Output = tmpFile("list-wide-char-native-signedness-and-iterator-identity.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-wide-char-native-signedness-and-iterator-identity" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListUtf16PointerRangeWidensToUtf32RunAtBothOptimizations) {
  const auto Source = tmpFile("list-utf16-pointer-range-widens-to-utf32.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  const char16_t input[] = {u'A', u'\u03bb', u'\u6c34'};
  std::list<char32_t> letters(input, input + 3);
  auto value = letters.cbegin();
  return letters.size() != 3 || *value++ != U'A' || *value++ != U'\u03bb' ||
         *value++ != U'\u6c34' || value != letters.cend();
}
)r19containers");
  const auto Output = tmpFile("list-utf16-pointer-range-widens-to-utf32.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-utf16-pointer-range-widens-to-utf32" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListUtf16ConstReverseIteratorRunAtBothOptimizations) {
  const auto Source = tmpFile("list-utf16-const-reverse-iterator.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  const std::list<char16_t> letters{u'A', u'\u03bb'};
  auto value = letters.crbegin();
  if (&*value != &letters.back() || *value != u'\u03bb')
    return 1;
  ++value;
  if (&*value != &letters.front() || *value != u'A')
    return 2;
  ++value;
  return value != letters.crend();
}
)r19containers");
  const auto Output = tmpFile("list-utf16-const-reverse-iterator.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-utf16-const-reverse-iterator" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListFixedSignedEnumSortPreservesReferencesRunAtBothOptimizations) {
  const auto Source = tmpFile("list-fixed-signed-enum-sort-preserves-references.cpp");
  writeFile(Source, R"r19containers(#include <list>
enum class Status : signed char { missing = -2, present = 3 };
int main() {
  std::list<Status> values{Status::present, Status::missing};
  auto saved = values.begin();
  Status* address = &*saved;
  values.sort();
  return values.front() != Status::missing || values.back() != Status::present ||
         &values.back() != address || &*saved != address;
}
)r19containers");
  const auto Output = tmpFile("list-fixed-signed-enum-sort-preserves-references.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-fixed-signed-enum-sort-preserves-references" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListFixedEnumRangeAndDefaultInitializationRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-fixed-enum-range-and-default-initialization.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
enum Code : unsigned short { first = 3, second = 65531 };
int main() {
  const Code input[] = {first, second};
  std::forward_list<Code> values;
  values.assign(input, input + 2);
  values.resize(3);
  auto item = values.cbegin();
  return *item++ != first || *item++ != second || *item++ != Code{} || item != values.cend();
}
)r19containers");
  const auto Output = tmpFile("forward-list-fixed-enum-range-and-default-initialization.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-fixed-enum-range-and-default-initialization" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListUtf16VolatilePointerRangeGuardRejected) {
  const auto Source = tmpFile("list-utf16-volatile-pointer-range-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  volatile char16_t input[] = {u'\u03bb'};
  std::list<char16_t> letters(input, input + 1);
  return letters.front() != u'\u03bb';
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListFixedEnumCustomEqualityGuardRejected) {
  const auto Source = tmpFile("list-fixed-enum-custom-equality-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
enum class Code : unsigned short { a = 1, b = 3 };
bool operator==(Code left, Code right) {
  return (static_cast<unsigned short>(left) & 1) ==
         (static_cast<unsigned short>(right) & 1);
}
int main() {
  std::list<Code> left{Code::a}, right{Code::b};
  return !(left == right);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListObjectPointerSortingKeepsStoredSlotRunAtBothOptimizations) {
  const auto Source = tmpFile("list-object-pointer-sorting-keeps-stored-slot.cpp");
  writeFile(Source, R"r19containers(#include <list>
int main() {
  int data[] = {11, 22, 33};
  std::list<int*> pointers{&data[2], &data[0]};
  auto old = pointers.begin();
  int** slot = &*old;
  pointers.sort();
  if (pointers.front() != &data[0] || pointers.back() != &data[2] || &pointers.back() != slot)
    return 1;
  *old = &data[1];
  **old = 42;
  return pointers.back() != &data[1] || data[1] != 42 || data[2] != 33;
}
)r19containers");
  const auto Output = tmpFile("list-object-pointer-sorting-keeps-stored-slot.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-object-pointer-sorting-keeps-stored-slot" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListConstPointerAssignmentAndNullResizeRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-const-pointer-assignment-and-null-resize.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int main() {
  const int data[] = {5, 8};
  const int* const input[] = {data, data + 1};
  std::forward_list<const int*> pointers;
  pointers.assign(input, input + 2);
  pointers.resize(3);
  auto item = pointers.cbegin();
  return *item++ != &data[0] || *item++ != &data[1] || *item++ != nullptr || item != pointers.cend();
}
)r19containers");
  const auto Output = tmpFile("forward-list-const-pointer-assignment-and-null-resize.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-const-pointer-assignment-and-null-resize" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListFunctionPointerElementGuardRejected) {
  const auto Source = tmpFile("list-function-pointer-element-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int identity(int value) { return value; }
int main() {
  std::list<int (*)(int)> callbacks{&identity};
  return callbacks.front()(7) != 7;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListTrivialRecordCopyAndValueInitRunAtBothOptimizations) {
  const auto Source = tmpFile("list-trivial-record-copy-and-value-init.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry { int code; unsigned serial; };
int main() {
  Entry input{4, 7};
  std::list<Entry> values;
  values.push_back(input);
  auto& saved = values.back();
  auto& zero = values.emplace_front();
  if (zero.code || zero.serial || &values.back() != &saved)
    return 1;
  input.serial = 9;
  zero.code = 5;
  const auto& read = values;
  return read.begin()->code != 5 || read.back().serial != 7;
}
)r19containers");
  const auto Output = tmpFile("list-trivial-record-copy-and-value-init.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-trivial-record-copy-and-value-init" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListTrivialRecordCopyAssignmentRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-trivial-record-copy-assignment.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry { int code; int serial; };
int main() {
  std::forward_list<Entry> source{Entry{21, 4}, Entry{22, 5}};
  std::forward_list<Entry> destination{Entry{8, 9}};
  destination = source;
  source.front().serial = 14;
  auto item = destination.cbegin();
  if (item->code != 21 || item->serial != 4)
    return 1;
  ++item;
  return item == destination.cend() || item->code != 22 || item->serial != 5;
}
)r19containers");
  const auto Output = tmpFile("forward-list-trivial-record-copy-assignment.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-trivial-record-copy-assignment" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListWiderRecordRangeAndAssignmentRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-wider-record-range-and-assignment.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry { int first; int second; int third; };
int main() {
  Entry input[] = {{2, 5, 8}, {3, 6, 9}, {7, 10, 13}};
  std::forward_list<Entry> values(input, input + 2);
  values.assign(input + 1, input + 3);
  auto item = values.begin();
  if (item == values.end() || item->first != 3 || item->second != 6 || item->third != 9)
    return 1;
  ++item;
  if (item == values.end() || item->first != 7 || item->second != 10 || item->third != 13)
    return 2;
  ++item;
  return item != values.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-wider-record-range-and-assignment.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-wider-record-range-and-assignment" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListMixedTrivialRecordCopyAndSpliceRunAtBothOptimizations) {
  const auto Source = tmpFile("list-mixed-trivial-record-copy-and-splice.cpp");
  writeFile(Source, R"r19containers(#include <list>
enum class Kind : unsigned short { item = 3, saved = 29 };
struct Entry { Kind kind; double score; int* target; };
int main() {
  int value = 11;
  Entry entry{Kind::item, 2.25, &value};
  std::list<Entry> source;
  source.push_back(entry);
  std::list<Entry> copy(source);
  auto saved = copy.begin();
  Entry* address = &*saved;
  std::list<Entry> destination;
  destination.splice(destination.end(), copy);
  source.front().kind = Kind::saved;
  *saved->target = 41;
  return !copy.empty() || destination.size() != 1 ||
         &destination.front() != address || destination.front().kind != Kind::item ||
         destination.front().score != 2.25 || destination.front().target != &value ||
         source.front().kind != Kind::saved || value != 41;
}
)r19containers");
  const auto Output = tmpFile("list-mixed-trivial-record-copy-and-splice.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-mixed-trivial-record-copy-and-splice" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListMixedTrivialRecordReverseAndEraseRunAtBothOptimizations) {
  const auto Source = tmpFile("list-mixed-trivial-record-reverse-and-erase.cpp");
  writeFile(Source, R"r19containers(#include <list>
enum class Category : signed char { low = -9, high = 11 };
struct Entry { char key; Category category; float weight; double cost; };
int main() {
  std::list<Entry> values{{'a', Category::low, 1.5f, 4.25},
                          {'b', Category::high, 2.0f, 8.5}};
  Entry* address = &values.front();
  values.reverse();
  if (values.begin()->key != 'b' || &values.back() != address)
    return 1;
  auto remaining = values.erase(values.begin());
  return remaining != values.begin() || &*remaining != address ||
         remaining->category != Category::low || remaining->weight != 1.5f ||
         remaining->cost != 4.25;
}
)r19containers");
  const auto Output = tmpFile("list-mixed-trivial-record-reverse-and-erase.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-mixed-trivial-record-reverse-and-erase" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListNestedTrivialRecordValueInitializationRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-nested-trivial-record-value-initialization.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Point { int x; int y; };
struct Atom { Point position; unsigned tag; };
struct Element { Atom value; long long id; };
int main() {
  std::forward_list<Element> values;
  auto& extra = values.emplace_front();
  if (extra.value.position.x || extra.value.position.y || extra.value.tag || extra.id)
    return 1;
  Element value{{{3, 5}, 7}, 11};
  values.push_front(value);
  auto cursor = values.begin();
  if (cursor->value.position.y != 5 || cursor->value.tag != 7 || cursor->id != 11)
    return 2;
  ++cursor;
  return &*cursor != &extra || cursor->value.position.x != 0;
}
)r19containers");
  const auto Output = tmpFile("forward-list-nested-trivial-record-value-initialization.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-nested-trivial-record-value-initialization" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordFixedArrayValueCopyRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-fixed-array-value-copy.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry { long long tag; short samples[3]; };
int main() {
  std::list<Entry> values{{17, {3, 5, 7}}};
  auto saved = values.begin();
  short* address = saved->samples;
  std::list<Entry> copy(values);
  copy.front().samples[1] = 41;
  values.emplace_back();
  return values.size() != 2 || saved->samples != address || saved->samples[1] != 5 ||
         copy.front().samples[0] != 3 || copy.front().samples[1] != 41 ||
         copy.front().samples[2] != 7 || copy.front().tag != 17 ||
         values.back().tag != 0 || values.back().samples[2] != 0;
}
)r19containers");
  const auto Output = tmpFile("list-record-fixed-array-value-copy.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-fixed-array-value-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordArrayRangeAndSpliceRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-array-range-and-splice.cpp");
  writeFile(Source, R"r19containers(#include <list>
enum class Code : unsigned short { first = 5, second = 12 };
struct Entry { Code options[2]; int id; };
int main() {
  Entry input[] = {{{Code::first, Code::second}, 17}, {{Code::second, Code::first}, 29}};
  std::list<Entry> source(input, input + 2), destination;
  auto moved = source.begin();
  const Code* address = moved->options;
  destination.splice(destination.end(), source, moved);
  return source.size() != 1 || source.front().id != 29 ||
         destination.size() != 1 || destination.front().id != 17 ||
         destination.front().options != address || moved->options[1] != Code::second;
}
)r19containers");
  const auto Output = tmpFile("list-record-array-range-and-splice.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-array-range-and-splice" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRunsStoredRecordDestructors) {
  const auto Source = tmpFile("list-nontrivial-record-destruction-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry {
  int code;
  ~Entry() { code = 0; }
};
int main() {
  std::list<Entry> values;
  values.emplace_back().code = 9;
  return values.back().code != 9;
}
)r19containers");
  const auto Output = tmpFile("list-nontrivial-record-destruction-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-nontrivial-record-destruction-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListOveralignedRecordGuardRejected) {
  const auto Source = tmpFile("forward-list-overaligned-record-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct alignas(16) Entry { int code; };
int main() {
  std::forward_list<Entry> values;
  values.emplace_front().code = 4;
  return values.front().code != 4;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListWiderRecordAlignmentGuardRejected) {
  const auto Source = tmpFile("list-wider-record-alignment-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct alignas(16) Entry { long long first; long long second; int third; };
int main() {
  std::list<Entry> values;
  values.emplace_back().third = 17;
  return values.back().third != 17;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListRecordFunctionPointerFieldGuardRejected) {
  const auto Source = tmpFile("forward-list-record-function-pointer-field-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int identity(int value) { return value; }
struct Entry { int key; int (*callback)(int); };
int main() {
  std::forward_list<Entry> values{{9, &identity}};
  return values.front().callback(values.front().key) != 9;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListNestedPackedRecordLayoutGuardRejected) {
  const auto Source = tmpFile("forward-list-nested-packed-record-layout-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
#pragma pack(push, 1)
struct Inner { char first; int second; };
#pragma pack(pop)
struct Element { int tag; Inner value; };
int main() {
  std::forward_list<Element> values{{7, {'x', 11}}};
  return values.front().value.second != 11;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListRecordArrayOfFunctionPointersGuardRejected) {
  const auto Source = tmpFile("list-record-array-of-function-pointers-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int identity(int value) { return value; }
struct Entry { int (*methods[2])(int); };
int main() {
  std::list<Entry> values{{{&identity, &identity}}};
  return values.front().methods[1](5) != 5;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListRecordVolatileArrayValueGuardRejected) {
  const auto Source = tmpFile("list-record-volatile-array-value-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry { volatile short signs[2]; int id; };
int main() {
  std::list<Entry> values;
  auto& entry = values.emplace_back();
  entry.signs[0] = 13;
  return values.back().signs[0] != 13;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListRecordUniqueBorrowsAdjacentValuesRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-unique-borrows-adjacent-values.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry { short code; short tag; };
bool same_code(const Entry& left, const Entry& right) {
  return left.code == right.code;
}
int main() {
  std::list<Entry> values{{2, 11}, {2, 13}, {5, 17}, {5, 19}};
  auto first = values.begin();
  bool (*compare)(const Entry&, const Entry&) = &same_code;
  values.unique(compare);
  return values.size() != 2 || &*first != &values.front() ||
         values.front().tag != 11 || values.back().tag != 17;
}
)r19containers");
  const auto Output = tmpFile("list-record-unique-borrows-adjacent-values.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-unique-borrows-adjacent-values" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListRecordUniqueKeepsFirstOfRunRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-record-unique-keeps-first-of-run.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry { int code; int tag; };
bool same_code(const Entry& left, const Entry& right) {
  return left.code == right.code;
}
int main() {
  std::forward_list<Entry> values{{3, 11}, {3, 13}, {5, 17}, {3, 19}};
  auto first = values.begin();
  values.unique(&same_code);
  auto position = values.begin();
  if (&*position != &*first || position->tag != 11) return 1;
  ++position;
  if (position->tag != 17) return 2;
  ++position;
  return position->tag != 19 || ++position != values.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-unique-keeps-first-of-run.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-unique-keeps-first-of-run" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordFieldStableMergeRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-field-stable-merge.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Item { int rank; int id; };
bool ascending(const Item& left, const Item& right) { return left.rank < right.rank; }
int main() {
  std::list<Item> into{{1, 11}, {3, 13}, {5, 17}};
  std::list<Item> from{{2, 19}, {3, 23}, {7, 29}};
  auto retained = from.begin();
  into.merge(from, ascending);
  auto item = into.begin();
  if (!from.empty() || into.size() != 6 || item->id != 11) return 1;
  if (++item != retained || item->id != 19 || (++item)->id != 13) return 2;
  return (++item)->id != 23 || (++item)->id != 17 ||
         (++item)->id != 29 || ++item != into.end();
}
)r19containers");
  const auto Output = tmpFile("list-record-field-stable-merge.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-field-stable-merge" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListRecordFieldStableMergeRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-record-field-stable-merge.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Item { unsigned rank; short id; };
bool descending(const Item& left, const Item& right) { return left.rank > right.rank; }
int main() {
  std::forward_list<Item> into{{7, 11}, {4, 13}, {3, 17}};
  std::forward_list<Item> from{{6, 19}, {4, 23}, {1, 29}};
  auto moved = from.begin();
  into.merge(from, &descending);
  auto item = into.begin();
  if (!from.empty() || item->id != 11 ||
      ++item != moved || item->id != 19 || (++item)->id != 13) return 1;
  return (++item)->id != 23 || (++item)->id != 17 ||
         (++item)->id != 29 || ++item != into.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-field-stable-merge.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-field-stable-merge" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListDefaultRecordOrderStableSortRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-default-record-order-stable-sort.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Item { short rank; short id; };
bool operator<(const Item& left, const Item& right) { return left.rank < right.rank; }
int main() {
  std::forward_list<Item> values{{4, 11}, {2, 13}, {4, 17}};
  auto saved = values.begin();
  values.sort();
  auto item = values.begin();
  if (item->id != 13 || ++item != saved || item->id != 11) return 1;
  return (++item)->id != 17 || ++item != values.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-default-record-order-stable-sort.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-default-record-order-stable-sort" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListSortUsesConstRecordMember) {
  const auto Source = tmpFile("list-record-member-default-order-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Item {
  int rank;
  bool operator<(const Item& other) const { return rank < other.rank; }
};
int main() {
  std::list<Item> values{{7}, {2}};
  values.sort();
  return values.front().rank != 2;
}
)r19containers");
  const auto Output = tmpFile("list-record-member-default-order-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-member-default-order-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListSortUsesCompoundRecordComparison) {
  const auto Source = tmpFile("forward-list-record-compound-default-order-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Item { int rank; };
bool operator<(const Item& left, const Item& right) { return !(left.rank >= right.rank); }
int main() {
  std::forward_list<Item> values{{7}, {2}};
  values.sort();
  return values.front().rank != 2;
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-compound-default-order-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-compound-default-order-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListScalarReferencePredicateGuardRejected) {
  const auto Source = tmpFile("forward-list-scalar-reference-predicate-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
bool odd(const int& value) { return value % 2 != 0; }
int main() {
  std::forward_list<int> values{1, 2};
  values.remove_if(&odd);
  return values.front() != 2;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListRecordRemoveWithAliasedValueRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-remove-with-aliased-value.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry { int code; int tag; };
bool operator==(const Entry& left, const Entry& right) { return left.code == right.code; }
int main() {
  std::list<Entry> values{{2, 11}, {5, 13}, {2, 17}, {7, 19}};
  const auto& value = values.front();
  auto kept = values.begin();
  ++kept;
  const auto* address = &*kept;
  values.remove(value);
  return values.size() != 2 || &values.front() != address ||
         values.front().tag != 13 || values.back().tag != 19;
}
)r19containers");
  const auto Output = tmpFile("list-record-remove-with-aliased-value.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-remove-with-aliased-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListRecordRemoveWithAliasedValueRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-record-remove-with-aliased-value.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry { unsigned code; int tag; };
bool operator==(const Entry& left, const Entry& right) { return left.code == right.code; }
int main() {
  std::forward_list<Entry> values{{5, 11}, {2, 13}, {2, 17}, {7, 19}, {2, 23}};
  auto removed = values.begin();
  ++removed;
  const auto& value = *removed;
  auto saved = values.begin();
  values.remove(value);
  auto item = values.begin();
  if (item != saved || item->tag != 11) return 1;
  return (++item)->tag != 19 || ++item != values.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-remove-with-aliased-value.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-remove-with-aliased-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordOwnerEqualityRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-owner-equality.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry { unsigned short key; int tag; };
bool operator==(const Entry& left, const Entry& right) { return left.key == right.key; }
int main() {
  const std::list<Entry> left{{1, 7}, {3, 5}}, same{{1, 0}, {3, 2}};
  const std::list<Entry> shorter{{1, 7}}, different{{1, 7}, {2, 5}};
  return !(left == same) || left != same || left == shorter || !(left != different);
}
)r19containers");
  const auto Output = tmpFile("list-record-owner-equality.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-owner-equality" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListRecordOwnerEqualityRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-record-owner-equality.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry { short key; int tag; };
bool operator==(const Entry& left, const Entry& right) { return left.key == right.key; }
int main() {
  const std::forward_list<Entry> left{{1, 7}, {-3, 5}}, same{{1, 8}, {-3, 2}};
  const std::forward_list<Entry> empty, prefix{{1, 7}}, different{{-3, 7}, {1, 5}};
  return !(left == same) || left != same || !(empty == empty) ||
         left == prefix || prefix == left || !(left != different);
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-owner-equality.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-owner-equality" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordFloatingFieldStableSortRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-floating-field-stable-sort.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry { float key; int tag; };
bool comesBefore(const Entry& left, const Entry& right) { return left.key < right.key; }
int main() {
  std::list<Entry> data{{2.5f, 1}, {-0.0f, 2}, {2.5f, 3}, {0.0f, 4}};
  const auto* first = &data.front();
  data.sort(comesBefore);
  auto item = data.begin();
  if (item->tag != 2 || (++item)->tag != 4) return 1;
  return &*(++item) != first || item->tag != 1 || (++item)->tag != 3 || ++item != data.end();
}
)r19containers");
  const auto Output = tmpFile("list-record-floating-field-stable-sort.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-floating-field-stable-sort" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordPointerFieldEqualityWithAliasRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-pointer-field-equality-with-alias.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry { const int* key; int tag; };
bool operator==(const Entry& left, const Entry& right) { return left.key == right.key; }
int main() {
  const int keys[]{4, 4};
  std::list<Entry> data{{keys, 7}, {keys + 1, 9}, {keys, 5}, {nullptr, 6}};
  const auto& value = data.front();
  data.remove(value);
  auto item = data.begin();
  return data.size() != 2 || item->tag != 9 || (++item)->tag != 6;
}
)r19containers");
  const auto Output = tmpFile("list-record-pointer-field-equality-with-alias.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-pointer-field-equality-with-alias" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListUniqueUsesConstRecordMember) {
  const auto Source = tmpFile("list-record-member-equality-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry {
  int code;
  bool operator==(const Entry& other) const { return code == other.code; }
};
int main() {
  std::list<Entry> values{{2}, {2}, {4}};
  values.unique();
  return values.size() != 2;
}
)r19containers");
  const auto Output = tmpFile("list-record-member-equality-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-member-equality-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListUniqueUsesCompoundRecordEquality) {
  const auto Source = tmpFile("forward-list-record-compound-equality-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry { int code; int tag; };
bool operator==(const Entry& left, const Entry& right) {
  return left.code == right.code && left.tag == right.tag;
}
int main() {
  std::forward_list<Entry> values{{2, 11}, {2, 13}, {2, 13}};
  values.remove(Entry{2, 13});
  return values.front().tag != 11;
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-compound-equality-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-compound-equality-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListUsesCompoundFloatingRecordEquality) {
  const auto Source = tmpFile("list-record-compound-floating-equality-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry { float key; int tag; };
bool operator==(const Entry& left, const Entry& right) {
  return left.key == right.key && left.tag == right.tag;
}
int main() { return std::list<Entry>{{2.0f, 1}} == std::list<Entry>{{2.0f, 1}}; }
)r19containers");
  const auto Output = tmpFile("list-record-compound-floating-equality-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-compound-floating-equality-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 1) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordLiteralDefaultInsertionRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-literal-default-insertion.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry { int key = 7; float weight = 0.5f; int count; };
int main() {
  std::list<Entry> values(2);
  values.front().count = 9;
  values.resize(4);
  values.emplace_front().key = 5;
  auto i = values.begin();
  if (i->key != 5 || i->weight != 0.5f || i->count != 0 || (++i)->count != 9) return 1;
  return values.size() != 5 || values.back().key != 7 ||
         values.back().weight != 0.5f || values.back().count != 0;
}
)r19containers");
  const auto Output = tmpFile("list-record-literal-default-insertion.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-literal-default-insertion" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordLiteralDefaultCopyRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-literal-default-copy.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry { int key = 7; char tag = 'r'; };
int main() {
  Entry selected{9};
  std::list<Entry> values(2, selected);
  values.emplace_back(Entry{});
  values.emplace(values.begin(), Entry{5, 'x'});
  return values.size() != 4 || values.front().tag != 'x' ||
         (++values.begin())->key != 9 || values.back().key != 7 || values.back().tag != 'r';
}
)r19containers");
  const auto Output = tmpFile("list-record-literal-default-copy.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-literal-default-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListEvaluatesEarlierRecordMemberDefaults) {
  const auto Source = tmpFile("forward-list-record-member-dependent-default-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry { int key; int count = key + 1; };
int main() { std::forward_list<Entry> values(1); return values.front().count != 1; }
)r19containers");
  const auto Output = tmpFile("forward-list-record-member-dependent-default-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-member-dependent-default-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordMemberOwnerOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-member-owner-order.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry {
  int key;
  int tag;
  bool operator<(const Entry& other) const & { return key < other.key; }
};
int main() {
  const std::list<Entry> left{{1, 6}, {2, 7}}, right{{1, 8}, {3, 9}}, prefix{{1, 0}};
  return !(left < right) || !(right > left) || right <= left ||
         !(prefix < left) || !(left >= prefix);
}
)r19containers");
  const auto Output = tmpFile("list-record-member-owner-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-member-owner-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListRecordMemberOwnerOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-record-member-owner-order.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry {
  short key;
  int tag;
  bool operator<(const Entry& other) const { return this->key > other.key; }
};
int main() {
  const std::forward_list<Entry> left{{8, 6}, {4, 7}}, right{{8, 8}, {2, 9}}, prefix{{8, 0}};
  return !(left < right) || !(right > left) || right <= left ||
         !(prefix < left) || !(left >= prefix);
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-member-owner-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-member-owner-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListRecordMemberStableSortAndMergeRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-record-member-stable-sort-and-merge.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry {
  unsigned key;
  int tag;
  bool operator<(const Entry& rhs) const { return key < rhs.key; }
};
int main() {
  std::forward_list<Entry> left{{3, 7}, {1, 5}, {3, 6}}, right{{1, 8}, {3, 9}};
  auto original = left.begin();
  left.sort();
  left.merge(right);
  auto it = left.begin();
  if (!right.empty() || it->tag != 5 || (++it)->tag != 8) return 1;
  return ++it != original || (++it)->tag != 6 || (++it)->tag != 9 || ++it != left.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-member-stable-sort-and-merge.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-member-stable-sort-and-merge" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListOwnersUseCompoundRecordMemberOrder) {
  const auto Source = tmpFile("list-record-compound-member-owner-order-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry {
  int key;
  int tag;
  bool operator<(const Entry& other) const {
    return key < other.key || (key == other.key && tag < other.tag);
  }
};
int main() { return std::list<Entry>{{1, 2}} < std::list<Entry>{{1, 3}}; }
)r19containers");
  const auto Output = tmpFile("list-record-compound-member-owner-order-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-compound-member-owner-order-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 1) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListNormalizesIntegralRecordEquality) {
  const auto Source = tmpFile("forward-list-record-integer-member-owner-equality-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry {
  int key;
  int operator==(const Entry& other) const { return key == other.key; }
};
int main() { return std::forward_list<Entry>{{2}} == std::forward_list<Entry>{{3}}; }
)r19containers");
  const auto Output = tmpFile("forward-list-record-integer-member-owner-equality-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-integer-member-owner-equality-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordMemberLexicographicSortRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-member-lexicographic-sort.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry {
  int rank;
  int tag;
  int value;
  bool operator<(const Entry& other) const {
    return rank < other.rank || (rank == other.rank && tag < other.tag);
  }
};
int main() {
  std::list<Entry> values{{4, 1, 12}, {2, 8, 6}, {2, 4, 5}, {2, 4, 3}};
  auto retained = values.begin();
  values.sort();
  auto it = values.begin();
  if (it->value != 5) return 1;
  ++it;
  if (it->value != 3) return 2;
  ++it;
  if (it->value != 6) return 3;
  ++it;
  return it != retained || it->value != 12;
}
)r19containers");
  const auto Output = tmpFile("list-record-member-lexicographic-sort.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-member-lexicographic-sort" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordByValueDescendingSortRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-by-value-descending-sort.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Row { long key; char value; };
bool after(Row left, Row right) { return left.key > right.key; }
int main() {
  std::list<Row> rows{{3, 8}, {5, 4}, {3, 9}};
  auto first = rows.begin();
  rows.sort(&after);
  auto it = rows.begin();
  if (it->key != 5) return 1;
  ++it;
  return it != first || it->value != 8 || rows.back().value != 9;
}
)r19containers");
  const auto Output = tmpFile("list-record-by-value-descending-sort.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-by-value-descending-sort" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordByValueMismatchedFieldGuardRejected) {
  const auto Source = tmpFile("list-record-by-value-mismatched-field-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Row { int key; int tag; };
bool before(Row left, Row right) { return left.key < right.tag; }
int main() {
  std::list<Row> rows{{2, 6}, {1, 4}};
  rows.sort(&before);
  return rows.empty();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListRecordByValueVolatileParameterGuardRejected) {
  const auto Source = tmpFile("list-record-by-value-volatile-parameter-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Row { int key; };
bool before(volatile Row left, Row right) { return left.key < right.key; }
int main() {
  std::list<Row> rows{{2}, {1}};
  rows.sort(&before);
  return rows.front().key != 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListRecordNestedDestructionOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-record-nested-destruction-order.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int trace;
struct Leaf {
  int value;
  ~Leaf() { trace = trace * 10 + value; }
};
struct Row { Leaf first; Leaf second; };
int main() {
  std::forward_list<Row> rows;
  auto& later = rows.emplace_front();
  later.first.value = 3;
  later.second.value = 4;
  auto& earlier = rows.emplace_front();
  earlier.first.value = 1;
  earlier.second.value = 2;
  rows.pop_front();
  if (trace != 21) return 1;
  rows.clear();
  return trace != 2143 || !rows.empty();
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-nested-destruction-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-nested-destruction-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListRecordDestructorWithCustomCopyGuardRejected) {
  const auto Source = tmpFile("forward-list-record-destructor-with-custom-copy-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int copied;
struct Entry {
  int value;
  Entry() = default;
  Entry(const Entry& other) : value(other.value) { ++copied; }
  ~Entry() { value = 0; }
};
int main() {
  std::forward_list<Entry> rows;
  rows.emplace_front().value = 2;
  auto copy = rows;
  return copy.front().value != 2 || copied != 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListRecordDependentDefaultEmplacementRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-record-dependent-default-emplacement.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry { int key = -2; int tag = -key; bool selected = key < 0 && tag == 2; };
int main() {
  std::forward_list<Entry> rows;
  auto it = rows.emplace_after(rows.before_begin());
  if (!it->selected || it->tag != 2) return 1;
  rows.emplace_front().key = 8;
  return rows.front().key != 8 || rows.front().tag != 2 || !rows.front().selected;
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-dependent-default-emplacement.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-dependent-default-emplacement" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordIntegralDefaultSortResultRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-integral-default-sort-result.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry {
  int key;
  int id;
  long operator<(const Entry& other) const { return key < other.key; }
};
int main() {
  std::list<Entry> values{{3, 4}, {1, 2}, {1, 7}};
  auto last = values.begin();
  values.sort();
  auto it = values.begin();
  if (it->id != 2 || (++it)->id != 7) return 1;
  return ++it != last || it->id != 4;
}
)r19containers");
  const auto Output = tmpFile("list-record-integral-default-sort-result.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-integral-default-sort-result" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListNormalizesFloatingRecordEquality) {
  const auto Source = tmpFile("list-record-floating-comparison-result-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry {
  int key;
  double operator==(const Entry& other) const { return key == other.key; }
};
int main() {
  const std::list<Entry> first{{2}};
  const std::list<Entry> second{{3}};
  return first == second;
}
)r19containers");
  const auto Output = tmpFile("list-record-floating-comparison-result-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-floating-comparison-result-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListRecordLaterFieldDefaultGuardRejected) {
  const auto Source = tmpFile("forward-list-record-later-field-default-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry { int second = first + 2; int first = 4; };
int main() {
  std::forward_list<Entry> rows(1);
  return rows.front().first != 4;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListRecordDefaultCallResizeRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-default-call-resize.cpp");
  writeFile(Source, R"r19containers(#include <list>
int calls;
int next() { return ++calls; }
struct Entry { int serial = next(); int checksum = serial * 10; };
int main() {
  std::list<Entry> rows(2);
  auto first = rows.begin();
  rows.resize(4);
  if (calls != 4 || first != rows.begin() || first->checksum != 10 || rows.back().checksum != 40) return 1;
  rows.resize(1);
  return calls != 4 || first != rows.begin() || rows.back().serial != 1;
}
)r19containers");
  const auto Output = tmpFile("list-record-default-call-resize.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-default-call-resize" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordDefaultCallAggregateSuppressionRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-default-call-aggregate-suppression.cpp");
  writeFile(Source, R"r19containers(#include <list>
int calls;
int next() { return ++calls; }
struct Entry { int id = next(); int tag = id + 3; };
int main() {
  Entry values[]{{9}, {}};
  std::list<Entry> rows(values, values + 2);
  return calls != 1 || rows.front().id != 9 || rows.front().tag != 12 ||
         rows.back().id != 1 || rows.back().tag != 4;
}
)r19containers");
  const auto Output = tmpFile("list-record-default-call-aggregate-suppression.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-default-call-aggregate-suppression" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordIndirectDefaultCallGuardRejected) {
  const auto Source = tmpFile("list-record-indirect-default-call-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int seed = 0;
int next() { return ++seed; }
int (*selected)() = next;
struct Entry { int id = selected(); };
int main() {
  std::list<Entry> rows(2);
  return rows.back().id != 2;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListRecordDefaultCallAndDestructionRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-record-default-call-and-destruction.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int calls, released;
int next() { return ++calls; }
struct Entry {
  int key = next();
  int tag = key + 3;
  ~Entry() noexcept { released += tag; }
};
int main() {
  std::forward_list<Entry> rows(1);
  auto first = rows.begin();
  rows.resize(3);
  if (calls != 3 || first != rows.begin() || first->tag != 4) return 1;
  rows.pop_front();
  if (released != 4 || rows.front().tag != 5) return 1;
  rows.clear();
  return calls != 3 || released != 15 || !rows.empty();
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-default-call-and-destruction.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-default-call-and-destruction" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListRecordDefaultAndNestedDestructionOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-record-default-and-nested-destruction-order.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int order;
struct Child {
  int id;
  ~Child() noexcept { order = order * 10 + id; }
};
struct Entry {
  int tag = 3;
  Child first;
  Child second;
  ~Entry() noexcept { order = order * 10 + tag; }
};
int main() {
  std::forward_list<Entry> rows(1);
  rows.front().first.id = 1;
  rows.front().second.id = 2;
  rows.clear();
  return order != 321;
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-default-and-nested-destruction-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-default-and-nested-destruction-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListRecordDoubleDefaultUniqueRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-record-double-default-unique.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry {
  int key;
  int id;
  double operator==(const Entry& other) const { return key == other.key; }
};
int main() {
  std::forward_list<Entry> rows{{2, 0}, {2, 1}, {3, 2}, {3, 3}};
  auto first = rows.begin();
  auto third = first;
  ++third;
  ++third;
  rows.unique();
  auto after = rows.begin();
  ++after;
  return rows.begin() != first || after != third || (++after) != rows.end();
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-double-default-unique.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-double-default-unique" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordDefaultWithThrowingDestructorGuardRejected) {
  const auto Source = tmpFile("list-record-default-with-throwing-destructor-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry { int key = 4; ~Entry() noexcept(false) {} };
int main() {
  std::list<Entry> rows(1);
  return rows.front().key != 4;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListRecordLongDoubleComparisonResultGuardRejected) {
  const auto Source = tmpFile("list-record-long-double-comparison-result-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry {
  int key;
  long double operator==(const Entry& other) const { return key == other.key; }
};
int main() {
  const std::list<Entry> first{{2}};
  const std::list<Entry> second{{3}};
  return first == second;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListDefaultConstructionCountAndResizeRunAtBothOptimizations) {
  const auto Source = tmpFile("list-default-construction-count-and-resize.cpp");
  writeFile(Source, R"r19containers(#include <list>
int calls;
int next() { return ++calls; }
struct Entry { int serial = next(); int checksum = serial + 4; };
int main() {
  std::list<Entry> empty(0);
  empty.resize(0);
  if (calls || !empty.empty()) return 1;
  std::list<Entry> rows(2);
  auto first = rows.begin();
  rows.resize(2);
  if (calls != 2 || first->checksum != 5 || rows.back().checksum != 6) return 2;
  rows.resize(4);
  if (calls != 4 || first != rows.begin() || rows.back().checksum != 8) return 3;
  rows.resize(1);
  return calls != 4 || first != rows.begin() || rows.back().checksum != 5;
}
)r19containers");
  const auto Output = tmpFile("list-default-construction-count-and-resize.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-default-construction-count-and-resize" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListDefaultEmplacementAndExplicitFillRunAtBothOptimizations) {
  const auto Source = tmpFile("list-default-emplacement-and-explicit-fill.cpp");
  writeFile(Source, R"r19containers(#include <list>
int calls;
int next() { return ++calls; }
struct Entry { int id = next(); int data = id + 10; };
int main() {
  Entry value{9};
  std::list<Entry> rows(2, value);
  rows.resize(3, value);
  if (calls || rows.size() != 3 || rows.back().data != 19) return 1;
  auto first = rows.emplace(rows.begin());
  auto& last = rows.emplace_back();
  return calls != 2 || first != rows.begin() || first->data != 11 || last.data != 12;
}
)r19containers");
  const auto Output = tmpFile("list-default-emplacement-and-explicit-fill.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-default-emplacement-and-explicit-fill" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListNodeDefaultIndirectCallGuardRejected) {
  const auto Source = tmpFile("list-node-default-indirect-call-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int count;
int next() { return ++count; }
int (*stamp)() = next;
struct Entry { int id = stamp(); };
int main() {
  std::list<Entry> rows(2);
  rows.resize(3);
  return rows.back().id != 3;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListRecordDefaultCallScalarEarlierFieldRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-default-call-scalar-earlier-field.cpp");
  writeFile(Source, R"r19containers(#include <list>
int calls;
int offset(int amount) { return amount + ++calls; }
int tag(int id, int code) { return id * 10 + code; }
struct Entry { int id = offset(3); int checksum = tag(id, 5); };
int main() {
  std::list<Entry> rows(0);
  if (calls != 0) return 1;
  rows.resize(2);
  auto first = rows.begin();
  if (calls != 2 || first->id != 4 || first->checksum != 45 || rows.back().checksum != 55) return 1;
  rows.emplace_back();
  return calls != 3 || first != rows.begin() || rows.back().checksum != 65;
}
)r19containers");
  const auto Output = tmpFile("list-record-default-call-scalar-earlier-field.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-default-call-scalar-earlier-field" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordDefaultCallFloatingScalarRunAtBothOptimizations) {
  const auto Source = tmpFile("list-record-default-call-floating-scalar.cpp");
  writeFile(Source, R"r19containers(#include <list>
int calls;
double measure(double base = 1.5) { return base * ++calls; }
struct Entry { double value = measure(); double doubled = measure(value); };
int main() {
  std::list<Entry> rows(2);
  auto first = rows.begin();
  if (calls != 4 || first->value != 1.5 || first->doubled != 3.0 ||
      rows.back().value != 4.5 || rows.back().doubled != 18.0) return 1;
  rows.resize(1);
  return calls != 4 || first != rows.begin() || rows.back().doubled != 3.0;
}
)r19containers");
  const auto Output = tmpFile("list-record-default-call-floating-scalar.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-record-default-call-floating-scalar" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListEvaluatesSelectedDefaultArgumentAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-record-default-call-nonliteral-default-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int calls;
int seed() { return ++calls; }
int tag(int value = seed()) { return value; }
struct Entry { int checksum = tag(); };
int main() {
  std::forward_list<Entry> rows(1);
  return rows.front().checksum != 1 || calls != 1;
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-default-call-nonliteral-default-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-default-call-nonliteral-default-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListRecordSelectedDefaultCallExpressionRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-record-selected-default-call-expression.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int calls;
int seed() { return ++calls; }
int code(int value = seed() * 2 + 3) { return value; }
struct Entry { int checksum = code(); };
int main() {
  std::forward_list<Entry> rows(0);
  if (calls || !rows.empty()) return 1;
  rows.resize(2);
  auto first = rows.begin();
  auto second = first;
  ++second;
  if (calls != 2 || first->checksum != 5 || second->checksum != 7) return 1;
  rows.emplace_after(second, Entry{21});
  return calls != 2 || first != rows.begin() || rows.emplace_front().checksum != 9;
}
)r19containers");
  const auto Output = tmpFile("forward-list-record-selected-default-call-expression.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-record-selected-default-call-expression" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListRecordSelectedDefaultIndirectCallGuardRejected) {
  const auto Source = tmpFile("list-record-selected-default-indirect-call-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
int calls;
int seed() { return ++calls; }
int (*initial)() = seed;
int tag(int value = initial()) { return value; }
struct Entry { int checksum = tag(); };
int main() {
  std::list<Entry> rows(1);
  return calls != 1 || rows.front().checksum != 1;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListNestedRecordDefaultAndDestroyOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("list-nested-record-default-and-destroy-order.cpp");
  writeFile(Source, R"r19containers(#include <list>
int sequence, released;
int next() { return ++sequence; }
struct Leaf {
  int value = next();
  int mirrored = value + 10;
  ~Leaf() noexcept { released = released * 10 + value; }
};
struct Entry { int prefix = next(); Leaf leaf; int suffix = next(); };
int main() {
  std::list<Entry> rows(1);
  if (sequence != 3 || rows.front().prefix != 1 || rows.front().leaf.mirrored != 12 ||
      rows.front().suffix != 3) return 1;
  rows.resize(2);
  if (sequence != 6 || rows.back().prefix != 4 || rows.back().leaf.mirrored != 15 ||
      rows.back().suffix != 6) return 1;
  rows.pop_back();
  if (released != 5) return 1;
  rows.clear();
  return released != 52;
}
)r19containers");
  const auto Output = tmpFile("list-nested-record-default-and-destroy-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-nested-record-default-and-destroy-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListDeepRecordOnlyNestedDefaultsRunAtBothOptimizations) {
  const auto Source = tmpFile("list-deep-record-only-nested-defaults.cpp");
  writeFile(Source, R"r19containers(#include <list>
int sequence;
int next() { return ++sequence; }
struct Leaf { int key = next(); int mirror = key + 3; };
struct Middle { Leaf leaf; };
struct Entry { Middle inner; };
int main() {
  std::list<Entry> rows(1);
  if (sequence != 1 || rows.front().inner.leaf.mirror != 4) return 1;
  rows.emplace_back();
  return sequence != 2 || rows.back().inner.leaf.mirror != 5;
}
)r19containers");
  const auto Output = tmpFile("list-deep-record-only-nested-defaults.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-deep-record-only-nested-defaults" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListNestedIndirectDefaultCallGuardRejected) {
  const auto Source = tmpFile("forward-list-nested-indirect-default-call-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int next() { return 7; }
int (*getValue)() = next;
struct Leaf { int value = getValue(); };
struct Entry { Leaf field; };
int main() {
  std::forward_list<Entry> rows(1);
  return rows.front().field.value != 7;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListNestedUserDefaultConstructorRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-nested-user-default-constructor.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
int sequence, released;
int next() { return ++sequence; }
struct Leaf {
  int ticket;
  Leaf() noexcept : ticket(++sequence) {}
  ~Leaf() noexcept { released = released * 10 + ticket; }
};
struct Entry { int before = next(); Leaf fields[2]; int after = next(); };
int main() {
  std::forward_list<Entry> rows;
  auto& row = rows.emplace_front();
  if (sequence != 4 || row.before != 1 || row.fields[0].ticket != 2 ||
      row.fields[1].ticket != 3 || row.after != 4) return 1;
  rows.clear();
  return released != 32;
}
)r19containers");
  const auto Output = tmpFile("forward-list-nested-user-default-constructor.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-nested-user-default-constructor" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListUserDefaultConstructorDestructionRunAtBothOptimizations) {
  const auto Source = tmpFile("list-user-default-constructor-destruction.cpp");
  writeFile(Source, R"r19containers(#include <list>
int sequence, released;
struct Entry {
  int value;
  Entry() noexcept : value(++sequence) {}
  ~Entry() noexcept { released = released * 10 + value; }
};
int main() {
  std::list<Entry> rows;
  auto& first = rows.emplace_back();
  auto& second = rows.emplace_front();
  if (first.value != 1 || second.value != 2 || released) return 1;
  rows.pop_front();
  if (released != 2) return 1;
  rows.clear();
  return released != 21;
}
)r19containers");
  const auto Output = tmpFile("list-user-default-constructor-destruction.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-user-default-constructor-destruction" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListUserDefaultConstructorMoveGuardRejected) {
  const auto Source = tmpFile("forward-list-user-default-constructor-move-guard.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Entry {
  int value;
  Entry() noexcept : value(7) {}
  Entry(const Entry&) = default;
  Entry(Entry&& other) noexcept : value(other.value + 1) {}
};
int main() {
  std::forward_list<Entry> rows(1);
  return rows.front().value != 7;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListPlainDefaultCtorNestedFieldDefaultsRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-plain-default-ctor-nested-field-defaults.cpp");
  writeFile(Source, R"r19containers(#include <forward_list>
struct Leaf {
  int key = 7;
  int mirror = key + 3;
  Leaf() {}
};
struct Entry { Leaf values[2]; };
int main() {
  std::forward_list<Entry> rows;
  auto& current = rows.emplace_front();
  if (current.values[0].key != 7 || current.values[1].mirror != 10) return 1;
  rows.resize(2);
  auto after = rows.begin();
  ++after;
  return after->values[1].mirror != 10;
}
)r19containers");
  const auto Output = tmpFile("forward-list-plain-default-ctor-nested-field-defaults.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-plain-default-ctor-nested-field-defaults" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListPlainDefaultCtorBodyGuardRejected) {
  const auto Source = tmpFile("list-plain-default-ctor-body-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry {
  int value;
  Entry() : value(7) { ++value; }
};
int main() {
  std::list<Entry> rows(1);
  return rows.front().value != 8;
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListTwoScalarEmplaceBackRunAtBothOptimizations) {
  const auto Source = tmpFile("list-two-scalar-emplace-back.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry {
  int key, value;
  Entry() noexcept : key(0), value(0) {}
  Entry(short k, int v) noexcept : key(k), value(v) {}
};
int main() {
  std::list<Entry> rows;
  short k = 3;
  auto& out = rows.emplace_back(k, 11);
  return &out != &rows.front() || out.key != 3 || out.value != 11;
}
)r19containers");
  const auto Output = tmpFile("list-two-scalar-emplace-back.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-two-scalar-emplace-back" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListTwoScalarEmplacePositionOverloadRunAtBothOptimizations) {
  const auto Source = tmpFile("list-two-scalar-emplace-position-overload.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry {
  int value;
  Entry() noexcept : value(0) {}
  Entry(int a, int b) noexcept : value(a * 10 + b) {}
  Entry(double, double) noexcept : value(99) {}
};
int main() {
  std::list<Entry> rows(1);
  auto it = rows.emplace(rows.cbegin(), 3, 7);
  return it != rows.begin() || it->value != 37 || rows.back().value != 0;
}
)r19containers");
  const auto Output = tmpFile("list-two-scalar-emplace-position-overload.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-two-scalar-emplace-position-overload" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListEmplacePreservesSelectedReferenceParameters) {
  const auto Source = tmpFile("list-two-scalar-emplace-reference-parameter-guard.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry {
  int value;
  Entry() noexcept : value(0) {}
  Entry(int& a, int b) noexcept : value(a + b) { ++a; }
};
int main() {
  std::list<Entry> rows;
  int a = 3;
  return rows.emplace_back(a, 4).value != 7 || a != 4;
}
)r19containers");
  const auto Output = tmpFile("list-two-scalar-emplace-reference-parameter-guard.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-two-scalar-emplace-reference-parameter-guard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListOneScalarEmplaceExplicitRunAtBothOptimizations) {
  const auto Source = tmpFile("list-one-scalar-emplace-explicit.cpp");
  writeFile(Source, R"r19containers(#include <list>
struct Entry {
  int value;
  Entry() noexcept : value(0) {}
  explicit Entry(int v) noexcept : value(v) {}
};
int main() {
  std::list<Entry> rows;
  auto& out = rows.emplace_front(11);
  return &out != &rows.front() || out.value != 11;
}
)r19containers");
  const auto Output = tmpFile("list-one-scalar-emplace-explicit.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-one-scalar-emplace-explicit" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListScalarEmplaceReferenceAliasRunAtBothOptimizations) {
  const auto Source = tmpFile("list-scalar-emplace-reference-alias.cpp");
  writeFile(Source, R"r19containers(#include <list>
int arguments;
int amount() noexcept { ++arguments; return 4; }
struct Entry {
  int* target;
  int seen;
  Entry() noexcept : target(nullptr), seen(0) {}
  Entry(int& out, const int& old, int&& add) noexcept : target(&out), seen(old) { out += add; }
};
int main() {
  std::list<Entry> rows;
  int value = 3;
  auto& entry = rows.emplace_back(value, value, amount());
  return arguments != 1 || value != 7 || entry.target != &value || entry.seen != 3;
}
)r19containers");
  const auto Output = tmpFile("list-scalar-emplace-reference-alias.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-scalar-emplace-reference-alias" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}
