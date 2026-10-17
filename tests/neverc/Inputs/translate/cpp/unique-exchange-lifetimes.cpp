#include <memory>
#include <utility>
static int destroyed;
struct Item {
  int value;
  explicit Item(int x) noexcept : value(x) {}
  ~Item() { destroyed = destroyed * 10 + value; }
};
using Owner = std::unique_ptr<Item>;
Owner take(Owner& value) { return std::exchange(value, nullptr); }
int main() {
  {
    Owner value = std::make_unique<Item>(1);
    std::exchange(value, std::make_unique<Item>(2));
    if (destroyed != 1 || value->value != 2) return 1;
    Owner result = take(value);
    if (value || destroyed != 1 || result->value != 2) return 2;
    Owner other = std::make_unique<Item>(3);
    result = std::exchange(other, std::move(result));
    if (destroyed != 1 || result->value != 3 || other->value != 2) return 3;
  }
  return destroyed == 123 ? 0 : 4;
}
