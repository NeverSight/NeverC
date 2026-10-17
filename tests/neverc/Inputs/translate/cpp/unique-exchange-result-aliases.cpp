#include <memory>
#include <utility>
static int destroyed;
struct Item {
  int value;
  explicit Item(int x) noexcept : value(x) {}
  ~Item() { destroyed = destroyed * 10 + value; }
};
int main() {
  {
    auto p = std::make_unique<Item>(1);
    auto q = std::make_unique<Item>(2);
    p = std::exchange(p, std::move(q));
    if (destroyed != 2 || q || p->value != 1) return 1;
  }
  if (destroyed != 21) return 2;
  destroyed = 0;
  {
    auto p = std::make_unique<Item>(3);
    auto q = std::make_unique<Item>(4);
    q = std::exchange(p, std::move(q));
    if (destroyed || p->value != 4 || q->value != 3) return 3;
  }
  if (destroyed != 34) return 4;
  destroyed = 0;
  {
    auto p = std::make_unique<Item>(5);
    const auto& old = std::exchange(p, nullptr);
    if (p || destroyed || old->value != 5) return 5;
  }
  return destroyed == 5 ? 0 : 6;
}
