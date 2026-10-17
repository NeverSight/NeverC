#include <functional>
#include <tuple>
#include <utility>
#include <cstddef>

std::size_t word_hash(unsigned long long bits) {
  if constexpr (sizeof(std::size_t) == 8)
    return static_cast<std::size_t>(bits);
  unsigned int h = 8;
  for (unsigned int index = 0; index != 2; ++index) {
    unsigned int k = static_cast<unsigned int>(bits >> (index * 32));
    k *= 0x5bd1e995u;
    k ^= k >> 24;
    k *= 0x5bd1e995u;
    h *= 0x5bd1e995u;
    h ^= k;
  }
  h ^= h >> 13;
  h *= 0x5bd1e995u;
  h ^= h >> 15;
  return h;
}

template<class T>
int check_hash(T value, std::size_t expected) {
  std::hash<T> hash;
  auto wrapper = std::ref(hash);
  const T& borrowed = value;
  if (hash(borrowed) != expected || std::invoke(hash, borrowed) != expected ||
      std::apply(hash, std::tie(borrowed)) != expected || wrapper(borrowed) != expected ||
      std::invoke(wrapper, borrowed) != expected ||
      std::apply(wrapper, std::tie(borrowed)) != expected)
    return 1;
  int receivers = 0;
  int arguments = 0;
  if (std::invoke((++receivers, hash), (++arguments, value)) != expected ||
      receivers != 1 || arguments != 1)
    return 2;
  std::apply(hash, std::tie(value));
  return 0;
}

int main() {
  if (check_hash(0.25f, std::size_t(0x3e800000u)) ||
      check_hash(1.5, word_hash(0x3ff8000000000000ULL)) ||
      check_hash(-3LL, word_hash(0xfffffffffffffffdULL)) ||
      check_hash(0x123456789abcdef0ULL, word_hash(0x123456789abcdef0ULL)) ||
      check_hash(0.0f, std::size_t(0)) || check_hash(-0.0f, std::size_t(0)) ||
      check_hash(0.0, std::size_t(0)) || check_hash(-0.0, std::size_t(0)))
    return 1;
  return 0;
}
