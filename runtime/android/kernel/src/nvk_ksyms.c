/* SPDX-License-Identifier: GPL-2.0 */
/* neverc_krt_ksyms.c — kernel symbol resolution engine. */
#include <nvk.h>
#include "nvk_internal.h"

/* ---- internal types (file-local) ---- */

/* Keep both callback ABIs in the version-neutral embedded runtime bitcode. */
typedef int (*neverc_krt_ksym_iter_with_module_fn)(void *data, const char *name,
					      struct module *module,
					      unsigned long addr);
typedef int (*neverc_krt_ksym_iter_address_only_fn)(void *data, const char *name,
					      unsigned long addr);
typedef int (*neverc_krt_ksym_on_each_with_module_fn)(
	neverc_krt_ksym_iter_with_module_fn callback, void *data);
typedef int (*neverc_krt_ksym_on_each_address_only_fn)(
	neverc_krt_ksym_iter_address_only_fn callback, void *data);
typedef unsigned long (*neverc_krt_sprint_symbol_fn)(char *buf, unsigned long addr);

/* ---- resolver state shared with nvkmod.c ---- */

_neverc_krt_sym_resolver_fn _neverc_krt_sym_resolver = (void *)0;

/* ---- internal variables (file-local) ---- */

#define _NEVERC_KRT_SYM_CACHE_BITS 7
#define _NEVERC_KRT_SYM_CACHE_SIZE (1 << _NEVERC_KRT_SYM_CACHE_BITS)
#define _NEVERC_KRT_SYM_CACHE_MASK (_NEVERC_KRT_SYM_CACHE_SIZE - 1)
#define _NEVERC_KRT_SYM_CACHE_NAME_MAX 128
#define _NEVERC_KRT_KSYM_SYMBOL_MAX 768

struct _neverc_krt_sym_entry {
	u64 sequence;
	u32 length;
	u64 epoch;
	u64 fingerprint;
	unsigned long enc;
	char name[_NEVERC_KRT_SYM_CACHE_NAME_MAX];
};

static struct _neverc_krt_sym_entry
	_neverc_krt_sym_cache[_NEVERC_KRT_SYM_CACHE_SIZE];
static unsigned long _neverc_krt_cache_key;
static u64 _neverc_krt_cache_epoch = 1;
static void                         *_neverc_krt_on_each_symbol;
static neverc_krt_sprint_symbol_fn   _neverc_krt_sprint_symbol;
static neverc_krt_sprint_symbol_fn   _neverc_krt_sprint_symbol_no_off;
static int                           _neverc_krt_ksyms_inited;

/* ---- internal types ---- */

struct _neverc_krt_ksym_ctx {
	neverc_krt_ksym_callback_t cb;
	void               *data;
};

struct _neverc_krt_walk_ctx {
	neverc_krt_ksym_callback_t cb;
	void               *data;
	int                 count;
	int                 max;
};

struct _neverc_krt_raw_ksyms {
	u32           *num_syms;
	s32           *offsets;
	unsigned long *relative_base;
	unsigned long *addresses;
	unsigned char *names;
	unsigned char *token_table;
	u16           *token_index;
	int            valid;
};

struct _neverc_krt_batch_ctx {
	struct neverc_krt_batch_entry *entries;
	int                      count;
	int                      resolved;
};

static struct _neverc_krt_raw_ksyms _neverc_krt_rks;

/* ---- internal helpers ---- */

