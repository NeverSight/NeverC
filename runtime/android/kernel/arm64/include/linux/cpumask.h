/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_CPUMASK_H
#define _NEVERC_KRT_LINUX_CPUMASK_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/atomic.h>
#include <linux/bitops.h>
#include <asm/smp.h>
#include <nvkmod_version.h>

#ifndef NR_CPUS
#define NR_CPUS NEVERC_KRT_NR_CPUS
#endif
#define NR_CPUS_MAX NR_CPUS

struct cpumask {
	unsigned long bits[NR_CPUS / BITS_PER_LONG + !!(NR_CPUS % BITS_PER_LONG)];
};
typedef struct cpumask cpumask_t;

_Static_assert(NR_CPUS == NEVERC_KRT_NR_CPUS,
	       "NR_CPUS must match the selected GKI profile");

extern unsigned int nr_cpu_ids;
extern struct cpumask __cpu_possible_mask;
extern struct cpumask __cpu_online_mask;
extern atomic_t __num_online_cpus;

#define cpu_possible_mask ((const struct cpumask *)&__cpu_possible_mask)
#define cpu_online_mask ((const struct cpumask *)&__cpu_online_mask)

static __always_inline bool cpumask_test_cpu(unsigned int cpu,
					     const struct cpumask *mask)
{
	return cpu < nr_cpu_ids &&
	       !!(READ_ONCE(mask->bits[cpu / BITS_PER_LONG]) &
		  (1UL << (cpu % BITS_PER_LONG)));
}

static __always_inline unsigned int
cpumask_next(int cpu, const struct cpumask *mask)
{
	unsigned int next = (unsigned int)(cpu + 1);

	while (next < nr_cpu_ids && !cpumask_test_cpu(next, mask))
		++next;
	return next;
}

static __always_inline unsigned int num_online_cpus(void)
{
	return (unsigned int)atomic_read(&__num_online_cpus);
}

static __always_inline unsigned int num_possible_cpus(void)
{
	return (unsigned int)hweight64(READ_ONCE(cpu_possible_mask->bits[0]));
}

#define for_each_cpu(cpu, mask)                                               \
	for ((cpu) = (int)cpumask_next(-1, (mask));                           \
	     (cpu) < (int)nr_cpu_ids;                                         \
	     (cpu) = (int)cpumask_next((cpu), (mask)))

#define for_each_possible_cpu(cpu) for_each_cpu((cpu), cpu_possible_mask)
#define for_each_online_cpu(cpu) for_each_cpu((cpu), cpu_online_mask)

#endif /* _NEVERC_KRT_LINUX_CPUMASK_H */
