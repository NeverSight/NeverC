/* SPDX-License-Identifier: GPL-2.0 */
/* nvk_compat.c — implementations extracted from nvk_compat.h. */
#include <nvk.h>

void _nvk_parse_version(const char *str, struct nvk_kernel_info *info)
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

int nvk_compat_init(void)
{
	const char *banner;

	if (_nvk_compat_inited) return 0;

	if (!_nvk_mem_inited)
		nvk_mem_init();

	banner = (const char *)NVK_LOOKUP("linux_banner");
	if (!banner) {
		banner = (const char *)NVK_LOOKUP("linux_proc_banner");
	}

	if (banner) {
		const char *ver_start = banner;
		while (*ver_start && !(*ver_start >= '0' && *ver_start <= '9'))
			ver_start++;
		if (*ver_start)
			_nvk_parse_version(ver_start, &_nvk_kinfo);
	}

	if (!_nvk_kinfo.detected) {
#if NVK_KERNEL == 510
		_nvk_kinfo.major = 5;
		_nvk_kinfo.minor = 10;
		_nvk_kinfo.android_version = 12;
#elif NVK_KERNEL == 515
		_nvk_kinfo.major = 5;
		_nvk_kinfo.minor = 15;
		_nvk_kinfo.android_version = 13;
#elif NVK_KERNEL == 601
		_nvk_kinfo.major = 6;
		_nvk_kinfo.minor = 1;
		_nvk_kinfo.android_version = 14;
#elif NVK_KERNEL == 606
		_nvk_kinfo.major = 6;
		_nvk_kinfo.minor = 6;
		_nvk_kinfo.android_version = 15;
#elif NVK_KERNEL == 612
		_nvk_kinfo.major = 6;
		_nvk_kinfo.minor = 12;
		_nvk_kinfo.android_version = 16;
#endif
		_nvk_kinfo.detected = 1;
	}

	nvk_fmt_init();

	_nvk_compat_inited = 1;
	return 0;
}

void nvk_detect_hw_caps(struct nvk_hw_caps *caps)
{
	if (!caps) return;
	caps->pac  = nvk_has_pac();
	caps->epac = nvk_has_epac();
	caps->fpac = nvk_has_fpac();
	caps->bti  = nvk_has_bti();
	caps->mte  = nvk_has_mte();
	caps->sve  = nvk_has_sve();
	caps->cfi  = nvk_has_cfi();
}

int nvk_check_kernel_match(void)
{
	if (!_nvk_kinfo.detected)
		return NVK_VER_UNKNOWN;

	u32 expected_major = 0, expected_minor = 0;
#if NVK_KERNEL == 510
	expected_major = 5; expected_minor = 10;
#elif NVK_KERNEL == 515
	expected_major = 5; expected_minor = 15;
#elif NVK_KERNEL == 601
	expected_major = 6; expected_minor = 1;
#elif NVK_KERNEL == 606
	expected_major = 6; expected_minor = 6;
#elif NVK_KERNEL == 612
	expected_major = 6; expected_minor = 12;
#endif

	if (_nvk_kinfo.major == expected_major &&
	    _nvk_kinfo.minor == expected_minor)
		return NVK_VER_EXACT;

	if (_nvk_kinfo.major == expected_major)
		return NVK_VER_COMPAT;

	return NVK_VER_MISMATCH;
}

int nvk_verify_module_offsets(struct nvk_this_module *mod,
				     const char *expected_name)
{
	struct list_head *list;
	const char *name;

	list = (struct list_head *)((char *)mod + NVK_OFF_LIST);
	if ((unsigned long)list->next < 0xFFFF000000000000UL &&
	    list->next != list)
		return -1;
	if ((unsigned long)list->prev < 0xFFFF000000000000UL &&
	    list->prev != list)
		return -2;

	name = (const char *)((char *)mod + NVK_OFF_NAME);
	if (expected_name) {
		const char *a = name;
		const char *b = expected_name;
		while (*a && *b && *a == *b) { a++; b++; }
		if (*a != *b) return -3;
	} else {
		unsigned char c;
		if (nvk_mem_read(&c, name, 1))
			return -4;
		if (c < 0x20 || c > 0x7E)
			return -5;
	}

	return 0;
}

int nvk_probe_module_offsets(struct nvk_this_module *mod,
				    void *expected_init,
				    void *expected_exit)
{
	if (!mod || !expected_init) return -1;

	const unsigned char *base = (const unsigned char *)mod;
	unsigned long i;

	for (i = 64; i < NVK_MODULE_SIZE; i += 8) {
		unsigned long v;
		if (nvk_mem_read(&v, base + i, 8)) continue;
		if (v == (unsigned long)expected_init) {
			_nvk_rt_off_init = i;
			break;
		}
	}

	if (expected_exit && _nvk_rt_off_init) {
		for (i = _nvk_rt_off_init + 8; i < NVK_MODULE_SIZE; i += 8) {
			unsigned long v;
			if (nvk_mem_read(&v, base + i, 8)) continue;
			if (v == (unsigned long)expected_exit) {
				_nvk_rt_off_exit = i;
				break;
			}
		}
	}

	return _nvk_rt_off_init ? 0 : -1;
}

int nvk_validate_runtime(struct nvk_this_module *mod,
				const char *name,
				void *init_fn, void *exit_fn)
{
	int ret;

	ret = nvk_check_kernel_match();
	if (ret == NVK_VER_MISMATCH) {
		nvk_probe_module_offsets(mod, init_fn, exit_fn);
	}

	ret = nvk_verify_module_offsets(mod, name);
	return ret;
}

int nvk_patch_vermagic(struct nvk_this_module *mod)
{
	const char *banner;

	banner = (const char *)NVK_LOOKUP("linux_banner");
	if (!banner)
		banner = (const char *)NVK_LOOKUP("linux_proc_banner");
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
	for (scan = 0; scan + 8 < NVK_MODULE_SIZE; scan++) {
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

int nvk_fixup_runtime(struct nvk_this_module *mod,
			     const char *name,
			     void *init_fn, void *exit_fn)
{
	int ret;

	ret = nvk_check_kernel_match();
	if (ret == NVK_VER_MISMATCH || ret == NVK_VER_COMPAT) {
		nvk_probe_module_offsets(mod, init_fn, exit_fn);
	}

	return nvk_verify_module_offsets(mod, name);
}

