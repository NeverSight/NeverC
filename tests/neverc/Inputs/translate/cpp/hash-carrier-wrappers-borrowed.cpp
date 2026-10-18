#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
struct Deleted { ~Deleted() = delete; };
std::size_t owner(std::reference_wrapper<const std::unique_ptr<Deleted>> value) {
  std::hash<std::unique_ptr<Deleted>> hash;
  auto callable = std::ref(hash);
  auto args = std::tuple<std::reference_wrapper<const std::unique_ptr<Deleted>>>(value);
  return hash(value) ^ std::invoke(hash, value) ^ std::apply(hash, args) ^
      callable(value) ^ std::invoke(callable, value) ^ std::apply(callable, args);
}
std::size_t text(std::reference_wrapper<const std::string> value) {
  std::hash<std::string> hash;
  auto callable = std::ref(hash);
  auto args = std::tuple<std::reference_wrapper<const std::string>>(value);
  return hash(value) ^ std::invoke(hash, value) ^ std::apply(hash, args) ^
      callable(value) ^ std::invoke(callable, value) ^ std::apply(callable, args);
}
std::size_t view(std::reference_wrapper<const std::string_view> value) {
  std::hash<std::string_view> hash;
  auto callable = std::ref(hash);
  auto args = std::tuple<std::reference_wrapper<const std::string_view>>(value);
  return hash(value) ^ std::invoke(hash, value) ^ std::apply(hash, args) ^
      callable(value) ^ std::invoke(callable, value) ^ std::apply(callable, args);
}
