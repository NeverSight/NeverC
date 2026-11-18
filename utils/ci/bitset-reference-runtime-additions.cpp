
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
