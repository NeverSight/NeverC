enum Plain : int { plain = 1 };
enum class Scoped : int { one = 1 };
using Function = int(int);
using CertainFunction = int(int) noexcept;
using Null = decltype(nullptr);
struct Record { int value; };
struct Base {};
struct Derived : Base {};
struct PrivateDerived : private Base {};

int effects;
int constructions;
int destructions;
int next() { return ++effects; }
struct Probe {
  int *pointer;
  Probe() : pointer(nullptr) { ++constructions; }
  ~Probe() { ++destructions; }
};

template<class T, class... Args> constexpr bool constructible() {
  return __is_constructible(T, Args...);
}
template<class T> constexpr bool operation_pair() {
  return __is_constructible(T, int) && __is_assignable(T&, int) &&
         __is_convertible(T, int);
}
template<class... T> constexpr bool all_destructible() {
  return (__is_nothrow_destructible(T) && ...);
}
template<class T, bool B = __is_trivially_constructible(T)>
struct Default { static constexpr bool value = B; };
template<class T> int category() {
  if constexpr (__is_assignable(T, int)) return 1;
  else if constexpr (__is_constructible(T, int)) return 2;
  else return 3;
}
template<class T, class... Args> inline constexpr bool trivial =
    __is_trivially_constructible(T, Args...);

extern "C" bool trait_construct() { return __is_constructible(int, double); }
extern "C" bool trait_nothrow_construct() { return __is_nothrow_constructible(int*, Null); }
extern "C" bool trait_trivial_construct() { return __is_trivially_constructible(int[2]); }
extern "C" bool trait_assign() { return __is_assignable(int&&, int); }
extern "C" bool trait_nothrow_assign() { return __is_nothrow_assignable(const int&, int); }
extern "C" bool trait_trivial_assign() { return __is_trivially_assignable(int*&, Null); }
extern "C" bool trait_convert() { return __is_convertible(Scoped, int); }
extern "C" bool trait_convert_to() { return __is_convertible_to(Plain, int); }
extern "C" bool trait_nothrow_convert() { return __is_nothrow_convertible(CertainFunction*, Function*); }
extern "C" bool trait_destruct() { return __is_destructible(Function); }
extern "C" bool trait_nothrow_destruct() { return __is_nothrow_destructible(const int(&)[2]); }
extern "C" bool trait_trivial_destruct() { return __is_trivially_destructible(Record*); }

static_assert(constructible<int>() && constructible<int, double>());
static_assert(!constructible<int, int, int>() && !constructible<void>());
static_assert(all_destructible<>() && all_destructible<int, int*, Null>());
static_assert(!all_destructible<int, void>());

int main() {
  if (!trait_construct() || !trait_nothrow_construct() ||
      !trait_trivial_construct()) return 1;
  if (trait_assign() || trait_nothrow_assign() || !trait_trivial_assign()) return 2;
  if (trait_convert() || !trait_convert_to() || !trait_nothrow_convert()) return 3;
  if (trait_destruct() || !trait_nothrow_destruct() ||
      !trait_trivial_destruct()) return 4;
  if (__is_constructible(int&) ||
      !__is_constructible(int&, int&) || __is_constructible(int&, int) ||
      !__is_constructible(const int&, int) || !__is_constructible(int&&, int) ||
      __is_constructible(int&&, int&)) return 5;
  if (!__is_constructible(const int) || !__is_constructible(int*) ||
      !__is_trivially_constructible(Null) || __is_constructible(void)) return 6;
  if (!__is_convertible(int&, const int&) || __is_convertible(const int&, int&) ||
      !__is_nothrow_convertible(int, const int&) || __is_convertible(int, int&)) return 7;
  if (!__is_assignable(const int*&, int*) || __is_assignable(int*&, const int*) ||
      __is_assignable(int* const&, int*) || __is_assignable(int, int)) return 8;
  if (!__is_convertible(int*, const void*) || __is_convertible(const int*, void*) ||
      !__is_convertible(Null, Record*) || __is_convertible(Null, bool) ||
      !__is_constructible(bool, Null)) return 9;
  if (!__is_convertible(void, const void) || !__is_nothrow_convertible(void, void) ||
      __is_convertible(int, void) || __is_convertible(void, int) ||
      __is_destructible(void) || __is_assignable(void, int)) return 10;
  if (!__is_constructible(int[2][3]) || !__is_trivially_destructible(const int[2][3]) ||
      __is_constructible(int[2], int) || __is_assignable(int(&)[2], int(&)[2])) return 11;
  if (!__is_convertible(int[3], const int*) || __is_convertible(int*, int[3]) ||
      !__is_constructible(int(&)[3], int(&)[3]) ||
      __is_constructible(int(&)[3], int*)) return 12;
  if (!__is_constructible(Function*, Function) || __is_constructible(Function) ||
      !__is_convertible(Function, Function*) ||
      __is_convertible(Function*, CertainFunction*)) return 13;
  if (!__is_constructible(Record*) || !__is_trivially_assignable(Record*&, Record*) ||
      !__is_destructible(Record*[2]) || !__is_destructible(Record*&)) return 14;
  if (!__is_convertible(Derived*, Base*) || __is_convertible(Base*, Derived*) ||
      __is_convertible(PrivateDerived*, Base*)) return 15;
  if (!Default<int>::value || Default<int&>::value ||
      category<int&>() != 1 || category<int>() != 2 || category<void*>() != 3 ||
      !trivial<int, int> || trivial<int, int, int>) return 16;
  if (!__is_constructible(decltype(next()), decltype(++effects)) ||
      !__is_assignable(decltype(++effects), int) ||
      !__is_constructible(decltype(Probe{}.pointer)) ||
      !__is_destructible(decltype(Probe{}.pointer)) ||
      effects || constructions || destructions) return 17;
  bool value = (++effects, __is_nothrow_constructible(int));
  if (!value || effects != 1 || constructions || destructions ||
      !noexcept(__is_assignable(decltype(++effects), int))) return 18;
  if (!operation_pair<int>() || !operation_pair<double>() ||
      !operation_pair<int>() || constructible<int, int, int>() ||
      !constructible<int, int>()) return 19;
  if (!__is_constructible(decltype(__is_convertible(void, void)), int) ||
      __is_convertible(int, void) || !__is_convertible(void, void) ||
      !__is_assignable(int&, int) || effects != 1 ||
      constructions || destructions) return 20;
  return 0;
}
