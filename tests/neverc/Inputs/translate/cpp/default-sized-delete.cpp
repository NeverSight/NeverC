using Size = decltype(sizeof(0));
struct Storage { unsigned long long alignment; unsigned char bytes[512]; };
Storage slots[12]{};
int used = 0, scalarFrees = 0, arrayFrees = 0, classFrees = 0;
int events[16]{}, eventCount = 0, releasedAfter = 0, operandCalls = 0;
Size allocatedSize = 0;
void *allocatedRaw = nullptr, *freedRaw = nullptr;
void *take(Size size) {
  allocatedSize = size;
  allocatedRaw = slots[used++].bytes;
  return allocatedRaw;
}
void *operator new(Size size) { return take(size); }
void *operator new[](Size size) { return take(size); }
void operator delete(void *pointer) noexcept {
  ++scalarFrees; freedRaw = pointer; releasedAfter = eventCount;
}
void operator delete[](void *pointer) noexcept {
  ++arrayFrees; freedRaw = pointer; releasedAfter = eventCount;
}
struct R;
R **reseat = nullptr;
struct R {
  int value;
  R *self;
  R(int n = 0) : value(n), self(this) {}
  ~R() { events[eventCount++] = value; if (reseat) *reseat = nullptr; }
};
// Even without a destructor, this class's sized usual delete[] determines the
// Itanium cookie used by new[]. Global-qualified delete[] must retain that
// layout while forwarding its implicit global sized call to the unsized body.
struct Cookie {
  int value;
  static void operator delete[](void *pointer, Size) noexcept {
    ++classFrees; freedRaw = pointer;
  }
};
R *operand(R *pointer) { ++operandCalls; return pointer; }
extern "C" void release_scalar(R *pointer) { delete operand(pointer); }
extern "C" void release_array(R *pointer) { delete[] operand(pointer); }
extern "C" void release_trivial(Cookie *pointer) { ::delete[] pointer; }
Size recordCookie() {
#if defined(__APPLE__) && defined(__aarch64__)
  return 2 * sizeof(Size);
#else
  return sizeof(Size) > alignof(R) ? sizeof(Size) : alignof(R);
#endif
}
Size trivialCookie() {
#if defined(_MSC_VER)
  return 0;
#elif defined(__APPLE__) && defined(__aarch64__)
  return 2 * sizeof(Size);
#else
  return sizeof(Size);
#endif
}
extern "C" int default_sized_delete_check() {
  R *single = new R(7);
  void *raw = allocatedRaw;
  if (single->self != single || allocatedSize != sizeof(R)) return 1;
  reseat = &single;
  release_scalar(single);
  reseat = nullptr;
  if (single || freedRaw != raw || scalarFrees != 1 || arrayFrees ||
      eventCount != 1 || events[0] != 7 || releasedAfter != 1 || operandCalls != 1) return 2;
  release_scalar(nullptr);
  if (scalarFrees != 1 || eventCount != 1 || operandCalls != 2) return 3;
  R *records = new R[3]{R(1), R(2), R(3)};
  raw = allocatedRaw;
  if (allocatedSize != 3 * sizeof(R) + recordCookie() || records[2].self != &records[2]) return 4;
  eventCount = 0; reseat = &records;
  release_array(records);
  reseat = nullptr;
  if (records || freedRaw != raw || arrayFrees != 1 || scalarFrees != 1 ||
      eventCount != 3 || events[0] != 3 || events[1] != 2 || events[2] != 1 ||
      releasedAfter != 3 || operandCalls != 3) return 5;
  release_array(nullptr);
  if (arrayFrees != 1 || eventCount != 3 || operandCalls != 4) return 6;
  Cookie *trivial = new Cookie[3]{{4}, {5}, {6}};
  raw = allocatedRaw;
  if (allocatedSize != 3 * sizeof(Cookie) + trivialCookie() || trivial[2].value != 6) return 7;
  release_trivial(trivial);
  if (freedRaw != raw || arrayFrees != 2 || classFrees || eventCount != 3) return 8;
  release_trivial(nullptr);
  if (arrayFrees != 2 || classFrees) return 9;
  R *empty = new R[0];
  raw = allocatedRaw;
  if (allocatedSize != recordCookie()) return 10;
  release_array(empty);
  if (freedRaw != raw || arrayFrees != 3 || eventCount != 3 || operandCalls != 5) return 11;
  int *plain = new int(9);
  raw = allocatedRaw;
  if (*plain != 9) return 12;
  delete plain;
  if (freedRaw != raw || scalarFrees != 2 || eventCount != 3) return 13;
  int *values = new int[2]{8,9};
  raw = allocatedRaw;
  if (values[1] != 9 || allocatedSize != 2 * sizeof(int)) return 14;
  delete[] values;
  if (freedRaw != raw || arrayFrees != 4 || scalarFrees != 2 || classFrees) return 15;
  return 0;
}
