// SPDX-License-Identifier: GPL-2.0
#ifndef NEVERC_KRT_TEST_INIT_ALL_SHIM_H
#define NEVERC_KRT_TEST_INIT_ALL_SHIM_H

#include <stdint.h>

typedef uint32_t u32;

#define EAGAIN 35
#define EBUSY 16

#define NEVERC_KRT_KERNEL 612
#define NEVERC_KRT_BOOTSTRAP() \
	neverc_krt_bootstrap(1, NEVERC_KRT_KERNEL)

#define NEVERC_KRT_SUB_MEM       0
#define NEVERC_KRT_SUB_PROCESS   1
#define NEVERC_KRT_SUB_CRED      2
#define NEVERC_KRT_SUB_VIS       3
#define NEVERC_KRT_SUB_ADDR      4
#define NEVERC_KRT_SUB_COMPAT    5
#define NEVERC_KRT_SUB_FILE      6
#define NEVERC_KRT_SUB_SELINUX   7
#define NEVERC_KRT_SUB_THREAD    8
#define NEVERC_KRT_SUB_NETLINK   9
#define NEVERC_KRT_SUB_INTERPOSE 10
#define NEVERC_KRT_SUB_SYSCALL   11
#define NEVERC_KRT_SUB_KSYMS     12
#define NEVERC_KRT_SUB_XMEM      13
#define NEVERC_KRT_SUB_NS        14
#define NEVERC_KRT_SUB_BINDER    15
#define NEVERC_KRT_SUB_TIMER     16
#define NEVERC_KRT_SUB_POWER     17
#define NEVERC_KRT_SUB_CPU       18
#define NEVERC_KRT_SUB_VMA       19
#define NEVERC_KRT_SUB_COUNT     20

struct neverc_krt_state {
	u32 ready;
	int sub_status[NEVERC_KRT_SUB_COUNT];
};

int neverc_krt_sub_ok(int sub);
const struct neverc_krt_state *neverc_krt_get_state(void);
int neverc_krt_init_all(void);
int neverc_krt_cleanup_all(void);

struct neverc_krt_interpose;
struct neverc_krt_interpose_ctx;
struct neverc_krt_ftrace_interpose;
typedef int (*neverc_krt_ctx_handler_t)(void *);

int neverc_krt_bootstrap(int cfi, int kernel_profile);
int neverc_krt_mem_init(void);
int neverc_krt_compat_init(void);
int neverc_krt_process_init(void);
int neverc_krt_cred_init(void);
int neverc_krt_vis_init(void);
int neverc_krt_addr_init(void);
int neverc_krt_file_init(void);
int neverc_krt_selinux_init(void);
int neverc_krt_thread_init(void);
int neverc_krt_nl_init(void);
int neverc_krt_interpose_init(void);
int neverc_krt_syscall_init(void);
int neverc_krt_ksyms_init(void);
int neverc_krt_xmem_init(void);
int neverc_krt_ns_init(void);
int neverc_krt_binder_init(void);
int neverc_krt_timer_init(void);
int neverc_krt_power_init(void);
int neverc_krt_cpu_init(void);
int neverc_krt_vma_init(void);

unsigned long kallsyms_lookup_name(const char *name);
int neverc_krt_interpose_install(
	struct neverc_krt_interpose *handle, void *target,
	void *replacement, void **original);
int neverc_krt_interpose_install_ctx(
	struct neverc_krt_interpose_ctx *handle, void *target,
	neverc_krt_ctx_handler_t handler, void **call_original);
int neverc_krt_interpose_auto(
	struct neverc_krt_interpose *handle, void *target,
	void *replacement, void **original,
	struct neverc_krt_ftrace_interpose *ftrace);

int _neverc_krt_user_ptmap_claim_cleanup(void);
void _neverc_krt_user_ptmap_release_cleanup(void);
void neverc_krt_thread_stop_all(void);
int neverc_krt_vis_pause_interposes(void);
int neverc_krt_selinux_pause_interposes(void);
int neverc_krt_binder_cleanup(void);
void neverc_krt_vis_file_rewrite_cleanup(void);
void neverc_krt_vis_cmdline_filter_cleanup(void);
void neverc_krt_vis_net_cleanup(void);
void neverc_krt_vis_dmesg_suppress_cleanup(void);
void neverc_krt_vis_kmsg_read_filter_cleanup(void);
void neverc_krt_vis_pid_cleanup(void);
void neverc_krt_vis_mount_filter_cleanup(void);
void neverc_krt_vis_maps_filter_cleanup(void);
void neverc_krt_vis_proc_attr_filter_cleanup(void);
void neverc_krt_se_selective_cleanup(void);
void neverc_krt_selinux_remove_interposes(void);
int neverc_krt_vis_remove_interposes(void);
int neverc_krt_interpose_cleanup(void);
int neverc_krt_ftrace_init(void);

#endif
