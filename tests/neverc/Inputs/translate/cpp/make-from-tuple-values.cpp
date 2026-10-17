#include <tuple>
#include <array>
#include <utility>
int calls = 0, conversions = 0;
struct Converted {
  int value;
  explicit Converted(int n) noexcept : value(n) {}
  operator int() const noexcept { ++conversions; return value; }
};
struct Pair {
  int a,b;
  Pair(int x,int y) noexcept : a(x),b(y) { ++calls; }
};
struct Ref {
  int& value;
  explicit Ref(int& n) noexcept : value(n) {}
};
struct Observe {
  int* shared;
  operator int() const noexcept { return ++*shared; }
};
int seeds = 0;
int next() noexcept { ++seeds; return 43; }
struct WithDefault {
  int value;
  explicit WithDefault(int n, int m = next()) noexcept : value(n + m) {}
};
int inputs = 0;
std::tuple<int,int> input() { ++inputs; return {3,5}; }
int main() {
  auto first = std::make_from_tuple<Pair>(input());
  if (inputs != 1 || calls != 1 || first.a != 3 || first.b != 5) return 1;
  std::pair<int,int> pair{7,11};
  auto second = std::make_from_tuple<Pair>(pair);
  const std::array<int,2> array{{13,17}};
  auto third = std::make_from_tuple<Pair>(array);
  if (calls != 3 || second.a != 7 || second.b != 11 || third.a != 13 || third.b != 17) return 2;
  std::tuple<Converted,Converted> converted{Converted(19),Converted(23)};
  auto fourth = std::make_from_tuple<Pair>(converted);
  if (conversions != 2 || calls != 4 || fourth.a != 19 || fourth.b != 23) return 3;
  int n = 29;
  auto references = std::tie(n);
  auto reference = std::make_from_tuple<Ref>(references);
  reference.value = 31;
  int& alias = std::make_from_tuple<int&>(references);
  alias = 37;
  if (n != 37) return 4;
  if (std::make_from_tuple<int>(std::tuple<>{}) != 0) return 5;
  if (std::make_from_tuple<int>(std::tuple<short>{41}) != 41) return 6;
  int shared = 0;
  std::tuple<Observe,Observe> observers{Observe{&shared},Observe{&shared}};
  auto ordered = std::make_from_tuple<Pair>(observers);
  if (shared != 2 || calls != 5 || ordered.a + ordered.b != 3 ||
      ordered.a * ordered.b != 2) return 7;
  auto seeded = std::make_from_tuple<WithDefault>(std::tuple<int>{47});
  if (seeded.value != 90 || seeds != 1) return 8;
  return 0;
}
