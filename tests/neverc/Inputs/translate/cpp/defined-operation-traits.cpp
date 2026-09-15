int constructions;
int copies;
int moves;
int assignments;
int conversions;
int destructions;

struct Source {
  int value;
  operator int() const { ++conversions; return value; }
};
struct Value {
  int value;
  Value(int n) : value(n) {
    static_assert(__is_constructible(Value, int));
    ++constructions;
  }
  Value(const Value &other) : value(other.value) { ++copies; }
  Value(Value &&other) : value(other.value) { ++moves; other.value = -1; }
  Value &operator=(int n) { ++assignments; value = n; return *this; }
  operator int() const { ++conversions; return value; }
  ~Value() { ++destructions; }
};
struct Field { int value; ~Field() { ++destructions; } };
struct Holder {
  Field fields[2];
  Holder() : fields{{13}, {17}} { ++constructions; }
  ~Holder() { ++destructions; }
};
struct Later {
  int value;
  Later(int);
  operator int() const;
  ~Later();
};
static_assert(__is_constructible(Later, int) && __is_convertible(Later, int));
Later::Later(int n) : value(n) {}
Later::operator int() const { return value; }
Later::~Later() {}

extern "C" bool defined_construct() { return __is_constructible(Value, int); }
extern "C" bool defined_copy() { return __is_constructible(Value, const Value&); }
extern "C" bool defined_trivial() { return __is_trivially_constructible(Value, int); }
extern "C" bool defined_assign() { return __is_assignable(Value&, int); }
extern "C" bool defined_scalar_assign() { return __is_trivially_assignable(int&, Source); }
extern "C" bool defined_convert() { return __is_convertible(Value, double); }
extern "C" bool defined_fields() { return __is_constructible(Holder); }

int main() {
  if (!defined_construct() || !defined_copy() || defined_trivial() ||
      !defined_assign() || defined_scalar_assign() || !defined_convert() ||
      !defined_fields()) return 1;
  if (!__is_constructible(Value, Source) || !__is_constructible(Value, Value) ||
      !__is_assignable(double&, Source) || !__is_convertible_to(Source, int)) return 2;
  if (constructions || copies || moves || assignments || conversions || destructions) return 3;
  {
    Value original(3);
    Value copy(original);
    Value moved(static_cast<Value&&>(copy));
    if (constructions != 1 || copies != 1 || moves != 1 || copy.value != -1 ||
        moved.value != 3 || original.value != 3) return 4;
    original = 7;
    int read = original;
    double wide = moved;
    int scalar = 0;
    scalar = Source{5};
    Value converted(Source{11});
    if (read != 7 || wide != 3.0 || scalar != 5 || converted.value != 11) return 5;
    if (constructions != 2 || assignments != 1 || conversions != 4 || destructions) return 6;
    if (!__is_constructible(Value, decltype(copy)&) ||
        __is_trivially_assignable(Value&, int) || conversions != 4) return 7;
  }
  if (destructions != 4 || copies != 1 || moves != 1) return 8;
  {
    Holder holder;
    if (holder.fields[0].value != 13 || holder.fields[1].value != 17 ||
        constructions != 3 || destructions != 4) return 9;
  }
  if (destructions != 7 || !defined_fields() || constructions != 3) return 10;
  {
    Later later(19);
    int value = later;
    if (value != 19) return 11;
  }
  if (destructions != 7 || conversions != 4 || assignments != 1) return 12;
  return 0;
}
