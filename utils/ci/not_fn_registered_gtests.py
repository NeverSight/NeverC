"""Restore only independently hashed registered not_fn GTest suffixes."""
import hashlib

ORIGINAL_SOURCE_SHA256 = '0099922e5f92464dde0ea0c4d9cd0359d0659eeb886a099d6f558c17ef8e70b5'
FOUR_REGISTERED_SOURCE_SHA256 = '6fab1ae0e41fac1b3c351d2f23f9043c41e18cd641b0bb4620ef2c98acd55866'
FIVE_REGISTERED_SOURCE_SHA256 = '67efba952e2e495682b7ad08e317a7bd3c4e77541d0c279f90c6e242a51d1e8c'
SIX_REGISTERED_SOURCE_SHA256 = '2fa44b258cb1ef0bbbd45435f5150673f35a46f1574d7a724ede6668e57daac2'
TWELVE_REGISTERED_SOURCE_SHA256 = '006fa380968b6acc0c7c8d99ff7a1c5a59196045e08934805550b649966bbf7d'
BIND21_REGISTERED_SOURCE_SHA256 = 'a38050d1e0d40689c4cec7cd03e344cbbf042118bd410e3aa59cc8a149719201'
BIND21_GOLDEN_SUFFIX_SHA256 = 'dcb85be2ebb3cea63d018f3db3d9238a3064fc3e9398010a647ea5aa17f247a1'
BIND48_REGISTERED_SOURCE_SHA256 = 'd24fb9ba8d90ef5ac0f72238e04be70e08a50e583bd723bee5a56dc95164346b'
BIND48_GOLDEN_SUFFIX_SHA256 = '620adce3d4aa3c926ade2dd4e76c048cca4b10ac1b645eed6caa889cc910ec88'
BIND40_REGISTERED_SOURCE_SHA256 = 'bcb391dec4e2db6181eda34c778404b2d8846fd7f05c487c6e6e57c06e7a06fc'
BIND40_GOLDEN_SUFFIX_SHA256 = '48e16535ad52835351d6beb425f6b69690209d1014cd24df4da2ba4fe8f8ae1d'
BIND32_REGISTERED_SOURCE_SHA256 = 'b30cba17a5378fabc41f5aa381f6ae5caabaadfa032facec8d6b1017d20c0423'
BIND32_GOLDEN_SUFFIX_SHA256 = '304a075bc7b2a52190b0d94f09dba6c5b45aa3f3e690063ca83f9343c3a55e4f'
ELEVEN_REGISTERED_SOURCE_SHA256 = 'bcae7353fcc16f7d198d72c9a7a3f0d92caff4d5e24b5d520c64ab8bcb25d87a'
TEN_REGISTERED_SOURCE_SHA256 = '694506523a109084f1a5a1ce910525915a4855936dc7687715cee2eb34f39271'
FOUR_GOLDEN_SUFFIX_SHA256 = 'a4be872bcc824c43b7962f2a341247286fe363b78f60beab56b28892e3905137'
FIVE_GOLDEN_SUFFIX_SHA256 = '79d803d7ead8ca27481ca76b4d91287209d2460fe49b148ddd38764b11a18fbe'
SIX_GOLDEN_SUFFIX_SHA256 = '7b85dcd35b2ed3581e4baf3a6b3318f471cf815e26237b516b672f4b421c8e5c'
TWELVE_GOLDEN_SUFFIX_SHA256 = '1773950ecb88dc8436566bb9e2e1bf7601deffb7527ffedfd051bdc9feb4b0c1'
ELEVEN_GOLDEN_SUFFIX_SHA256 = '69c4582879c7ffd6a65315a9882a9d42a96ff2b9ccb10a43819384f0356751f8'
TEN_GOLDEN_SUFFIX_SHA256 = '7df227f8aa712296687c703d6dcc0d9cfbca678a3c3ed9f033c8cddc7417c22e'

BIND97_REGISTERED_SOURCE_SHA256 = '437f0c48ca2f8b9c8821fdde33e65d21dd2934d49cd43b57e01f08f455294ca3'
BIND97_GOLDEN_SUFFIX_SHA256 = '8d3a87ebacdae8d321140f50dd0bfcfe627acf7aa0b48808e89d9b447ae94411'