#ifndef NEVERC_KRT_CACHE_SEED
#  if __has_builtin(__builtin_neverc_random_u64)
#    define NEVERC_KRT_CACHE_SEED ((unsigned long)__builtin_neverc_random_u64())
#  else
#    define _NEVERC_KRT_CT(s, i) ((unsigned long)((unsigned char)(s)[i]))
#    define NEVERC_KRT_CACHE_SEED (                                      \
	(_NEVERC_KRT_CT(__TIME__, 0) << 56) |                            \
	(_NEVERC_KRT_CT(__TIME__, 1) << 48) |                            \
	(_NEVERC_KRT_CT(__TIME__, 3) << 40) |                            \
	(_NEVERC_KRT_CT(__TIME__, 4) << 32) |                            \
	(_NEVERC_KRT_CT(__TIME__, 6) << 24) |                            \
	(_NEVERC_KRT_CT(__TIME__, 7) << 16) |                            \
	(_NEVERC_KRT_CT(__DATE__, 4) <<  8) |                            \
	(_NEVERC_KRT_CT(__DATE__, 5)      ))
#  endif
#endif

static unsigned long _neverc_krt_xor_opaque(unsigned long a, unsigned long b)
{
	unsigned long sum = a + b;
	unsigned long both = a & b;

	return sum - both - both;
}

