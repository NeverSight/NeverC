/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>
#include <linux/errno.h>
#include <linux/refcount.h>
#include "nvk_internal.h"

/* ---- file-local typedefs ---- */

typedef int  (*neverc_krt_task_pid_nr_fn)(struct task_struct *);
typedef int  (*neverc_krt_task_tgid_nr_fn)(struct task_struct *);
typedef int  (*neverc_krt_task_pid_nr_ns_fn)(struct task_struct *, int type,
					     void *ns);
typedef struct pid *(*neverc_krt_find_get_pid_fn)(int pid);
typedef struct task_struct *(*neverc_krt_get_pid_task_fn)(struct pid *pid,
							  int type);
typedef void (*neverc_krt_put_pid_fn)(struct pid *pid);
typedef void (*neverc_krt_put_task_struct_fn)(struct task_struct *task);
typedef void (*neverc_krt_put_task_stack_fn)(struct task_struct *task);
typedef void (*neverc_krt_release_task_stack_fn)(struct task_struct *task);
typedef int   (*neverc_krt_send_sig_info_fn)(int sig, void *info,
					     struct task_struct *p, int type);
typedef void (*neverc_krt_rcu_lock_fn)(void);
typedef void (*neverc_krt_rcu_unlock_fn)(void);

struct _neverc_krt_task_offsets {
	unsigned long tasks;
	unsigned long usage;
	volatile int resolved;
};

/* ---- file-local state ---- */

static neverc_krt_task_pid_nr_fn    _neverc_krt_task_pid_nr;
static neverc_krt_task_tgid_nr_fn   _neverc_krt_task_tgid_nr;
static neverc_krt_task_pid_nr_ns_fn _neverc_krt_task_pid_nr_ns;
static neverc_krt_find_get_pid_fn   _neverc_krt_find_get_pid;
static neverc_krt_get_pid_task_fn   _neverc_krt_get_pid_task;
static neverc_krt_put_pid_fn        _neverc_krt_put_pid;
static neverc_krt_put_task_struct_fn _neverc_krt_put_task_struct;
static neverc_krt_put_task_stack_fn _neverc_krt_put_task_stack;
static neverc_krt_release_task_stack_fn _neverc_krt_release_task_stack;
static neverc_krt_send_sig_info_fn  _neverc_krt_send_sig_info;
static int                          _neverc_krt_proc_inited;
static struct _neverc_krt_task_offsets _neverc_krt_toff;
static neverc_krt_rcu_lock_fn       _neverc_krt_rcu_read_lock;
static neverc_krt_rcu_unlock_fn     _neverc_krt_rcu_read_unlock;

/* Shared with nvk_anti.c through the private runtime interface. */
unsigned long _neverc_krt_off_comm = 0;

/* ---- implementation ---- */

static const struct _neverc_krt_task_offsets *_neverc_krt_task_offsets(void);

#define _NEVERC_KRT_TASK_ANCESTRY_LIMIT 256U
#define _NEVERC_KRT_ARM64_PSTATE_MODE_MASK 0xFUL

/*
 * ARM64 GKI (5.10–6.12+):
 *   #define task_pt_regs(p) \
 *     ((struct pt_regs *)(THREAD_SIZE + task_stack_page(p)) - 1)
 * i.e. stack + THREAD_SIZE - sizeof(*pt_regs).
 *
 * Do not subtract the historical THREAD_START_SP 16-byte pad.  That pad was
 * the initial *userspace* SP, not the kernel exception frame.  Subtracting
 * it here reads pt_regs.regs[30] (LR) as PC, so SIGNAL is_in_range never
 * matches the handshake stub page.
 */
static __always_inline unsigned long _neverc_krt_task_pt_regs_addr(
	unsigned long stack, unsigned long stack_size, unsigned long pt_regs_size)
{
	return stack + stack_size - pt_regs_size;
}

static __always_inline int _neverc_krt_task_field_fits(
	unsigned long size, unsigned long offset, unsigned long width)
{
	return size && offset < size && width <= size - offset;
}

