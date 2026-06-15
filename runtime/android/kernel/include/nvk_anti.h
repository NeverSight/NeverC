/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_ANTI_H
#define NVK_ANTI_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <linux/string.h>
#include <nvk_mem.h>
#include <nvk_process.h>

static __always_inline int nvk_anti_is_root(void)
{
	unsigned long cred;
	__asm__ __volatile__("mrs %0, sp_el0" : "=r"(cred));
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

static int nvk_anti_detect_hook_on(void *addr)
{
	u32 insn;
	if (nvk_mem_read(&insn, addr, 4))
		return -1;

	if (insn == 0x58000050U)
		return 1;

	u32 insn2;
	if (nvk_mem_read(&insn2, (char *)addr + 4, 4))
		return -1;
	if (insn2 == 0x58000050U)
		return 1;

	return 0;
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

#endif /* NVK_ANTI_H */
