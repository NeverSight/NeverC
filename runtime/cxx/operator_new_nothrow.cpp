// NeverC C++ ABI v1 — nothrow new
#include <stddef.h>
#include <stdlib.h>

namespace std {
struct nothrow_t { explicit nothrow_t() = default; };
extern const nothrow_t nothrow;
const nothrow_t nothrow{};
} // namespace std

void *operator new(size_t size, const std::nothrow_t &) noexcept {
  if (size == 0)
    size = 1;
  return malloc(size);
}
void *operator new[](size_t size, const std::nothrow_t &) noexcept {
  return operator new(size, std::nothrow);
}
void operator delete(void *p, const std::nothrow_t &) noexcept { free(p); }
void operator delete[](void *p, const std::nothrow_t &) noexcept {
  operator delete(p, std::nothrow);
}