static void _neverc_krt_ensure_rcu_ops(void)
{
	if (_neverc_krt_rcu_read_lock && _neverc_krt_rcu_read_unlock)
		return;
	if (!_neverc_krt_proc_inited)
		(void)neverc_krt_process_init();

	if (!_neverc_krt_rcu_read_lock) {
		_neverc_krt_rcu_read_lock =
			(neverc_krt_rcu_lock_fn)NEVERC_KRT_LOOKUP("rcu_read_lock");
		if (!_neverc_krt_rcu_read_lock)
			_neverc_krt_rcu_read_lock =
				(neverc_krt_rcu_lock_fn)NEVERC_KRT_LOOKUP(
					"__rcu_read_lock");
	}
	if (!_neverc_krt_rcu_read_unlock) {
		_neverc_krt_rcu_read_unlock =
			(neverc_krt_rcu_unlock_fn)NEVERC_KRT_LOOKUP(
				"rcu_read_unlock");
		if (!_neverc_krt_rcu_read_unlock)
			_neverc_krt_rcu_read_unlock =
				(neverc_krt_rcu_unlock_fn)NEVERC_KRT_LOOKUP(
					"__rcu_read_unlock");
	}
}

int _neverc_krt_rcu_read_begin(void)
{
	_neverc_krt_ensure_rcu_ops();
	if (!_neverc_krt_rcu_read_lock || !_neverc_krt_rcu_read_unlock)
		return -ENOTSUPP;
	_neverc_krt_rcu_read_lock();
	return 0;
}

void _neverc_krt_rcu_read_end(void)
{
	if (_neverc_krt_rcu_read_unlock)
		_neverc_krt_rcu_read_unlock();
}

int _neverc_krt_rcu_available(void)
{
	_neverc_krt_ensure_rcu_ops();
	return _neverc_krt_rcu_read_lock && _neverc_krt_rcu_read_unlock;
}

static void _neverc_krt_ensure_pid_ops(void)
{
	if ((_neverc_krt_task_pid_nr && _neverc_krt_task_tgid_nr) ||
	    _neverc_krt_task_pid_nr_ns)
		return;
	if (!_neverc_krt_proc_inited)
		neverc_krt_process_init();

	_neverc_krt_task_pid_nr =
		(neverc_krt_task_pid_nr_fn)NEVERC_KRT_LOOKUP("task_pid_nr");
	_neverc_krt_task_tgid_nr =
		(neverc_krt_task_tgid_nr_fn)NEVERC_KRT_LOOKUP("task_tgid_nr");
	if (!_neverc_krt_task_pid_nr || !_neverc_krt_task_tgid_nr)
		_neverc_krt_task_pid_nr_ns =
			(neverc_krt_task_pid_nr_ns_fn)NEVERC_KRT_LOOKUP(
					"__task_pid_nr_ns");
}

int _neverc_krt_task_pid_available(void)
{
	_neverc_krt_ensure_pid_ops();
	return (_neverc_krt_task_pid_nr && _neverc_krt_task_tgid_nr) ||
		_neverc_krt_task_pid_nr_ns;
}

static void _neverc_krt_ensure_task_lookup(void)
{
	_neverc_krt_ensure_rcu_ops();
	if (_neverc_krt_find_get_pid && _neverc_krt_get_pid_task &&
	    _neverc_krt_put_pid && _neverc_krt_put_task_struct)
		return;
	if (!_neverc_krt_proc_inited)
		neverc_krt_process_init();

	_neverc_krt_find_get_pid =
		(neverc_krt_find_get_pid_fn)NEVERC_KRT_LOOKUP("find_get_pid");
	_neverc_krt_get_pid_task =
		(neverc_krt_get_pid_task_fn)NEVERC_KRT_LOOKUP("get_pid_task");
	_neverc_krt_put_pid =
		(neverc_krt_put_pid_fn)NEVERC_KRT_LOOKUP("put_pid");
	_neverc_krt_put_task_struct =
		(neverc_krt_put_task_struct_fn)NEVERC_KRT_LOOKUP(
			"__put_task_struct");
}

