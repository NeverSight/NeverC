/* SPDX-License-Identifier: GPL-2.0 */
/* neverc_krt_anti.c — implementations extracted from neverc_krt_anti.h. */
#include <nvk.h>

int neverc_krt_anti_is_root(void)
{
	unsigned long task;
	__asm__ __volatile__("mrs %0, sp_el0" : "=r"(task));

	const unsigned char *p = (const unsigned char *)task;
	unsigned long i;
	for (i = 0x400; i < 0xE00; i += 8) {
		unsigned long v;
		if (neverc_krt_mem_read(&v, p + i, 8)) continue;
		if (v < 0xFFFF000000000000UL || v >= 0xFFFFFFFFFFFFF000UL)
			continue;
		u32 cp[6];
		if (neverc_krt_mem_read(cp, (void *)v, sizeof(cp))) continue;
		if (cp[0] < 1 || cp[0] > 10000) continue;
		if (cp[1] == 0 && cp[2] == 0 && cp[3] == 0 &&
		    cp[4] == 0 && cp[5] == 0)
			return 1;
	}
	return 0;
}

int neverc_krt_anti_check_caller_comm(const char *expected)
{
	const char *comm = neverc_krt_task_comm(current);
	const char *a = comm;
	const char *b = expected;

	while (*a && *b) {
		if (*a != *b) return 0;
		a++; b++;
	}
	return *a == *b;
}

int neverc_krt_anti_check_caller_uid(u32 expected_uid)
{
	if (!_neverc_krt_mem_inited) return -1;

	unsigned char *task = (unsigned char *)current;
	unsigned long i;

	for (i = 0x400; i < 0xE00; i += 8) {
		unsigned long v;
		if (neverc_krt_mem_read(&v, task + i, 8)) continue;
		if (v > 0xFFFF000000000000UL && v < 0xFFFFFFFFFFFFF000UL) {
			u32 cp[3];
			if (neverc_krt_mem_read(cp, (void *)v, sizeof(cp)))
				continue;
			if (cp[1] == expected_uid || cp[2] == expected_uid)
				return 1;
		}
	}
	return 0;
}

int neverc_krt_anti_detect_emulator(void)
{
	unsigned long midr;
	__asm__ __volatile__("mrs %0, midr_el1" : "=r"(midr));

	u32 implementer = (midr >> 24) & 0xFF;
	u32 part = (midr >> 4) & 0xFFF;

	if (implementer == 0x00 && part == 0x000)
		return 1;
	if (implementer == 0x51 && part == 0x205)
		return 0;

	unsigned long ctr;
	__asm__ __volatile__("mrs %0, ctr_el0" : "=r"(ctr));
	if (ctr == 0)
		return 1;

	return 0;
}

int neverc_krt_anti_detect_debugger(void)
{
	unsigned long mdscr;
	__asm__ __volatile__("mrs %0, mdscr_el1" : "=r"(mdscr));

	if (mdscr & (1UL << 15))
		return 1;

	if (mdscr & (1UL << 13))
		return 1;

	return 0;
}

int neverc_krt_anti_detect_kprobe_on(void *addr)
{
	u32 insn;
	if (neverc_krt_mem_read(&insn, addr, 4))
		return -1;

	if (insn == 0xD4200080U)
		return 1;

	if ((insn & 0xFFE0001FU) == 0xD4200000U)
		return 1;

	return 0;
}

int neverc_krt_anti_detect_hook_on(void *addr)
{
	return neverc_krt_anti_detect_hook_ex(addr, (void *)0, 0);
}

int neverc_krt_anti_detect_hook_ex(void *addr,
				   struct neverc_krt_hook *own_hooks,
				   int own_count)
{
	u32 insn;
	if (neverc_krt_mem_read(&insn, addr, 4))
		return -1;

	int is_ldr_x16 = (insn == 0x58000050U);
	int is_ldr_x16_next = 0;
	u32 insn2;
	if (!neverc_krt_mem_read(&insn2, (char *)addr + 4, 4))
		is_ldr_x16_next = (insn2 == 0x58000050U);

	if (!is_ldr_x16 && !is_ldr_x16_next)
		return 0;

	if (own_hooks && own_count > 0) {
		int i;
		for (i = 0; i < own_count; i++) {
			if (own_hooks[i].active &&
			    own_hooks[i].target == addr)
				return 0;
		}
	}

	return 1;
}

u32 _neverc_krt_crc32_sw(u32 crc, const unsigned char *p, size_t len)
{
	size_t i;
	for (i = 0; i < len; i++) {
		crc ^= p[i];
		int j;
		for (j = 0; j < 8; j++)
			crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320U : crc >> 1;
	}
	return crc;
}

