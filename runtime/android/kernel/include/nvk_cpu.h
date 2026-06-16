/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_CPU_H
#define NVK_CPU_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <nvk_mem.h>

/* ------------------------------------------------------------------ */
/*  CPU topology queries  (no kernel symbol needed — all from regs)   */
/* ------------------------------------------------------------------ */

static __always_inline u32 nvk_cpu_id(void)
{
	u64 mpidr;
	__asm__ __volatile__("mrs %0, mpidr_el1" : "=r"(mpidr));
	return (u32)(mpidr & 0xFFUL);
}

static __always_inline u32 nvk_cpu_cluster(void)
{
	u64 mpidr;
	__asm__ __volatile__("mrs %0, mpidr_el1" : "=r"(mpidr));
	return (u32)((mpidr >> 8) & 0xFFUL);
}

static __always_inline u64 nvk_cpu_midr(void)
{
	u64 midr;
	__asm__ __volatile__("mrs %0, midr_el1" : "=r"(midr));
	return midr;
}

static __always_inline int nvk_cpu_is_big_core(void)
{
	u64 midr = nvk_cpu_midr();
	u32 part = (u32)((midr >> 4) & 0xFFF);
	/*
	 * ARM big cores: A72=0xD08, A73=0xD09, A75=0xD0A, A76=0xD0B,
	 * A77=0xD0D, A78=0xD41, X1=0xD44, A710=0xD47, X2=0xD48,
	 * A715=0xD4B, X3=0xD4E, A720=0xD81, X4=0xD82
	 */
	if (part >= 0xD08 && part <= 0xD0D) return 1;  /* A72–A77 */
	if (part >= 0xD40) return 1;                    /* A78+ / X-series */
	return 0;
}


/* ------------------------------------------------------------------ */
/*  Online CPU enumeration                                            */
/* ------------------------------------------------------------------ */

typedef int (*nvk_nr_cpu_ids_fn)(void);
typedef int (*nvk_cpu_online_fn)(unsigned int cpu);

static nvk_nr_cpu_ids_fn    _nvk_nr_cpus;
static nvk_cpu_online_fn    _nvk_cpu_online_check;
static unsigned long       *_nvk_cpu_online_mask;
static int                 *_nvk_nr_cpu_ids_ptr;
static int                  _nvk_cpu_inited;

static int nvk_cpu_init(void)
{
	if (_nvk_cpu_inited) return 0;

	_nvk_nr_cpu_ids_ptr = (int *)NVK_LOOKUP("nr_cpu_ids");
	_nvk_cpu_online_mask =
		(unsigned long *)NVK_LOOKUP("__cpu_online_mask");
	if (!_nvk_cpu_online_mask)
		_nvk_cpu_online_mask =
			(unsigned long *)NVK_LOOKUP("cpu_online_mask");
	_nvk_cpu_online_check =
		(nvk_cpu_online_fn)NVK_LOOKUP("cpu_online");

	_nvk_cpu_inited = 1;
	return 0;
}

static int nvk_num_possible_cpus(void)
{
	if (_nvk_nr_cpu_ids_ptr) return *_nvk_nr_cpu_ids_ptr;
	return 8;
}

static int nvk_cpu_is_online(int cpu)
{
	if (_nvk_cpu_online_check)
		return _nvk_cpu_online_check((unsigned int)cpu);
	if (_nvk_cpu_online_mask) {
		int word = cpu / 64;
		int bit  = cpu % 64;
		unsigned long val;
		if (nvk_mem_read(&val, &_nvk_cpu_online_mask[word], 8))
			return 0;
		return (val >> bit) & 1;
	}
	return 1;
}

static int nvk_num_online_cpus(void)
{
	int n = nvk_num_possible_cpus();
	int count = 0, i;
	for (i = 0; i < n; i++) {
		if (nvk_cpu_is_online(i))
			count++;
	}
	return count;
}

#define nvk_for_each_online_cpu(cpu)                                  \
	for ((cpu) = 0; (cpu) < nvk_num_possible_cpus(); (cpu)++)     \
		if (nvk_cpu_is_online(cpu))


