/* SPDX-License-Identifier: GPL-2.0 */
/* nvk_power.c — implementations extracted from nvk_power.h. */
#include <nvk.h>

int nvk_power_init(void)
{
	if (_nvk_power_inited) return 0;

	_nvk_register_pm =
		(nvk_reg_pm_fn)NVK_LOOKUP("register_pm_notifier");
	_nvk_unregister_pm =
		(nvk_unreg_pm_fn)NVK_LOOKUP("unregister_pm_notifier");
	_nvk_register_reboot =
		(nvk_reg_reboot_fn)NVK_LOOKUP("register_reboot_notifier");
	_nvk_unregister_reboot =
		(nvk_unreg_reboot_fn)NVK_LOOKUP("unregister_reboot_notifier");

	_nvk_power_inited = 1;
	return 0;
}

int _nvk_pm_trampoline(void *nb_ptr, unsigned long event, void *unused)
{
	struct nvk_pm_notifier *pm = (struct nvk_pm_notifier *)(
		(char *)nb_ptr -
		__builtin_offsetof(struct nvk_pm_notifier, nb));
	if (pm->callback)
		pm->callback(event);
	return NVK_NOTIFY_DONE;
}

int nvk_pm_register(struct nvk_pm_notifier *pm,
			   nvk_pm_callback_t cb, int priority)
{
	if (!pm || !cb) return -1;
	if (!_nvk_register_pm) return -2;

	__builtin_memset(pm, 0, sizeof(*pm));
	pm->callback = cb;
	pm->nb.notifier_call = (unsigned long)_nvk_pm_trampoline;
	pm->nb.priority = priority;

	int ret = _nvk_register_pm(&pm->nb);
	if (ret == 0)
		pm->registered = 1;
	return ret;
}

void nvk_pm_unregister(struct nvk_pm_notifier *pm)
{
	if (!pm || !pm->registered) return;
	if (_nvk_unregister_pm)
		_nvk_unregister_pm(&pm->nb);
	pm->registered = 0;
}

int _nvk_reboot_trampoline(void *nb_ptr, unsigned long event,
				  void *unused)
{
	struct nvk_reboot_notifier *rn = (struct nvk_reboot_notifier *)(
		(char *)nb_ptr -
		__builtin_offsetof(struct nvk_reboot_notifier, nb));
	if (rn->callback)
		rn->callback(event);
	return NVK_NOTIFY_DONE;
}

int nvk_reboot_register(struct nvk_reboot_notifier *rn,
			       nvk_reboot_callback_t cb, int priority)
{
	if (!rn || !cb) return -1;
	if (!_nvk_register_reboot) return -2;

	__builtin_memset(rn, 0, sizeof(*rn));
	rn->callback = cb;
	rn->nb.notifier_call = (unsigned long)_nvk_reboot_trampoline;
	rn->nb.priority = priority;

	int ret = _nvk_register_reboot(&rn->nb);
	if (ret == 0)
		rn->registered = 1;
	return ret;
}

void nvk_reboot_unregister(struct nvk_reboot_notifier *rn)
{
	if (!rn || !rn->registered) return;
	if (_nvk_unregister_reboot)
		_nvk_unregister_reboot(&rn->nb);
	rn->registered = 0;
}

