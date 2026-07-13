/* SPDX-License-Identifier: GPL-2.0 */
#define __builtin_neverc_xorstr(s) (s)

#include <linux/types.h>

typedef ssize_t (*test_read_backend_fn)(
	void __user *to, size_t count, loff_t *ppos,
	const void *from, size_t available);
typedef ssize_t (*test_write_backend_fn)(
	void *to, size_t available, loff_t *ppos,
	const void __user *from, size_t count);

static test_read_backend_fn test_lookup_read_backend(const char *name);
static test_write_backend_fn test_lookup_write_backend(const char *name);

#define NEVERC_KRT_USERCOPY_TEST 1
#define NEVERC_KRT_USERCOPY_READ_BACKEND(sym) test_lookup_read_backend(sym)
#define NEVERC_KRT_USERCOPY_WRITE_BACKEND(sym) test_lookup_write_backend(sym)
#include "../src/nvk_usercopy.c"

#define TEST_BACKEND_READ  (1U << 0)
#define TEST_BACKEND_WRITE (1U << 1)

static unsigned int test_backend_mask;
static unsigned int test_read_lookups;
static unsigned int test_write_lookups;
static unsigned int test_read_calls;
static unsigned int test_write_calls;
static ssize_t test_read_result;
static ssize_t test_write_result;

static int test_streq(const char *left, const char *right)
{
	while (*left && *left == *right) {
		left++;
		right++;
	}
	return *left == *right;
}

static void test_fill(unsigned char *dst, unsigned char value, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++)
		dst[i] = value;
}

static int test_bytes_equal(const unsigned char *left,
			    const unsigned char *right, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		if (left[i] != right[i])
			return 0;
	}
	return 1;
}

static ssize_t test_simple_read_from_buffer(
	void __user *to, size_t count, loff_t *ppos,
	const void *from, size_t available)
{
	size_t copied = 0;
	size_t i;

	test_read_calls++;
	if (test_read_result > 0 &&
	    (size_t)test_read_result <= count &&
	    (size_t)test_read_result <= available)
		copied = (size_t)test_read_result;
	for (i = 0; i < copied; i++)
		((unsigned char *)to)[i] = ((const unsigned char *)from)[i];
	*ppos += (loff_t)copied;
	return test_read_result;
}

static ssize_t test_simple_write_to_buffer(
	void *to, size_t available, loff_t *ppos,
	const void __user *from, size_t count)
{
	size_t copied = 0;
	size_t i;

	test_write_calls++;
	if (test_write_result > 0 &&
	    (size_t)test_write_result <= count &&
	    (size_t)test_write_result <= available)
		copied = (size_t)test_write_result;
	for (i = 0; i < copied; i++)
		((unsigned char *)to)[i] = ((const unsigned char *)from)[i];
	*ppos += (loff_t)copied;
	return test_write_result;
}

static test_read_backend_fn test_lookup_read_backend(const char *name)
{
	if (!test_streq(name, "simple_read_from_buffer"))
		return 0;
	test_read_lookups++;
	if (test_backend_mask & TEST_BACKEND_READ)
		return test_simple_read_from_buffer;
	return 0;
}

static test_write_backend_fn test_lookup_write_backend(const char *name)
{
	if (!test_streq(name, "simple_write_to_buffer"))
		return 0;
	test_write_lookups++;
	if (test_backend_mask & TEST_BACKEND_WRITE)
		return test_simple_write_to_buffer;
	return 0;
}

static void test_select_backends(unsigned int mask)
{
	test_backend_mask = mask;
	test_read_lookups = 0;
	test_write_lookups = 0;
	test_read_calls = 0;
	test_write_calls = 0;
	_neverc_krt_usercopy_init();
}

#define TEST_CHECK(condition, code) \
	do { \
		if (!(condition)) \
			return (code); \
	} while (0)

static int test_complete_copies(void)
{
	const unsigned char source[] = { 1, 2, 3, 4, 5, 6 };
	unsigned char destination[sizeof(source)];

	test_select_backends(TEST_BACKEND_READ | TEST_BACKEND_WRITE);
	TEST_CHECK(test_read_lookups == 1, 1);
	TEST_CHECK(test_write_lookups == 1, 2);

	test_write_result = (ssize_t)sizeof(source);
	test_fill(destination, 0xa5, sizeof(destination));
	TEST_CHECK(_neverc_krt_mem_copy_from_user_compat(
			   destination, source, sizeof(source)) == 0, 3);
	TEST_CHECK(test_bytes_equal(destination, source, sizeof(source)), 4);
	TEST_CHECK(test_write_calls == 1 && test_read_calls == 0, 5);

	test_read_result = (ssize_t)sizeof(source);
	test_fill(destination, 0xa5, sizeof(destination));
	TEST_CHECK(_neverc_krt_mem_copy_to_user_compat(
			   destination, source, sizeof(source)) == 0, 6);
	TEST_CHECK(test_bytes_equal(destination, source, sizeof(source)), 7);
	TEST_CHECK(test_write_calls == 1 && test_read_calls == 1, 8);

	TEST_CHECK(neverc_krt_mem_read_user(
			   destination, source, sizeof(source)) == 0, 9);
	TEST_CHECK(neverc_krt_mem_write_user(
			   destination, source, sizeof(source)) == 0, 10);
	return 0;
}

