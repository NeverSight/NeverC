/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>
#include "nvk_internal.h"
#include "nvk_compat_table.inc"

static const struct neverc_krt_profile *_neverc_krt_selected_profile;
static const struct neverc_krt_profile *_neverc_krt_active_profile;
static const struct neverc_krt_layout_entry *_neverc_krt_active_layout;

static __always_inline const struct neverc_krt_layout_entry *
_neverc_krt_find_layout(unsigned int profile_id)
{
	unsigned long i;

	for (i = 0; i < NEVERC_KRT_LAYOUT_COUNT; i++) {
		if (_neverc_krt_layouts[i].profile_id == profile_id)
			return &_neverc_krt_layouts[i];
	}
	return (const struct neverc_krt_layout_entry *)0;
}

static __always_inline const struct neverc_krt_profile *
_neverc_krt_current_profile(void)
{
	return __atomic_load_n(&_neverc_krt_active_profile, __ATOMIC_ACQUIRE);
}

static __always_inline const struct neverc_krt_layout_entry *
_neverc_krt_current_layout(void)
{
	if (!_neverc_krt_current_profile())
		return (const struct neverc_krt_layout_entry *)0;
	return __atomic_load_n(&_neverc_krt_active_layout, __ATOMIC_ACQUIRE);
}

int _neverc_krt_version_setup(unsigned int profile_id)
{
	const struct neverc_krt_profile *profile =
		neverc_krt_find_profile(profile_id);
	const struct neverc_krt_profile *selected;

	if (!profile || !_neverc_krt_find_layout(profile_id))
		return -1;
	selected = __atomic_load_n(&_neverc_krt_selected_profile,
				   __ATOMIC_ACQUIRE);
	if (selected)
		return selected->legacy_id == profile_id ? 0 : -2;
	if (!__atomic_compare_exchange_n(&_neverc_krt_selected_profile, &selected,
					 profile, 0, __ATOMIC_RELEASE,
					 __ATOMIC_ACQUIRE))
		return selected->legacy_id == profile_id ? 0 : -2;
	return 0;
}

const struct neverc_krt_runtime_caps *_neverc_krt_current_caps(void)
{
	const struct neverc_krt_profile *profile = _neverc_krt_current_profile();

	return profile ? &profile->caps :
		(const struct neverc_krt_runtime_caps *)0;
}

unsigned long _neverc_krt_get_module_size(void)
{
	const struct neverc_krt_layout_entry *layout =
		_neverc_krt_current_layout();

	return layout ? layout->module_size : 0;
}

unsigned long _neverc_krt_get_kimage_vaddr_base(void)
{
	const struct neverc_krt_profile *profile = _neverc_krt_current_profile();

	return profile ? profile->kimage_vaddr : 0;
}

const struct neverc_krt_gki_layout *_neverc_krt_get_gki_layout(void)
{
	const struct neverc_krt_layout_entry *layout =
		_neverc_krt_current_layout();

	return layout ? &layout->layout :
		(const struct neverc_krt_gki_layout *)0;
}

