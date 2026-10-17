#include <tuple>
#include <utility>
int permanent = 11;
int& borrowed(int& value) {
  return std::make_from_tuple<int&>(std::tie(value));
}
const int& stable() {
  return std::make_from_tuple<const int&>(std::tie(permanent));
}
struct Self {
  int value;
  const int& reference;
  explicit Self(int n) noexcept : value(n), reference(value) {}
  Self(const Self&) = delete;
  Self(Self&&) = delete;
};
Self create(std::tuple<int>& input) {
  return std::make_from_tuple<Self>(input);
}
int main() {
  // The literal is live until this full expression ends.
  if (std::make_from_tuple<int>(std::forward_as_tuple(17)) != 17)
    return 1;
  std::tuple<int> values{19};
  int& alias = std::make_from_tuple<int&>(values);
  alias = 23;
  if (std::get<0>(values) != 23)
    return 2;
  int original = 29;
  borrowed(original) = 31;
  if (original != 31 || stable() != 11)
    return 3;
  Self direct = std::make_from_tuple<Self>(values);
  Self returned = create(values);
  Self array[2] = {std::make_from_tuple<Self>(values),
                   std::make_from_tuple<Self>(values)};
  direct.value = 37;
  returned.value = 41;
  array[0].value = 43;
  array[1].value = 47;
  return direct.reference == 37 && returned.reference == 41 &&
                 array[0].reference == 43 && array[1].reference == 47
             ? 0 : 4;
}
