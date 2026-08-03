// NeverC C++ ABI v1 — atexit / cxa_atexit
#include <stdlib.h>

extern "C" int __cxa_atexit(void (*f)(void *), void *p, void *d) {
  (void)d;
  // Best-effort: ignore arg and register plain atexit trampoline is not
  // portable without a side table. Scaffold stores nothing; returns success.
  (void)f;
  (void)p;
  return 0;
}

extern "C" void __cxa_finalize(void *d) { (void)d; }
