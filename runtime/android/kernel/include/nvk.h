/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_H
#define NVK_H

#include <nvkmod.h>
#include <nvk_rt.h>
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
#include <nvk_su.h>
#include <nvk_ksyms.h>
#include <nvk_seccomp.h>
#include <nvk_pmu.h>
#include <nvk_inject.h>
#include <nvk_ns.h>
#include <nvk_binder.h>
#include <nvk_crypto.h>
#include <nvk_timer.h>
#include <nvk_power.h>
#include <nvk_cpu.h>

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
#define NVK_SUB_KSYMS    12
#define NVK_SUB_INJECT   13
#define NVK_SUB_NS       14
#define NVK_SUB_BINDER   15
#define NVK_SUB_TIMER    16
#define NVK_SUB_POWER    17
#define NVK_SUB_CPU      18
#define NVK_SUB_COUNT    19

struct nvk_state {
	u32 ready;
	int sub_status[NVK_SUB_COUNT];
};

NVK_RT_VAR struct nvk_state _nvk_state;

static __always_inline int nvk_sub_ok(int sub)
{
	return sub >= 0 && sub < NVK_SUB_COUNT
	       && _nvk_state.sub_status[sub] == 0;
}

int nvk_init_all(void);


static __always_inline const struct nvk_state *nvk_get_state(void)
{
	return &_nvk_state;
}

int _nvk_hook_by_sym(struct nvk_hook *h, const char *sym_name,
			    void *replace, void **orig);


int _nvk_hook_ctx_by_sym(struct nvk_hook_ctx *h, const char *sym_name,
				nvk_ctx_handler_t handler, void **call_orig);


int _nvk_hook_auto_by_sym(struct nvk_hook *h, const char *sym_name,
				 void *replace, void **orig,
				 struct nvk_ftrace_hook *ft);


#define nvk_hook_by_sym(h, sym, replace, orig) \
	_nvk_hook_by_sym((h), NC_XORSTR(sym), (replace), (orig))

#define nvk_hook_ctx_by_sym(h, sym, handler, call_orig) \
	_nvk_hook_ctx_by_sym((h), NC_XORSTR(sym), (handler), (call_orig))

#define nvk_hook_auto_by_sym(h, sym, replace, orig, ft) \
	_nvk_hook_auto_by_sym((h), NC_XORSTR(sym), (replace), (orig), (ft))

void nvk_cleanup_all(void);


int nvk_init_ftrace(void);


#endif /* NVK_H */