/* ------------------------------------------------------------------ */
/*  Per-CPU data (lightweight, module-local)                          */
/* ------------------------------------------------------------------ */

#define NVK_MAX_CPUS 16

#define NVK_DEFINE_PER_CPU(type, name)                                \
	static type __nvk_pcpu_##name[NVK_MAX_CPUS]                  \
		__attribute__((aligned(64)))

static __always_inline u32 _nvk_cpu_idx_safe(u32 cpu)
{
	return cpu < NVK_MAX_CPUS ? cpu : 0;
}

#define nvk_this_cpu(name)                                            \
	__nvk_pcpu_##name[_nvk_cpu_idx_safe(nvk_cpu_id())]

#define nvk_per_cpu(name, cpu)                                        \
	__nvk_pcpu_##name[_nvk_cpu_idx_safe(cpu)]

#define nvk_per_cpu_ptr(name, cpu)                                    \
	(&__nvk_pcpu_##name[_nvk_cpu_idx_safe(cpu)])


/* ------------------------------------------------------------------ */
/*  SMP utilities                                                     */
/* ------------------------------------------------------------------ */

typedef void (*nvk_smp_call_fn)(void *info);
typedef void (*nvk_on_each_cpu_fn)(nvk_smp_call_fn func, void *info,
				   int wait);
typedef int  (*nvk_smp_call_single_fn)(int cpu, nvk_smp_call_fn func,
				       void *info, int wait);

static nvk_on_each_cpu_fn     _nvk_on_each_cpu;
static nvk_smp_call_single_fn _nvk_smp_call_single;

static int nvk_smp_init(void)
{
	_nvk_on_each_cpu =
		(nvk_on_each_cpu_fn)NVK_LOOKUP("on_each_cpu");
	_nvk_smp_call_single =
		(nvk_smp_call_single_fn)NVK_LOOKUP("smp_call_function_single");
	return (_nvk_on_each_cpu || _nvk_smp_call_single) ? 0 : -1;
}

static int nvk_smp_on_each(nvk_smp_call_fn func, void *info, int wait)
{
	if (!_nvk_on_each_cpu) return -1;
	_nvk_on_each_cpu(func, info, wait);
	return 0;
}

static int nvk_smp_call_on(int cpu, nvk_smp_call_fn func,
			   void *info, int wait)
{
	if (!_nvk_smp_call_single) return -1;
	return _nvk_smp_call_single(cpu, func, info, wait);
}


/* ------------------------------------------------------------------ */
/*  CPU feature detection  (ID_AA64* register reads)                  */
/* ------------------------------------------------------------------ */

static __always_inline int nvk_cpu_has_crc32(void)
{
	u64 isar0;
	__asm__ __volatile__("mrs %0, id_aa64isar0_el1" : "=r"(isar0));
	return ((isar0 >> 16) & 0xF) >= 1;
}

static __always_inline int nvk_cpu_has_sha256(void)
{
	u64 isar0;
	__asm__ __volatile__("mrs %0, id_aa64isar0_el1" : "=r"(isar0));
	return ((isar0 >> 12) & 0xF) >= 1;
}

static __always_inline int nvk_cpu_has_aes(void)
{
	u64 isar0;
	__asm__ __volatile__("mrs %0, id_aa64isar0_el1" : "=r"(isar0));
	return ((isar0 >> 4) & 0xF) >= 1;
}

static __always_inline int nvk_cpu_has_atomics(void)
{
	u64 isar0;
	__asm__ __volatile__("mrs %0, id_aa64isar0_el1" : "=r"(isar0));
	return ((isar0 >> 20) & 0xF) >= 2;
}

static __always_inline int nvk_cpu_has_sve(void)
{
	u64 pfr0;
	__asm__ __volatile__("mrs %0, id_aa64pfr0_el1" : "=r"(pfr0));
	return ((pfr0 >> 32) & 0xF) >= 1;
}

#endif /* NVK_CPU_H */