static void _neverc_krt_ensure_task_stack_ops(void)
{
	if (_neverc_krt_put_task_stack || _neverc_krt_release_task_stack)
		return;
	if (!_neverc_krt_proc_inited)
		(void)neverc_krt_process_init();
	_neverc_krt_put_task_stack =
		(neverc_krt_put_task_stack_fn)NEVERC_KRT_LOOKUP(
			"put_task_stack");
	if (_neverc_krt_put_task_stack)
		return;
	_neverc_krt_release_task_stack =
		(neverc_krt_release_task_stack_fn)NEVERC_KRT_LOOKUP(
			"release_task_stack");
}

int _neverc_krt_task_ref_available(void)
{
	_neverc_krt_ensure_task_lookup();
	return _neverc_krt_find_get_pid && _neverc_krt_get_pid_task &&
		_neverc_krt_put_pid && _neverc_krt_put_task_struct;
}

int _neverc_krt_task_user_state_available(void)
{
	_neverc_krt_ensure_task_stack_ops();
	return _neverc_krt_put_task_stack ||
		_neverc_krt_release_task_stack;
}

static void _neverc_krt_ensure_send_sig(void)
{
	if (_neverc_krt_send_sig_info)
		return;
	if (!_neverc_krt_proc_inited)
		neverc_krt_process_init();

	_neverc_krt_ensure_task_lookup();
	_neverc_krt_send_sig_info =
		(neverc_krt_send_sig_info_fn)NEVERC_KRT_LOOKUP("send_sig_info");
}

static void _neverc_krt_resolve_task_offsets(void)
{
	const struct neverc_krt_gki_layout *layout;

	if (__atomic_load_n(&_neverc_krt_toff.resolved, __ATOMIC_ACQUIRE))
		return;

	layout = _neverc_krt_get_proven_gki_layout(
		NEVERC_KRT_LAYOUT_CERT_TASK_WALK);
	if (layout) {
		_neverc_krt_toff.tasks = layout->task_tasks;
		__atomic_store_n(&_neverc_krt_off_comm, layout->task_comm,
				 __ATOMIC_RELEASE);
	}
	layout = _neverc_krt_get_proven_gki_layout(
		NEVERC_KRT_LAYOUT_CERT_TASK_REF);
	if (layout)
		_neverc_krt_toff.usage = layout->task_usage;

	__atomic_store_n(&_neverc_krt_toff.resolved, 1, __ATOMIC_RELEASE);
}

static int _neverc_krt_task_get(struct task_struct *task)
{
	const struct neverc_krt_gki_layout *layout;
	unsigned long usage_off;
	refcount_t *usage;

	if (!task || !_neverc_krt_kernel_pointer_is_valid(task))
		return -1;
	layout = _neverc_krt_get_proven_gki_layout(
		NEVERC_KRT_LAYOUT_CERT_TASK_REF);
	if (!layout)
		return -1;
	usage_off = _neverc_krt_task_offsets()->usage;
	if (!_neverc_krt_task_field_fits(
		layout->task_size, usage_off,
		sizeof(*usage)) || (unsigned long)task > ~0UL - usage_off)
		return -1;
	usage = (refcount_t *)((unsigned long)task + usage_off);
	return refcount_inc_not_zero(usage) ? 0 : -1;
}

static const struct _neverc_krt_task_offsets *_neverc_krt_task_offsets(void)
{
	if (!__atomic_load_n(&_neverc_krt_toff.resolved, __ATOMIC_ACQUIRE))
		_neverc_krt_resolve_task_offsets();
	return &_neverc_krt_toff;
}

int neverc_krt_process_init(void)
{
	int ret;

	if (_neverc_krt_proc_inited) return 0;

	ret = neverc_krt_mem_init();
	if (ret)
		return ret;
	if (neverc_krt_compat_init())
		return -1;
	_neverc_krt_resolve_task_offsets();

	_neverc_krt_proc_inited = 1;
	/* Hooks may invoke these accessors from non-sleepable paths.  Resolve the
	 * required helpers during bootstrap instead of doing the first kallsyms
	 * walk inside an active Hook.  Missing helpers remain a fail-closed API
	 * result and do not make the unrelated process runtime unusable. */
	_neverc_krt_ensure_pid_ops();
	_neverc_krt_ensure_rcu_ops();
	_neverc_krt_ensure_task_lookup();
	_neverc_krt_ensure_task_stack_ops();
	(void)_neverc_krt_task_walk_init();
	return 0;
}

