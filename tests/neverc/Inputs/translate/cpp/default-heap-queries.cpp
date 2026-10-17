#include <memory>
#include <type_traits>
#include <vector>
#include <unordered_map>
struct Owner { std::unique_ptr<int[]> values; };
static_assert(std::is_destructible<Owner>::value);
static_assert(std::is_move_constructible<Owner>::value);
static_assert(std::is_destructible<std::unique_ptr<int[]>>::value);
using Vector = std::vector<int>;
using Map = std::unordered_map<int,int>;
using Result = Map::insert_return_type;
int inspect(const Vector& v, const Map& m, const Result* r) {
  return int(v.size() + m.size()) + (r != nullptr);
}
