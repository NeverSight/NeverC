// SPDX-License-Identifier: GPL-2.0
#ifdef NEVERC_KRT_THREAD_IDS_HOST_TEST
#include "../tools/test-task-thread-ids-shim.h"
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

#define NEVERC_KRT_THREAD_POINTER_VALID(pointer) \
	_neverc_krt_kernel_pointer_is_valid(pointer)
#define NEVERC_KRT_THREAD_RCU_LOCK() _neverc_krt_rcu_read_begin()
#define NEVERC_KRT_THREAD_RCU_UNLOCK() _neverc_krt_rcu_read_end()
#define NEVERC_KRT_THREAD_NOFAULT_AVAILABLE() \
	_neverc_krt_mem_nofault_available()
#define _NEVERC_KRT_THREAD_WALK_LIMIT \
	((unsigned long)NEVERC_KRT_TASK_THREAD_IDS_MAX)
#endif

static __always_inline int _neverc_krt_thread_field_fits(
	unsigned long object_size, unsigned long offset, unsigned long width)
{
	return object_size && offset < object_size &&
		width <= object_size - offset;
}

static __always_inline int _neverc_krt_thread_layout_is_valid(
	const struct neverc_krt_gki_layout *layout)
{
	if (!layout)
		return 0;
	return _neverc_krt_thread_field_fits(
			layout->task_size, layout->task_pid, sizeof(int)) &&
		_neverc_krt_thread_field_fits(
			layout->task_size, layout->task_thread_pid,
			sizeof(void *)) &&
		_neverc_krt_thread_field_fits(
			layout->task_size, layout->task_signal,
			sizeof(void *)) &&
		_neverc_krt_thread_field_fits(
			layout->task_size, layout->task_thread_node,
			sizeof(struct list_head)) &&
		_neverc_krt_thread_field_fits(
			layout->signal_size, layout->signal_thread_head,
			sizeof(struct list_head));
}

static __always_inline void _neverc_krt_thread_ids_clear(
	int *tids, size_t capacity)
{
	size_t i;

	for (i = 0; i < capacity; i++)
		tids[i] = 0;
}

static __always_inline int _neverc_krt_thread_id_seen(
	const int *tids, size_t count, int tid)
{
	size_t i;

	for (i = 0; i < count; i++) {
		if (tids[i] == tid)
			return 1;
	}
	return 0;
}

int neverc_krt_task_thread_ids(struct task_struct *task, int *tids,
			       size_t capacity)
{
	const struct neverc_krt_gki_layout *layout;
	struct list_head *head;
	struct list_head *node = (void *)0;
	void *signal = (void *)0;
	unsigned long walked = 0;
	unsigned long cycle_power = 1;
	unsigned long cycle_span = 0;
	struct list_head *cycle_anchor;
	size_t count = 0;
	int match;
	int result = -EFAULT;

	if (!tids || !capacity)
		return -EINVAL;
	if (capacity > NEVERC_KRT_TASK_THREAD_IDS_MAX)
		return -E2BIG;
	_neverc_krt_thread_ids_clear(tids, capacity);
	if (!task)
		return -EINVAL;
	if (!NEVERC_KRT_THREAD_POINTER_VALID(task))
		return -EFAULT;

	match = neverc_krt_check_kernel_match();
	if (match != NEVERC_KRT_VER_EXACT &&
	    match != NEVERC_KRT_VER_COMPAT)
		return -EOPNOTSUPP;
	layout = _neverc_krt_get_proven_gki_layout(
		NEVERC_KRT_LAYOUT_CERT_TASK_THREADS);
	if (!_neverc_krt_thread_layout_is_valid(layout))
		return -EOPNOTSUPP;
	if (!NEVERC_KRT_THREAD_NOFAULT_AVAILABLE())
		return -EOPNOTSUPP;

	if (NEVERC_KRT_THREAD_RCU_LOCK())
		return -EOPNOTSUPP;
	if (neverc_krt_mem_read(
			&signal, (const char *)task + layout->task_signal,
			sizeof(signal)))
		goto out_fail;
	if (!NEVERC_KRT_THREAD_POINTER_VALID(signal))
		goto out_fail;
	if ((uintptr_t)signal >
	    (uintptr_t)-1 - layout->signal_thread_head)
		goto out_fail;
	head = (struct list_head *)((uintptr_t)signal +
					 layout->signal_thread_head);
	if (!NEVERC_KRT_THREAD_POINTER_VALID(head))
		goto out_fail;
	if (neverc_krt_mem_read(&node, &head->next, sizeof(node)))
		goto out_fail;
	if (!node)
		goto out_fail;
	cycle_anchor = node;

	while (node != head) {
		struct list_head *next = (void *)0;
		struct task_struct *thread;
		void *thread_pid = (void *)0;
		int tid = 0;

		if (++walked > _NEVERC_KRT_THREAD_WALK_LIMIT) {
			result = -ELOOP;
			goto out_fail;
		}
		if (!NEVERC_KRT_THREAD_POINTER_VALID(node) ||
		    (uintptr_t)node < layout->task_thread_node)
			goto out_fail;
		thread = (struct task_struct *)((uintptr_t)node -
					       layout->task_thread_node);
		if (!NEVERC_KRT_THREAD_POINTER_VALID(thread))
			goto out_fail;
		if (neverc_krt_mem_read(
				&thread_pid,
				(const char *)thread + layout->task_thread_pid,
				sizeof(thread_pid)))
			goto out_fail;
		if (thread_pid && !NEVERC_KRT_THREAD_POINTER_VALID(thread_pid))
			goto out_fail;
		if (thread_pid) {
			if (neverc_krt_mem_read(
					&tid, (const char *)thread + layout->task_pid,
					sizeof(tid)))
				goto out_fail;
			if (tid <= 0)
				goto out_fail;
			if (!_neverc_krt_thread_id_seen(tids, count, tid)) {
				tids[count++] = tid;
				if (count == capacity) {
					result = (int)count;
					goto out_unlock;
				}
			}
		}

		if (neverc_krt_mem_read(&next, &node->next, sizeof(next)))
			goto out_fail;
		if (!next)
			goto out_fail;
		if (next == node) {
			result = -ELOOP;
			goto out_fail;
		}
		if (next != head) {
			cycle_span++;
			if (next == cycle_anchor) {
				result = -ELOOP;
				goto out_fail;
			}
			if (cycle_span == cycle_power) {
				cycle_anchor = next;
				cycle_power <<= 1;
				cycle_span = 0;
			}
		}
		node = next;
	}

	result = (int)count;
	goto out_unlock;

out_fail:
	_neverc_krt_thread_ids_clear(tids, capacity);
out_unlock:
	NEVERC_KRT_THREAD_RCU_UNLOCK();
	return result;
}
