#ifndef CSUPPORT_LMEMORY_H
#define CSUPPORT_LMEMORY_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define CSUPPORT_MF_READ 0x1000000
#define CSUPPORT_MF_WRITE 0x2000000
#define CSUPPORT_MF_EXEC 0x4000000
#define CSUPPORT_MF_RWE_MASK 0x7000000

void *csupport_mmap_alloc_mapped(size_t num_bytes, void *near_addr,
                                 size_t near_size, unsigned prot_flags,
                                 size_t *out_size, int *err_out);
int csupport_mmap_release(void *addr, size_t size);
int csupport_mmap_protect(void *addr, size_t size, unsigned flags);
void csupport_invalidate_icache(const void *addr, size_t len);

void *csupport_mmap_alloc(size_t size, int readable, int writable,
                          int executable);
int csupport_mmap_free(void *addr, size_t size);
int csupport_mmap_protect_raw(void *addr, size_t size, int readable,
                              int writable, int executable);

#ifdef __cplusplus
}
#endif
#endif
