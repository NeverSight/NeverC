/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_KSYMS_H
#define NEVERC_KRT_KSYMS_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <nvk_mem.h>

typedef int (*neverc_krt_ksym_on_each_fn)(const char *name, void *module,
				   unsigned long addr);
typedef unsigned long (*neverc_krt_sprint_symbol_fn)(char *buf, unsigned long addr);

NEVERC_KRT_RT_VAR neverc_krt_ksym_on_each_fn    _neverc_krt_on_each_symbol;
NEVERC_KRT_RT_VAR neverc_krt_sprint_symbol_fn   _neverc_krt_sprint_symbol;
NEVERC_KRT_RT_VAR neverc_krt_sprint_symbol_fn   _neverc_krt_sprint_symbol_no_off;
NEVERC_KRT_RT_VAR int                    _neverc_krt_ksyms_inited;

int neverc_krt_ksyms_init(void);


typedef int (*neverc_krt_ksym_callback_t)(const char *name, unsigned long addr,
				   void *data);

int neverc_krt_ksyms_walk(neverc_krt_ksym_callback_t cb, void *data, int max);


int neverc_krt_ksyms_for_each(neverc_krt_ksym_callback_t cb, void *data);


int neverc_krt_ksyms_resolve(const char *name, unsigned long *out_addr);


int neverc_krt_ksyms_name(unsigned long addr, char *buf, int buflen);


int neverc_krt_ksyms_find_prefix(const char *prefix,
				 unsigned long *out, int max_results);


int neverc_krt_ksyms_find_in_range(unsigned long start, unsigned long end,
				   const char *name);


struct neverc_krt_ksym_info {
	unsigned long addr;
	unsigned long size;
	unsigned long offset;
	char          name[64];
};

int neverc_krt_ksyms_info(unsigned long addr, struct neverc_krt_ksym_info *info);


unsigned long neverc_krt_ksyms_func_size(const char *name);



/*
 * Direct kallsyms binary data parser.
 *
 * Resolves symbols by parsing the kernel's embedded kallsyms tables
 * without calling kallsyms_lookup_name. Works on 5.7+ kernels where
 * kallsyms_lookup_name is not exported.
 *
 * Kernel stores:
 *   kallsyms_num_syms        — number of symbols
 *   kallsyms_offsets[]       — relative offsets (s32) from relative_base
 *   kallsyms_relative_base   — base address for offsets
 *     OR
 *   kallsyms_addresses[]     — absolute addresses (older kernels)
 *   kallsyms_names[]         — compressed symbol names
 *   kallsyms_token_table[]   — decompression tokens
 *   kallsyms_token_index[]   — token indices
 */

unsigned long neverc_krt_ksyms_raw_lookup(const char *name);


typedef int (*neverc_krt_raw_sym_callback_t)(const char *name, unsigned long addr,
				      char type, void *data);

int neverc_krt_ksyms_raw_walk(neverc_krt_raw_sym_callback_t cb, void *data, int max);


struct _neverc_krt_batch_entry {
	const char    *name;
	unsigned long *out;
};

int neverc_krt_ksyms_raw_batch(struct _neverc_krt_batch_entry *entries, int count);


#define NEVERC_KRT_BATCH_ENTRY(name_str, addr_var)  \
	{ .name = (name_str), .out = (unsigned long *)&(addr_var) }

#endif /* NEVERC_KRT_KSYMS_H */
