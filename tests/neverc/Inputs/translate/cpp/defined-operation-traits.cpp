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
template<class T> struct TrivialQueryRoot {
  T value;
  TrivialQueryRoot() = default;
  TrivialQueryRoot &operator=(const TrivialQueryRoot&) = default;
};
template<class T> struct ThrowingQueryAssignment {
  T value;
  ThrowingQueryAssignment &operator=(const ThrowingQueryAssignment&) noexcept(false) = default;
};
int root_constructions;
int root_copies;
int root_moves;
int root_assignments;
int root_move_assignments;
int root_destructions;
struct GeneratedRootLeaf {
  int value;
  GeneratedRootLeaf() noexcept : value(3) { ++root_constructions; }
  GeneratedRootLeaf(const GeneratedRootLeaf &other) noexcept : value(other.value) { ++root_copies; }
  GeneratedRootLeaf(GeneratedRootLeaf &&other) noexcept : value(other.value) { ++root_moves; other.value = -1; }
  GeneratedRootLeaf &operator=(const GeneratedRootLeaf &other) noexcept {
    value = other.value; ++root_assignments; return *this;
  }
  GeneratedRootLeaf &operator=(GeneratedRootLeaf &&other) noexcept {
    value = other.value; other.value = -1; ++root_move_assignments; return *this;
  }
  ~GeneratedRootLeaf() noexcept { ++root_destructions; }
};
struct GeneratedQueryRoot {
  GeneratedRootLeaf field;
  GeneratedQueryRoot() = default;
  GeneratedQueryRoot(const GeneratedQueryRoot&) = default;
  GeneratedQueryRoot(GeneratedQueryRoot&&) = default;
  GeneratedQueryRoot &operator=(const GeneratedQueryRoot&) = default;
  GeneratedQueryRoot &operator=(GeneratedQueryRoot&&) = default;
  ~GeneratedQueryRoot() = default;
};
int selected_default_calls;
GeneratedQueryRoot *default_source;
GeneratedQueryRoot *default_target;
ThrowingQueryAssignment<int> *default_throwing_source;
ThrowingQueryAssignment<int> *default_throwing_target;
struct GeneratedDefaultConstruction {
  int value;
  GeneratedDefaultConstruction(const GeneratedQueryRoot &object = GeneratedQueryRoot()) noexcept
      : value(object.field.value) { ++selected_default_calls; }
};
struct GeneratedDefaultCopy {
  int value;
  GeneratedDefaultCopy(const GeneratedQueryRoot &object = GeneratedQueryRoot(*default_source)) noexcept
      : value(object.field.value) { ++selected_default_calls; }
};
struct GeneratedDefaultMove {
  int value;
  GeneratedDefaultMove(const GeneratedQueryRoot &object =
      GeneratedQueryRoot(static_cast<GeneratedQueryRoot&&>(*default_source))) noexcept
      : value(object.field.value) { ++selected_default_calls; }
};
struct GeneratedDefaultAssignment {
  int value;
  GeneratedDefaultAssignment(int n = (*default_target = *default_source, default_target->field.value)) noexcept
      : value(n) { ++selected_default_calls; }
};
struct GeneratedDefaultMoveAssignment {
  int value;
  GeneratedDefaultMoveAssignment(int n =
      (default_target->operator=(static_cast<GeneratedQueryRoot&&>(*default_source)),
       default_target->field.value)) noexcept : value(n) { ++selected_default_calls; }
};
struct GeneratedDefaultTrivial {
  int value;
  GeneratedDefaultTrivial(const TrivialQueryRoot<int> &object = TrivialQueryRoot<int>()) noexcept
      : value(object.value) { ++selected_default_calls; }
};
struct GeneratedDefaultThrowingAssignment {
  int value;
  GeneratedDefaultThrowingAssignment(int n =
      (*default_throwing_target = *default_throwing_source, default_throwing_target->value)) noexcept
      : value(n) { ++selected_default_calls; }
};
int template_default_calls;
int template_default_constructions;
int template_default_destructions;
int template_default_owners;
int templateDefaultValue(int n) noexcept { ++template_default_calls; return n; }
int throwingTemplateDefaultValue(int n) noexcept(false) { ++template_default_calls; return n; }
struct TemplateDefaultToken {
  int value;
  TemplateDefaultToken(int n) noexcept : value(n) { ++template_default_constructions; }
  ~TemplateDefaultToken() noexcept { ++template_default_destructions; }
};
template<class T, int N> struct TemplateDefaultOwner {
  int value;
  TemplateDefaultOwner(T n = T(templateDefaultValue(N)),
      const TemplateDefaultToken &token = TemplateDefaultToken(N + 1)) noexcept
      : value(static_cast<int>(n) + token.value) { ++template_default_owners; }
};
struct TemplateDefaultMember {
  int value;
  template<class T> TemplateDefaultMember(T first, int second = sizeof(T)) noexcept
      : value(second) { ++template_default_owners; }
};
template<class T> struct TemplateThrowingDefault {
  int value;
  TemplateThrowingDefault(T n = T(throwingTemplateDefaultValue(11))) noexcept
      : value(n) { ++template_default_owners; }
};
int owning_signature_destructions;
int owning_signature_order;
struct OwningSignatureTracked {
  int digit;
  ~OwningSignatureTracked() noexcept {
    ++owning_signature_destructions;
    owning_signature_order = owning_signature_order * 10 + digit;
  }
};
template<class T> struct OwningSignatureLeaf {
  T value;
  OwningSignatureTracked tracked;
  ~OwningSignatureLeaf() noexcept = default;
};
template<class T> struct OwningSignatureMiddle {
  OwningSignatureLeaf<T> leaves[2];
  ~OwningSignatureMiddle() = default;
};
template<class T> struct OwningSignatureTree { OwningSignatureMiddle<T> branches[2]; };
template<bool B> struct OwningSignatureFlag { ~OwningSignatureFlag() noexcept(B) = default; };
struct OwningSignatureThrowing { OwningSignatureFlag<false> first; OwningSignatureFlag<true> last; };
template<class T> struct OwningSignatureOverride {
  OwningSignatureFlag<false> field;
  ~OwningSignatureOverride() noexcept = default;
};
template<class T> struct OwningSignatureLazy { T value; ~OwningSignatureLazy() noexcept(T::missing) = default; };
static_assert(sizeof(OwningSignatureLazy<int>) == sizeof(int));
struct OwningSignatureReferences { OwningSignatureLazy<int> *pointer; OwningSignatureLazy<int> &reference; };

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
extern "C" bool defined_defaulted_trivial_construct() { return __is_trivially_constructible(TrivialQueryRoot<int>); }
extern "C" bool defined_defaulted_template_assign() { return __is_nothrow_assignable(TrivialQueryRoot<int>&, const TrivialQueryRoot<int>&); }
extern "C" bool defined_defaulted_throwing_assign() { return __is_nothrow_assignable(ThrowingQueryAssignment<int>&, const ThrowingQueryAssignment<int>&); }
extern "C" bool defined_defaulted_materialized_construct() { return __is_nothrow_constructible(GeneratedQueryRoot); }
extern "C" bool defined_defaulted_materialized_copy() { return __is_nothrow_constructible(GeneratedQueryRoot, const GeneratedQueryRoot&); }
extern "C" bool defined_defaulted_materialized_move() { return __is_nothrow_constructible(GeneratedQueryRoot, GeneratedQueryRoot&&); }
extern "C" bool defined_defaulted_materialized_assign() { return __is_nothrow_assignable(GeneratedQueryRoot&, const GeneratedQueryRoot&); }
extern "C" bool defined_defaulted_materialized_trivial() { return __is_trivially_constructible(GeneratedQueryRoot); }
extern "C" bool defined_defaulted_materialized_move_assign() { return __is_nothrow_assignable(GeneratedQueryRoot&, GeneratedQueryRoot&&); }
extern "C" bool defined_generated_default_construct() { return __is_nothrow_constructible(GeneratedDefaultConstruction); }
extern "C" bool defined_generated_default_copy() { return __is_nothrow_constructible(GeneratedDefaultCopy); }
extern "C" bool defined_generated_default_move() { return __is_nothrow_constructible(GeneratedDefaultMove); }
extern "C" bool defined_generated_default_assign() { return __is_nothrow_constructible(GeneratedDefaultAssignment); }
extern "C" bool defined_generated_default_move_assign() { return __is_nothrow_constructible(GeneratedDefaultMoveAssignment); }
extern "C" bool defined_generated_default_trivial_temporary() { return __is_nothrow_constructible(GeneratedDefaultTrivial); }
extern "C" bool defined_generated_default_throwing_assign() { return __is_nothrow_constructible(GeneratedDefaultThrowingAssignment); }
extern "C" bool defined_generated_default_trivial() { return __is_trivially_constructible(GeneratedDefaultConstruction); }
extern "C" bool defined_template_default_int() { return __is_nothrow_constructible(TemplateDefaultOwner<int, 3>); }
extern "C" bool defined_template_default_bool() { return __is_nothrow_constructible(TemplateDefaultOwner<bool, 7>); }
extern "C" bool defined_template_default_partial() { return __is_nothrow_constructible(TemplateDefaultOwner<int, 3>, int); }
extern "C" bool defined_template_default_trivial() { return __is_trivially_constructible(TemplateDefaultOwner<int, 3>); }
extern "C" bool defined_template_default_member() { return __is_nothrow_constructible(TemplateDefaultMember, int); }
extern "C" bool defined_template_default_throwing() { return __is_nothrow_constructible(TemplateThrowingDefault<int>); }
extern "C" bool defined_template_default_explicit() { return __is_nothrow_constructible(TemplateThrowingDefault<int>, int); }
extern "C" bool defined_owning_signature_tree() { return __is_nothrow_destructible(OwningSignatureTree<int>[2]); }
extern "C" bool defined_owning_signature_throwing() { return __is_nothrow_destructible(OwningSignatureThrowing); }
extern "C" bool defined_owning_signature_override() { return __is_nothrow_destructible(OwningSignatureOverride<int>); }
extern "C" bool defined_owning_signature_references() { return __is_nothrow_destructible(OwningSignatureReferences); }

