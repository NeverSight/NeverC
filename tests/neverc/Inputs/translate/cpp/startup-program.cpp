int events[64], eventCount;
int mark(int n) { events[eventCount++] = n; return n; }
int first = mark(1);
const int second = mark(2);
#define BOTH int z = mark(3); int a = mark(4);
BOTH
extern int fixed;
int readsLaterConstant = mark(fixed);
int fixed = 7;
extern int later;
int readsLaterZero = mark(later);
int later = mark(8);
struct Temp { int n; ~Temp() { mark(10); } };
int take(const Temp &t) { return mark(t.n); }
int cleaned = take(Temp{9});
int afterCleanup = mark(11);
struct Record {
  int n;
  const Record *self;
  Record(int value) : n(mark(value)), self(this) {}
};
const Record record(12);
const Record records[2] = {Record(13), Record(14)};
const int &extended = mark(15);
struct Binding { const int &n; };
const Binding binding{mark(16)};
int referent = 42;
struct Ref { int &n; };
struct Forward { static const Ref copy; static const Ref source; };
const Ref Forward::copy = Forward::source;
const Ref Forward::source{referent};
int *selectPointer() { mark(17); return &referent; }
int *const pointer = selectPointer();
int local() { static const int value = mark(18); return value; }
int fromLocal = local();
int identity(int n) { return n; }
using Callback = int (*)(int);
Callback selectCallback() { mark(19); return identity; }
Callback const callback = selectCallback();
using Null = decltype(nullptr);
Null selectNull() { mark(20); return nullptr; }
const Null nullValue = selectNull();
float selectFloat() { mark(21); return 1.5f; }
const float floating = selectFloat();
int unused = mark(22);
using Size = decltype(sizeof(0));
struct Storage { unsigned long long alignment; unsigned char bytes[128]; };
Storage storage{};
void *operator new(Size) { mark(23); return storage.bytes; }
void operator delete(void *) noexcept { mark(25); }
void operator delete(void *p, Size) noexcept { operator delete(p); }
Record *allocated = new Record(24);
int main() {
  int expected[] = {1,2,3,4,7,0,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24};
  if (eventCount != 23) return 1;
  for (int i = 0; i < 23; ++i) if (events[i] != expected[i]) return 2;
  if (first != 1 || second != 2 || z != 3 || a != 4) return 3;
  if (readsLaterConstant != 7 || readsLaterZero != 0 || later != 8) return 4;
  if (cleaned != 9 || afterCleanup != 11) return 5;
  if (record.n != 12 || record.self != &record) return 6;
  if (records[0].self != &records[0] || records[1].self != &records[1] || records[1].n != 14) return 7;
  if (extended != 15 || binding.n != 16 || &extended == &binding.n) return 8;
  if (&Forward::copy.n != &referent || &Forward::source.n != &referent || Forward::copy.n != 42) return 9;
  if (pointer != &referent || fromLocal != 18 || local() != 18 || eventCount != 23) return 10;
  if (callback(7) != 7 || nullValue != nullptr || floating != 1.5f) return 11;
  if (allocated->n != 24 || allocated->self != allocated || eventCount != 23) return 12;
  delete allocated;
  if (eventCount != 24 || events[23] != 25) return 13;
  return 0;
}
