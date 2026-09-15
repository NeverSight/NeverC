enum Plain : int { plain = 1 };
enum class Scoped : unsigned { one = 1 };
struct Empty {};
struct EmptyMiddle : Empty {};
struct EmptyDerived : EmptyMiddle {};
struct PrivateDerived : private Empty {};
struct PlainRecord { int value; };
struct ReferenceRecord { int &value; };
struct ConstructedRecord { int value; ConstructedRecord() : value(3) {} };
struct DestructedRecord { int value; ~DestructedRecord() {} };
struct FinalRecord final {
  int value;
  constexpr FinalRecord(int n = 3) : value(n) {}
};
template<class T> struct FinalBox final { T value; };
struct Bytes { unsigned char a, b; };
struct Padded { char a; int b; };
using Function = int(int);
using CertainFunction = int(int) noexcept;
using Null = decltype(nullptr);

int effects;
int constructed;
int destroyed;
int effect() { return ++effects; }
struct Probe {
  int value;
  Probe() : value(++constructed) {}
  ~Probe() { ++destroyed; }
};

template<class T> inline constexpr bool integral = __is_integral(T);
template<class A, class B> inline constexpr bool same = __is_same(A, B);
template<class... T> constexpr bool all_integral() {
  return (__is_integral(T) && ...);
}
template<class T, bool Value = __is_pointer(T)> struct Category {
  static constexpr bool value = Value;
};
template<class T> int category() {
  if constexpr (__is_integral(T)) return 1;
  else if constexpr (__is_pointer(T)) return 2;
  else return 3;
}
template<bool Value, class T = void> struct Enable {};
template<class T> struct Enable<true, T> { using type = T; };
template<class T> using WhenIntegral = typename Enable<__is_integral(T), int>::type;
template<class T, WhenIntegral<T> N = 7> int selected() { return N; }
template<bool B> int bit() { return B ? 1 : 0; }
extern "C" bool classified_integer() { return __is_integral(int); }
extern "C" bool classified_enum() { return __is_integral(Plain); }
extern "C" bool classified_empty() { return __is_empty(EmptyDerived); }
extern "C" bool classified_reference_layout() { return __is_standard_layout(ReferenceRecord); }
extern "C" bool classified_private_base() { return __is_base_of(Empty, PrivateDerived); }
extern "C" bool classified_reverse_base() { return __is_base_of(EmptyDerived, Empty); }
extern "C" bool classified_final() { return __is_final(FinalRecord); }
extern "C" bool classified_literal() { return __is_literal(FinalRecord); }
extern "C" bool classified_unique_padded() { return __has_unique_object_representations(Padded); }
extern "C" bool classified_unique_empty() { return __has_unique_object_representations(EmptyDerived); }
template<class Base, class Derived> inline constexpr bool base_of = __is_base_of(Base, Derived);
template<class... T> constexpr bool all_empty() { return (__is_empty(T) && ...); }

static_assert(__is_integral(bool) && !__is_integral(Plain));
static_assert(__is_same(Null, decltype(nullptr)) && !__is_same(Null, void*));
static_assert(!__is_same(int, Plain) && !__is_same(char, signed char));
static_assert(!__is_same(long, long long));
static_assert(!__is_same(int&, int*) && !__is_same(int&, int&&));
static_assert(!__is_same(Function, CertainFunction));
static_assert(!__is_same(int[1], int*));
static_assert(__is_same_as(int(int[2]), int(int*)));

