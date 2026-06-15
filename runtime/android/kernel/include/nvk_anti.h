/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_ANTI_H
#define NVK_ANTI_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <linux/string.h>
#include <nvk_mem.h>
#include <nvk_process.h>
#include <nvk_addr.h>
#include <nvk_hook.h>

static int nvk_anti_is_root(void)
{
	unsigned long task;
	__asm__ __volatile__("mrs %0, sp_el0" : "=r"(task));

	const unsigned char *p = (const unsigned char *)task;
	unsigned long i;
	for (i = 0x500; i < 0x800; i += 8) {
		unsigned long v = *(unsigned long *)(p + i);
		if (v < 0xFFFF000000000000UL || v >= 0xFFFFFFFFFFFFF000UL)
			continue;
		const u32 *cp = (const u32 *)v;
		u32 refcnt = cp[0];
		if (refcnt < 1 || refcnt > 10000) continue;
		if (cp[1] == 0 && cp[2] == 0 && cp[3] == 0 &&
		    cp[4] == 0 && cp[5] == 0)
			return 1;
	}
	return 0;
}

static int nvk_anti_check_caller_comm(const char *expected)
{
	const char *comm = nvk_task_comm(current);
	const char *a = comm;
	const char *b = expected;

	while (*a && *b) {
		if (*a != *b) return 0;
		a++; b++;
	}
	return *a == *b;
}

static int nvk_anti_check_caller_uid(u32 expected_uid)
{
	if (!_nvk_mem_inited) return -1;

	unsigned char *task = (unsigned char *)current;
	unsigned long i;

	for (i = 0x500; i < 0x800; i += 8) {
		unsigned long v = *(unsigned long *)(task + i);
		if (v > 0xFFFF000000000000UL && v < 0xFFFFFFFFFFFFF000UL) {
			const u32 *cp = (const u32 *)v;
			if (cp[1] == expected_uid || cp[2] == expected_uid)
				return 1;
		}
	}
	return 0;
}

enum nvk_env_type {
	NVK_ENV_NORMAL   = 0,
	NVK_ENV_EMULATOR = 1,
	NVK_ENV_DEBUGGER = 2,
	NVK_ENV_ROOTED   = 3,
};

static int nvk_anti_detect_emulator(void)
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

static int nvk_anti_detect_debugger(void)
{
	unsigned long mdscr;
	__asm__ __volatile__("mrs %0, mdscr_el1" : "=r"(mdscr));

	if (mdscr & (1UL << 15))
		return 1;

	if (mdscr & (1UL << 13))
		return 1;

	return 0;
}

static int nvk_anti_detect_kprobe_on(void *addr)
{
	u32 insn;
	if (nvk_mem_read(&insn, addr, 4))
		return -1;

	if (insn == 0xD4200080U)
		return 1;

	if ((insn & 0xFFE0001FU) == 0xD4200000U)
		return 1;

	return 0;
}

static int nvk_anti_detect_hook_ex(void *addr,
				   struct nvk_hook *own_hooks,
				   int own_count);

static int nvk_anti_detect_hook_on(void *addr)
{
	return nvk_anti_detect_hook_ex(addr, (void *)0, 0);
}

static int nvk_anti_detect_hook_ex(void *addr,
				   struct nvk_hook *own_hooks,
				   int own_count)
{
	u32 insn;
	if (nvk_mem_read(&insn, addr, 4))
		return -1;

	int is_ldr_x16 = (insn == 0x58000050U);
	int is_ldr_x16_next = 0;
	u32 insn2;
	if (!nvk_mem_read(&insn2, (char *)addr + 4, 4))
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

static int nvk_anti_verify_text_integrity(const void *addr, size_t len,
					  u32 expected_crc)
{
	const unsigned char *p = (const unsigned char *)addr;
	u32 crc = 0xFFFFFFFF;
	size_t i;

	for (i = 0; i < len; i++) {
		unsigned char b;
		if (nvk_mem_read(&b, &p[i], 1))
			return -1;
		crc ^= b;
		int j;
		for (j = 0; j < 8; j++) {
			if (crc & 1)
				crc = (crc >> 1) ^ 0xEDB88320U;
			else
				crc >>= 1;
		}
	}
	crc ^= 0xFFFFFFFF;

	return (crc == expected_crc) ? 0 : 1;
}

static u32 nvk_anti_compute_crc32(const void *addr, size_t len)
{
	const unsigned char *p = (const unsigned char *)addr;
	u32 crc = 0xFFFFFFFF;
	size_t i;

	for (i = 0; i < len; i++) {
		unsigned char b;
		if (nvk_mem_read(&b, &p[i], 1))
			return 0;
		crc ^= b;
		int j;
		for (j = 0; j < 8; j++) {
			if (crc & 1)
				crc = (crc >> 1) ^ 0xEDB88320U;
			else
				crc >>= 1;
		}
	}
	return crc ^ 0xFFFFFFFF;
}

static __always_inline u64 nvk_anti_timestamp(void)
{
	u64 v;
	__asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(v));
	return v;
}

