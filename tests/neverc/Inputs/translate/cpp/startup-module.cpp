int calls;
int seed(int n) { ++calls; return n; }
int first = seed(11);
struct Record { int value; const Record *self; Record(int n) : value(seed(n)), self(this) {} };
const Record object(12);
const int &extended = seed(13);
int local() { static const int value = seed(14); return value; }
int fromLocal = local();
extern "C" int startup_state() {
  return calls == 4 && first == 11 && object.value == 12 &&
      object.self == &object && extended == 13 && fromLocal == 14 && local() == 14;
}
extern "C" int startup_calls() { return calls; }
