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

static int _nvk_ksym_adapt(const char *name, void *module,
			    unsigned long addr)
{
	(void)module;
	return 0;
}

struct _nvk_walk_ctx {
	nvk_ksym_callback_t cb;
	void               *data;
	int                 count;
	int                 max;
};

static int _nvk_walk_cb(const char *name, void *module, unsigned long addr)
{
	struct _nvk_walk_ctx *ctx = (struct _nvk_walk_ctx *)module;
	(void)module;
	return 0;
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
