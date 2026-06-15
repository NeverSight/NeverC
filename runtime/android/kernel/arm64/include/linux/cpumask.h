/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_CPUMASK_H
#define _NVK_LINUX_CPUMASK_H

#include <linux/types.h>
#include <linux/bitops.h>

#define NR_CPUS_MAX 256

typedef struct { unsigned long bits[NR_CPUS_MAX / BITS_PER_LONG]; } cpumask_t;

unsigned int nr_cpu_ids;
unsigned int num_online_cpus(void);
unsigned int num_possible_cpus(void);

#define for_each_possible_cpu(cpu)                                            \
	for ((cpu) = 0; (cpu) < nr_cpu_ids; (cpu)++)

#define for_each_online_cpu(cpu)                                              \
	for_each_possible_cpu(cpu)

int smp_processor_id(void);

#endif /* _NVK_LINUX_CPUMASK_H */