unsigned long _neverc_krt_get_file_dentry_off(void)
{
	const struct neverc_krt_layout_entry *layout =
		_neverc_krt_current_layout();

	return layout ? layout->file_dentry_off : 0;
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

static __always_inline unsigned int _neverc_krt_runtime_page_shift(void)
{
	unsigned long tcr;

	__asm__ __volatile__("mrs %0, tcr_el1" : "=r"(tcr));
	switch ((tcr >> 30) & 3UL) {
	case 1:
		return 14; /* 16 KiB */
	case 2:
		return 12; /* 4 KiB */
	case 3:
		return 16; /* 64 KiB */
	default:
		return 0;
	}
}

static int
_neverc_krt_activate_observed(
	const struct neverc_krt_kernel_info *observed,
	const char *release_token, unsigned long release_token_length)
{
	const struct neverc_krt_profile *profile;
	const struct neverc_krt_profile *selected;
	const struct neverc_krt_profile *active;
	const struct neverc_krt_layout_entry *layout;

	profile = neverc_krt_find_profile_by_identity(
		observed->major, observed->minor, observed->patch,
		observed->android_version, observed->kmi_generation,
		observed->page_shift, release_token, release_token_length);
	if (!profile)
		return -1;
	selected = __atomic_load_n(&_neverc_krt_selected_profile,
				   __ATOMIC_ACQUIRE);
	if (selected && selected->legacy_id != profile->legacy_id)
		return -2;
	if (!selected &&
	    !__atomic_compare_exchange_n(&_neverc_krt_selected_profile, &selected,
					 profile, 0, __ATOMIC_RELEASE,
					 __ATOMIC_ACQUIRE) &&
	    selected->legacy_id != profile->legacy_id)
		return -2;
	layout = _neverc_krt_find_layout(profile->legacy_id);
	if (!layout)
		return -1;

	_neverc_krt_kinfo = *observed;
	__atomic_store_n(&_neverc_krt_active_layout, layout, __ATOMIC_RELEASE);
	active = (const struct neverc_krt_profile *)0;
	if (!__atomic_compare_exchange_n(&_neverc_krt_active_profile, &active,
					 profile, 0, __ATOMIC_RELEASE,
					 __ATOMIC_ACQUIRE) &&
	    active->legacy_id != profile->legacy_id)
		return -2;
	return 0;
}

static int _neverc_krt_version_try_detect_from_banner(void)
{
	const char *banner;
	char buf[128];
	struct neverc_krt_observed_identity identity;
	struct neverc_krt_kernel_info observed = {0};

	if (_neverc_krt_current_profile())
		return 0;
	banner = (const char *)NEVERC_KRT_LOOKUP("linux_banner");
	if (!banner)
		banner = (const char *)NEVERC_KRT_LOOKUP("linux_proc_banner");
	if (!banner)
		return -1;
	if (neverc_krt_mem_read(buf, banner, sizeof(buf)) != 0)
		return -1;
	buf[sizeof(buf) - 1] = '\0';
	if (neverc_krt_parse_banner_identity(buf, &identity))
		return -1;

	observed.major = identity.linux_major;
	observed.minor = identity.linux_minor;
	observed.patch = identity.linux_patch;
	observed.android_version = identity.android_release;
	observed.kmi_generation = identity.kmi_generation;
	observed.page_shift = _neverc_krt_runtime_page_shift();
	observed.detected = 1;
	_neverc_krt_kinfo = observed;
	return _neverc_krt_activate_observed(
		&observed, identity.release_token,
		identity.release_token_length);
}

int neverc_krt_compat_init(void)
{
	int ret;

	if (_neverc_krt_current_profile())
		return 0;
	ret = neverc_krt_mem_init();
	if (ret)
		return ret;
	ret = _neverc_krt_version_try_detect_from_banner();
	if (ret)
		return ret;
	(void)neverc_krt_fmt_init();
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
	int ret = neverc_krt_compat_init();

	if (!ret && _neverc_krt_current_profile())
		return NEVERC_KRT_VER_EXACT;
	return _neverc_krt_kinfo.detected ? NEVERC_KRT_VER_MISMATCH :
		NEVERC_KRT_VER_UNKNOWN;
}

int neverc_krt_verify_module_offsets(struct neverc_krt_this_module *mod,
				     const char *expected_name)
{
	const struct neverc_krt_gki_layout *layout =
		_neverc_krt_get_gki_layout();
	struct list_head *list;
	const char *name;

	if (!layout || !mod)
		return -1;
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
	if (ret != NEVERC_KRT_VER_EXACT)
		return ret;
	(void)init_fn;
	(void)exit_fn;
	return neverc_krt_verify_module_offsets(mod, name);
}

int neverc_krt_patch_vermagic(struct neverc_krt_this_module *mod)
{
	const char *banner;
	int match = neverc_krt_check_kernel_match();

	if (match != NEVERC_KRT_VER_EXACT)
		return match;

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
	if (ret != NEVERC_KRT_VER_EXACT)
		return ret;
	(void)init_fn;
	(void)exit_fn;
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
	return r != NEVERC_KRT_VER_EXACT;
}

unsigned long neverc_krt_rt_off_init(void)
{
	const struct neverc_krt_layout_entry *layout;

	if (_neverc_krt_rt_off_init) return _neverc_krt_rt_off_init;
	layout = _neverc_krt_current_layout();
	return layout ? layout->off_init : 0;
}

unsigned long neverc_krt_rt_off_exit(void)
{
	const struct neverc_krt_layout_entry *layout;

	if (_neverc_krt_rt_off_exit) return _neverc_krt_rt_off_exit;
	layout = _neverc_krt_current_layout();
	return layout ? layout->off_exit : 0;
}
