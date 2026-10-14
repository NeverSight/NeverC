#include <algorithm>
#include <functional>

using W = std::reference_wrapper<int>;

template <class Count>
bool check_boundary(W *first, const W *const_first, const W &value,
                    Count count, bool nonpositive) {
  W *expected = nonpositive ? first : first + 6;
  const W *const_expected = nonpositive ? const_first : const_first + 6;
  return std::search_n(first, first + 6, count, value) == expected &&
         std::search_n(const_first, const_first + 6, count, value) == const_expected &&
         std::search_n(first, first, count, value) == first &&
         std::search_n(const_first, const_first, count, value) == const_first;
}

template <class Count>
bool check_comparisons(W *first, const W *const_first, const W &value,
                       Count count, bool nonpositive) {
  W *expected = nonpositive ? first : first + 6;
  const W *const_expected = nonpositive ? const_first : const_first + 6;
  const std::equal_to<> equal;
  std::not_equal_to<> unequal;
  const std::less<> less;
  std::greater<> greater;
  const std::less_equal<> less_equal;
  std::greater_equal<> greater_equal;
  if (std::search_n(first, first + 6, count, value, equal) != expected ||
      std::search_n(first, first + 6, count, value, unequal) != expected ||
      std::search_n(first, first + 6, count, value, less) != expected ||
      std::search_n(first, first + 6, count, value, greater) != expected ||
      std::search_n(first, first + 6, count, value, less_equal) != expected ||
      std::search_n(first, first + 6, count, value, greater_equal) != expected)
    return false;
  if (std::search_n(const_first, const_first + 6, count, value, equal) != const_expected ||
      std::search_n(const_first, const_first + 6, count, value, unequal) != const_expected ||
      std::search_n(const_first, const_first + 6, count, value, less) != const_expected ||
      std::search_n(const_first, const_first + 6, count, value, greater) != const_expected ||
      std::search_n(const_first, const_first + 6, count, value, less_equal) != const_expected ||
      std::search_n(const_first, const_first + 6, count, value, greater_equal) != const_expected)
    return false;
  return std::search_n(first, first, count, value, equal) == first &&
         std::search_n(const_first, const_first, count, value, equal) == const_first;
}

int evaluations, cleanups;
struct PointerArgument {
  W *value;
  ~PointerArgument() { ++cleanups; }
  W *get() { ++evaluations; return value; }
};
struct CountArgument {
  long long value;
  ~CountArgument() { ++cleanups; }
  long long get() { ++evaluations; return value; }
};
struct ValueArgument {
  const W *value;
  ~ValueArgument() { ++cleanups; }
  const W &get() { ++evaluations; return *value; }
};

int main() {
  int x = 1, y = 2, z = 3;
  W values[] = {x, y, y, z, y, y};
  const W const_values[] = {x, y, y, z, y, y};
  const W value = y;
  if (!check_boundary(values, const_values, value, -1, true) ||
      !check_boundary(values, const_values, value, -2147483647 - 1, true) ||
      !check_boundary(values, const_values, value, -9223372036854775807LL - 1LL, true) ||
      !check_boundary(values, const_values, value, static_cast<signed char>(-1), true) ||
      !check_boundary(values, const_values, value, static_cast<short>(-32767 - 1), true) ||
      !check_boundary(values, const_values, value, false, true) ||
      !check_boundary(values, const_values, value, 0U, true) ||
      !check_boundary(values, const_values, value, 0ULL, true) ||
      !check_boundary(values, const_values, value, static_cast<unsigned char>(255), false) ||
      !check_boundary(values, const_values, value, static_cast<unsigned short>(65535), false) ||
      !check_boundary(values, const_values, value, 4294967295U, false) ||
      !check_boundary(values, const_values, value, 9223372036854775808ULL, false) ||
      !check_boundary(values, const_values, value, 18446744073709551615ULL, false))
    return 1;
  if (!check_comparisons(values, const_values, value,
                         -9223372036854775807LL - 1LL, true) ||
      !check_comparisons(values, const_values, value, 4294967295U, false) ||
      !check_comparisons(values, const_values, value,
                         18446744073709551615ULL, false))
    return 4;
  const std::equal_to<> equal;
  auto positive = std::search_n(values, values + 6, static_cast<unsigned short>(2), value, equal);
  static_assert(__is_same(decltype(positive), W *));
  auto const_positive = std::search_n(const_values, const_values + 6, true, value);
  static_assert(__is_same(decltype(const_positive), const W *));
  if (positive != values + 1 || const_positive != const_values + 1 ||
      std::search_n(values, values + 6, 2ULL, value) != values + 1)
    return 2;
  auto negative = std::search_n(PointerArgument{values}.get(),
                               PointerArgument{values + 6}.get(),
                               CountArgument{-9223372036854775807LL - 1LL}.get(),
                               ValueArgument{&value}.get(), equal);
  static_assert(__is_same(decltype(negative), W *));
  if (negative != values || evaluations != 4 || cleanups != 4)
    return 3;
  static_assert(__is_same(decltype(std::search_n(PointerArgument{values}.get(),
                                                PointerArgument{values + 6}.get(),
                                                CountArgument{-1}.get(),
                                                ValueArgument{&value}.get(), equal)), W *));
  return evaluations != 4 || cleanups != 4 || x != 1 || y != 2 || z != 3 ||
         &values[0].get() != &x || &values[4].get() != &y ||
         &const_values[1].get() != &y || &value.get() != &y;
}
