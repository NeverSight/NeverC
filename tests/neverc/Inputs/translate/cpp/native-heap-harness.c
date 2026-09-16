#include <stddef.h>
#include <stdlib.h>

void *native_heap_allocate(size_t);
void *native_heap_zero(size_t, size_t);
void *native_heap_resize(void *, size_t);
void native_heap_release(void *);
void native_heap_fill(void *, size_t);
int native_heap_lifetimes(int);
int native_heap_argument_sequence(void);
#ifdef __NEVERC_MIMALLOC__
int mi_version(void);
#endif

int main(void) {
#ifdef __NEVERC_MIMALLOC__
  if (mi_version() <= 0) return 100;
#endif
  unsigned char *from_cpp = native_heap_allocate(32);
  if (!from_cpp) return 1;
  native_heap_fill(from_cpp, 32);
  for (size_t i = 0; i < 32; ++i) if (from_cpp[i] != i + 17) return 2;
  from_cpp = realloc(from_cpp, 64);
  if (!from_cpp) return 3;
  for (size_t i = 0; i < 32; ++i) if (from_cpp[i] != i + 17) return 4;
  free(from_cpp);

  unsigned char *from_c = malloc(32);
  if (!from_c) return 5;
  native_heap_fill(from_c, 32);
  for (size_t i = 0; i < 32; ++i) if (from_c[i] != i + 17) return 6;
  from_c = native_heap_resize(from_c, 64);
  if (!from_c) return 7;
  for (size_t i = 0; i < 32; ++i) if (from_c[i] != i + 17) return 8;
  native_heap_release(from_c);

  unsigned char *zero = native_heap_zero(4, 8);
  if (!zero) return 9;
  for (size_t i = 0; i < 32; ++i) if (zero[i]) return 10;
  free(zero);
  // Both null and nonnull zero-size results must be accepted by free.
  free(native_heap_allocate(0));
  native_heap_release(calloc(0, 8));
  free(native_heap_resize(NULL, 0));
  native_heap_release(NULL);
  int result = native_heap_lifetimes(3);
  if (result) return 20 + result;
  result = native_heap_argument_sequence();
  if (result) return 40 + result;
  return 0;
}
