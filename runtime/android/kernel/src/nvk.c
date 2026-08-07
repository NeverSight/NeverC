/* SPDX-License-Identifier: GPL-2.0 */
/* nvk.c — runtime init/cleanup coordinator. */
#include <nvk.h>

static struct neverc_krt_state _neverc_krt_state;

int neverc_krt_sub_ok(int sub)
{
	return sub >= 0 && sub < NEVERC_KRT_SUB_COUNT
	       && _neverc_krt_state.sub_status[sub] == 0;
}

const struct neverc_krt_state *neverc_krt_get_state(void)
{
	return &_neverc_krt_state;
}

int neverc_krt_init_all(void)
{
	int ret = neverc_krt_bootstrap(1, 0);
	if (ret) return ret;

	/* Bootstrap verifies the exact runtime profile before any subsystem that
	 * consumes profile layouts or ABI capabilities can run. */
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_MEM]     = 0;
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_COMPAT]  = 0;
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_PROCESS] = neverc_krt_process_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_CRED]    = neverc_krt_cred_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_VIS]     = neverc_krt_vis_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_ADDR]    = neverc_krt_addr_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_FILE]    = neverc_krt_file_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_SELINUX] = neverc_krt_selinux_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_THREAD]  = neverc_krt_thread_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_NETLINK] = neverc_krt_nl_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_INTERPOSE]    = neverc_krt_interpose_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_SYSCALL] = neverc_krt_syscall_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_KSYMS]  = neverc_krt_ksyms_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_XMEM]  = neverc_krt_xmem_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_NS]     = neverc_krt_ns_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_BINDER] = neverc_krt_binder_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_TIMER]  = neverc_krt_timer_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_POWER]  = neverc_krt_power_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_CPU]    = neverc_krt_cpu_init();
	neverc_krt_vma_init();

	_neverc_krt_state.ready = 1;

	return _neverc_krt_state.sub_status[NEVERC_KRT_SUB_INTERPOSE];
}

int neverc_krt_interpose_by_name(struct neverc_krt_interpose *h,
				 const char *symbol_name,
				 void *replace, void **orig)
{
	void *target = (void *)kallsyms_lookup_name(symbol_name);
	if (!target) return -1;
	return neverc_krt_interpose_install(h, target, replace, orig);
}

int neverc_krt_interpose_ctx_by_name(struct neverc_krt_interpose_ctx *h,
				     const char *symbol_name,
				     neverc_krt_ctx_handler_t handler,
				     void **call_orig)
{
	void *target = (void *)kallsyms_lookup_name(symbol_name);
	if (!target) return -1;
	return neverc_krt_interpose_install_ctx(h, target, handler, call_orig);
}

int neverc_krt_interpose_auto_by_name(struct neverc_krt_interpose *h,
				      const char *symbol_name,
				      void *replace, void **orig,
				      struct neverc_krt_ftrace_interpose *ft)
{
	void *target = (void *)kallsyms_lookup_name(symbol_name);
	if (!target) return -1;
	return neverc_krt_interpose_auto(h, target, replace, orig, ft);
}

void neverc_krt_cleanup_all(void)
{
	_neverc_krt_state.ready = 0;
	__asm__ __volatile__("dsb ish" ::: "memory");

	neverc_krt_thread_stop_all();

	neverc_krt_vis_pause_interposes();
	neverc_krt_selinux_pause_interposes();

	__asm__ __volatile__("dsb ish" ::: "memory");

	neverc_krt_binder_cleanup();
	neverc_krt_vis_file_rewrite_cleanup();
	neverc_krt_vis_cmdline_filter_cleanup();
	neverc_krt_vis_net_cleanup();
	neverc_krt_vis_dmesg_suppress_cleanup();
	neverc_krt_vis_kmsg_read_filter_cleanup();
	neverc_krt_vis_pid_cleanup();
	neverc_krt_vis_mount_filter_cleanup();
	neverc_krt_vis_maps_filter_clear();
	neverc_krt_vis_proc_attr_filter_cleanup();
	neverc_krt_se_selective_cleanup();

	neverc_krt_selinux_remove_interposes();
	neverc_krt_vis_remove_interposes();

	__asm__ __volatile__("dsb ish" ::: "memory");
	__asm__ __volatile__("isb" ::: "memory");

	neverc_krt_interpose_cleanup();
}

int neverc_krt_init_ftrace(void)
{
	return neverc_krt_ftrace_init();
}
