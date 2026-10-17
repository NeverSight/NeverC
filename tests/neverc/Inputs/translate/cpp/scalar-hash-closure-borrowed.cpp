#include <functional>
#include <tuple>
#include <cstddef>

std::size_t double_hash(double value) {
  return std::invoke(std::hash<double>{}, value);
}
std::size_t integer_hash(const long long& value) {
  std::hash<long long> hash;
  auto wrapper = std::ref(hash);
  return std::apply(wrapper, std::tie(value));
}
