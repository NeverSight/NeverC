#include <tuple>
#include <array>
#include <utility>
template<class Tag> struct Query {
  int value;
  explicit Query(int n) noexcept : value(n) {}
  ~Query() noexcept {}
};
template<class Tag> struct Conversion {
  int value;
  operator int() const noexcept { return value; }
};
template<class Tag> struct Throwing {
  int value;
  explicit Throwing(int n) noexcept(false) : value(n) {}
};
template<class Tag> struct TemplateQuery {
  int value;
  template<class T> explicit TemplateQuery(T n) noexcept : value(int(n)) {}
  ~TemplateQuery() noexcept {}
};
template<class Tag> struct TemplateConversion {
  int value;
  template<class T> operator T() const noexcept { return T(value); }
};
static_assert(sizeof(std::make_from_tuple<Query<int>>(std::declval<std::tuple<int>&>())) == sizeof(Query<int>));
static_assert(noexcept(std::make_from_tuple<Query<int>>(std::declval<std::tuple<int>&>())));
static_assert(noexcept(std::make_from_tuple<int>(std::declval<std::tuple<Conversion<int>>&>())));
static_assert(!noexcept(std::make_from_tuple<Throwing<int>>(std::declval<std::array<int, 1>&>())));
static_assert(sizeof(std::make_from_tuple<int>(std::tuple<>{})) == sizeof(int));
static_assert(noexcept(std::make_from_tuple<TemplateQuery<int>>(std::declval<std::tuple<short>&>())));
static_assert(noexcept(std::make_from_tuple<int>(std::declval<std::tuple<TemplateConversion<int>>&>())));
// Querying a reference binding creates neither a runtime reference nor a
// concrete constructor, conversion or destructor specialization body.
static_assert(noexcept(std::make_from_tuple<const int&>(std::declval<std::tuple<int>&&>())));
int main() { return 0; }