BIND84_REGISTERED_SOURCE_SHA256 = '4794d7921206f429cd3418f9f4211c4d25de42a6c296bd8d194665ce5b32a2a0'
BIND84_GOLDEN_SUFFIX_SHA256 = 'dd5ac6b2aaae175783f9d51932a89b09d80df9abaeb2e8418b82e30590fead4d'

BIND77_REGISTERED_SOURCE_SHA256 = '3876bd21a94eea64e965d78ec7cff2278e2aba8e92c8d4bce1f643a9b01183a1'
BIND77_GOLDEN_SUFFIX_SHA256 = '7ada3488050fcb697a7046e3ff43bfa88d68be0e90f5c37751bdd6e6f50ccae7'

BIND74_REGISTERED_SOURCE_SHA256 = '7773c0427e09acd364c4902b1d7b26bcd5b25d64cbe05d57ee02905141614c99'
BIND74_GOLDEN_SUFFIX_SHA256 = '2ef5c4fe1b79816647351b7fbc20cd20810e7b6335bb9c637be2e08e4b9895f5'

BIND71_REGISTERED_SOURCE_SHA256 = '52b52f1a8c9911eaa822df66d871f6d214da6fd3623c16d0bc7aa617ffc80d7c'
BIND71_GOLDEN_SUFFIX_SHA256 = '4b93f31e40633a58faaaf93752a3d4dab3a4e073cabf556f7e77018d2b6abc2a'

BIND61_REGISTERED_SOURCE_SHA256 = 'dc42e287ed5f6ad0c66369429ff1cfc852f22b4977d308db18f40d42e4df4665'
BIND61_GOLDEN_SUFFIX_SHA256 = '73f36c04fabb4af01ede679c2fede542876f865a81143bc65f512970ce615df9'


# The target-size fixture repair keeps every archived registration unchanged.
WINDOWS64_MAINTAINED_ORIGINAL_SHA256 = '28d17d01980bed3e058d29abf852f494a5b9f16354e0b7d101f53e93e0a1f909'
WINDOWS64_MAINTAINED_BIND97_SHA256 = '897d3f3dd65d00ca8f03117cef9196aa93eca05082a6282d4ffd75557a9e0acf'

FIXTURE_MAINTAINED_ORIGINAL_SHA256 = '726b475241352fd3ee832e479968f9daf5e44a27cb7b6f9f0f7f52b9a84ae4e3'
FIXTURE_MAINTAINED_BIND97_SHA256 = 'cabce24d8eadcc4fd4b384b1f3d92e3536ba884d13afce0c3a2d28bc94ff2ab0'
BIND109_REGISTERED_SOURCE_SHA256 = '3db5ef196fd52c95dfb7fccfe80b6d5417a723899cf315ef7abbc4b6c2a06f5e'
BIND109_GOLDEN_SUFFIX_SHA256 = 'b93a872a3221693dacc805153c4bc126edf4651e5eaf696295726872b376bcfb'

R19_DEQUE_MAINTAINED_SHA256 = 'b3aaaaee7a1d4990a2972a247dcd2052352b0170c06b0065567c4cdb0bfcd6f5'
R19_DEQUE_REGISTERED_SHA256 = '40a94203aa58d29437e4c03f871157a4948ba401406cfc93dc6e1103de0cd572'

R19_DEQUE_ADAPTER_MAINTAINED_SHA256 = 'f78087a41d7783873f9080d002099fb2289aa8661975da0efc9487c8a74379df'
R19_DEQUE_ADAPTER_REGISTERED_SHA256 = 'ceeba042dcd503af9834542a9d2abe2ff8db61999fda893b3ab52f45db9ed65d'

R19_LIST_MAINTAINED_SHA256 = 'be5ce8350e32f64b17b9db709b4721d2cb5923bc5a36171431a5b441d072509f'
R19_LIST_REGISTERED_SHA256 = '52215bf1cb1ab3a7d5c468f698ae96e15895d849867720708f51c89d6437a0ef'

