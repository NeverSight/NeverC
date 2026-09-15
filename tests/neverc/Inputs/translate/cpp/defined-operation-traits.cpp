int constructions;
int copies;
int moves;
int assignments;
int conversions;
int destructions;

struct Source {
  int value;
  operator int() const noexcept { ++conversions; return value; }
};
struct Value {
  int value;
  Value(int n) noexcept : value(n) {
    static_assert(__is_constructible(Value, int));
    ++constructions;
  }
  Value(const Value &other) : value(other.value) { ++copies; }
  Value(Value &&other) : value(other.value) { ++moves; other.value = -1; }
  Value &operator=(int n) noexcept { ++assignments; value = n; return *this; }
  operator int() const { ++conversions; return value; }
  ~Value() { ++destructions; }
};
struct Field { int value; ~Field() { ++destructions; } };
struct Holder {
  Field fields[2];
  Holder() noexcept : fields{{13}, {17}} { ++constructions; }
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

struct ThrowingSource {
  operator int() const noexcept(false) { ++conversions; return 23; }
};
struct ThrowingDestruction {
  ThrowingDestruction() noexcept { ++constructions; }
  ~ThrowingDestruction() noexcept(false) { ++destructions; }
};

int default_calls;
int default_constructions;
int default_destructions;
int defaultValue() { return 30 + ++default_calls; }
struct DefaultToken {
  int value;
  DefaultToken(int n) noexcept : value(n) { ++default_constructions; }
  ~DefaultToken() noexcept { ++default_destructions; }
};
struct Defaults {
  int value;
  Defaults(int n = defaultValue(), const DefaultToken &token = DefaultToken(7)) noexcept
      : value(n + token.value) { ++default_constructions; }
  ~Defaults() { ++default_destructions; }
};
struct QuietDefaults {
  QuietDefaults(int = 7) noexcept { ++default_constructions; }
  ~QuietDefaults() noexcept { ++default_destructions; }
};
struct PlainDestruction { int value; };
struct DeletedDestruction { ~DeletedDestruction() = delete; };
class PrivateDestruction { ~PrivateDestruction() noexcept {} };

extern "C" bool defined_construct() { return __is_constructible(Value, int); }
extern "C" bool defined_copy() { return __is_constructible(Value, const Value&); }
extern "C" bool defined_trivial() { return __is_trivially_constructible(Value, int); }
extern "C" bool defined_assign() { return __is_assignable(Value&, int); }
extern "C" bool defined_scalar_assign() { return __is_trivially_assignable(int&, Source); }
extern "C" bool defined_convert() { return __is_convertible(Value, double); }
extern "C" bool defined_fields() { return __is_constructible(Holder); }
extern "C" bool defined_nothrow() { return __is_nothrow_constructible(Value, int); }
extern "C" bool defined_nothrow_copy() { return __is_nothrow_constructible(Value, const Value&); }
extern "C" bool defined_nothrow_convert() { return __is_nothrow_convertible(Value, int); }
extern "C" bool defined_nothrow_assign() { return __is_nothrow_assignable(Value&, int); }
extern "C" bool defined_nothrow_fields() { return __is_nothrow_constructible(Holder); }
extern "C" bool defined_nothrow_destruction() { return __is_nothrow_constructible(ThrowingDestruction); }
extern "C" bool defined_defaults() { return __is_constructible(Defaults); }
extern "C" bool defined_trivial_defaults() { return __is_trivially_constructible(Defaults); }
extern "C" bool defined_nothrow_defaults() { return __is_nothrow_constructible(Defaults); }
extern "C" bool defined_nothrow_quiet_defaults() { return __is_nothrow_constructible(QuietDefaults); }
extern "C" bool defined_nothrow_explicit_default() { return __is_nothrow_constructible(Defaults, int); }
extern "C" bool defined_destruct_value() { return __is_nothrow_destructible(Value); }
extern "C" bool defined_destruct_array() { return __is_nothrow_destructible(Holder[2]); }
extern "C" bool defined_destruct_throwing() { return __is_nothrow_destructible(ThrowingDestruction); }
extern "C" bool defined_destruct_deleted() { return __is_nothrow_destructible(DeletedDestruction); }
extern "C" bool defined_destruct_private() { return __is_nothrow_destructible(PrivateDestruction); }
extern "C" bool defined_destruct_reference() { return __is_nothrow_destructible(ThrowingDestruction&); }
extern "C" bool defined_destruct_implicit() { return __is_nothrow_destructible(PlainDestruction[2]); }

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
  if (!defined_nothrow() || defined_nothrow_copy() || defined_nothrow_convert() ||
      !defined_nothrow_assign() || !defined_nothrow_fields() || defined_nothrow_destruction() ||
      !__is_nothrow_constructible(Value, Source) || __is_nothrow_constructible(Value, ThrowingSource) ||
      !__is_nothrow_assignable(double&, Source) || conversions != 4) return 13;
  {
    ThrowingDestruction value;
    if (constructions != 4 || destructions != 7) return 14;
  }
  if (destructions != 8 || constructions != 4 || conversions != 4) return 15;
  if (!defined_defaults() || defined_trivial_defaults() ||
      default_calls || default_constructions || default_destructions) return 16;
  {
    Defaults first;
    if (first.value != 38 || default_calls != 1 ||
        default_constructions != 2 || default_destructions != 1) return 17;
    Defaults second(40);
    if (second.value != 47 || default_calls != 1 ||
        default_constructions != 4 || default_destructions != 2) return 18;
    if (!__is_constructible(Defaults, int) || !defined_defaults() ||
        default_calls != 1 || default_constructions != 4 || default_destructions != 2) return 19;
  }
  if (default_destructions != 4) return 20;
  {
    Defaults third;
    if (third.value != 39 || default_calls != 2 ||
        default_constructions != 6 || default_destructions != 5) return 21;
  }
  if (default_destructions != 6 || default_calls != 2) return 22;
  if (defined_nothrow_defaults() || !defined_nothrow_quiet_defaults() ||
      !defined_nothrow_explicit_default() || default_calls != 2 ||
      default_constructions != 6 || default_destructions != 6) return 23;
  { QuietDefaults value; }
  if (default_constructions != 7 || default_destructions != 7 || default_calls != 2) return 24;
  if (!defined_destruct_value() || !defined_destruct_array() || defined_destruct_throwing() ||
      defined_destruct_deleted() || defined_destruct_private() ||
      !defined_destruct_reference() || !defined_destruct_implicit() ||
      constructions != 4 || destructions != 8) return 25;
  {
    Value values[2] = {Value(41), Value(43)};
    if (values[0].value != 41 || values[1].value != 43 || constructions != 6 ||
        destructions != 8 || copies != 1 || moves != 1) return 26;
    if (!__is_nothrow_destructible(decltype(values)) || !defined_destruct_value() ||
        constructions != 6 || destructions != 8) return 27;
  }
  if (constructions != 6 || destructions != 10 || conversions != 4 ||
      default_constructions != 7 || default_destructions != 7 || default_calls != 2) return 28;
  return 0;
}