int neverc_krt_current_pid(void)
{
	_neverc_krt_ensure_pid_ops();
	if (_neverc_krt_task_pid_nr)
		return _neverc_krt_task_pid_nr(current);
	if (_neverc_krt_task_pid_nr_ns)
		return _neverc_krt_task_pid_nr_ns(current, 0 /* PIDTYPE_PID */,
						  (void *)0);
	return -1;
}

int neverc_krt_current_tgid(void)
{
	_neverc_krt_ensure_pid_ops();
	if (_neverc_krt_task_tgid_nr)
		return _neverc_krt_task_tgid_nr(current);
	if (_neverc_krt_task_pid_nr_ns)
		return _neverc_krt_task_pid_nr_ns(current, 1 /* PIDTYPE_TGID */,
						  (void *)0);
	return -1;
}

int neverc_krt_task_pid(struct task_struct *task)
{
	_neverc_krt_ensure_pid_ops();
	if (_neverc_krt_task_pid_nr && task)
		return _neverc_krt_task_pid_nr(task);
	if (_neverc_krt_task_pid_nr_ns && task)
		return _neverc_krt_task_pid_nr_ns(task, 0 /* PIDTYPE_PID */,
						  (void *)0);
	return -1;
}

int neverc_krt_task_tgid(struct task_struct *task)
{
	_neverc_krt_ensure_pid_ops();
	if (_neverc_krt_task_tgid_nr && task)
		return _neverc_krt_task_tgid_nr(task);
	if (_neverc_krt_task_pid_nr_ns && task)
		return _neverc_krt_task_pid_nr_ns(task, 1 /* PIDTYPE_TGID */,
						  (void *)0);
	return -1;
}

int neverc_krt_task_has_user_mm(struct task_struct *task)
{
	const struct neverc_krt_gki_layout *layout;
	void *mm = (void *)0;

	if (!task || !_neverc_krt_kernel_pointer_is_valid(task))
		return -1;
	if (!_neverc_krt_mem_nofault_available())
		return -1;
	layout = _neverc_krt_get_proven_gki_layout(
		NEVERC_KRT_LAYOUT_CERT_TASK_WALK);
	if (!layout || !layout->task_mm)
		return -1;
	if (neverc_krt_mem_read(&mm,
			(const char *)task + layout->task_mm, sizeof(mm)))
		return -1;
	return mm ? 1 : 0;
}

static int _neverc_krt_task_pointer_is_valid(struct task_struct *task)
{
	return _neverc_krt_kernel_pointer_is_valid(task);
}

static int _neverc_krt_read_task_pointer(struct task_struct *task,
					 unsigned long offset,
					 struct task_struct **value)
{
	struct task_struct *result = (void *)0;

	if (!task || !_neverc_krt_task_pointer_is_valid(task) || !offset ||
	    !value || (unsigned long)task > ~0UL - offset)
		return -1;
	if (neverc_krt_mem_read(&result, (const char *)task + offset,
				sizeof(result)))
		return -1;
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	if (result && !_neverc_krt_task_pointer_is_valid(result))
		return -1;
	*value = result;
	return 0;
}

static int _neverc_krt_task_tgid_ready(struct task_struct *task)
{
	if (_neverc_krt_task_tgid_nr)
		return _neverc_krt_task_tgid_nr(task);
	if (_neverc_krt_task_pid_nr_ns)
		return _neverc_krt_task_pid_nr_ns(task, 1 /* PIDTYPE_TGID */,
						  (void *)0);
	return -1;
}

