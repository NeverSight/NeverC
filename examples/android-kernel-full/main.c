/* SPDX-License-Identifier: GPL-2.0 */
#include <nvkmod.h>
#include <nvk_interpose.h>
#ifdef NEVERC_KRT_CONTEXT_INTERPOSE
#include <nvk_interpose_advanced.h>
#endif
#include <nvk_vis.h>
#include <nvk_process.h>
#include <nvk_cred.h>
#include <nvk_mem.h>
#include <nvk_selinux.h>
#include <nvk_thread.h>
#include <nvk_netlink.h>
#include <nvk_file.h>
#include <nvk_compat.h>
#include <nvk_cpu.h>
#include <nvk_anti.h>
#include <nvk_addr.h>
#include <nvk_vma.h>

#define NEVERC_KRT_LOG_TAG "neverc_krt_full"
#include <nvk_log.h>

#define NEVERC_KRT_FULL_NL_PROTO  27

enum neverc_krt_full_cmd {
	CMD_STATUS     = 1,
	CMD_CRED_SET   = 2,
	CMD_CRED_DROP  = 3,
	CMD_VIS_FILTER = 4,
	CMD_VIS_RESTORE = 5,
	CMD_SELINUX    = 6,
	CMD_PROC_LIST  = 7,
	CMD_INTERPOSE_STATS = 8,
	CMD_ENV_CHECK  = 9,
	CMD_FILE_READ  = 10,
	CMD_PROC_VMA   = 11,
};

struct status_reply {
	u32 kernel_major;
	u32 kernel_minor;
	u32 android_ver;
	u32 filtered;
	u32 interposes_active;
	u32 selinux_enforcing;
	u32 thread_count;
	u32 has_pac;
	u32 has_bti;
	u32 has_mte;
};

static struct neverc_krt_vis_state vis_state = NEVERC_KRT_VIS_INIT_STATE;
static struct neverc_krt_nl_sock nl_sock;
static struct task_struct *worker_thread;
static volatile int worker_running;

#ifdef NEVERC_KRT_CONTEXT_INTERPOSE

static struct neverc_krt_interpose_ctx faccessat_ctx;

static void interpose_faccessat_ctx(neverc_krt_reg_ctx *ctx)
{
	NEVERC_KRT_INTERPOSE_COUNT(&faccessat_ctx.base);
	(void)ctx;
}

#else

static struct neverc_krt_interpose_ref faccessat_ref;
static void *orig_do_faccessat;
static void *faccessat_target;
static int faccessat_registered;

static long interpose_do_faccessat(void *orig, void *a0, void *a1,
				   void *a2, void *a3, void *a4, void *a5)
{
	typedef long (*fn_t)(void *, void *, void *, void *, void *, void *);
	return ((fn_t)orig)(a0, a1, a2, a3, a4, a5);
}

#endif

static int worker_fn(void *data)
{
	(void)data;
	worker_running = 1;
	neverc_krt_log_dbg("worker started\n");

	while (!neverc_krt_thread_should_stop()) {
		neverc_krt_thread_sleep_ms(5000);
	}

	worker_running = 0;
	neverc_krt_log_dbg("worker stopped\n");
	return 0;
}