static __always_inline u64 nvk_anti_timer_freq(void)
{
	u64 v;
	__asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(v));
	return v;
}

static __always_inline int nvk_anti_timing_check(u64 start, u64 max_cycles)
{
	u64 now = nvk_anti_timestamp();
	return (now - start) > max_cycles;
}

static int nvk_anti_detect_trace(void)
{
	unsigned long mdscr;
	__asm__ __volatile__("mrs %0, mdscr_el1" : "=r"(mdscr));
	if (mdscr & 1UL)
		return 1;
	return 0;
}

static int nvk_anti_detect_virtualization(void)
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

static int nvk_anti_check_fn_patched(void *addr, int insn_count)
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

struct nvk_anti_env {
	int is_emulator;
	int is_debugger;
	int is_trace;
	int is_virtual;
	u64 va_bits;
	u64 page_size;
	u64 timer_freq;
};

static void nvk_anti_full_check(struct nvk_anti_env *env)
{
	if (!env) return;

	env->is_emulator = nvk_anti_detect_emulator();
	env->is_debugger = nvk_anti_detect_debugger();
	env->is_trace    = nvk_anti_detect_trace();
	env->is_virtual  = nvk_anti_detect_virtualization();
	env->va_bits     = nvk_va_bits();
	env->page_size   = nvk_page_size();
	env->timer_freq  = nvk_anti_timer_freq();
}


/* --- Watchdog: periodic hook integrity check --- */

#define NVK_WD_MAX_HOOKS 16

struct nvk_watchdog_entry {
	struct nvk_hook *hook;
	u32              orig_patch[NVK_HOOK_MAX_PATCH];
	u32              expected_patch[NVK_HOOK_MAX_PATCH];
	int              patch_count;
};

struct nvk_watchdog {
	struct nvk_watchdog_entry entries[NVK_WD_MAX_HOOKS];
	int                       count;
	volatile u64              check_count;
	volatile u64              violation_count;
	volatile int              running;
};

static struct nvk_watchdog _nvk_wd;

static int nvk_wd_register(struct nvk_hook *h)
{
	int idx;

	if (!h || !h->active) return -1;
	if (_nvk_wd.count >= NVK_WD_MAX_HOOKS) return -2;

	idx = _nvk_wd.count;
	_nvk_wd.entries[idx].hook = h;
	_nvk_wd.entries[idx].patch_count = h->patch_count;

	u32 *target = (u32 *)h->target;
	int i;
	for (i = 0; i < h->patch_count; i++) {
		_nvk_wd.entries[idx].orig_patch[i] = h->orig_insns[i];
		_nvk_wd.entries[idx].expected_patch[i] = target[i];
	}

	_nvk_wd.count++;
	return 0;
}

static int nvk_wd_check(void)
{
	int i, j, violations = 0;

	for (i = 0; i < _nvk_wd.count; i++) {
		struct nvk_watchdog_entry *e = &_nvk_wd.entries[i];
		struct nvk_hook *h = e->hook;

		if (!h || !h->active) continue;

		u32 *target = (u32 *)h->target;
		for (j = 0; j < e->patch_count; j++) {
			u32 current_insn;
			if (nvk_mem_read(&current_insn, &target[j], 4))
				continue;
			if (current_insn != e->expected_patch[j]) {
				violations++;
				_nvk_wd.violation_count++;
			}
		}
	}

	_nvk_wd.check_count++;
	return violations;
}

static int nvk_wd_repair(void)
{
	int i, j, repaired = 0;

	for (i = 0; i < _nvk_wd.count; i++) {
		struct nvk_watchdog_entry *e = &_nvk_wd.entries[i];
		struct nvk_hook *h = e->hook;

		if (!h || !h->active) continue;

		u32 *target = (u32 *)h->target;
		for (j = 0; j < e->patch_count; j++) {
			u32 current_insn;
			if (nvk_mem_read(&current_insn, &target[j], 4))
				continue;
			if (current_insn != e->expected_patch[j]) {
				_nvk_patch_multi(target, e->expected_patch,
						 e->patch_count);
				repaired++;
				break;
			}
		}
	}
	return repaired;
}

