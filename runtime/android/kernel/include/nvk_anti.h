/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_ANTI_H
#define NEVERC_KRT_ANTI_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <linux/string.h>
#include <nvk_mem.h>
#include <nvk_process.h>
#include <nvk_addr.h>
#include <nvk_hook.h>

int neverc_krt_anti_is_root(void);


int neverc_krt_anti_check_caller_comm(const char *expected);


int neverc_krt_anti_check_caller_uid(u32 expected_uid);


enum neverc_krt_env_type {
	NEVERC_KRT_ENV_NORMAL   = 0,
	NEVERC_KRT_ENV_EMULATOR = 1,
	NEVERC_KRT_ENV_DEBUGGER = 2,
	NEVERC_KRT_ENV_ROOTED   = 3,
};

int neverc_krt_anti_detect_emulator(void);


int neverc_krt_anti_detect_debugger(void);


int neverc_krt_anti_detect_kprobe_on(void *addr);


NEVERC_KRT_RT_VAR int neverc_krt_anti_detect_hook_ex(void *addr,
				   struct neverc_krt_hook *own_hooks,
				   int own_count);

int neverc_krt_anti_detect_hook_on(void *addr);


int neverc_krt_anti_detect_hook_ex(void *addr,
				   struct neverc_krt_hook *own_hooks,
				   int own_count);


static __always_inline int _neverc_krt_has_crc32_hw(void)
{
	u64 isar0;
	__asm__ __volatile__("mrs %0, id_aa64isar0_el1" : "=r"(isar0));
	return ((isar0 >> 16) & 0xF) >= 1;
}

static __always_inline u32 _neverc_krt_crc32_hw_byte(u32 crc, u8 val)
{
	u32 result;
	__asm__("crc32b %w0, %w1, %w2" : "=r"(result) : "r"(crc), "r"(val));
	return result;
}

static __always_inline u32 _neverc_krt_crc32_hw_word(u32 crc, u32 val)
{
	u32 result;
	__asm__("crc32w %w0, %w1, %w2" : "=r"(result) : "r"(crc), "r"(val));
	return result;
}

static __always_inline u32 _neverc_krt_crc32_hw_dword(u32 crc, u64 val)
{
	u32 result;
	__asm__("crc32x %w0, %w1, %2" : "=r"(result) : "r"(crc), "r"(val));
	return result;
}

u32 _neverc_krt_crc32_sw(u32 crc, const unsigned char *p, size_t len);


u32 _neverc_krt_crc32_hw(u32 crc, const unsigned char *p, size_t len);


u32 _neverc_krt_crc32_auto(const void *addr, size_t len);


int neverc_krt_anti_verify_text_integrity(const void *addr, size_t len,
					  u32 expected_crc);


u32 neverc_krt_anti_compute_crc32(const void *addr, size_t len);


static __always_inline u64 neverc_krt_anti_timestamp(void)
{
	u64 v;
	__asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(v));
	return v;
}

static __always_inline u64 neverc_krt_anti_timer_freq(void)
{
	u64 v;
	__asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(v));
	return v;
}

static __always_inline int neverc_krt_anti_timing_check(u64 start, u64 max_cycles)
{
	u64 now = neverc_krt_anti_timestamp();
	return (now - start) > max_cycles;
}

int neverc_krt_anti_detect_trace(void);


int neverc_krt_anti_detect_virtualization(void);


int neverc_krt_anti_check_fn_patched(void *addr, int insn_count);


struct neverc_krt_anti_env {
	int is_emulator;
	int is_debugger;
	int is_trace;
	int is_virtual;
	u64 va_bits;
	u64 page_size;
	u64 timer_freq;
};

void neverc_krt_anti_full_check(struct neverc_krt_anti_env *env);



/* --- Watchdog: periodic hook integrity check with sealed storage --- */

#define NEVERC_KRT_WD_MAX_HOOKS 16

