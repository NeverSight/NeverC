#include <string>
#include <string_view>
#include <functional>
int calls = 0;
int receivers = 0;
std::hash<std::string_view> make_hash() { ++receivers; return {}; }
const std::string& source(const std::string& value) { ++calls; return value; }
std::string_view view_source(std::string_view value) { ++calls; return value; }
int main() {
  const char bytes[] = {'a', '\0', static_cast<char>(239), 'b', 'c'};
  std::string text(bytes, 5);
  std::hash<std::string> hash;
  std::hash<std::string_view> view_hash;
  auto first = hash(source(text));
  if (calls != 1 || first != view_hash(std::string_view(bytes, 5))) return 1;
  auto second = view_hash(view_source(std::string_view(text.data(), text.size())));
  if (calls != 2 || second != first) return 2;
  if (make_hash()(view_source(std::string_view(bytes, 5))) != first) return 10;
  if (receivers != 1 || calls != 3) return 11;
  auto copied = view_hash;
  copied = view_hash;
  if (copied(std::string_view(bytes, 5)) != first) return 12;
  text[3] = 'X';
  if (hash(text) != view_hash(std::string_view(text.data(), text.size()))) return 3;
  if (hash(text) == first) return 4;
  std::string empty;
  if (hash(empty) != view_hash(std::string_view{})) return 5;
  std::string long_text(193, 'q');
  for (unsigned i = 0; i < long_text.size(); ++i) long_text[i] = static_cast<char>((i * 83u + 17u) & 255u);
  if (hash(long_text) != view_hash(std::string_view(long_text.data(), long_text.size()))) return 6;
  std::string moved(static_cast<std::string&&>(long_text));
  if (hash(moved) != view_hash(std::string_view(moved.data(), moved.size()))) return 7;
  if (hash(std::string(bytes, 5)) != first) return 8;
  const std::hash<std::string> const_hash = hash;
  if (const_hash(moved) != hash(moved)) return 9;
  return 0;
}
