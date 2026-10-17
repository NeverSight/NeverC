#include <string>
#include <string_view>
#include <functional>
#include <type_traits>
#include <utility>
struct ThrowingView {
  operator std::string_view() const noexcept(false) { return {}; }
};
struct QuietView {
  operator std::string_view() const noexcept { return {}; }
};
static_assert(noexcept(std::declval<const std::hash<std::string_view>&>()(std::declval<std::string_view>())));
static_assert(noexcept(std::declval<const std::hash<std::string>&>()(std::declval<const std::string&>())));
static_assert(!noexcept(std::hash<std::string_view>{}(ThrowingView{})));
static_assert(noexcept(std::hash<std::string_view>{}(QuietView{})));
static_assert(std::is_same<decltype(std::hash<std::string_view>{}(std::string_view{})), std::size_t>::value);
static_assert(std::is_same<decltype(std::hash<std::string>{}(std::declval<const std::string&>())), std::size_t>::value);
int main() { return 0; }
