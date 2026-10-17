#include <memory>
#include <functional>
#include <type_traits>
#include <utility>
#include <cstddef>
struct Undestroyable{Undestroyable()=delete;~Undestroyable()=delete;};
struct D{void operator()(Undestroyable*)const noexcept{}};
using P=std::unique_ptr<Undestroyable,D>;using A=std::unique_ptr<Undestroyable[],D>;
static_assert(std::is_same<decltype(std::hash<P>{}(std::declval<const P&>())),std::size_t>::value);
static_assert(std::is_same<decltype(std::hash<A>{}(std::declval<const A&>())),std::size_t>::value);
static_assert(!noexcept(std::hash<P>{}(std::declval<const P&>())));
static_assert(!noexcept(std::hash<A>{}(std::declval<const A&>())));
static_assert(sizeof(std::hash<P>{}(std::declval<const P&>()))==sizeof(std::size_t));
static_assert(sizeof(std::hash<A>{}(std::declval<const A&>()))==sizeof(std::size_t));
int main(){return 0;}
