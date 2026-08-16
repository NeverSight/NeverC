/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_PROCESS_H
#define NEVERC_KRT_PROCESS_H

#include <linux/types.h>
#include <linux/sched.h>

int neverc_krt_process_init(void);

#define NEVERC_KRT_TASK_LAYOUT_WALK       (1U << 0)
#define NEVERC_KRT_TASK_LAYOUT_REF        (1U << 1)
#define NEVERC_KRT_TASK_LAYOUT_USER_STATE (1U << 2)
#define NEVERC_KRT_TASK_LAYOUT_THREADS    (1U << 3)

/* Return 1 when every requested task-layout class is covered by the selected
 * family table (EXACT or same-series COMPAT) and the non-sleeping
 * read/lifetime helpers are ready.  A certificate may overlay offsets; it is
 * not required.  A zero request is available; unknown flag bits are rejected.
 * Call during module initialization, before Hooks. */
int neverc_krt_task_layout_available(unsigned int required);

int neverc_krt_current_pid(void);

int neverc_krt_current_tgid(void);

int neverc_krt_task_pid(struct task_struct *task);

int neverc_krt_task_tgid(struct task_struct *task);

#define NEVERC_KRT_TASK_THREAD_IDS_MAX 4096U

/*
 * Copy live, distinct thread IDs from task's thread group into a scalar-only
 * caller buffer.  The runtime owns the RCU read-side section and retains no
 * task pointer.  Returns the number written (0..capacity); a return equal to
 * capacity means the snapshot may be truncated.  Every unused output slot is
 * zeroed.  Invalid arguments, unsupported/unproven layouts, corrupt lists,
 * cycles, and read failures return a negative errno and expose no partial
 * snapshot.  capacity must be in 1..NEVERC_KRT_TASK_THREAD_IDS_MAX.
 */
int neverc_krt_task_thread_ids(struct task_struct *task, int *tids,
			       size_t capacity);

/* Return 1 for a user task with a non-NULL mm, 0 for a kernel task, and -1
 * when the selected profile or field read cannot be proven.  The task pointer
 * must remain valid for the duration of this scalar snapshot. */
int neverc_krt_task_has_user_mm(struct task_struct *task);

/*
 * Return a value snapshot of task->parent's TGID.  The parent pointer is read
 * inside a runtime-owned RCU read-side section and never escapes this call.
 * Returns -1 when the task, active profile, RCU backend, or parent is absent.
 */
int neverc_krt_task_parent_tgid(struct task_struct *task);

/*
 * Test task itself and its task->parent chain for target_tgid.  The walk is
 * RCU protected and has a fixed defensive depth limit; 1 means found, 0 means
 * a complete non-match, and -1 means the walk could not be completed safely.
 */
int neverc_krt_task_has_tgid_ancestor(struct task_struct *task,
				       int target_tgid);

/* Scalar identity returned together with an opaque ancestry-name walk. */
struct neverc_krt_task_identity {
	int pid;
	int tgid;
	u32 uid;
};

/*
 * The predicate receives a private, NUL-terminated 16-byte comm snapshot.  It
 * runs inside the runtime-owned RCU read-side section and therefore must not
 * sleep or retain the pointer.  Return >0 for a match, 0 to continue, <0 to
 * abort the walk.
 */
typedef int (*neverc_krt_task_comm_predicate_t)(const char *comm, void *data);

/*
 * Match the task's thread-group leader and each real-parent group leader while
 * keeping every task_struct pointer inside the profile-backed runtime.  The
 * caller receives only pid/tgid/uid scalar values.  Returns 1 for a predicate
 * match, 0 for a complete non-match, and -1 when the active profile, RCU
 * backend, credential snapshot, or any task pointer cannot be proven safe.
 */
int neverc_krt_task_match_group_ancestry(
	struct task_struct *task, unsigned int max_depth,
	neverc_krt_task_comm_predicate_t predicate, void *data,
	struct neverc_krt_task_identity *identity);

/* Value-only snapshot of the user register state saved on an ARM64 task stack. */
struct neverc_krt_task_user_state {
	unsigned int flags;
	unsigned long pc;
	unsigned long pstate;
	int user_mode;
};

/*
 * Pin task's kernel stack for the duration of the copy, then return only
 * scalar values.  No task-stack or pt_regs pointer escapes.  The caller must
 * supply a task pointer that is valid for the duration of this call (for
 * example current, a referenced task, or a live task argument from a kernel
 * callback).  Returns 0 on success and -1 on any unavailable/invalid state.
 */
int neverc_krt_task_user_state_snapshot(
	struct task_struct *task, struct neverc_krt_task_user_state *snapshot);

/* Returns a referenced current task. Pair with neverc_krt_put_task(). */
struct task_struct *neverc_krt_get_current_task(void);

/* Acquire a reference to an existing task. Pair with neverc_krt_put_task(). */
struct task_struct *neverc_krt_get_task(struct task_struct *task);

/* Returns a referenced task. Pair every successful lookup with put_task(). */
struct task_struct *neverc_krt_find_task(int pid);
void neverc_krt_put_task(struct task_struct *task);

/*
 * Returns a raw pointer into task_struct->comm.  Prefer
 * neverc_krt_task_comm_safe() which copies into a caller buffer.
 */
const char *neverc_krt_task_comm(struct task_struct *task);

int neverc_krt_task_comm_safe(struct task_struct *task, char *buf, int bufsz);

typedef int (*neverc_krt_task_callback_t)(struct task_struct *task, void *data);

/*
 * Callback receives task pointers valid only for the duration of the
 * callback.  Do not store or use them after the callback returns.  The
 * callback must not sleep.  A negative return means the list snapshot was
 * incomplete; scalar side effects from earlier callbacks are temporary and
 * callers must discard them.  A non-zero callback return is an intentional
 * successful stop and preserves the historical count-before-stop result.
 */
int neverc_krt_for_each_task(neverc_krt_task_callback_t callback, void *data);

/* Returns a referenced task. Pair every successful lookup with put_task(). */
struct task_struct *neverc_krt_find_task_by_name(const char *name);

int neverc_krt_send_signal(int pid, int sig);

int neverc_krt_is_current_root(void);

#endif /* NEVERC_KRT_PROCESS_H */
