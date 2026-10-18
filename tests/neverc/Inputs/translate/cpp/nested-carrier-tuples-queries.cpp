#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

using Leaf = std::tuple<std::string, std::string_view>;
using Row = std::tuple<Leaf, int>;
using Deep = std::tuple<Row, int>;
using P = std::unique_ptr<int>;
using Owners = std::tuple<std::tuple<P>, int>;
struct Read {
  std::size_t operator()(const Leaf&, int) const noexcept { return 0; }
};
static_assert(__is_same(decltype(std::get<0>(std::declval<Row&>())), Leaf&));
static_assert(__is_same(decltype(std::get<0>(std::declval<const Row&>())), const Leaf&));
static_assert(__is_same(decltype(std::get<0>(std::declval<Row&&>())), Leaf&&));
static_assert(__is_same(decltype(std::get<0>(std::get<0>(std::declval<Owners&>()))), P&));
static_assert(sizeof(Row) >= sizeof(Leaf) + sizeof(int));
static_assert(alignof(Owners) >= alignof(P));
static_assert(__is_same(decltype(std::get<0>(std::declval<Deep&>())), Row&));
static_assert(__is_same(decltype(Deep(std::declval<const Deep&>())), Deep));
static_assert(__is_same(decltype(Deep(std::declval<Deep&&>())), Deep));
static_assert(__is_same(decltype(Row(std::declval<Leaf>(), 1)), Row));
static_assert(__is_same(decltype(Row(std::declval<const Row&>())), Row));
static_assert(__is_same(decltype(Row(std::declval<Row&&>())), Row));
static_assert(__is_same(decltype(std::tuple_cat(std::declval<Row&>(),
    std::declval<std::tuple<int>>())), std::tuple<Leaf, int, int>));
static_assert(__is_same(decltype(std::apply(std::declval<Read>(),
    std::declval<Row&>())), std::size_t));
static_assert(noexcept(std::apply(std::declval<Read>(), std::declval<Row&>())) ==
    noexcept(std::declval<Read>()(std::declval<const Leaf&>(), 1)));
int main() { return 0; }