NEVERC_KRT_RT_VAR u64 _neverc_krt_wd_seal_key;

static __always_inline u64 _neverc_krt_wd_gen_key(void)
{
	u64 ts, ctr;
	__asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(ts));
	__asm__ __volatile__("mrs %0, cntpct_el0" : "=r"(ctr));
	u64 sp;
	__asm__ __volatile__("mov %0, sp" : "=r"(sp));
	return ts ^ (ctr * 0x9E3779B97F4A7C15ULL) ^ (sp >> 3);
}

static __always_inline u32 _neverc_krt_wd_seal(u32 val, int slot)
{
	u32 k = (u32)(_neverc_krt_wd_seal_key >> (slot & 1 ? 32 : 0));
	return val ^ k ^ (u32)(slot * 0x45D9F3BU);
}

static __always_inline u32 _neverc_krt_wd_unseal(u32 val, int slot)
{
	return _neverc_krt_wd_seal(val, slot);
}

struct neverc_krt_watchdog_entry {
	struct neverc_krt_hook *hook;
	u32              sealed_orig[NEVERC_KRT_HOOK_MAX_PATCH];
	u32              sealed_expect[NEVERC_KRT_HOOK_MAX_PATCH];
	u32              tramp_crc;
	int              tramp_len;
	int              patch_count;
};

struct neverc_krt_watchdog {
	struct neverc_krt_watchdog_entry entries[NEVERC_KRT_WD_MAX_HOOKS];
	int                       count;
	volatile u64              check_count;
	volatile u64              violation_count;
	volatile u64              tramp_violations;
	volatile int              running;
};

NEVERC_KRT_RT_VAR struct neverc_krt_watchdog _neverc_krt_wd;

u32 _neverc_krt_wd_crc32(const void *data, int len);


int neverc_krt_wd_register(struct neverc_krt_hook *h);


int neverc_krt_wd_check(void);


int neverc_krt_wd_repair(void);


void neverc_krt_wd_unregister(struct neverc_krt_hook *h);


static __always_inline u64 neverc_krt_wd_checks(void)
{
	return __atomic_load_n(&_neverc_krt_wd.check_count, __ATOMIC_RELAXED);
}

static __always_inline u64 neverc_krt_wd_violations(void)
{
	return __atomic_load_n(&_neverc_krt_wd.violation_count, __ATOMIC_RELAXED);
}

static __always_inline u64 neverc_krt_wd_tramp_violations(void)
{
	return __atomic_load_n(&_neverc_krt_wd.tramp_violations, __ATOMIC_RELAXED);
}


int neverc_krt_anti_scan_for_brk(const void *start, size_t len);


static __always_inline int neverc_krt_anti_check_stack_depth(void)
{
	unsigned long sp, sp_el0;
	__asm__ __volatile__("mov %0, sp" : "=r"(sp));
	__asm__ __volatile__("mrs %0, sp_el0" : "=r"(sp_el0));
	unsigned long depth = sp_el0 - sp;
	if (depth > 0x4000)
		return 1;
	return 0;
}

static __always_inline u64 neverc_krt_anti_read_midr(void)
{
	u64 v;
	__asm__ __volatile__("mrs %0, midr_el1" : "=r"(v));
	return v;
}


int _neverc_krt_try_open_path(void *(*fopen)(const char *, int, u16),
			      int (*fclose)(void *, void *),
			      const char *path);


int neverc_krt_anti_detect_su_binary(void);


int neverc_krt_anti_detect_magisk(void);


int neverc_krt_anti_detect_selinux_permissive(void);


struct neverc_krt_anti_full_env {
	struct neverc_krt_anti_env base;
	int is_rooted;
	int su_binaries;
	int magisk_detected;
	int selinux_permissive;
	int kprobe_on_self;
};

void neverc_krt_anti_full_scan(struct neverc_krt_anti_full_env *env);


#endif /* NEVERC_KRT_ANTI_H */
