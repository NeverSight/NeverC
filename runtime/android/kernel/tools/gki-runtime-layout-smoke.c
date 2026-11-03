/* SPDX-License-Identifier: GPL-2.0 */
/* Exercise measured private layouts in a disposable guest, as PID 1. */
#include <nvk.h>
#include "../src/nvk_internal.h"
#include <linux/string.h>
#include <asm/preempt.h>

static struct neverc_krt_interpose_ctx context_hook;
static int context_hits;

static noinline unsigned long context_target(unsigned long value)
{
	__asm__ __volatile__("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop" ::: "memory");
	return value + 1;
}

static noinline unsigned long context_replacement(unsigned long value)
{
	return value + 5;
}

static unsigned long (*volatile context_invoke)(unsigned long) = context_target;

static void context_handler(neverc_krt_reg_ctx *context)
{
	context_hits++;
	NEVERC_KRT_CTX_REDIRECT(context, context_replacement);
}

static int check_context_redirect(void)
{
	unsigned long value;
	int status = neverc_krt_interpose_init();
	if (status)
		return -11;
	status = neverc_krt_interpose_install_ctx(
		&context_hook, (void *)context_target, context_handler, 0);
	if (status) {
		printk("NEVERC_GKI_RUNTIME_CTX_INSTALL_FAIL status=%d\n", status);
		return -12;
	}
	value = context_invoke(10);
	status = neverc_krt_interpose_remove_ctx(&context_hook);
	if (status) {
		printk("NEVERC_GKI_RUNTIME_CTX_REMOVE_FAIL status=%d\n", status);
		return -13;
	}
	return value == 15 && context_hits == 1 && context_invoke(10) == 11 ? 0 : -14;
}

int init_module(void)
{
	struct neverc_krt_task_user_state state;
	struct neverc_krt_cred_ids ids;
	struct neverc_krt_file maps = { 0 };
	struct neverc_krt_interpose guard = { 0 };
	const struct neverc_krt_runtime_caps *caps;
	const struct neverc_krt_gki_layout *layout;
	void *mm, *seq = 0, *priv = 0, *maps_mm = 0;
	char comm[32];
	int tids[4], count, result = 0;
	int pid, task_pid, tgid, comm_status;
	int saved_preempt;
	if (NEVERC_KRT_BOOTSTRAP() || neverc_krt_process_init() || neverc_krt_file_init())
		return -1;
	caps = _neverc_krt_current_caps();
	layout = _neverc_krt_get_proven_gki_layout(NEVERC_KRT_LAYOUT_CERT_FULL);
	/* The harness has one CPU. Exercise caller-side current readers too. */
	if (neverc_krt_cpu_id() != 0)
		return -8;
	saved_preempt = preempt_count();
	__preempt_count_add(1);
	count = preempt_count();
	__preempt_count_sub(1);
	if (count != saved_preempt + 1 || preempt_count() != saved_preempt)
		return -9;
	if (!neverc_krt_interpose_enter(&guard) ||
	    guard.guard != (unsigned long)current || neverc_krt_interpose_enter(&guard))
		return -10;
	neverc_krt_interpose_leave(&guard);
	pid = neverc_krt_current_pid();
	task_pid = neverc_krt_task_pid(current);
	tgid = neverc_krt_task_tgid(current);
	comm_status = neverc_krt_task_comm_safe(current, comm, sizeof(comm));
	if (!caps || !layout || pid != 1 || task_pid != 1 || tgid != 1 ||
	    comm_status < 0 || strcmp(comm, "init")) {
		printk("NEVERC_GKI_RUNTIME_TASK_FAIL pid=%d task_pid=%d tgid=%d comm_status=%d comm=%s comm_off=%lu pid_off=%lu\n",
		       pid, task_pid, tgid, comm_status, comm_status < 0 ? "<unavailable>" : comm,
		       layout ? layout->task_comm : 0, layout ? layout->task_pid : 0);
		return -2;
	}
	if (neverc_krt_cred_get_ids(current, &ids) || ids.uid || ids.euid || ids.gid)
		return -3;
	count = neverc_krt_task_thread_ids(current, tids, 4);
	if (count != 1 || tids[0] != 1 ||
	    neverc_krt_task_user_state_snapshot(current, &state) ||
	    !state.pc || !state.user_mode)
		return -4;
	mm = neverc_krt_task_mm_get(current);
	if (!mm)
		return -5;
	if (neverc_krt_file_open(&maps, "/proc/self/maps", NEVERC_KRT_O_RDONLY)) {
		result = -6;
		goto out_mm;
	}
	if (neverc_krt_mem_read(&seq, (char *)maps.filp +
		caps->procmap_ioctl.file_private_data, sizeof(seq)) || !seq ||
	    neverc_krt_mem_read(&priv, (char *)seq +
		caps->procmap_ioctl.seq_file_private, sizeof(priv)) || !priv ||
	    neverc_krt_mem_read(&maps_mm, (char *)priv +
		caps->procmap_ioctl.proc_maps_private_mm, sizeof(maps_mm)) ||
	    maps_mm != mm)
		result = -7;
	neverc_krt_file_close(&maps);
out_mm:
	neverc_krt_task_mm_put(mm);
	if (!result)
		result = check_context_redirect();
	if (!result)
		printk("NEVERC_GKI_RUNTIME_LAYOUT_PASS\n");
	return result;
}

void cleanup_module(void) {}
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("NeverC configured task and procmap layout verification");
NEVERC_KRT_DEFINE_MODULE("neverc_gki_smoke");
