/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_WAIT_H
#define _NEVERC_KRT_LINUX_WAIT_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/compiler.h>
#include <linux/sched.h>

/* ---- wait_queue_head -------------------------------------------------- */

struct wait_queue_head {
	unsigned int __lock;        /* arch_spinlock_t (arm64: 4 bytes) */
	struct list_head head;      /* 8-byte aligned after 4-byte pad */
};
typedef struct wait_queue_head wait_queue_head_t;

_Static_assert(sizeof(wait_queue_head_t) == 24,
	       "wait_queue_head_t size mismatch for GKI arm64 (no debug)");

#define DECLARE_WAIT_QUEUE_HEAD(name)                                         \
	wait_queue_head_t name = {                                            \
		.__lock = 0,                                                  \
		.head = LIST_HEAD_INIT(name.head),                            \
	}

static __always_inline void init_waitqueue_head(wait_queue_head_t *wq)
{
	wq->__lock = 0;
	INIT_LIST_HEAD(&wq->head);
}

/* Wait-queue entry (created on stack by wait_event macros). */
typedef int (*wait_queue_func_t)(void *wq_entry, unsigned mode,
				 int flags, void *key);

struct wait_queue_entry {
	unsigned int flags;
	void *private;
	wait_queue_func_t func;
	struct list_head entry;
};
typedef struct wait_queue_entry wait_queue_entry_t;

_Static_assert(sizeof(wait_queue_entry_t) == 40,
	       "unexpected arm64 GKI wait_queue_entry layout");
_Static_assert(__builtin_offsetof(wait_queue_entry_t, entry) == 24,
	       "unexpected arm64 GKI wait_queue_entry.entry offset");

/* Exported helpers used by wait_event expansion. */
void prepare_to_wait(wait_queue_head_t *wq_head,
		     wait_queue_entry_t *wq_entry, int state);
long prepare_to_wait_event(wait_queue_head_t *wq_head,
			   wait_queue_entry_t *wq_entry, int state);
void finish_wait(wait_queue_head_t *wq_head, wait_queue_entry_t *wq_entry);
int autoremove_wake_function(void *wq_entry, unsigned mode,
			     int sync, void *key);

/* Task states for prepare_to_wait and wake_up macros. */
#define TASK_RUNNING         0x00000000
#define TASK_INTERRUPTIBLE   0x00000001
#define TASK_UNINTERRUPTIBLE 0x00000002
#define TASK_NORMAL          (TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE)

/*
 * wake_up* are always macros wrapping __wake_up (the real export).
 * __wake_up(wq_head, mode, nr_exclusive, key)
 */
void __wake_up(wait_queue_head_t *wq_head, unsigned int mode,
	       int nr_exclusive, void *key);

#define wake_up(wq)                   __wake_up(wq, TASK_NORMAL, 1, (void *)0)
#define wake_up_all(wq)               __wake_up(wq, TASK_NORMAL, 0, (void *)0)
#define wake_up_interruptible(wq)     __wake_up(wq, TASK_INTERRUPTIBLE, 1, (void *)0)
#define wake_up_interruptible_all(wq) __wake_up(wq, TASK_INTERRUPTIBLE, 0, (void *)0)

void schedule(void);

static __always_inline void
init_wait_entry(wait_queue_entry_t *wq_entry, int flags)
{
	wq_entry->flags = flags;
	wq_entry->private = current;
	wq_entry->func = autoremove_wake_function;
	INIT_LIST_HEAD(&wq_entry->entry);
}

#define ___wait_event(wq_head, condition, state, ret, cmd)                    \
	({                                                                    \
		wait_queue_entry_t __wqe;                                     \
		long __ret = (ret);                                           \
		init_wait_entry(&__wqe, 0);                                   \
		for (;;) {                                                    \
			long __int = prepare_to_wait_event(&(wq_head),        \
							   &__wqe, state);    \
			if (condition)                                        \
				break;                                        \
			if ((state) == TASK_INTERRUPTIBLE && __int) {          \
				__ret = __int;                                \
				break;                                        \
			}                                                     \
			cmd;                                                  \
		}                                                             \
		finish_wait(&(wq_head), &__wqe);                              \
		__ret;                                                        \
	})

#define wait_event(wq_head, condition)                                        \
	do {                                                                  \
		if (!(condition))                                             \
			___wait_event(wq_head, condition,                     \
				      TASK_UNINTERRUPTIBLE, 0, schedule());   \
	} while (0)

#define wait_event_interruptible(wq_head, condition)                          \
	({                                                                    \
		int __ret = 0;                                                \
		if (!(condition))                                             \
			__ret = ___wait_event(wq_head, condition,             \
					     TASK_INTERRUPTIBLE, -512,        \
					     schedule());                     \
		__ret;                                                        \
	})

/* ---- completion ------------------------------------------------------- */

struct completion {
	unsigned int done;
	struct wait_queue_head wait;
};

_Static_assert(sizeof(struct completion) == 32,
	       "unexpected arm64 GKI completion layout");
_Static_assert(__builtin_offsetof(struct completion, wait) == 8,
	       "unexpected arm64 GKI completion.wait offset");

#define DECLARE_COMPLETION(work)                                              \
	struct completion work = {                                            \
		.done = 0,                                                    \
		.wait = { .__lock = 0,                                        \
			  .head = LIST_HEAD_INIT((work).wait.head) },         \
	}

static __always_inline void init_completion(struct completion *x)
{
	x->done = 0;
	init_waitqueue_head(&x->wait);
}

static __always_inline void reinit_completion(struct completion *x)
{
	WRITE_ONCE(x->done, 0);
}

void wait_for_completion(struct completion *x);
int wait_for_completion_interruptible(struct completion *x);
unsigned long wait_for_completion_timeout(struct completion *x,
					  unsigned long timeout);
bool try_wait_for_completion(struct completion *x);
bool completion_done(struct completion *x);
void complete(struct completion *x);
void complete_all(struct completion *x);

#endif /* _NEVERC_KRT_LINUX_WAIT_H */
