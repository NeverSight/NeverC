#include <memory>
#include <string>
#include <string_view>
#include <tuple>

using Leaf = std::tuple<std::string, std::string_view>;
using Row = std::tuple<Leaf, int>;
struct NoDestroy { int value; NoDestroy() = delete; ~NoDestroy() = delete; };
using P = std::unique_ptr<NoDestroy>;
using Owners = std::tuple<std::tuple<P>, int>;
extern "C" std::size_t nested_string_size(const Row& row) {
  return std::get<0>(std::get<0>(row)).size() +
         std::get<1>(std::get<0>(row)).size() + std::get<1>(row);
}
extern "C" bool nested_owner_empty(const Owners& row) {
  return !std::get<0>(std::get<0>(row));
}
