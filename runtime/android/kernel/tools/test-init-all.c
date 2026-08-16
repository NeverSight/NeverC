// SPDX-License-Identifier: GPL-2.0
/* Host behavior fixture for the runtime initialization coordinator. */

#include "test-init-all-shim.h"

#include <assert.h>
#include <stddef.h>

static int fixture_bootstrap_status;
static int fixture_bootstrap_profile;
static int fixture_init_status[NEVERC_KRT_SUB_COUNT];
static int fixture_init_calls[NEVERC_KRT_SUB_COUNT];
static int fixture_interpose_cleanup_status;
static int fixture_vis_cleanup_status;
static int fixture_binder_cleanup_status;
static int fixture_ptmap_release_calls;
static int fixture_reenter_mem_init;
static int fixture_reentrant_init_status;
static int fixture_reentrant_cleanup_status;

static void fixture_reset(void)
{
	int sub;

	fixture_bootstrap_status = 0;
	fixture_bootstrap_profile = 0;
	fixture_interpose_cleanup_status = 0;
	fixture_vis_cleanup_status = 0;
	fixture_binder_cleanup_status = 0;
	fixture_ptmap_release_calls = 0;
	fixture_reenter_mem_init = 0;
	fixture_reentrant_init_status = 0;
	fixture_reentrant_cleanup_status = 0;
	for (sub = 0; sub < NEVERC_KRT_SUB_COUNT; sub++) {
		fixture_init_status[sub] = 0;
		fixture_init_calls[sub] = 0;
	}
}

int neverc_krt_bootstrap(int cfi, int kernel_profile)
{
	assert(cfi == 1);
	fixture_bootstrap_profile = kernel_profile;
	return fixture_bootstrap_status;
}

#define DEFINE_FIXTURE_INIT(function_name, subsystem) \
	int function_name(void) \
	{ \
		fixture_init_calls[subsystem]++; \
		return fixture_init_status[subsystem]; \
	}

int neverc_krt_mem_init(void)
{
	fixture_init_calls[NEVERC_KRT_SUB_MEM]++;
	if (fixture_reenter_mem_init) {
		fixture_reenter_mem_init = 0;
		fixture_reentrant_init_status = neverc_krt_init_all();
		fixture_reentrant_cleanup_status = neverc_krt_cleanup_all();
	}
	return fixture_init_status[NEVERC_KRT_SUB_MEM];
}

DEFINE_FIXTURE_INIT(neverc_krt_compat_init, NEVERC_KRT_SUB_COMPAT)
DEFINE_FIXTURE_INIT(neverc_krt_process_init, NEVERC_KRT_SUB_PROCESS)
DEFINE_FIXTURE_INIT(neverc_krt_cred_init, NEVERC_KRT_SUB_CRED)
DEFINE_FIXTURE_INIT(neverc_krt_vis_init, NEVERC_KRT_SUB_VIS)
DEFINE_FIXTURE_INIT(neverc_krt_addr_init, NEVERC_KRT_SUB_ADDR)
DEFINE_FIXTURE_INIT(neverc_krt_file_init, NEVERC_KRT_SUB_FILE)
DEFINE_FIXTURE_INIT(neverc_krt_selinux_init, NEVERC_KRT_SUB_SELINUX)
DEFINE_FIXTURE_INIT(neverc_krt_thread_init, NEVERC_KRT_SUB_THREAD)
DEFINE_FIXTURE_INIT(neverc_krt_nl_init, NEVERC_KRT_SUB_NETLINK)
DEFINE_FIXTURE_INIT(neverc_krt_interpose_init, NEVERC_KRT_SUB_INTERPOSE)
DEFINE_FIXTURE_INIT(neverc_krt_syscall_init, NEVERC_KRT_SUB_SYSCALL)
DEFINE_FIXTURE_INIT(neverc_krt_ksyms_init, NEVERC_KRT_SUB_KSYMS)
DEFINE_FIXTURE_INIT(neverc_krt_xmem_init, NEVERC_KRT_SUB_XMEM)
DEFINE_FIXTURE_INIT(neverc_krt_ns_init, NEVERC_KRT_SUB_NS)
DEFINE_FIXTURE_INIT(neverc_krt_binder_init, NEVERC_KRT_SUB_BINDER)
DEFINE_FIXTURE_INIT(neverc_krt_timer_init, NEVERC_KRT_SUB_TIMER)
DEFINE_FIXTURE_INIT(neverc_krt_power_init, NEVERC_KRT_SUB_POWER)
DEFINE_FIXTURE_INIT(neverc_krt_cpu_init, NEVERC_KRT_SUB_CPU)
DEFINE_FIXTURE_INIT(neverc_krt_vma_init, NEVERC_KRT_SUB_VMA)

unsigned long kallsyms_lookup_name(const char *name)
{
	(void)name;
	return 0;
}

int neverc_krt_interpose_install(
	struct neverc_krt_interpose *handle, void *target,
	void *replacement, void **original)
{
	(void)handle;
	(void)target;
	(void)replacement;
	(void)original;
	return -1;
}

