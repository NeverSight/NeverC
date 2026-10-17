#include <functional>
#include <memory>
#include <tuple>
#include <utility>

int live, destroyed, deletions, order;
struct Item {
  int tag;
  Item() noexcept : tag(0) { ++live; }
  ~Item() noexcept { --live; ++destroyed; if (tag) order = order * 10 + tag; }
};
struct D { void operator()(Item* p) const noexcept { delete p; ++deletions; } };
using P = std::unique_ptr<Item>;
using C = std::unique_ptr<Item,D>;
int main() {
  std::hash<P> hash;
  auto wrapper = std::ref(hash);
  std::invoke(hash, std::make_unique<Item>());
  if (live != 0 || destroyed != 1) return 1;
  wrapper(std::make_unique<Item>());
  if (live != 0 || destroyed != 2) return 2;
  std::apply(hash, std::make_tuple(std::make_unique<Item>()));
  if (live != 0 || destroyed != 3) return 3;
  std::apply(wrapper, std::make_tuple(std::make_unique<Item>()));
  if (live != 0 || destroyed != 4) return 4;
  std::hash<C> custom;
  std::invoke(custom, C(new Item()));
  if (live != 0 || destroyed != 5 || deletions != 1) return 5;
  std::apply(custom, std::make_tuple(C(new Item())));
  if (live != 0 || destroyed != 6 || deletions != 2) return 6;
  using A = std::unique_ptr<Item[]>;
  std::hash<A> array;
  std::invoke(array, std::make_unique<Item[]>(2));
  if (live != 0 || destroyed != 8) return 7;
  std::apply(array, std::make_tuple(std::make_unique<Item[]>(2)));
  if (live != 0 || destroyed != 10 || deletions != 2) return 8;
  {
    auto owners = std::make_tuple(std::make_unique<Item>(), std::make_unique<Item>());
    std::get<0>(owners)->tag = 1;
    std::get<1>(owners)->tag = 2;
    auto moved = std::move(owners);
    if (std::get<0>(owners).get() != nullptr || std::get<1>(owners).get() != nullptr ||
        !std::get<0>(moved) || !std::get<1>(moved) || live != 2) return 9;
  }
  if (live != 0 || destroyed != 12 || order != 21) return 10;
  {
    auto owners = std::make_tuple(C(new Item()));
    auto moved = std::move(owners);
    if (std::get<0>(owners).get() != nullptr || !std::get<0>(moved) ||
        std::apply(custom, moved) != custom(std::get<0>(moved))) return 11;
  }
  return live == 0 && destroyed == 13 && deletions == 3 ? 0 : 12;
}