int neverc_krt_task_parent_tgid(struct task_struct *task)
{
	const struct neverc_krt_gki_layout *layout;
	struct task_struct *parent = (void *)0;
	int result = -1;

	if (!task || !_neverc_krt_task_pointer_is_valid(task))
		return -1;
	_neverc_krt_ensure_pid_ops();
	layout = _neverc_krt_get_proven_gki_layout(
		NEVERC_KRT_LAYOUT_CERT_TASK_WALK);
	if (!layout || !layout->task_parent ||
	    (!_neverc_krt_task_tgid_nr && !_neverc_krt_task_pid_nr_ns) ||
	    !_neverc_krt_mem_nofault_available() ||
	    _neverc_krt_rcu_read_begin())
		return -1;

	if (!_neverc_krt_read_task_pointer(task, layout->task_parent, &parent) &&
	    parent)
		result = _neverc_krt_task_tgid_ready(parent);
	_neverc_krt_rcu_read_end();
	return result;
}

int neverc_krt_task_has_tgid_ancestor(struct task_struct *task,
				       int target_tgid)
{
	const struct neverc_krt_gki_layout *layout;
	struct task_struct *walk;
	unsigned int depth;
	int result = -1;

	if (!task || !_neverc_krt_task_pointer_is_valid(task) ||
	    target_tgid <= 0)
		return -1;
	_neverc_krt_ensure_pid_ops();
	layout = _neverc_krt_get_proven_gki_layout(
		NEVERC_KRT_LAYOUT_CERT_TASK_WALK);
	if (!layout || !layout->task_parent ||
	    (!_neverc_krt_task_tgid_nr && !_neverc_krt_task_pid_nr_ns) ||
	    !_neverc_krt_mem_nofault_available() ||
	    _neverc_krt_rcu_read_begin())
		return -1;

	walk = task;
	for (depth = 0; depth < _NEVERC_KRT_TASK_ANCESTRY_LIMIT; depth++) {
		struct task_struct *parent = (void *)0;
		int tgid = _neverc_krt_task_tgid_ready(walk);

		if (tgid < 0)
			break;
		if (tgid == target_tgid) {
			result = 1;
			break;
		}
		if (_neverc_krt_read_task_pointer(walk, layout->task_parent,
						  &parent))
			break;
		if (!parent || parent == walk) {
			result = 0;
			break;
		}
		walk = parent;
	}
	_neverc_krt_rcu_read_end();
	return result;
}

int neverc_krt_task_match_group_ancestry(
	struct task_struct *task, unsigned int max_depth,
	neverc_krt_task_comm_predicate_t predicate, void *data,
	struct neverc_krt_task_identity *identity)
{
	const struct neverc_krt_gki_layout *layout;
	struct neverc_krt_cred_ids ids;
	struct neverc_krt_task_identity value = { 0 };
	struct task_struct *walk;
	unsigned int depth;
	int result = -1;

	if (!task || !_neverc_krt_task_pointer_is_valid(task) || !predicate ||
	    !identity || max_depth == 0 ||
	    max_depth > _NEVERC_KRT_TASK_ANCESTRY_LIMIT)
		return -1;

	_neverc_krt_ensure_pid_ops();
	layout = _neverc_krt_get_proven_gki_layout(
		NEVERC_KRT_LAYOUT_CERT_TASK_WALK);
	if (!layout || !layout->task_group_leader ||
	    !layout->task_real_parent || !layout->task_comm ||
	    (!_neverc_krt_task_pid_nr && !_neverc_krt_task_pid_nr_ns) ||
	    (!_neverc_krt_task_tgid_nr && !_neverc_krt_task_pid_nr_ns) ||
	    !_neverc_krt_mem_nofault_available())
		return -1;

	if (_neverc_krt_rcu_read_begin())
		return -1;
	if (neverc_krt_cred_get_ids(task, &ids))
		goto out_unlock;
	value.pid = neverc_krt_task_pid(task);
	value.tgid = neverc_krt_task_tgid(task);
	value.uid = ids.uid;
	if (value.pid < 0 || value.tgid < 0)
		goto out_unlock;
	walk = task;
	{
		struct task_struct *leader = (void *)0;

		if (_neverc_krt_read_task_pointer(
			    task, layout->task_group_leader, &leader))
			goto out_unlock;
		if (leader)
			walk = leader;
	}

