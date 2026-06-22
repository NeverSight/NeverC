/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_H
#define NEVERC_KRT_H

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

#define NEVERC_KRT_SUB_MEM       0
#define NEVERC_KRT_SUB_PROCESS   1
#define NEVERC_KRT_SUB_CRED      2
#define NEVERC_KRT_SUB_HIDE      3
#define NEVERC_KRT_SUB_ADDR      4
#define NEVERC_KRT_SUB_COMPAT    5
#define NEVERC_KRT_SUB_FILE      6
#define NEVERC_KRT_SUB_SELINUX   7
#define NEVERC_KRT_SUB_THREAD    8
#define NEVERC_KRT_SUB_NETLINK   9
#define NEVERC_KRT_SUB_HOOK     10
#define NEVERC_KRT_SUB_SYSCALL  11
#define NEVERC_KRT_SUB_KSYMS    12
#define NEVERC_KRT_SUB_INJECT   13
#define NEVERC_KRT_SUB_NS       14
#define NEVERC_KRT_SUB_BINDER   15
#define NEVERC_KRT_SUB_TIMER    16
#define NEVERC_KRT_SUB_POWER    17
#define NEVERC_KRT_SUB_CPU      18
#define NEVERC_KRT_SUB_COUNT    19

struct neverc_krt_state {
	u32 ready;
	int sub_status[NEVERC_KRT_SUB_COUNT];
};

int neverc_krt_sub_ok(int sub);
int neverc_krt_init_all(void);
void neverc_krt_cleanup_all(void);
int neverc_krt_init_ftrace(void);
const struct neverc_krt_state *neverc_krt_get_state(void);

/* ---- Symbol-name hook helpers ---- */

/*
 * Internal: called by the neverc_krt_hook_*_by_sym() macros only.
 * NC_XORSTR() must expand at the call site, so these cannot be static.
 * Do NOT call directly — use the macros below.
 */
int _neverc_krt_hook_by_sym(struct neverc_krt_hook *h, const char *sym_name,
			    void *replace, void **orig);
int _neverc_krt_hook_ctx_by_sym(struct neverc_krt_hook_ctx *h,
				const char *sym_name,
				neverc_krt_ctx_handler_t handler,
				void **call_orig);
int _neverc_krt_hook_auto_by_sym(struct neverc_krt_hook *h,
				 const char *sym_name,
				 void *replace, void **orig,
				 struct neverc_krt_ftrace_hook *ft);

#define neverc_krt_hook_by_sym(h, sym, replace, orig) \
	_neverc_krt_hook_by_sym((h), NC_XORSTR(sym), (replace), (orig))

#define neverc_krt_hook_ctx_by_sym(h, sym, handler, call_orig) \
	_neverc_krt_hook_ctx_by_sym((h), NC_XORSTR(sym), (handler), (call_orig))

#define neverc_krt_hook_auto_by_sym(h, sym, replace, orig, ft) \
	_neverc_krt_hook_auto_by_sym((h), NC_XORSTR(sym), (replace), (orig), (ft))


#endif /* NEVERC_KRT_H */
