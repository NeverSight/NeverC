#include <functional>
#include <memory>
#include <tuple>
#include <utility>
#include <cstddef>
struct Undestroyable { Undestroyable()=delete; ~Undestroyable()=delete; };
using P = std::unique_ptr<Undestroyable>;
using A = std::unique_ptr<Undestroyable[]>;
using H = std::hash<P>;
using AH = std::hash<A>;
using W = std::reference_wrapper<H>;
using T = std::tuple<const P&>;
using AW = std::reference_wrapper<AH>;
using AT = std::tuple<const A&>;
static_assert(__is_same(decltype(std::invoke(std::declval<const H&>(), std::declval<const P&>())), std::size_t));
static_assert(__is_same(decltype(std::apply(std::declval<const H&>(), std::declval<T&>())), std::size_t));
static_assert(__is_same(decltype(std::declval<const W&>()(std::declval<const P&>())), std::size_t));
static_assert(__is_same(decltype(std::invoke(std::declval<const AH&>(), std::declval<const A&>())), std::size_t));
static_assert(__is_same(decltype(std::invoke(std::declval<const W&>(), std::declval<const P&>())), std::size_t));
static_assert(__is_same(decltype(std::apply(std::declval<const W&>(), std::declval<T&>())), std::size_t));
static_assert(__is_same(decltype(std::apply(std::declval<const AH&>(), std::declval<AT&>())), std::size_t));
static_assert(__is_same(decltype(std::declval<const AW&>()(std::declval<const A&>())), std::size_t));
static_assert(__is_same(decltype(std::invoke(std::declval<const AW&>(), std::declval<const A&>())), std::size_t));
static_assert(__is_same(decltype(std::apply(std::declval<const AW&>(), std::declval<AT&>())), std::size_t));
static_assert(!noexcept(std::invoke(std::declval<const H&>(), std::declval<const P&>())));
static_assert(!noexcept(std::declval<const W&>()(std::declval<const P&>())));
static_assert(!noexcept(std::apply(std::declval<const H&>(), std::declval<T&>())));
static_assert(!noexcept(std::invoke(std::declval<const W&>(), std::declval<const P&>())));
static_assert(!noexcept(std::apply(std::declval<const W&>(), std::declval<T&>())));
static_assert(sizeof(std::apply(std::declval<const H&>(), std::declval<T&>())) == sizeof(std::size_t));
int main() { return 0; }
