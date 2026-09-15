using Size = decltype(sizeof(0));
extern "C" void *malloc(Size);
extern "C" void free(void *);

int serial, effects, allocations, frees, fail;
int events[32]{}, eventCount;
int existing = 17;
int next() { ++effects; return ++serial; }
int twice(int value = 5) { ++effects; return value * 2; }
struct Inner { int values[2] = {twice(), twice(6)}; };
struct R {
  int id = next();
  R *self = this;
  const int &own = id;
  int &shared = existing;
  int copied = own;
  Inner inner{};
  decltype(nullptr) null{};
  ~R() { events[eventCount++] = id; }
  static void *operator new[](Size size) noexcept {
    ++allocations;
    return fail ? nullptr : malloc(size);
  }
  static void operator delete[](void *pointer) noexcept { ++frees; free(pointer); }
};

R *make(int count) { return new R[count]{}; }
R *prefix(int count) { return new R[count]{{41}}; }
using Row = R[2];
Row *rows(int count) { return new R[count][2]{}; }
template<class T> T *generic(int count) { return new T[count]{}; }
bool valid(R &value, int id) {
  return value.id == id && value.self == &value && &value.own == &value.id &&
         &value.shared == &existing && value.copied == id && value.null == nullptr &&
         value.inner.values[0] == 10 && value.inner.values[1] == 12;
}

extern "C" int runtime_aggregate_array_check() {
  serial = effects = allocations = frees = eventCount = fail = 0;
  R *values = make(3);
  if (!values || !valid(values[0], 1) || !valid(values[2], 3) || effects != 9)
    return 1;
  values[1].shared = 29;
  if (existing != 29 || values[0].shared != 29 || &values[0].own == &values[1].own)
    return 2;
  delete[] values;
  if (frees != 1 || eventCount != 3 || events[0] != 3 || events[2] != 1)
    return 3;
  int before = effects, allocated = allocations;
  values = make(0);
  if (effects != before || allocations != allocated + 1)
    return 4;
  delete[] values;
  allocated = allocations;
  fail = 1;
  values = prefix(3);
  if (values || effects != before || allocations != allocated + 1)
    return 7;
  fail = 0;
  serial = effects = eventCount = 0;
  values = prefix(3);
  if (!values || !valid(values[0], 41) || !valid(values[1], 1) ||
      !valid(values[2], 2) || effects != 8)
    return 8;
  delete[] values;
  if (eventCount != 3 || events[0] != 2 || events[1] != 1 || events[2] != 41)
    return 9;
  serial = effects = eventCount = 0;
  Row *matrix = rows(2);
  if (!matrix || !valid(matrix[0][1], 2) || !valid(matrix[1][0], 3) ||
      !valid(matrix[1][1], 4) || effects != 12)
    return 10;
  delete[] matrix;
  if (eventCount != 4 || events[0] != 4 || events[3] != 1)
    return 11;
  serial = effects = eventCount = 0;
  values = generic<R>(2);
  if (!values || !valid(values[0], 1) || !valid(values[1], 2) || effects != 6)
    return 12;
  delete[] values;
  if (eventCount != 2 || events[0] != 2 || events[1] != 1)
    return 13;
  // Check invalid lengths after all valid initialization and destruction cases.
  // Unmodified Clang 20 calls the allocator with SIZE_MAX here; C++17 requires
  // no allocation call. Keep the translated result strict while letting its
  // upstream diagnostic baseline exercise the preceding independent cases.
  before = effects;
  allocated = allocations;
  values = make(-1);
  if (values || effects != before || allocations != allocated)
    return 5;
  values = prefix(0);
  if (values || effects != before || allocations != allocated)
    return 6;
  return 0;
}

int main() { return runtime_aggregate_array_check(); }
