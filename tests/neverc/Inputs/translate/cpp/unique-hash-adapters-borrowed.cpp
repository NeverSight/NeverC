#include <functional>
#include <memory>
#include <tuple>
#include <cstddef>
struct Undestroyable { Undestroyable()=delete; ~Undestroyable()=delete; };
using P = std::unique_ptr<Undestroyable>;
using A = std::unique_ptr<Undestroyable[]>;
std::size_t scalar_hash(const P& owner) {
  std::hash<P> hash;
  return std::invoke(hash, owner);
}
std::size_t array_hash(const A& owner) {
  std::hash<A> hash;
  auto wrapper = std::ref(hash);
  return std::apply(wrapper, std::tie(owner));
}
