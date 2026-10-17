#include <functional>
#include <utility>
#include <cstddef>
using H = std::hash<int*>;
using V = std::hash<const void*>;
using F = int (*)(int) noexcept;
using FH = std::hash<F>;
static_assert(__is_same(decltype(std::declval<const H&>()(static_cast<int*>(nullptr))), std::size_t));
static_assert(__is_same(decltype(std::declval<const V&>()(static_cast<const void*>(nullptr))), std::size_t));
static_assert(__is_same(decltype(std::declval<const FH&>()(static_cast<F>(nullptr))), std::size_t));
static_assert(noexcept(std::declval<const H&>()(static_cast<int*>(nullptr))));
static_assert(noexcept(std::declval<const FH&>()(static_cast<F>(nullptr))));
static_assert(sizeof(std::declval<const V&>()(static_cast<const void*>(nullptr))) == sizeof(std::size_t));
int main() { return 0; }
