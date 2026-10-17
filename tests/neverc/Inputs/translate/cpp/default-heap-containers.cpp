#include <vector>
#include <string>
#include <unordered_map>
#include <utility>
int main() {
  std::vector<int> values{2,3};
  values.reserve(12); values.push_back(5); values.insert(values.begin()+1, 7);
  if (values.size() != 4 || values[1] != 7 || values[3] != 5) return 1;
  auto copied = values;
  auto moved = std::move(copied);
  values.erase(values.begin()); values.shrink_to_fit();
  if (moved.size() != 4 || values.size() != 3 || values[0] != 7) return 2;
  std::string text("abcdefghijklmnopqrstuvwxyz0123456789");
  text.append("xyz"); text.reserve(96);
  auto other = text; other.append("!");
  if (text.size() != 39 || other.size() != 40 || text[38] != 'z') return 3;
  using Map = std::unordered_map<int,int>;
  Map map{{1,11},{2,22},{3,33}};
  map.reserve(24); map.insert({4,44});
  auto node = map.extract(2);
  node.key() = 5; node.mapped() = 55;
  auto result = map.insert(std::move(node));
  if (!result.inserted || result.node || result.position->second != 55) return 4;
  Map copy = map;
  Map destination = std::move(copy);
  destination.erase(1);
  map.merge(destination);
  if (map.size() != 4 || map.find(5)->second != 55 || map.find(4)->second != 44) return 5;
  Map::insert_return_type results[2];
  results[0].inserted = false; results[1].inserted = false;
  results[0].node = map.extract(3);
  if (results[0].node.empty() || map.size() != 3) return 6;
  return 0;
}
