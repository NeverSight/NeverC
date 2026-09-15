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
namespace EmptyLifecycle {
int events[64], event_count, bad, live, defaults, copy_defaults;
int constructions, copies, moves, copy_assignments, move_assignments, destructions;
int token_live, early_return;
void mark(int event) { if (event_count < 64) events[event_count++] = event; else ++bad; }
int next() noexcept { ++defaults; return 3; }
int copyNext() noexcept { ++copy_defaults; return 11; }
struct Token {
  Token() noexcept { ++token_live; }
  ~Token() { --token_live; mark(2); }
};
struct Base;
const Base *last_source;
Base *last_target;
struct Base {
  Base(int n = next(), const Token& = Token()) noexcept {
    if (token_live != 1 || (n != 3 && n != 9)) ++bad;
    ++live; ++constructions; last_target = this; mark(1);
  }
  Base(const Base& other, int n = copyNext()) noexcept {
    if (n != 11) ++bad;
    ++live; ++copies; last_source = &other; last_target = this; mark(11);
  }
  Base(Base&& other) noexcept {
    ++live; ++moves; last_source = &other; last_target = this; mark(12);
  }
  Base& operator=(const Base& other) noexcept {
    ++copy_assignments; last_source = &other; last_target = this; mark(21); return *this;
  }
  Base& operator=(Base&& other) noexcept {
    ++move_assignments; last_source = &other; last_target = this; mark(22); return *this;
  }
  ~Base() { --live; ++destructions; mark(6); }
};
struct Middle : Base {
  Middle() : Base() { if (token_live) ++bad; mark(3); }
  ~Middle() { mark(5); if (early_return) return; mark(50); }
};
struct Chain : Middle {
  Chain() : Middle() { mark(7); }
  ~Chain() { mark(8); }
};
struct Generated : Base {};
struct Heap : Base {
  static void *operator new[](Size n) noexcept { return malloc(n); }
  static void operator delete[](void *p) noexcept { free(p); }
};
int delegations;
struct ZeroBase {
  ZeroBase() = default;
  ZeroBase(int) : ZeroBase() { ZeroBase local{}; ++delegations; }
  ZeroBase(long) : ZeroBase(1) {}
};
struct ZeroDerived : ZeroBase { ZeroDerived() : ZeroBase(1L) {} };
int static_initializations, static_observed;
int initial() { ++static_initializations; return 4; }
template<class T> struct SharedBase {
  SharedBase(int) { static int value = initial(); static_observed = ++value; }
};
template<class T> struct SharedDerived : SharedBase<T> {
  SharedDerived() : SharedBase<T>(1) {}
};
template<class T> struct GenericBase {
  GenericBase() { if constexpr (__is_same(T, int)) T::body(); else mark(31); }
  ~GenericBase() { if constexpr (__is_same(T, int)) T::body(); else mark(32); }
};
template<class T> struct GenericDerived : T { GenericDerived() : T() { mark(33); } };
static_assert(sizeof(GenericDerived<GenericBase<int>>) == 1);
int value_live, value_destroyed, value_used;
struct Value {
  int n;
  Value(int v) : n(v) { ++value_live; }
  Value(const Value& v) : n(v.n) { ++value_live; }
  ~Value() { --value_live; ++value_destroyed; }
};
struct ByValueBase {
  ByValueBase(Value value) { value_used += value.n; }
  ByValueBase() : ByValueBase(Value(11)) {}
};
struct ByValueDerived : ByValueBase { ByValueDerived() : ByValueBase(Value(7)) {} };
struct ByValueDelegated : ByValueBase { ByValueDelegated() : ByValueBase() {} };
}

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
  using namespace EmptyLifecycle;
  event_count = 0;
  early_return = 1;
  {
    Chain object;
    if (bad || live != 1 || token_live || defaults != 1 || constructions != 1 ||
        event_count != 4 || events[0] != 1 || events[1] != 2 || events[2] != 3 || events[3] != 7 ||
        last_target != static_cast<EmptyLifecycle::Base*>(&object)) return 16;
  }
  if (bad || live || destructions != 1 || event_count != 7 ||
      events[4] != 8 || events[5] != 5 || events[6] != 6) return 17;
  event_count = 0;
  {
    Generated first;
    Generated second(first);
    if (copies != 1 || copy_defaults != 1 || last_source != static_cast<EmptyLifecycle::Base*>(&first) ||
        last_target != static_cast<EmptyLifecycle::Base*>(&second) || last_source == last_target) return 18;
    Generated third(static_cast<Generated&&>(second));
    if (moves != 1 || last_source != static_cast<EmptyLifecycle::Base*>(&second) ||
        last_target != static_cast<EmptyLifecycle::Base*>(&third) || last_source == last_target) return 19;
    Generated& assigned = (first = third);
    if (&assigned != &first || copy_assignments != 1 ||
        last_source != static_cast<EmptyLifecycle::Base*>(&third) ||
        last_target != static_cast<EmptyLifecycle::Base*>(&first)) return 20;
    Generated& moved = (third = static_cast<Generated&&>(first));
    if (&moved != &third || move_assignments != 1 || live != 3 || token_live || bad ||
        event_count != 6 || events[0] != 1 || events[1] != 2 || events[2] != 11 ||
        events[3] != 12 || events[4] != 21 || events[5] != 22) return 21;
  }
  if (live || destructions != 4 || event_count != 9 ||
      events[6] != 6 || events[7] != 6 || events[8] != 6) return 22;
  event_count = 0;
  {
    Generated objects[2];
    if (live != 2 || token_live || bad || event_count != 4 ||
        events[0] != 1 || events[1] != 2 || events[2] != 1 || events[3] != 2 ||
        static_cast<EmptyLifecycle::Base*>(&objects[0]) == static_cast<EmptyLifecycle::Base*>(&objects[1])) return 23;
  }
  if (live || destructions != 6 || event_count != 6 || events[4] != 6 || events[5] != 6) return 24;
  event_count = 0;
  {
    int previous = defaults;
    int count = 3;
    Heap *objects = new Heap[count];
    if (!objects || live != 3 || defaults != previous + 3 || token_live || bad || event_count != 6 ||
        static_cast<Heap*>(static_cast<EmptyLifecycle::Base*>(objects + 1)) != objects + 1) return 25;
    delete[] objects;
  }
  if (live || destructions != 9 || event_count != 9 ||
      events[6] != 6 || events[7] != 6 || events[8] != 6) return 26;
  event_count = 0;
  {
    int previous = defaults;
    Generated object{9};
    if (live != 1 || defaults != previous || token_live || bad || event_count != 2 ||
        events[0] != 1 || events[1] != 2) return 27;
  }
  if (live || destructions != 10 || event_count != 3 || events[2] != 6) return 28;
  ZeroBase complete(1L);
  ZeroDerived subobject;
  if (delegations != 2) return 29;
  SharedBase<unsigned> complete_first(1);
  if (static_initializations != 1 || static_observed != 5) return 30;
  SharedDerived<unsigned> base_second;
  SharedBase<unsigned> complete_third(1);
  SharedDerived<unsigned> base_fourth;
  if (static_initializations != 1 || static_observed != 8) return 31;
  SharedDerived<char> base_first;
  if (static_initializations != 2 || static_observed != 5) return 32;
  SharedBase<char> complete_second(1);
  if (static_initializations != 2 || static_observed != 6) return 33;
  event_count = 0;
  {
    GenericDerived<GenericBase<unsigned>> object;
    if (event_count != 2 || events[0] != 31 || events[1] != 33) return 34;
  }
  if (event_count != 3 || events[2] != 32 || live || token_live || bad) return 35;
  {
    ByValueBase complete(Value(3));
    if (value_live || value_destroyed != 1 || value_used != 3) return 36;
    ByValueDerived base;
    if (value_live || value_destroyed != 2 || value_used != 10) return 37;
    Value source(5);
    ByValueBase copied(source);
    if (value_live != 1 || value_destroyed != 3 || value_used != 15 || source.n != 5) return 38;
  }
  if (value_live || value_destroyed != 4) return 39;
  ByValueBase delegated;
  if (value_live || value_destroyed != 5 || value_used != 26) return 40;
  ByValueDelegated delegated_base;
  if (value_live || value_destroyed != 6 || value_used != 37) return 41;
  return 0;
}