static __always_inline u64 nvk_wd_checks(void)
{
	return __atomic_load_n(&_nvk_wd.check_count, __ATOMIC_RELAXED);
}

static __always_inline u64 nvk_wd_violations(void)
{
	return __atomic_load_n(&_nvk_wd.violation_count, __ATOMIC_RELAXED);
}


static int nvk_anti_scan_for_brk(const void *start, size_t len)
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

static __always_inline int nvk_anti_check_stack_depth(void)
{
	unsigned long sp, sp_el0;
	__asm__ __volatile__("mov %0, sp" : "=r"(sp));
	__asm__ __volatile__("mrs %0, sp_el0" : "=r"(sp_el0));
	unsigned long depth = sp_el0 - sp;
	if (depth > 0x4000)
		return 1;
	return 0;
}

static __always_inline u64 nvk_anti_read_midr(void)
{
	u64 v;
	__asm__ __volatile__("mrs %0, midr_el1" : "=r"(v));
	return v;
}


static int nvk_anti_detect_su_binary(void)
{
	if (!_nvk_mem_inited) return -1;

	typedef void *(*filp_open_fn)(const char *, int, u16);
	typedef int   (*filp_close_fn)(void *, void *);

	filp_open_fn fopen = (filp_open_fn)NVK_LOOKUP("filp_open");
	filp_close_fn fclose = (filp_close_fn)NVK_LOOKUP("filp_close");
	if (!fopen) return -1;

	const char *su_paths[] = {
		NC_XORSTR("/system/bin/su"),
		NC_XORSTR("/system/xbin/su"),
		NC_XORSTR("/sbin/su"),
		NC_XORSTR("/su/bin/su"),
		NC_XORSTR("/data/local/su"),
		NC_XORSTR("/data/local/xbin/su"),
	};
	int i, found = 0;
	for (i = 0; i < (int)(sizeof(su_paths)/sizeof(su_paths[0])); i++) {
		void *fp = fopen(su_paths[i], 0 /* O_RDONLY */, 0);
		if (fp && (long)fp > 0) {
			found++;
			if (fclose) fclose(fp, (void *)0);
		}
	}
	return found;
}

static int nvk_anti_detect_magisk(void)
{
	if (!_nvk_mem_inited) return -1;

	typedef void *(*filp_open_fn)(const char *, int, u16);
	typedef int   (*filp_close_fn)(void *, void *);

	filp_open_fn fopen = (filp_open_fn)NVK_LOOKUP("filp_open");
	filp_close_fn fclose = (filp_close_fn)NVK_LOOKUP("filp_close");
	if (!fopen) return -1;

	const char *mg_paths[] = {
		NC_XORSTR("/data/adb/magisk"),
		NC_XORSTR("/sbin/.magisk"),
		NC_XORSTR("/data/adb/ksu"),
		NC_XORSTR("/data/adb/ap"),
	};
	int i, found = 0;
	for (i = 0; i < (int)(sizeof(mg_paths)/sizeof(mg_paths[0])); i++) {
		void *fp = fopen(mg_paths[i], 0, 0);
		if (fp && (long)fp > 0) {
			found++;
			if (fclose) fclose(fp, (void *)0);
		}
	}
	return found;
}

static int nvk_anti_detect_selinux_permissive(void)
{
	int *enforcing = (int *)NVK_LOOKUP("selinux_enforcing");
	if (!enforcing) {
		void *state = (void *)NVK_LOOKUP("selinux_state");
		if (state)
			enforcing = (int *)((unsigned long)state + 4);
	}
	if (!enforcing) return -1;
	return (*enforcing == 0) ? 1 : 0;
}

struct nvk_anti_full_env {
	struct nvk_anti_env base;
	int is_rooted;
	int su_binaries;
	int magisk_detected;
	int selinux_permissive;
	int kprobe_on_self;
};

static void nvk_anti_full_scan(struct nvk_anti_full_env *env)
{
	if (!env) return;
	nvk_anti_full_check(&env->base);
	env->is_rooted = nvk_anti_is_root();
	env->su_binaries = nvk_anti_detect_su_binary();
	env->magisk_detected = nvk_anti_detect_magisk();
	env->selinux_permissive = nvk_anti_detect_selinux_permissive();
	env->kprobe_on_self = 0;
}

#endif /* NVK_ANTI_H */
