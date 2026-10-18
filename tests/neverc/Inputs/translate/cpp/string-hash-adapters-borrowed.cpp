#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
std::size_t hash_view(std::string_view view) {
  std::hash<std::string_view> hash;
  auto wrapper = std::ref(hash);
  return std::invoke(hash, view) ^ std::apply(hash, std::tie(view)) ^ wrapper(view) ^
      std::invoke(wrapper, view) ^ std::apply(wrapper, std::tie(view));
}
std::size_t hash_string(const std::string& text) {
  std::hash<std::string> hash;
  auto wrapper = std::ref(hash);
  return std::invoke(hash, text) ^ std::apply(hash, std::tie(text)) ^ wrapper(text) ^
      std::invoke(wrapper, text) ^ std::apply(wrapper, std::tie(text));
}
