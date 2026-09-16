using Size = decltype(sizeof(0));
extern "C" void *malloc(Size);
extern "C" void *calloc(Size, Size);
extern "C" void *realloc(void *, Size);
extern "C" void free(void *);

extern "C" void *native_heap_allocate(Size n) { return malloc(n); }
extern "C" void *native_heap_zero(Size n, Size width) { return calloc(n, width); }
extern "C" void *native_heap_resize(void *p, Size n) { return realloc(p, n); }
extern "C" void native_heap_release(void *p) { free(p); }
extern "C" void native_heap_fill(void *p, Size n) {
  auto *bytes = static_cast<unsigned char *>(p);
  for (Size i = 0; i < n; ++i) bytes[i] = static_cast<unsigned char>(i + 17);
}

int created, destroyed, destroyOrder, liveArguments;
struct Argument {
  Argument() { ++liveArguments; }
  ~Argument() { --liveArguments; }
};
struct Item {
  int id;
  const Item *self;
  Item(const Argument &arg = Argument()) : id(++created), self(this) {}
  ~Item() { destroyOrder = destroyOrder * 10 + id; ++destroyed; }
  static void *operator new(Size n) noexcept { return malloc(n); }
  static void operator delete(void *p) noexcept { free(p); }
  static void *operator new[](Size n) noexcept { return malloc(n); }
  static void operator delete[](void *p) noexcept { free(p); }
};
extern "C" int native_heap_lifetimes(int n) {
  created = destroyed = destroyOrder = liveArguments = 0;
  Item *single = new Item;
  if (!single) return 1;
  if (single->self != single || single->id != 1 || liveArguments) return 2;
  delete single;
  if (destroyed != 1 || destroyOrder != 1) return 3;
  created = destroyed = destroyOrder = 0;
  Item *items = new Item[n];
  if (!items) return 4;
  if (created != n || liveArguments) return 5;
  for (int i = 0; i < n; ++i)
    if (items[i].id != i + 1 || items[i].self != &items[i]) return 6;
  delete[] items;
  if (destroyed != n || (n == 3 && destroyOrder != 321)) return 7;
  Item *empty = new Item[0];
  delete[] empty;
  if (created != n || destroyed != n) return 8;
  int negative = -n;
  Item *invalid = new Item[negative];
  if (invalid) { delete[] invalid; return 9; }
  return created != n || destroyed != n ? 10 : 0;
}
extern "C" int native_heap_argument_sequence() {
  Size count = 3;
  auto *p = static_cast<unsigned char *>(calloc(count++, count++));
  if (!p) return 1;
  int result = count == 5 ? 0 : 2;
  for (Size i = 0; i < 12; ++i) if (p[i]) result = 3;
  free(p);
  free(nullptr);
  return result;
}
