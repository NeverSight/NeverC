/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>

typedef int (*neverc_krt_reg_pm_fn)(void *nb);
typedef int (*neverc_krt_unreg_pm_fn)(void *nb);
typedef int (*neverc_krt_reg_reboot_fn)(void *nb);
typedef int (*neverc_krt_unreg_reboot_fn)(void *nb);

neverc_krt_reg_pm_fn       _neverc_krt_register_pm;
neverc_krt_unreg_pm_fn     _neverc_krt_unregister_pm;
neverc_krt_reg_reboot_fn   _neverc_krt_register_reboot;
neverc_krt_unreg_reboot_fn _neverc_krt_unregister_reboot;
int                        _neverc_krt_power_inited;

int neverc_krt_power_init(void)
{
	if (_neverc_krt_power_inited) return 0;

	_neverc_krt_register_pm =
		(neverc_krt_reg_pm_fn)NEVERC_KRT_LOOKUP("register_pm_notifier");
	_neverc_krt_unregister_pm =
		(neverc_krt_unreg_pm_fn)NEVERC_KRT_LOOKUP("unregister_pm_notifier");
	_neverc_krt_register_reboot =
		(neverc_krt_reg_reboot_fn)NEVERC_KRT_LOOKUP("register_reboot_notifier");
	_neverc_krt_unregister_reboot =
		(neverc_krt_unreg_reboot_fn)NEVERC_KRT_LOOKUP("unregister_reboot_notifier");

	_neverc_krt_power_inited = 1;
	return 0;
}

static int _neverc_krt_pm_trampoline(void *nb_ptr, unsigned long event, void *unused)
{
	struct neverc_krt_pm_notifier *pm = (struct neverc_krt_pm_notifier *)(
		(char *)nb_ptr -
		__builtin_offsetof(struct neverc_krt_pm_notifier, nb));
	if (pm->callback)
		pm->callback(event);
	return NEVERC_KRT_NOTIFY_DONE;
}

int neverc_krt_pm_register(struct neverc_krt_pm_notifier *pm,
			   neverc_krt_pm_callback_t cb, int priority)
{
	if (!pm || !cb) return -1;
	if (!_neverc_krt_register_pm) return -2;

	__builtin_memset(pm, 0, sizeof(*pm));
	pm->callback = cb;
	pm->nb.notifier_call = (unsigned long)_neverc_krt_pm_trampoline;
	pm->nb.priority = priority;

	int ret = _neverc_krt_register_pm(&pm->nb);
	if (ret == 0)
		pm->registered = 1;
	return ret;
}

void neverc_krt_pm_unregister(struct neverc_krt_pm_notifier *pm)
{
	if (!pm || !pm->registered) return;
	if (_neverc_krt_unregister_pm)
		_neverc_krt_unregister_pm(&pm->nb);
	pm->registered = 0;
}

static int _neverc_krt_reboot_trampoline(void *nb_ptr, unsigned long event,
					 void *unused)
{
	struct neverc_krt_reboot_notifier *rn = (struct neverc_krt_reboot_notifier *)(
		(char *)nb_ptr -
		__builtin_offsetof(struct neverc_krt_reboot_notifier, nb));
	if (rn->callback)
		rn->callback(event);
	return NEVERC_KRT_NOTIFY_DONE;
}

int neverc_krt_reboot_register(struct neverc_krt_reboot_notifier *rn,
			       neverc_krt_reboot_callback_t cb, int priority)
{
	if (!rn || !cb) return -1;
	if (!_neverc_krt_register_reboot) return -2;

	__builtin_memset(rn, 0, sizeof(*rn));
	rn->callback = cb;
	rn->nb.notifier_call = (unsigned long)_neverc_krt_reboot_trampoline;
	rn->nb.priority = priority;

	int ret = _neverc_krt_register_reboot(&rn->nb);
	if (ret == 0)
		rn->registered = 1;
	return ret;
}

void neverc_krt_reboot_unregister(struct neverc_krt_reboot_notifier *rn)
{
	if (!rn || !rn->registered) return;
	if (_neverc_krt_unregister_reboot)
		_neverc_krt_unregister_reboot(&rn->nb);
	rn->registered = 0;
}

