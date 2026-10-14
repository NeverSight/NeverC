#include <algorithm>

int predicate_calls;
bool same(int a, int b) { ++predicate_calls; return a == b; }
struct Item {
  int value;
  bool operator==(const Item &other) const { return value == other.value; }
};
enum SignedCount : long long { negative_count = -9223372036854775807LL - 1LL };
enum UnsignedCount : unsigned long long { oversized_count = 18446744073709551615ULL };
int first_function(int x) { return x + 1; }
int second_function(int x) { return x + 2; }

int main() {
  int values[] = {1, 2, 2, 3, 2, 2};
  const int value = 2;
  if (std::search_n(values, values + 6, -1, value) != values ||
      std::search_n(values, values + 6, negative_count, value) != values ||
      std::search_n(values, values + 6, 18446744073709551615ULL, value) != values + 6 ||
      std::search_n(values, values + 6, oversized_count, value) != values + 6 ||
      std::search_n(values, values + 6, 4294967295U, value) != values + 6 ||
      std::search_n(values, values + 6, 2ULL, value) != values + 1)
    return 1;
  if (std::search_n(values, values + 6, negative_count, value, same) != values ||
      std::search_n(values, values + 6, false, value, same) != values ||
      predicate_calls != 0)
    return 2;
  if (std::search_n(values, values + 6, oversized_count, value, same) != values + 6 ||
      std::search_n(values, values, oversized_count, value, same) != values ||
      predicate_calls != 0 ||
      std::search_n(values, values + 6, static_cast<unsigned char>(2), value, same) != values + 1)
    return 3;
  Item items[] = {{1}, {2}, {2}, {3}};
  const Item needle{2};
  if (std::search_n(items, items + 4, negative_count, needle) != items ||
      std::search_n(items, items + 4, oversized_count, needle) != items + 4 ||
      std::search_n(items, items + 4, 2ULL, needle) != items + 1)
    return 4;
  using Function = int (*)(int);
  Function functions[] = {first_function, second_function, second_function};
  const Function selected = second_function;
  if (std::search_n(functions, functions + 3, negative_count, selected) != functions ||
      std::search_n(functions, functions + 3, oversized_count, selected) != functions + 3 ||
      std::search_n(functions, functions + 3, 2ULL, selected) != functions + 1)
    return 5;
  return values[0] != 1 || values[5] != 2 || items[0].value != 1 ||
         items[3].value != 3 || functions[0] != first_function;
}
