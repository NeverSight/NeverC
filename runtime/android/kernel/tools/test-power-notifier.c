// SPDX-License-Identifier: GPL-2.0
/* Host behavior fixture for retry-safe PM and reboot notifier ownership. */

#include "test-power-notifier-shim.h"

#include <assert.h>
#include <string.h>

static struct neverc_krt_pm_notifier *fixture_pm;
static int fixture_pm_register_calls;
static int fixture_pm_unregister_calls;
static int fixture_pm_register_status;
static int fixture_pm_unregister_status;
static int fixture_pm_reenter;
static int fixture_pm_reentrant_register_status;
static int fixture_pm_reentrant_unregister_status;
static int fixture_pm_callback_calls;

static int fixture_reboot_register_calls;
static int fixture_reboot_unregister_calls;
static int fixture_reboot_unregister_status;
static int fixture_reboot_callback_calls;

static void fixture_pm_callback(unsigned long event)
{
	assert(event == NEVERC_KRT_PM_SUSPEND_PREPARE);
	fixture_pm_callback_calls++;
}

static void fixture_reboot_callback(unsigned long event)
{
	assert(event == NEVERC_KRT_SYS_POWER_OFF);
	fixture_reboot_callback_calls++;
}

static int fixture_register_pm(struct notifier_block *nb)
{
	fixture_pm_register_calls++;
	assert(fixture_pm);
	assert(fixture_pm->registered == 2);
	if (fixture_pm_reenter) {
		fixture_pm_reenter = 0;
		fixture_pm_reentrant_register_status =
			neverc_krt_pm_register(
				fixture_pm, fixture_pm_callback, 8);
		fixture_pm_reentrant_unregister_status =
			neverc_krt_pm_unregister(fixture_pm);
	}
	nb->notifier_call(
		nb, NEVERC_KRT_PM_SUSPEND_PREPARE, (void *)0);
	return fixture_pm_register_status;
}

static int fixture_unregister_pm(struct notifier_block *nb)
{
	(void)nb;
	fixture_pm_unregister_calls++;
	assert(fixture_pm);
	assert(fixture_pm->registered == 2);
	return fixture_pm_unregister_status;
}

static int fixture_register_reboot(struct notifier_block *nb)
{
	fixture_reboot_register_calls++;
	nb->notifier_call(nb, NEVERC_KRT_SYS_POWER_OFF, (void *)0);
	return 0;
}

static int fixture_unregister_reboot(struct notifier_block *nb)
{
	(void)nb;
	fixture_reboot_unregister_calls++;
	return fixture_reboot_unregister_status;
}

unsigned long neverc_krt_power_test_lookup(const char *name)
{
	if (strcmp(name, "register_pm_notifier") == 0)
		return (unsigned long)fixture_register_pm;
	if (strcmp(name, "unregister_pm_notifier") == 0)
		return (unsigned long)fixture_unregister_pm;
	if (strcmp(name, "register_reboot_notifier") == 0)
		return (unsigned long)fixture_register_reboot;
	if (strcmp(name, "unregister_reboot_notifier") == 0)
		return (unsigned long)fixture_unregister_reboot;
	return 0;
}

static void check_pm_lifecycle(void)
{
	struct neverc_krt_pm_notifier pm =
		NEVERC_KRT_PM_NOTIFIER_INIT;

	fixture_pm = &pm;
	fixture_pm_reenter = 1;
	assert(neverc_krt_pm_register(
		       &pm, fixture_pm_callback, 7) == 0);
	assert(pm.registered == 1);
	assert(fixture_pm_callback_calls == 1);
	assert(fixture_pm_reentrant_register_status == -EBUSY);
	assert(fixture_pm_reentrant_unregister_status == -EBUSY);
	assert(neverc_krt_pm_register(
		       &pm, fixture_pm_callback, 7) == -EBUSY);
	assert(fixture_pm_register_calls == 1);

	fixture_pm_unregister_status = -77;
	assert(neverc_krt_pm_unregister(&pm) == -77);
	assert(pm.registered == 1);
	assert(pm.callback == fixture_pm_callback);

	fixture_pm_unregister_status = 0;
	assert(neverc_krt_pm_unregister(&pm) == 0);
	assert(pm.registered == 0);
	assert(pm.callback == (neverc_krt_pm_callback_t)0);
	assert(fixture_pm_unregister_calls == 2);
	assert(neverc_krt_pm_unregister(&pm) == 0);

	fixture_pm_register_status = -88;
	assert(neverc_krt_pm_register(
		       &pm, fixture_pm_callback, 7) == -88);
	assert(pm.registered == 0);
	fixture_pm_register_status = 0;
	assert(neverc_krt_pm_register(
		       &pm, fixture_pm_callback, 7) == 0);
	assert(neverc_krt_pm_unregister(&pm) == 0);
}

static void check_reboot_lifecycle(void)
{
	struct neverc_krt_reboot_notifier rn =
		NEVERC_KRT_REBOOT_NOTIFIER_INIT;

	assert(neverc_krt_reboot_register(
		       &rn, fixture_reboot_callback, 4) == 0);
	assert(rn.registered == 1);
	assert(fixture_reboot_callback_calls == 1);
	assert(neverc_krt_reboot_register(
		       &rn, fixture_reboot_callback, 4) == -EBUSY);
	assert(fixture_reboot_register_calls == 1);

	fixture_reboot_unregister_status = -91;
	assert(neverc_krt_reboot_unregister(&rn) == -91);
	assert(rn.registered == 1);
	fixture_reboot_unregister_status = 0;
	assert(neverc_krt_reboot_unregister(&rn) == 0);
	assert(rn.registered == 0);
	assert(fixture_reboot_unregister_calls == 2);
}

int main(void)
{
	assert(neverc_krt_power_init() == 0);
	check_pm_lifecycle();
	check_reboot_lifecycle();
	return 0;
}