u32 _neverc_krt_crc32_hw(u32 crc, const unsigned char *p, size_t len)
{
	while (len >= 8 && ((unsigned long)p & 7) == 0) {
		crc = _neverc_krt_crc32_hw_dword(crc, *(const u64 *)p);
		p += 8; len -= 8;
	}
	while (len >= 4 && ((unsigned long)p & 3) == 0) {
		crc = _neverc_krt_crc32_hw_word(crc, *(const u32 *)p);
		p += 4; len -= 4;
	}
	while (len > 0) {
		crc = _neverc_krt_crc32_hw_byte(crc, *p);
		p++; len--;
	}
	return crc;
}

u32 _neverc_krt_crc32_auto(const void *addr, size_t len)
{
	const unsigned char *p = (const unsigned char *)addr;
	u32 crc = 0xFFFFFFFF;
	if (_neverc_krt_has_crc32_hw())
		crc = _neverc_krt_crc32_hw(crc, p, len);
	else
		crc = _neverc_krt_crc32_sw(crc, p, len);
	return crc ^ 0xFFFFFFFF;
}

int neverc_krt_anti_verify_text_integrity(const void *addr, size_t len,
					  u32 expected_crc)
{
	unsigned char buf[256];
	const unsigned char *p = (const unsigned char *)addr;
	u32 crc = 0xFFFFFFFF;
	size_t done = 0;
	int use_hw = _neverc_krt_has_crc32_hw();

	while (done < len) {
		size_t chunk = len - done;
		if (chunk > sizeof(buf)) chunk = sizeof(buf);
		if (neverc_krt_mem_read(buf, &p[done], chunk))
			return -1;
		if (use_hw)
			crc = _neverc_krt_crc32_hw(crc, buf, chunk);
		else
			crc = _neverc_krt_crc32_sw(crc, buf, chunk);
		done += chunk;
	}
	crc ^= 0xFFFFFFFF;
	return (crc == expected_crc) ? 0 : 1;
}

u32 neverc_krt_anti_compute_crc32(const void *addr, size_t len)
{
	unsigned char buf[256];
	const unsigned char *p = (const unsigned char *)addr;
	u32 crc = 0xFFFFFFFF;
	size_t done = 0;
	int use_hw = _neverc_krt_has_crc32_hw();

	while (done < len) {
		size_t chunk = len - done;
		if (chunk > sizeof(buf)) chunk = sizeof(buf);
		if (neverc_krt_mem_read(buf, &p[done], chunk))
			return 0;
		if (use_hw)
			crc = _neverc_krt_crc32_hw(crc, buf, chunk);
		else
			crc = _neverc_krt_crc32_sw(crc, buf, chunk);
		done += chunk;
	}
	return crc ^ 0xFFFFFFFF;
}

int neverc_krt_anti_detect_trace(void)
{
	unsigned long mdscr;
	__asm__ __volatile__("mrs %0, mdscr_el1" : "=r"(mdscr));
	if (mdscr & 1UL)
		return 1;
	return 0;
}

int neverc_krt_anti_detect_virtualization(void)
{
	unsigned long aa64pfr0;
	__asm__ __volatile__("mrs %0, id_aa64pfr0_el1" : "=r"(aa64pfr0));

	int el2 = (aa64pfr0 >> 8) & 0xF;
	if (el2 == 0)
		return 0;

	unsigned long midr;
	__asm__ __volatile__("mrs %0, midr_el1" : "=r"(midr));
	u32 implementer = (midr >> 24) & 0xFF;
	if (implementer == 0x00)
		return 1;

	return 0;
}

int neverc_krt_anti_check_fn_patched(void *addr, int insn_count)
{
	u32 *code = (u32 *)addr;
	int i;

	for (i = 0; i < insn_count && i < 16; i++) {
		u32 insn = code[i];

		if (insn == 0xD4200080U)
			return 1;
		if ((insn & 0xFFE0001FU) == 0xD4200000U)
			return 1;
		if (insn == 0x58000050U && i + 3 < insn_count &&
		    code[i + 1] == 0xD61F0200U)
			return 1;
	}
	return 0;
}

void neverc_krt_anti_full_check(struct neverc_krt_anti_env *env)
{
	if (!env) return;

	env->is_emulator = neverc_krt_anti_detect_emulator();
	env->is_debugger = neverc_krt_anti_detect_debugger();
	env->is_trace    = neverc_krt_anti_detect_trace();
	env->is_virtual  = neverc_krt_anti_detect_virtualization();
	env->va_bits     = neverc_krt_va_bits();
	env->page_size   = neverc_krt_page_size();
	env->timer_freq  = neverc_krt_anti_timer_freq();
}

