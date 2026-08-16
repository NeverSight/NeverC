/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_TEST_POWER_NOTIFIER_SHIM_H
#define NEVERC_KRT_TEST_POWER_NOTIFIER_SHIM_H

#ifdef __weak
#undef __weak
#endif

#include <linux/errno.h>
#include <nvk_power.h>

unsigned long neverc_krt_power_test_lookup(const char *name);

#define NEVERC_KRT_LOOKUP(name) \
	((void *)neverc_krt_power_test_lookup(name))

#endif /* NEVERC_KRT_TEST_POWER_NOTIFIER_SHIM_H */
