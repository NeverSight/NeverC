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

TEST_F(TranslateTest, CoreV2PointerTraitsSignatureQueryNestedWrapper) {
  const auto Source = tmpFile("pointer-traits-signature-nested-wrapper.cpp");
  const auto Output = tmpFile("pointer-traits-signature-nested-wrapper.nc");
  writeFile(Source, R"cpp(
#include <memory>
#include <functional>
#include <type_traits>
#include <utility>
using W=std::reference_wrapper<int>;using E=std::reference_wrapper<W>;
using T=std::pointer_traits<E*>;
static_assert(std::is_same<decltype(T::pointer_to(std::declval<E&>())),E*>::value,"result");
static_assert(noexcept(T::pointer_to(std::declval<E&>())),"nothrow");
int main(){return 0;}
)cpp");
  const auto Result =
      translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable =
        tmpFile("pointer-traits-signature-nested-wrapper" + Optimization);
    const auto Compile =
        compileGenerated(Output, Executable, Optimization, {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    const auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AddressofWrapperSignatureQueryNestedWrapper) {
  const auto Source = tmpFile("addressof-wrapper-signature-nested-wrapper.cpp");
  const auto Output = tmpFile("addressof-wrapper-signature-nested-wrapper.nc");
  writeFile(Source, R"cpp(
#include <memory>
#include <functional>
#include <type_traits>
#include <utility>
using W=std::reference_wrapper<int>;using E=std::reference_wrapper<W>;
static_assert(std::is_same<decltype(std::addressof(std::declval<E&>())),E*>::value,"result");
static_assert(noexcept(std::addressof(std::declval<E&>())),"nothrow");
int main(){return 0;}
)cpp");
  const auto Result =
      translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable =
        tmpFile("addressof-wrapper-signature-nested-wrapper" + Optimization);
    const auto Compile =
        compileGenerated(Output, Executable, Optimization, {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    const auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2WrapperReferenceCastSignatureQueryNestedAsConst) {
  const auto Source =
      tmpFile("wrapper-reference-cast-signature-nested-as-const.cpp");
  const auto Output =
      tmpFile("wrapper-reference-cast-signature-nested-as-const.nc");
  writeFile(Source, R"cpp(
#include <functional>
#include <utility>
#include <type_traits>
using W=std::reference_wrapper<int>;using E=std::reference_wrapper<W>;
static_assert(std::is_same<decltype(std::as_const(std::declval<E&>())),const E&>::value,"result");
static_assert(noexcept(std::as_const(std::declval<E&>())),"nothrow");
int main(){return 0;}
)cpp");
  const auto Result =
      translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile(
        "wrapper-reference-cast-signature-nested-as-const" + Optimization);
    const auto Compile =
        compileGenerated(Output, Executable, Optimization, {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    const auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2LaunderWrapperSignatureQueryNestedWrapper) {
  const auto Source = tmpFile("launder-wrapper-signature-nested-wrapper.cpp");
  const auto Output = tmpFile("launder-wrapper-signature-nested-wrapper.nc");
  writeFile(Source, R"cpp(
#include <new>
#include <functional>
#include <utility>
#include <type_traits>
using W=std::reference_wrapper<int>;using E=std::reference_wrapper<W>;
static_assert(std::is_same<decltype(std::launder(std::declval<E*>())),E*>::value,"result");
static_assert(noexcept(std::launder(std::declval<E*>())),"nothrow");
int main(){return 0;}
)cpp");
  const auto Result =
      translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable =
        tmpFile("launder-wrapper-signature-nested-wrapper" + Optimization);
    const auto Compile =
        compileGenerated(Output, Executable, Optimization, {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    const auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}
}
