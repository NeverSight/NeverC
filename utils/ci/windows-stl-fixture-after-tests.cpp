#include "NeverCTestFixture.h"
#include <algorithm>
#include <tuple>
#include <utility>
namespace {
class TranslateTest : public NeverCTest {
protected:
  std::vector<std::string> args(const fs::path &Source,
                                const std::vector<std::string> &Output) {
    std::vector<std::string> Args = {"translate", "--from", "cpp",
                                     Source.string()};
    Args.insert(Args.end(), Output.begin(), Output.end());
    return Args;
  }
  CmdResult translate(const fs::path &Source,
                      const std::vector<std::string> &Output) {
    auto Args = args(Source, Output);
    Args.insert(Args.end(), {"--", "-std=c++17"});
    return ncc(Args);
  }
  void expectNoArtifacts(const fs::path &Output) {
    for (const auto &Path : {Output, fs::path(Output.string() + ".map.json"),
                             fs::path(Output.string() + ".manifest.json")})
      EXPECT_FALSE(fs::exists(Path) || fs::is_symlink(Path)) << Path;
  }
  CmdResult compileGenerated(const fs::path &Source, const fs::path &Output,
                             const std::string &Optimization,
                             const std::vector<std::string> &Extra = {}) {
    std::vector<std::string> Args = {
        Source.string(),         Optimization, "-fno-lto",
        "-fno-builtin-mimalloc", "-o",         Output.string()};
    Args.insert(Args.end(), Extra.begin(), Extra.end());
    return ncc(Args);
  }
};
TEST_F(
    TranslateTest,
    CoreV2WrapperSetSymmetricDifferenceComparisonRuntimeRetainsSourceAndLifetimeBoundaries) {
  const std::pair<const char *, const char *> Cases[] = {
      {"casted-callee", R"cpp(
#include <functional>
#include <algorithm>
#include <utility>
using W=std::reference_wrapper<int>;using P=W*;
using F=P(*)(P,P,P,P,P);auto selected(P a,P b){return static_cast<F>(&std::set_symmetric_difference<P,P,P>)(a,b,a,b,a);}
)cpp"},
      {"enum-referents", R"cpp(
#include <functional>
#include <algorithm>
enum E{A,B};using W=std::reference_wrapper<E>;auto selected(W*a,W*b){return std::set_symmetric_difference(a,a+1,b,b+1,a,std::less<>{});}
)cpp"},
      {"expression-comparison", R"cpp(
#include <algorithm>
#include <functional>
using W=std::reference_wrapper<int>;using P=W*;
auto selected(P a,P b){return std::set_symmetric_difference(a,b,a,b,b,(0,std::less<>{}));}
)cpp"},
      {"indirect-callee", R"cpp(
#include <functional>
#include <algorithm>
#include <utility>
using W=std::reference_wrapper<int>;using P=W*;
using F=P(*)(P,P,P,P,P);auto selected(P a,P b){F f=&std::set_symmetric_difference<P,P,P>;return f(a,b,a,b,a);}
)cpp"},
      {"nested-wrappers", R"cpp(
#include <functional>
#include <algorithm>
using I=std::reference_wrapper<int>;using W=std::reference_wrapper<I>;namespace std{inline namespace __1{bool operator<(const W&a,const W&b){return a.get().get()<b.get().get();}}}
auto selected(W*a,W*b){return std::set_symmetric_difference(a,a+1,b,b+1,a,std::less<>{});}
)cpp"},
      {"returned-comparison", R"cpp(
#include <algorithm>
#include <functional>
using W=std::reference_wrapper<int>;using P=W*;
std::less<> predicate(){return{};}auto selected(P a,P b){return std::set_symmetric_difference(a,b,a,b,b,predicate());}
)cpp"},
      {"source-alias-argument", R"cpp(
#include <functional>
#include <algorithm>
#include <utility>
#include <type_traits>
template<int N>using Alias=std::reference_wrapper<int>;using W=Alias<sizeof(long double)>;auto selected(W*a,W*b){return std::set_symmetric_difference(a,a+1,b,b+1,a,std::less<>{});}
)cpp"},
      {"source-comparator-reference-alias-redeclaration", R"cpp(
#include <functional>
#include <algorithm>
#include <utility>
#include <iterator>
#include <type_traits>
using W=std::reference_wrapper<int>;using P=W*;
namespace std{inline namespace __1{template<class C>using __comp_ref_type=C&;}}
auto selected(P a,P b){return std::set_symmetric_difference(a,a+1,b,b+1,a,std::less<>{});}
)cpp"},
      {"source-exception-signature", R"cpp(
#include <functional>
#include <algorithm>
using W=std::reference_wrapper<int>;
W&source(W&w)noexcept(sizeof(long double)>0){return w;}void selected(W&a,W&b){std::set_symmetric_difference(&source(a),&a+1,&b,&b+1,&a,std::less<>{});}
)cpp"},
      {"source-forward-wrapper-mixed-qualification", R"cpp(
#include <algorithm>
#include <functional>
using W=std::reference_wrapper<int>;using P=const W*;
namespace std{inline namespace __1{template<>constexpr const W&forward<const W&>(const W&t)noexcept{return t;}}}
auto selected(P a,P b,W*other){return std::set_symmetric_difference(a,b,other,other+1,other,std::less<>{});}
)cpp"},
      {"source-forward-wrapper", R"cpp(
#include <algorithm>
#include <functional>
using W=std::reference_wrapper<int>;using P=W*;
namespace std{inline namespace __1{template<>constexpr W&forward<W&>(W&t)noexcept{return t;}}}
auto selected(P a,P b){return std::set_symmetric_difference(a,b,a,b,b,std::less<>{});}
)cpp"},
      {"source-greater-operation", R"cpp(
#include <algorithm>
#include <functional>
using W=std::reference_wrapper<int>;using P=W*;
namespace std{inline namespace __1{template<>constexpr auto greater<void>::operator()<W&,W&>(W&a,W&b)const noexcept(noexcept(std::forward<W&>(a)>std::forward<W&>(b)))->decltype(std::forward<W&>(a)>std::forward<W&>(b)){return a.get()>b.get();}}}
auto selected(P a,P b){return std::set_symmetric_difference(a,b,a,b,b,std::greater<>{});}
)cpp"},
      {"source-internal-copy-primary-redeclaration", R"cpp(
#include <functional>
#include <algorithm>
#include <utility>
#include <iterator>
#include <type_traits>
using W=std::reference_wrapper<int>;using P=W*;
namespace std{inline namespace __1{template<class I,class S,class O>_LIBCPP_CONSTEXPR_SINCE_CXX17 pair<I,O> __copy(I,S,O);}}
auto selected(P a,P b){return std::set_symmetric_difference(a,a+1,b,b+1,a,std::less<>{});}
)cpp"},
      {"source-internal-copy-specialization-declaration", R"cpp(
#include <functional>
#include <algorithm>
#include <utility>
#include <iterator>
#include <type_traits>
using W=std::reference_wrapper<int>;using P=W*;
namespace std{inline namespace __1{template<>pair<P,P> __copy<P,P,P>(P,P,P);}}
auto selected(P a,P b){return std::set_symmetric_difference(a,a+1,b,b+1,a,std::less<>{});}
)cpp"},
      {"source-internal-copy-specialization", R"cpp(
#include <functional>
#include <algorithm>
#include <utility>
#include <iterator>
#include <type_traits>
using W=std::reference_wrapper<int>;using P=W*;
namespace std{inline namespace __1{template<>pair<P,P> __copy<P,P,P>(P a,P b,P o){return {a,o};}}}
auto selected(P a,P b){return std::set_symmetric_difference(a,a+1,b,b+1,a,std::less<>{});}
)cpp"},
      {"source-less-operation-declaration", R"cpp(
#include <algorithm>
#include <functional>
using W=std::reference_wrapper<int>;using P=W*;
namespace std{inline namespace __1{template<>constexpr auto less<void>::operator()<W&,W&>(W&a,W&b)const noexcept(noexcept(std::forward<W&>(a)<std::forward<W&>(b)))->decltype(std::forward<W&>(a)<std::forward<W&>(b));}}
auto selected(P a,P b){return std::set_symmetric_difference(a,b,a,b,b,std::less<>{});}
)cpp"},
      {"source-less-operation-mixed-qualification", R"cpp(
#include <algorithm>
#include <functional>
using W=std::reference_wrapper<int>;using P=const W*;
namespace std{inline namespace __1{template<>constexpr auto less<void>::operator()<W&,const W&>(W&a,const W&b)const noexcept(noexcept(std::forward<W&>(a)<std::forward<const W&>(b)))->decltype(std::forward<W&>(a)<std::forward<const W&>(b)){return a.get()<b.get();}}}
auto selected(P a,P b,W*other){return std::set_symmetric_difference(a,b,other,other+1,other,std::less<>{});}
)cpp"},
      {"source-less-operation", R"cpp(
#include <algorithm>
#include <functional>
using W=std::reference_wrapper<int>;using P=W*;
namespace std{inline namespace __1{template<>constexpr auto less<void>::operator()<W&,W&>(W&a,W&b)const noexcept(noexcept(std::forward<W&>(a)<std::forward<W&>(b)))->decltype(std::forward<W&>(a)<std::forward<W&>(b)){return a.get()<b.get();}}}
auto selected(P a,P b){return std::set_symmetric_difference(a,b,a,b,b,std::less<>{});}
)cpp"},
      {"source-less-primary", R"cpp(
#include <algorithm>
#include <functional>
using W=std::reference_wrapper<int>;using P=W*;
namespace std{inline namespace __1{template<class T>struct less;}}
auto selected(P a,P b){return std::set_symmetric_difference(a,b,a,b,b,std::less<>{});}
)cpp"},
      {"source-less-record", R"cpp(
#include <algorithm>
#include <functional>
using W=std::reference_wrapper<int>;using P=W*;
namespace std{inline namespace __1{template<>struct less<void>;}}
auto selected(P a,P b){return std::set_symmetric_difference(a,b,a,b,b,std::less<>{});}
)cpp"},
      {"source-move-specialization-declaration", R"cpp(
#include <functional>
#include <algorithm>
#include <utility>
using W=std::reference_wrapper<int>;using P=W*;
namespace std{inline namespace __1{template<>_LIBCPP_CONSTEXPR P&& move<P&>(P&)noexcept;}}
auto selected(P a,P b,P o){return std::set_symmetric_difference(a,a+1,b,b+1,o,std::less<>{});}
)cpp"},
      {"source-move-specialization", R"cpp(
#include <functional>
#include <algorithm>
#include <utility>
using W=std::reference_wrapper<int>;using P=W*;
namespace std{inline namespace __1{template<>_LIBCPP_CONSTEXPR P&& move<P&>(P&p)noexcept{return static_cast<P&&>(p);}}}
auto selected(P a,P b,P o){return std::set_symmetric_difference(a,a+1,b,b+1,o,std::less<>{});}
)cpp"},
      {"source-operand-body", R"cpp(
#include <functional>
#include <algorithm>
using W=std::reference_wrapper<int>;W&source(W&w){long double unsupported=1;return w;}auto selected(W&a,W&b){return std::set_symmetric_difference(&source(a),&a+1,&b,&b+1,&a,std::less<>{});}
)cpp"},
      {"source-operand-default", R"cpp(
#include <functional>
#include <algorithm>
#include <utility>
#include <type_traits>
using W=std::reference_wrapper<int>;
W&source(W&w,int n=sizeof(long double)){return w;}void selected(W&a,W&b){std::set_symmetric_difference(&source(a),&a+1,&b,&b+1,&a,std::less<>{});}int main(){return 0;}
)cpp"},
      {"source-record-layout", R"cpp(
#include <functional>
#include <algorithm>
struct R{long double n;};bool operator<(const R&a,const R&b){return a.n<b.n;}using W=std::reference_wrapper<R>;auto selected(W*a,W*b){return std::set_symmetric_difference(a,a+1,b,b+1,a,std::less<>{});}
)cpp"},
      {"source-referent-conversion-declaration", R"cpp(
#include <functional>
#include <algorithm>
using W=std::reference_wrapper<int>;using P=W*;
namespace std{inline namespace __1{template<>reference_wrapper<int>::operator int&()const noexcept;}}
auto selected(W*a,W*b){return std::set_symmetric_difference(a,a+1,b,b+1,a,std::less<>{});}
)cpp"},
      {"source-referent-conversion", R"cpp(
#include <functional>
#include <algorithm>
using W=std::reference_wrapper<int>;using P=W*;
namespace std{inline namespace __1{template<>reference_wrapper<int>::operator int&()const noexcept{return get();}}}
auto selected(W*a,W*b){return std::set_symmetric_difference(a,a+1,b,b+1,a,std::less<>{});}
)cpp"},
      {"source-referent-equality", R"cpp(
#include <functional>
#include <algorithm>
struct R{int n;};bool operator<(const R&a,const R&b){return a.n<b.n;}using W=std::reference_wrapper<R>;auto selected(W*a,W*b){return std::set_symmetric_difference(a,a+1,b,b+1,a,std::less<>{});}
)cpp"},
      {"source-result-constructor-specialization-declaration", R"cpp(
#include <functional>
#include <algorithm>
#include <utility>
#include <iterator>
#include <type_traits>
using W=std::reference_wrapper<int>;using P=W*;
namespace std{inline namespace __1{template<>__set_symmetric_difference_result<P,P,P>::__set_symmetric_difference_result(P&&,P&&,P&&);}}
auto selected(P a,P b){return std::set_symmetric_difference(a,a+1,b,b+1,a,std::less<>{});}
)cpp"},
      {"source-result-record-primary-redeclaration", R"cpp(
#include <functional>
#include <algorithm>
#include <utility>
#include <iterator>
#include <type_traits>
using W=std::reference_wrapper<int>;using P=W*;
namespace std{inline namespace __1{template<class A,class B,class C>struct __set_symmetric_difference_result;}}
auto selected(P a,P b){return std::set_symmetric_difference(a,a+1,b,b+1,a,std::less<>{});}
)cpp"},
      {"source-set-symmetric-difference-loop-primary-redeclaration", R"cpp(
#include <functional>
#include <algorithm>
#include <utility>
#include <iterator>
#include <type_traits>
using W=std::reference_wrapper<int>;using P=W*;
namespace std{inline namespace __1{template<class C,class I1,class S1,class I2,class S2,class O>__set_symmetric_difference_result<I1,I2,O> __set_symmetric_difference(I1,S1,I2,S2,O,C&&);}}
auto selected(P a,P b){return std::set_symmetric_difference(a,a+1,b,b+1,a,std::less<>{});}
)cpp"},
      {"source-set-symmetric-difference-loop-specialization-declaration", R"cpp(
#include <functional>
#include <algorithm>
#include <utility>
#include <iterator>
#include <type_traits>
using W=std::reference_wrapper<int>;using P=W*;
namespace std{inline namespace __1{template<>__set_symmetric_difference_result<P,P,P> __set_symmetric_difference<less<void>&,P,P,P,P,P>(P,P,P,P,P,less<void>&);}}
auto selected(P a,P b){return std::set_symmetric_difference(a,a+1,b,b+1,a,std::less<>{});}
)cpp"},
      {"source-set-symmetric-difference-predicate-primary-redeclaration", R"cpp(
#include <functional>
#include <algorithm>
#include <utility>
#include <iterator>
#include <type_traits>
using W=std::reference_wrapper<int>;using P=W*;
namespace std{inline namespace __1{template<class I,class J,class O,class C>O set_symmetric_difference(I,I,J,J,O,C);}}
auto selected(P a,P b){return std::set_symmetric_difference(a,a+1,b,b+1,a,std::less<>{});}
)cpp"},
      {"source-set-symmetric-difference-predicate-specialization-declaration",
       R"cpp(
#include <functional>
#include <algorithm>
#include <utility>
#include <iterator>
#include <type_traits>
using W=std::reference_wrapper<int>;using P=W*;
namespace std{inline namespace __1{template<>P set_symmetric_difference<P,P,P,less<void>>(P,P,P,P,P,less<void>);}}
auto selected(P a,P b){return std::set_symmetric_difference(a,a+1,b,b+1,a,std::less<>{});}
)cpp"},
      {"source-set-symmetric-difference-predicate-specialization", R"cpp(
#include <functional>
#include <algorithm>
#include <utility>
#include <iterator>
#include <type_traits>
using W=std::reference_wrapper<int>;using P=W*;
namespace std{inline namespace __1{template<>P set_symmetric_difference<P,P,P,less<void>>(P,P,P,P,P,less<void>){return nullptr;}}}
auto selected(P a,P b){return std::set_symmetric_difference(a,a+1,b,b+1,a,std::less<>{});}
)cpp"},
      {"source-wrapper-primary-redeclaration", R"cpp(
#include <functional>
#include <algorithm>
#include <utility>
#include <type_traits>
namespace std{inline namespace __1{template<class T>class reference_wrapper;}}
using W=std::reference_wrapper<int>;
auto selected(W*a,W*b){return std::set_symmetric_difference(a,a+1,b,b+1,a,std::less<>{});}
)cpp"},
      {"stored-comparison", R"cpp(
#include <algorithm>
#include <functional>
using W=std::reference_wrapper<int>;using P=W*;
auto selected(P a,P b){std::less<> p;return std::set_symmetric_difference(a,b,a,b,b,p);}
)cpp"},
      {"typed-comparison", R"cpp(
#include <algorithm>
#include <functional>
using W=std::reference_wrapper<int>;using P=W*;
auto selected(P a,P b){return std::set_symmetric_difference(a,b,a,b,b,std::less<W>{});}
)cpp"},
      {"using-independent-function-address", R"cpp(
#include <functional>
#include <algorithm>
#include <utility>
using W=std::reference_wrapper<int>;using P=W*;
using F=P(*)(P,P,P,P,P);F selected(){using std::set_symmetric_difference;return &set_symmetric_difference<P,P,P>;}
)cpp"},
  };
  for (const auto &Case : Cases) {
    SCOPED_TRACE(Case.first);
    const auto Source = tmpFile(
        std::string(
            "wrapper-set-symmetric-difference-comparison-runtime-guard-") +
        Case.first + ".cpp");
    const auto Output = tmpFile(
        std::string(
            "wrapper-set-symmetric-difference-comparison-runtime-guard-") +
        Case.first + ".nc");
    writeFile(Source, Case.second);
    const auto Result =
        translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    EXPECT_NE(Result.exitCode, 0) << Result.out << Result.err;
    EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
                Result.err.find("TR0203") != std::string::npos)
        << Result.out << Result.err;
    expectNoArtifacts(Output);
  }
}

