/* SPDX-License-Identifier: GPL-2.0 */
/* nvk_ksyms.c — implementations extracted from nvk_ksyms.h. */
#include <nvk.h>

int nvk_ksyms_init(void)
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

int _nvk_ksym_adapt(void *data, const char *name,
			    unsigned long arg2, unsigned long arg3)
{
	struct _nvk_ksym_ctx *ctx = (struct _nvk_ksym_ctx *)data;
	if (!ctx || !ctx->cb) return 0;
	unsigned long addr = arg2;
	if (arg2 < 0xFFFF000000000000UL && arg3 >= 0xFFFF000000000000UL)
		addr = arg3;
	return ctx->cb(name, addr, ctx->data);
}

int _nvk_walk_cb(void *data, const char *name,
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

int nvk_ksyms_walk(nvk_ksym_callback_t cb, void *data, int max)
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

int nvk_ksyms_for_each(nvk_ksym_callback_t cb, void *data)
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

int nvk_ksyms_resolve(const char *name, unsigned long *out_addr)
{
	unsigned long addr = kallsyms_lookup_name(name);
	if (!addr) return -1;
	if (out_addr) *out_addr = addr;
	return 0;
}

int nvk_ksyms_name(unsigned long addr, char *buf, int buflen)
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

int nvk_ksyms_find_prefix(const char *prefix,
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

int nvk_ksyms_find_in_range(unsigned long start, unsigned long end,
				   const char *name)
{
	unsigned long addr = kallsyms_lookup_name(name);
	if (!addr) return -1;
	return (addr >= start && addr < end) ? 1 : 0;
}

int nvk_ksyms_info(unsigned long addr, struct nvk_ksym_info *info)
{
	typedef int (*ksize_fn)(unsigned long addr, unsigned long *sz,
				unsigned long *off);
	NVK_RT_VAR ksize_fn _ksize;

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

unsigned long nvk_ksyms_func_size(const char *name)
{
	unsigned long addr = kallsyms_lookup_name(name);
	if (!addr) return 0;

	struct nvk_ksym_info info;
	nvk_ksyms_info(addr, &info);
	return info.size;
}

int _nvk_rks_init(void)
{
	if (_nvk_rks.valid) return 0;

	_nvk_rks.num_syms =
		(unsigned long *)NVK_LOOKUP("kallsyms_num_syms");
	if (!_nvk_rks.num_syms) return -1;

	_nvk_rks.offsets =
		(s32 *)NVK_LOOKUP("kallsyms_offsets");
	_nvk_rks.relative_base =
		(unsigned long *)NVK_LOOKUP("kallsyms_relative_base");
	_nvk_rks.addresses =
		(unsigned long *)NVK_LOOKUP("kallsyms_addresses");

	if (!_nvk_rks.offsets && !_nvk_rks.addresses)
		return -2;

	_nvk_rks.names =
		(unsigned char *)NVK_LOOKUP("kallsyms_names");
	_nvk_rks.token_table =
		(unsigned char *)NVK_LOOKUP("kallsyms_token_table");
	_nvk_rks.token_index =
		(u16 *)NVK_LOOKUP("kallsyms_token_index");

	if (!_nvk_rks.names || !_nvk_rks.token_table ||
	    !_nvk_rks.token_index)
		return -3;

	_nvk_rks.valid = 1;
	return 0;
}

unsigned long _nvk_rks_sym_addr(unsigned long idx)
{
	if (_nvk_rks.offsets && _nvk_rks.relative_base) {
		s32 off;
		if (nvk_mem_read(&off, &_nvk_rks.offsets[idx], 4))
			return 0;
		unsigned long base;
		if (nvk_mem_read(&base, _nvk_rks.relative_base, 8))
			return 0;
		if (off >= 0)
			return base + (unsigned long)off;
		return base - (unsigned long)(-off);
	}
	if (_nvk_rks.addresses) {
		unsigned long addr;
		if (nvk_mem_read(&addr, &_nvk_rks.addresses[idx], 8))
			return 0;
		return addr;
	}
	return 0;
}

int _nvk_rks_expand_sym(unsigned long name_off, char *buf, int bufsz)
{
	unsigned char *src = _nvk_rks.names + name_off;
	unsigned char len_byte;
	if (nvk_mem_read(&len_byte, src, 1))
		return -1;
	int remaining = len_byte;
	src++;
	int out = 0;

	while (remaining > 0 && out < bufsz - 1) {
		unsigned char tok;
		if (nvk_mem_read(&tok, src, 1))
			return -1;
		src++;
		remaining--;

		u16 tidx;
		if (nvk_mem_read(&tidx, &_nvk_rks.token_index[tok], 2))
			return -1;
		unsigned char *token = _nvk_rks.token_table + tidx;
		unsigned char c;
		if (nvk_mem_read(&c, token, 1)) return -1;
		while (c && out < bufsz - 1) {
			buf[out++] = c;
			token++;
			if (nvk_mem_read(&c, token, 1)) break;
		}
	}
	buf[out] = '\0';
	return out;
}

int _nvk_rks_streq(const char *a, const char *b)
{
	while (*a && *b) {
		if (*a != *b) return 0;
		a++; b++;
	}
	return *a == *b;
}

unsigned long nvk_ksyms_raw_lookup(const char *name)
{
	unsigned long i, count, off;
	char sym_buf[128];

	if (!name) return 0;
	if (_nvk_rks_init()) return 0;

	if (nvk_mem_read(&count, _nvk_rks.num_syms, 8))
		return 0;
	if (count > 500000) return 0;

	off = 0;
	for (i = 0; i < count; i++) {
		unsigned char len;
		if (nvk_mem_read(&len, _nvk_rks.names + off, 1))
			return 0;

		int slen = _nvk_rks_expand_sym(off, sym_buf, sizeof(sym_buf));
		if (slen <= 0) { off += 1 + len; continue; }

		/*
		 * First char in expanded name is the symbol type (T/t/D/d/...),
		 * actual name starts at index 1.
		 */
		if (_nvk_rks_streq(&sym_buf[1], name))
			return _nvk_rks_sym_addr(i);

		off += 1 + len;
	}

	return 0;
}

int nvk_ksyms_raw_walk(nvk_raw_sym_callback_t cb, void *data, int max)
{
	unsigned long i, count, off;
	char sym_buf[128];
	int found = 0;

	if (!cb) return -1;
	if (_nvk_rks_init()) return -1;

	if (nvk_mem_read(&count, _nvk_rks.num_syms, 8))
		return -1;
	if (count > 500000) return -1;

	off = 0;
	for (i = 0; i < count && (max <= 0 || found < max); i++) {
		unsigned char len;
		if (nvk_mem_read(&len, _nvk_rks.names + off, 1))
			return found;

		int slen = _nvk_rks_expand_sym(off, sym_buf, sizeof(sym_buf));
		if (slen <= 0) { off += 1 + len; continue; }

		char type = sym_buf[0];
		unsigned long addr = _nvk_rks_sym_addr(i);
		if (addr && cb(&sym_buf[1], addr, type, data))
			return found + 1;
		found++;
		off += 1 + len;
	}

	return found;
}

int _nvk_batch_cb(const char *name, unsigned long addr,
			  char type, void *data)
{
	struct _nvk_batch_ctx *ctx = (struct _nvk_batch_ctx *)data;
	int i;
	if (type != 'T' && type != 't' && type != 'D' && type != 'd' &&
	    type != 'B' && type != 'b' && type != 'R' && type != 'r')
		return 0;
	for (i = 0; i < ctx->count; i++) {
		if (!*ctx->entries[i].out &&
		    _nvk_rks_streq(name, ctx->entries[i].name)) {
			*ctx->entries[i].out = addr;
			ctx->resolved++;
			if (ctx->resolved >= ctx->count)
				return 1;
		}
	}
	return 0;
}

int nvk_ksyms_raw_batch(struct _nvk_batch_entry *entries, int count)
{
	struct _nvk_batch_ctx ctx;
	int i;

	if (!entries || count <= 0) return -1;

	for (i = 0; i < count; i++)
		*entries[i].out = 0;

	ctx.entries = entries;
	ctx.count = count;
	ctx.resolved = 0;

	nvk_ksyms_raw_walk(_nvk_batch_cb, &ctx, 0);
	return ctx.resolved;
}

