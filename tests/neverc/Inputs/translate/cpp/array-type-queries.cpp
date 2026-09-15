using Size = decltype(sizeof(0));
using Array = int[2][3];
struct Forward;
using ForwardArray = Forward[][3];
template<class T> struct Uninstantiated { typename T::missing value; };
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
using Unknown = int[][3];
using UnknownGenerated = int[][assignedIndex() + 2];
template<class T> using UnknownOf = T[];
template<class T> struct UnknownPartial { static constexpr Size value = 0; };
template<class T> struct UnknownPartial<T[]> { static constexpr Size value = __array_rank(T) + 1; };
template<class... T> constexpr Size unknownRanks() { return (__array_rank(T) + ... + 0); }
template<unsigned... I> constexpr Size unknownExtents() { return (__array_extent(Unknown, I) + ... + 0); }
template<class T> int adjusted(T values) { return values[0] + int(__array_extent(T, 0)); }
extern "C" Size query_unknown_rank() { return __array_rank(Unknown); }
extern "C" Size query_unknown_outer() { return __array_extent(Unknown, 0); }
extern "C" Size query_unknown_inner() { return __array_extent(Unknown, 1); }
extern "C" Size query_unknown_far() { return __array_extent(Unknown, 18446744073709551615ULL); }
extern "C" Size query_unknown_pointer() { return __array_rank(Unknown*); }
extern "C" Size query_unknown_source() { return __array_extent(UnknownGenerated, 1); }
extern "C" Size query_forward_rank() { return __array_rank(ForwardArray); }
extern "C" Size query_forward_outer() { return __array_extent(ForwardArray, 0); }
extern "C" Size query_forward_inner() { return __array_extent(ForwardArray, 1); }
extern "C" Size query_forward_bare() { return __array_rank(Forward); }
extern "C" Size query_forward_lazy() { return __array_rank(Uninstantiated<int>); }

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
  if (query_unknown_rank() != 2 || query_unknown_outer() != 0 ||
      query_unknown_inner() != 3 || query_unknown_far() != 0) return 15;
  if (query_unknown_pointer() || __array_rank(Unknown&) || __array_rank(Unknown&&) ||
      __array_extent(Unknown*, 1) || __array_extent(Unknown&, 0)) return 16;
  if (rank<Unknown>() != 2 || extent<Unknown, 0>() != 0 || extent<Unknown, 1>() != 3) return 17;
  if (extent_value<Unknown> != 0 || extent_value<Unknown, 1> != 3 ||
      extent_value<Unknown, 7> != 0) return 18;
  Bound<Unknown> unknown_bound;
  Row<Unknown> unknown_row{};
  if (unknown_bound.value || __array_extent(decltype(unknown_row), 0) != 3) return 19;
  if (unknownRanks<>() != 0 || unknownRanks<int[], Unknown, int>() != 3 ||
      unknownExtents<>() != 0 || unknownExtents<0, 1, 2>() != 3) return 20;
  if (query_unknown_source() != 3 || __array_extent(Unknown, (sizeof(++effects), 0)) != 0 || effects != 1) return 21;
  if (UnknownPartial<Unknown>::value != 2 || UnknownPartial<int[]>::value != 1 ||
      UnknownPartial<int[2]>::value || __array_rank(UnknownOf<int[3]>) != 2) return 22;
  int known[2] = {5, 6};
  if (adjusted<int[]>(known) != 5 || adjusted<int[2]>(known) != 7 || known[0] != 5) return 23;
  if (query_forward_rank() != 2 || query_forward_outer() || query_forward_inner() != 3 ||
      query_forward_bare() || query_forward_lazy()) return 24;
  if (rank<ForwardArray>() != 2 || extent<Forward[2][3], 0>() != 2 ||
      extent<ForwardArray, 1>() != 3 || extent_value<ForwardArray, 1> != 3) return 25;
  Bound<ForwardArray> forward_bound;
  Row<ForwardArray> forward_row{};
  if (forward_bound.value || __array_extent(decltype(forward_row), 0) != 3 ||
      UnknownPartial<ForwardArray>::value != 2 || unknownRanks<Forward, ForwardArray>() != 2) return 26;
  if (__array_extent(ForwardArray, (sizeof(++effects), 1)) != 3 ||
      __array_rank(Forward[(sizeof(++effects), 2)][3]) != 2 ||
      __array_rank(ForwardArray*) || __array_extent(ForwardArray&, 1) || effects != 1) return 27;
  return 0;
}
