/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>
#include "nvk_internal.h"
#include "nvk_compat_table.inc"

static unsigned long _neverc_krt_module_size;
int           _neverc_krt_kernel_ver = 0;

static __always_inline const struct neverc_krt_version_entry *
_neverc_krt_lookup_version(int kv)
{
	int i;
	const struct neverc_krt_version_entry *best =
		&_neverc_krt_version_table[0];
	for (i = 0; i < NEVERC_KRT_VERSION_TABLE_LEN; i++) {
		if (_neverc_krt_version_table[i].kv <= kv)
			best = &_neverc_krt_version_table[i];
	}
	return best;
}

static __always_inline unsigned long _neverc_krt_module_size_for(int kv)
{
	return _neverc_krt_lookup_version(kv)->module_size;
}

void _neverc_krt_version_try_detect_from_banner(void)
{
	const char *banner;
	char buf[64];
	const char *p;
	u32 major, minor;
	int kv;

	if (__atomic_load_n(&_neverc_krt_kernel_ver, __ATOMIC_ACQUIRE))
		return;

	banner = (const char *)NEVERC_KRT_LOOKUP("linux_banner");
	if (!banner)
		banner = (const char *)NEVERC_KRT_LOOKUP("linux_proc_banner");
	if (!banner)
		return;

	if (neverc_krt_mem_read(buf, banner, sizeof(buf)) != 0)
		return;
	buf[63] = '\0';

	p = buf;
	while (*p && !(*p >= '0' && *p <= '9'))
		p++;
	major = 0;
	minor = 0;
	while (*p >= '0' && *p <= '9') {
		major = major * 10 + (*p - '0');
		p++;
	}
	if (*p == '.') {
		p++;
		while (*p >= '0' && *p <= '9') {
			minor = minor * 10 + (*p - '0');
			p++;
		}
	}
	kv = (int)(major * 100 + minor);
	if (kv < 510)
		kv = 510;

	__atomic_store_n(&_neverc_krt_module_size,
			 _neverc_krt_module_size_for(kv),
			 __ATOMIC_RELAXED);
	__atomic_store_n(&_neverc_krt_kernel_ver, kv, __ATOMIC_RELEASE);
}

void _neverc_krt_version_setup(int kv)
{
	if (!__atomic_load_n(&_neverc_krt_kernel_ver, __ATOMIC_ACQUIRE)) {
		__atomic_store_n(&_neverc_krt_module_size,
				 _neverc_krt_module_size_for(kv),
				 __ATOMIC_RELAXED);
		__atomic_store_n(&_neverc_krt_kernel_ver,
				 kv, __ATOMIC_RELEASE);
	}
}

unsigned long _neverc_krt_get_module_size(void)
{
	unsigned long sz = __atomic_load_n(&_neverc_krt_module_size,
					   __ATOMIC_RELAXED);
	return sz ? sz : _neverc_krt_version_table[
		NEVERC_KRT_VERSION_TABLE_LEN - 1].module_size;
}

unsigned long _neverc_krt_get_kimage_vaddr_base(void)
{
	int kv = __atomic_load_n(&_neverc_krt_kernel_ver, __ATOMIC_ACQUIRE);

	return _neverc_krt_lookup_version(kv ? kv : 510)->kimage_vaddr;
}

const struct neverc_krt_gki_layout *_neverc_krt_get_gki_layout(void)
{
	int kv = __atomic_load_n(&_neverc_krt_kernel_ver, __ATOMIC_ACQUIRE);

	return &_neverc_krt_lookup_version(kv ? kv : 510)->layout;
}

unsigned long _neverc_krt_get_file_dentry_off(void)
{
	int kv = __atomic_load_n(&_neverc_krt_kernel_ver, __ATOMIC_ACQUIRE);
	return _neverc_krt_lookup_version(kv ? kv : 510)->file_dentry_off;
}

/* ---- internal variables ---- */

static struct neverc_krt_kernel_info _neverc_krt_kinfo;

typedef int (*neverc_krt_vsnprintf_fn)(char *buf, size_t size, const char *fmt,
				      __builtin_va_list ap);
typedef int (*neverc_krt_vsscanf_fn)(const char *buf, const char *fmt,
				     __builtin_va_list ap);
static neverc_krt_vsnprintf_fn _neverc_krt_vsnprintf_ptr;
static neverc_krt_vsscanf_fn   _neverc_krt_vsscanf_ptr;

static unsigned long _neverc_krt_rt_off_init;
static unsigned long _neverc_krt_rt_off_exit;

static int _neverc_krt_compat_inited;

