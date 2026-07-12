/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>

#define NEVERC_KRT_PMU_FORCE_INLINE __attribute__((always_inline))

NEVERC_KRT_PMU_FORCE_INLINE void neverc_krt_pmu_enable(void)
{
	u64 value;

	__asm__ __volatile__("mrs %0, pmcr_el0" : "=r"(value));
	value |= 1UL;
	__asm__ __volatile__("msr pmcr_el0, %0" : : "r"(value));
	__asm__ __volatile__("isb");
}

NEVERC_KRT_PMU_FORCE_INLINE void neverc_krt_pmu_disable(void)
{
	u64 value;

	__asm__ __volatile__("mrs %0, pmcr_el0" : "=r"(value));
	value &= ~1UL;
	__asm__ __volatile__("msr pmcr_el0, %0" : : "r"(value));
	__asm__ __volatile__("isb");
}

NEVERC_KRT_PMU_FORCE_INLINE void neverc_krt_pmu_reset(void)
{
	u64 value;

	__asm__ __volatile__("mrs %0, pmcr_el0" : "=r"(value));
	value |= (1UL << 1) | (1UL << 2);
	__asm__ __volatile__("msr pmcr_el0, %0" : : "r"(value));
	__asm__ __volatile__("isb");
}

NEVERC_KRT_PMU_FORCE_INLINE u64 neverc_krt_pmu_cycle_count(void)
{
	u64 value;

	__asm__ __volatile__("mrs %0, pmccntr_el0" : "=r"(value));
	return value;
}

NEVERC_KRT_PMU_FORCE_INLINE void neverc_krt_pmu_cycle_enable(void)
{
	u64 value = 1UL << 31;

	__asm__ __volatile__("msr pmcntenset_el0, %0" : : "r"(value));
	__asm__ __volatile__("isb");
}

NEVERC_KRT_PMU_FORCE_INLINE void neverc_krt_pmu_cycle_disable(void)
{
	u64 value = 1UL << 31;

	__asm__ __volatile__("msr pmcntenclr_el0, %0" : : "r"(value));
	__asm__ __volatile__("isb");
}

NEVERC_KRT_PMU_FORCE_INLINE int neverc_krt_pmu_counter_count(void)
{
	u64 value;

	__asm__ __volatile__("mrs %0, pmcr_el0" : "=r"(value));
	return (int)((value >> 11) & 0x1FUL);
}

NEVERC_KRT_PMU_FORCE_INLINE void
neverc_krt_pmu_counter_setup(int idx, u32 event)
{
	u64 selector = (u64)idx;
	u64 event_type = (u64)event;

	__asm__ __volatile__("msr pmselr_el0, %0" : : "r"(selector));
	__asm__ __volatile__("isb");
	__asm__ __volatile__("msr pmxevtyper_el0, %0" : : "r"(event_type));
	__asm__ __volatile__("isb");
}

NEVERC_KRT_PMU_FORCE_INLINE void neverc_krt_pmu_counter_enable(int idx)
{
	u64 value = 1UL << idx;

	__asm__ __volatile__("msr pmcntenset_el0, %0" : : "r"(value));
	__asm__ __volatile__("isb");
}

NEVERC_KRT_PMU_FORCE_INLINE void neverc_krt_pmu_counter_disable(int idx)
{
	u64 value = 1UL << idx;

	__asm__ __volatile__("msr pmcntenclr_el0, %0" : : "r"(value));
	__asm__ __volatile__("isb");
}

NEVERC_KRT_PMU_FORCE_INLINE u64 neverc_krt_pmu_counter_read(int idx)
{
	u64 selector = (u64)idx;
	u64 value;

	__asm__ __volatile__("msr pmselr_el0, %0" : : "r"(selector));
	__asm__ __volatile__("isb");
	__asm__ __volatile__("mrs %0, pmxevcntr_el0" : "=r"(value));
	return value;
}

void neverc_krt_pmu_session_start(struct neverc_krt_pmu_session *s,
				  const u32 *events, int count)
{
	int i;
	if (!s) return;
	if (count > 4) count = 4;
	if (count > neverc_krt_pmu_counter_count())
		count = neverc_krt_pmu_counter_count();

	s->num_counters = count;
	neverc_krt_pmu_reset();
	neverc_krt_pmu_enable();
	neverc_krt_pmu_cycle_enable();

	for (i = 0; i < count; i++) {
		s->events[i] = events[i];
		neverc_krt_pmu_counter_setup(i, events[i]);
		neverc_krt_pmu_counter_enable(i);
		s->start_cnt[i] = neverc_krt_pmu_counter_read(i);
	}

	s->start_cycles = neverc_krt_pmu_cycle_count();
}

void neverc_krt_pmu_session_stop(struct neverc_krt_pmu_session *s,
				 struct neverc_krt_pmu_result *r)
{
	int i;
	if (!s || !r) return;

	r->cycles = neverc_krt_pmu_cycle_count() - s->start_cycles;
	r->num_counters = s->num_counters;

	for (i = 0; i < s->num_counters; i++) {
		r->counters[i] = neverc_krt_pmu_counter_read(i) - s->start_cnt[i];
		r->events[i] = s->events[i];
		neverc_krt_pmu_counter_disable(i);
	}

	neverc_krt_pmu_cycle_disable();
	neverc_krt_pmu_disable();
}

u64 neverc_krt_pmu_rdtsc(void)
{
	return neverc_krt_arch_counter();
}

u64 neverc_krt_pmu_freq(void)
{
	return (u64)neverc_krt_arch_counter_freq();
}

u64 neverc_krt_pmu_ns_elapsed(u64 start, u64 end)
{
	return neverc_krt_arch_counter_to_ns(end - start);
}

#undef NEVERC_KRT_PMU_FORCE_INLINE
