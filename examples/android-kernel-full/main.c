/* SPDX-License-Identifier: GPL-2.0 */
#include <nvkmod.h>
#include <nvk_hook.h>
#include <nvk_hide.h>
#include <nvk_process.h>
#include <nvk_cred.h>
#include <nvk_mem.h>
#include <nvk_selinux.h>
#include <nvk_thread.h>
#include <nvk_netlink.h>
#include <nvk_file.h>
#include <nvk_compat.h>
#include <nvk_anti.h>
#include <nvk_addr.h>
#include <nvk_vma.h>

#define NVK_LOG_TAG "nvk_full"
#include <nvk_log.h>

#define NVK_FULL_NL_PROTO  27

enum nvk_full_cmd {
	CMD_STATUS     = 1,
	CMD_ROOT       = 2,
	CMD_UNROOT     = 3,
	CMD_HIDE       = 4,
	CMD_UNHIDE     = 5,
	CMD_SELINUX    = 6,
	CMD_PROC_LIST  = 7,
	CMD_HOOK_STATS = 8,
	CMD_ENV_CHECK  = 9,
	CMD_FILE_READ  = 10,
	CMD_PROC_VMA   = 11,
};

struct status_reply {
	u32 kernel_major;
	u32 kernel_minor;
	u32 android_ver;
	u32 hidden;
	u32 hooks_active;
	u32 selinux_enforcing;
	u32 thread_count;
	u32 has_pac;
	u32 has_bti;
	u32 has_mte;
};

static struct nvk_hide_state hide_state = NVK_HIDE_INIT_STATE;
static struct nvk_nl_sock nl_sock;
static struct task_struct *worker_thread;
static volatile int worker_running;

#ifdef NVK_CONTEXT_HOOK

static struct nvk_hook_ctx faccessat_ctx;

static void hook_faccessat_ctx(nvk_reg_ctx *ctx)
{
	(void)ctx;
}

#else

typedef long (*faccessat_fn)(int dfd, const char __user *filename,
			     int mode, int flags);
static struct nvk_hook faccessat_hook;
static faccessat_fn orig_do_faccessat;

static long hook_do_faccessat(int dfd, const char __user *filename,
			      int mode, int flags)
{
	if (!nvk_hook_enter(&faccessat_hook))
		return orig_do_faccessat(dfd, filename, mode, flags);

	long ret = orig_do_faccessat(dfd, filename, mode, flags);
	nvk_hook_leave(&faccessat_hook);
	return ret;
}

#endif

static int worker_fn(void *data)
{
	(void)data;
	worker_running = 1;
	nvk_log_dbg("worker started\n");

	while (!nvk_thread_should_stop()) {
		nvk_thread_sleep_ms(5000);
	}

	worker_running = 0;
	nvk_log_dbg("worker stopped\n");
	return 0;
}

