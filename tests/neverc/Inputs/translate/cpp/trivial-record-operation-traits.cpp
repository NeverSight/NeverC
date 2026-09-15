struct Plain { int value; };
struct Nested { Plain values[2]; };
struct Empty {};
struct Derived : Empty {};
struct Reference { int &value; };
struct Constant { const int value; };
int effects;
int destructions;
struct Probe { int value; ~Probe() { ++destructions; } };
struct Deleted { ~Deleted() = delete; };
template<class T> struct Lazy { T value; Lazy() { T::missing(); } };
template<class T, class... Args> constexpr bool constructs() {
  return __is_constructible(T, Args...) && __is_trivially_constructible(T, Args...);
}
extern "C" bool record_default() { return __is_constructible(Plain); }
extern "C" bool record_copy() { return __is_trivially_constructible(Plain, const Plain&); }
extern "C" bool record_assign() { return __is_trivially_assignable(Plain&, Plain); }
extern "C" bool record_convert() { return __is_convertible(Plain&, Plain); }
extern "C" bool record_reference() { return __is_constructible(const Probe&, Probe&); }
extern "C" bool record_false() { return __is_convertible(Plain, void); }
extern "C" bool record_reference_destruct() { return __is_nothrow_destructible(Deleted&); }
extern "C" bool record_nothrow() { return __is_nothrow_constructible(Nested, Nested); }
static_assert(constructs<Plain>() && constructs<Plain, Plain>() && constructs<Plain, const Plain&>());
static_assert(__is_constructible(Lazy<int>, Lazy<int>) && __is_assignable(Lazy<int>&, Lazy<int>));

using UnknownRecord = Plain[];
template<class T> struct UnknownLazy {
  T value;
  ~UnknownLazy() noexcept(T::missing) { T::destroy(); }
};
static_assert(sizeof(UnknownLazy<int>) == sizeof(int));
using UnknownPoison = UnknownLazy<int>[];
extern "C" bool record_unknown_construct() { return __is_constructible(UnknownRecord); }
extern "C" bool record_unknown_destruct() { return __is_nothrow_destructible(UnknownPoison); }
extern "C" bool record_unknown_reference() { return __is_nothrow_constructible(UnknownPoison&,UnknownPoison&); }
extern "C" bool record_unknown_convert() { return __is_nothrow_convertible(UnknownPoison,UnknownLazy<int>*); }
extern "C" bool record_unknown_assign() { return __is_trivially_assignable(Plain*&,UnknownRecord); }

int main() {
  if (!record_default() || !record_copy() || !record_assign() || !record_convert() ||
      !record_reference() || record_false()) return 1;
  if (!__is_constructible(Plain[2]) || !__is_trivially_constructible(Plain[2][3]) ||
      !__is_convertible(Plain[2], Plain*)) return 2;
  if (!__is_constructible(Reference, Reference) ||
      !__is_constructible(Constant, const Constant&)) return 3;
  if (!__is_constructible(Nested, Nested) || !__is_assignable(Nested&, Nested) ||
      !__is_constructible(Derived, Derived) || !__is_assignable(Derived&, Derived)) return 4;
  if (!__is_constructible(Deleted&, Deleted&) || !__is_convertible(Probe&, const Probe&)) return 5;
  if (__is_constructible(void, Plain) || __is_assignable(void, Plain) ||
      __is_assignable(Plain&, void) || __is_convertible(Plain, Plain[2])) return 6;
  if (!__is_constructible(decltype((++effects, Plain{})), Plain) || effects || destructions) return 7;
  Plain original{7};
  Plain copy = original;
  Plain assigned{};
  assigned = copy;
  if (copy.value != 7 || assigned.value != 7 || &copy == &original) return 8;
  int value = 9;
  Reference reference{value};
  Reference reference_copy = reference;
  reference_copy.value = 11;
  if (value != 11 || &reference_copy.value != &reference.value) return 9;
  Nested nested{{{3}, {4}}};
  Nested nested_copy = nested;
  Nested nested_assigned{};
  nested_assigned = nested_copy;
  if (nested_assigned.values[0].value != 3 || nested_assigned.values[1].value != 4) return 10;
  {
    Probe probe{13};
    if (!__is_constructible(Probe&, decltype(probe)&) || destructions) return 11;
  }
  if (destructions != 1 || effects || !constructs<Plain, Plain>()) return 12;
  if (!record_reference_destruct() || !__is_nothrow_destructible(Probe&) ||
      !__is_nothrow_destructible(Nested(&)[2]) || destructions != 1) return 13;
  if (!record_nothrow() || !__is_nothrow_constructible(Plain[2]) ||
      !__is_nothrow_assignable(Nested&, Nested) || !__is_nothrow_convertible(Plain, Plain) ||
      destructions != 1 || effects) return 14;
  if (record_unknown_construct() || record_unknown_destruct() ||
      !record_unknown_reference() || !record_unknown_convert() ||
      !record_unknown_assign() || effects || destructions != 1) return 15;
  if (__is_constructible(UnknownPoison) ||
      !__is_nothrow_destructible(UnknownPoison&) ||
      !__is_convertible(UnknownPoison&, const UnknownPoison&) ||
      !__is_trivially_constructible(UnknownRecord&, UnknownRecord&)) return 16;
  if (!__is_nothrow_convertible(Plain[][3], Plain(*)[3]) ||
      !__is_nothrow_assignable(Plain(*&)[3], Plain[][3]) ||
      __is_convertible(Plain*, UnknownRecord) || constructs<UnknownRecord>()) return 17;
  if (!__is_convertible(decltype((++effects, Plain{}))[], Plain*) ||
      effects || destructions != 1) return 18;
  return 0;
}
