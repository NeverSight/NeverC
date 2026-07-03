/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_CPU_H
#define NEVERC_KRT_CPU_H

#include <linux/types.h>
#include <linux/compiler.h>

/* ------------------------------------------------------------------ */
/*  CPU topology queries  (no kernel symbol needed — all from regs)   */
/* ------------------------------------------------------------------ */

__always_inline u32 neverc_krt_cpu_id(void)
{
	u64 mpidr;
	__asm__ __volatile__("mrs %0, mpidr_el1" : "=r"(mpidr));
	return (u32)(mpidr & 0xFFUL);
}

__always_inline u32 neverc_krt_cpu_cluster(void)
{
	u64 mpidr;
	__asm__ __volatile__("mrs %0, mpidr_el1" : "=r"(mpidr));
	return (u32)((mpidr >> 8) & 0xFFUL);
}

__always_inline u64 neverc_krt_cpu_midr(void)
{
	u64 midr;
	__asm__ __volatile__("mrs %0, midr_el1" : "=r"(midr));
	return midr;
}

__always_inline int neverc_krt_cpu_is_big_core(void)
{
	u64 midr = neverc_krt_cpu_midr();
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

int neverc_krt_cpu_init(void);
int neverc_krt_num_possible_cpus(void);
int neverc_krt_cpu_is_online(int cpu);
int neverc_krt_num_online_cpus(void);


#define neverc_krt_for_each_online_cpu(cpu)                                  \
	for ((cpu) = 0; (cpu) < neverc_krt_num_possible_cpus(); (cpu)++)     \
		if (neverc_krt_cpu_is_online(cpu))


/* ------------------------------------------------------------------ */
/*  Per-CPU data (lightweight, module-local)                          */
/* ------------------------------------------------------------------ */

#define NEVERC_KRT_MAX_CPUS 32

#define NEVERC_KRT_DEFINE_PER_CPU(type, name)                                \
	type __neverc_krt_pcpu_##name[NEVERC_KRT_MAX_CPUS]                  \
		__attribute__((aligned(64)))

__always_inline u32 _neverc_krt_cpu_idx_safe(u32 cpu)
{
	return (cpu < NEVERC_KRT_MAX_CPUS) ? cpu : (NEVERC_KRT_MAX_CPUS - 1);
}


#define neverc_krt_this_cpu(name)                                            \
	__neverc_krt_pcpu_##name[_neverc_krt_cpu_idx_safe(neverc_krt_cpu_id())]

#define neverc_krt_per_cpu(name, cpu)                                        \
	__neverc_krt_pcpu_##name[_neverc_krt_cpu_idx_safe(cpu)]

#define neverc_krt_per_cpu_ptr(name, cpu)                                    \
	(&__neverc_krt_pcpu_##name[_neverc_krt_cpu_idx_safe(cpu)])


/* ------------------------------------------------------------------ */
/*  SMP utilities                                                     */
/* ------------------------------------------------------------------ */

typedef void (*neverc_krt_smp_call_fn)(void *info);

int neverc_krt_smp_init(void);
int neverc_krt_smp_on_each(neverc_krt_smp_call_fn func, void *info, int wait);
int neverc_krt_smp_call_on(int cpu, neverc_krt_smp_call_fn func,
			   void *info, int wait);

/* ------------------------------------------------------------------ */
/*  CPU feature detection  (ID_AA64* register reads)                  */
/* ------------------------------------------------------------------ */

__always_inline int neverc_krt_cpu_has_crc32(void)
{
	u64 isar0;
	__asm__ __volatile__("mrs %0, id_aa64isar0_el1" : "=r"(isar0));
	return ((isar0 >> 16) & 0xF) >= 1;
}

__always_inline int neverc_krt_cpu_has_sha256(void)
{
	u64 isar0;
	__asm__ __volatile__("mrs %0, id_aa64isar0_el1" : "=r"(isar0));
	return ((isar0 >> 12) & 0xF) >= 1;
}

__always_inline int neverc_krt_cpu_has_aes(void)
{
	u64 isar0;
	__asm__ __volatile__("mrs %0, id_aa64isar0_el1" : "=r"(isar0));
	return ((isar0 >> 4) & 0xF) >= 1;
}

__always_inline int neverc_krt_cpu_has_atomics(void)
{
	u64 isar0;
	__asm__ __volatile__("mrs %0, id_aa64isar0_el1" : "=r"(isar0));
	return ((isar0 >> 20) & 0xF) >= 2;
}

__always_inline int neverc_krt_cpu_has_sve(void)
{
	u64 pfr0;
	__asm__ __volatile__("mrs %0, id_aa64pfr0_el1" : "=r"(pfr0));
	return ((pfr0 >> 32) & 0xF) >= 1;
}


/* ------------------------------------------------------------------ */
/*  Security / pointer-auth / branch-target feature detection         */
/* ------------------------------------------------------------------ */

__always_inline int neverc_krt_has_pac(void)
{
	unsigned long isar1;
	__asm__ __volatile__("mrs %0, id_aa64isar1_el1" : "=r"(isar1));
	int apa = (isar1 >> 4) & 0xF;
	int api = (isar1 >> 8) & 0xF;
	return (apa | api) != 0;
}

__always_inline int neverc_krt_has_bti(void)
{
	unsigned long pfr1;
	__asm__ __volatile__("mrs %0, id_aa64pfr1_el1" : "=r"(pfr1));
	return (pfr1 & 0xF) != 0;
}

__always_inline int neverc_krt_has_mte(void)
{
	unsigned long pfr1;
	__asm__ __volatile__("mrs %0, id_aa64pfr1_el1" : "=r"(pfr1));
	return ((pfr1 >> 8) & 0xF) >= 2;
}

__always_inline int neverc_krt_has_epac(void)
{
	unsigned long isar1;
	__asm__ __volatile__("mrs %0, id_aa64isar1_el1" : "=r"(isar1));
	int apa = (isar1 >> 4) & 0xF;
	int api = (isar1 >> 8) & 0xF;
	return (apa >= 2) || (api >= 2);
}

__always_inline int neverc_krt_has_fpac(void)
{
	unsigned long isar1;
	__asm__ __volatile__("mrs %0, id_aa64isar1_el1" : "=r"(isar1));
	int apa = (isar1 >> 4) & 0xF;
	int api = (isar1 >> 8) & 0xF;
	return (apa >= 3) || (api >= 3);
}

/* ------------------------------------------------------------------ */
/*  Aggregate HW capability snapshot                                  */
/* ------------------------------------------------------------------ */

struct neverc_krt_hw_caps {
	int pac;
	int epac;
	int fpac;
	int bti;
	int mte;
	int sve;
	int cfi;
};

void neverc_krt_detect_hw_caps(struct neverc_krt_hw_caps *caps);

#endif /* NEVERC_KRT_CPU_H */
