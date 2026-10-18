#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
int calls;
int receivers;
std::string make_string() { ++calls; return std::string(193, 'q'); }
std::string_view make_view(std::string_view value) { ++calls; return value; }
std::hash<std::string> make_hash() { ++receivers; return {}; }
int main() {
  std::hash<std::string> hash;
  std::hash<std::string_view> view_hash;
  auto wrapper = std::ref(hash);
  auto view_wrapper = std::ref(view_hash);
  std::string expected_text(193, 'q');
  const auto expected = hash(expected_text);
  if (std::invoke(make_hash(), make_string()) != expected || calls != 1 || receivers != 1) return 1;
  if (std::apply(hash, std::tuple<std::string>{make_string()}) != expected || calls != 2) return 2;
  if (wrapper(make_string()) != expected || calls != 3) return 3;
  if (std::invoke(wrapper, make_string()) != expected || calls != 4) return 4;
  if (std::apply(wrapper, std::tuple<std::string>{make_string()}) != expected || calls != 5) return 5;
  std::invoke(make_hash(), make_string());
  std::apply(wrapper, std::tuple<std::string>{make_string()});
  if (calls != 7 || receivers != 2) return 6;
  const char bytes[] = {'a', '\0', 'b', static_cast<char>(239), 'z'};
  std::string_view view(bytes, 5);
  const auto view_expected = view_hash(view);
  if (std::invoke(view_hash, make_view(view)) != view_expected || calls != 8) return 7;
  if (std::apply(view_hash, std::tuple<std::string_view>{make_view(view)}) != view_expected || calls != 9) return 8;
  if (view_wrapper(make_view(view)) != view_expected || calls != 10) return 9;
  if (std::invoke(view_wrapper, make_view(view)) != view_expected || calls != 11) return 10;
  if (std::apply(view_wrapper, std::tuple<std::string_view>{make_view(view)}) != view_expected || calls != 12) return 11;
  std::invoke(view_wrapper, make_view(view));
  std::apply(view_wrapper, std::tuple<std::string_view>{make_view(view)});
  if (calls != 14) return 12;
  std::tuple<std::string, std::string> original{std::string(193, 'q'), std::string("short")};
  auto copied = original;
  std::get<0>(original)[0] = 'x';
  std::get<1>(original)[0] = 'y';
  if (hash(std::get<0>(copied)) != expected || std::get<1>(copied) != "short") return 13;
  auto moved = std::move(copied);
  if (!std::get<0>(copied).empty() || !std::get<1>(copied).empty() ||
      hash(std::get<0>(moved)) != expected || std::get<1>(moved) != "short") return 14;
  std::tuple<std::string_view> views{view};
  auto copied_views = views;
  auto moved_views = std::move(copied_views);
  if (std::apply(view_hash, views) != view_expected ||
      std::apply(view_wrapper, moved_views) != view_expected ||
      std::get<0>(copied_views).data() != view.data()) return 15;
  return 0;
}