static void nl_handler(struct nvk_nl_sock *ns, u32 pid,
		       u32 type, u32 seq,
		       const void *data, u32 len)
{
	switch (type) {

	case CMD_STATUS: {
		const struct nvk_kernel_info *ki = nvk_kernel_version();
		struct status_reply sr;
		sr.kernel_major = ki->major;
		sr.kernel_minor = ki->minor;
		sr.android_ver = ki->android_version;
		sr.hidden = nvk_mod_is_hidden(&hide_state);
#ifdef NVK_CONTEXT_HOOK
		sr.hooks_active = faccessat_ctx.base.active;
#else
		sr.hooks_active = faccessat_hook.active;
#endif
		sr.selinux_enforcing = nvk_selinux_is_enforcing();
		sr.thread_count = nvk_thread_active_count();
		sr.has_pac = nvk_has_pac();
		sr.has_bti = nvk_has_bti();
		sr.has_mte = nvk_has_mte();
		nvk_nl_reply(ns, pid, seq, &sr, sizeof(sr));
		break;
	}

	case CMD_ROOT:
		nvk_cred_set_root();
		nvk_cred_clear_securebits();
		nvk_nl_reply(ns, pid, seq, "ok", 3);
		nvk_log_info("root granted to pid=%u\n", pid);
		break;

	case CMD_UNROOT:
		nvk_cred_set_uid(2000, 2000);
		nvk_nl_reply(ns, pid, seq, "ok", 3);
		break;

	case CMD_HIDE:
		nvk_mod_full_hide(&hide_state, &__this_module, "nvk_full");
		nvk_nl_reply(ns, pid, seq, "ok", 3);
		nvk_log_info("hidden\n");
		break;

	case CMD_UNHIDE:
		_nvk_hide_cleanup(&hide_state, &__this_module);
		nvk_nl_reply(ns, pid, seq, "ok", 3);
		nvk_log_info("visible\n");
		break;

	case CMD_SELINUX: {
		int se_ret = 0;
		if (len >= 4) {
			u32 enforce = *(const u32 *)data;
			se_ret = enforce ? nvk_selinux_set_enforcing()
					 : nvk_selinux_set_permissive();
		}
		struct { int enforcing; int result; } se_reply;
		se_reply.enforcing = nvk_selinux_is_enforcing();
		se_reply.result = se_ret;
		nvk_nl_reply(ns, pid, seq, &se_reply, sizeof(se_reply));
		break;
	}

	case CMD_HOOK_STATS: {
#ifdef NVK_CONTEXT_HOOK
		u64 hits = nvk_hook_hits(&faccessat_ctx.base);
#else
		u64 hits = nvk_hook_hits(&faccessat_hook);
#endif
		nvk_nl_reply(ns, pid, seq, &hits, sizeof(hits));
		break;
	}

	case CMD_ENV_CHECK: {
		struct {
			u32 is_emulator;
			u32 is_debugger;
			u64 va_bits;
			u64 page_size;
		} env;
		env.is_emulator = nvk_anti_detect_emulator();
		env.is_debugger = nvk_anti_detect_debugger();
		env.va_bits = nvk_va_bits();
		env.page_size = nvk_page_size();
		nvk_nl_reply(ns, pid, seq, &env, sizeof(env));
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
		long n = nvk_file_read_all(path, buf, sizeof(buf) - 1);
		if (n > 0) {
			buf[n] = '\0';
			nvk_nl_reply(ns, pid, seq, buf, (u32)n);
		} else {
			nvk_nl_reply(ns, pid, seq, "err", 4);
		}
		break;
	}

	case CMD_PROC_VMA: {
		if (!data || len < 4) break;
		int target_pid = *(const int *)data;
		struct task_struct *task = nvk_find_task(target_pid);
		if (!task) {
			nvk_nl_reply(ns, pid, seq, "err", 4);
			break;
		}
		struct nvk_vma_info vinfo;
		int ret = nvk_vma_find_exec(task, 0x1000, &vinfo);
		if (ret == 0) {
			struct { u64 start; u64 end; u64 flags; } vr;
			vr.start = vinfo.start;
			vr.end   = vinfo.end;
			vr.flags = vinfo.flags;
			nvk_nl_reply(ns, pid, seq, &vr, sizeof(vr));
		} else {
			nvk_nl_reply(ns, pid, seq, "err", 4);
		}
		break;
	}

	default:
		nvk_log_warn("unknown cmd=%u from pid=%u\n", type, pid);
		break;
	}
}

static int nvk_full_init(void)
{
	int ret;
	void *target;

	ret = NVK_BOOTSTRAP();
	if (ret) return ret;

	nvk_log_info("init on %s\n", NVK_KERNEL_STR);

	nvk_mem_init();
	nvk_process_init();
	nvk_cred_init();
	nvk_hide_init();
	nvk_addr_init();
	nvk_compat_init();
	nvk_file_init();

	ret = nvk_hook_init();
	if (ret) {
		nvk_log_err("hook init: %d\n", ret);
		return ret;
	}

	nvk_vma_init();

	target = NVK_LOOKUP("do_faccessat");
	if (target) {
#ifdef NVK_CONTEXT_HOOK
		ret = nvk_hook_install_ctx(&faccessat_ctx, target,
					    hook_faccessat_ctx, (void *)0);
#else
		ret = nvk_hook_install(&faccessat_hook, target,
				       (void *)hook_do_faccessat,
				       (void **)&orig_do_faccessat);
#endif
		if (ret == 0)
			nvk_log_info("faccessat hooked\n");
	}

	nvk_selinux_init();

	ret = nvk_thread_init();
	if (ret == 0) {
		worker_thread = nvk_thread_run(worker_fn, (void *)0,
					       "nvk_worker");
		if (worker_thread)
			nvk_log_info("worker thread started\n");
	}

	ret = nvk_nl_init();
	if (ret == 0) {
		ret = nvk_nl_open(&nl_sock, NVK_FULL_NL_PROTO, nl_handler);
		if (ret == 0)
			nvk_log_info("netlink IPC on proto=%d\n",
				     NVK_FULL_NL_PROTO);
	}

	const struct nvk_kernel_info *ki = nvk_kernel_version();
	nvk_log_info("kernel %u.%u.%u android%u  PAC=%d BTI=%d MTE=%d\n",
		     ki->major, ki->minor, ki->patch,
		     ki->android_version,
		     nvk_has_pac(), nvk_has_bti(), nvk_has_mte());

	return 0;
}

static void nvk_full_exit(void)
{
	nvk_nl_close(&nl_sock);

	if (worker_thread)
		nvk_thread_stop(worker_thread);

#ifdef NVK_CONTEXT_HOOK
	if (faccessat_ctx.base.active)
		nvk_hook_remove_ctx(&faccessat_ctx);
#else
	if (faccessat_hook.active)
		nvk_hook_remove(&faccessat_hook);
#endif

	_nvk_hide_cleanup(&hide_state, &__this_module);

	nvk_log_info("unloaded\n");
}

module_init(nvk_full_init);
module_exit(nvk_full_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("NeverC");
MODULE_DESCRIPTION("NeverC full SDK demo");

NVK_DEFINE_MODULE("nvk_full");