static void _neverc_krt_parse_version(const char *str, struct neverc_krt_kernel_info *info)
{
	const char *p = str;
	u32 parts[3] = {0, 0, 0};
	int pi = 0;

	while (*p && pi < 3) {
		if (*p >= '0' && *p <= '9') {
			parts[pi] = parts[pi] * 10 + (*p - '0');
		} else if (*p == '.') {
			pi++;
		} else {
			break;
		}
		p++;
	}

	info->major = parts[0];
	info->minor = parts[1];
	info->patch = parts[2];

	while (*p && *p != '-') p++;
	if (*p == '-') p++;
	if (p[0] == 'a' && p[1] == 'n' && p[2] == 'd' && p[3] == 'r') {
		while (*p && !(*p >= '0' && *p <= '9')) p++;
		u32 av = 0;
		while (*p >= '0' && *p <= '9') {
			av = av * 10 + (*p - '0');
			p++;
		}
		info->android_version = av;
	}

	info->detected = 1;
}

int neverc_krt_compat_init(void)
{
	const char *banner;

	if (_neverc_krt_compat_inited) return 0;

	neverc_krt_mem_init();

	banner = (const char *)NEVERC_KRT_LOOKUP("linux_banner");
	if (!banner) {
		banner = (const char *)NEVERC_KRT_LOOKUP("linux_proc_banner");
	}

	if (banner) {
		char banbuf[64];
		if (!neverc_krt_mem_read(banbuf, banner, sizeof(banbuf))) {
			banbuf[63] = '\0';
			const char *ver_start = banbuf;
			while (*ver_start && !(*ver_start >= '0' && *ver_start <= '9'))
				ver_start++;
			if (*ver_start)
				_neverc_krt_parse_version(ver_start, &_neverc_krt_kinfo);
		}
	}

	if (!_neverc_krt_kinfo.detected) {
		int kv = __atomic_load_n(&_neverc_krt_kernel_ver,
					__ATOMIC_ACQUIRE);
		const struct neverc_krt_version_entry *ent =
			_neverc_krt_lookup_version(kv ? kv : 510);
		_neverc_krt_kinfo.major = ent->major;
		_neverc_krt_kinfo.minor = ent->minor;
		_neverc_krt_kinfo.android_version = ent->android_version;
		_neverc_krt_kinfo.detected = 1;
	}

	neverc_krt_fmt_init();

	_neverc_krt_compat_inited = 1;
	return 0;
}

int neverc_krt_fmt_init(void)
{
	if (!_neverc_krt_vsnprintf_ptr) {
		_neverc_krt_vsnprintf_ptr =
			(neverc_krt_vsnprintf_fn)NEVERC_KRT_LOOKUP("vsnprintf");
	}
	if (!_neverc_krt_vsscanf_ptr) {
		_neverc_krt_vsscanf_ptr =
			(neverc_krt_vsscanf_fn)NEVERC_KRT_LOOKUP("vsscanf");
	}
	return _neverc_krt_vsnprintf_ptr ? 0 : -1;
}

int neverc_krt_snprintf(char *buf, size_t size, const char *fmt, ...)
{
	if (!_neverc_krt_vsnprintf_ptr) return -1;
	__builtin_va_list ap;
	__builtin_va_start(ap, fmt);
	int ret = _neverc_krt_vsnprintf_ptr(buf, size, fmt, ap);
	__builtin_va_end(ap);
	return ret;
}

int neverc_krt_sscanf(const char *buf, const char *fmt, ...)
{
	if (!_neverc_krt_vsscanf_ptr) return -1;
	__builtin_va_list ap;
	__builtin_va_start(ap, fmt);
	int ret = _neverc_krt_vsscanf_ptr(buf, fmt, ap);
	__builtin_va_end(ap);
	return ret;
}

int neverc_krt_has_cfi(void)
{
	return NEVERC_KRT_LOOKUP("__cfi_check") != (void *)0;
}

int neverc_krt_check_kernel_match(void)
{
	neverc_krt_compat_init();
	if (!_neverc_krt_kinfo.detected)
		return NEVERC_KRT_VER_UNKNOWN;

	int kv = __atomic_load_n(&_neverc_krt_kernel_ver, __ATOMIC_ACQUIRE);
	const struct neverc_krt_version_entry *ent =
		_neverc_krt_lookup_version(kv ? kv : 510);

	if (_neverc_krt_kinfo.major == ent->major &&
	    _neverc_krt_kinfo.minor == ent->minor)
		return NEVERC_KRT_VER_EXACT;

	if (_neverc_krt_kinfo.major == ent->major)
		return NEVERC_KRT_VER_COMPAT;

	return NEVERC_KRT_VER_MISMATCH;
}