static int test_partial_from_user(void)
{
	const unsigned char source[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	const unsigned char expected[] = { 1, 2, 3, 0, 0, 0, 0, 0 };
	unsigned char destination[sizeof(source)];

	test_select_backends(TEST_BACKEND_READ | TEST_BACKEND_WRITE);
	test_write_result = 3;
	test_fill(destination, 0xa5, sizeof(destination));
	TEST_CHECK(_neverc_krt_mem_copy_from_user_compat(
			   destination, source, sizeof(source)) == 5, 11);
	TEST_CHECK(test_bytes_equal(destination, expected, sizeof(expected)), 12);

	test_fill(destination, 0xa5, sizeof(destination));
	TEST_CHECK(neverc_krt_mem_read_user(
			   destination, source, sizeof(source)) == -EFAULT, 13);
	TEST_CHECK(test_bytes_equal(destination, expected, sizeof(expected)), 14);
	return 0;
}

static int test_failed_from_user(void)
{
	const unsigned char source[] = { 1, 2, 3, 4 };
	const unsigned char zeros[] = { 0, 0, 0, 0 };
	unsigned char destination[sizeof(source)];
	ssize_t results[] = { -EFAULT, 0, 5 };
	size_t i;

	test_select_backends(TEST_BACKEND_WRITE);
	for (i = 0; i < sizeof(results) / sizeof(results[0]); i++) {
		test_write_result = results[i];
		test_fill(destination, 0xa5, sizeof(destination));
		TEST_CHECK(_neverc_krt_mem_copy_from_user_compat(
				   destination, source, sizeof(source)) ==
			   sizeof(source), 15 + (int)i * 2);
		TEST_CHECK(test_bytes_equal(
				   destination, zeros, sizeof(zeros)),
			   16 + (int)i * 2);
	}
	return 0;
}

static int test_partial_and_failed_to_user(void)
{
	const unsigned char source[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	const unsigned char partial[] = {
		1, 2, 3, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5
	};
	unsigned char destination[sizeof(source)];
	ssize_t results[] = { -EFAULT, 0, 9 };
	size_t i;

	test_select_backends(TEST_BACKEND_READ);
	test_read_result = 3;
	test_fill(destination, 0xa5, sizeof(destination));
	TEST_CHECK(_neverc_krt_mem_copy_to_user_compat(
			   destination, source, sizeof(source)) == 5, 21);
	TEST_CHECK(test_bytes_equal(destination, partial, sizeof(partial)), 22);

	test_fill(destination, 0xa5, sizeof(destination));
	TEST_CHECK(neverc_krt_mem_write_user(
			   destination, source, sizeof(source)) == -EFAULT, 23);
	TEST_CHECK(test_bytes_equal(destination, partial, sizeof(partial)), 24);

	for (i = 0; i < sizeof(results) / sizeof(results[0]); i++) {
		test_read_result = results[i];
		test_fill(destination, 0xa5, sizeof(destination));
		TEST_CHECK(_neverc_krt_mem_copy_to_user_compat(
				   destination, source, sizeof(source)) ==
			   sizeof(source), 25 + (int)i * 2);
		TEST_CHECK(destination[0] == 0xa5, 26 + (int)i * 2);
	}
	return 0;
}

static int test_missing_backend_and_zero_length(void)
{
	unsigned char byte = 0xa5;

	test_select_backends(0);
	TEST_CHECK(_neverc_krt_mem_copy_from_user_compat(
			   &byte, &byte, 0) == 0, 31);
	TEST_CHECK(_neverc_krt_mem_copy_to_user_compat(
			   &byte, &byte, 0) == 0, 32);
	TEST_CHECK(neverc_krt_mem_read_user(&byte, &byte, 1) == -1, 33);
	TEST_CHECK(neverc_krt_mem_write_user(&byte, &byte, 1) == -1, 34);
	TEST_CHECK(byte == 0xa5, 35);

	test_select_backends(TEST_BACKEND_WRITE);
	test_write_result = 1;
	TEST_CHECK(neverc_krt_mem_read_user(&byte, &byte, 1) == 0, 36);
	TEST_CHECK(neverc_krt_mem_write_user(&byte, &byte, 1) == -1, 37);

	test_select_backends(TEST_BACKEND_READ);
	test_read_result = 1;
	TEST_CHECK(neverc_krt_mem_read_user(&byte, &byte, 1) == -1, 38);
	TEST_CHECK(neverc_krt_mem_write_user(&byte, &byte, 1) == 0, 39);
	return 0;
}

int main(void)
{
	int result;

	result = test_complete_copies();
	if (result)
		return result;
	result = test_partial_from_user();
	if (result)
		return result;
	result = test_failed_from_user();
	if (result)
		return result;
	result = test_partial_and_failed_to_user();
	if (result)
		return result;
	return test_missing_backend_and_zero_length();
}
