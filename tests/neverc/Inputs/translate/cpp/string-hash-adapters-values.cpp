#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

template<class V> bool adapters(V& value) {
  using H = std::hash<V>;
  H hash;
  const V& constant = value;
  auto wrapper = std::ref(hash);
  auto quiet = std::cref(hash);
  const auto expected = hash(value);
  if (std::invoke(hash, value) != expected ||
      std::invoke(H{}, constant) != expected ||
      std::apply(hash, std::tie(value)) != expected ||
      std::apply(H{}, std::tie(constant)) != expected ||
      wrapper(value) != expected || quiet(constant) != expected ||
      std::invoke(wrapper, value) != expected ||
      std::invoke(quiet, constant) != expected ||
      std::apply(wrapper, std::tie(value)) != expected ||
      std::apply(quiet, std::tie(constant)) != expected) return false;
  if (std::invoke(hash, std::move(value)) != expected ||
      wrapper(std::move(value)) != expected || hash(value) != expected) return false;
  int effects = 0;
  if (std::invoke((++effects, hash), (++effects, value)) != expected || effects != 2) return false;
  effects = 0;
  if (std::apply((++effects, wrapper), std::tie((++effects, value))) != expected || effects != 2) return false;
  return true;
}
int main() {
  char bytes[259];
  for (unsigned i = 0; i < sizeof(bytes); ++i)
    bytes[i] = static_cast<char>((i * 197u + 239u) & 255u);
  const unsigned lengths[] = {0, 1, 3, 7, 8, 16, 17, 32, 33, 64, 65, 129, 257};
  for (unsigned offset = 0; offset < 2; ++offset) {
    for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
      std::string_view view(bytes + offset, lengths[i]);
      std::string text(bytes + offset, lengths[i]);
      if (!adapters(view) || !adapters(text)) return 1;
      if (std::hash<std::string>{}(text) != std::hash<std::string_view>{}(view)) return 2;
      if (!text.empty()) {
        text[0] = static_cast<char>(text[0] ^ 0x35);
        if (!adapters(text) || std::hash<std::string>{}(text) !=
            std::hash<std::string_view>{}(std::string_view(text.data(), text.size()))) return 3;
      }
    }
  }
  std::string_view empty;
  return adapters(empty) ? 0 : 4;
}
