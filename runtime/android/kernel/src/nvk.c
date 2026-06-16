/* SPDX-License-Identifier: GPL-2.0 */
/* nvk.c — implementations extracted from nvk.h. */
#include <nvk.h>

int nvk_init_all(void)
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
	_nvk_state.sub_status[NVK_SUB_KSYMS]  = nvk_ksyms_init();
	_nvk_state.sub_status[NVK_SUB_INJECT] = nvk_inject_init();
	_nvk_state.sub_status[NVK_SUB_NS]     = nvk_ns_init();
	_nvk_state.sub_status[NVK_SUB_BINDER] = nvk_binder_init();
	_nvk_state.sub_status[NVK_SUB_TIMER]  = nvk_timer_init();
	_nvk_state.sub_status[NVK_SUB_POWER]  = nvk_power_init();
	_nvk_state.sub_status[NVK_SUB_CPU]    = nvk_cpu_init();
	nvk_vma_init();

	if (nvk_check_kernel_match() != NVK_VER_EXACT)
		nvk_patch_vermagic(&__this_module);

	_nvk_state.ready = 1;

	return _nvk_state.sub_status[NVK_SUB_HOOK];
}

int _nvk_hook_by_sym(struct nvk_hook *h, const char *sym_name,
			    void *replace, void **orig)
{
	void *target = (void *)kallsyms_lookup_name(sym_name);
	if (!target) return -1;
	return nvk_hook_install(h, target, replace, orig);
}

int _nvk_hook_ctx_by_sym(struct nvk_hook_ctx *h, const char *sym_name,
				nvk_ctx_handler_t handler, void **call_orig)
{
	void *target = (void *)kallsyms_lookup_name(sym_name);
	if (!target) return -1;
	return nvk_hook_install_ctx(h, target, handler, call_orig);
}

int _nvk_hook_auto_by_sym(struct nvk_hook *h, const char *sym_name,
				 void *replace, void **orig,
				 struct nvk_ftrace_hook *ft)
{
	void *target = (void *)kallsyms_lookup_name(sym_name);
	if (!target) return -1;
	return nvk_hook_auto(h, target, replace, orig, ft);
}

void nvk_cleanup_all(void)
{
	_nvk_state.ready = 0;
	__asm__ __volatile__("dsb ish" ::: "memory");

	nvk_thread_stop_all();

	if (_nvk_vmalloc_hooked)
		nvk_hook_pause(&_nvk_vmalloc_hook);
	if (_nvk_ks_hooked)
		nvk_hook_pause(&_nvk_ks_hook);
	if (_nvk_avc_hook.active) nvk_hook_pause(&_nvk_avc_hook);
	if (_nvk_inode_hook.active) nvk_hook_pause(&_nvk_inode_hook);
	if (_nvk_task_perm_hook.active) nvk_hook_pause(&_nvk_task_perm_hook);
	if (_nvk_cred_perm_hook.active) nvk_hook_pause(&_nvk_cred_perm_hook);

	__asm__ __volatile__("dsb ish" ::: "memory");

	nvk_binder_cleanup();
	nvk_file_spoof_cleanup();
	nvk_cmdline_filter_cleanup();
	nvk_net_hide_cleanup();
	nvk_dmesg_suppress_cleanup();
	nvk_kmsg_read_filter_cleanup();
	nvk_pid_hide_cleanup();
	nvk_mount_filter_cleanup();
	nvk_maps_filter_clear();
	nvk_proc_attr_filter_cleanup();
	nvk_se_selective_cleanup();

	if (_nvk_cred_perm_hook.active) nvk_hook_remove(&_nvk_cred_perm_hook);
	if (_nvk_task_perm_hook.active) nvk_hook_remove(&_nvk_task_perm_hook);
	if (_nvk_inode_hook.active) nvk_hook_remove(&_nvk_inode_hook);
	if (_nvk_avc_hook.active) nvk_hook_remove(&_nvk_avc_hook);

	if (_nvk_ks_hooked) {
		nvk_hook_remove(&_nvk_ks_hook);
		_nvk_ks_hooked = 0;
	}
	if (_nvk_vmalloc_hooked) {
		nvk_hook_remove(&_nvk_vmalloc_hook);
		_nvk_vmalloc_hooked = 0;
	}

	__asm__ __volatile__("dsb ish" ::: "memory");
	__asm__ __volatile__("isb" ::: "memory");

	nvk_hook_cleanup();
}

int nvk_init_ftrace(void)
{
	return nvk_ftrace_init();
}

