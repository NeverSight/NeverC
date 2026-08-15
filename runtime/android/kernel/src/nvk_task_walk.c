// SPDX-License-Identifier: GPL-2.0
#ifdef NEVERC_KRT_TASK_WALK_HOST_TEST
#include "../tools/test-task-walk-shim.h"
#else
#include <linux/errno.h>
#include <linux/list.h>
#include <nvk.h>
#include "nvk_internal.h"

#ifndef EOPNOTSUPP
#define EOPNOTSUPP ENOTSUPP
#endif
#ifndef ELOOP
#define ELOOP 40
#endif

#define _NEVERC_KRT_TASK_WALK_LIMIT 4096UL
#define NEVERC_KRT_TASK_WALK_POINTER_VALID(pointer) \
	_neverc_krt_kernel_pointer_is_valid(pointer)
#define NEVERC_KRT_TASK_WALK_RCU_LOCK() _neverc_krt_rcu_read_begin()
#define NEVERC_KRT_TASK_WALK_RCU_UNLOCK() _neverc_krt_rcu_read_end()
#define NEVERC_KRT_TASK_WALK_RCU_AVAILABLE() _neverc_krt_rcu_available()
#define NEVERC_KRT_TASK_WALK_NOFAULT_AVAILABLE() \
	_neverc_krt_mem_nofault_available()
#define NEVERC_KRT_TASK_RUNTIME_INIT() neverc_krt_process_init()
#define NEVERC_KRT_TASK_PID_AVAILABLE() _neverc_krt_task_pid_available()
#define NEVERC_KRT_TASK_REF_AVAILABLE() _neverc_krt_task_ref_available()
#define NEVERC_KRT_TASK_USER_STATE_AVAILABLE() \
	_neverc_krt_task_user_state_available()

static struct task_struct *_neverc_krt_task_walk_init_task;

int _neverc_krt_task_walk_init(void)
{
	struct task_struct *task;

	if (__atomic_load_n(&_neverc_krt_task_walk_init_task,
				    __ATOMIC_ACQUIRE))
		return 0;
	task = (struct task_struct *)NEVERC_KRT_LOOKUP("init_task");
	if (!task || !NEVERC_KRT_TASK_WALK_POINTER_VALID(task))
		return -ENOENT;
	__atomic_store_n(&_neverc_krt_task_walk_init_task, task,
			 __ATOMIC_RELEASE);
	return 0;
}

static struct task_struct *_neverc_krt_task_walk_get_init_task(void)
{
	if (!__atomic_load_n(&_neverc_krt_task_walk_init_task,
				     __ATOMIC_ACQUIRE)) {
		if (neverc_krt_process_init() || _neverc_krt_task_walk_init())
			return (struct task_struct *)0;
	}
	return __atomic_load_n(&_neverc_krt_task_walk_init_task,
			       __ATOMIC_ACQUIRE);
}

#define NEVERC_KRT_TASK_WALK_INIT_TASK() \
	_neverc_krt_task_walk_get_init_task()
#endif

static __always_inline int _neverc_krt_task_walk_field_fits(
	unsigned long size, unsigned long offset, unsigned long width)
{
	return size && offset < size && width <= size - offset;
}

int neverc_krt_task_layout_available(unsigned int required)
{
	const unsigned int supported =
		NEVERC_KRT_TASK_LAYOUT_WALK |
		NEVERC_KRT_TASK_LAYOUT_REF |
		NEVERC_KRT_TASK_LAYOUT_USER_STATE |
		NEVERC_KRT_TASK_LAYOUT_THREADS;
	unsigned long certificates = 0;

	if (required & ~supported)
		return 0;
	if (!required)
		return 1;
	if (required & NEVERC_KRT_TASK_LAYOUT_WALK)
		certificates |= NEVERC_KRT_LAYOUT_CERT_TASK_WALK;
	if (required & NEVERC_KRT_TASK_LAYOUT_REF)
		certificates |= NEVERC_KRT_LAYOUT_CERT_TASK_REF;
	if (required & NEVERC_KRT_TASK_LAYOUT_USER_STATE)
		certificates |= NEVERC_KRT_LAYOUT_CERT_TASK_USER_STATE;
	if (required & NEVERC_KRT_TASK_LAYOUT_THREADS)
		certificates |= NEVERC_KRT_LAYOUT_CERT_TASK_THREADS;
	if (!_neverc_krt_get_proven_gki_layout(certificates))
		return 0;
	if (NEVERC_KRT_TASK_RUNTIME_INIT())
		return 0;
	if ((required & (NEVERC_KRT_TASK_LAYOUT_WALK |
			 NEVERC_KRT_TASK_LAYOUT_USER_STATE |
			 NEVERC_KRT_TASK_LAYOUT_THREADS)) &&
	    !NEVERC_KRT_TASK_WALK_NOFAULT_AVAILABLE())
		return 0;
	if ((required & (NEVERC_KRT_TASK_LAYOUT_WALK |
			 NEVERC_KRT_TASK_LAYOUT_THREADS)) &&
	    !NEVERC_KRT_TASK_WALK_RCU_AVAILABLE())
		return 0;
	if ((required & NEVERC_KRT_TASK_LAYOUT_WALK) &&
	    !NEVERC_KRT_TASK_WALK_POINTER_VALID(
		NEVERC_KRT_TASK_WALK_INIT_TASK()))
		return 0;
	if ((required & NEVERC_KRT_TASK_LAYOUT_WALK) &&
	    !NEVERC_KRT_TASK_PID_AVAILABLE())
		return 0;
	if ((required & NEVERC_KRT_TASK_LAYOUT_REF) &&
	    !NEVERC_KRT_TASK_REF_AVAILABLE())
		return 0;
	if ((required & NEVERC_KRT_TASK_LAYOUT_USER_STATE) &&
	    !NEVERC_KRT_TASK_USER_STATE_AVAILABLE())
		return 0;
	return 1;
}

