/* SPDX-License-Identifier: GPL-2.0 */
/* nvk.c — implementations extracted from nvk.h. */
#include <nvk.h>

int neverc_krt_init_all(void)
{
	int ret = NEVERC_KRT_BOOTSTRAP();
	if (ret) return ret;

	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_MEM]     = neverc_krt_mem_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_PROCESS] = neverc_krt_process_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_CRED]    = neverc_krt_cred_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_HIDE]    = neverc_krt_hide_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_ADDR]    = neverc_krt_addr_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_COMPAT]  = neverc_krt_compat_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_FILE]    = neverc_krt_file_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_SELINUX] = neverc_krt_selinux_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_THREAD]  = neverc_krt_thread_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_NETLINK] = neverc_krt_nl_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_HOOK]    = neverc_krt_hook_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_SYSCALL] = neverc_krt_syscall_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_KSYMS]  = neverc_krt_ksyms_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_INJECT] = neverc_krt_inject_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_NS]     = neverc_krt_ns_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_BINDER] = neverc_krt_binder_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_TIMER]  = neverc_krt_timer_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_POWER]  = neverc_krt_power_init();
	_neverc_krt_state.sub_status[NEVERC_KRT_SUB_CPU]    = neverc_krt_cpu_init();
	neverc_krt_vma_init();

	if (neverc_krt_check_kernel_match() != NEVERC_KRT_VER_EXACT)
		neverc_krt_patch_vermagic(&__this_module);

	_neverc_krt_state.ready = 1;

	return _neverc_krt_state.sub_status[NEVERC_KRT_SUB_HOOK];
}

int _neverc_krt_hook_by_sym(struct neverc_krt_hook *h, const char *sym_name,
			    void *replace, void **orig)
{
	void *target = (void *)kallsyms_lookup_name(sym_name);
	if (!target) return -1;
	return neverc_krt_hook_install(h, target, replace, orig);
}

int _neverc_krt_hook_ctx_by_sym(struct neverc_krt_hook_ctx *h, const char *sym_name,
				neverc_krt_ctx_handler_t handler, void **call_orig)
{
	void *target = (void *)kallsyms_lookup_name(sym_name);
	if (!target) return -1;
	return neverc_krt_hook_install_ctx(h, target, handler, call_orig);
}

int _neverc_krt_hook_auto_by_sym(struct neverc_krt_hook *h, const char *sym_name,
				 void *replace, void **orig,
				 struct neverc_krt_ftrace_hook *ft)
{
	void *target = (void *)kallsyms_lookup_name(sym_name);
	if (!target) return -1;
	return neverc_krt_hook_auto(h, target, replace, orig, ft);
}

void neverc_krt_cleanup_all(void)
{
	_neverc_krt_state.ready = 0;
	__asm__ __volatile__("dsb ish" ::: "memory");

	neverc_krt_thread_stop_all();

	if (_neverc_krt_vmalloc_hooked)
		neverc_krt_hook_pause(&_neverc_krt_vmalloc_hook);
	if (_neverc_krt_ks_hooked)
		neverc_krt_hook_pause(&_neverc_krt_ks_hook);
	if (_neverc_krt_avc_hook.active) neverc_krt_hook_pause(&_neverc_krt_avc_hook);
	if (_neverc_krt_inode_hook.active) neverc_krt_hook_pause(&_neverc_krt_inode_hook);
	if (_neverc_krt_task_perm_hook.active) neverc_krt_hook_pause(&_neverc_krt_task_perm_hook);
	if (_neverc_krt_cred_perm_hook.active) neverc_krt_hook_pause(&_neverc_krt_cred_perm_hook);

	__asm__ __volatile__("dsb ish" ::: "memory");

	neverc_krt_binder_cleanup();
	neverc_krt_file_spoof_cleanup();
	neverc_krt_cmdline_filter_cleanup();
	neverc_krt_net_hide_cleanup();
	neverc_krt_dmesg_suppress_cleanup();
	neverc_krt_kmsg_read_filter_cleanup();
	neverc_krt_pid_hide_cleanup();
	neverc_krt_mount_filter_cleanup();
	neverc_krt_maps_filter_clear();
	neverc_krt_proc_attr_filter_cleanup();
	neverc_krt_se_selective_cleanup();

	if (_neverc_krt_cred_perm_hook.active) neverc_krt_hook_remove(&_neverc_krt_cred_perm_hook);
	if (_neverc_krt_task_perm_hook.active) neverc_krt_hook_remove(&_neverc_krt_task_perm_hook);
	if (_neverc_krt_inode_hook.active) neverc_krt_hook_remove(&_neverc_krt_inode_hook);
	if (_neverc_krt_avc_hook.active) neverc_krt_hook_remove(&_neverc_krt_avc_hook);

	if (_neverc_krt_ks_hooked) {
		neverc_krt_hook_remove(&_neverc_krt_ks_hook);
		_neverc_krt_ks_hooked = 0;
	}
	if (_neverc_krt_vmalloc_hooked) {
		neverc_krt_hook_remove(&_neverc_krt_vmalloc_hook);
		_neverc_krt_vmalloc_hooked = 0;
	}

	__asm__ __volatile__("dsb ish" ::: "memory");
	__asm__ __volatile__("isb" ::: "memory");

	neverc_krt_hook_cleanup();
}

int neverc_krt_init_ftrace(void)
{
	return neverc_krt_ftrace_init();
}

