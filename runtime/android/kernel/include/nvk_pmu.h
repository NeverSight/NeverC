/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_PMU_H
#define NEVERC_KRT_PMU_H

#include <linux/types.h>
#include <nvk_timer.h>

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

void neverc_krt_pmu_enable(void);
void neverc_krt_pmu_disable(void);
void neverc_krt_pmu_reset(void);
u64 neverc_krt_pmu_cycle_count(void);
void neverc_krt_pmu_cycle_enable(void);
void neverc_krt_pmu_cycle_disable(void);
int neverc_krt_pmu_counter_count(void);
void neverc_krt_pmu_counter_setup(int idx, u32 event);
void neverc_krt_pmu_counter_enable(int idx);
void neverc_krt_pmu_counter_disable(int idx);
u64 neverc_krt_pmu_counter_read(int idx);

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

u64 neverc_krt_pmu_rdtsc(void);
u64 neverc_krt_pmu_freq(void);
u64 neverc_krt_pmu_ns_elapsed(u64 start, u64 end);

#define NEVERC_KRT_PMU_BENCH(name, code_block)                                  \
	do {                                                             \
		u64 __start = neverc_krt_pmu_rdtsc();                          \
		code_block;                                              \
		u64 __end = neverc_krt_pmu_rdtsc();                            \
		u64 __ns = neverc_krt_pmu_ns_elapsed(__start, __end);          \
		(void)__ns;                                              \
	} while (0)

#endif /* NEVERC_KRT_PMU_H */
