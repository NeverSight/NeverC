#include <stddef.h>
#include <stdlib.h>

void *native_heap_allocate(size_t);
void *native_heap_zero(size_t, size_t);
void native_heap_release(void *);
void native_heap_fill(void *, size_t);
int native_heap_lifetimes(int);
int native_heap_argument_sequence(void);
#ifdef __NEVERC_MIMALLOC__
int mi_version(void);
#endif

int main(void) {
#ifdef __NEVERC_MIMALLOC__
  if (mi_version() <= 0) return 7;
#endif
  unsigned char *from_cpp = native_heap_allocate(32);
  if (!from_cpp) return 1;
  native_heap_fill(from_cpp, 32);
  for (size_t i = 0; i < 32; ++i) if (from_cpp[i] != i + 17) return 2;
  free(from_cpp);

  unsigned char *from_c = malloc(32);
  if (!from_c) return 3;
  native_heap_fill(from_c, 32);
  for (size_t i = 0; i < 32; ++i) if (from_c[i] != i + 17) return 4;
  native_heap_release(from_c);

  unsigned char *zero = native_heap_zero(4, 8);
  if (!zero) return 5;
  for (size_t i = 0; i < 32; ++i) if (zero[i]) return 6;
  free(zero);
  // Both null and nonnull zero-size results must be accepted by free.
  free(native_heap_allocate(0));
  native_heap_release(calloc(0, 8));
  native_heap_release(NULL);
  int result = native_heap_lifetimes(3);
  if (result) return 10 + result;
  result = native_heap_argument_sequence();
  if (result) return 30 + result;
  return 0;
}
