using Size = decltype(sizeof(0));
struct Storage { unsigned long long alignment; unsigned char bytes[1024]; };
Storage slots[64]{};
int allocations = 0, frees = 0, used = 0;
Size allocatedSize = 0, freedSize = 0;
void *allocatedRaw = nullptr, *freedRaw = nullptr;
void *take(Size size) {
  ++allocations;
  allocatedSize = size;
  allocatedRaw = slots[used++].bytes;
  return allocatedRaw;
}
void *operator new[](Size size) { return take(size); }
void operator delete[](void *pointer) noexcept { ++frees; freedRaw = pointer; }
struct Tag {};
void *operator new(Size, Tag, void *pointer) { return pointer; }
int events[64]{}, eventCount = 0, nextValue = 0;
struct R;
R **reseat = nullptr;
struct R {
  int value;
  R *self;
  R(int n = ++nextValue) : value(n), self(this) {}
  ~R() { events[eventCount++] = value; if (reseat) *reseat = nullptr; }
  static void *operator new[](Size size) { return take(size); }
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
R *makeRecords() { return new R[3]; }
void deleteRecords(R *pointer) { delete[] pointer; }
int liveArguments = 0, argumentDrops = 0, seenArguments = 0;
struct Argument {
  Argument() { ++liveArguments; }
  ~Argument() { --liveArguments; ++argumentDrops; }
};
struct Direct {
  Direct(const Argument &argument = Argument()) {
    seenArguments = seenArguments * 10 + liveArguments;
  }
  ~Direct() {}
};
struct Aggregate { Direct field; };
int initializerCalls = 0, placementSeen = 0;
int initial() { ++initializerCalls; return 1; }
struct Failed {
  int value;
  Failed(int n = initial()) : value(n) {}
  ~Failed() { ++initializerCalls; }
  static void *operator new[](Size, const Argument &) noexcept {
    placementSeen = liveArguments;
    return nullptr;
  }
};
struct Temporary { int value; ~Temporary() { events[eventCount++] = value; } };
struct Reference { const Temporary &value; };
template<class T> void *operator new[](Size size, T *pointer, int) {
  ++allocations; allocatedSize = size; allocatedRaw = pointer; return pointer;
}
int identity(int n) { return n + 1; }
extern "C" int array_lifetime_check() {
  int *values = new int[5]{2,3};
  if (values[0] != 2 || values[1] != 3 || values[4] != 0 || allocatedSize != 5 * sizeof(int)) return 1;
  void *raw = allocatedRaw;
  delete[] values;
  if (freedRaw != raw || frees != 1) return 2;
  int *uninitialized = new int[2];
  uninitialized[0] = 4; uninitialized[1] = 5;
  if (uninitialized[1] != 5) return 3;
  delete[] uninitialized;
  int *zeroed = new int[2]();
  if (zeroed[0] || zeroed[1]) return 4;
  delete[] zeroed;
  int before = allocations;
  int *zero = new int[0]{};
  raw = allocatedRaw;
  if (!zero || allocations != before + 1 || allocatedSize != 0) return 5;
  delete[] zero;
  if (freedRaw != raw) return 6;
  const int *constant = new const int[3]{6,7};
  if (constant[1] != 7 || constant[2]) return 7;
  delete[] constant;
  int (*matrix)[3] = new int[2][3]{{1,2,3},{4,5,6}};
  if (matrix[1][2] != 6 || matrix[0][1] != 2 || allocatedSize != 6 * sizeof(int)) return 8;
  raw = allocatedRaw; delete[] matrix;
  if (freedRaw != raw) return 9;
  char *text = new char[8]{"abc"};
  if (text[0] != 'a' || text[2] != 'c' || text[3] || text[7]) return 10;
  delete[] text;
  using Callback = int (*)(int);
  Callback *callbacks = new Callback[2]{identity};
  if (callbacks[0](8) != 9 || callbacks[1]) return 11;
  delete[] callbacks;
  R *records = makeRecords();
  raw = allocatedRaw;
  const Size bytes = allocatedSize;
  if (bytes != 3 * sizeof(R) + cookieBytes() || records[2].value != 3 || records[2].self != &records[2]) return 12;
  reseat = &records;
  deleteRecords(records);
  reseat = nullptr;
  if (records || freedRaw != raw || freedSize != bytes || eventCount != 3 || events[0] != 3 || events[1] != 2 || events[2] != 1) return 13;
  before = frees; delete[] records;
  if (frees != before) return 14;
  eventCount = 0; nextValue = 0;
  R (*rows)[2] = new R[3][2];
  raw = allocatedRaw;
  const Size rowBytes = allocatedSize;
  if (rows[2][1].value != 6 || rows[2][1].self != &rows[2][1] || rowBytes != 6 * sizeof(R) + cookieBytes()) return 15;
  delete[] rows;
  if (eventCount != 6 || freedRaw != raw || freedSize != rowBytes) return 16;
  for (int i = 0; i < 6; ++i) if (events[i] != 6 - i) return 17;
  eventCount = 0; before = allocations;
  R *empty = new R[0];
  raw = allocatedRaw;
  if (allocations != before + 1 || allocatedSize != cookieBytes() || eventCount) return 18;
  delete[] empty;
  if (eventCount || freedSize != cookieBytes() || freedRaw != raw) return 19;
  Direct *direct = new Direct[3];
  if (seenArguments != 111 || liveArguments || argumentDrops != 3) return 20;
  delete[] direct;
  seenArguments = argumentDrops = 0;
  Direct *explicitClauses = new Direct[3]{{},{},{}};
  if (seenArguments != 123 || liveArguments || argumentDrops != 3) return 21;
  delete[] explicitClauses;
  seenArguments = argumentDrops = 0;
  Aggregate *aggregates = new Aggregate[3]{};
  if (seenArguments != 123 || liveArguments || argumentDrops != 3) return 22;
  delete[] aggregates;
  seenArguments = argumentDrops = 0;
  Direct *noElements = new Direct[0];
  if (seenArguments || argumentDrops || liveArguments) return 23;
  delete[] noElements;
  Failed *failed = new(Argument()) Failed[3];
  if (failed || initializerCalls || liveArguments || placementSeen != 1 || argumentDrops != 1) return 24;
  eventCount = 0;
  Reference *references = new Reference[2]{{Temporary{1}}, {Temporary{2}}};
  if (eventCount != 2 || events[0] != 2 || events[1] != 1) return 25;
  delete[] references;
  if (eventCount != 2) return 26;
  Storage placement{};
  int *placed = new(placement.bytes, 0) int[3]{10,11,12};
  if (placed[2] != 12 || allocatedSize != 3 * sizeof(int)) return 27;
  delete[] placed;
  if (freedRaw != placement.bytes) return 28;
  eventCount = 0;
  R *rebuilt = new R[2]{R(31), R(32)};
  raw = allocatedRaw;
  rebuilt[0].~R();
  new(Tag{}, &rebuilt[0]) R(33);
  delete[] rebuilt;
  if (eventCount != 3 || events[0] != 31 || events[1] != 32 || events[2] != 33 || freedRaw != raw) return 29;
  return 0;
}