R19_FORWARD_LIST_MAINTAINED_SHA256 = '99164b5b4e996784afc538467afe5afe2f7205d31a6af5cd9a59d21aa9582d21'
R19_FORWARD_LIST_REGISTERED_SHA256 = '8c1f94c7ed33bd87b6dd06bb2fcd50953fa5f690c12264bb5a10a656167c1217'

R19_VALARRAY_MAINTAINED_SHA256 = '0fef0914482ab677ee540f2ebbf62bed291cc9a49af17e8d08de4d2dd25afc63'
R19_VALARRAY_REGISTERED_SHA256 = 'def7a355ea477abd045289ddd0af42d44b7f52aa0930123aa1212281aa293fb2'

R19_POPCOUNT_MAINTAINED_SHA256 = '93fefb63333bea119087a1265f504b3d20f6db1e4ccdf794021a973899d0be50'
R19_POPCOUNT_REGISTERED_SHA256 = '7267b21a6e640ede8ecffb05c9971fcd865475b9e4e0fd66c691570fe7ea57aa'

R19_SCOPED_BITSET_MAINTAINED_SHA256 = 'c758bb163adc94a8c5988a3093cddb16c78c0bf500948f9a0c136962088db782'
R19_SCOPED_BITSET_REGISTERED_SHA256 = '4cfc01142e1cfefab6eaa8ae58ff236117a3fb1e133ea03b12092cb0ff177b29'

QUERY_CONSTRUCTOR_CHECKED_MAINTAINED_ORIGINAL_SHA256 = '7a86aa397d598346ab9ef84f05098452ed0c41c16852586e7ce6ee4f5b1fc31a'
QUERY_CONSTRUCTOR_CHECKED_REGISTERED_BIND109_SHA256 = '79228dd2d2f0d750eb8aa75faa5e255bf3786c14c0f92e3f3e211e073c33fdea'

QUERY_CONSTRUCTOR_MAINTAINED_ORIGINAL_SHA256 = '748a1966c60985df355d52f5746a7610b155aec2e1480a55144efb52a9c054b8'
QUERY_CONSTRUCTOR_REGISTERED_BIND109_SHA256 = 'feaff87d9013ab41c4db3920b2377502799cb43b7cf34cbfcdc56aee680d0753'


R19_DEFINITION_FIXTURE_REGISTERED_SHA256 = '404564b2e706d0ca9c99a4233b3b99b74feca42149f34357d2d1a8783dacbd4d'
R19_DEFINITION_FIXTURE_MAINTAINED_SHA256 = '6b9f8e9f180e15630e45a9f3a625a106091b021d530e54ac4a8c02a3676171cd'
R19_DEFINITION_FIXTURE_OLD_REGISTERED_SHA256 = '40a94203aa58d29437e4c03f871157a4948ba401406cfc93dc6e1103de0cd572'
R19_DEFINITION_FIXTURE_OLD_MAINTAINED_SHA256 = 'b3aaaaee7a1d4990a2972a247dcd2052352b0170c06b0065567c4cdb0bfcd6f5'
_R19_DEFINITION_FIXTURE_OLD = b'writeFile(Source, R"cpp(struct C { C(int) noexcept; };\nstatic_assert(__is_constructible(C, int));\nint main() { return 0; }\n)cpp");'
_R19_DEFINITION_FIXTURE_NEW = b'writeFile(Source, R"cpp(struct C { C(int) noexcept; };\nstatic_assert(__is_constructible(C, int));\nC::C(int) noexcept {}\nint main() { return 0; }\n)cpp");'

def restore_definition_fixture_history(source):
    """Reconstruct only the exact archived fixture for historical registry checks."""
    digest = hashlib.sha256(source).hexdigest()
    assert digest in {R19_DEFINITION_FIXTURE_REGISTERED_SHA256, R19_DEFINITION_FIXTURE_MAINTAINED_SHA256}
    assert source.count(_R19_DEFINITION_FIXTURE_NEW) == 1
    restored = source.replace(_R19_DEFINITION_FIXTURE_NEW, _R19_DEFINITION_FIXTURE_OLD, 1)
    expected = (R19_DEFINITION_FIXTURE_OLD_REGISTERED_SHA256
                if digest == R19_DEFINITION_FIXTURE_REGISTERED_SHA256
                else R19_DEFINITION_FIXTURE_OLD_MAINTAINED_SHA256)
    assert hashlib.sha256(restored).hexdigest() == expected
    return restored


