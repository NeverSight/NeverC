#include <tuple>
#include <utility>
int selected = 0, destroyed = 0;
struct Selected {
  int value;
  template<class T> explicit Selected(T n) noexcept : value(int(n)) { ++selected; }
  Selected(const Selected&) = delete;
  Selected(Selected&&) = delete;
  ~Selected() { ++destroyed; }
};
Selected create(std::tuple<short>& source) {
  return std::make_from_tuple<Selected>(source);
}
struct Box {
  Selected item;
  explicit Box(std::tuple<short>& source) : item(std::make_from_tuple<Selected>(source)) {}
};
int main() {
  std::tuple<short> first{43}, second{47};
  {
    Selected returned = create(first);
    Box field(second);
    Selected array[2] = {std::make_from_tuple<Selected>(first),
                         std::make_from_tuple<Selected>(second)};
    if (selected != 4 || destroyed || returned.value != 43 ||
        field.item.value != 47 || array[0].value != 43 || array[1].value != 47)
      return 1;
    std::make_from_tuple<Selected>(first);
    if (selected != 5 || destroyed != 1)
      return 2;
  }
  return selected == 5 && destroyed == 5 ? 0 : 3;
}