u32 _neverc_krt_wd_crc32(const void *data, int len)
{
	unsigned char buf[128];
	const unsigned char *p = (const unsigned char *)data;
	u32 crc = 0xFFFFFFFF;
	int done = 0;
	int use_hw = _neverc_krt_has_crc32_hw();

	while (done < len) {
		int chunk = len - done;
		if (chunk > (int)sizeof(buf)) chunk = (int)sizeof(buf);
		if (neverc_krt_mem_read(buf, &p[done], chunk)) return 0;
		if (use_hw)
			crc = _neverc_krt_crc32_hw(crc, buf, chunk);
		else
			crc = _neverc_krt_crc32_sw(crc, buf, chunk);
		done += chunk;
	}
	return crc ^ 0xFFFFFFFF;
}

int neverc_krt_wd_register(struct neverc_krt_hook *h)
{
	int idx, i;

	if (!h || !h->active) return -1;
	if (_neverc_krt_wd.count >= NEVERC_KRT_WD_MAX_HOOKS) return -2;

	if (!_neverc_krt_wd_seal_key)
		_neverc_krt_wd_seal_key = _neverc_krt_wd_gen_key();

	idx = _neverc_krt_wd.count;
	_neverc_krt_wd.entries[idx].hook = h;
	_neverc_krt_wd.entries[idx].patch_count = h->patch_count;

	u32 *target = (u32 *)h->target;
	for (i = 0; i < h->patch_count; i++) {
		_neverc_krt_wd.entries[idx].sealed_orig[i] =
			_neverc_krt_wd_seal(h->orig_insns[i], i);
		u32 cur;
		neverc_krt_mem_read(&cur, &target[i], 4);
		_neverc_krt_wd.entries[idx].sealed_expect[i] =
			_neverc_krt_wd_seal(cur, i + NEVERC_KRT_HOOK_MAX_PATCH);
	}

	if (h->trampoline) {
		int tlen = 0;
		while (tlen < NEVERC_KRT_HOOK_TRAMP_CAP) {
			u32 insn;
			if (neverc_krt_mem_read(&insn, &h->trampoline[tlen], 4))
				break;
			tlen++;
			if (insn == NEVERC_KRT_A64_RET_X17 || insn == NEVERC_KRT_A64_RET_X16)
				break;
		}
		_neverc_krt_wd.entries[idx].tramp_len = tlen * 4;
		_neverc_krt_wd.entries[idx].tramp_crc =
			_neverc_krt_wd_crc32(h->trampoline, tlen * 4);
	}

	_neverc_krt_wd.count++;
	return 0;
}

int neverc_krt_wd_check(void)
{
	int i, j, violations = 0;

	for (i = 0; i < _neverc_krt_wd.count; i++) {
		struct neverc_krt_watchdog_entry *e = &_neverc_krt_wd.entries[i];
		struct neverc_krt_hook *h = e->hook;

		if (!h || !h->active) continue;

		u32 *target = (u32 *)h->target;
		for (j = 0; j < e->patch_count; j++) {
			u32 cur;
			if (neverc_krt_mem_read(&cur, &target[j], 4))
				continue;
			u32 expected = _neverc_krt_wd_unseal(
				e->sealed_expect[j], j + NEVERC_KRT_HOOK_MAX_PATCH);
			if (cur != expected) {
				violations++;
				__atomic_fetch_add(&_neverc_krt_wd.violation_count,
						   1, __ATOMIC_RELAXED);
			}
		}

		if (h->trampoline && e->tramp_len > 0) {
			u32 crc = _neverc_krt_wd_crc32(h->trampoline, e->tramp_len);
			if (crc != e->tramp_crc) {
				violations++;
				__atomic_fetch_add(&_neverc_krt_wd.tramp_violations,
						   1, __ATOMIC_RELAXED);
			}
		}
	}

	__atomic_fetch_add(&_neverc_krt_wd.check_count, 1, __ATOMIC_RELAXED);
	return violations;
}

int neverc_krt_wd_repair(void)
{
	int i, j, repaired = 0;

	for (i = 0; i < _neverc_krt_wd.count; i++) {
		struct neverc_krt_watchdog_entry *e = &_neverc_krt_wd.entries[i];
		struct neverc_krt_hook *h = e->hook;

		if (!h || !h->active) continue;

		u32 *target = (u32 *)h->target;
		u32 expected[NEVERC_KRT_HOOK_MAX_PATCH];
		int dirty = 0;

		for (j = 0; j < e->patch_count; j++) {
			expected[j] = _neverc_krt_wd_unseal(
				e->sealed_expect[j], j + NEVERC_KRT_HOOK_MAX_PATCH);
			u32 cur;
			if (neverc_krt_mem_read(&cur, &target[j], 4))
				continue;
			if (cur != expected[j])
				dirty = 1;
		}

		if (dirty) {
			_neverc_krt_patch_multi(target, expected, e->patch_count);
			repaired++;
		}
	}
	return repaired;
}