	for (depth = 0; depth < max_depth; depth++) {
		struct task_struct *parent = (void *)0;
		struct task_struct *leader = (void *)0;
		char comm[32] = { 0 };
		unsigned long comm_size = layout->task_comm_size;
		int match;

		if (!comm_size || comm_size > sizeof(comm) ||
		    !layout->task_size ||
		    layout->task_comm + comm_size > layout->task_size)
			goto out_unlock;
		if (neverc_krt_mem_read(comm,
				(const char *)walk + layout->task_comm,
				comm_size - 1))
			goto out_unlock;
		comm[comm_size - 1] = '\0';
		match = predicate(comm, data);
		if (match < 0)
			goto out_unlock;
		if (match > 0) {
			result = 1;
			goto out_unlock;
		}

		if (_neverc_krt_read_task_pointer(
			    walk, layout->task_real_parent, &parent))
			goto out_unlock;
		if (!parent || parent == walk) {
			result = 0;
			goto out_unlock;
		}
		if (_neverc_krt_read_task_pointer(
			    parent, layout->task_group_leader, &leader))
			goto out_unlock;
		if (!leader)
			leader = parent;
		if (leader == walk) {
			result = 0;
			goto out_unlock;
		}
		walk = leader;
	}
	/* Preserve the original bounded-walk behavior: reaching the defensive
	 * limit is a complete non-match, not an unbounded parent traversal. */
	result = 0;

out_unlock:
	_neverc_krt_rcu_read_end();
	if (result >= 0)
		*identity = value;
	return result;
}

static void _neverc_krt_put_task_stack_ref(struct task_struct *task,
					    refcount_t *stack_ref)
{
	if (_neverc_krt_put_task_stack) {
		_neverc_krt_put_task_stack(task);
		return;
	}
	if (refcount_dec_and_test(stack_ref))
		_neverc_krt_release_task_stack(task);
}

int neverc_krt_task_user_state_snapshot(
	struct task_struct *task, struct neverc_krt_task_user_state *snapshot)
{
	const struct neverc_krt_gki_layout *layout;
	struct neverc_krt_task_user_state value = { 0 };
	refcount_t *stack_ref;
	unsigned long stack = 0;
	unsigned long regs;
	int result = -1;

	if (!snapshot)
		return -1;
	__builtin_memset(snapshot, 0, sizeof(*snapshot));
	if (!task || !_neverc_krt_kernel_pointer_is_valid(task) ||
	    !_neverc_krt_mem_nofault_available())
		return -1;
	if (!_neverc_krt_proc_inited && neverc_krt_process_init())
		return -1;
	_neverc_krt_ensure_task_stack_ops();
	layout = _neverc_krt_get_proven_gki_layout(
		NEVERC_KRT_LAYOUT_CERT_TASK_USER_STATE);
	if (!layout || !layout->task_stack || !layout->task_stack_refcount ||
	    !layout->task_stack_size || !layout->task_flags ||
	    !layout->pt_regs_size || !layout->pt_regs_pc ||
	    !layout->pt_regs_pstate ||
	    !_neverc_krt_task_field_fits(
		layout->task_size, layout->task_stack, sizeof(stack)) ||
	    !_neverc_krt_task_field_fits(
		layout->task_size, layout->task_stack_refcount,
		sizeof(refcount_t)) ||
	    !_neverc_krt_task_field_fits(
		layout->task_size, layout->task_flags, sizeof(value.flags)) ||
	    !_neverc_krt_task_field_fits(
		layout->pt_regs_size, layout->pt_regs_pc, sizeof(value.pc)) ||
	    !_neverc_krt_task_field_fits(
		layout->pt_regs_size, layout->pt_regs_pstate,
		sizeof(value.pstate)) ||
	    (!_neverc_krt_put_task_stack && !_neverc_krt_release_task_stack))
		return -1;
	if (!layout->task_stack_size ||
	    layout->pt_regs_size >= layout->task_stack_size)
		return -1;

	stack_ref = (refcount_t *)((char *)task + layout->task_stack_refcount);
	if (!refcount_inc_not_zero(stack_ref))
		return -1;

