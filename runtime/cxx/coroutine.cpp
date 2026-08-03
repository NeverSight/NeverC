// NeverC C++ ABI v1 — coroutine frame helpers (scaffold)
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

extern "C" {

void *__neverc_coro_alloc(size_t size) {
  if (size == 0)
    size = 1;
  void *p = malloc(size);
  if (!p)
    abort();
  return p;
}

void __neverc_coro_dealloc(void *p) { free(p); }

// Placeholder resume/destroy trampolines — real IR uses generated funcs.
void __neverc_coro_resume(void *frame) { (void)frame; }
void __neverc_coro_destroy(void *frame) {
  __neverc_coro_dealloc(frame);
}

} // extern "C"
