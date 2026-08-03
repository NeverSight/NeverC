// NeverC C++ ABI v1 — operator new/delete
#include <stddef.h>
#include <stdlib.h>

void *operator new(size_t size) {
  if (size == 0)
    size = 1;
  void *p = malloc(size);
  if (!p)
    abort();
  return p;
}
void *operator new[](size_t size) { return operator new(size); }

void operator delete(void *p) noexcept { free(p); }
void operator delete[](void *p) noexcept { operator delete(p); }
void operator delete(void *p, size_t) noexcept { operator delete(p); }
void operator delete[](void *p, size_t) noexcept { operator delete[](p); }

void *operator new(size_t size, void *ptr) noexcept {
  (void)size;
  return ptr;
}
void *operator new[](size_t size, void *ptr) noexcept {
  (void)size;
  return ptr;
}
void operator delete(void *, void *) noexcept {}
void operator delete[](void *, void *) noexcept {}

// Itanium-style mangled entry points used by NeverCCXXABI.
extern "C" {
void *_Znwm(size_t size) { return operator new(size); }
void *_Znam(size_t size) { return operator new[](size); }
void _ZdlPv(void *p) { operator delete(p); }
void _ZdaPv(void *p) { operator delete[](p); }
}
