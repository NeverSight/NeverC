/* SPDX-License-Identifier: GPL-2.0 */
/* nvk_pmu.c — implementations extracted from nvk_pmu.h. */
#include <nvk.h>

void nvk_pmu_session_start(struct nvk_pmu_session *s,
				  const u32 *events, int count)
{
	int i;
	if (!s) return;
	if (count > 4) count = 4;
	if (count > nvk_pmu_counter_count())
		count = nvk_pmu_counter_count();

	s->num_counters = count;
	nvk_pmu_reset();
	nvk_pmu_enable();
	nvk_pmu_cycle_enable();

	for (i = 0; i < count; i++) {
		s->events[i] = events[i];
		nvk_pmu_counter_setup(i, events[i]);
		nvk_pmu_counter_enable(i);
		s->start_cnt[i] = nvk_pmu_counter_read(i);
	}

	s->start_cycles = nvk_pmu_cycle_count();
}

void nvk_pmu_session_stop(struct nvk_pmu_session *s,
				 struct nvk_pmu_result *r)
{
	int i;
	if (!s || !r) return;

	r->cycles = nvk_pmu_cycle_count() - s->start_cycles;
	r->num_counters = s->num_counters;

	for (i = 0; i < s->num_counters; i++) {
		r->counters[i] = nvk_pmu_counter_read(i) - s->start_cnt[i];
		r->events[i] = s->events[i];
		nvk_pmu_counter_disable(i);
	}

	nvk_pmu_cycle_disable();
	nvk_pmu_disable();
}