R19_PRIORITY_REGISTERED_SHA256 = 'a2b8c65bb7affeb36c45d6c9a9101e20987d2e2a5cf186b3797bc7e1e869298f'
R19_PRIORITY_MAINTAINED_SHA256 = 'e918c2f8ee5d9bc260a95fa010842b93572c5c8b844cedab0f9bff6d69ef849d'
R19_PRIORITY_PREVIOUS_REGISTERED_SHA256 = '404564b2e706d0ca9c99a4233b3b99b74feca42149f34357d2d1a8783dacbd4d'
R19_PRIORITY_PREVIOUS_MAINTAINED_SHA256 = '6b9f8e9f180e15630e45a9f3a625a106091b021d530e54ac4a8c02a3676171cd'
_R19_PRIORITY_ADDITIONS = b'TEST_F(TranslateTest, CoreV2PriorityQueueIntEmptyRunAtBothOptimizations) {\n  const auto Source = tmpFile("priority-queue-empty-and-top.cpp");\n  const auto Output = tmpFile("priority-queue-empty-and-top.nc");\n  writeFile(Source, R"r19priority(#include <queue>\nint main(){std::priority_queue<int> q;const auto& c=q;\nif(!c.empty()||c.size()!=0)return 1;q.push(3);q.push(8);q.push(1);\nif(c.empty()||c.size()!=3||c.top()!=8)return 2;\nq.pop();if(c.top()!=3)return 3;q.pop();q.pop();return !c.empty();}\n)r19priority");\n  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});\n  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;\n  for (const std::string &Optimization : {"-O0", "-O2"}) {\n    SCOPED_TRACE(Optimization);\n    const auto Executable = tmpFile("priority-queue-empty-and-top" + Optimization);\n    auto Compile = compileGenerated(Output, Executable, Optimization);\n    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;\n    auto Run = exec(Executable.string(), {});\n    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;\n  }\n}\n\nTEST_F(TranslateTest, CoreV2PriorityQueueIntExtremesRunAtBothOptimizations) {\n  const auto Source = tmpFile("priority-queue-duplicates-and-extremes.cpp");\n  const auto Output = tmpFile("priority-queue-duplicates-and-extremes.nc");\n  writeFile(Source, R"r19priority(#include <queue>\nint main(){std::priority_queue<int> q;\nint input[]={-2147483647-1,-7,0,7,2147483647,7};\nint expected[]={2147483647,7,7,0,-7,-2147483647-1};\nfor(int i=0;i!=6;++i)q.push(input[i]);\nfor(int i=0;i!=6;++i){if(q.top()!=expected[i])return 1;q.pop();}\nreturn !q.empty();}\n)r19priority");\n  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});\n  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;\n  for (const std::string &Optimization : {"-O0", "-O2"}) {\n    SCOPED_TRACE(Optimization);\n    const auto Executable = tmpFile("priority-queue-duplicates-and-extremes" + Optimization);\n    auto Compile = compileGenerated(Output, Executable, Optimization);\n    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;\n    auto Run = exec(Executable.string(), {});\n    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;\n  }\n}\n\nTEST_F(TranslateTest, CoreV2PriorityQueueIntAliasedGrowthRunAtBothOptimizations) {\n  const auto Source = tmpFile("priority-queue-growth-alias.cpp");\n  const auto Output = tmpFile("priority-queue-growth-alias.nc");\n  writeFile(Source, R"r19priority(#include <queue>\nint main(){std::priority_queue<int> q;q.push(11);\nfor(int i=0;i!=2048;++i)q.push(q.top());\nif(q.size()!=2049)return 1;\nfor(int i=0;i!=2049;++i){if(q.top()!=11)return 2;q.pop();}\nq.push(-3);return q.size()!=1||q.top()!=-3;}\n)r19priority");\n  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});\n  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;\n  for (const std::string &Optimization : {"-O0", "-O2"}) {\n    SCOPED_TRACE(Optimization);\n    const auto Executable = tmpFile("priority-queue-growth-alias" + Optimization);\n    auto Compile = compileGenerated(Output, Executable, Optimization);\n    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;\n    auto Run = exec(Executable.string(), {});\n    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;\n  }\n}\n\nTEST_F(TranslateTest, CoreV2PriorityQueueIntInterleavedRunAtBothOptimizations) {\n  const auto Source = tmpFile("priority-queue-interleaved-heaps.cpp");\n  const auto Output = tmpFile("priority-queue-interleaved-heaps.nc");\n  writeFile(Source, R"r19priority(#include <queue>\nint main(){std::priority_queue<int> q;\nfor(int i=0;i!=1025;++i)q.push(i-512);\nfor(int i=512;i!=0;--i){if(q.top()!=i)return 1;q.pop();}\nfor(int i=0;i!=1025;++i)q.push(i+513);\nfor(int i=1537;i!=512;--i){if(q.top()!=i)return 2;q.pop();}\nfor(int i=0;i!=-513;--i){if(q.top()!=i)return 3;q.pop();}\nreturn !q.empty();}\n)r19priority");\n  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});\n  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;\n  for (const std::string &Optimization : {"-O0", "-O2"}) {\n    SCOPED_TRACE(Optimization);\n    const auto Executable = tmpFile("priority-queue-interleaved-heaps" + Optimization);\n    auto Compile = compileGenerated(Output, Executable, Optimization);\n    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;\n    auto Run = exec(Executable.string(), {});\n    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;\n  }\n}\n\nTEST_F(TranslateTest, CoreV2PriorityQueueIntAllocationReferenceRunAtBothOptimizations) {\n  const auto Source = tmpFile("priority-queue-live-allocation-argument.cpp");\n  const auto Output = tmpFile("priority-queue-live-allocation-argument.nc");\n  writeFile(Source, R"r19priority(using Size=decltype(sizeof(0));\nextern "C" void* malloc(Size);extern "C" void free(void*);\n#include <new>\n#include <queue>\nstatic int* armed;static int observed;\nvoid* operator new(Size n){void* p=malloc(n);if(armed){++observed;*armed=19;}return p;}\nvoid operator delete(void* p) noexcept {free(p);}\nvoid operator delete(void* p,Size) noexcept {free(p);}\nint main(){std::priority_queue<int> q;int value=3;armed=&value;q.push(value);armed=nullptr;\nreturn !observed||value!=19||q.size()!=1||q.top()!=19;}\n)r19priority");\n  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});\n  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;\n  for (const std::string &Optimization : {"-O0", "-O2"}) {\n    SCOPED_TRACE(Optimization);\n    const auto Executable = tmpFile("priority-queue-live-allocation-argument" + Optimization);\n    auto Compile = compileGenerated(Output, Executable, Optimization);\n    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;\n    auto Run = exec(Executable.string(), {});\n    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;\n  }\n}\n\nTEST_F(TranslateTest, CoreV2PriorityQueueIntArgumentEffectsRunAtBothOptimizations) {\n  const auto Source = tmpFile("priority-queue-receiver-argument-effects.cpp");\n  const auto Output = tmpFile("priority-queue-receiver-argument-effects.nc");\n  writeFile(Source, R"r19priority(#include <queue>\nstatic std::priority_queue<int>* current;static int effects;\nstd::priority_queue<int>* receiver(){effects=effects*10+1;return current;}\nconst int& argument(){effects=effects*10+2;return current->top();}\nint main(){std::priority_queue<int> q;q.push(7);current=&q;\nreceiver()->push(argument());receiver()->push(argument());\nreturn effects!=1212||q.size()!=3||q.top()!=7;}\n)r19priority");\n  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});\n  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;\n  for (const std::string &Optimization : {"-O0", "-O2"}) {\n    SCOPED_TRACE(Optimization);\n    const auto Executable = tmpFile("priority-queue-receiver-argument-effects" + Optimization);\n    auto Compile = compileGenerated(Output, Executable, Optimization);\n    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;\n    auto Run = exec(Executable.string(), {});\n    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;\n  }\n}\n\nTEST_F(TranslateTest, CoreV2PriorityQueueOwnedPushRejected) {\n  const auto Source = tmpFile("priority-queue-owned-push-body.cpp");\n  writeFile(Source, R"r19priority(#include <queue>\nnamespace std { inline namespace __1 {\ntemplate<> inline void priority_queue<int>::push(const int&) {}\n}}\nint main(){std::priority_queue<int> q;int x=1;q.push(x);return q.empty();}\n)r19priority");\n  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});\n  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;\n  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||\n              Result.err.find("TR0203") != std::string::npos) << Result.err;\n}\n\nTEST_F(TranslateTest, CoreV2PriorityQueueOwnedTopMacroRejected) {\n  const auto Source = tmpFile("priority-queue-owned-top-macro.cpp");\n  writeFile(Source, R"r19priority(#define top owned_top\n#include <queue>\nint main(){std::priority_queue<int> q;q.push(1);return q.top()!=1;}\n)r19priority");\n  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});\n  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;\n  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||\n              Result.err.find("TR0203") != std::string::npos) << Result.err;\n}\n\nTEST_F(TranslateTest, CoreV2PriorityQueueOwnedComparatorFieldMacroRejected) {\n  const auto Source = tmpFile("priority-queue-owned-comparator-field-macro.cpp");\n  writeFile(Source, R"r19priority(#define comp owned_comp\n#include <queue>\nint main(){std::priority_queue<int> q;q.push(1);return q.top()!=1;}\n)r19priority");\n  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});\n  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;\n  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||\n              Result.err.find("TR0203") != std::string::npos) << Result.err;\n}\n\nTEST_F(TranslateTest, CoreV2PriorityQueueOwnedEmptyRejected) {\n  const auto Source = tmpFile("priority-queue-owned-empty-body.cpp");\n  writeFile(Source, R"r19priority(#include <queue>\nnamespace std { inline namespace __1 {\ntemplate<> inline bool priority_queue<int>::empty() const {return true;}\n}}\nint main(){std::priority_queue<int> q;return !q.empty();}\n)r19priority");\n  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});\n  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;\n  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||\n              Result.err.find("TR0203") != std::string::npos) << Result.err;\n}\n\n'

