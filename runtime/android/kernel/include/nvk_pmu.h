/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_PMU_H
#define NEVERC_KRT_PMU_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>

#define NEVERC_KRT_PMU_EVT_SW_INCR        0x00
#define NEVERC_KRT_PMU_EVT_L1I_REFILL     0x01
#define NEVERC_KRT_PMU_EVT_L1I_TLB_REFILL 0x02
#define NEVERC_KRT_PMU_EVT_L1D_REFILL     0x03
#define NEVERC_KRT_PMU_EVT_L1D_ACCESS     0x04
#define NEVERC_KRT_PMU_EVT_L1D_TLB_REFILL 0x05
#define NEVERC_KRT_PMU_EVT_INST_RETIRED   0x08
#define NEVERC_KRT_PMU_EVT_EXC_TAKEN      0x09
#define NEVERC_KRT_PMU_EVT_EXC_RETURN     0x0A
#define NEVERC_KRT_PMU_EVT_BR_RETIRED     0x21
#define NEVERC_KRT_PMU_EVT_BR_MIS_PRED    0x10
#define NEVERC_KRT_PMU_EVT_CPU_CYCLES     0x11
#define NEVERC_KRT_PMU_EVT_BR_PRED        0x12
#define NEVERC_KRT_PMU_EVT_MEM_ACCESS     0x13
#define NEVERC_KRT_PMU_EVT_L2D_ACCESS     0x16
#define NEVERC_KRT_PMU_EVT_L2D_REFILL     0x17
#define NEVERC_KRT_PMU_EVT_CHAIN          0x1E
#define NEVERC_KRT_PMU_EVT_STALL_FRONTEND 0x23
#define NEVERC_KRT_PMU_EVT_STALL_BACKEND  0x24

static __always_inline void neverc_krt_pmu_enable(void)
{
	u64 val;
	__asm__ __volatile__("mrs %0, pmcr_el0" : "=r"(val));
	val |= 1UL;
	__asm__ __volatile__("msr pmcr_el0, %0" : : "r"(val));
	__asm__ __volatile__("isb");
}

static __always_inline void neverc_krt_pmu_disable(void)
{
	u64 val;
	__asm__ __volatile__("mrs %0, pmcr_el0" : "=r"(val));
	val &= ~1UL;
	__asm__ __volatile__("msr pmcr_el0, %0" : : "r"(val));
	__asm__ __volatile__("isb");
}

static __always_inline void neverc_krt_pmu_reset(void)
{
	u64 val;
	__asm__ __volatile__("mrs %0, pmcr_el0" : "=r"(val));
	val |= (1UL << 1) | (1UL << 2);
	__asm__ __volatile__("msr pmcr_el0, %0" : : "r"(val));
	__asm__ __volatile__("isb");
}

static __always_inline u64 neverc_krt_pmu_cycle_count(void)
{
	u64 val;
	__asm__ __volatile__("mrs %0, pmccntr_el0" : "=r"(val));
	return val;
}

static __always_inline void neverc_krt_pmu_cycle_enable(void)
{
	u64 val = (1UL << 31);
	__asm__ __volatile__("msr pmcntenset_el0, %0" : : "r"(val));
	__asm__ __volatile__("isb");
}

static __always_inline void neverc_krt_pmu_cycle_disable(void)
{
	u64 val = (1UL << 31);
	__asm__ __volatile__("msr pmcntenclr_el0, %0" : : "r"(val));
	__asm__ __volatile__("isb");
}

static __always_inline int neverc_krt_pmu_counter_count(void)
{
	u64 pmcr;
	__asm__ __volatile__("mrs %0, pmcr_el0" : "=r"(pmcr));
	return (int)((pmcr >> 11) & 0x1F);
}

static __always_inline void neverc_krt_pmu_counter_setup(int idx, u32 event)
{
	u64 sel = (u64)idx;
	__asm__ __volatile__("msr pmselr_el0, %0" : : "r"(sel));
	__asm__ __volatile__("isb");
	u64 evt = (u64)event;
	__asm__ __volatile__("msr pmxevtyper_el0, %0" : : "r"(evt));
	__asm__ __volatile__("isb");
}

static __always_inline void neverc_krt_pmu_counter_enable(int idx)
{
	u64 val = (1UL << idx);
	__asm__ __volatile__("msr pmcntenset_el0, %0" : : "r"(val));
	__asm__ __volatile__("isb");
}

static __always_inline void neverc_krt_pmu_counter_disable(int idx)
{
	u64 val = (1UL << idx);
	__asm__ __volatile__("msr pmcntenclr_el0, %0" : : "r"(val));
	__asm__ __volatile__("isb");
}

static __always_inline u64 neverc_krt_pmu_counter_read(int idx)
{
	u64 sel = (u64)idx;
	__asm__ __volatile__("msr pmselr_el0, %0" : : "r"(sel));
	__asm__ __volatile__("isb");
	u64 val;
	__asm__ __volatile__("mrs %0, pmxevcntr_el0" : "=r"(val));
	return val;
}

struct neverc_krt_pmu_session {
	u64 start_cycles;
	u64 start_cnt[4];
	u32 events[4];
	int num_counters;
};

void neverc_krt_pmu_session_start(struct neverc_krt_pmu_session *s,
				  const u32 *events, int count);


struct neverc_krt_pmu_result {
	u64 cycles;
	u64 counters[4];
	u32 events[4];
	int num_counters;
};

void neverc_krt_pmu_session_stop(struct neverc_krt_pmu_session *s,
				 struct neverc_krt_pmu_result *r);


static __always_inline u64 neverc_krt_pmu_rdtsc(void)
{
	u64 val;
	__asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(val));
	return val;
}

static __always_inline u64 neverc_krt_pmu_freq(void)
{
	u64 val;
	__asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(val));
	return val;
}

static __always_inline u64 neverc_krt_pmu_ns_elapsed(u64 start, u64 end)
{
	u64 freq = neverc_krt_pmu_freq();
	if (!freq) return 0;
	return (end - start) * 1000000000ULL / freq;
}

#define NEVERC_KRT_PMU_BENCH(name, code_block)                                  \
	do {                                                             \
		u64 __start = neverc_krt_pmu_rdtsc();                          \
		code_block;                                              \
		u64 __end = neverc_krt_pmu_rdtsc();                            \
		u64 __ns = neverc_krt_pmu_ns_elapsed(__start, __end);          \
		(void)__ns;                                              \
	} while (0)

#endif /* NEVERC_KRT_PMU_H */
