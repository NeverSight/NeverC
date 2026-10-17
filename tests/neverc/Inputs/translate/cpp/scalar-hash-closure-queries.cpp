#include <functional>
#include <tuple>
#include <utility>
#include <cstddef>

template<class T>
struct Queries {
  using H = std::hash<T>;
  using W = std::reference_wrapper<H>;
  using Tuple = std::tuple<const T&>;
  static_assert(__is_same(decltype(std::declval<const H&>()(std::declval<const T&>())), std::size_t));
  static_assert(__is_same(decltype(std::invoke(std::declval<H&>(), std::declval<const T&>())), std::size_t));
  static_assert(__is_same(decltype(std::apply(std::declval<H&>(), std::declval<Tuple&>())), std::size_t));
  static_assert(__is_same(decltype(std::declval<W&>()(std::declval<const T&>())), std::size_t));
  static_assert(__is_same(decltype(std::invoke(std::declval<W&>(), std::declval<const T&>())), std::size_t));
  static_assert(__is_same(decltype(std::apply(std::declval<W&>(), std::declval<Tuple&>())), std::size_t));
  static_assert(noexcept(std::apply(std::declval<H&>(), std::declval<Tuple&>())));
  static_assert(noexcept(std::apply(std::declval<W&>(), std::declval<Tuple&>())));
};
static_assert(sizeof(Queries<float>) == 1);
static_assert(sizeof(Queries<double>) == 1);
static_assert(sizeof(Queries<long long>) == 1);
static_assert(sizeof(Queries<unsigned long long>) == 1);
int main() { return 0; }