int neverc_krt_verify_module_offsets(struct neverc_krt_this_module *mod,
				     const char *expected_name)
{
	const struct neverc_krt_gki_layout *layout =
		_neverc_krt_get_gki_layout();
	struct list_head *list;
	const char *name;

	list = (struct list_head *)((char *)mod + layout->module_list);
	{
		unsigned long ln, lp;
		if (neverc_krt_mem_read(&ln, &list->next, 8))
			return -1;
		if (neverc_krt_mem_read(&lp, &list->prev, 8))
			return -2;
		if (ln < 0xFFFF000000000000UL &&
		    ln != (unsigned long)list)
			return -1;
		if (lp < 0xFFFF000000000000UL &&
		    lp != (unsigned long)list)
			return -2;
	}

	name = (const char *)((char *)mod + layout->module_name);
	if (expected_name) {
		int elen = 0;
		while (expected_name[elen]) elen++;
		char nbuf[64];
		if (elen >= (int)sizeof(nbuf))
			elen = (int)sizeof(nbuf) - 1;
		if (neverc_krt_mem_read(nbuf, name, elen + 1))
			return -3;
		nbuf[elen] = '\0';
		const char *a = nbuf;
		const char *b = expected_name;
		while (*a && *b && *a == *b) { a++; b++; }
		if (*a != *b) return -3;
	} else {
		unsigned char c;
		if (neverc_krt_mem_read(&c, name, 1))
			return -4;
		if (c < 0x20 || c > 0x7E)
			return -5;
	}

	return 0;
}

int neverc_krt_probe_module_offsets(struct neverc_krt_this_module *mod,
				    void *expected_init,
				    void *expected_exit)
{
	if (!mod || !expected_init) return -1;

	const unsigned char *base = (const unsigned char *)mod;
	unsigned long i;

	for (i = 64; i < _neverc_krt_get_module_size(); i += 8) {
		unsigned long v;
		if (neverc_krt_mem_read(&v, base + i, 8)) continue;
		if (v == (unsigned long)expected_init) {
			_neverc_krt_rt_off_init = i;
			break;
		}
	}

	if (expected_exit && _neverc_krt_rt_off_init) {
		for (i = _neverc_krt_rt_off_init + 8; i < _neverc_krt_get_module_size(); i += 8) {
			unsigned long v;
			if (neverc_krt_mem_read(&v, base + i, 8)) continue;
			if (v == (unsigned long)expected_exit) {
				_neverc_krt_rt_off_exit = i;
				break;
			}
		}
	}

	return _neverc_krt_rt_off_init ? 0 : -1;
}

int neverc_krt_validate_runtime(struct neverc_krt_this_module *mod,
				const char *name,
				void *init_fn, void *exit_fn)
{
	int ret;

	ret = neverc_krt_check_kernel_match();
	if (ret == NEVERC_KRT_VER_MISMATCH) {
		neverc_krt_probe_module_offsets(mod, init_fn, exit_fn);
	}

	ret = neverc_krt_verify_module_offsets(mod, name);
	return ret;
}

int neverc_krt_patch_vermagic(struct neverc_krt_this_module *mod)
{
	const char *banner;

	banner = (const char *)NEVERC_KRT_LOOKUP("linux_banner");
	if (!banner)
		banner = (const char *)NEVERC_KRT_LOOKUP("linux_proc_banner");
	if (!banner) return -1;

	char ban_raw[64];
	if (neverc_krt_mem_read(ban_raw, banner, sizeof(ban_raw)))
		return -1;
	ban_raw[63] = '\0';

	const char *p = ban_raw;
	while (*p && !(*p >= '0' && *p <= '9')) p++;
	if (!*p) return -2;

	char ver_buf[64];
	int vi = 0;
	while (*p && *p != ' ' && *p != '\n' && vi < 30) {
		ver_buf[vi++] = *p++;
	}

	while (*p == ' ') p++;

	const char *flags[] = {
		"SMP", "preempt", "mod_unload", "modversions", "aarch64", 0
	};
	int fi;
	for (fi = 0; flags[fi]; fi++) {
		if (vi > 0) ver_buf[vi++] = ' ';
		const char *f = flags[fi];
		while (*f && vi < 62) ver_buf[vi++] = *f++;
	}
	ver_buf[vi] = '\0';

	unsigned char *base = (unsigned char *)mod;
	unsigned long modsz = _neverc_krt_get_module_size();
	unsigned long scan;
	for (scan = 0; scan + 9 < modsz; scan++) {
		unsigned char sw[9];
		if (neverc_krt_mem_read(sw, base + scan, 9))
			continue;
		if (sw[0] == 'v' && sw[1] == 'e' &&
		    sw[2] == 'r' && sw[3] == 'm' &&
		    sw[4] == 'a' && sw[5] == 'g' &&
		    sw[6] == 'i' && sw[7] == 'c' && sw[8] == '=') {
			if (vi > 60) vi = 60;
			ver_buf[vi] = '\0';
			neverc_krt_mem_write_protected(
				(unsigned long)(base + scan + 9),
				ver_buf, vi + 1);
			return 0;
		}
	}

	return -3;
}

