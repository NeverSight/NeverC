/* SPDX-License-Identifier: GPL-2.0 */
/* neverc_krt_compat.c — implementations extracted from neverc_krt_compat.h. */
#include <nvk.h>

void _neverc_krt_parse_version(const char *str, struct neverc_krt_kernel_info *info)
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

	if (!_neverc_krt_mem_inited)
		neverc_krt_mem_init();

	banner = (const char *)NEVERC_KRT_LOOKUP("linux_banner");
	if (!banner) {
		banner = (const char *)NEVERC_KRT_LOOKUP("linux_proc_banner");
	}

	if (banner) {
		const char *ver_start = banner;
		while (*ver_start && !(*ver_start >= '0' && *ver_start <= '9'))
			ver_start++;
		if (*ver_start)
			_neverc_krt_parse_version(ver_start, &_neverc_krt_kinfo);
	}

	if (!_neverc_krt_kinfo.detected) {
#if NEVERC_KRT_KERNEL == 510
		_neverc_krt_kinfo.major = 5;
		_neverc_krt_kinfo.minor = 10;
		_neverc_krt_kinfo.android_version = 12;
#elif NEVERC_KRT_KERNEL == 515
		_neverc_krt_kinfo.major = 5;
		_neverc_krt_kinfo.minor = 15;
		_neverc_krt_kinfo.android_version = 13;
#elif NEVERC_KRT_KERNEL == 601
		_neverc_krt_kinfo.major = 6;
		_neverc_krt_kinfo.minor = 1;
		_neverc_krt_kinfo.android_version = 14;
#elif NEVERC_KRT_KERNEL == 606
		_neverc_krt_kinfo.major = 6;
		_neverc_krt_kinfo.minor = 6;
		_neverc_krt_kinfo.android_version = 15;
#elif NEVERC_KRT_KERNEL == 612
		_neverc_krt_kinfo.major = 6;
		_neverc_krt_kinfo.minor = 12;
		_neverc_krt_kinfo.android_version = 16;
#endif
		_neverc_krt_kinfo.detected = 1;
	}

	neverc_krt_fmt_init();

	_neverc_krt_compat_inited = 1;
	return 0;
}

void neverc_krt_detect_hw_caps(struct neverc_krt_hw_caps *caps)
{
	if (!caps) return;
	caps->pac  = neverc_krt_has_pac();
	caps->epac = neverc_krt_has_epac();
	caps->fpac = neverc_krt_has_fpac();
	caps->bti  = neverc_krt_has_bti();
	caps->mte  = neverc_krt_has_mte();
	caps->sve  = neverc_krt_has_sve();
	caps->cfi  = neverc_krt_has_cfi();
}

int neverc_krt_check_kernel_match(void)
{
	if (!_neverc_krt_kinfo.detected)
		return NEVERC_KRT_VER_UNKNOWN;

	u32 expected_major = 0, expected_minor = 0;
#if NEVERC_KRT_KERNEL == 510
	expected_major = 5; expected_minor = 10;
#elif NEVERC_KRT_KERNEL == 515
	expected_major = 5; expected_minor = 15;
#elif NEVERC_KRT_KERNEL == 601
	expected_major = 6; expected_minor = 1;
#elif NEVERC_KRT_KERNEL == 606
	expected_major = 6; expected_minor = 6;
#elif NEVERC_KRT_KERNEL == 612
	expected_major = 6; expected_minor = 12;
#endif

	if (_neverc_krt_kinfo.major == expected_major &&
	    _neverc_krt_kinfo.minor == expected_minor)
		return NEVERC_KRT_VER_EXACT;

	if (_neverc_krt_kinfo.major == expected_major)
		return NEVERC_KRT_VER_COMPAT;

	return NEVERC_KRT_VER_MISMATCH;
}

int neverc_krt_verify_module_offsets(struct neverc_krt_this_module *mod,
				     const char *expected_name)
{
	struct list_head *list;
	const char *name;

	list = (struct list_head *)((char *)mod + NEVERC_KRT_OFF_LIST);
	if ((unsigned long)list->next < 0xFFFF000000000000UL &&
	    list->next != list)
		return -1;
	if ((unsigned long)list->prev < 0xFFFF000000000000UL &&
	    list->prev != list)
		return -2;

	name = (const char *)((char *)mod + NEVERC_KRT_OFF_NAME);
	if (expected_name) {
		const char *a = name;
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

	for (i = 64; i < NEVERC_KRT_MODULE_SIZE; i += 8) {
		unsigned long v;
		if (neverc_krt_mem_read(&v, base + i, 8)) continue;
		if (v == (unsigned long)expected_init) {
			_neverc_krt_rt_off_init = i;
			break;
		}
	}

	if (expected_exit && _neverc_krt_rt_off_init) {
		for (i = _neverc_krt_rt_off_init + 8; i < NEVERC_KRT_MODULE_SIZE; i += 8) {
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

	const char *p = banner;
	while (*p && !(*p >= '0' && *p <= '9')) p++;
	if (!*p) return -2;

	char ver_buf[64];
	int vi = 0;
	while (*p && *p != ' ' && *p != '\n' && vi < 30) {
		ver_buf[vi++] = *p++;
	}

	while (*p == ' ') p++;

	const char *flags[] = {
		"SMP", "preempt", "mod_unload", "aarch64", 0
	};
	int fi;
	for (fi = 0; flags[fi]; fi++) {
		if (vi > 0) ver_buf[vi++] = ' ';
		const char *f = flags[fi];
		while (*f && vi < 62) ver_buf[vi++] = *f++;
	}
	ver_buf[vi] = '\0';

	unsigned char *base = (unsigned char *)mod;
	unsigned long scan;
	for (scan = 0; scan + 8 < NEVERC_KRT_MODULE_SIZE; scan++) {
		if (base[scan] == 'v' && base[scan+1] == 'e' &&
		    base[scan+2] == 'r' && base[scan+3] == 'm' &&
		    base[scan+4] == 'a' && base[scan+5] == 'g' &&
		    base[scan+6] == 'i' && base[scan+7] == 'c') {
			unsigned long eq = scan + 8;
			if (base[eq] == '=') {
				char *dst = (char *)&base[eq + 1];
				int di = 0;
				while (di < vi && di < 60) {
					dst[di] = ver_buf[di];
					di++;
				}
				dst[di] = '\0';
				return 0;
			}
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

