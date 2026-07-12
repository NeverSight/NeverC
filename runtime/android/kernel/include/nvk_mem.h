/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_MEM_H
#define NEVERC_KRT_MEM_H

#include <linux/types.h>
#include <linux/compiler.h>  /* __user */

int neverc_krt_mem_init(void);

long neverc_krt_mem_read(void *dst, const void *src, size_t len);
long neverc_krt_mem_write(void *dst, const void *src, size_t len);

long neverc_krt_mem_read_user(void *dst, const void __user *src, size_t len);
long neverc_krt_mem_write_user(void __user *dst, const void *src, size_t len);

int neverc_krt_mem_make_rw(unsigned long addr);
int neverc_krt_mem_make_ro(unsigned long addr);
int neverc_krt_mem_write_protected(unsigned long addr, const void *src,
				   size_t len);

void *neverc_krt_mem_scan(const void *start, size_t region_len,
			  const void *pattern, size_t pat_len);
void *neverc_krt_mem_scan_mask(const void *start, size_t region_len,
			       const unsigned char *pattern,
			       const unsigned char *mask, size_t pat_len);

#define neverc_krt_mem_read_val(dst, src) \
	neverc_krt_mem_read((dst), (src), sizeof(*(dst)))

#define neverc_krt_mem_write_val(dst, src) \
	neverc_krt_mem_write((dst), (src), sizeof(*(src)))

u8  neverc_krt_mem_read8(const void *addr);
u16 neverc_krt_mem_read16(const void *addr);
u32 neverc_krt_mem_read32(const void *addr);
u64 neverc_krt_mem_read64(const void *addr);

int neverc_krt_mem_cmp(const void *a, const void *b, size_t len);
int neverc_krt_mem_cmp_ct(const void *a, const void *b, size_t len);

void *neverc_krt_mem_scan_safe(const void *start, size_t region_len,
			       const void *pattern, size_t pat_len);
void *neverc_krt_mem_scan_mask_safe(const void *start, size_t region_len,
				    const unsigned char *pattern,
				    const unsigned char *mask, size_t pat_len);

void neverc_krt_mem_fill(void *dst, unsigned char val, size_t len);
void neverc_krt_mem_zero(void *dst, size_t len);

long neverc_krt_mem_read_cross_page(void *dst, const void *src, size_t len);

/* --- Atomic operations (ARM64 exclusive monitors) --- */

u64 neverc_krt_mem_atomic_read64(const volatile u64 *addr);
void neverc_krt_mem_atomic_write64(volatile u64 *addr, u64 value);
u64 neverc_krt_mem_xchg64(volatile u64 *addr, u64 new_value);
int neverc_krt_mem_cas64(volatile u64 *addr, u64 expected, u64 desired);

#endif /* NEVERC_KRT_MEM_H */
