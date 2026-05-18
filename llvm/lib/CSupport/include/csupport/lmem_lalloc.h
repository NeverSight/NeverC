#ifndef CSUPPORT_LMEM_LALLOC_H
#define CSUPPORT_LMEM_LALLOC_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

void *csupport_allocate_buffer(size_t size, size_t alignment);
void csupport_deallocate_buffer(void *ptr, size_t size, size_t alignment);

#ifdef __cplusplus
}
#endif
#endif