int neverc_krt_interpose_install_ctx(
	struct neverc_krt_interpose_ctx *handle, void *target,
	neverc_krt_ctx_handler_t handler, void **call_original)
{
	(void)handle;
	(void)target;
	(void)handler;
	(void)call_original;
	return -1;
}

int neverc_krt_interpose_auto(
	struct neverc_krt_interpose *handle, void *target,
	void *replacement, void **original,
	struct neverc_krt_ftrace_interpose *ftrace)
{
	(void)handle;
	(void)target;
	(void)replacement;
	(void)original;
	(void)ftrace;
	return -1;
}

int _neverc_krt_user_ptmap_claim_cleanup(void)
{
	return 0;
}

void _neverc_krt_user_ptmap_release_cleanup(void)
{
	fixture_ptmap_release_calls++;
}
void neverc_krt_thread_stop_all(void) {}
int neverc_krt_vis_pause_interposes(void) { return 0; }
int neverc_krt_selinux_pause_interposes(void) { return 0; }
int neverc_krt_binder_cleanup(void)
{
	return fixture_binder_cleanup_status;
}
void neverc_krt_vis_file_rewrite_cleanup(void) {}
void neverc_krt_vis_cmdline_filter_cleanup(void) {}
void neverc_krt_vis_net_cleanup(void) {}
void neverc_krt_vis_dmesg_suppress_cleanup(void) {}
void neverc_krt_vis_kmsg_read_filter_cleanup(void) {}
void neverc_krt_vis_pid_cleanup(void) {}
void neverc_krt_vis_mount_filter_cleanup(void) {}
void neverc_krt_vis_maps_filter_cleanup(void) {}
void neverc_krt_vis_proc_attr_filter_cleanup(void) {}
void neverc_krt_se_selective_cleanup(void) {}
void neverc_krt_selinux_remove_interposes(void) {}
int neverc_krt_vis_remove_interposes(void)
{
	return fixture_vis_cleanup_status;
}
int neverc_krt_interpose_cleanup(void)
{
	return fixture_interpose_cleanup_status;
}
int neverc_krt_ftrace_init(void) { return 0; }

static void check_success_records_every_subsystem(void)
{
	const struct neverc_krt_state *state;
	int sub;

	fixture_reset();
	fixture_reenter_mem_init = 1;
	assert(!neverc_krt_sub_ok(NEVERC_KRT_SUB_MEM));
	assert(neverc_krt_init_all() == 0);
	assert(fixture_reentrant_init_status == -EBUSY);
	assert(fixture_reentrant_cleanup_status == -EBUSY);
	assert(fixture_bootstrap_profile == NEVERC_KRT_KERNEL);

	state = neverc_krt_get_state();
	assert(state->ready == 1);
	for (sub = 0; sub < NEVERC_KRT_SUB_COUNT; sub++) {
		assert(fixture_init_calls[sub] == 1);
		assert(neverc_krt_sub_ok(sub));
	}

	assert(neverc_krt_init_all() == 0);
	for (sub = 0; sub < NEVERC_KRT_SUB_COUNT; sub++)
		assert(fixture_init_calls[sub] == 1);
	assert(neverc_krt_cleanup_all() == 0);
	assert(state->ready == 0);
	assert(!neverc_krt_sub_ok(NEVERC_KRT_SUB_MEM));
	assert(fixture_ptmap_release_calls == 1);
}

static void check_optional_subsystem_does_not_block_ready(void)
{
	const struct neverc_krt_state *state;
	static const int optional[] = {
		NEVERC_KRT_SUB_VIS,
		NEVERC_KRT_SUB_ADDR,
		NEVERC_KRT_SUB_FILE,
		NEVERC_KRT_SUB_SELINUX,
		NEVERC_KRT_SUB_NETLINK,
		NEVERC_KRT_SUB_SYSCALL,
		NEVERC_KRT_SUB_XMEM,
		NEVERC_KRT_SUB_NS,
		NEVERC_KRT_SUB_BINDER,
		NEVERC_KRT_SUB_POWER,
	};
	int i;

	fixture_reset();
	for (i = 0; i < (int)(sizeof(optional) / sizeof(optional[0])); i++)
		fixture_init_status[optional[i]] = -30 - i;
	assert(neverc_krt_init_all() == 0);

	state = neverc_krt_get_state();
	assert(state->ready == 1);
	assert(neverc_krt_sub_ok(NEVERC_KRT_SUB_MEM));
	assert(neverc_krt_sub_ok(NEVERC_KRT_SUB_COMPAT));
	assert(neverc_krt_sub_ok(NEVERC_KRT_SUB_CRED));
	assert(neverc_krt_sub_ok(NEVERC_KRT_SUB_THREAD));
	assert(neverc_krt_sub_ok(NEVERC_KRT_SUB_INTERPOSE));
	assert(neverc_krt_sub_ok(NEVERC_KRT_SUB_KSYMS));
	assert(neverc_krt_sub_ok(NEVERC_KRT_SUB_TIMER));
	assert(neverc_krt_sub_ok(NEVERC_KRT_SUB_CPU));
	assert(neverc_krt_sub_ok(NEVERC_KRT_SUB_VMA));
	for (i = 0; i < (int)(sizeof(optional) / sizeof(optional[0])); i++) {
		assert(!neverc_krt_sub_ok(optional[i]));
		assert(state->sub_status[optional[i]] == -30 - i);
	}
	assert(neverc_krt_init_all() == 0);

	assert(neverc_krt_cleanup_all() == 0);
	for (i = 0; i < (int)(sizeof(optional) / sizeof(optional[0])); i++)
		fixture_init_status[optional[i]] = 0;
	assert(neverc_krt_init_all() == 0);
	assert(neverc_krt_cleanup_all() == 0);
}

