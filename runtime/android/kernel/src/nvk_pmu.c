/* SPDX-License-Identifier: GPL-2.0 */
/* neverc_krt_pmu.c — implementations extracted from neverc_krt_pmu.h. */
#include <nvk.h>

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