int inferred_false_destructions;
int inferred_false_owner_destructions;
struct InferredFalseLeaf {
  int value;
  ~InferredFalseLeaf() noexcept(false) { ++inferred_false_destructions; }
};
struct InferredFalseOwner { InferredFalseLeaf fields[2]; };
struct InferredFalseOrdinary {
  InferredFalseOwner field;
  ~InferredFalseOrdinary() { ++inferred_false_owner_destructions; }
};
struct InferredFalseDefaulted {
  InferredFalseOwner field;
  ~InferredFalseDefaulted() = default;
};
extern "C" bool defined_inferred_false_construct() { return __is_constructible(InferredFalseOwner); }
extern "C" bool defined_inferred_false_nothrow() { return __is_nothrow_constructible(InferredFalseOwner); }
extern "C" bool defined_inferred_false_ordinary() { return __is_nothrow_destructible(InferredFalseOrdinary); }
extern "C" bool defined_inferred_false_defaulted() { return __is_nothrow_destructible(InferredFalseDefaulted); }

int query_result_destructions;
struct QueryResultTracked { ~QueryResultTracked() noexcept { ++query_result_destructions; } };
template<class T> struct QueryResultLeaf {
  T value;
  QueryResultTracked tracked;
  ~QueryResultLeaf() = default;
};
template<class T> struct QueryResultOwner { QueryResultLeaf<T> fields[2]; };
extern "C" bool defined_result_signature_construct() { return __is_constructible(QueryResultOwner<int>); }
extern "C" bool defined_result_signature_nothrow() { return __is_nothrow_constructible(QueryResultOwner<int>); }
extern "C" bool defined_result_signature_copy() { return __is_constructible(QueryResultOwner<int>, const QueryResultOwner<int>&); }
extern "C" bool defined_result_signature_trivial() { return __is_trivially_constructible(QueryResultOwner<int>); }

