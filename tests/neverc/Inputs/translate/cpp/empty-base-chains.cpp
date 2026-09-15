int effects = 0;
using Size = decltype(sizeof(0));
extern "C" void *malloc(Size);
extern "C" void free(void *);
struct Base {
  static constexpr int value = 7;
  inline static int data = 5;
  constexpr operator bool() const { return true; }
  constexpr int operator()() const { return value; }
  int bump() { return ++effects; }
  const Base *self() const { return this; }
};
struct Middle : Base {};
struct Derived : Middle {
  static void *operator new[](Size n) noexcept { return malloc(n); }
  static void operator delete[](void *p) noexcept { free(p); }
};
constexpr Derived constant{};
constexpr const Base *constant_base = &constant;
constexpr const Derived *constant_roundtrip = static_cast<const Derived *>(constant_base);
static_assert(constant_roundtrip == &constant);
static_assert(sizeof(Derived) == 1 && alignof(Derived) == 1);
template<bool V> struct Boolean {
  static constexpr bool value = V;
  constexpr operator bool() const { return V; }
};
template<class T, class U> struct Same : Boolean<__is_same(T, U)> {};
template<class T> struct Wrapper : T {};
template<class T> struct Choice : Boolean<false> {};
template<class T> struct Choice<T *> : Boolean<true> {};
static_assert(Same<int, int>::value && !Same<int, unsigned>::value);
static_assert(Choice<int *>::value && !Choice<int>::value);
Derived *once(Derived *p) { ++effects; return p; }
int count() { return 3; }
int main() {
  Derived first, second{};
  Base *base = &first;
  if (static_cast<Derived *>(base) != &first || first.self() != base) return 1;
  if (&first == &second || !first || first() != 7) return 2;
  if (constant_base != &constant || constant_roundtrip != &constant) return 3;
  effects = 0;
  Base *null_base = once(nullptr);
  if (null_base || effects != 1 || static_cast<Derived *>(null_base)) return 4;
  Base *once_base = once(&second);
  if (once_base != &second || effects != 2) return 5;
  if (first.bump() != 3 || effects != 3) return 6;
  Derived array[2]{};
  Base *left = &array[0], *right = &array[1];
  if (left == right || static_cast<Derived *>(right) != array + 1) return 7;
  if ((left + 1) - left != 1) return 8;
  const Base &reference = first;
  if (&static_cast<const Derived &>(reference) != &first) return 9;
  const Base &temporary = Derived{};
  if (!temporary || temporary.self() != &temporary) return 10;
  second = first;
  Derived copied = second;
  Derived moved = static_cast<Derived &&>(copied);
  if (&copied == &moved || copied.self() == moved.self()) return 11;
  Wrapper<Derived> wrapped{};
  if (static_cast<Wrapper<Derived> *>(static_cast<Base *>(&wrapped)) != &wrapped) return 12;
  if (!Same<int, int>{} || Same<int, unsigned>{} || !Choice<int *>{}) return 13;
  int n = count();
  Derived *heap = new Derived[n]{};
  Base *heap_base = &heap[1];
  if (static_cast<Derived *>(heap_base) != heap + 1 || !heap[2]) return 14;
  delete[] heap;
  Derived::data = 9;
  if (Base::data != 9 || wrapped.data != 9) return 15;
  return 0;
}