	if (neverc_krt_mem_read(&stack, (const char *)task + layout->task_stack,
				sizeof(stack)) ||
	    !_neverc_krt_kernel_pointer_is_valid((const void *)stack) ||
	    (stack & 15UL))
		goto out_put_stack;
	if (neverc_krt_mem_read(&value.flags,
				(const char *)task + layout->task_flags,
				sizeof(value.flags)))
		goto out_put_stack;
	if (stack > ~0UL - layout->task_stack_size)
		goto out_put_stack;
	regs = _neverc_krt_task_pt_regs_addr(stack, layout->task_stack_size,
					     layout->pt_regs_size);
	if (neverc_krt_mem_read(&value.pc,
				(const char *)regs + layout->pt_regs_pc,
				sizeof(value.pc)) ||
	    neverc_krt_mem_read(&value.pstate,
				(const char *)regs + layout->pt_regs_pstate,
				sizeof(value.pstate)))
		goto out_put_stack;

	value.user_mode =
		(value.pstate & _NEVERC_KRT_ARM64_PSTATE_MODE_MASK) == 0;
	*snapshot = value;
	result = 0;

out_put_stack:
	_neverc_krt_put_task_stack_ref(task, stack_ref);
	return result;
}

struct task_struct *neverc_krt_get_task(struct task_struct *task)
{
	_neverc_krt_ensure_task_lookup();
	if (!task || !_neverc_krt_put_task_struct)
		return (void *)0;
	return _neverc_krt_task_get(task) == 0 ? task : (void *)0;
}

struct task_struct *neverc_krt_get_current_task(void)
{
	return neverc_krt_get_task(current);
}

struct task_struct *neverc_krt_find_task(int pid)
{
	struct pid *pid_ref;
	struct task_struct *t;

	_neverc_krt_ensure_task_lookup();
	if (!_neverc_krt_get_proven_gki_layout(
		    NEVERC_KRT_LAYOUT_CERT_TASK_REF))
		return (void *)0;
	if (!_neverc_krt_find_get_pid || !_neverc_krt_get_pid_task ||
	    !_neverc_krt_put_pid)
		return (void *)0;

	pid_ref = _neverc_krt_find_get_pid(pid);
	if (!pid_ref)
		return (void *)0;
	t = _neverc_krt_get_pid_task(pid_ref, 0 /* PIDTYPE_PID */);
	_neverc_krt_put_pid(pid_ref);
	return t;
}

void neverc_krt_put_task(struct task_struct *task)
{
	const struct neverc_krt_gki_layout *layout;
	unsigned long usage_off;
	refcount_t *usage;

	if (!task || !_neverc_krt_kernel_pointer_is_valid(task))
		return;
	_neverc_krt_ensure_task_lookup();
	if (!_neverc_krt_put_task_struct)
		return;
	layout = _neverc_krt_get_proven_gki_layout(
		NEVERC_KRT_LAYOUT_CERT_TASK_REF);
	if (!layout)
		return;
	usage_off = _neverc_krt_task_offsets()->usage;
	if (!_neverc_krt_task_field_fits(
		layout->task_size, usage_off,
		sizeof(*usage)) || (unsigned long)task > ~0UL - usage_off)
		return;

	usage = (refcount_t *)((unsigned long)task + usage_off);
	if (refcount_dec_and_test(usage))
		_neverc_krt_put_task_struct(task);
}

struct _neverc_krt_find_ctx {
	const char *target;
	struct task_struct *result;
};

static int _neverc_krt_find_by_name_cb(struct task_struct *task, void *data)
{
	struct _neverc_krt_find_ctx *ctx = (struct _neverc_krt_find_ctx *)data;
	unsigned long off = __atomic_load_n(&_neverc_krt_off_comm,
					    __ATOMIC_ACQUIRE);
	const struct neverc_krt_gki_layout *layout;
	char buf[32];
	unsigned long comm_size;

	if (!off)
		return 0;
	layout = _neverc_krt_get_proven_gki_layout(
		NEVERC_KRT_LAYOUT_CERT_TASK_WALK);
	if (!layout || !layout->task_comm_size || !layout->task_size ||
	    off + layout->task_comm_size > layout->task_size)
		return 0;
	comm_size = layout->task_comm_size;
	if (comm_size > sizeof(buf))
		return 0;
	if (neverc_krt_mem_read(buf, (const char *)task + off, comm_size - 1))
		return 0;
	buf[comm_size - 1] = '\0';

	const char *a = buf;
	const char *b = ctx->target;
	while (*a && *b && *a == *b) { a++; b++; }
	if (*a == *b) {
		if (!_neverc_krt_task_get(task)) {
			ctx->result = task;
			return 1;
		}
	}
	return 0;
}