void _neverc_krt_cache_key_init(void)
{
	unsigned long k;
	unsigned long expected;

	if (__atomic_load_n(&_neverc_krt_cache_key, __ATOMIC_ACQUIRE))
		return;
	k = _neverc_krt_xor_opaque(
		(unsigned long)(void *)_neverc_krt_sym_cache +
			(unsigned long)NEVERC_KRT_CACHE_SEED,
		(unsigned long)(void *)&_neverc_krt_cache_key);
	k |= 1UL;
	expected = 0;
	__atomic_compare_exchange_n(&_neverc_krt_cache_key, &expected, k, 0,
				    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

static unsigned long _neverc_krt_ptr_enc(unsigned long addr)
{
	return _neverc_krt_xor_opaque(
		addr, __atomic_load_n(&_neverc_krt_cache_key, __ATOMIC_RELAXED));
}

static unsigned long _neverc_krt_ptr_dec(unsigned long enc)
{
	return _neverc_krt_xor_opaque(
		enc, __atomic_load_n(&_neverc_krt_cache_key, __ATOMIC_RELAXED));
}

#define _NEVERC_KRT_FNV64_OFFSET 14695981039346656037ULL
#define _NEVERC_KRT_FNV64_PRIME  1099511628211ULL

static u64 _neverc_krt_sym_fingerprint(const char *s, u32 *length)
{
	u64 fingerprint = _NEVERC_KRT_FNV64_OFFSET;
	u32 len = 0;

	while (*s) {
		fingerprint ^= (unsigned char)*s++;
		fingerprint *= _NEVERC_KRT_FNV64_PRIME;
		len++;
	}
	*length = len;
	return fingerprint;
}

static int _neverc_krt_sym_cache_name_matches(
	const struct _neverc_krt_sym_entry *entry, const char *name, u32 length)
{
	u32 i;

	for (i = 0; i <= length; i++)
		if (__atomic_load_n(&entry->name[i], __ATOMIC_RELAXED) != name[i])
			return 0;
	return 1;
}

static void _neverc_krt_sym_cache_publish(
	struct _neverc_krt_sym_entry *entry, const char *name, u64 epoch,
	u32 length, u64 fingerprint, unsigned long enc)
{
	u64 sequence = __atomic_load_n(&entry->sequence, __ATOMIC_ACQUIRE);
	u32 i;

	if ((sequence & 1ULL) ||
	    !__atomic_compare_exchange_n(&entry->sequence, &sequence,
					 sequence + 1ULL, 0,
					 __ATOMIC_ACQ_REL,
					 __ATOMIC_RELAXED))
		return;

	for (i = 0; i <= length; i++)
		__atomic_store_n(&entry->name[i], name[i], __ATOMIC_RELAXED);
	__atomic_store_n(&entry->length, length, __ATOMIC_RELAXED);
	__atomic_store_n(&entry->epoch, epoch, __ATOMIC_RELAXED);
	__atomic_store_n(&entry->fingerprint, fingerprint, __ATOMIC_RELAXED);
	__atomic_store_n(&entry->enc, enc, __ATOMIC_RELAXED);
	__atomic_store_n(&entry->sequence, sequence + 2ULL, __ATOMIC_RELEASE);
}

unsigned long neverc_krt_lookup_name(const char *name)
{
	struct _neverc_krt_sym_entry *entry;
	_neverc_krt_sym_resolver_fn resolver;
	u32 length;
	u32 idx;
	u32 cached_length;
	u64 sequence_before;
	u64 sequence_after;
	u64 epoch;
	u64 cached_epoch;
	u64 fingerprint;
	u64 cached_fingerprint;
	unsigned long enc;
	unsigned long addr;
	int name_matches;

	if (!name)
		return 0;

	_neverc_krt_cache_key_init();
	fingerprint = _neverc_krt_sym_fingerprint(name, &length);
	if (length >= _NEVERC_KRT_SYM_CACHE_NAME_MAX) {
		resolver = READ_ONCE(_neverc_krt_sym_resolver);
		return resolver ? resolver(name) : 0;
	}
	idx = (u32)fingerprint & _NEVERC_KRT_SYM_CACHE_MASK;
	entry = &_neverc_krt_sym_cache[idx];
	epoch = __atomic_load_n(&_neverc_krt_cache_epoch, __ATOMIC_ACQUIRE);

	sequence_before =
		__atomic_load_n(&entry->sequence, __ATOMIC_ACQUIRE);
	if (!(sequence_before & 1ULL)) {
		cached_length =
			__atomic_load_n(&entry->length, __ATOMIC_RELAXED);
		cached_epoch =
			__atomic_load_n(&entry->epoch, __ATOMIC_RELAXED);
		cached_fingerprint =
			__atomic_load_n(&entry->fingerprint, __ATOMIC_RELAXED);
		enc = __atomic_load_n(&entry->enc, __ATOMIC_RELAXED);
		name_matches =
			_neverc_krt_sym_cache_name_matches(entry, name, length);
		__atomic_thread_fence(__ATOMIC_ACQ_REL);
		sequence_after =
			__atomic_load_n(&entry->sequence, __ATOMIC_ACQUIRE);
		if (sequence_before == sequence_after &&
		    cached_epoch == epoch &&
		    cached_length == length &&
		    cached_fingerprint == fingerprint &&
		    name_matches &&
		    enc &&
		    __atomic_load_n(&_neverc_krt_cache_epoch,
				    __ATOMIC_ACQUIRE) == epoch)
			return _neverc_krt_ptr_dec(enc);
	}

	epoch = __atomic_load_n(&_neverc_krt_cache_epoch, __ATOMIC_ACQUIRE);
	resolver = READ_ONCE(_neverc_krt_sym_resolver);
	if (!resolver)
		return 0;

	addr = resolver(name);
	if (addr &&
	    __atomic_load_n(&_neverc_krt_cache_epoch,
			    __ATOMIC_ACQUIRE) == epoch)
		_neverc_krt_sym_cache_publish(
			entry, name, epoch,
			length, fingerprint, _neverc_krt_ptr_enc(addr));
	return addr;
}

static int _neverc_krt_rks_streq(const char *a, const char *b)
{
	while (*a && *b) {
		if (*a != *b) return 0;
		a++; b++;
	}
	return *a == *b;
}

void neverc_krt_sym_cache_clear(void)
{
	/*
	 * Generation invalidation is atomic with concurrent readers and writers.
	 * The encryption key is deterministic for this module, so resetting it
	 * would only create a window where an old entry is decoded with zero.
	 */
	__atomic_add_fetch(&_neverc_krt_cache_epoch, 1, __ATOMIC_ACQ_REL);
}

int neverc_krt_ksyms_init(void)
{
	if (_neverc_krt_ksyms_inited) return 0;

	_neverc_krt_on_each_symbol = NEVERC_KRT_LOOKUP("kallsyms_on_each_symbol");
	_neverc_krt_sprint_symbol =
		(neverc_krt_sprint_symbol_fn)NEVERC_KRT_LOOKUP("sprint_symbol");
	_neverc_krt_sprint_symbol_no_off =
		(neverc_krt_sprint_symbol_fn)NEVERC_KRT_LOOKUP("sprint_symbol_no_offset");

	if (!_neverc_krt_on_each_symbol)
		return -1;
	_neverc_krt_ksyms_inited = 1;
	return 0;
}

static int _neverc_krt_ksym_adapt_common(void *data, const char *name,
					 unsigned long addr)
{
	struct _neverc_krt_ksym_ctx *ctx = (struct _neverc_krt_ksym_ctx *)data;
	if (!ctx || !ctx->cb) return 0;
	return ctx->cb(name, addr, ctx->data);
}

static int _neverc_krt_ksym_adapt_with_module(void *data, const char *name,
					 struct module *module,
					 unsigned long addr)
{
	(void)module;
	return _neverc_krt_ksym_adapt_common(data, name, addr);
}

static int _neverc_krt_ksym_adapt_address_only(void *data, const char *name,
					 unsigned long addr)
{
	return _neverc_krt_ksym_adapt_common(data, name, addr);
}

static int _neverc_krt_walk_cb_common(void *data, const char *name,
				      unsigned long addr)
{
	struct _neverc_krt_walk_ctx *ctx = (struct _neverc_krt_walk_ctx *)data;
	if (!ctx || !ctx->cb) return 0;
	if (ctx->max > 0 && ctx->count >= ctx->max) return 1;
	int ret = ctx->cb(name, addr, ctx->data);
	if (ret >= 0) ctx->count++;
	return ret;
}

static int _neverc_krt_walk_cb_with_module(void *data, const char *name,
				      struct module *module,
				      unsigned long addr)
{
	(void)module;
	return _neverc_krt_walk_cb_common(data, name, addr);
}

static int _neverc_krt_walk_cb_address_only(void *data, const char *name,
				      unsigned long addr)
{
	return _neverc_krt_walk_cb_common(data, name, addr);
}

int neverc_krt_ksyms_walk(neverc_krt_ksym_callback_t cb, void *data, int max)
{
	const struct neverc_krt_runtime_caps *caps;

	if (!cb) return -1;
	if (!_neverc_krt_on_each_symbol) return -1;

	struct _neverc_krt_walk_ctx wctx;
	wctx.cb = cb;
	wctx.data = data;
	wctx.count = 0;
	wctx.max = max;

	caps = _neverc_krt_current_caps();
	if (!caps)
		return -1;
	switch (caps->kallsyms_iter_abi) {
	case NEVERC_KRT_KALLSYMS_ABI_ADDRESS_ONLY: {
		neverc_krt_ksym_on_each_address_only_fn on_each =
			(neverc_krt_ksym_on_each_address_only_fn)
			_neverc_krt_on_each_symbol;
		on_each(_neverc_krt_walk_cb_address_only, &wctx);
		break;
	}
	case NEVERC_KRT_KALLSYMS_ABI_WITH_MODULE: {
		neverc_krt_ksym_on_each_with_module_fn on_each =
			(neverc_krt_ksym_on_each_with_module_fn)
			_neverc_krt_on_each_symbol;
		on_each(_neverc_krt_walk_cb_with_module, &wctx);
		break;
	}
	default:
		return -1;
	}
	return wctx.count;
}

int neverc_krt_ksyms_for_each(neverc_krt_ksym_callback_t cb, void *data)
{
	const struct neverc_krt_runtime_caps *caps;

	if (!cb) return -1;
	if (!_neverc_krt_on_each_symbol) return -1;

	struct _neverc_krt_ksym_ctx ctx;
	ctx.cb = cb;
	ctx.data = data;

	caps = _neverc_krt_current_caps();
	if (!caps)
		return -1;
	switch (caps->kallsyms_iter_abi) {
	case NEVERC_KRT_KALLSYMS_ABI_ADDRESS_ONLY: {
		neverc_krt_ksym_on_each_address_only_fn on_each =
			(neverc_krt_ksym_on_each_address_only_fn)
			_neverc_krt_on_each_symbol;
		return on_each(_neverc_krt_ksym_adapt_address_only, &ctx);
	}
	case NEVERC_KRT_KALLSYMS_ABI_WITH_MODULE: {
		neverc_krt_ksym_on_each_with_module_fn on_each =
			(neverc_krt_ksym_on_each_with_module_fn)
			_neverc_krt_on_each_symbol;
		return on_each(_neverc_krt_ksym_adapt_with_module, &ctx);
	}
	default:
		return -1;
	}
}

int neverc_krt_ksyms_resolve(const char *name, unsigned long *out_addr)
{
	unsigned long addr = kallsyms_lookup_name(name);
	if (!addr) return -1;
	if (out_addr) *out_addr = addr;
	return 0;
}

int neverc_krt_ksyms_name(unsigned long addr, char *buf, int buflen)
{
	char symbol[_NEVERC_KRT_KSYM_SYMBOL_MAX];
	int i;

	if (!buf || buflen <= 0)
		return -1;
	if (_neverc_krt_sprint_symbol_no_off)
		_neverc_krt_sprint_symbol_no_off(symbol, addr);
	else if (_neverc_krt_sprint_symbol)
		_neverc_krt_sprint_symbol(symbol, addr);
	else
		return -1;

	symbol[sizeof(symbol) - 1] = '\0';
	for (i = 0; i < buflen - 1 && symbol[i]; i++)
		buf[i] = symbol[i];
	buf[i] = '\0';
	return 0;
}

int neverc_krt_ksyms_find_prefix(const char *prefix,
				 unsigned long *out, int max_results)
{
	static const char *const suffixes[] = {
		"", "_1", "_2", ".isra.0", ".constprop.0"
	};
	char try_buf[128];
	int plen = 0;
	int count = 0;
	int ns = (int)(sizeof(suffixes) / sizeof(suffixes[0]));
	int s;
	unsigned long addr;
	const char *p;

	if (!prefix || !out || max_results <= 0)
		return 0;

	p = prefix;
	while (*p++) plen++;
	if (plen >= (int)sizeof(try_buf))
		return 0;

	for (s = 0; s < ns && count < max_results; s++) {
		char *d = try_buf;
		char *end = try_buf + sizeof(try_buf) - 1;
		const char *a = prefix;
		while (*a) *d++ = *a++;
		a = suffixes[s];
		while (*a && d < end) *d++ = *a++;
		if (*a)
			continue;
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

int neverc_krt_ksyms_find_in_range(unsigned long start, unsigned long end,
				   const char *name)
{
	unsigned long addr = kallsyms_lookup_name(name);
	if (!addr) return -1;
	return (addr >= start && addr < end) ? 1 : 0;
}

int neverc_krt_ksyms_info(unsigned long addr, struct neverc_krt_ksym_info *info)
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
		_ksize = (ksize_fn)NEVERC_KRT_LOOKUP("kallsyms_lookup_size_offset");

	if (_ksize) {
		_ksize(addr, &info->size, &info->offset);
		info->addr = addr - info->offset;
	}

	neverc_krt_ksyms_name(info->addr, info->name, sizeof(info->name));
	return 0;
}

unsigned long neverc_krt_ksyms_func_size(const char *name)
{
	unsigned long addr = kallsyms_lookup_name(name);
	if (!addr) return 0;

	struct neverc_krt_ksym_info info;
	neverc_krt_ksyms_info(addr, &info);
	return info.size;
}

static int _neverc_krt_rks_init(void)
{
	if (_neverc_krt_rks.valid) return 0;

	_neverc_krt_rks.num_syms =
		(u32 *)NEVERC_KRT_LOOKUP("kallsyms_num_syms");
	if (!_neverc_krt_rks.num_syms) return -1;

	_neverc_krt_rks.offsets =
		(s32 *)NEVERC_KRT_LOOKUP("kallsyms_offsets");
	_neverc_krt_rks.relative_base =
		(unsigned long *)NEVERC_KRT_LOOKUP("kallsyms_relative_base");
	_neverc_krt_rks.addresses =
		(unsigned long *)NEVERC_KRT_LOOKUP("kallsyms_addresses");

	if (!_neverc_krt_rks.offsets && !_neverc_krt_rks.addresses)
		return -2;

	_neverc_krt_rks.names =
		(unsigned char *)NEVERC_KRT_LOOKUP("kallsyms_names");
	_neverc_krt_rks.token_table =
		(unsigned char *)NEVERC_KRT_LOOKUP("kallsyms_token_table");
	_neverc_krt_rks.token_index =
		(u16 *)NEVERC_KRT_LOOKUP("kallsyms_token_index");

	if (!_neverc_krt_rks.names || !_neverc_krt_rks.token_table ||
	    !_neverc_krt_rks.token_index)
		return -3;

	_neverc_krt_rks.valid = 1;
	return 0;
}

static unsigned long _neverc_krt_rks_sym_addr(unsigned long idx)
{
	if (_neverc_krt_rks.offsets && _neverc_krt_rks.relative_base) {
		s32 off;
		if (neverc_krt_mem_read(&off, &_neverc_krt_rks.offsets[idx], 4))
			return 0;
		unsigned long base;
		if (neverc_krt_mem_read(&base, _neverc_krt_rks.relative_base, 8))
			return 0;
		/* arm64 GKI uses unsigned base-relative 32-bit offsets. */
		return base + (unsigned long)(u32)off;
	}
	if (_neverc_krt_rks.addresses) {
		unsigned long addr;
		if (neverc_krt_mem_read(&addr, &_neverc_krt_rks.addresses[idx], 8))
			return 0;
		return addr;
	}
	return 0;
}

static int _neverc_krt_rks_name_header(unsigned long name_off,
				       unsigned int *length,
				       unsigned int *header_size)
{
	unsigned char first;

	if (neverc_krt_mem_read(
		    &first, _neverc_krt_rks.names + name_off, sizeof(first)))
		return -1;
	if (!(first & 0x80)) {
		*length = first;
		*header_size = 1;
		return 0;
	}

	unsigned char second;
	if (neverc_krt_mem_read(
		    &second, _neverc_krt_rks.names + name_off + 1,
		    sizeof(second)))
		return -1;
	*length = (unsigned int)(first & 0x7f) |
		  ((unsigned int)second << 7);
	*header_size = 2;
	return 0;
}

static int _neverc_krt_rks_expand_sym(unsigned long name_off, char *buf, int bufsz)
{
	unsigned int length;
	unsigned int header_size;
	unsigned char *src;
	int remaining;
	int out = 0;

	if (!buf || bufsz <= 0 ||
	    _neverc_krt_rks_name_header(name_off, &length, &header_size))
		return -1;
	src = _neverc_krt_rks.names + name_off + header_size;
	remaining = (int)length;

	while (remaining > 0 && out < bufsz - 1) {
		unsigned char tok;
		if (neverc_krt_mem_read(&tok, src, 1))
			return -1;
		src++;
		remaining--;

		u16 tidx;
		if (neverc_krt_mem_read(&tidx, &_neverc_krt_rks.token_index[tok], 2))
			return -1;
		unsigned char *token = _neverc_krt_rks.token_table + tidx;
		unsigned char c;
		if (neverc_krt_mem_read(&c, token, 1)) return -1;
		while (c && out < bufsz - 1) {
			buf[out++] = c;
			token++;
			if (neverc_krt_mem_read(&c, token, 1))
				return -1;
		}
	}
	buf[out] = '\0';
	return out;
}

unsigned long neverc_krt_ksyms_raw_lookup(const char *name)
{
	unsigned long i, off;
	u32 count;
	char sym_buf[_NEVERC_KRT_KSYM_SYMBOL_MAX];

	if (!name) return 0;
	if (_neverc_krt_rks_init()) return 0;

	if (neverc_krt_mem_read(
		    &count, _neverc_krt_rks.num_syms, sizeof(count)))
		return 0;
	if (count > 500000) return 0;

	off = 0;
	for (i = 0; i < count; i++) {
		unsigned int length;
		unsigned int header_size;
		if (_neverc_krt_rks_name_header(
			    off, &length, &header_size))
			return 0;

		int slen = _neverc_krt_rks_expand_sym(off, sym_buf, sizeof(sym_buf));
		if (slen <= 0) {
			off += header_size + length;
			continue;
		}

		/*
		 * First char in expanded name is the symbol type (T/t/D/d/...),
		 * actual name starts at index 1.
		 */
		if (_neverc_krt_rks_streq(&sym_buf[1], name))
			return _neverc_krt_rks_sym_addr(i);

		off += header_size + length;
	}

	return 0;
}

int neverc_krt_ksyms_raw_walk(neverc_krt_raw_sym_callback_t cb, void *data, int max)
{
	unsigned long i, off;
	u32 count;
	char sym_buf[_NEVERC_KRT_KSYM_SYMBOL_MAX];
	int found = 0;

	if (!cb) return -1;
	if (_neverc_krt_rks_init()) return -1;

	if (neverc_krt_mem_read(
		    &count, _neverc_krt_rks.num_syms, sizeof(count)))
		return -1;
	if (count > 500000) return -1;

	off = 0;
	for (i = 0; i < count && (max <= 0 || found < max); i++) {
		unsigned int length;
		unsigned int header_size;
		if (_neverc_krt_rks_name_header(
			    off, &length, &header_size))
			return found;

		int slen = _neverc_krt_rks_expand_sym(off, sym_buf, sizeof(sym_buf));
		if (slen <= 0) {
			off += header_size + length;
			continue;
		}

		char type = sym_buf[0];
		unsigned long addr = _neverc_krt_rks_sym_addr(i);
		if (!addr) {
			off += header_size + length;
			continue;
		}
		found++;
		if (cb(&sym_buf[1], addr, type, data))
			return found;
		off += header_size + length;
	}

	return found;
}

static int _neverc_krt_batch_cb(const char *name, unsigned long addr,
				char type, void *data)
{
	struct _neverc_krt_batch_ctx *ctx = (struct _neverc_krt_batch_ctx *)data;
	int i;
	if (type != 'T' && type != 't' && type != 'D' && type != 'd' &&
	    type != 'B' && type != 'b' && type != 'R' && type != 'r')
		return 0;
	for (i = 0; i < ctx->count; i++) {
		if (!*ctx->entries[i].out &&
		    _neverc_krt_rks_streq(name, ctx->entries[i].name)) {
			*ctx->entries[i].out = addr;
			ctx->resolved++;
			if (ctx->resolved >= ctx->count)
				return 1;
		}
	}
	return 0;
}

int neverc_krt_ksyms_raw_batch(struct neverc_krt_batch_entry *entries, int count)
{
	struct _neverc_krt_batch_ctx ctx;
	int i;

	if (!entries || count <= 0) return -1;

	for (i = 0; i < count; i++) {
		if (!entries[i].name || !entries[i].out)
			return -1;
		*entries[i].out = 0;
	}

	ctx.entries = entries;
	ctx.count = count;
	ctx.resolved = 0;

	neverc_krt_ksyms_raw_walk(_neverc_krt_batch_cb, &ctx, 0);
	return ctx.resolved;
}
