/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_H
#define NVK_H

#include <nvkmod.h>
#include <nvk_hook.h>
#include <nvk_mem.h>
#include <nvk_syscall.h>
#include <nvk_process.h>
#include <nvk_cred.h>
#include <nvk_selinux.h>
#include <nvk_hide.h>
#include <nvk_log.h>
#include <nvk_thread.h>
#include <nvk_netlink.h>
#include <nvk_file.h>
#include <nvk_addr.h>
#include <nvk_compat.h>
#include <nvk_anti.h>

static int nvk_init_all(void)
{
	int ret = NVK_BOOTSTRAP();
	if (ret) return ret;
	nvk_mem_init();
	nvk_process_init();
	nvk_cred_init();
	nvk_hide_init();
	nvk_addr_init();
	nvk_compat_init();
	nvk_file_init();
	nvk_selinux_init();
	nvk_thread_init();
	nvk_nl_init();
	return nvk_hook_init();
}

static int nvk_hook_by_sym(struct nvk_hook *h, const char *sym_name,
			   void *replace, void **orig)
{
	void *target = (void *)kallsyms_lookup_name(sym_name);
	if (!target) return -1;
	return nvk_hook_install(h, target, replace, orig);
}

static int nvk_hook_ctx_by_sym(struct nvk_hook_ctx *h, const char *sym_name,
			       nvk_ctx_handler_t handler, void **call_orig)
{
	void *target = (void *)kallsyms_lookup_name(sym_name);
	if (!target) return -1;
	return nvk_hook_install_ctx(h, target, handler, call_orig);
}

#endif /* NVK_H */