int lazy_class_constructions;
int lazy_class_copies;
int lazy_class_defaults;
int lazyClassDefault(int value) noexcept { ++lazy_class_defaults; return value; }
template<class T> struct LazyClassConstructor {
  T value;
  LazyClassConstructor(T n = T(lazyClassDefault(7))) noexcept : value(n) {
    if constexpr (__is_same(T, int)) T::body();
    else ++lazy_class_constructions;
  }
  LazyClassConstructor(const LazyClassConstructor &other) noexcept : value(other.value) {
    if constexpr (__is_same(T, int)) T::body();
    else ++lazy_class_copies;
  }
};
extern "C" bool defined_lazy_class_default() { return __is_constructible(LazyClassConstructor<int>); }
extern "C" bool defined_lazy_class_nothrow() { return __is_nothrow_constructible(LazyClassConstructor<int>); }
extern "C" bool defined_lazy_class_explicit() { return __is_constructible(LazyClassConstructor<int>, int); }
extern "C" bool defined_lazy_class_trivial() { return __is_trivially_constructible(LazyClassConstructor<int>); }
extern "C" bool defined_lazy_class_copy() { return __is_constructible(LazyClassConstructor<int>, const LazyClassConstructor<int>&); }
extern "C" bool defined_lazy_class_nothrow_copy() { return __is_nothrow_constructible(LazyClassConstructor<int>, const LazyClassConstructor<int>&); }

