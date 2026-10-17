#include <functional>
#include <tuple>
#include <utility>
#include <type_traits>
#include <cstddef>

int callback_calls;
int callback(int value) noexcept { ++callback_calls; return value; }
template<class P> bool family(P pointer) {
  std::hash<P> hash;
  const auto constant = hash;
  auto wrapper = std::ref(hash);
  auto const_wrapper = std::cref(hash);
  const auto expected = hash(pointer);
  if (constant(pointer) != expected || std::invoke(hash, pointer) != expected ||
      std::apply(hash, std::make_tuple(pointer)) != expected ||
      wrapper(pointer) != expected || const_wrapper(pointer) != expected ||
      std::invoke(wrapper, pointer) != expected ||
      std::invoke(const_wrapper, pointer) != expected) return false;
  if (hash(nullptr) != std::invoke(hash, nullptr) ||
      hash(nullptr) != std::apply(hash, std::make_tuple(static_cast<P>(nullptr)))) return false;
  static_assert(__is_same(decltype(hash(pointer)), std::size_t));
  static_assert(noexcept(hash(pointer)));
  static_assert(noexcept(std::invoke(wrapper, pointer)));
  return true;
}
int main() {
  int value = 7;
  bool flag = true;
  double fraction = 0.5;
  using Callback = int (*)(int) noexcept;
  if (!family(&value) || !family(static_cast<const int*>(&value)) ||
      !family(static_cast<void*>(&value)) || !family(static_cast<const void*>(&value)) ||
      !family(&flag) || !family(&fraction) || !family(static_cast<Callback>(&callback))) return 1;
  std::hash<int*> hash;
  int trace = 0;
  const auto expected = hash(&value);
  const auto observed = (trace = trace * 10 + 1, hash)((trace = trace * 10 + 2, &value));
  if (observed != expected || trace != 12) return 2;
  int effects = 0;
  auto wrapper = std::ref(hash);
  if (std::invoke((++effects, wrapper), (++effects, &value)) != expected || effects != 2) return 3;
  effects = 0;
  if (std::apply((++effects, hash), std::make_tuple((++effects, &value))) != expected || effects != 2) return 4;
  (trace = 0, hash)((trace = trace * 10 + 5, &value));
  if (trace != 5 || value != 7 || callback_calls != 0) return 5;
  return std::hash<int*>{}(nullptr) == std::hash<void*>{}(nullptr) ? 0 : 6;
}
