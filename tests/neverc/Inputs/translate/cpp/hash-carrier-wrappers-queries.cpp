#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
template<class V> struct Queries {
  using H = std::hash<V>;
  using C = std::reference_wrapper<H>;
  using A = std::reference_wrapper<const V>;
  using T = std::tuple<A>;
  static_assert(__is_same(decltype(std::declval<H>()(std::declval<A>())), std::size_t));
  static_assert(__is_same(decltype(std::invoke(std::declval<H>(), std::declval<A>())), std::size_t));
  static_assert(__is_same(decltype(std::apply(std::declval<H>(), std::declval<T>())), std::size_t));
  static_assert(__is_same(decltype(std::declval<C>()(std::declval<A>())), std::size_t));
  static_assert(__is_same(decltype(std::invoke(std::declval<C>(), std::declval<A>())), std::size_t));
  static_assert(__is_same(decltype(std::apply(std::declval<C>(), std::declval<T>())), std::size_t));
  static_assert(noexcept(std::invoke(std::declval<H>(), std::declval<A>())) == noexcept(std::declval<H>()(std::declval<A>())));
  static_assert(noexcept(std::apply(std::declval<C>(), std::declval<T>())) == noexcept(std::declval<H>()(std::declval<A>())));
};
static_assert(sizeof(Queries<std::unique_ptr<int>>) + sizeof(Queries<std::unique_ptr<int[]>>) +
              sizeof(Queries<std::string>) + sizeof(Queries<std::string_view>) > 0);
int main() { return 0; }
