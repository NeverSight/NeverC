#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

int deletions;
struct D { void operator()(int*) const noexcept { ++deletions; } };
template<class V> const V& keep(const V& value) { return value; }
bool temporary_wrappers() {
  int value = 11;
  using P = std::unique_ptr<int,D>;
  using A = std::reference_wrapper<const P>;
  std::hash<P> hash;
  auto callable = std::ref(hash);
  const auto expected = std::hash<int*>{}(&value);
  int before = deletions;
  auto result = hash(std::cref(keep(P(&value))));
  if (result != expected || deletions != before + 1) return false;
  result = std::invoke(hash, std::cref(keep(P(&value))));
  if (result != expected || deletions != before + 2) return false;
  result = std::apply(hash, std::tuple<A>(std::cref(keep(P(&value)))));
  if (result != expected || deletions != before + 3) return false;
  result = callable(std::cref(keep(P(&value))));
  if (result != expected || deletions != before + 4) return false;
  result = std::invoke(callable, std::cref(keep(P(&value))));
  if (result != expected || deletions != before + 5) return false;
  result = std::apply(callable, std::tuple<A>(std::cref(keep(P(&value)))));
  if (result != expected || deletions != before + 6) return false;
  std::invoke(callable, std::cref(keep(P(&value))));
  if (deletions != before + 7) return false;
  std::apply(callable, std::tuple<A>(std::cref(keep(P(&value)))));
  if (deletions != before + 8) return false;
  using S = std::string;
  using R = std::reference_wrapper<const S>;
  std::hash<S> string_hash;
  auto wrapped_string_hash = std::cref(string_hash);
  const auto string_expected = string_hash(S(193, 'q'));
  if (string_hash(std::cref(keep(S(193, 'q')))) != string_expected ||
      std::invoke(string_hash, std::cref(keep(S(193, 'q')))) != string_expected ||
      std::apply(string_hash, std::tuple<R>(std::cref(keep(S(193, 'q'))))) != string_expected ||
      wrapped_string_hash(std::cref(keep(S(193, 'q')))) != string_expected ||
      std::invoke(wrapped_string_hash, std::cref(keep(S(193, 'q')))) != string_expected ||
      std::apply(wrapped_string_hash, std::tuple<R>(std::cref(keep(S(193, 'q'))))) != string_expected) return false;
  std::invoke(wrapped_string_hash, std::cref(keep(S(193, 'q'))));
  using V = std::string_view;
  using W = std::reference_wrapper<const V>;
  std::hash<V> view_hash;
  auto wrapped_view_hash = std::ref(view_hash);
  const auto view_expected = view_hash(V("abc", 3));
  return view_hash(std::cref(keep(V("abc", 3)))) == view_expected &&
      std::invoke(view_hash, std::cref(keep(V("abc", 3)))) == view_expected &&
      std::apply(view_hash, std::tuple<W>(std::cref(keep(V("abc", 3))))) == view_expected &&
      wrapped_view_hash(std::cref(keep(V("abc", 3)))) == view_expected &&
      std::invoke(wrapped_view_hash, std::cref(keep(V("abc", 3)))) == view_expected &&
      std::apply(wrapped_view_hash, std::tuple<W>(std::cref(keep(V("abc", 3))))) == view_expected;
}
template<class V> bool adapters(V& value) {
  using H = std::hash<V>;
  H hash;
  auto callable = std::ref(hash);
  auto quiet = std::cref(hash);
  auto argument = std::ref(value);
  auto constant = std::cref(value);
  auto arguments = std::tuple<std::reference_wrapper<V>>(argument);
  auto constants = std::tuple<std::reference_wrapper<const V>>(constant);
  const auto expected = hash(value);
  if (hash(argument) != expected || hash(constant) != expected ||
      std::invoke(hash, argument) != expected ||
      std::invoke(H{}, constant) != expected ||
      std::apply(hash, arguments) != expected ||
      std::apply(H{}, constants) != expected ||
      std::apply(hash, std::tie(argument)) != expected ||
      std::apply(hash, std::tie(constant)) != expected ||
      callable(argument) != expected || quiet(constant) != expected ||
      std::invoke(callable, argument) != expected ||
      std::invoke(quiet, constant) != expected ||
      std::apply(callable, arguments) != expected ||
      std::apply(quiet, constants) != expected ||
      std::apply(callable, std::tie(argument)) != expected ||
      std::apply(quiet, std::tie(constant)) != expected) return false;
  if (std::invoke(hash, std::move(argument)) != expected ||
      std::apply(callable, std::move(arguments)) != expected ||
      argument.get().data() == nullptr) return false;
  int effects = 0;
  if (std::invoke((++effects, hash), (++effects, argument)) != expected || effects != 2) return false;
  effects = 0;
  if (std::apply((++effects, callable), (++effects, constants)) != expected || effects != 2) return false;
  return true;
}
template<class P> bool owners(P& owner) {
  using H = std::hash<P>;
  H hash;
  auto callable = std::ref(hash);
  auto argument = std::ref(owner);
  auto constant = std::cref(owner);
  auto arguments = std::tuple<std::reference_wrapper<P>>(argument);
  auto constants = std::tuple<std::reference_wrapper<const P>>(constant);
  auto* pointer = owner.get();
  const auto expected = hash(owner);
  if (hash(argument) != expected || hash(constant) != expected ||
      std::invoke(hash, argument) != expected ||
      std::apply(hash, arguments) != expected || callable(argument) != expected ||
      std::apply(hash, std::tie(argument)) != expected ||
      std::apply(hash, std::tie(constant)) != expected ||
      std::invoke(callable, constant) != expected ||
      std::apply(callable, constants) != expected ||
      std::apply(callable, std::tie(argument)) != expected ||
      std::apply(callable, std::tie(constant)) != expected ||
      std::invoke(H{}, std::move(argument)) != expected ||
      std::apply(callable, std::move(arguments)) != expected) return false;
  int effects = 0;
  if (std::invoke((++effects, callable), (++effects, constant)) != expected || effects != 2) return false;
  return owner.get() == pointer;
}
int main() {
  {
    auto scalar = std::make_unique<int>(7);
    auto array = std::make_unique<int[]>(2);
    int value = 5;
    int values[2] = {3, 4};
    std::unique_ptr<int,D> custom(&value);
    std::unique_ptr<int[],D> custom_array(values);
    if (!owners(scalar) || !owners(array) || !owners(custom) || !owners(custom_array) || deletions != 0) return 1;
  }
  if (deletions != 2) return 2;
  const char bytes[] = {'a', '\0', 'b', static_cast<char>(239), 'z'};
  std::string text(193, 'q');
  std::string_view view(bytes, 5);
  if (!adapters(text) || !adapters(view)) return 3;
  text[0] = 'x';
  if (!adapters(text)) return 4;
  return temporary_wrappers() ? 0 : 5;
}