static void check_required_mem_failure_blocks_ready(void)
{
	const struct neverc_krt_state *state;

	fixture_reset();
	fixture_init_status[NEVERC_KRT_SUB_MEM] = -11;
	assert(neverc_krt_init_all() == -11);

	state = neverc_krt_get_state();
	assert(state->ready == 0);
	assert(!neverc_krt_sub_ok(NEVERC_KRT_SUB_MEM));
	assert(state->sub_status[NEVERC_KRT_SUB_MEM] == -11);
	assert(neverc_krt_init_all() == -EAGAIN);

	assert(neverc_krt_cleanup_all() == 0);
	fixture_init_status[NEVERC_KRT_SUB_MEM] = 0;
	assert(neverc_krt_init_all() == 0);
	assert(neverc_krt_cleanup_all() == 0);
}

static void check_required_gki_helper_failure_blocks_ready(void)
{
	static const int required[] = {
		NEVERC_KRT_SUB_CRED,
		NEVERC_KRT_SUB_THREAD,
		NEVERC_KRT_SUB_INTERPOSE,
		NEVERC_KRT_SUB_KSYMS,
		NEVERC_KRT_SUB_TIMER,
		NEVERC_KRT_SUB_CPU,
		NEVERC_KRT_SUB_VMA,
	};
	const struct neverc_krt_state *state;
	int i;

	for (i = 0; i < (int)(sizeof(required) / sizeof(required[0])); i++) {
		fixture_reset();
		fixture_init_status[required[i]] = -17 - i;
		assert(neverc_krt_init_all() == -17 - i);

		state = neverc_krt_get_state();
		assert(state->ready == 0);
		assert(!neverc_krt_sub_ok(required[i]));
		assert(state->sub_status[required[i]] == -17 - i);
		assert(neverc_krt_init_all() == -EAGAIN);

		assert(neverc_krt_cleanup_all() == 0);
		fixture_init_status[required[i]] = 0;
		assert(neverc_krt_init_all() == 0);
		assert(neverc_krt_cleanup_all() == 0);
	}
}

static void check_partial_cleanup_never_restores_ready(void)
{
	const struct neverc_krt_state *state;

	fixture_reset();
	assert(neverc_krt_init_all() == 0);
	state = neverc_krt_get_state();
	assert(state->ready == 1);

	fixture_binder_cleanup_status = -61;
	assert(neverc_krt_cleanup_all() == -61);
	assert(state->ready == 0);
	assert(neverc_krt_init_all() == -EAGAIN);
	assert(fixture_ptmap_release_calls == 1);

	fixture_binder_cleanup_status = 0;
	fixture_vis_cleanup_status = -63;
	assert(neverc_krt_cleanup_all() == -63);
	assert(state->ready == 0);
	assert(neverc_krt_init_all() == -EAGAIN);
	assert(fixture_ptmap_release_calls == 2);

	fixture_vis_cleanup_status = 0;
	fixture_interpose_cleanup_status = -62;
	assert(neverc_krt_cleanup_all() == -62);
	assert(state->ready == 0);
	assert(neverc_krt_init_all() == -EAGAIN);
	assert(fixture_ptmap_release_calls == 3);

	fixture_interpose_cleanup_status = 0;
	assert(neverc_krt_cleanup_all() == 0);
	assert(fixture_ptmap_release_calls == 4);
	assert(neverc_krt_init_all() == 0);
	assert(neverc_krt_cleanup_all() == 0);
}

static void check_bootstrap_failure_stays_inactive(void)
{
	const struct neverc_krt_state *state;

	fixture_reset();
	fixture_bootstrap_status = -41;
	assert(neverc_krt_init_all() == -41);

	state = neverc_krt_get_state();
	assert(state->ready == 0);
	assert(state->sub_status[NEVERC_KRT_SUB_COMPAT] == -41);
	assert(!neverc_krt_sub_ok(NEVERC_KRT_SUB_COMPAT));

	fixture_bootstrap_status = 0;
	assert(neverc_krt_init_all() == 0);
	assert(neverc_krt_cleanup_all() == 0);
}

int main(void)
{
	check_success_records_every_subsystem();
	check_optional_subsystem_does_not_block_ready();
	check_required_mem_failure_blocks_ready();
	check_required_gki_helper_failure_blocks_ready();
	check_partial_cleanup_never_restores_ready();
	check_bootstrap_failure_stays_inactive();
	return 0;
}
