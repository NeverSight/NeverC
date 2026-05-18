/*===- Memory.c - Memory management (pure C) --------------------*- C -*-===*/
#include "include/csupport/lmemory.h"
#include "include/csupport/lprocess.h"
#include "include/csupport/valgrind.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Config/config.h"
#include <errno.h>
#include <string.h>

#ifdef LLVM_ON_UNIX
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef __APPLE__
#include <mach/mach.h>
extern void sys_icache_invalidate(const void *Addr, size_t len);
#elif !defined(__Fuchsia__)
extern void __clear_cache(void *, void *);
#endif

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#endif

static int get_posix_prot(unsigned flags) {
  switch (flags & CSUPPORT_MF_RWE_MASK) {
  case CSUPPORT_MF_READ: return PROT_READ;
  case CSUPPORT_MF_WRITE: return PROT_WRITE;
  case CSUPPORT_MF_READ | CSUPPORT_MF_WRITE: return PROT_READ | PROT_WRITE;
  case CSUPPORT_MF_READ | CSUPPORT_MF_EXEC: return PROT_READ | PROT_EXEC;
  case CSUPPORT_MF_READ | CSUPPORT_MF_WRITE | CSUPPORT_MF_EXEC:
    return PROT_READ | PROT_WRITE | PROT_EXEC;
  case CSUPPORT_MF_EXEC:
#if defined(__FreeBSD__)
    return PROT_READ | PROT_EXEC;
#else
    return PROT_EXEC;
#endif
  default: return PROT_NONE;
  }
}

void *csupport_mmap_alloc_mapped(size_t num_bytes, void *near_addr,
                                 size_t near_size, unsigned prot_flags,
                                 size_t *out_size, int *err_out) {
  *err_out = 0;
  if (num_bytes == 0) {
    *out_size = 0;
    return 0;
  }

  int fd;
#if defined(MAP_ANON)
  fd = -1;
#else
  fd = open("/dev/zero", O_RDWR);
  if (fd == -1) { *err_out = errno; *out_size = 0; return 0; }
#endif

  int mm_flags = MAP_PRIVATE;
#if defined(MAP_ANON)
  mm_flags |= MAP_ANON;
#endif
  int prot = get_posix_prot(prot_flags);

#if defined(__NetBSD__) && defined(PROT_MPROTECT)
  prot |= PROT_MPROTECT(PROT_READ | PROT_WRITE | PROT_EXEC);
#endif

  size_t page_size = (size_t)csupport_get_page_size();
  size_t num_pages = (num_bytes + page_size - 1) / page_size;
  size_t alloc_size = page_size * num_pages;

  uintptr_t start = 0;
  if (near_addr)
    start = (uintptr_t)near_addr + near_size;
  if (start && start % page_size)
    start += page_size - start % page_size;

  void *addr = mmap((void *)start, alloc_size, prot, mm_flags, fd, 0);
  if (addr == MAP_FAILED) {
    if (near_addr) {
#if !defined(MAP_ANON)
      close(fd);
#endif
      return csupport_mmap_alloc_mapped(num_bytes, 0, 0, prot_flags,
                                        out_size, err_out);
    }
    *err_out = errno;
#if !defined(MAP_ANON)
    close(fd);
#endif
    *out_size = 0;
    return 0;
  }

#if !defined(MAP_ANON)
  close(fd);
#endif

  *out_size = alloc_size;

  if (prot_flags & CSUPPORT_MF_EXEC) {
    int ec = csupport_mmap_protect(addr, alloc_size, prot_flags);
    if (ec != 0) {
      *err_out = ec;
      *out_size = 0;
      return 0;
    }
  }

  return addr;
}

int csupport_mmap_release(void *addr, size_t size) {
  if (!addr || size == 0)
    return 0;
  if (munmap(addr, size) != 0)
    return errno;
  return 0;
}

int csupport_mmap_protect(void *addr, size_t size, unsigned flags) {
  size_t page_size = (size_t)csupport_get_page_size();
  if (!addr || size == 0)
    return 0;
  if (!flags)
    return EINVAL;

  int prot = get_posix_prot(flags);
  uintptr_t start = ((uintptr_t)addr) & ~(page_size - 1);
  uintptr_t end = ((uintptr_t)addr + size + page_size - 1) & ~(page_size - 1);

  int invalidate = (flags & CSUPPORT_MF_EXEC) != 0;

#if defined(__aarch64__)
  if (invalidate && !(prot & PROT_READ)) {
    int result = mprotect((void *)start, end - start, prot | PROT_READ);
    if (result != 0)
      return errno;
    csupport_invalidate_icache(addr, size);
    invalidate = 0;
  }
#endif

  int result = mprotect((void *)start, end - start, prot);
  if (result != 0)
    return errno;
  if (invalidate)
    csupport_invalidate_icache(addr, size);
  return 0;
}

void csupport_invalidate_icache(const void *addr, size_t len) {
#if defined(__APPLE__)
#if defined(__arm64__)
  sys_icache_invalidate(addr, len);
#endif
#elif defined(__Fuchsia__)
  zx_cache_flush(addr, len, ZX_CACHE_FLUSH_INSN);
#elif defined(__aarch64__) && defined(__GNUC__)
  __clear_cache((char *)addr, (char *)addr + len);
#endif
  csupport_valgrind_discard_translations(addr, len);
}

void *csupport_mmap_alloc(size_t size, int readable, int writable,
                          int executable) {
  int prot = 0;
  if (readable) prot |= PROT_READ;
  if (writable) prot |= PROT_WRITE;
  if (executable) prot |= PROT_EXEC;
  void *p = mmap(0, size, prot, MAP_PRIVATE | MAP_ANON, -1, 0);
  return p == MAP_FAILED ? 0 : p;
}

int csupport_mmap_free(void *addr, size_t size) {
  return munmap(addr, size);
}

int csupport_mmap_protect_raw(void *addr, size_t size, int readable,
                              int writable, int executable) {
  int prot = 0;
  if (readable) prot |= PROT_READ;
  if (writable) prot |= PROT_WRITE;
  if (executable) prot |= PROT_EXEC;
  return mprotect(addr, size, prot);
}

#else /* Windows */

void *csupport_mmap_alloc_mapped(size_t nb, void *na, size_t ns,
                                 unsigned pf, size_t *os, int *eo) {
  (void)nb; (void)na; (void)ns; (void)pf;
  *os = 0; *eo = ENOSYS; return 0;
}
int csupport_mmap_release(void *a, size_t s) { (void)a; (void)s; return 0; }
int csupport_mmap_protect(void *a, size_t s, unsigned f) {
  (void)a; (void)s; (void)f; return 0;
}
void csupport_invalidate_icache(const void *a, size_t l) {
  (void)a; (void)l;
}

void *csupport_mmap_alloc(size_t s, int r, int w, int e) {
  (void)s; (void)r; (void)w; (void)e; return 0;
}
int csupport_mmap_free(void *a, size_t s) { (void)a; (void)s; return -1; }
int csupport_mmap_protect_raw(void *a, size_t s, int r, int w, int e) {
  (void)a; (void)s; (void)r; (void)w; (void)e; return -1;
}

#endif