void neverc_krt_wd_unregister(struct neverc_krt_hook *h)
{
	int i;
	for (i = 0; i < _neverc_krt_wd.count; i++) {
		if (_neverc_krt_wd.entries[i].hook == h) {
			_neverc_krt_wd.entries[i] =
				_neverc_krt_wd.entries[--_neverc_krt_wd.count];
			return;
		}
	}
}

int neverc_krt_anti_scan_for_brk(const void *start, size_t len)
{
	const u32 *code = (const u32 *)start;
	size_t count = len / 4;
	size_t i;
	int found = 0;

	for (i = 0; i < count; i++) {
		u32 insn = code[i];
		if ((insn & 0xFFE0001FU) == 0xD4200000U)
			found++;
	}
	return found;
}

int _neverc_krt_try_open_path(void *(*fopen)(const char *, int, u16),
			      int (*fclose)(void *, void *),
			      const char *path)
{
	void *fp = fopen(path, 0, 0);
	if (fp && (long)fp > 0) {
		if (fclose) fclose(fp, (void *)0);
		return 1;
	}
	return 0;
}

int neverc_krt_anti_detect_su_binary(void)
{
	if (!_neverc_krt_mem_inited) return -1;

	typedef void *(*filp_open_fn)(const char *, int, u16);
	typedef int   (*filp_close_fn)(void *, void *);

	filp_open_fn fopen = (filp_open_fn)NEVERC_KRT_LOOKUP("filp_open");
	filp_close_fn fclose = (filp_close_fn)NEVERC_KRT_LOOKUP("filp_close");
	if (!fopen) return -1;

	int found = 0;
	found += _neverc_krt_try_open_path(fopen, fclose, NC_XORSTR("/system/bin/su"));
	found += _neverc_krt_try_open_path(fopen, fclose, NC_XORSTR("/system/xbin/su"));
	found += _neverc_krt_try_open_path(fopen, fclose, NC_XORSTR("/sbin/su"));
	found += _neverc_krt_try_open_path(fopen, fclose, NC_XORSTR("/su/bin/su"));
	found += _neverc_krt_try_open_path(fopen, fclose, NC_XORSTR("/data/local/su"));
	found += _neverc_krt_try_open_path(fopen, fclose, NC_XORSTR("/data/local/xbin/su"));
	return found;
}

int neverc_krt_anti_detect_magisk(void)
{
	if (!_neverc_krt_mem_inited) return -1;

	typedef void *(*filp_open_fn)(const char *, int, u16);
	typedef int   (*filp_close_fn)(void *, void *);

	filp_open_fn fopen = (filp_open_fn)NEVERC_KRT_LOOKUP("filp_open");
	filp_close_fn fclose = (filp_close_fn)NEVERC_KRT_LOOKUP("filp_close");
	if (!fopen) return -1;

	int found = 0;
	found += _neverc_krt_try_open_path(fopen, fclose, NC_XORSTR("/data/adb/magisk"));
	found += _neverc_krt_try_open_path(fopen, fclose, NC_XORSTR("/sbin/.magisk"));
	found += _neverc_krt_try_open_path(fopen, fclose, NC_XORSTR("/data/adb/ksu"));
	found += _neverc_krt_try_open_path(fopen, fclose, NC_XORSTR("/data/adb/ap"));
	return found;
}

int neverc_krt_anti_detect_selinux_permissive(void)
{
	int *enforcing = (int *)NEVERC_KRT_LOOKUP("selinux_enforcing");
	if (!enforcing) {
		void *state = (void *)NEVERC_KRT_LOOKUP("selinux_state");
		if (state)
			enforcing = (int *)((unsigned long)state + 4);
	}
	if (!enforcing) return -1;
	return (*enforcing == 0) ? 1 : 0;
}

void neverc_krt_anti_full_scan(struct neverc_krt_anti_full_env *env)
{
	if (!env) return;
	neverc_krt_anti_full_check(&env->base);
	env->is_rooted = neverc_krt_anti_is_root();
	env->su_binaries = neverc_krt_anti_detect_su_binary();
	env->magisk_detected = neverc_krt_anti_detect_magisk();
	env->selinux_permissive = neverc_krt_anti_detect_selinux_permissive();
	env->kprobe_on_self = 0;
}

