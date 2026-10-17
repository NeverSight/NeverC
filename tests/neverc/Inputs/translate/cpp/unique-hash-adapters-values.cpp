#include <functional>
#include <memory>
#include <tuple>
#include <utility>

int deleted;
struct D { void operator()(int*) const noexcept { ++deleted; } };
struct CD { void operator()(const int*) const noexcept { ++deleted; } };
template<class P> bool adapters(P& owner) {
  using H = std::hash<P>;
  H hash;
  auto wrapper = std::ref(hash);
  auto constant = std::cref(hash);
  const P& constant_owner = owner;
  const auto expected = std::hash<typename P::pointer>{}(owner.get());
  if (std::invoke(hash, owner) != expected ||
      std::invoke(H{}, owner) != expected ||
      std::apply(hash, std::tie(owner)) != expected ||
      wrapper(owner) != expected || constant(owner) != expected ||
      std::invoke(wrapper, owner) != expected ||
      std::invoke(constant, owner) != expected ||
      std::apply(wrapper, std::tie(owner)) != expected ||
      std::invoke(hash, constant_owner) != expected ||
      std::apply(constant, std::tie(constant_owner)) != expected) return false;
  int effects = 0;
  if (std::invoke((++effects, hash), (++effects, owner)) != expected || effects != 2) return false;
  effects = 0;
  if (std::apply((++effects, hash), std::tie((++effects, owner))) != expected || effects != 2) return false;
  auto moved = std::move(owner);
  if (owner.get() != nullptr || moved.get() == nullptr) return false;
  if (std::invoke(hash, owner) != std::hash<typename P::pointer>{}(nullptr) ||
      wrapper(std::move(moved)) != expected || moved.get() == nullptr) return false;
  owner = std::move(moved);
  return owner.get() != nullptr;
}
int main() {
  {
    auto scalar = std::make_unique<int>(7);
    auto array = std::make_unique<int[]>(2);
    array[1] = 9;
    if (!adapters(scalar) || !adapters(array) || *scalar != 7 || array[1] != 9) return 1;
    int value = 5;
    int values[2] = {3, 4};
    std::unique_ptr<int,D> custom(&value);
    std::unique_ptr<int[],D> custom_array(values);
    if (!adapters(custom) || !adapters(custom_array) || deleted != 0) return 2;
    const int constant_value = 6;
    const int constant_values[2] = {8, 9};
    std::unique_ptr<const int,CD> constant_custom(&constant_value);
    std::unique_ptr<const int[],CD> constant_array(constant_values);
    if (!adapters(constant_custom) || !adapters(constant_array) || deleted != 0) return 3;
  }
  return deleted == 4 ? 0 : 4;
}
