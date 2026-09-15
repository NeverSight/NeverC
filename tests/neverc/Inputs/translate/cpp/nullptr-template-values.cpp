using Null = decltype(nullptr);

template<auto Value> auto value() { return Value; }
template<auto Value> int &slot() { static int state = 3; return state; }
template<class T, T Value = nullptr> T default_value() { return Value; }
template<auto... Values> int count() { return sizeof...(Values); }
template<Null... Values> bool all_null() { return (true && ... && (Values == nullptr)); }

template<auto Value> struct Box { inline static int state = 5; };
template<Null Value = nullptr> using NullBox = Box<Value>;
template<auto Value> inline int variable = 7;
template<class T, auto Value> inline int partial_variable = 11;
template<class T> inline int partial_variable<T *, nullptr> = 13;
template<class T, auto Value> struct Partial { int state = 17; };
template<class T> struct Partial<T *, nullptr> { int state = 19; };
template<auto Value> struct Full { int state = 23; };
template<> struct Full<nullptr> { int state = 29; };

template<class T> struct Owner {
  template<T Value = nullptr> T member() { return Value; }
  template<T Value = nullptr> using Alias = Box<Value>;
  template<T Value> struct Nested { T get() { return Value; } };
};

extern "C" Null null_template_value() { return value<nullptr>(); }
extern "C" int *null_template_pointer() { return value<nullptr>(); }
extern "C" int &null_template_slot() { return slot<nullptr>(); }
extern "C" int &same_null_template_slot() { return slot<(sizeof(int), nullptr)>(); }
extern "C" int &zero_template_slot() { return slot<0>(); }
extern "C" int &false_template_slot() { return slot<false>(); }

int main() {
  if (null_template_value() != nullptr || null_template_pointer() != nullptr)
    return 1;
  if (&null_template_slot() != &same_null_template_slot())
    return 2;
  if (&null_template_slot() == &zero_template_slot() ||
      &null_template_slot() == &false_template_slot() ||
      &zero_template_slot() == &false_template_slot())
    return 3;
  null_template_slot() = 31;
  if (same_null_template_slot() != 31 || zero_template_slot() != 3 || false_template_slot() != 3)
    return 4;
  if (default_value<Null>() != nullptr || default_value<Null, nullptr>() != nullptr)
    return 5;
  if (count<nullptr, 0, false, nullptr>() != 4 || count<>() != 0 ||
      !all_null<nullptr, nullptr>() || !all_null<>())
    return 6;
  if (&NullBox<>::state != &Box<nullptr>::state || &Box<nullptr>::state == &Box<0>::state)
    return 7;
  if (&variable<nullptr> != &variable<(sizeof(int), nullptr)> ||
      &variable<nullptr> == &variable<0>)
    return 8;
  if (partial_variable<int *, nullptr> != 13 || partial_variable<int *, 0> != 11 ||
      Partial<int *, nullptr>{}.state != 19 || Partial<int *, 0>{}.state != 17)
    return 9;
  if (Full<nullptr>{}.state != 29 || Full<0>{}.state != 23)
    return 10;
  Owner<Null> owner;
  Owner<Null>::Nested<nullptr> nested;
  if (owner.member() != nullptr || nested.get() != nullptr ||
      &Owner<Null>::Alias<>::state != &Box<nullptr>::state)
    return 11;
  return 0;
}
