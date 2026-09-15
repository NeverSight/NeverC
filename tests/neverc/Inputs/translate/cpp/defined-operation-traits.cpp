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
int generated_destructions;
struct GeneratedLeaf { int value; ~GeneratedLeaf() noexcept { ++generated_destructions; } };
struct GeneratedOwner { GeneratedLeaf fields[2]; ~GeneratedOwner() = default; };
struct ImplicitOwner { GeneratedLeaf fields[2]; };
struct ThrowingGenerated { ~ThrowingGenerated() noexcept(false) = default; };
int template_constructions;
int template_copies;
int template_assignments;
int template_conversions;
int template_destructions;
template<class T> struct QueryTemplate {
  T value;
  QueryTemplate(T n) noexcept : value(n) { ++template_constructions; }
  QueryTemplate(const QueryTemplate &other) noexcept : value(other.value) { ++template_copies; }
  QueryTemplate &operator=(T n) noexcept { value = n; ++template_assignments; return *this; }
  operator T() const noexcept { ++template_conversions; return value; }
  ~QueryTemplate() noexcept { ++template_destructions; }
};
struct QueryMemberTemplate {
  int value;
  template<class T> QueryMemberTemplate(T n) noexcept : value(n) { ++template_constructions; }
  template<class T> operator T() const noexcept { ++template_conversions; return T(value); }
  ~QueryMemberTemplate() noexcept { ++template_destructions; }
};
template<class T> struct LazyDecltype {
  T value;
  ~LazyDecltype() noexcept(T::missing) { T::body(); }
};
template<class T> LazyDecltype<T> lazyResult() { return {7}; }
static_assert(sizeof(LazyDecltype<int>) == sizeof(int));
using LazyResult = decltype((lazyResult<int>()));
using CommaResult = decltype((defaultValue(), lazyResult<int>()));
int lazy_signature_calls;
int signatureInput(int = noexcept(PlainDestruction())) noexcept { ++lazy_signature_calls; return 0; }
template<class T> int lazySignature() noexcept(noexcept(signatureInput())) { T::missing(); return 0; }
template<class T> struct LazyFriendTag {
  friend int lazyFriend(LazyFriendTag, int = sizeof(T)) noexcept(noexcept(signatureInput())) {
    T::body();
    return 0;
  }
};
struct SignatureOwner {
  SignatureOwner() noexcept(noexcept(lazySignature<int>()) &&
                            noexcept(lazyFriend(LazyFriendTag<int>{}, 0))) {}
};
using FriendResult = decltype(lazyFriend(LazyFriendTag<int>{}, 0));
struct SourceAssignmentValue {
  int value;
  constexpr SourceAssignmentValue &operator=(const SourceAssignmentValue&) = default;
};
constexpr int sourceAssignment() {
  SourceAssignmentValue first{1}, second{3};
  first = second;
  return first.value;
}
struct SourceGeneratedLeaf { int value; constexpr SourceGeneratedLeaf() : value(5) {} };
struct SourceGeneratedOwner { SourceGeneratedLeaf field; constexpr SourceGeneratedOwner() = default; };
constexpr int sourceConstruction() { SourceGeneratedOwner owner; return owner.field.value; }
using AssignmentExtent = int[sourceAssignment()];
using ConstructionExtent = int[sourceConstruction()];
template<class T> struct InlineSourceAssignment {
  T value;
  constexpr InlineSourceAssignment &operator=(const InlineSourceAssignment&) = default;
};
constexpr int inlineSourceAssignment() {
  InlineSourceAssignment<int> first{1}, second{7};
  first = second;
  return first.value;
}
template<class T> struct InlineSourceConstruction { T field; constexpr InlineSourceConstruction() = default; };
constexpr int inlineSourceConstruction() {
  InlineSourceConstruction<SourceGeneratedLeaf> owner;
  return owner.field.value;
}
template<class T> struct InlineSourceDestruction { T fields[2]; ~InlineSourceDestruction() = default; };
using InlineAssignmentExtent = int[inlineSourceAssignment()];
using InlineConstructionExtent = int[inlineSourceConstruction()];
template<class T> struct ConsumedLazyDestruction {
  T value;
  ~ConsumedLazyDestruction() noexcept(sizeof(T) > 0) = default;
  int unused(int = T::missing) { T::body(); return 0; }
};
template<bool B> struct ConsumedThrowingDestruction { ~ConsumedThrowingDestruction() noexcept(B) = default; };

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
extern "C" bool defined_destruct_generated() { return __is_nothrow_destructible(GeneratedOwner); }
extern "C" bool defined_destruct_generated_array() { return __is_nothrow_destructible(GeneratedOwner[2]); }
extern "C" bool defined_destruct_generated_throwing() { return __is_nothrow_destructible(ThrowingGenerated); }
extern "C" bool defined_destruct_implicit_nontrivial() { return __is_nothrow_destructible(ImplicitOwner); }
extern "C" bool defined_implicit_construct() { return __is_constructible(ImplicitOwner); }
extern "C" bool defined_implicit_copy() { return __is_constructible(ImplicitOwner, const ImplicitOwner&); }
extern "C" bool defined_implicit_nothrow() { return __is_nothrow_constructible(ImplicitOwner); }
extern "C" bool defined_implicit_trivial() { return __is_trivially_constructible(ImplicitOwner); }
extern "C" bool defined_implicit_trivial_throwing() { return __is_nothrow_constructible(ThrowingGenerated); }
extern "C" bool defined_template_construct() { return __is_nothrow_constructible(QueryTemplate<int>, int); }
extern "C" bool defined_template_copy() { return __is_nothrow_constructible(QueryTemplate<int>, const QueryTemplate<int>&); }
extern "C" bool defined_template_assign() { return __is_nothrow_assignable(QueryTemplate<int>&, int); }
extern "C" bool defined_template_convert() { return __is_nothrow_convertible(QueryTemplate<int>, int); }
extern "C" bool defined_template_destruct() { return __is_nothrow_destructible(QueryTemplate<int>); }
extern "C" bool defined_template_trivial() { return __is_trivially_constructible(QueryTemplate<int>, int); }
extern "C" bool defined_member_construct() { return __is_nothrow_constructible(QueryMemberTemplate, int); }
extern "C" bool defined_member_convert() { return __is_nothrow_convertible(QueryMemberTemplate, int); }
extern "C" bool defined_decltype_reference() { return __is_nothrow_destructible(LazyResult&); }
extern "C" bool defined_decltype_pointer() { return __is_constructible(CommaResult*, decltype(nullptr)); }
extern "C" bool defined_decltype_false() { return __is_convertible(CommaResult*, int*); }
extern "C" bool defined_lazy_signature() { return __is_nothrow_constructible(SignatureOwner); }
extern "C" bool defined_lazy_friend_alias() { return __is_constructible(FriendResult); }
extern "C" bool defined_generated_assignment_source() { return __is_constructible(AssignmentExtent*); }
extern "C" bool defined_generated_construction_source() { return __is_constructible(ConstructionExtent*); }
extern "C" bool defined_inline_defaulted_assignment_source() { return __is_constructible(InlineAssignmentExtent*); }
extern "C" bool defined_inline_defaulted_construction_source() { return __is_constructible(InlineConstructionExtent*); }
extern "C" bool defined_inline_defaulted_destruction() { return __is_nothrow_destructible(InlineSourceDestruction<GeneratedLeaf>[2]); }
extern "C" bool defined_consumed_lazy_destruction() { return __is_nothrow_destructible(ConsumedLazyDestruction<int>[2]); }
extern "C" bool defined_consumed_throwing_destruction() { return __is_nothrow_destructible(ConsumedThrowingDestruction<false>); }

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
  if (!defined_destruct_generated() || !defined_destruct_generated_array() ||
      defined_destruct_generated_throwing() || !defined_destruct_implicit_nontrivial() ||
      generated_destructions != 0) return 29;
  {
    GeneratedOwner owners[2] = {
        { {{11}, {13}} },
        { {{17}, {19}} }
    };
    ImplicitOwner implicit = {{{23}, {29}}};
    if (owners[0].fields[1].value != 13 || owners[1].fields[0].value != 17 ||
        implicit.fields[1].value != 29 || generated_destructions != 0) return 30;
    if (!__is_nothrow_destructible(decltype(owners)) || !defined_destruct_implicit_nontrivial() ||
        generated_destructions != 0) return 31;
  }
  if (generated_destructions != 6 || constructions != 6 || destructions != 10) return 32;
  if (!defined_implicit_construct() || !defined_implicit_copy() || !defined_implicit_nothrow() ||
      defined_implicit_trivial() || !defined_implicit_trivial_throwing() ||
      generated_destructions != 6) return 33;
  {
    ImplicitOwner original = {{{31}, {37}}};
    ImplicitOwner copy(original);
    if (copy.fields[0].value != 31 || copy.fields[1].value != 37 ||
        generated_destructions != 6) return 34;
    copy.fields[0].value = 41;
    if (original.fields[0].value != 31 || !defined_implicit_copy() ||
        generated_destructions != 6) return 35;
  }
  if (generated_destructions != 10 || constructions != 6 || destructions != 10) return 36;
  if (!defined_template_construct() || !defined_template_copy() || !defined_template_assign() ||
      !defined_template_convert() || !defined_template_destruct() || defined_template_trivial() ||
      !defined_member_construct() || !defined_member_convert() || template_constructions ||
      template_copies || template_assignments || template_conversions || template_destructions) return 37;
  {
    QueryTemplate<int> original(5);
    QueryTemplate<int> copy(original);
    copy = 7;
    QueryMemberTemplate member(11);
    int total = static_cast<int>(copy) + static_cast<int>(member);
    if (original.value != 5 || copy.value != 7 || total != 18 || template_constructions != 2 ||
        template_copies != 1 || template_assignments != 1 || template_conversions != 2 ||
        template_destructions != 0) return 38;
    if (!defined_template_copy() || !defined_template_destruct() || !defined_member_convert() ||
        template_constructions != 2 || template_copies != 1 || template_assignments != 1 ||
        template_conversions != 2 || template_destructions != 0) return 39;
  }
  if (template_constructions != 2 || template_copies != 1 || template_assignments != 1 ||
      template_conversions != 2 || template_destructions != 3 || generated_destructions != 10 ||
      constructions != 6 || destructions != 10) return 40;
  if (!defined_decltype_reference() || !defined_decltype_pointer() || defined_decltype_false() ||
      default_calls != 2 || default_constructions != 7 || default_destructions != 7 ||
      constructions != 6 || destructions != 10) return 41;
  if (!defined_lazy_signature() || !defined_lazy_friend_alias() || lazy_signature_calls ||
      default_calls != 2 || destructions != 10) return 42;
  if (!defined_generated_assignment_source() || !defined_generated_construction_source() ||
      sourceAssignment() != 3 || sourceConstruction() != 5 || lazy_signature_calls) return 43;
  SourceAssignmentValue first{2}, second{7};
  first = second;
  first.value = 11;
  if (first.value != 11 || second.value != 7 || &first == &second) return 44;
  SourceGeneratedOwner generated;
  if (generated.field.value != 5 || !defined_generated_construction_source() ||
      default_calls != 2 || constructions != 6 || destructions != 10) return 45;
  if (!defined_inline_defaulted_assignment_source() || !defined_inline_defaulted_construction_source() ||
      !defined_inline_defaulted_destruction() || inlineSourceAssignment() != 7 ||
      inlineSourceConstruction() != 5 || generated_destructions != 10) return 46;
  InlineSourceAssignment<int> inlineFirst{2}, inlineSecond{7};
  inlineFirst = inlineSecond;
  inlineFirst.value = 11;
  if (inlineFirst.value != 11 || inlineSecond.value != 7 || &inlineFirst == &inlineSecond) return 47;
  {
    InlineSourceDestruction<GeneratedLeaf> original{{{2}, {3}}};
    InlineSourceDestruction<GeneratedLeaf> copy(original);
    copy.fields[0].value = 9;
    if (original.fields[0].value != 2 || copy.fields[0].value != 9 ||
        &original.fields[0] == &copy.fields[0] || generated_destructions != 10) return 48;
  }
  if (generated_destructions != 14 || default_calls != 2 || constructions != 6 ||
      destructions != 10 || lazy_signature_calls) return 49;
  if (!defined_consumed_lazy_destruction() || defined_consumed_throwing_destruction() ||
      generated_destructions != 14 || default_calls != 2 || destructions != 10) return 50;
  ConsumedLazyDestruction<unsigned> consumed{13};
  consumed.value = 17;
  if (consumed.value != 17 || !defined_consumed_lazy_destruction() ||
      defined_consumed_throwing_destruction() || lazy_signature_calls) return 51;
  return 0;
}