def restore_priority_queue_history(source):
    """Remove only the exact authenticated priority layer for historical checks."""
    digest = hashlib.sha256(source).hexdigest()
    assert digest in {R19_PRIORITY_REGISTERED_SHA256,R19_PRIORITY_MAINTAINED_SHA256}
    assert source.count(_R19_PRIORITY_ADDITIONS) == 1
    restored = source.replace(_R19_PRIORITY_ADDITIONS,b'',1)
    expected = (R19_PRIORITY_PREVIOUS_REGISTERED_SHA256 if digest == R19_PRIORITY_REGISTERED_SHA256
                else R19_PRIORITY_PREVIOUS_MAINTAINED_SHA256)
    assert hashlib.sha256(restored).hexdigest() == expected
    return restored


def restore_original_gtests(source, additions):
    """Reject changed, missing, repeated or mismatched registered additions."""
    digest = lambda data: hashlib.sha256(data).hexdigest()
    suffix_digest = digest(additions)
    if suffix_digest == BIND109_GOLDEN_SUFFIX_SHA256:
        registered_digest = BIND109_REGISTERED_SOURCE_SHA256
    elif suffix_digest == BIND97_GOLDEN_SUFFIX_SHA256:
        registered_digest = BIND97_REGISTERED_SOURCE_SHA256
    elif suffix_digest == BIND84_GOLDEN_SUFFIX_SHA256:
        registered_digest = BIND84_REGISTERED_SOURCE_SHA256
    elif suffix_digest == BIND77_GOLDEN_SUFFIX_SHA256:
        registered_digest = BIND77_REGISTERED_SOURCE_SHA256
    elif suffix_digest == BIND74_GOLDEN_SUFFIX_SHA256:
        registered_digest = BIND74_REGISTERED_SOURCE_SHA256
    elif suffix_digest == BIND71_GOLDEN_SUFFIX_SHA256:
        registered_digest = BIND71_REGISTERED_SOURCE_SHA256
    elif suffix_digest == BIND61_GOLDEN_SUFFIX_SHA256:
        registered_digest = BIND61_REGISTERED_SOURCE_SHA256
    elif suffix_digest == BIND48_GOLDEN_SUFFIX_SHA256:
        registered_digest = BIND48_REGISTERED_SOURCE_SHA256
    elif suffix_digest == BIND40_GOLDEN_SUFFIX_SHA256:
        registered_digest = BIND40_REGISTERED_SOURCE_SHA256
    elif suffix_digest == BIND32_GOLDEN_SUFFIX_SHA256:
        registered_digest = BIND32_REGISTERED_SOURCE_SHA256
    elif suffix_digest == BIND21_GOLDEN_SUFFIX_SHA256:
        registered_digest = BIND21_REGISTERED_SOURCE_SHA256
    elif suffix_digest == FOUR_GOLDEN_SUFFIX_SHA256:
        registered_digest = FOUR_REGISTERED_SOURCE_SHA256
    elif suffix_digest == FIVE_GOLDEN_SUFFIX_SHA256:
        registered_digest = FIVE_REGISTERED_SOURCE_SHA256
    elif suffix_digest == SIX_GOLDEN_SUFFIX_SHA256:
        registered_digest = SIX_REGISTERED_SOURCE_SHA256
    elif suffix_digest == TEN_GOLDEN_SUFFIX_SHA256:
        registered_digest = TEN_REGISTERED_SOURCE_SHA256
    elif suffix_digest == ELEVEN_GOLDEN_SUFFIX_SHA256:
        registered_digest = ELEVEN_REGISTERED_SOURCE_SHA256
    else:
        assert suffix_digest == TWELVE_GOLDEN_SUFFIX_SHA256
        registered_digest = TWELVE_REGISTERED_SOURCE_SHA256
    if digest(source) == ORIGINAL_SOURCE_SHA256:
        return source
    source_digest = digest(source)
    if source_digest == R19_PRIORITY_MAINTAINED_SHA256:
        return source
    if source_digest == R19_PRIORITY_REGISTERED_SHA256:
        assert suffix_digest == BIND109_GOLDEN_SUFFIX_SHA256 and source.endswith(additions)
        original = source[:-len(additions)]
        assert digest(original) == R19_PRIORITY_MAINTAINED_SHA256
        return original
    if source_digest == R19_DEFINITION_FIXTURE_MAINTAINED_SHA256:
        return source
    if source_digest == R19_DEFINITION_FIXTURE_REGISTERED_SHA256:
        assert suffix_digest == BIND109_GOLDEN_SUFFIX_SHA256 and source.endswith(additions)
        original = source[:-len(additions)]
        assert digest(original) == R19_DEFINITION_FIXTURE_MAINTAINED_SHA256
        return original
    if source_digest == R19_DEQUE_MAINTAINED_SHA256:
        return source
    if source_digest == R19_DEQUE_REGISTERED_SHA256:
        assert suffix_digest == BIND109_GOLDEN_SUFFIX_SHA256 and source.endswith(additions)
        original = source[:-len(additions)]
        assert digest(original) == R19_DEQUE_MAINTAINED_SHA256
        return original
    if source_digest == R19_DEQUE_ADAPTER_MAINTAINED_SHA256:
        return source
    if source_digest == R19_DEQUE_ADAPTER_REGISTERED_SHA256:
        assert suffix_digest == BIND109_GOLDEN_SUFFIX_SHA256 and source.endswith(additions)
        original = source[:-len(additions)]
        assert digest(original) == R19_DEQUE_ADAPTER_MAINTAINED_SHA256
        return original
    if source_digest == R19_LIST_MAINTAINED_SHA256:
        return source
    if source_digest == R19_LIST_REGISTERED_SHA256:
        assert suffix_digest == BIND109_GOLDEN_SUFFIX_SHA256 and source.endswith(additions)
        original = source[:-len(additions)]
        assert digest(original) == R19_LIST_MAINTAINED_SHA256
        return original
    if source_digest == R19_FORWARD_LIST_MAINTAINED_SHA256:
        return source
    if source_digest == R19_FORWARD_LIST_REGISTERED_SHA256:
        assert suffix_digest == BIND109_GOLDEN_SUFFIX_SHA256 and source.endswith(additions)
        original = source[:-len(additions)]
        assert digest(original) == R19_FORWARD_LIST_MAINTAINED_SHA256
        return original
    if source_digest == R19_VALARRAY_MAINTAINED_SHA256:
        return source
    if source_digest == R19_VALARRAY_REGISTERED_SHA256:
        assert suffix_digest == BIND109_GOLDEN_SUFFIX_SHA256 and source.endswith(additions)
        original = source[:-len(additions)]
        assert digest(original) == R19_VALARRAY_MAINTAINED_SHA256
        return original
    if source_digest == R19_POPCOUNT_MAINTAINED_SHA256:
        return source
    if source_digest == R19_POPCOUNT_REGISTERED_SHA256:
        assert suffix_digest == BIND109_GOLDEN_SUFFIX_SHA256 and source.endswith(additions)
        original = source[:-len(additions)]
        assert digest(original) == R19_POPCOUNT_MAINTAINED_SHA256
        return original
    if source_digest == R19_SCOPED_BITSET_MAINTAINED_SHA256:
        return source
    if source_digest == R19_SCOPED_BITSET_REGISTERED_SHA256:
        assert suffix_digest == BIND109_GOLDEN_SUFFIX_SHA256 and source.endswith(additions)
        original = source[:-len(additions)]
        assert digest(original) == R19_SCOPED_BITSET_MAINTAINED_SHA256
        return original
    if source_digest == QUERY_CONSTRUCTOR_CHECKED_MAINTAINED_ORIGINAL_SHA256:
        return source
    if source_digest == QUERY_CONSTRUCTOR_CHECKED_REGISTERED_BIND109_SHA256:
        assert suffix_digest == BIND109_GOLDEN_SUFFIX_SHA256 and source.endswith(additions)
        original = source[:-len(additions)]
        assert digest(original) == QUERY_CONSTRUCTOR_CHECKED_MAINTAINED_ORIGINAL_SHA256
        return original
    if source_digest == QUERY_CONSTRUCTOR_MAINTAINED_ORIGINAL_SHA256:
        return source
    if source_digest == QUERY_CONSTRUCTOR_REGISTERED_BIND109_SHA256:
        assert suffix_digest == BIND109_GOLDEN_SUFFIX_SHA256 and source.endswith(additions)
        original = source[:-len(additions)]
        assert digest(original) == QUERY_CONSTRUCTOR_MAINTAINED_ORIGINAL_SHA256
        return original
    if source_digest == FIXTURE_MAINTAINED_ORIGINAL_SHA256:
        return source
    if source_digest in [FIXTURE_MAINTAINED_BIND97_SHA256, BIND109_REGISTERED_SOURCE_SHA256]:
        expected_suffix = (BIND97_GOLDEN_SUFFIX_SHA256 if source_digest == FIXTURE_MAINTAINED_BIND97_SHA256
                           else BIND109_GOLDEN_SUFFIX_SHA256)
        assert suffix_digest == expected_suffix and source.endswith(additions)
        original = source[:-len(additions)]
        assert digest(original) == FIXTURE_MAINTAINED_ORIGINAL_SHA256
        return original
    if source_digest == WINDOWS64_MAINTAINED_ORIGINAL_SHA256:
        return source
    if source_digest == WINDOWS64_MAINTAINED_BIND97_SHA256:
        assert suffix_digest == BIND97_GOLDEN_SUFFIX_SHA256
        assert source.endswith(additions)
        original = source[:-len(additions)]
        assert digest(original) == WINDOWS64_MAINTAINED_ORIGINAL_SHA256
        return original
    assert digest(source) == registered_digest
    assert source.endswith(additions)
    original = source[:-len(additions)]
    assert digest(original) == ORIGINAL_SOURCE_SHA256
    return original
