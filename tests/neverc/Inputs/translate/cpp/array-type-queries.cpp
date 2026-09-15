using Size = decltype(sizeof(0));
using Array = int[2][3];
enum Dimension { first = 0, second = 1 };

// The type stays fixed while only the dimension needs substitution.
template<int I> constexpr Size fixed_extent() {
  return __array_extent(int[2][3], I);
}
template<class T, unsigned I> constexpr Size extent() {
  return __array_extent(T, I);
}
template<class T> constexpr Size rank() { return __array_rank(T); }
template<class T, unsigned I = 0> inline constexpr Size extent_value = __array_extent(T, I);
template<class T, Size N = __array_extent(T, 0)> struct Bound { int value = int(N); };
template<class T, Size N = __array_extent(T, 1)> using Row = int[N];
template<int I> constexpr int index() { return I; }
template<int I> Size nested() { return __array_extent(int[2][3], index<I>()); }
template<unsigned... I> constexpr Size packed_extent() {
  return (__array_extent(int[2][3], index<I>()) + ... + 0);
}
static_assert(packed_extent<>() == 0 && packed_extent<0, 1, 2>() == 5);
static_assert(noexcept(__array_extent(Array, index<1>())));
static_assert(sizeof(int[__array_extent(Array, index<1>())]) == 3 * sizeof(int));
int effects;
int effect() { return ++effects; }
template<class T> struct SourceAssignment {
  T value;
  constexpr SourceAssignment &operator=(const SourceAssignment&) = default;
};
constexpr int assignedIndex() {
  SourceAssignment<int> first{0}, second{1};
  first = second;
  return first.value;
}
struct SourceLeaf { int value; constexpr SourceLeaf() : value(1) {} };
template<class T> struct SourceConstruction { T field; constexpr SourceConstruction() = default; };
constexpr int constructedIndex() { SourceConstruction<SourceLeaf> owner; return owner.field.value; }
using GeneratedArray = int[assignedIndex() + 1][constructedIndex() + 2];
extern "C" Size query_assignment_dimension() { return __array_extent(Array, assignedIndex()); }
extern "C" Size query_construction_dimension() { return __array_extent(Array, constructedIndex()); }
extern "C" Size query_source_rank() { return __array_rank(GeneratedArray); }

static_assert(fixed_extent<0>() == 2 && fixed_extent<1>() == 3 && fixed_extent<2>() == 0);
static_assert(rank<Array>() == 2 && extent<Array, 1>() == 3);
static_assert(__is_same(decltype(__array_rank(int)), Size));
static_assert(__is_same(decltype(__array_extent(Array, 0)), Size));
static_assert(__array_extent(int[2][3], __is_integral(int)) == 3);

int main() {
  if (__array_rank(int) != 0 || __array_rank(int[4]) != 1 ||
      __array_rank(const Array) != 2) return 1;
  if (__array_extent(Array, first) != 2 || __array_extent(Array, second) != 3 ||
      __array_extent(Array, 2) != 0 ||
      __array_extent(Array, 18446744073709551615ULL) != 0) return 2;
  if (__array_rank(int*) || __array_rank(Array&) || __array_rank(void) ||
      __array_rank(int()) || __array_extent(Array*, 0) ||
      __array_extent(Array&, 0) || __array_extent(void, 0)) return 3;
  if (fixed_extent<0>() != 2 || fixed_extent<1>() != 3 || fixed_extent<2>() != 0 ||
      extent<Array, 0>() != 2 || extent<Array, 1>() != 3 ||
      extent<int, 0>() != 0 || rank<Array>() != 2) return 4;
  if (extent_value<Array> != 2 || extent_value<Array, 1> != 3 ||
      extent_value<Array, 7> != 0) return 5;
  Bound<Array> bound;
  Row<Array> row{};
  if (bound.value != 2 || __array_rank(decltype(row)) != 1 ||
      __array_extent(decltype(row), 0) != 3) return 6;
  if (nested<0>() != 2 || nested<1>() != 3 ||
      __array_extent(Array, __array_rank(int[7])) != 3 ||
      __array_extent(Array, __array_extent(int, 0)) != 2) return 7;
  if (__array_rank(decltype(effect())) ||
      __array_extent(Array, (sizeof(++effects), 0)) != 2 || effects) return 8;
  Size value = (++effects, __array_extent(Array, 1));
  if (value != 3 || effects != 1) return 9;
  if (packed_extent<>() != 0 || packed_extent<0, 1, 2>() != 5) return 10;
  if (query_assignment_dimension() != 3 || query_construction_dimension() != 3 ||
      query_source_rank() != 2 || effects != 1) return 11;
  if (assignedIndex() != 1 || constructedIndex() != 1 || effects != 1) return 12;
  SourceAssignment<int> first{2}, second{7};
  first = second;
  first.value = 11;
  if (first.value != 11 || second.value != 7 || &first == &second) return 13;
  SourceConstruction<SourceLeaf> owner;
  if (owner.field.value != 1 || sizeof(GeneratedArray) != sizeof(Array) || effects != 1) return 14;
  return 0;
}