TEST_F(TranslateTest, CoreV2StringHashAdaptersRejectSelectedSourcesAndConversions) {
  struct Case { const char *Name; const char *Source; };
  const Case Cases[] = {
    {"string_view-public-redeclaration-invoke", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
namespace std { inline namespace __1 { template<>struct hash<string_view>; }}
int main(){std::string_view value("abcdefghijklmnop",16);std::hash<std::string_view> hash;auto wrapper=std::ref(hash);return std::invoke(hash,value)==0;}
)cpp"},
    {"string_view-public-redeclaration-apply", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
namespace std { inline namespace __1 { template<>struct hash<string_view>; }}
int main(){std::string_view value("abcdefghijklmnop",16);std::hash<std::string_view> hash;auto wrapper=std::ref(hash);return std::apply(hash,std::tie(value))==0;}
)cpp"},
    {"string_view-public-redeclaration-wrapper", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
namespace std { inline namespace __1 { template<>struct hash<string_view>; }}
int main(){std::string_view value("abcdefghijklmnop",16);std::hash<std::string_view> hash;auto wrapper=std::ref(hash);return wrapper(value)==0;}
)cpp"},
    {"string_view-public-redeclaration-invoke-wrapper", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
namespace std { inline namespace __1 { template<>struct hash<string_view>; }}
int main(){std::string_view value("abcdefghijklmnop",16);std::hash<std::string_view> hash;auto wrapper=std::ref(hash);return std::invoke(wrapper,value)==0;}
)cpp"},
    {"string_view-public-redeclaration-apply-wrapper", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
namespace std { inline namespace __1 { template<>struct hash<string_view>; }}
int main(){std::string_view value("abcdefghijklmnop",16);std::hash<std::string_view> hash;auto wrapper=std::ref(hash);return std::apply(wrapper,std::tie(value))==0;}
)cpp"},
    {"string_view-helper-specialization-invoke", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
namespace std { inline namespace __1 { template<>inline size_t __do_string_hash<const char*>(const char*,const char*){return 17;} }}
int main(){std::string_view value("abcdefghijklmnop",16);std::hash<std::string_view> hash;auto wrapper=std::ref(hash);return std::invoke(hash,value)==0;}
)cpp"},
    {"string_view-helper-specialization-apply", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
namespace std { inline namespace __1 { template<>inline size_t __do_string_hash<const char*>(const char*,const char*){return 17;} }}
int main(){std::string_view value("abcdefghijklmnop",16);std::hash<std::string_view> hash;auto wrapper=std::ref(hash);return std::apply(hash,std::tie(value))==0;}
)cpp"},
    {"string_view-helper-specialization-wrapper", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
namespace std { inline namespace __1 { template<>inline size_t __do_string_hash<const char*>(const char*,const char*){return 17;} }}
int main(){std::string_view value("abcdefghijklmnop",16);std::hash<std::string_view> hash;auto wrapper=std::ref(hash);return wrapper(value)==0;}
)cpp"},
    {"string_view-helper-specialization-invoke-wrapper", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
namespace std { inline namespace __1 { template<>inline size_t __do_string_hash<const char*>(const char*,const char*){return 17;} }}
int main(){std::string_view value("abcdefghijklmnop",16);std::hash<std::string_view> hash;auto wrapper=std::ref(hash);return std::invoke(wrapper,value)==0;}
)cpp"},
    {"string_view-helper-specialization-apply-wrapper", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
namespace std { inline namespace __1 { template<>inline size_t __do_string_hash<const char*>(const char*,const char*){return 17;} }}
int main(){std::string_view value("abcdefghijklmnop",16);std::hash<std::string_view> hash;auto wrapper=std::ref(hash);return std::apply(wrapper,std::tie(value))==0;}
)cpp"},
    {"string_view-copy-source-invoke", R"cpp(extern "C" void* user_word_copy(void* destination, const void*, __SIZE_TYPE__ size) { auto* bytes=static_cast<unsigned char*>(destination); for(__SIZE_TYPE__ i=0;i<size;++i) bytes[i]=0; return destination; }
#define memcpy user_word_copy
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int main(){std::string_view value("abcdefghijklmnop",16);std::hash<std::string_view> hash;auto wrapper=std::ref(hash);return std::invoke(hash,value)==0;}
)cpp"},
    {"string_view-copy-source-apply", R"cpp(extern "C" void* user_word_copy(void* destination, const void*, __SIZE_TYPE__ size) { auto* bytes=static_cast<unsigned char*>(destination); for(__SIZE_TYPE__ i=0;i<size;++i) bytes[i]=0; return destination; }
#define memcpy user_word_copy
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int main(){std::string_view value("abcdefghijklmnop",16);std::hash<std::string_view> hash;auto wrapper=std::ref(hash);return std::apply(hash,std::tie(value))==0;}
)cpp"},
    {"string_view-copy-source-wrapper", R"cpp(extern "C" void* user_word_copy(void* destination, const void*, __SIZE_TYPE__ size) { auto* bytes=static_cast<unsigned char*>(destination); for(__SIZE_TYPE__ i=0;i<size;++i) bytes[i]=0; return destination; }
#define memcpy user_word_copy
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int main(){std::string_view value("abcdefghijklmnop",16);std::hash<std::string_view> hash;auto wrapper=std::ref(hash);return wrapper(value)==0;}
)cpp"},
    {"string_view-copy-source-invoke-wrapper", R"cpp(extern "C" void* user_word_copy(void* destination, const void*, __SIZE_TYPE__ size) { auto* bytes=static_cast<unsigned char*>(destination); for(__SIZE_TYPE__ i=0;i<size;++i) bytes[i]=0; return destination; }
#define memcpy user_word_copy
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int main(){std::string_view value("abcdefghijklmnop",16);std::hash<std::string_view> hash;auto wrapper=std::ref(hash);return std::invoke(wrapper,value)==0;}
)cpp"},
    {"string_view-copy-source-apply-wrapper", R"cpp(extern "C" void* user_word_copy(void* destination, const void*, __SIZE_TYPE__ size) { auto* bytes=static_cast<unsigned char*>(destination); for(__SIZE_TYPE__ i=0;i<size;++i) bytes[i]=0; return destination; }
#define memcpy user_word_copy
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int main(){std::string_view value("abcdefghijklmnop",16);std::hash<std::string_view> hash;auto wrapper=std::ref(hash);return std::apply(wrapper,std::tie(value))==0;}
)cpp"},
    {"string_view-abi2-invoke", R"cpp(#define _LIBCPP_ABI_FIX_CITYHASH_IMPLEMENTATION
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int main(){std::string_view value("abcdefghijklmnop",16);std::hash<std::string_view> hash;auto wrapper=std::ref(hash);return std::invoke(hash,value)==0;}
)cpp"},
    {"string_view-abi2-apply", R"cpp(#define _LIBCPP_ABI_FIX_CITYHASH_IMPLEMENTATION
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int main(){std::string_view value("abcdefghijklmnop",16);std::hash<std::string_view> hash;auto wrapper=std::ref(hash);return std::apply(hash,std::tie(value))==0;}
)cpp"},
    {"string_view-abi2-wrapper", R"cpp(#define _LIBCPP_ABI_FIX_CITYHASH_IMPLEMENTATION
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int main(){std::string_view value("abcdefghijklmnop",16);std::hash<std::string_view> hash;auto wrapper=std::ref(hash);return wrapper(value)==0;}
)cpp"},
    {"string_view-abi2-invoke-wrapper", R"cpp(#define _LIBCPP_ABI_FIX_CITYHASH_IMPLEMENTATION
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int main(){std::string_view value("abcdefghijklmnop",16);std::hash<std::string_view> hash;auto wrapper=std::ref(hash);return std::invoke(wrapper,value)==0;}
)cpp"},
    {"string_view-abi2-apply-wrapper", R"cpp(#define _LIBCPP_ABI_FIX_CITYHASH_IMPLEMENTATION
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int main(){std::string_view value("abcdefghijklmnop",16);std::hash<std::string_view> hash;auto wrapper=std::ref(hash);return std::apply(wrapper,std::tie(value))==0;}
)cpp"},
    {"string_view-copy-source-ignored", R"cpp(extern "C" void* user_word_copy(void* destination, const void*, __SIZE_TYPE__ size) { auto* bytes=static_cast<unsigned char*>(destination); for(__SIZE_TYPE__ i=0;i<size;++i) bytes[i]=0; return destination; }
#define memcpy user_word_copy
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int main(){std::string_view value("abcdefghijklmnop",16);std::hash<std::string_view> hash;std::invoke(hash,value);return 0;}
)cpp"},
    {"string_view-helper-specialization-ignored", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
namespace std { inline namespace __1 { template<>inline size_t __do_string_hash<const char*>(const char*,const char*){return 17;} }}
int main(){std::string_view value("abcdefghijklmnop",16);std::hash<std::string_view> hash;std::invoke(hash,value);return 0;}
)cpp"},
    {"string-public-redeclaration-invoke", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
namespace std { inline namespace __1 { template<class A>struct hash<basic_string<char,char_traits<char>,A>>; }}
int main(){std::string value("abcdefghijklmnop",16);std::hash<std::string> hash;auto wrapper=std::ref(hash);return std::invoke(hash,value)==0;}
)cpp"},
    {"string-public-redeclaration-apply", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
namespace std { inline namespace __1 { template<class A>struct hash<basic_string<char,char_traits<char>,A>>; }}
int main(){std::string value("abcdefghijklmnop",16);std::hash<std::string> hash;auto wrapper=std::ref(hash);return std::apply(hash,std::tie(value))==0;}
)cpp"},
    {"string-public-redeclaration-wrapper", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
namespace std { inline namespace __1 { template<class A>struct hash<basic_string<char,char_traits<char>,A>>; }}
int main(){std::string value("abcdefghijklmnop",16);std::hash<std::string> hash;auto wrapper=std::ref(hash);return wrapper(value)==0;}
)cpp"},
    {"string-public-redeclaration-invoke-wrapper", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
namespace std { inline namespace __1 { template<class A>struct hash<basic_string<char,char_traits<char>,A>>; }}
int main(){std::string value("abcdefghijklmnop",16);std::hash<std::string> hash;auto wrapper=std::ref(hash);return std::invoke(wrapper,value)==0;}
)cpp"},
    {"string-public-redeclaration-apply-wrapper", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
namespace std { inline namespace __1 { template<class A>struct hash<basic_string<char,char_traits<char>,A>>; }}
int main(){std::string value("abcdefghijklmnop",16);std::hash<std::string> hash;auto wrapper=std::ref(hash);return std::apply(wrapper,std::tie(value))==0;}
)cpp"},
    {"string-helper-specialization-invoke", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
namespace std { inline namespace __1 { template<>inline size_t __do_string_hash<const char*>(const char*,const char*){return 17;} }}
int main(){std::string value("abcdefghijklmnop",16);std::hash<std::string> hash;auto wrapper=std::ref(hash);return std::invoke(hash,value)==0;}
)cpp"},
    {"string-helper-specialization-apply", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
namespace std { inline namespace __1 { template<>inline size_t __do_string_hash<const char*>(const char*,const char*){return 17;} }}
int main(){std::string value("abcdefghijklmnop",16);std::hash<std::string> hash;auto wrapper=std::ref(hash);return std::apply(hash,std::tie(value))==0;}
)cpp"},
    {"string-helper-specialization-wrapper", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
namespace std { inline namespace __1 { template<>inline size_t __do_string_hash<const char*>(const char*,const char*){return 17;} }}
int main(){std::string value("abcdefghijklmnop",16);std::hash<std::string> hash;auto wrapper=std::ref(hash);return wrapper(value)==0;}
)cpp"},
    {"string-helper-specialization-invoke-wrapper", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
namespace std { inline namespace __1 { template<>inline size_t __do_string_hash<const char*>(const char*,const char*){return 17;} }}
int main(){std::string value("abcdefghijklmnop",16);std::hash<std::string> hash;auto wrapper=std::ref(hash);return std::invoke(wrapper,value)==0;}
)cpp"},
    {"string-helper-specialization-apply-wrapper", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
namespace std { inline namespace __1 { template<>inline size_t __do_string_hash<const char*>(const char*,const char*){return 17;} }}
int main(){std::string value("abcdefghijklmnop",16);std::hash<std::string> hash;auto wrapper=std::ref(hash);return std::apply(wrapper,std::tie(value))==0;}
)cpp"},
    {"string-copy-source-invoke", R"cpp(extern "C" void* user_word_copy(void* destination, const void*, __SIZE_TYPE__ size) { auto* bytes=static_cast<unsigned char*>(destination); for(__SIZE_TYPE__ i=0;i<size;++i) bytes[i]=0; return destination; }
#define memcpy user_word_copy
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int main(){std::string value("abcdefghijklmnop",16);std::hash<std::string> hash;auto wrapper=std::ref(hash);return std::invoke(hash,value)==0;}
)cpp"},
    {"string-copy-source-apply", R"cpp(extern "C" void* user_word_copy(void* destination, const void*, __SIZE_TYPE__ size) { auto* bytes=static_cast<unsigned char*>(destination); for(__SIZE_TYPE__ i=0;i<size;++i) bytes[i]=0; return destination; }
#define memcpy user_word_copy
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int main(){std::string value("abcdefghijklmnop",16);std::hash<std::string> hash;auto wrapper=std::ref(hash);return std::apply(hash,std::tie(value))==0;}
)cpp"},
    {"string-copy-source-wrapper", R"cpp(extern "C" void* user_word_copy(void* destination, const void*, __SIZE_TYPE__ size) { auto* bytes=static_cast<unsigned char*>(destination); for(__SIZE_TYPE__ i=0;i<size;++i) bytes[i]=0; return destination; }
#define memcpy user_word_copy
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int main(){std::string value("abcdefghijklmnop",16);std::hash<std::string> hash;auto wrapper=std::ref(hash);return wrapper(value)==0;}
)cpp"},
    {"string-copy-source-invoke-wrapper", R"cpp(extern "C" void* user_word_copy(void* destination, const void*, __SIZE_TYPE__ size) { auto* bytes=static_cast<unsigned char*>(destination); for(__SIZE_TYPE__ i=0;i<size;++i) bytes[i]=0; return destination; }
#define memcpy user_word_copy
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int main(){std::string value("abcdefghijklmnop",16);std::hash<std::string> hash;auto wrapper=std::ref(hash);return std::invoke(wrapper,value)==0;}
)cpp"},
    {"string-copy-source-apply-wrapper", R"cpp(extern "C" void* user_word_copy(void* destination, const void*, __SIZE_TYPE__ size) { auto* bytes=static_cast<unsigned char*>(destination); for(__SIZE_TYPE__ i=0;i<size;++i) bytes[i]=0; return destination; }
#define memcpy user_word_copy
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int main(){std::string value("abcdefghijklmnop",16);std::hash<std::string> hash;auto wrapper=std::ref(hash);return std::apply(wrapper,std::tie(value))==0;}
)cpp"},
    {"string-abi2-invoke", R"cpp(#define _LIBCPP_ABI_FIX_CITYHASH_IMPLEMENTATION
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int main(){std::string value("abcdefghijklmnop",16);std::hash<std::string> hash;auto wrapper=std::ref(hash);return std::invoke(hash,value)==0;}
)cpp"},
    {"string-abi2-apply", R"cpp(#define _LIBCPP_ABI_FIX_CITYHASH_IMPLEMENTATION
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int main(){std::string value("abcdefghijklmnop",16);std::hash<std::string> hash;auto wrapper=std::ref(hash);return std::apply(hash,std::tie(value))==0;}
)cpp"},
    {"string-abi2-wrapper", R"cpp(#define _LIBCPP_ABI_FIX_CITYHASH_IMPLEMENTATION
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int main(){std::string value("abcdefghijklmnop",16);std::hash<std::string> hash;auto wrapper=std::ref(hash);return wrapper(value)==0;}
)cpp"},
    {"string-abi2-invoke-wrapper", R"cpp(#define _LIBCPP_ABI_FIX_CITYHASH_IMPLEMENTATION
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int main(){std::string value("abcdefghijklmnop",16);std::hash<std::string> hash;auto wrapper=std::ref(hash);return std::invoke(wrapper,value)==0;}
)cpp"},
    {"string-abi2-apply-wrapper", R"cpp(#define _LIBCPP_ABI_FIX_CITYHASH_IMPLEMENTATION
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int main(){std::string value("abcdefghijklmnop",16);std::hash<std::string> hash;auto wrapper=std::ref(hash);return std::apply(wrapper,std::tie(value))==0;}
)cpp"},
    {"string-copy-source-ignored", R"cpp(extern "C" void* user_word_copy(void* destination, const void*, __SIZE_TYPE__ size) { auto* bytes=static_cast<unsigned char*>(destination); for(__SIZE_TYPE__ i=0;i<size;++i) bytes[i]=0; return destination; }
#define memcpy user_word_copy
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int main(){std::string value("abcdefghijklmnop",16);std::hash<std::string> hash;std::invoke(hash,value);return 0;}
)cpp"},
    {"string-helper-specialization-ignored", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
namespace std { inline namespace __1 { template<>inline size_t __do_string_hash<const char*>(const char*,const char*){return 17;} }}
int main(){std::string value("abcdefghijklmnop",16);std::hash<std::string> hash;std::invoke(hash,value);return 0;}
)cpp"},
    {"private-view-base-invoke", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int main(){return std::invoke(std::__string_view_hash<char>{},std::string_view("abc"))==0;}
)cpp"},
    {"private-string-base-invoke", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int main(){std::string value("abc");return std::invoke(std::__string_hash<char,std::allocator<char>>{},value)==0;}
)cpp"},
    {"private-byte-hasher-invoke", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int main(){return std::invoke(std::__murmur2_or_cityhash<std::size_t>{},"abc",3)==0;}
)cpp"},
    {"private-view-helper-apply", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int main(){return std::apply(std::__string_view_hash<char>{},std::tuple<std::string_view>{std::string_view("abc")})==0;}
)cpp"},
    {"owner-view-source-conversion", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
struct Converted{std::string_view*value;operator const std::string_view&()const noexcept{return *value;}};int main(){std::hash<std::string_view> hash;std::string_view value("abc");return std::invoke(hash,Converted{&value})==0;}
)cpp"},
    {"owner-string-source-conversion", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
struct Converted{std::string*value;operator const std::string&()const noexcept{return *value;}};int main(){std::hash<std::string> hash;std::string value("abc");return std::invoke(hash,Converted{&value})==0;}
)cpp"},
    {"conversion-view-argument", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int main(){std::hash<std::string_view> hash;return std::invoke(hash,"abc")==0;}
)cpp"},
    {"wide-view-adapter", R"cpp(#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int main(){return std::invoke(std::hash<std::u16string_view>{},std::u16string_view(u"abc"))==0;}
)cpp"},
    {"nested-view-value-default", R"cpp(#include <string_view>
#include <tuple>
int main() {
  std::tuple<std::tuple<std::string_view>> value{};
  return std::get<0>(std::get<0>(value)).size() != 0;
}
)cpp"},
    {"nested-string-child-source-replacement", R"cpp(#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <type_traits>
namespace std{template<>class tuple<std::string>{public:std::string value;explicit tuple(std::string v):value(std::move(v)){}tuple(const tuple&)=default;tuple(tuple&&)=default;};}
using V=std::string;using Inner=std::tuple<V>;using Outer=std::tuple<Inner>;
int main(){Inner inner(std::string(193,'q'));Outer outer(inner);return 0;}
)cpp"},
  };
  for (const auto &Case : Cases) {
    SCOPED_TRACE(Case.Name);
    const auto Source = tmpFile(std::string("string-hash-adapter-guard-") + Case.Name + ".cpp");
    const auto Output = tmpFile(std::string("string-hash-adapter-guard-") + Case.Name + ".nc");
    writeFile(Source, Case.Source);
    const auto Result = translate(Source,
        {"--profile", "cpp-core-v2", "-o", Output.string()});
    EXPECT_NE(Result.exitCode, 0) << Result.out << Result.err;
    EXPECT_EQ(Result.err.find("TR0101"), std::string::npos) << Result.err;
    EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
                Result.err.find("TR0203") != std::string::npos) << Result.err;
    expectNoArtifacts(Output);
  }
}

