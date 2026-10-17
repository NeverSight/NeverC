#include <memory>
#include <utility>
using Owner = std::unique_ptr<int>;
static int destinations;
static int replacements;
Owner& destination(Owner& value) { ++destinations; return value; }
Owner replacement() { ++replacements; return std::make_unique<int>(17); }
Owner&& mutate(Owner& value, Owner& other) {
  ++replacements;
  value = std::make_unique<int>(29);
  return std::move(other);
}
decltype(nullptr) null_value() { ++replacements; return nullptr; }
int main() {
  Owner value = std::make_unique<int>(3);
  Owner old = std::exchange(destination(value), replacement());
  if (destinations != 1 || replacements != 1 || *old != 3 || *value != 17)
    return 1;
  Owner self = std::exchange(value, std::move(value));
  if (value || !self || *self != 17) return 2;
  Owner next = std::make_unique<int>(41);
  Owner changed = std::exchange(destination(value), mutate(value, next));
  if (destinations != 2 || replacements != 2 || *changed != 29 ||
      *value != 41 || next) return 3;
  Owner cleared = std::exchange(value, null_value());
  if (replacements != 3 || value || *cleared != 41) return 4;
  const decltype(nullptr) nil = nullptr;
  Owner empty = std::exchange(value, nil);
  if (empty || value) return 5;
  Owner back = std::exchange(value, std::move(old));
  if (back || old || *value != 3) return 6;
  int read = *std::exchange(value, std::make_unique<int>(53));
  if (read != 3 || *value != 53) return 7;
  return 0;
}
