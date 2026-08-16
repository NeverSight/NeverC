/* SPDX-License-Identifier: GPL-2.0 */
/* nvk.c — runtime init/cleanup coordinator. */
#if defined(NEVERC_KRT_INIT_HOST_TEST)
#include "test-init-all-shim.h"
#else
#include <nvk.h>
#include "nvk_internal.h"

#include <linux/errno.h>
#endif

static struct neverc_krt_state _neverc_krt_state;
static volatile int _neverc_krt_state_active;
static volatile int _neverc_krt_state_mutating;

static int _neverc_krt_state_mutation_begin(void)
{
	int expected = 0;

	if (!__atomic_compare_exchange_n(
		    &_neverc_krt_state_mutating, &expected, 1, 0,
		    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
		return -EBUSY;
	return 0;
}

static void _neverc_krt_state_mutation_end(void)
{
	__atomic_store_n(&_neverc_krt_state_mutating, 0, __ATOMIC_RELEASE);
}

static void _neverc_krt_state_reset_pending(void)
{
	int sub;

	_neverc_krt_state.ready = 0;
	for (sub = 0; sub < NEVERC_KRT_SUB_COUNT; sub++)
		_neverc_krt_state.sub_status[sub] = -EAGAIN;
}

static void _neverc_krt_state_record(int sub, int status)
{
	_neverc_krt_state.sub_status[sub] = status;
}

static void _neverc_krt_state_record_required(int sub, int status,
					      int *first_error)
{
	_neverc_krt_state_record(sub, status);
	if (status && !*first_error)
		*first_error = status;
}

int neverc_krt_sub_ok(int sub)
{
	return __atomic_load_n(&_neverc_krt_state_active, __ATOMIC_ACQUIRE) &&
	       sub >= 0 && sub < NEVERC_KRT_SUB_COUNT
	       && _neverc_krt_state.sub_status[sub] == 0;
}

const struct neverc_krt_state *neverc_krt_get_state(void)
{
	return &_neverc_krt_state;
}

int neverc_krt_init_all(void)
{
	int first_error = 0;
	int ret;

	ret = _neverc_krt_state_mutation_begin();
	if (ret)
		return ret;
	if (__atomic_load_n(&_neverc_krt_state_active, __ATOMIC_ACQUIRE)) {
		ret = __atomic_load_n(&_neverc_krt_state.ready,
				      __ATOMIC_ACQUIRE) ? 0 : -EAGAIN;
		goto out;
	}

	_neverc_krt_state_reset_pending();
	ret = NEVERC_KRT_BOOTSTRAP();
	if (ret) {
		_neverc_krt_state.sub_status[NEVERC_KRT_SUB_COMPAT] = ret;
		goto out;
	}

	/* Core helpers that every 5.10–6.18 GKI must export fail the load.
	 * Feature backends (visibility, file extras, pid-ns, power, binder)
	 * stay recorded and fail at their APIs. */
	_neverc_krt_state_record_required(NEVERC_KRT_SUB_MEM,
					  neverc_krt_mem_init(), &first_error);
	_neverc_krt_state_record_required(NEVERC_KRT_SUB_COMPAT,
					  neverc_krt_compat_init(),
					  &first_error);
	_neverc_krt_state_record_required(NEVERC_KRT_SUB_PROCESS,
					  neverc_krt_process_init(),
					  &first_error);
	_neverc_krt_state_record_required(NEVERC_KRT_SUB_CRED,
					  neverc_krt_cred_init(),
					  &first_error);
	_neverc_krt_state_record(NEVERC_KRT_SUB_VIS,
				 neverc_krt_vis_init());
	_neverc_krt_state_record(NEVERC_KRT_SUB_ADDR,
				 neverc_krt_addr_init());
	_neverc_krt_state_record(NEVERC_KRT_SUB_FILE,
				 neverc_krt_file_init());
	_neverc_krt_state_record(NEVERC_KRT_SUB_SELINUX,
				 neverc_krt_selinux_init());
	_neverc_krt_state_record_required(NEVERC_KRT_SUB_THREAD,
					  neverc_krt_thread_init(),
					  &first_error);
	_neverc_krt_state_record(NEVERC_KRT_SUB_NETLINK,
				 neverc_krt_nl_init());
	_neverc_krt_state_record_required(NEVERC_KRT_SUB_INTERPOSE,
					  neverc_krt_interpose_init(),
					  &first_error);
	_neverc_krt_state_record(NEVERC_KRT_SUB_SYSCALL,
				 neverc_krt_syscall_init());
	_neverc_krt_state_record_required(NEVERC_KRT_SUB_KSYMS,
					  neverc_krt_ksyms_init(),
					  &first_error);
	_neverc_krt_state_record(NEVERC_KRT_SUB_XMEM,
				 neverc_krt_xmem_init());
	_neverc_krt_state_record(NEVERC_KRT_SUB_NS,
				 neverc_krt_ns_init());
	_neverc_krt_state_record(NEVERC_KRT_SUB_BINDER,
				 neverc_krt_binder_init());
	_neverc_krt_state_record_required(NEVERC_KRT_SUB_TIMER,
					  neverc_krt_timer_init(),
					  &first_error);
	_neverc_krt_state_record(NEVERC_KRT_SUB_POWER,
				 neverc_krt_power_init());
	_neverc_krt_state_record_required(NEVERC_KRT_SUB_CPU,
					  neverc_krt_cpu_init(),
					  &first_error);
	_neverc_krt_state_record_required(NEVERC_KRT_SUB_VMA,
					  neverc_krt_vma_init(),
					  &first_error);

	__atomic_store_n(&_neverc_krt_state.ready, first_error == 0,
			 __ATOMIC_RELEASE);
	__atomic_store_n(&_neverc_krt_state_active, 1, __ATOMIC_RELEASE);
	ret = first_error;
out:
	_neverc_krt_state_mutation_end();
	return ret;
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

int neverc_krt_cleanup_all(void)
{
	int next;
	int ret;

	ret = _neverc_krt_state_mutation_begin();
	if (ret)
		return ret;
	ret = _neverc_krt_user_ptmap_claim_cleanup();
	if (ret) {
		ret = -EBUSY;
		goto out;
	}

	__atomic_store_n(&_neverc_krt_state.ready, 0, __ATOMIC_RELEASE);
#if !defined(NEVERC_KRT_INIT_HOST_TEST)
	__asm__ __volatile__("dsb ish" ::: "memory");
#endif

	neverc_krt_thread_stop_all();

	ret = neverc_krt_vis_pause_interposes();
	if (!ret)
		ret = neverc_krt_selinux_pause_interposes();
	if (ret)
		goto release_ptmap;

#if !defined(NEVERC_KRT_INIT_HOST_TEST)
	__asm__ __volatile__("dsb ish" ::: "memory");
#endif

	ret = neverc_krt_binder_cleanup();
	if (ret)
		goto release_ptmap;
	neverc_krt_vis_file_rewrite_cleanup();
	neverc_krt_vis_cmdline_filter_cleanup();
	neverc_krt_vis_net_cleanup();
	neverc_krt_vis_dmesg_suppress_cleanup();
	neverc_krt_vis_kmsg_read_filter_cleanup();
	neverc_krt_vis_pid_cleanup();
	neverc_krt_vis_mount_filter_cleanup();
	neverc_krt_vis_maps_filter_cleanup();
	neverc_krt_vis_proc_attr_filter_cleanup();
	neverc_krt_se_selective_cleanup();

	neverc_krt_selinux_remove_interposes();
	ret = neverc_krt_vis_remove_interposes();

#if !defined(NEVERC_KRT_INIT_HOST_TEST)
	__asm__ __volatile__("dsb ish" ::: "memory");
	__asm__ __volatile__("isb" ::: "memory");
#endif

	next = neverc_krt_interpose_cleanup();
	if (!ret)
		ret = next;
	if (!ret)
		__atomic_store_n(&_neverc_krt_state_active, 0,
				 __ATOMIC_RELEASE);
release_ptmap:
	_neverc_krt_user_ptmap_release_cleanup();
out:
	_neverc_krt_state_mutation_end();
	return ret;
}

int neverc_krt_init_ftrace(void)
{
	return neverc_krt_ftrace_init();
}
