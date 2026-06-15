/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_KSYMS_H
#define NVK_KSYMS_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <nvk_mem.h>

typedef int (*nvk_ksym_on_each_fn)(const char *name, void *module,
				   unsigned long addr);
typedef unsigned long (*nvk_sprint_symbol_fn)(char *buf, unsigned long addr);

static nvk_ksym_on_each_fn    _nvk_on_each_symbol;
static nvk_sprint_symbol_fn   _nvk_sprint_symbol;
static nvk_sprint_symbol_fn   _nvk_sprint_symbol_no_off;
static int                    _nvk_ksyms_inited;

static int nvk_ksyms_init(void)
{
	if (_nvk_ksyms_inited) return 0;

	_nvk_on_each_symbol =
		(nvk_ksym_on_each_fn)NVK_LOOKUP("kallsyms_on_each_symbol");
	_nvk_sprint_symbol =
		(nvk_sprint_symbol_fn)NVK_LOOKUP("sprint_symbol");
	_nvk_sprint_symbol_no_off =
		(nvk_sprint_symbol_fn)NVK_LOOKUP("sprint_symbol_no_offset");

	_nvk_ksyms_inited = 1;
	return _nvk_on_each_symbol ? 0 : -1;
}

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

static int _nvk_ksym_adapt(void *data, const char *name,
			    unsigned long arg2, unsigned long arg3)
{
	struct _nvk_ksym_ctx *ctx = (struct _nvk_ksym_ctx *)data;
	if (!ctx || !ctx->cb) return 0;
	unsigned long addr = arg2;
	if (arg2 < 0xFFFF000000000000UL && arg3 >= 0xFFFF000000000000UL)
		addr = arg3;
	return ctx->cb(name, addr, ctx->data);
}

struct _nvk_walk_ctx {
	nvk_ksym_callback_t cb;
	void               *data;
	int                 count;
	int                 max;
};

static int _nvk_walk_cb(void *data, const char *name,
			unsigned long arg2, unsigned long arg3)
{
	struct _nvk_walk_ctx *ctx = (struct _nvk_walk_ctx *)data;
	if (!ctx || !ctx->cb) return 0;
	if (ctx->max > 0 && ctx->count >= ctx->max) return 1;
	unsigned long addr = arg2;
	if (arg2 < 0xFFFF000000000000UL && arg3 >= 0xFFFF000000000000UL)
		addr = arg3;
	int ret = ctx->cb(name, addr, ctx->data);
	if (ret >= 0) ctx->count++;
	return ret;
}

static int nvk_ksyms_walk(nvk_ksym_callback_t cb, void *data, int max)
{
	if (!cb) return -1;
	if (!_nvk_on_each_symbol) return -1;

	struct _nvk_walk_ctx wctx;
	wctx.cb = cb;
	wctx.data = data;
	wctx.count = 0;
	wctx.max = max;

	typedef int (*onesym_fn)(void *, void *);
	((onesym_fn)_nvk_on_each_symbol)(
		(void *)_nvk_walk_cb, (void *)&wctx);
	return wctx.count;
}

static int nvk_ksyms_for_each(nvk_ksym_callback_t cb, void *data)
{
	if (!cb) return -1;
	if (!_nvk_on_each_symbol) return -1;

	struct _nvk_ksym_ctx ctx;
	ctx.cb = cb;
	ctx.data = data;

	typedef int (*onesym_fn)(void *, void *);
	return ((onesym_fn)_nvk_on_each_symbol)(
		(void *)_nvk_ksym_adapt, (void *)&ctx);
}

static int nvk_ksyms_resolve(const char *name, unsigned long *out_addr)
{
	unsigned long addr = kallsyms_lookup_name(name);
	if (!addr) return -1;
	if (out_addr) *out_addr = addr;
	return 0;
}

static int nvk_ksyms_name(unsigned long addr, char *buf, int buflen)
{
	if (_nvk_sprint_symbol_no_off && buf && buflen > 0) {
		_nvk_sprint_symbol_no_off(buf, addr);
		return 0;
	}
	if (_nvk_sprint_symbol && buf && buflen > 0) {
		_nvk_sprint_symbol(buf, addr);
		return 0;
	}
	return -1;
}

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

static int nvk_ksyms_find_prefix(const char *prefix,
				 unsigned long *out, int max_results)
{
	if (!prefix || !out || max_results <= 0)
		return 0;

	int plen = 0;
	const char *p = prefix;
	while (*p++) plen++;

	int count = 0;
	unsigned long addr;
	char try_buf[128];

	const char *suffixes[] = {"", "_1", "_2", ".isra.0", ".constprop.0"};
	int ns = 5;
	int s;

	for (s = 0; s < ns && count < max_results; s++) {
		char *d = try_buf;
		const char *a = prefix;
		while (*a) *d++ = *a++;
		a = suffixes[s];
		while (*a) *d++ = *a++;
		*d = '\0';

		addr = kallsyms_lookup_name(try_buf);
		if (addr) {
			int dup = 0, i;
			for (i = 0; i < count; i++) {
				if (out[i] == addr) { dup = 1; break; }
			}
			if (!dup)
				out[count++] = addr;
		}
	}

	return count;
}

static int nvk_ksyms_find_in_range(unsigned long start, unsigned long end,
				   const char *name)
{
	unsigned long addr = kallsyms_lookup_name(name);
	if (!addr) return -1;
	return (addr >= start && addr < end) ? 1 : 0;
}

struct nvk_ksym_info {
	unsigned long addr;
	unsigned long size;
	unsigned long offset;
	char          name[64];
};

static int nvk_ksyms_info(unsigned long addr, struct nvk_ksym_info *info)
{
	typedef int (*ksize_fn)(unsigned long addr, unsigned long *sz,
				unsigned long *off);
	static ksize_fn _ksize;

	if (!info) return -1;

	info->addr = addr;
	info->size = 0;
	info->offset = 0;
	info->name[0] = '\0';

	if (!_ksize)
		_ksize = (ksize_fn)NVK_LOOKUP("kallsyms_lookup_size_offset");

	if (_ksize) {
		_ksize(addr, &info->size, &info->offset);
		info->addr = addr - info->offset;
	}

	nvk_ksyms_name(info->addr, info->name, sizeof(info->name));
	return 0;
}

static unsigned long nvk_ksyms_func_size(const char *name)
{
	unsigned long addr = kallsyms_lookup_name(name);
	if (!addr) return 0;

	struct nvk_ksym_info info;
	nvk_ksyms_info(addr, &info);
	return info.size;
}

#endif /* NVK_KSYMS_H */
