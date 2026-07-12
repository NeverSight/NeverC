/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_ADDR_H
#define NEVERC_KRT_ADDR_H

int neverc_krt_addr_init(void);

unsigned long neverc_krt_va_bits(void);
int neverc_krt_page_shift(void);
unsigned long neverc_krt_page_size(void);

unsigned long neverc_krt_virt_to_phys(unsigned long vaddr);

unsigned long neverc_krt_phys_to_virt(unsigned long paddr);

unsigned long neverc_krt_kimage_voffset(void);

unsigned long neverc_krt_kaslr_offset(void);

unsigned long neverc_krt_translate_user(unsigned long uaddr);

struct neverc_krt_pte_info {
	unsigned long pte_val;
	unsigned long phys_addr;
	int           valid;
	int           writable;
	int           executable;
	int           user_accessible;
	int           level;
};

struct neverc_krt_pte_walk_result {
	unsigned long pte_phys;
	unsigned long *pte_virt;
};

int neverc_krt_walk_pgtable(unsigned long vaddr, struct neverc_krt_pte_info *info);

int neverc_krt_walk_pgtable_ex(unsigned long vaddr, struct neverc_krt_pte_info *info,
			       struct neverc_krt_pte_walk_result *result);

unsigned long neverc_krt_read_ttbr0(void);
unsigned long neverc_krt_read_ttbr1(void);

int neverc_krt_pte_set_rw(unsigned long vaddr);
int neverc_krt_pte_set_ro(unsigned long vaddr);
int neverc_krt_pte_set_exec(unsigned long vaddr);
int neverc_krt_pte_set_rw_range(unsigned long start, unsigned long end);
int neverc_krt_pte_set_ro_range(unsigned long start, unsigned long end);

int neverc_krt_linmap_available(void);
unsigned long neverc_krt_linmap_offset(void);

#endif /* NEVERC_KRT_ADDR_H */
