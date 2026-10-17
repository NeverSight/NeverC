#define memcpy user_word_copy
#include <functional>
#include <tuple>
#include <utility>
#include <cstddef>
extern "C" void* user_word_copy(void* destination, const void*, std::size_t size) {
  auto* bytes = static_cast<unsigned char*>(destination);
  for (std::size_t index = 0; index < size; ++index)
    bytes[index] = 0;
  return destination;
}
using H = std::hash<long long>;
using W = std::reference_wrapper<H>;
using T = std::tuple<const long long&>;
static_assert(__is_same(decltype(std::invoke(std::declval<H&>(), std::declval<const long long&>())), std::size_t));
static_assert(__is_same(decltype(std::apply(std::declval<H&>(), std::declval<T&>())), std::size_t));
static_assert(__is_same(decltype(std::declval<W&>()(std::declval<const long long&>())), std::size_t));
static_assert(__is_same(decltype(std::invoke(std::declval<W&>(), std::declval<const long long&>())), std::size_t));
static_assert(__is_same(decltype(std::apply(std::declval<W&>(), std::declval<T&>())), std::size_t));
static_assert(noexcept(std::apply(std::declval<H&>(), std::declval<T&>())));
static_assert(noexcept(std::apply(std::declval<W&>(), std::declval<T&>())));
int main() { return 0; }
