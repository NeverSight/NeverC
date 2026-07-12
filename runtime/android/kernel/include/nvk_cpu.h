/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_CPU_H
#define NEVERC_KRT_CPU_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <asm/smp.h>
#include <nvkmod_version.h>

/* ------------------------------------------------------------------ */
/*  CPU topology queries  (no kernel symbol needed — all from regs)   */
/* ------------------------------------------------------------------ */

static __always_inline u32 neverc_krt_cpu_id(void)
{
	return (u32)raw_smp_processor_id();
}

u32 neverc_krt_cpu_cluster(void);
u64 neverc_krt_cpu_midr(void);
int neverc_krt_cpu_is_big_core(void);


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

#define NEVERC_KRT_MAX_CPUS NEVERC_KRT_NR_CPUS

#define NEVERC_KRT_DEFINE_PER_CPU(type, name)                                \
	type __neverc_krt_pcpu_##name[NEVERC_KRT_MAX_CPUS]                  \
		__attribute__((aligned(64)))

static __always_inline u32 _neverc_krt_cpu_idx_safe(u32 cpu)
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

int neverc_krt_cpu_has_crc32(void);
int neverc_krt_cpu_has_sha256(void);
int neverc_krt_cpu_has_aes(void);
int neverc_krt_cpu_has_atomics(void);
int neverc_krt_cpu_has_sve(void);


/* ------------------------------------------------------------------ */
/*  Security / pointer-auth / branch-target feature detection         */
/* ------------------------------------------------------------------ */

int neverc_krt_has_pac(void);
int neverc_krt_has_bti(void);
int neverc_krt_has_mte(void);
int neverc_krt_has_epac(void);
int neverc_krt_has_fpac(void);

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