TEST_F(TranslateTest, CoreV2OwnedPairConstructorDefaultsPreserveLifetimes) {
  const auto Source = tmpFile("pair-construction-default-lifetimes.cpp");
  const auto Output = tmpFile("pair-construction-default-lifetimes.nc");
  writeFile(Source, R"cpp(#include <utility>
int live, calls, drops, copies, moves, bad, events[96], count;
int outer_live, outer_drops, expected_outer, operands, picks;
void note(int n) { events[count++] = n; }
struct Guard {
  Guard() noexcept {
    ++live;
    ++calls;
    note(1);
  }
  ~Guard() noexcept(false) {
    --live;
    ++drops;
    note(3);
  }
};
struct Outer {
  Outer() noexcept { ++outer_live; }
  ~Outer() noexcept {
    --outer_live;
    ++outer_drops;
  }
};
struct Item {
  int value;
  Item(int n) noexcept : value(n) {}
  Item(const Item &v, const Guard & = Guard{}, int = 0) noexcept
      : value(v.value) {
    ++copies;
    note(2);
    if (live != 1 || outer_live != expected_outer)
      ++bad;
  }
  Item(Item &&v, const Guard & = Guard{}, int = 0) noexcept : value(v.value) {
    ++moves;
    note(2);
    if (live != 1 || outer_live != expected_outer)
      ++bad;
    v.value = -1;
  }
  ~Item() noexcept {
    if (live)
      ++bad;
  }
};
Item make(int n, const Outer & = Outer{}) noexcept {
  ++operands;
  return Item(n);
}
void reset() { calls = drops = copies = moves = count = 0; }
bool good(int c, int m) {
  if (copies != c || moves != m || calls != c + m || drops != c + m || live ||
      bad || count != 3 * (c + m))
    return false;
  for (int i = 0; i < count; ++i)
    if (events[i] != i % 3 + 1)
      return false;
  return true;
}
using Pair = std::pair<Item, Item>;
Pair &pick(Pair &p) noexcept {
  ++picks;
  return p;
}
int main() {
  expected_outer = 2;
  Pair p(make(1), make(2));
  if (!good(0, 2) || operands != 2 || outer_live || outer_drops != 2 ||
      p.first.value != 1 || p.second.value != 2)
    return 1;
  reset();
  auto q = std::make_pair(make(3), make(4));
  if (!good(0, 2) || operands != 4 || outer_live || outer_drops != 4 ||
      q.first.value != 3 || q.second.value != 4)
    return 2;
  expected_outer = 0;
  reset();
  Pair c(p);
  if (!good(2, 0) || c.first.value != 1 || c.second.value != 2)
    return 3;
  reset();
  Pair m(std::move(pick(c)));
  if (!good(0, 2) || picks != 1 || m.first.value != 1 || m.second.value != 2 ||
      c.first.value != -1 || c.second.value != -1)
    return 4;
  std::pair<Item &, Item &> refs(m.first, m.second);
  reset();
  Pair converted(refs);
  if (!good(2, 0) || converted.first.value != 1 || converted.second.value != 2)
    return 5;
  reset();
  Pair copied_references(std::move(refs));
  if (!good(2, 0) || m.first.value != 1 || m.second.value != 2 ||
      copied_references.second.value != 2)
    return 6;
  reset();
  const Item fixed(7);
  Item mutable_item(8);
  std::pair<Item, Item &> mixed(fixed, mutable_item);
  if (!good(1, 0) || mixed.first.value != 7)
    return 7;
  mixed.second.value = 9;
  return mutable_item.value == 9 ? 0 : 8;
}
)cpp");
  auto Result =
      translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable =
        tmpFile("pair-construction-default-lifetimes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}
}
