using Size = decltype(sizeof(0));
struct Storage { unsigned long long alignment; unsigned char bytes[1024]; };
Storage slots[64]{};
int allocations = 0, frees = 0, used = 0, failAllocation = 0;
Size allocatedSize = 0, freedSize = 0;
void *allocatedRaw = nullptr, *freedRaw = nullptr;
void *take(Size size) {
  ++allocations; allocatedSize = size;
  if (failAllocation || size > 1024 || used == 64) return nullptr;
  allocatedRaw = slots[used++].bytes;
  return allocatedRaw;
}
int events[64]{}, eventCount = 0, nextValue = 0, initializers = 0;
int boundLive = 0, boundDrops = 0, conversions = 0, seenBound = 0;
struct Bound {
  long long value;
  Bound(long long n) : value(n) { ++boundLive; }
  operator long long() const { ++conversions; return value; }
  ~Bound() { --boundLive; ++boundDrops; }
};
struct R;
R **reseat = nullptr;
struct R {
  int value;
  R *self;
  R(int n = ++nextValue) : value(n), self(this) { ++initializers; seenBound = boundLive; }
  ~R() { events[eventCount++] = value; if (reseat) *reseat = nullptr; }
  static void *operator new[](Size size) noexcept { return take(size); }
  static void operator delete[](void *pointer, Size size) noexcept {
    ++frees; freedRaw = pointer; freedSize = size;
  }
};
Size cookieBytes() {
#if defined(__APPLE__) && defined(__aarch64__)
  return 2 * sizeof(Size) > alignof(R) ? 2 * sizeof(Size) : alignof(R);
#else
  return sizeof(Size) > alignof(R) ? sizeof(Size) : alignof(R);
#endif
}
R *make(int n) { return new R[n]; }
R *makeUnsigned(unsigned long long n) { return new R[n]; }
R *makeSigned(long long n) { return new R[n]; }
R *makeConverted(long long n) { return new R[Bound(n)]; }
R *makeCasted(long long n) { return new R[static_cast<Size>(n)]; }
R *makePrefix(int n) { return new R[n]{R(41), R(42)}; }
void dispose(R *pointer) { delete[] pointer; }
int liveArguments = 0, argumentDrops = 0, argumentSerial = 0, seenArguments = 0;
int dropOrder[16]{};
struct Argument {
  int id;
  Argument() : id(++argumentSerial) { ++liveArguments; }
  ~Argument() { --liveArguments; dropOrder[argumentDrops++] = id; }
};
struct Direct {
  Direct(const Argument &argument = Argument()) {
    seenArguments = seenArguments * 10 + liveArguments;
  }
  ~Direct() {}
  static void *operator new[](Size n) noexcept { return take(n); }
  static void operator delete[](void *) noexcept { ++frees; }
};
Direct *makeDirect(int n) { return new Direct[n]{}; }
Direct *makeDirectPrefix(int n) { return new Direct[n]{{},{}}; }
using Row = Direct[2];
Row *makeRows(int n) { return new Direct[n][2]; }
struct Plain {
  int value;
  static void *operator new[](Size n) noexcept { return take(n); }
  static void operator delete[](void *) noexcept { ++frees; }
};
Plain *makeZero(int n) { return new Plain[n](); }
void resetArguments() {
  liveArguments = argumentDrops = argumentSerial = seenArguments = 0;
}
extern "C" int runtime_array_check() {
  R *values = make(3);
  if (!values || initializers != 3 || values[2].value != 3 || values[2].self != &values[2]) return 1;
  void *raw = allocatedRaw;
  Size bytes = allocatedSize;
  if (bytes != 3 * sizeof(R) + cookieBytes()) return 2;
  reseat = &values; dispose(values); reseat = nullptr;
  if (values || freedRaw != raw || freedSize != bytes || eventCount != 3 || events[0] != 3 || events[2] != 1) return 3;
  int before = allocations, initialized = initializers;
  values = make(-1);
  if (values || allocations != before || initializers != initialized) return 4;
  values = makeSigned(-4294967296LL);
  if (values || allocations != before || initializers != initialized) return 5;
  Size maximum = static_cast<Size>(-1);
  unsigned long long tooMany = (maximum - cookieBytes()) / sizeof(R) + 1;
  values = makeUnsigned(tooMany);
  if (values || allocations != before || initializers != initialized) return 6;
  values = makePrefix(1);
  if (values || allocations != before || initializers != initialized) return 7;
  eventCount = 0;
  values = make(0); raw = allocatedRaw;
  if (!values || allocations != before + 1 || allocatedSize != cookieBytes() || initializers != initialized) return 8;
  dispose(values);
  if (eventCount || freedRaw != raw || freedSize != cookieBytes()) return 9;
  before = allocations; failAllocation = 1;
  values = makePrefix(3);
  if (values || allocations != before + 1 || initializers != initialized) return 10;
  failAllocation = 0;
  values = makePrefix(4);
  if (!values || values[0].value != 41 || values[1].value != 42 || initializers != initialized + 4) return 11;
  dispose(values);
  eventCount = 0; before = allocations;
  values = makeConverted(-1);
  if (values || allocations != before || conversions != 1 || boundLive || boundDrops != 1) return 12;
  failAllocation = 1;
  values = makeConverted(2);
  if (values || allocations != before + 1 || conversions != 2 || boundLive || boundDrops != 2) return 13;
  failAllocation = 0;
  values = makeConverted(2);
  if (!values || conversions != 3 || boundLive || boundDrops != 3 || seenBound != 1) return 14;
  dispose(values);
  resetArguments();
  Direct *direct = makeDirect(3);
  if (!direct || seenArguments != 111 || liveArguments || argumentDrops != 3 || dropOrder[0] != 1 || dropOrder[2] != 3) return 15;
  delete[] direct;
  resetArguments();
  direct = makeDirectPrefix(5);
  if (!direct || seenArguments != 12333 || liveArguments || argumentDrops != 5) return 16;
  if (dropOrder[0] != 3 || dropOrder[1] != 4 || dropOrder[2] != 5 || dropOrder[3] != 2 || dropOrder[4] != 1) return 17;
  delete[] direct;
  resetArguments(); before = allocations;
  direct = makeDirectPrefix(1);
  if (direct || allocations != before || liveArguments || argumentDrops || seenArguments) return 18;
  direct = makeDirect(0);
  if (!direct || allocations != before + 1 || liveArguments || argumentDrops || seenArguments) return 19;
  delete[] direct;
  resetArguments();
  Row *rows = makeRows(2);
  if (!rows || seenArguments != 1111 || liveArguments || argumentDrops != 4) return 20;
  delete[] rows;
  Plain *plain = makeZero(3);
  if (!plain || plain[0].value || plain[2].value || allocatedSize != 3 * sizeof(Plain)) return 21;
  delete[] plain;
  if (sizeof(Size) == 4) {
    eventCount = 0;
    values = makeUnsigned(4294967298ULL);
    if (!values || allocatedSize != 2 * sizeof(R) + cookieBytes()) return 22;
    dispose(values);
    values = makeCasted(-4294967296LL);
    if (!values || allocatedSize != cookieBytes()) return 23;
    dispose(values);
  }
  int n = 2;
  eventCount = 0;
  values = new R[n++];
  if (!values || n != 3 || allocatedSize != 2 * sizeof(R) + cookieBytes()) return 24;
  dispose(values);
  return 0;
}
