// SPDX-License-Identifier: GPL-2.0
/* Host fixture for transactional module-list unlink and restore helpers. */

#include "test-visibility-list-shim.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

struct fixture_list {
	struct list_head head;
	struct list_head a;
	struct list_head our;
	struct list_head b;
};

static unsigned int fixture_write_calls;
static uint64_t fixture_failed_writes;

long neverc_krt_mem_read(void *dst, const void *src, size_t len)
{
	memcpy(dst, src, len);
	return 0;
}

long neverc_krt_mem_write(void *dst, const void *src, size_t len)
{
	fixture_write_calls++;
	if (fixture_failed_writes & (1ULL << fixture_write_calls))
		return -EIO;
	memcpy(dst, src, len);
	return 0;
}

static void fixture_visible(struct fixture_list *list)
{
	memset(list, 0, sizeof(*list));
	list->head.next = &list->a;
	list->head.prev = &list->b;
	list->a.next = &list->our;
	list->a.prev = &list->head;
	list->our.next = &list->b;
	list->our.prev = &list->a;
	list->b.next = &list->head;
	list->b.prev = &list->our;
	fixture_write_calls = 0;
	fixture_failed_writes = 0;
}

static void fixture_hidden(struct fixture_list *list)
{
	memset(list, 0, sizeof(*list));
	list->head.next = &list->a;
	list->head.prev = &list->b;
	list->a.next = &list->b;
	list->a.prev = &list->head;
	list->b.next = &list->head;
	list->b.prev = &list->a;
	list->our.next = &list->our;
	list->our.prev = &list->our;
	fixture_write_calls = 0;
	fixture_failed_writes = 0;
}

static void check_unlink_and_restore(void)
{
	struct fixture_list list;
	struct list_head *saved_prev = (struct list_head *)0;
	struct list_head *saved_next = (struct list_head *)0;

	fixture_visible(&list);
	assert(_neverc_krt_vis_list_unlink(
		       &list.our, &saved_prev, &saved_next) == 0);
	assert(saved_prev == &list.a);
	assert(saved_next == &list.b);
	assert(list.a.next == &list.b);
	assert(list.b.prev == &list.a);
	assert(list.our.next == &list.our);
	assert(list.our.prev == &list.our);

	fixture_write_calls = 0;
	assert(_neverc_krt_vis_list_restore_neighbors(
		       &list.our, saved_prev, saved_next) == 0);
	assert(list.a.next == &list.our);
	assert(list.our.prev == &list.a);
	assert(list.our.next == &list.b);
	assert(list.b.prev == &list.our);

	fixture_hidden(&list);
	assert(_neverc_krt_vis_list_restore(
		       &list.head, &list.our) == 0);
	assert(list.head.next == &list.our);
	assert(list.our.prev == &list.head);
	assert(list.our.next == &list.a);
	assert(list.a.prev == &list.our);
}

static void check_unlink_rolls_back(void)
{
	struct fixture_list list;
	struct list_head *saved_prev = (struct list_head *)0;
	struct list_head *saved_next = (struct list_head *)0;

	fixture_visible(&list);
	fixture_failed_writes = 1ULL << 1;
	assert(_neverc_krt_vis_list_unlink(
		       &list.our, &saved_prev, &saved_next) == -EIO);
	assert(list.a.next == &list.our);
	assert(list.b.prev == &list.our);

	fixture_visible(&list);
	fixture_failed_writes = 1ULL << 2;
	assert(_neverc_krt_vis_list_unlink(
		       &list.our, &saved_prev, &saved_next) == -EIO);
	assert(list.a.next == &list.our);
	assert(list.b.prev == &list.our);

	fixture_visible(&list);
	fixture_failed_writes = (1ULL << 2) | (1ULL << 3);
	assert(_neverc_krt_vis_list_unlink(
		       &list.our, &saved_prev, &saved_next) == -EUCLEAN);
}

static void check_restore_rolls_back(void)
{
	struct fixture_list list;

	fixture_hidden(&list);
	fixture_failed_writes = 1ULL << 1;
	assert(_neverc_krt_vis_list_restore(
		       &list.head, &list.our) == -EIO);
	assert(list.head.next == &list.a);
	assert(list.a.prev == &list.head);
	assert(list.our.next == &list.our);

	fixture_hidden(&list);
	fixture_failed_writes = 1ULL << 2;
	assert(_neverc_krt_vis_list_restore(
		       &list.head, &list.our) == -EIO);
	assert(list.head.next == &list.a);
	assert(list.a.prev == &list.head);
	assert(list.our.next == &list.our);

	fixture_hidden(&list);
	fixture_failed_writes = (1ULL << 2) | (1ULL << 3);
	assert(_neverc_krt_vis_list_restore(
		       &list.head, &list.our) == -EUCLEAN);
}

int main(void)
{
	check_unlink_and_restore();
	check_unlink_rolls_back();
	check_restore_rolls_back();
	return 0;
}