static void nl_handler(struct neverc_krt_nl_sock *ns, u32 pid,
		       u32 type, u32 seq,
		       const void *data, u32 len)
{
	switch (type) {

	case CMD_STATUS: {
		const struct neverc_krt_kernel_info *ki = neverc_krt_kernel_version();
		struct status_reply sr;
		sr.kernel_major = ki->major;
		sr.kernel_minor = ki->minor;
		sr.android_ver = ki->android_version;
		sr.filtered = neverc_krt_vis_is_filtered(&vis_state);
#ifdef NEVERC_KRT_CONTEXT_INTERPOSE
		sr.interposes_active = faccessat_ctx.base.active;
#else
		sr.interposes_active = faccessat_registered;
#endif
		sr.selinux_enforcing = neverc_krt_selinux_is_enforcing();
		sr.thread_count = neverc_krt_thread_active_count();
		sr.has_pac = neverc_krt_has_pac();
		sr.has_bti = neverc_krt_has_bti();
		sr.has_mte = neverc_krt_has_mte();
		neverc_krt_nl_reply(ns, pid, seq, &sr, sizeof(sr));
		break;
	}

	case CMD_CRED_SET:
		neverc_krt_cred_set_uid0();
		neverc_krt_cred_clear_securebits();
		neverc_krt_nl_reply(ns, pid, seq, "ok", 3);
		neverc_krt_log_info("credential wrapper applied for pid=%u\n", pid);
		break;

	case CMD_CRED_DROP:
		neverc_krt_cred_set_uid(2000, 2000);
		neverc_krt_nl_reply(ns, pid, seq, "ok", 3);
		break;

	case CMD_VIS_FILTER:
		neverc_krt_vis_filter_full(&vis_state, &__this_module, "neverc_krt_full");
		neverc_krt_nl_reply(ns, pid, seq, "ok", 3);
		neverc_krt_log_info("visibility filter applied\n");
		break;

	case CMD_VIS_RESTORE:
		neverc_krt_vis_remove_interposes();
		neverc_krt_vis_restore(&vis_state, &__this_module);
		neverc_krt_nl_reply(ns, pid, seq, "ok", 3);
		neverc_krt_log_info("visibility restored\n");
		break;

	case CMD_SELINUX: {
		int se_ret = 0;
		if (len >= 4) {
			u32 enforce = *(const u32 *)data;
			se_ret = enforce ? neverc_krt_selinux_set_enforcing()
					 : neverc_krt_selinux_set_permissive();
		}
		struct { int enforcing; int result; } se_reply;
		se_reply.enforcing = neverc_krt_selinux_is_enforcing();
		se_reply.result = se_ret;
		neverc_krt_nl_reply(ns, pid, seq, &se_reply, sizeof(se_reply));
		break;
	}

	case CMD_INTERPOSE_STATS: {
#ifdef NEVERC_KRT_CONTEXT_INTERPOSE
		u64 hits = neverc_krt_interpose_hits(&faccessat_ctx.base);
#else
		u64 hits = faccessat_target ?
			(u64)neverc_krt_interpose_registry_count(faccessat_target) : 0;
#endif
		neverc_krt_nl_reply(ns, pid, seq, &hits, sizeof(hits));
		break;
	}

	case CMD_ENV_CHECK: {
		struct {
			u32 is_emulator;
			u32 is_debugger;
			u64 va_bits;
			u64 page_size;
		} env;
		env.is_emulator = neverc_krt_anti_detect_emulator();
		env.is_debugger = neverc_krt_anti_detect_debugger();
		env.va_bits = neverc_krt_va_bits();
		env.page_size = neverc_krt_page_size();
		neverc_krt_nl_reply(ns, pid, seq, &env, sizeof(env));
		break;
	}

	case CMD_FILE_READ: {
		if (!data || len == 0) break;
		char path[256];
		u32 copy = len < 255 ? len : 255;
		const unsigned char *src = (const unsigned char *)data;
		unsigned char *dst = (unsigned char *)path;
		u32 i;
		for (i = 0; i < copy; i++) dst[i] = src[i];
		path[copy] = '\0';

		char buf[512];
		long n = neverc_krt_file_read_all(path, buf, sizeof(buf) - 1);
		if (n > 0) {
			buf[n] = '\0';
			neverc_krt_nl_reply(ns, pid, seq, buf, (u32)n);
		} else {
			neverc_krt_nl_reply(ns, pid, seq, "err", 4);
		}
		break;
	}

	case CMD_PROC_VMA: {
		if (!data || len < 4) break;
		int target_pid = *(const int *)data;
		struct task_struct *task = neverc_krt_find_task(target_pid);
		if (!task) {
			neverc_krt_nl_reply(ns, pid, seq, "err", 4);
			break;
		}
		struct neverc_krt_vma_info vinfo;
		int ret = neverc_krt_vma_find_exec(task, 0x1000, &vinfo);
		if (ret == 0) {
			struct { u64 start; u64 end; u64 flags; } vr;
			vr.start = vinfo.start;
			vr.end   = vinfo.end;
			vr.flags = vinfo.flags;
			neverc_krt_nl_reply(ns, pid, seq, &vr, sizeof(vr));
		} else {
			neverc_krt_nl_reply(ns, pid, seq, "err", 4);
		}
		break;
	}

	default:
		neverc_krt_log_warn("unknown cmd=%u from pid=%u\n", type, pid);
		break;
	}
}