struct task_struct *neverc_krt_find_task_by_name(const char *name)
{
	struct _neverc_krt_find_ctx ctx = { .target = name, .result = (void *)0 };

	if (!name || !_neverc_krt_get_proven_gki_layout(
			     NEVERC_KRT_LAYOUT_CERT_TASK_WALK |
			     NEVERC_KRT_LAYOUT_CERT_TASK_REF) ||
	    !_neverc_krt_mem_nofault_available())
		return (void *)0;
	neverc_krt_for_each_task(_neverc_krt_find_by_name_cb, &ctx);
	return ctx.result;
}

int neverc_krt_send_signal(int pid, int sig)
{
	struct task_struct *task;
	int ret;

	_neverc_krt_ensure_send_sig();
	if (!_neverc_krt_send_sig_info)
		return -1;

	task = neverc_krt_find_task(pid);
	if (!task)
		return -3;
	ret = _neverc_krt_send_sig_info(sig, (void *)0, task, 0);
	neverc_krt_put_task(task);
	return ret;
}

int neverc_krt_is_current_root(void)
{
	struct neverc_krt_cred_ids ids;

	if (!_neverc_krt_proc_inited) return -1;
	if (neverc_krt_cred_get_ids(current, &ids))
		return -1;
	return ids.uid == 0 ? 1 : 0;
}

int neverc_krt_task_comm_safe(struct task_struct *task, char *buf, int bufsz)
{
	if (!buf || bufsz < 1) return -1;
	buf[0] = '\0';
	if (!task || !_neverc_krt_kernel_pointer_is_valid(task) ||
	    !_neverc_krt_get_proven_gki_layout(
		    NEVERC_KRT_LAYOUT_CERT_TASK_WALK) ||
	    !_neverc_krt_mem_nofault_available())
		return -1;

	if (!__atomic_load_n(&_neverc_krt_off_comm, __ATOMIC_ACQUIRE))
		neverc_krt_task_comm(task);

	unsigned long off = __atomic_load_n(&_neverc_krt_off_comm,
					    __ATOMIC_ACQUIRE);
	const struct neverc_krt_gki_layout *layout;
	int n;
	int max;

	if (!off) return -1;
	layout = _neverc_krt_get_proven_gki_layout(
		NEVERC_KRT_LAYOUT_CERT_TASK_WALK);
	if (!layout || !layout->task_comm_size || !layout->task_size ||
	    off + layout->task_comm_size > layout->task_size)
		return -1;
	max = (int)layout->task_comm_size;
	n = bufsz < max ? bufsz - 1 : max - 1;
	if (n < 0)
		return -1;
	if (neverc_krt_mem_read(buf, (const char *)task + off, n)) {
		buf[0] = '\0';
		return -1;
	}
	buf[n] = '\0';
	return 0;
}

const char *neverc_krt_task_comm(struct task_struct *task)
{
	unsigned long off;

	if (!task || !_neverc_krt_kernel_pointer_is_valid(task) ||
	    !_neverc_krt_get_proven_gki_layout(
		    NEVERC_KRT_LAYOUT_CERT_TASK_WALK))
		return "";

	off = __atomic_load_n(&_neverc_krt_off_comm, __ATOMIC_ACQUIRE);
	if (!off) {
		_neverc_krt_resolve_task_offsets();
		off = __atomic_load_n(&_neverc_krt_off_comm, __ATOMIC_ACQUIRE);
	}
	if (off)
		return (const char *)((unsigned long)task + off);
	return "";
}