int lazy_destructor_constructions;
int lazy_destructor_calls;
int lazy_destructor_order;
int lazy_throwing_destructor_calls;
template<class T> struct LazyClassDestructor {
  T value;
  LazyClassDestructor(T n) noexcept : value(n) {
    if constexpr (__is_same(T, int)) T::construct();
    else ++lazy_destructor_constructions;
  }
  ~LazyClassDestructor() noexcept {
    if constexpr (__is_same(T, int)) T::destroy();
    else {
      ++lazy_destructor_calls;
      lazy_destructor_order = lazy_destructor_order * 10 + value;
    }
  }
};
template<class T> struct LazyThrowingDestructor {
  ~LazyThrowingDestructor() noexcept(false) {
    if constexpr (__is_same(T, int)) T::destroy();
    else ++lazy_throwing_destructor_calls;
  }
};
extern "C" bool defined_lazy_destructor_array() { return __is_nothrow_destructible(LazyClassDestructor<int>[2]); }
extern "C" bool defined_lazy_destructor_construct() { return __is_constructible(LazyClassDestructor<int>, int); }
extern "C" bool defined_lazy_destructor_copy() { return __is_constructible(LazyClassDestructor<int>, const LazyClassDestructor<int>&); }
extern "C" bool defined_lazy_destructor_throwing() { return __is_nothrow_destructible(LazyThrowingDestructor<int>); }
extern "C" bool defined_lazy_destructor_reference() { return __is_nothrow_destructible(LazyClassDestructor<int>&); }
extern "C" bool defined_lazy_destructor_trivial() { return __is_trivially_constructible(LazyClassDestructor<int>, int); }

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
  if (!defined_defaulted_trivial_construct() || !defined_defaulted_template_assign() ||
      defined_defaulted_throwing_assign() || !defined_defaulted_materialized_construct() ||
      !defined_defaulted_materialized_copy() || !defined_defaulted_materialized_move() ||
      !defined_defaulted_materialized_assign() || defined_defaulted_materialized_trivial() ||
      root_constructions || root_copies || root_moves || root_assignments ||
      root_move_assignments || root_destructions) return 52;
  {
    GeneratedQueryRoot original;
    GeneratedQueryRoot copy(original);
    GeneratedQueryRoot moved(static_cast<GeneratedQueryRoot&&>(copy));
    if (original.field.value != 3 || copy.field.value != -1 || moved.field.value != 3 ||
        &original.field == &moved.field || root_constructions != 1 || root_copies != 1 ||
        root_moves != 1 || root_destructions) return 53;
    original.field.value = 7;
    moved = original;
    copy = static_cast<GeneratedQueryRoot&&>(moved);
    if (original.field.value != 7 || moved.field.value != -1 || copy.field.value != 7 ||
        root_assignments != 1 || root_move_assignments != 1 || root_destructions) return 54;
    if (!defined_defaulted_materialized_construct() || !defined_defaulted_materialized_assign() ||
        defined_defaulted_materialized_trivial() || root_constructions != 1 || root_copies != 1 ||
        root_moves != 1 || root_assignments != 1 || root_move_assignments != 1 || root_destructions) return 55;
  }
  if (root_destructions != 3 || generated_destructions != 14 || constructions != 6 ||
      destructions != 10 || default_calls != 2 || lazy_signature_calls) return 56;
  TrivialQueryRoot<int> trivialFirst{2}, trivialSecond{7};
  trivialFirst = trivialSecond;
  trivialFirst.value = 11;
  if (trivialFirst.value != 11 || trivialSecond.value != 7 || &trivialFirst == &trivialSecond ||
      !defined_defaulted_template_assign()) return 57;
  if (!defined_defaulted_materialized_move_assign() || !defined_generated_default_construct() ||
      !defined_generated_default_copy() || !defined_generated_default_move() ||
      !defined_generated_default_assign() || !defined_generated_default_move_assign() ||
      !defined_generated_default_trivial_temporary() || defined_generated_default_throwing_assign() ||
      defined_generated_default_trivial() || selected_default_calls || root_constructions != 1 ||
      root_copies != 1 || root_moves != 1 || root_assignments != 1 ||
      root_move_assignments != 1 || root_destructions != 3) return 58;
  {
    GeneratedQueryRoot source, target;
    source.field.value = 11;
    target.field.value = 5;
    default_source = &source;
    default_target = &target;
    GeneratedDefaultConstruction constructed;
    if (constructed.value != 3 || selected_default_calls != 1 || root_constructions != 4 ||
        root_destructions != 4) return 59;
    GeneratedDefaultCopy copied;
    if (copied.value != 11 || source.field.value != 11 || selected_default_calls != 2 ||
        root_copies != 2 || root_destructions != 5) return 60;
    GeneratedDefaultMove moved;
    if (moved.value != 11 || source.field.value != -1 || selected_default_calls != 3 ||
        root_moves != 2 || root_destructions != 6) return 61;
    source.field.value = 17;
    GeneratedDefaultAssignment assigned;
    if (assigned.value != 17 || target.field.value != 17 || source.field.value != 17 ||
        selected_default_calls != 4 || root_assignments != 2) return 62;
    source.field.value = 19;
    GeneratedDefaultMoveAssignment moveAssigned;
    if (moveAssigned.value != 19 || target.field.value != 19 || source.field.value != -1 ||
        selected_default_calls != 5 || root_move_assignments != 2 ||
        &source.field == &target.field) return 63;
    GeneratedDefaultTrivial trivial;
    if (trivial.value != 0 || selected_default_calls != 6 || root_destructions != 6) return 64;
    if (!defined_generated_default_construct() || !defined_generated_default_copy() ||
        !defined_generated_default_move() || !defined_generated_default_assign() ||
        !defined_generated_default_move_assign() || defined_generated_default_trivial() ||
        selected_default_calls != 6 || root_constructions != 4 || root_copies != 2 ||
        root_moves != 2 || root_assignments != 2 || root_move_assignments != 2 ||
        root_destructions != 6 || source.field.value != -1 || target.field.value != 19) return 65;
    default_source = nullptr;
    default_target = nullptr;
  }
  if (root_destructions != 8 || root_constructions != 4 || root_copies != 2 ||
      root_moves != 2 || root_assignments != 2 || root_move_assignments != 2 ||
      selected_default_calls != 6) return 66;
  ThrowingQueryAssignment<int> throwingFirst{5}, throwingSecond{9};
  default_throwing_source = &throwingSecond;
  default_throwing_target = &throwingFirst;
  GeneratedDefaultThrowingAssignment throwing;
  throwingFirst.value = 11;
  if (throwing.value != 9 || throwingFirst.value != 11 || throwingSecond.value != 9 ||
      &throwingFirst == &throwingSecond || selected_default_calls != 7 ||
      defined_generated_default_throwing_assign() || root_destructions != 8 ||
      generated_destructions != 14 || default_calls != 2 || destructions != 10 ||
      lazy_signature_calls) return 67;
  if (!defined_template_default_int() || !defined_template_default_bool() ||
      !defined_template_default_partial() || defined_template_default_trivial() ||
      !defined_template_default_member() || defined_template_default_throwing() ||
      !defined_template_default_explicit() || template_default_calls ||
      template_default_constructions || template_default_destructions ||
      template_default_owners) return 68;
  {
    TemplateDefaultOwner<int, 3> first;
    if (first.value != 7 || template_default_calls != 1 || template_default_constructions != 1 ||
        template_default_destructions != 1 || template_default_owners != 1) return 69;
    TemplateDefaultOwner<bool, 7> second;
    if (second.value != 9 || first.value != 7 || template_default_calls != 2 ||
        template_default_constructions != 2 || template_default_destructions != 2 ||
        template_default_owners != 2) return 70;
    TemplateDefaultOwner<int, 3> partial(10);
    if (partial.value != 14 || template_default_calls != 2 || template_default_constructions != 3 ||
        template_default_destructions != 3 || template_default_owners != 3) return 71;
    TemplateDefaultToken token(20);
    TemplateDefaultOwner<int, 3> explicitArguments(5, token);
    if (explicitArguments.value != 25 || token.value != 20 || template_default_calls != 2 ||
        template_default_constructions != 4 || template_default_destructions != 3 ||
        template_default_owners != 4) return 72;
    TemplateDefaultMember member(1);
    if (member.value != sizeof(int) || template_default_calls != 2 ||
        template_default_owners != 5) return 73;
    TemplateThrowingDefault<int> throwingDefault;
    TemplateThrowingDefault<int> explicitDefault(13);
    if (throwingDefault.value != 11 || explicitDefault.value != 13 ||
        template_default_calls != 3 || template_default_owners != 7 ||
        template_default_constructions != 4 || template_default_destructions != 3) return 74;
    if (!defined_template_default_int() || !defined_template_default_bool() ||
        !defined_template_default_member() || defined_template_default_throwing() ||
        !defined_template_default_explicit() || template_default_calls != 3 ||
        template_default_owners != 7 || template_default_constructions != 4 ||
        template_default_destructions != 3 || first.value != 7 || second.value != 9) return 75;
  }
  if (template_default_calls != 3 || template_default_constructions != 4 ||
      template_default_destructions != 4 || template_default_owners != 7 ||
      root_destructions != 8 || selected_default_calls != 7 || generated_destructions != 14 ||
      default_calls != 2 || destructions != 10 || lazy_signature_calls) return 76;
  if (!defined_owning_signature_tree() || defined_owning_signature_throwing() ||
      !defined_owning_signature_override() || !defined_owning_signature_references() ||
      owning_signature_destructions || owning_signature_order) return 77;
  {
    OwningSignatureTree<unsigned> original{
        { { { {1, {1}}, {2, {2}} } }, { { {3, {3}}, {4, {4}} } } }
    };
    if (original.branches[0].leaves[0].value != 1 ||
        original.branches[1].leaves[1].value != 4 ||
        owning_signature_destructions || owning_signature_order) return 78;
    {
      OwningSignatureTree<unsigned> copy(original);
      copy.branches[0].leaves[0].value = 9;
      if (copy.branches[0].leaves[0].value != 9 || original.branches[0].leaves[0].value != 1 ||
          &copy.branches[0].leaves[0] == &original.branches[0].leaves[0] ||
          owning_signature_destructions || owning_signature_order) return 79;
      if (!defined_owning_signature_tree() || defined_owning_signature_throwing() ||
          !defined_owning_signature_override() || !defined_owning_signature_references() ||
          owning_signature_destructions || owning_signature_order) return 80;
    }
    if (owning_signature_destructions != 4 || owning_signature_order != 4321 ||
        original.branches[0].leaves[0].value != 1) return 81;
  }
  if (owning_signature_destructions != 8 || owning_signature_order != 43214321 ||
      template_default_calls != 3 || template_default_destructions != 4 || root_destructions != 8 ||
      selected_default_calls != 7 || generated_destructions != 14 || destructions != 10) return 82;
  if (!defined_inferred_false_construct() || defined_inferred_false_nothrow() ||
      defined_inferred_false_ordinary() || defined_inferred_false_defaulted() ||
      inferred_false_destructions || inferred_false_owner_destructions) return 83;
  {
    InferredFalseOwner first{{{3}, {5}}};
    InferredFalseOwner second(first);
    first.fields[0].value = 7;
    if (second.fields[0].value != 3 || second.fields[1].value != 5 ||
        &first.fields[0] == &second.fields[0] || inferred_false_destructions) return 84;
  }
  if (inferred_false_destructions != 4 || inferred_false_owner_destructions) return 85;
  {
    InferredFalseOrdinary ordinary{};
    InferredFalseDefaulted defaulted{};
  }
  if (inferred_false_destructions != 8 || inferred_false_owner_destructions != 1) return 86;
  if (!defined_result_signature_construct() || !defined_result_signature_nothrow() ||
      !defined_result_signature_copy() || defined_result_signature_trivial() ||
      query_result_destructions) return 87;
  {
    QueryResultOwner<unsigned> first{};
    first.fields[0].value = 3;
    first.fields[1].value = 5;
    QueryResultOwner<unsigned> second(first);
    first.fields[0].value = 9;
    if (second.fields[0].value != 3 || second.fields[1].value != 5 ||
        &first.fields[0] == &second.fields[0] || query_result_destructions) return 88;
  }
  if (query_result_destructions != 4 || inferred_false_destructions != 8 ||
      inferred_false_owner_destructions != 1) return 89;
  if (!defined_lazy_class_default() || !defined_lazy_class_nothrow() ||
      !defined_lazy_class_explicit() || defined_lazy_class_trivial() ||
      !defined_lazy_class_copy() || !defined_lazy_class_nothrow_copy() ||
      lazy_class_constructions || lazy_class_copies || lazy_class_defaults) return 90;
  {
    LazyClassConstructor<unsigned> first;
    LazyClassConstructor<unsigned> second(11);
    LazyClassConstructor<unsigned> third(first);
    first.value = 13;
    if (third.value != 7 || second.value != 11 || &first.value == &third.value ||
        lazy_class_constructions != 2 || lazy_class_copies != 1 || lazy_class_defaults != 1) return 91;
  }
  if (!defined_lazy_class_default() || !defined_lazy_class_nothrow() ||
      !defined_lazy_class_copy() || defined_lazy_class_trivial() ||
      lazy_class_constructions != 2 || lazy_class_copies != 1 || lazy_class_defaults != 1 ||
      query_result_destructions != 4 || inferred_false_destructions != 8) return 92;
  if (!defined_lazy_destructor_array() || !defined_lazy_destructor_construct() ||
      !defined_lazy_destructor_copy() || defined_lazy_destructor_throwing() ||
      !defined_lazy_destructor_reference() || defined_lazy_destructor_trivial() ||
      lazy_destructor_constructions || lazy_destructor_calls ||
      lazy_destructor_order || lazy_throwing_destructor_calls) return 93;
  {
    LazyClassDestructor<unsigned> first(3);
    LazyClassDestructor<unsigned> second(first);
    first.value = 7;
    if (second.value != 3 || &first.value == &second.value ||
        lazy_destructor_constructions != 1 || lazy_destructor_calls || lazy_destructor_order) return 94;
  }
  if (lazy_destructor_constructions != 1 || lazy_destructor_calls != 2 ||
      lazy_destructor_order != 37 || lazy_throwing_destructor_calls) return 95;
  {
    LazyThrowingDestructor<unsigned> throwing;
  }
  if (!defined_lazy_destructor_array() || !defined_lazy_destructor_construct() ||
      !defined_lazy_destructor_copy() || defined_lazy_destructor_throwing() ||
      lazy_destructor_constructions != 1 || lazy_destructor_calls != 2 ||
      lazy_destructor_order != 37 || lazy_throwing_destructor_calls != 1 ||
      lazy_class_constructions != 2 || lazy_class_copies != 1 || lazy_class_defaults != 1) return 96;
  return 0;
}
