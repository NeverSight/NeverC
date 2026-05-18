/*===- MemAlloc.c - Memory allocation functions -----------------*- C -*-===*/
#include "include/csupport/lmem_lalloc.h"
#include <stdlib.h>
#include <string.h>

void *csupport_allocate_buffer(size_t size, size_t alignment) {
  if (alignment <= sizeof(void *))
    return malloc(size);
#if defined(_WIN32)
  return _aligned_malloc(size, alignment);
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
  if (alignment == 0)
    return NULL;
  size_t padded_size = size;
  size_t rem = size % alignment;
  if (rem != 0) {
    /* C11 aligned_alloc requires size to be a multiple of alignment. */
    if (size > (size_t)-1 - (alignment - rem))
      return NULL;
    padded_size = size + (alignment - rem);
  }
  return aligned_alloc(alignment, padded_size);
#else
  void *ptr = NULL;
  posix_memalign(&ptr, alignment, size);
  return ptr;
#endif
}

void csupport_deallocate_buffer(void *ptr, size_t size, size_t alignment) {
  (void)size;
#if defined(_WIN32)
  /* Must pair with the allocator used in csupport_allocate_buffer: malloc vs
   * _aligned_malloc (see llvm/lib/Support/MemAlloc.cpp operator new/delete). */
  if (alignment <= sizeof(void *))
    free(ptr);
  else
    _aligned_free(ptr);
#else
  (void)alignment;
  free(ptr);
#endif
}
