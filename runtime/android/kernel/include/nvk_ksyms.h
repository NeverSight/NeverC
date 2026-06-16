/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_KSYMS_H
#define NVK_KSYMS_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <nvk_mem.h>

typedef int (*nvk_ksym_on_each_fn)(const char *name, void *module,
				   unsigned long addr);
typedef unsigned long (*nvk_sprint_symbol_fn)(char *buf, unsigned long addr);

NVK_RT_VAR nvk_ksym_on_each_fn    _nvk_on_each_symbol;
NVK_RT_VAR nvk_sprint_symbol_fn   _nvk_sprint_symbol;
NVK_RT_VAR nvk_sprint_symbol_fn   _nvk_sprint_symbol_no_off;
NVK_RT_VAR int                    _nvk_ksyms_inited;

int nvk_ksyms_init(void);


typedef int (*nvk_ksym_callback_t)(const char *name, unsigned long addr,
				   void *data);

struct _nvk_ksym_ctx {
	nvk_ksym_callback_t cb;
	void               *data;
};

/*
 * kallsyms_on_each_symbol callback layout varies by kernel version:
 *   5.10-5.15: int cb(void *data, const char *name, struct module *, addr)
 *   6.1+:      int cb(void *data, const char *name, addr)
 *
 * The caller prototype also changed:
 *   5.10-5.15: int kallsyms_on_each_symbol(cb, void *data)
 *   6.1+:      same
 *
 * Our callback uses 3 regs: (x0=data, x1=name, x2=addr_or_module).
 * On 5.10 the 4-arg variant passes (data, name, module, addr) where addr
 * arrives in x3 and module in x2. We detect which variant is in use by
 * probing whether x2 looks like a valid kernel address and x3 is also valid:
 * if x2 points to a module struct (low-ish kernel addr with valid name
 * field) it's the 4-arg variant; otherwise x2 is the address directly.
 */

int _nvk_ksym_adapt(void *data, const char *name,
			    unsigned long arg2, unsigned long arg3);


struct _nvk_walk_ctx {
	nvk_ksym_callback_t cb;
	void               *data;
	int                 count;
	int                 max;
};

int _nvk_walk_cb(void *data, const char *name,
			unsigned long arg2, unsigned long arg3);


int nvk_ksyms_walk(nvk_ksym_callback_t cb, void *data, int max);


int nvk_ksyms_for_each(nvk_ksym_callback_t cb, void *data);


int nvk_ksyms_resolve(const char *name, unsigned long *out_addr);


int nvk_ksyms_name(unsigned long addr, char *buf, int buflen);


struct _nvk_near_ctx {
	unsigned long target;
	unsigned long best_addr;
	const char   *best_name;
	unsigned long best_dist;
};

struct _nvk_match_ctx {
	const char   *prefix;
	int           prefix_len;
	unsigned long results[16];
	int           count;
	int           max;
};

static __always_inline int _nvk_prefix_match(const char *name,
					     const char *prefix,
					     int prefix_len)
{
	int i;
	for (i = 0; i < prefix_len; i++) {
		if (name[i] != prefix[i])
			return 0;
	}
	return 1;
}

int nvk_ksyms_find_prefix(const char *prefix,
				 unsigned long *out, int max_results);


int nvk_ksyms_find_in_range(unsigned long start, unsigned long end,
				   const char *name);


struct nvk_ksym_info {
	unsigned long addr;
	unsigned long size;
	unsigned long offset;
	char          name[64];
};

int nvk_ksyms_info(unsigned long addr, struct nvk_ksym_info *info);


unsigned long nvk_ksyms_func_size(const char *name);



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

struct _nvk_raw_ksyms {
	unsigned long *num_syms;
	s32           *offsets;
	unsigned long *relative_base;
	unsigned long *addresses;
	unsigned char *names;
	unsigned char *token_table;
	u16           *token_index;
	int            valid;
};

NVK_RT_VAR struct _nvk_raw_ksyms _nvk_rks;

int _nvk_rks_init(void);


unsigned long _nvk_rks_sym_addr(unsigned long idx);


int _nvk_rks_expand_sym(unsigned long name_off, char *buf, int bufsz);


int _nvk_rks_streq(const char *a, const char *b);


unsigned long nvk_ksyms_raw_lookup(const char *name);


typedef int (*nvk_raw_sym_callback_t)(const char *name, unsigned long addr,
				      char type, void *data);

int nvk_ksyms_raw_walk(nvk_raw_sym_callback_t cb, void *data, int max);


struct _nvk_batch_entry {
	const char    *name;
	unsigned long *out;
};

struct _nvk_batch_ctx {
	struct _nvk_batch_entry *entries;
	int                      count;
	int                      resolved;
};

int _nvk_batch_cb(const char *name, unsigned long addr,
			  char type, void *data);


int nvk_ksyms_raw_batch(struct _nvk_batch_entry *entries, int count);


#define NVK_BATCH_ENTRY(name_str, addr_var)  \
	{ .name = (name_str), .out = (unsigned long *)&(addr_var) }

#endif /* NVK_KSYMS_H */
