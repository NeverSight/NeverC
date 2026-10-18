extern "C" void* user_word_copy(void* destination, const void*, __SIZE_TYPE__ size) { auto* bytes=static_cast<unsigned char*>(destination); for(__SIZE_TYPE__ i=0;i<size;++i) bytes[i]=0; return destination; }
#define memcpy user_word_copy
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <cstddef>
template<class V> struct Queries {
  using H = std::hash<V>;
  using W = std::reference_wrapper<H>;
  using T = std::tuple<const V&>;
  static_assert(__is_same(decltype(std::invoke(std::declval<const H&>(), std::declval<const V&>())), std::size_t));
  static_assert(__is_same(decltype(std::apply(std::declval<const H&>(), std::declval<T&>())), std::size_t));
  static_assert(__is_same(decltype(std::declval<const W&>()(std::declval<const V&>())), std::size_t));
  static_assert(__is_same(decltype(std::invoke(std::declval<const W&>(), std::declval<const V&>())), std::size_t));
  static_assert(__is_same(decltype(std::apply(std::declval<const W&>(), std::declval<T&>())), std::size_t));
  static_assert(noexcept(std::invoke(std::declval<const H&>(), std::declval<const V&>())));
  static_assert(noexcept(std::declval<const W&>()(std::declval<const V&>())));
  static_assert(noexcept(std::apply(std::declval<const H&>(), std::declval<T&>())));
  static_assert(noexcept(std::invoke(std::declval<const W&>(), std::declval<const V&>())));
  static_assert(noexcept(std::apply(std::declval<const W&>(), std::declval<T&>())));
  static_assert(sizeof(std::apply(std::declval<const H&>(), std::declval<T&>())) == sizeof(std::size_t));
};
static_assert(sizeof(Queries<std::string>) + sizeof(Queries<std::string_view>) > 0);
using H = std::hash<std::string_view>;
using W = std::reference_wrapper<H>;
using T = std::tuple<std::string_view>;
static_assert(__is_same(decltype(std::invoke(std::declval<H>(), std::string_view{})), std::size_t));
static_assert(__is_same(decltype(std::declval<W>()(std::string_view{})), std::size_t));
static_assert(__is_same(decltype(std::invoke(std::declval<W>(), std::string_view{})), std::size_t));
static_assert(__is_same(decltype(std::apply(std::declval<H>(), std::declval<T>())), std::size_t));
static_assert(__is_same(decltype(std::apply(std::declval<W>(), std::declval<T>())), std::size_t));
int main() { return 0; }