int neverc_krt_for_each_task(neverc_krt_task_callback_t callback, void *data)
{
	const struct neverc_krt_gki_layout *layout;
	struct task_struct *init;
	struct list_head *head;
	struct list_head *node = (struct list_head *)0;
	struct list_head *cycle_anchor;
	unsigned long cycle_power = 1;
	unsigned long cycle_span = 0;
	unsigned long count = 0;
	int result = -EFAULT;

	if (!callback)
		return -EINVAL;
	layout = _neverc_krt_get_proven_gki_layout(
		NEVERC_KRT_LAYOUT_CERT_TASK_WALK);
	if (!layout || !layout->task_tasks ||
	    !_neverc_krt_task_walk_field_fits(
		layout->task_size, layout->task_tasks,
		sizeof(struct list_head)))
		return -EOPNOTSUPP;
	if (!NEVERC_KRT_TASK_WALK_NOFAULT_AVAILABLE())
		return -EOPNOTSUPP;
	init = NEVERC_KRT_TASK_WALK_INIT_TASK();
	if (!NEVERC_KRT_TASK_WALK_POINTER_VALID(init) ||
	    (unsigned long)init > ~0UL - layout->task_tasks)
		return -EFAULT;
	head = (struct list_head *)((unsigned long)init + layout->task_tasks);
	if (!NEVERC_KRT_TASK_WALK_POINTER_VALID(head))
		return -EFAULT;

	result = NEVERC_KRT_TASK_WALK_RCU_LOCK();
	if (result)
		return -EOPNOTSUPP;
	result = -EFAULT;
	if (neverc_krt_mem_read(&node, &head->next, sizeof(node)) || !node)
		goto out_unlock;
	if (node == head) {
		result = 0;
		goto out_unlock;
	}
	cycle_anchor = node;

	while (node != head) {
		struct list_head *next = (struct list_head *)0;
		struct task_struct *task;

		if (count >= _NEVERC_KRT_TASK_WALK_LIMIT) {
			result = -E2BIG;
			goto out_unlock;
		}
		if (!NEVERC_KRT_TASK_WALK_POINTER_VALID(node) ||
		    (unsigned long)node < layout->task_tasks)
			goto out_unlock;
		task = (struct task_struct *)((unsigned long)node -
					    layout->task_tasks);
		if (!NEVERC_KRT_TASK_WALK_POINTER_VALID(task))
			goto out_unlock;
		if (neverc_krt_mem_read(&next, &node->next, sizeof(next)) ||
		    !next)
			goto out_unlock;
		if (next != head &&
		    !NEVERC_KRT_TASK_WALK_POINTER_VALID(next))
			goto out_unlock;
		if (next == node) {
			result = -ELOOP;
			goto out_unlock;
		}
		if (next != head) {
			cycle_span++;
			if (next == cycle_anchor) {
				result = -ELOOP;
				goto out_unlock;
			}
			if (cycle_span == cycle_power) {
				cycle_anchor = next;
				cycle_power <<= 1;
				cycle_span = 0;
			}
		}

		/* A non-zero callback result is an intentional successful stop. */
		if (callback(task, data)) {
			result = (int)count;
			goto out_unlock;
		}
		count++;
		node = next;
	}
	result = (int)count;

out_unlock:
	NEVERC_KRT_TASK_WALK_RCU_UNLOCK();
	return result;
}