int main() {
  if (!classified_integer() || classified_enum()) return 1;
  if (!__is_arithmetic(float) || !__is_floating_point(double) ||
      __is_arithmetic(Scoped) || __is_signed(Scoped) ||
      !__is_signed(int) || !__is_signed(float) ||
      !__is_unsigned(bool) || !__is_unsigned(unsigned int)) return 2;
  if (!__is_enum(Plain) || !__is_enum(Scoped) || !__is_class(Empty) ||
      __is_union(Empty) || __is_class(int)) return 3;
  if (!__is_void(const void) || !__is_fundamental(Null) ||
      __is_fundamental(Plain) || !__is_scalar(Plain) ||
      !__is_scalar(Null) || __is_scalar(int&) || !__is_compound(int&)) return 4;
  if (!__is_array(int[2][3]) || __is_array(int*) ||
      !__is_object(int[2]) || __is_object(void) ||
      __is_object(Function) || __is_object(int&)) return 5;
  if (!__is_function(Function) || !__is_function(CertainFunction) ||
      __is_function(Function*) || !__is_pointer(Function*) ||
      __is_member_pointer(Function*) || __is_member_object_pointer(int*) ||
      __is_member_function_pointer(Function*)) return 6;
  if (!__is_reference(int&) || !__is_reference(int&&) ||
      !__is_lvalue_reference(const int&) || __is_lvalue_reference(int&&) ||
      !__is_rvalue_reference(int&&) || __is_rvalue_reference(int&) ||
      !__is_const(int* const) || __is_const(const int*) ||
      __is_volatile(int)) return 7;
  if (!integral<int> || integral<Plain> || !same<Null, Null> ||
      same<Plain, int> || !all_integral<bool, char, unsigned>() ||
      all_integral<int, double>() || !all_integral<>()) return 8;
  if (!Category<int*>::value || Category<int>::value ||
      category<int>() != 1 || category<void*>() != 2 || category<Plain>() != 3 ||
      selected<unsigned>() != 7 || bit<__is_same(int, int)>() != 1) return 9;
  if (!__is_same(decltype(++effects), int&) ||
      !__is_integral(decltype(effect())) || !__is_class(decltype(Probe{})) ||
      effects || constructed || destroyed) return 10;
  bool value = (++effects, __is_same(Plain, Plain));
  if (!value || effects != 1 || constructed || destroyed) return 11;
  if (__is_same(int[2], int[3]) || __is_same(int[2], int*) ||
      !__is_same(const int[2], const int[2]) ||
      __is_same(Function*, CertainFunction*)) return 12;
  if (!classified_empty() || classified_reference_layout() ||
      !classified_private_base() || classified_reverse_base()) return 13;
  if (!__is_aggregate(PlainRecord) || !__is_aggregate(int[2]) ||
      __is_aggregate(ConstructedRecord) || __is_aggregate(void)) return 14;
  if (!__is_trivial(PlainRecord) || __is_trivial(ConstructedRecord) ||
      !__is_trivially_copyable(ConstructedRecord) ||
      __is_trivially_copyable(DestructedRecord) ||
      !__is_trivially_copyable(int[2]) || __is_trivially_copyable(int&)) return 15;
  if (!__is_pod(PlainRecord) || __is_pod(ConstructedRecord) ||
      __is_polymorphic(DestructedRecord) || __is_abstract(DestructedRecord) ||
      !__is_standard_layout(EmptyDerived)) return 16;
  if (!base_of<Empty, EmptyDerived> || !base_of<const Empty, EmptyDerived> ||
      base_of<EmptyDerived, Empty> || !all_empty<Empty, EmptyDerived>() ||
      all_empty<Empty, PlainRecord>() || !all_empty<>()) return 17;
  if (__is_aggregate(decltype(Probe{})) || __is_trivial(decltype(Probe{})) ||
      effects != 1 || constructed || destroyed) return 18;
  if (!classified_final() || !classified_literal() ||
      classified_unique_padded() || classified_unique_empty()) return 19;
  if (!__is_literal(void) || !__is_literal(int&) || !__is_literal(int[2]) ||
      __is_literal(Function) || __is_literal(ConstructedRecord) ||
      __is_literal(DestructedRecord) || !__is_literal(ReferenceRecord)) return 20;
  if (!__has_unique_object_representations(Bytes) ||
      !__has_unique_object_representations(unsigned char[2][3]) ||
      !__has_unique_object_representations(ConstructedRecord) ||
      __has_unique_object_representations(double) ||
      __has_unique_object_representations(int&)) return 21;
  FinalRecord final(7);
  FinalRecord copied = final;
  FinalBox<int> box{9};
  if (copied.value != 7 || box.value != 9 || !__is_final(decltype(box)) ||
      sizeof(final) != sizeof(int) || sizeof(box) != sizeof(int)) return 22;
  if (__is_final(FinalRecord*) || __is_final(int) || !__is_final(const FinalRecord) ||
      __is_literal(decltype(Probe{})) || effects != 1 || constructed || destroyed) return 23;
  return 0;
}
