/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_ANTI_H
#define NEVERC_KRT_ANTI_H

#include <linux/types.h>
#include <linux/compiler.h>
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
int neverc_krt_anti_detect_hook_on(void *addr);
int neverc_krt_anti_detect_hook_ex(void *addr,
				   struct neverc_krt_hook *own_hooks,
				   int own_count);

int neverc_krt_anti_verify_text_integrity(const void *addr, size_t len,
					  u32 expected_crc);
u32 neverc_krt_anti_compute_crc32(const void *addr, size_t len);

u64 neverc_krt_anti_timestamp(void);
u64 neverc_krt_anti_timer_freq(void);
int neverc_krt_anti_timing_check(u64 start, u64 max_cycles);

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

int neverc_krt_wd_register(struct neverc_krt_hook *h);
int neverc_krt_wd_check(void);
int neverc_krt_wd_repair(void);
void neverc_krt_wd_unregister(struct neverc_krt_hook *h);
u64 neverc_krt_wd_checks(void);
u64 neverc_krt_wd_violations(void);
u64 neverc_krt_wd_tramp_violations(void);

int neverc_krt_anti_scan_for_brk(const void *start, size_t len);
int neverc_krt_anti_check_stack_depth(void);
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
