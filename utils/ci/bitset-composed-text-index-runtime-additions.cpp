
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

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryGuardCopyConstructionRejected) {
  const auto Source = tmpFile("bitset-wide-temporary-guard-copy-construction.cpp");
  writeFile(Source, R"r19bitwidechar(#include <bitset>
int main(){std::bitset<129> b(33);return std::bitset<129>{b}.to_ullong()!=33;}
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
  const auto Output = tmpFile("bitset-composed.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-composed" + Optimization);
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

TEST_F(TranslateTest, CoreV2BitsetConstLocalGuardConstReferenceRejected) {
  const auto Source = tmpFile("bitset-const-local-guard-const-reference.cpp");
  writeFile(Source, R"r19bitlocal(#include <bitset>
using Bits = std::bitset<129>;
int main(){const Bits& bits=Bits(2);return bits.to_ullong()!=2;}
)r19bitlocal");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstLocalGuardStaticConstRejected) {
  const auto Source = tmpFile("bitset-const-local-guard-static-const.cpp");
  writeFile(Source, R"r19bitlocal(#include <bitset>
using Bits = std::bitset<129>;
int main(){static const Bits bits(2);return bits.to_ullong()!=2;}
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

TEST_F(TranslateTest, CoreV2BitsetConstLocalGuardCopyInitializedRejected) {
  const auto Source = tmpFile("bitset-const-local-guard-copy-initialized.cpp");
  writeFile(Source, R"r19bitlocal(#include <bitset>
using Bits = std::bitset<129>;
int main(){Bits a(3);const Bits b=a;return b.to_ullong()!=3;}
)r19bitlocal");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstLocalGuardConstMemberRejected) {
  const auto Source = tmpFile("bitset-const-local-guard-const-member.cpp");
  writeFile(Source, R"r19bitlocal(#include <bitset>
using Bits = std::bitset<129>;
struct State{const Bits bits{2};};
int main(){State state;return state.bits.to_ullong()!=2;}
)r19bitlocal");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstLocalGuardConditionalReceiverRejected) {
  const auto Source = tmpFile("bitset-const-local-guard-conditional-receiver.cpp");
  writeFile(Source, R"r19bitlocal(#include <bitset>
using Bits = std::bitset<129>;
unsigned long long convert(bool select){const Bits a(1),b(2);return (select?a:b).to_ullong();}
int main(){return convert(true)!=1||convert(false)!=2;}
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
