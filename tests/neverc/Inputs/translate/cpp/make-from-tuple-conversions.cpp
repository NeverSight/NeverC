#include <tuple>
#include <utility>
int conversions = 0, copies = 0, moves = 0;
struct Convert {
  int value;
  template<class T> operator T() const noexcept {
    ++conversions;
    return T(value);
  }
};
struct Value {
  int value;
  explicit Value(int n) noexcept : value(n) {}
  Value(const Value& other) noexcept : value(other.value) { ++copies; }
  Value(Value&& other) noexcept : value(other.value) {
    other.value = -1;
    ++moves;
  }
};
int main() {
  std::tuple<Convert> first{Convert{53}};
  if (std::make_from_tuple<int>(first) != 53 || conversions != 1)
    return 1;
  Value original(59);
  Value copied = std::make_from_tuple<Value>(std::tie(original));
  Value moved = std::make_from_tuple<Value>(std::forward_as_tuple(std::move(original)));
  return copies == 1 && moves == 1 && copied.value == 59 &&
                 moved.value == 59 && original.value == -1
             ? 0 : 2;
}
