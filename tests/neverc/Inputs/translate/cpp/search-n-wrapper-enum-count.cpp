#include <algorithm>
#include <functional>

using W = std::reference_wrapper<int>;
enum Small : unsigned char { small_zero = 0, small_two = 2, small_large = 255 };
enum Signed : long long {
  signed_negative = -9223372036854775807LL - 1LL,
  signed_two = 2,
  signed_large = 9223372036854775807LL
};
enum Large : unsigned long long {
  large_zero = 0,
  large_two = 2,
  large_maximum = 18446744073709551615ULL
};
enum Ordinary { ordinary_two = 2 };
enum Boolean : bool { boolean_zero = false, boolean_one = true };

template <class Count>
bool defaults(W *first, const W *const_first, const W &value,
              Count nonpositive, Count oversized, Count two) {
  return std::search_n(first, first + 6, nonpositive, value) == first &&
         std::search_n(const_first, const_first + 6, nonpositive, value) == const_first &&
         std::search_n(first, first + 6, oversized, value) == first + 6 &&
         std::search_n(const_first, const_first + 6, oversized, value) == const_first + 6 &&
         std::search_n(first, first, oversized, value) == first &&
         std::search_n(first, first + 6, two, value) == first + 1;
}

template <class Count>
bool comparisons(W *first, const W *const_first, const W &value,
                 Count nonpositive, Count oversized) {
  const std::equal_to<> equal;
  std::not_equal_to<> unequal;
  const std::less<> less;
  std::greater<> greater;
  const std::less_equal<> less_equal;
  std::greater_equal<> greater_equal;
  return std::search_n(first, first + 6, nonpositive, value, equal) == first &&
         std::search_n(first, first + 6, nonpositive, value, unequal) == first &&
         std::search_n(first, first + 6, nonpositive, value, less) == first &&
         std::search_n(first, first + 6, nonpositive, value, greater) == first &&
         std::search_n(first, first + 6, nonpositive, value, less_equal) == first &&
         std::search_n(first, first + 6, nonpositive, value, greater_equal) == first &&
         std::search_n(const_first, const_first + 6, oversized, value, equal) == const_first + 6 &&
         std::search_n(const_first, const_first + 6, oversized, value, unequal) == const_first + 6 &&
         std::search_n(const_first, const_first + 6, oversized, value, less) == const_first + 6 &&
         std::search_n(const_first, const_first + 6, oversized, value, greater) == const_first + 6 &&
         std::search_n(const_first, const_first + 6, oversized, value, less_equal) == const_first + 6 &&
         std::search_n(const_first, const_first + 6, oversized, value, greater_equal) == const_first + 6;
}

int evaluations, cleanups;
struct CountArgument {
  Signed value;
  ~CountArgument() { ++cleanups; }
  Signed get() { ++evaluations; return value; }
};

int main() {
  int x = 1, y = 2, z = 3;
  W values[] = {x, y, y, z, y, y};
  const W const_values[] = {x, y, y, z, y, y};
  const W value = y;
  if (!defaults(values, const_values, value, small_zero, small_large, small_two) ||
      !defaults(values, const_values, value, signed_negative, signed_large, signed_two) ||
      !defaults(values, const_values, value, large_zero, large_maximum, large_two) ||
      !comparisons(values, const_values, value, signed_negative, signed_large) ||
      !comparisons(values, const_values, value, large_zero, large_maximum))
    return 1;
  const std::equal_to<> equal;
  auto positive = std::search_n(values, values + 6, ordinary_two, value, equal);
  auto const_positive = std::search_n(const_values, const_values + 6, small_two, value, equal);
  static_assert(__is_same(decltype(positive), W *));
  static_assert(__is_same(decltype(const_positive), const W *));
  if (positive != values + 1 || const_positive != const_values + 1 ||
      std::search_n(values, values + 6, boolean_zero, value) != values ||
      std::search_n(values, values + 6, boolean_one, value) != values + 1)
    return 2;
  auto negative = std::search_n(values, values + 6,
                               CountArgument{signed_negative}.get(), value, equal);
  static_assert(__is_same(decltype(negative), W *));
  static_assert(__is_same(decltype(std::search_n(values, values + 6,
                                               CountArgument{signed_negative}.get(),
                                               value, equal)), W *));
  return negative != values || evaluations != 1 || cleanups != 1 ||
         x != 1 || y != 2 || z != 3 || &values[0].get() != &x ||
         &const_values[1].get() != &y || &value.get() != &y;
}
