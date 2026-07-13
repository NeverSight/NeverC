/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/errno.h>
#include <linux/kallsyms.h>
#include <nvk_mem.h>
/* Host behavior tests inject typed backends without importing all ARM64 APIs. */
#ifndef NEVERC_KRT_USERCOPY_TEST
#include "nvk_internal.h"
#endif

typedef ssize_t (*neverc_krt_simple_read_from_buffer_fn)(
	void __user *to, size_t count, loff_t *ppos,
	const void *from, size_t available);
typedef ssize_t (*neverc_krt_simple_write_to_buffer_fn)(
	void *to, size_t available, loff_t *ppos,
	const void __user *from, size_t count);

#ifndef NEVERC_KRT_USERCOPY_READ_BACKEND
#define NEVERC_KRT_USERCOPY_READ_BACKEND(sym) \
	((neverc_krt_simple_read_from_buffer_fn)NEVERC_KRT_LOOKUP(sym))
#endif
#ifndef NEVERC_KRT_USERCOPY_WRITE_BACKEND
#define NEVERC_KRT_USERCOPY_WRITE_BACKEND(sym) \
	((neverc_krt_simple_write_to_buffer_fn)NEVERC_KRT_LOOKUP(sym))
#endif

static neverc_krt_simple_read_from_buffer_fn
	_neverc_krt_simple_read_from_buffer;
static neverc_krt_simple_write_to_buffer_fn
	_neverc_krt_simple_write_to_buffer;

void _neverc_krt_usercopy_init(void)
{
	_neverc_krt_simple_read_from_buffer =
		NEVERC_KRT_USERCOPY_READ_BACKEND("simple_read_from_buffer");
	_neverc_krt_simple_write_to_buffer =
		NEVERC_KRT_USERCOPY_WRITE_BACKEND("simple_write_to_buffer");
}

static size_t _neverc_krt_usercopy_copied(
	ssize_t result, size_t requested)
{
	if (result <= 0 || (size_t)result > requested)
		return 0;
	return (size_t)result;
}

unsigned long _neverc_krt_mem_copy_from_user_compat(
	void *to, const void __user *from, unsigned long n)
{
	loff_t pos = 0;
	size_t copied;
	unsigned char *tail;
	unsigned long i;
	ssize_t result;

	if (!n)
		return 0;
	if (!_neverc_krt_simple_write_to_buffer)
		return n;

	result = _neverc_krt_simple_write_to_buffer(
		to, (size_t)n, &pos, from, (size_t)n);
	copied = _neverc_krt_usercopy_copied(result, (size_t)n);
	tail = (unsigned char *)to + copied;
	for (i = (unsigned long)copied; i < n; i++)
		tail[i - (unsigned long)copied] = 0;
	return n - (unsigned long)copied;
}

unsigned long _neverc_krt_mem_copy_to_user_compat(
	void __user *to, const void *from, unsigned long n)
{
	loff_t pos = 0;
	size_t copied;
	ssize_t result;

	if (!n)
		return 0;
	if (!_neverc_krt_simple_read_from_buffer)
		return n;

	result = _neverc_krt_simple_read_from_buffer(
		to, (size_t)n, &pos, from, (size_t)n);
	copied = _neverc_krt_usercopy_copied(result, (size_t)n);
	return n - (unsigned long)copied;
}

long neverc_krt_mem_read_user(void *dst, const void __user *src, size_t len)
{
	if (!_neverc_krt_simple_write_to_buffer)
		return -1;
	return _neverc_krt_mem_copy_from_user_compat(
		dst, src, (unsigned long)len) ? -EFAULT : 0;
}

long neverc_krt_mem_write_user(
	void __user *dst, const void *src, size_t len)
{
	if (!_neverc_krt_simple_read_from_buffer)
		return -1;
	return _neverc_krt_mem_copy_to_user_compat(
		dst, src, (unsigned long)len) ? -EFAULT : 0;
}
