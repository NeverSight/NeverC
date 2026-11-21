
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

TEST_F(TranslateTest, CoreV2VariantLayoutRuntimeRecordRejected) {
  const auto Source = tmpFile("variant-layout-runtime-record.cpp");
  writeFile(Source, R"r19variantlayout(#include <variant>
using V = std::variant<int, double>;
int main() { V value{9}; return sizeof(value) == 0; }
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

TEST_F(TranslateTest, CoreV2VariantObjectsClassAlternativeRejected) {
  const auto Source = tmpFile("variant-objects-class-alternative.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
struct Payload { int field; };
extern "C" int check() { std::variant<Payload, int> value; return int(value.index()); }
int main() { return check(); }
)r19variantsclr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
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

TEST_F(TranslateTest, CoreV2VariantObjectsUnprovedConversionRejected) {
  const auto Source = tmpFile("variant-objects-unproved-conversion.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
extern "C" int check() {
  short original = 7;
  std::variant<int, double> value = original;
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

TEST_F(TranslateTest, CoreV2AnyLayoutUnevaluatedObjectRejected) {
  const auto Source = tmpFile("any-layout-unevaluated-object.cpp");
  writeFile(Source, R"r19any(#include <any>
int main() { return sizeof(std::any{}) == 0; }
)r19any");
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

TEST_F(TranslateTest, CoreV2AssociativeMapCustomTypeOrPolicyRejected) {
  const auto Source = tmpFile("associative-map-custom-type-or-policy.cpp");
  writeFile(Source, R"r19assoc(#include <map>
#include <type_traits>
using Container = std::map<int, double>;
int main() { return sizeof(Container) == 0; }
)r19assoc");
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

TEST_F(TranslateTest, CoreV2AssociativeSetCustomTypeOrPolicyRejected) {
  const auto Source = tmpFile("associative-set-custom-type-or-policy.cpp");
  writeFile(Source, R"r19assoc(#include <set>
#include <type_traits>
using Container = std::set<int, std::greater<int>>;
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

TEST_F(TranslateTest, CoreV2VariantEmplaceWideningOperandRejected) {
  const auto Source = tmpFile("variant-emplace-widening-operand.cpp");
  writeFile(Source, R"r19emplace(#include <variant>
int f() {
  std::variant<int, long> value;
  return value.emplace<1>(11);
}
)r19emplace");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
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

TEST_F(TranslateTest, CoreV2VariantEmplaceSourceOwnedRecordRejected) {
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

TEST_F(TranslateTest, CoreV2AssociativeMultimapCustomTypeOrPolicyRejected) {
  const auto Source = tmpFile("associative-multimap-custom-type-or-policy.cpp");
  writeFile(Source, R"r19containers(#include <map>
#include <type_traits>
using Container = std::multimap<int, double>;
int main() { return sizeof(Container) == 0; }
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
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

TEST_F(TranslateTest, CoreV2AssociativeMultisetCustomTypeOrPolicyRejected) {
  const auto Source = tmpFile("associative-multiset-custom-type-or-policy.cpp");
  writeFile(Source, R"r19containers(#include <set>
#include <type_traits>
using Container = std::multiset<int, std::greater<int>>;
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

TEST_F(TranslateTest, CoreV2VariantIntegralVisitGenericVisitorRejected) {
  const auto Source = tmpFile("variant-integral-visit-generic-visitor.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int f(std::variant<int, short>& value) {
  return std::visit([](auto v) -> int { return int(v); }, value);
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

TEST_F(TranslateTest, CoreV2AnyIntUnsupportedFloatingValueRejected) {
  const auto Source = tmpFile("any-int-unsupported-floating-value.cpp");
  writeFile(Source, R"r19containers(#include <any>
int f() {
  std::any value(1.0);
  return value.has_value();
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
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

TEST_F(TranslateTest, CoreV2VariantTwoVisitGenericVisitorRejected) {
  const auto Source = tmpFile("variant-two-visit-generic-visitor.cpp");
  writeFile(Source, R"r19containers(#include <variant>
int f(std::variant<int, short>& left, std::variant<int, short>& right) {
  return std::visit([](auto a, auto b) -> int { return a - b; }, left, right);
}
)r19containers");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
