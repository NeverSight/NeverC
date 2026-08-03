// NeverC C++ ABI v1 — static initialization guards
#include <stdint.h>

// Guard word: bit 0 = initialized, bit 1 = initializing (simple single-thread).
extern "C" int __cxa_guard_acquire(uint64_t *guard_object) {
  if ((*guard_object) & 1ull)
    return 0; // already initialized
  if ((*guard_object) & 2ull)
    return 0; // recursive / concurrent — treat as done for scaffold
  *guard_object |= 2ull;
  return 1;
}

extern "C" void __cxa_guard_release(uint64_t *guard_object) {
  *guard_object = 1ull;
}

extern "C" void __cxa_guard_abort(uint64_t *guard_object) {
  *guard_object = 0ull;
}
