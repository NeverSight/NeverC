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
#include <nvk_vma.h>

#define NVK_SUB_MEM       0
#define NVK_SUB_PROCESS   1
#define NVK_SUB_CRED      2
#define NVK_SUB_HIDE      3
#define NVK_SUB_ADDR      4
#define NVK_SUB_COMPAT    5
#define NVK_SUB_FILE      6
#define NVK_SUB_SELINUX   7
#define NVK_SUB_THREAD    8
#define NVK_SUB_NETLINK   9
#define NVK_SUB_HOOK     10
#define NVK_SUB_SYSCALL  11
#define NVK_SUB_COUNT    12

struct nvk_state {
	u32 ready;
	int sub_status[NVK_SUB_COUNT];
};

static struct nvk_state _nvk_state;

static __always_inline int nvk_sub_ok(int sub)
{
	return sub >= 0 && sub < NVK_SUB_COUNT
	       && _nvk_state.sub_status[sub] == 0;
}

static int nvk_init_all(void)
{
	int ret = NVK_BOOTSTRAP();
	if (ret) return ret;

	_nvk_state.sub_status[NVK_SUB_MEM]     = nvk_mem_init();
	_nvk_state.sub_status[NVK_SUB_PROCESS] = nvk_process_init();
	_nvk_state.sub_status[NVK_SUB_CRED]    = nvk_cred_init();
	_nvk_state.sub_status[NVK_SUB_HIDE]    = nvk_hide_init();
	_nvk_state.sub_status[NVK_SUB_ADDR]    = nvk_addr_init();
	_nvk_state.sub_status[NVK_SUB_COMPAT]  = nvk_compat_init();
	_nvk_state.sub_status[NVK_SUB_FILE]    = nvk_file_init();
	_nvk_state.sub_status[NVK_SUB_SELINUX] = nvk_selinux_init();
	_nvk_state.sub_status[NVK_SUB_THREAD]  = nvk_thread_init();
	_nvk_state.sub_status[NVK_SUB_NETLINK] = nvk_nl_init();
	_nvk_state.sub_status[NVK_SUB_HOOK]    = nvk_hook_init();
	_nvk_state.sub_status[NVK_SUB_SYSCALL] = nvk_syscall_init();
	nvk_vma_init();
	_nvk_state.ready = 1;

	return _nvk_state.sub_status[NVK_SUB_HOOK];
}

static __always_inline const struct nvk_state *nvk_get_state(void)
{
	return &_nvk_state;
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
