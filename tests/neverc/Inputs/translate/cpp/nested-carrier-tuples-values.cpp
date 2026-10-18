#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

using Leaf = std::tuple<std::string, std::string_view>;
using Row = std::tuple<Leaf, int>;
struct Read {
  std::size_t operator()(const Leaf& leaf, int extra) const noexcept {
    return std::get<0>(leaf).size() + std::get<1>(leaf).size() + extra;
  }
};
int effects;
const char bytes[] = {'a', '\0', 'b', static_cast<char>(239), 'z'};
Leaf make_leaf() {
  ++effects;
  return Leaf(std::string(193, 'q'), std::string_view(bytes, 5));
}
int live;
int destroyed;
struct Item {
  int tag;
  Item() noexcept : tag(0) { ++live; }
  ~Item() noexcept { --live; ++destroyed; }
};
using DefaultOwners = std::tuple<std::tuple<std::unique_ptr<Item>>, int>;
struct ReadDefault {
  int operator()(const std::tuple<std::unique_ptr<Item>>& inner,
                 int extra) const noexcept {
    return live == 1 ? std::get<0>(inner)->tag + extra : -1;
  }
};
int deleted_count;
int deleted_order;
struct D {
  void operator()(int* value) const noexcept {
    ++deleted_count;
    deleted_order = deleted_order * 10 + *value;
  }
};
using P = std::unique_ptr<int, D>;
using Owners = std::tuple<std::tuple<P, P>, int>;
bool owners() {
  int first = 1;
  int second = 2;
  {
    Owners row(std::tuple<P, P>(P(&first), P(&second)), 7);
    auto moved = std::move(row);
    if (std::get<0>(std::get<0>(row)) || std::get<1>(std::get<0>(row)) ||
        std::get<0>(std::get<0>(moved)).get() != &first ||
        std::get<1>(std::get<0>(moved)).get() != &second ||
        deleted_count != 0) return false;
    std::tuple<Owners, int> deep(std::move(moved), 10);
    auto combined = std::tuple_cat(std::move(deep), std::tuple<int>(8));
    if (std::get<0>(std::get<0>(moved)) || std::get<1>(std::get<0>(moved)) ||
        std::get<0>(std::get<0>(std::get<0>(deep))) ||
        std::get<1>(std::get<0>(std::get<0>(deep))) ||
        std::get<0>(std::get<0>(std::get<0>(combined))).get() != &first ||
        std::get<1>(std::get<0>(std::get<0>(combined))).get() != &second ||
        std::get<1>(std::get<0>(combined)) != 7 ||
        std::get<1>(combined) != 10 || std::get<2>(combined) != 8 ||
        deleted_count != 0) return false;
  }
  return deleted_count == 2 && deleted_order == 21;
}
int main() {
  Row value(make_leaf(), 7);
  if (effects != 1 || std::get<0>(std::get<0>(value)).size() != 193 ||
      std::get<1>(std::get<0>(value)).size() != 5 ||
      std::get<1>(std::get<0>(value))[1] != '\0' ||
      static_cast<unsigned char>(std::get<1>(std::get<0>(value))[3]) != 239) return 1;
  Row copied(value);
  std::get<0>(std::get<0>(copied))[0] = 'x';
  std::get<1>(std::get<0>(copied)) = std::string_view("z", 1);
  if (std::get<0>(std::get<0>(value))[0] != 'q' ||
      std::get<0>(std::get<0>(copied))[0] != 'x' ||
      std::get<1>(std::get<0>(copied)).size() != 1 ||
      std::get<1>(std::get<0>(value)).size() != 5) return 2;
  Row moved(std::move(copied));
  auto combined = std::tuple_cat(value, std::tuple<int>(8));
  std::get<0>(std::get<0>(combined))[1] = 'y';
  if (std::get<0>(std::get<0>(value))[1] != 'q' ||
      std::get<0>(std::get<0>(moved))[0] != 'x' ||
      std::get<1>(combined) != 7 || std::get<2>(combined) != 8) return 3;
  if (std::apply(Read{}, value) != 205 ||
      std::apply(Read{}, Row(make_leaf(), 9)) != 207 || effects != 2) return 4;
  Row short_row(Leaf(std::string(bytes, 5), std::string_view(bytes, 5)), 1);
  auto short_copy = short_row;
  std::get<0>(std::get<0>(short_copy))[0] = 'x';
  auto short_move = std::move(short_copy);
  if (std::get<0>(std::get<0>(short_row))[0] != 'a' ||
      std::get<0>(std::get<0>(short_move))[0] != 'x' ||
      std::get<0>(std::get<0>(short_move))[1] != '\0' ||
      static_cast<unsigned char>(std::get<0>(std::get<0>(short_move))[3]) != 239) return 5;
  int before = destroyed;
  auto answer = std::apply(ReadDefault{}, DefaultOwners(
      std::tuple<std::unique_ptr<Item>>(std::make_unique<Item>()), 9));
  if (answer != 9 || live != 0 || destroyed != before + 1) return 6;
  std::apply(ReadDefault{}, DefaultOwners(
      std::tuple<std::unique_ptr<Item>>(std::make_unique<Item>()), 9));
  if (live != 0 || destroyed != before + 2) return 7;
  std::tuple<Row, int> deep(value, 11);
  auto deep_copy = deep;
  std::get<0>(std::get<0>(std::get<0>(deep_copy)))[0] = 'd';
  std::get<1>(std::get<0>(std::get<0>(deep_copy))) = std::string_view("z", 1);
  auto deep_move = std::move(deep_copy);
  if (std::get<0>(std::get<0>(std::get<0>(deep)))[0] != 'q' ||
      std::get<1>(std::get<0>(std::get<0>(deep))).size() != 5 ||
      std::get<0>(std::get<0>(std::get<0>(deep_move)))[0] != 'd' ||
      std::get<1>(std::get<0>(std::get<0>(deep_move))).size() != 1 ||
      std::get<1>(deep_move) != 11) return 8;
  return owners() ? 0 : 9;
}