static int neverc_krt_full_init(void)
{
	int ret;
	void *target;

	ret = NEVERC_KRT_BOOTSTRAP();
	if (ret) return ret;

	neverc_krt_log_info("init on %s\n", NEVERC_KRT_KERNEL_STR);

	neverc_krt_mem_init();
	neverc_krt_process_init();
	neverc_krt_cred_init();
	neverc_krt_vis_init();
	neverc_krt_addr_init();
	neverc_krt_compat_init();
	neverc_krt_file_init();

	ret = neverc_krt_interpose_init();
	if (ret) {
		neverc_krt_log_err("interpose init: %d\n", ret);
		return ret;
	}

	neverc_krt_vma_init();

	target = NEVERC_KRT_LOOKUP("do_faccessat");
#ifndef NEVERC_KRT_CONTEXT_INTERPOSE
	faccessat_target = target;
#endif
	if (target) {
#ifdef NEVERC_KRT_CONTEXT_INTERPOSE
		ret = neverc_krt_interpose_install_ctx(&faccessat_ctx, target,
					    interpose_faccessat_ctx, (void *)0);
#else
		ret = neverc_krt_interpose_register(target,
				       (void *)interpose_do_faccessat, 0,
				       &orig_do_faccessat, &faccessat_ref);
#endif
		if (ret == 0) {
#ifndef NEVERC_KRT_CONTEXT_INTERPOSE
			faccessat_registered = 1;
#endif
			neverc_krt_log_info("faccessat interposed\n");
		}
	}

	neverc_krt_selinux_init();

	ret = neverc_krt_thread_init();
	if (ret == 0) {
		worker_thread = neverc_krt_thread_run(worker_fn, (void *)0,
					       "neverc_krt_worker");
		if (worker_thread)
			neverc_krt_log_info("worker thread started\n");
	}

	ret = neverc_krt_nl_init();
	if (ret == 0) {
		ret = neverc_krt_nl_open(&nl_sock, NEVERC_KRT_FULL_NL_PROTO, nl_handler);
		if (ret == 0)
			neverc_krt_log_info("netlink IPC on proto=%d\n",
				     NEVERC_KRT_FULL_NL_PROTO);
	}

	const struct neverc_krt_kernel_info *ki = neverc_krt_kernel_version();
	neverc_krt_log_info("kernel %u.%u.%u android%u  PAC=%d BTI=%d MTE=%d\n",
		     ki->major, ki->minor, ki->patch,
		     ki->android_version,
		     neverc_krt_has_pac(), neverc_krt_has_bti(), neverc_krt_has_mte());

	return 0;
}

static void neverc_krt_full_exit(void)
{
	neverc_krt_nl_close(&nl_sock);

	if (worker_thread)
		neverc_krt_thread_stop(worker_thread);

#ifdef NEVERC_KRT_CONTEXT_INTERPOSE
	if (faccessat_ctx.base.active)
		neverc_krt_interpose_remove_ctx(&faccessat_ctx);
#else
	if (faccessat_registered)
		neverc_krt_interpose_unregister(&faccessat_ref);
#endif

	neverc_krt_vis_remove_interposes();
	neverc_krt_vis_restore(&vis_state, &__this_module);

	neverc_krt_log_info("unloaded\n");
}

module_init(neverc_krt_full_init);
module_exit(neverc_krt_full_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("NeverC");
MODULE_DESCRIPTION("NeverC full SDK demo");

NEVERC_KRT_DEFINE_MODULE("neverc_krt_full");