int neverc_krt_fixup_runtime(struct neverc_krt_this_module *mod,
			     const char *name,
			     void *init_fn, void *exit_fn)
{
	int ret;

	ret = neverc_krt_check_kernel_match();
	if (ret == NEVERC_KRT_VER_MISMATCH || ret == NEVERC_KRT_VER_COMPAT) {
		neverc_krt_probe_module_offsets(mod, init_fn, exit_fn);
	}

	return neverc_krt_verify_module_offsets(mod, name);
}

void *neverc_krt_lookup_printk(void)
{
	void *sym = NEVERC_KRT_LOOKUP("_printk");
	if (!sym) sym = NEVERC_KRT_LOOKUP("printk");
	return sym;
}

void *neverc_krt_lookup_copy_from_user(void)
{
	neverc_krt_mem_init();
	return (void *)_neverc_krt_mem_copy_from_user_compat;
}

void *neverc_krt_lookup_copy_to_user(void)
{
	neverc_krt_mem_init();
	return (void *)_neverc_krt_mem_copy_to_user_compat;
}

void *neverc_krt_lookup_probe_read(void)
{
	void *sym = NEVERC_KRT_LOOKUP("copy_from_kernel_nofault");
	if (!sym) sym = NEVERC_KRT_LOOKUP("probe_kernel_read");
	return sym;
}

void *neverc_krt_lookup_probe_write(void)
{
	void *sym = NEVERC_KRT_LOOKUP("copy_to_kernel_nofault");
	if (!sym) sym = NEVERC_KRT_LOOKUP("probe_kernel_write");
	return sym;
}

void *neverc_krt_lookup_module_alloc(void)
{
	void *sym = NEVERC_KRT_LOOKUP("execmem_alloc");
	if (!sym) sym = NEVERC_KRT_LOOKUP("module_alloc");
	return sym;
}

void *neverc_krt_lookup_module_free(void)
{
	void *sym = NEVERC_KRT_LOOKUP("execmem_free");
	if (!sym) sym = NEVERC_KRT_LOOKUP("module_memfree");
	if (!sym) sym = NEVERC_KRT_LOOKUP("vfree");
	return sym;
}

const struct neverc_krt_kernel_info *neverc_krt_kernel_version(void)
{
	neverc_krt_compat_init();
	return &_neverc_krt_kinfo;
}

u32 neverc_krt_kernel_code(void)
{
	neverc_krt_compat_init();
	return _neverc_krt_kinfo.major * 10000 + _neverc_krt_kinfo.minor * 100
	       + _neverc_krt_kinfo.patch;
}

int neverc_krt_kernel_ge(u32 maj, u32 min)
{
	neverc_krt_compat_init();
	return _neverc_krt_kinfo.major > maj ||
	       (_neverc_krt_kinfo.major == maj && _neverc_krt_kinfo.minor >= min);
}

int neverc_krt_kernel_lt(u32 maj, u32 min)
{
	return !neverc_krt_kernel_ge(maj, min);
}

int neverc_krt_should_abort_on_mismatch(void)
{
	int r = neverc_krt_check_kernel_match();
	return r == NEVERC_KRT_VER_MISMATCH;
}

unsigned long neverc_krt_rt_off_init(void)
{
	if (_neverc_krt_rt_off_init) return _neverc_krt_rt_off_init;
	int kv = __atomic_load_n(&_neverc_krt_kernel_ver, __ATOMIC_ACQUIRE);
	return _neverc_krt_lookup_version(kv ? kv : 510)->off_init;
}

unsigned long neverc_krt_rt_off_exit(void)
{
	if (_neverc_krt_rt_off_exit) return _neverc_krt_rt_off_exit;
	int kv = __atomic_load_n(&_neverc_krt_kernel_ver, __ATOMIC_ACQUIRE);
	return _neverc_krt_lookup_version(kv ? kv : 510)->off_exit;
}

