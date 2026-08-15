// SPDX-License-Identifier: GPL-2.0
/* Host behavior fixture for the opaque filename/path/inode API. */

#include "test-inode-metadata-shim.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

struct fixture_inode {
	unsigned char before[88];
	s64 atime_sec;
	s64 mtime_sec;
	unsigned char gap[8];
	u32 atime_nsec;
	u32 mtime_nsec;
	unsigned char after[704 - 120];
};

struct fixture_timespec_inode {
	unsigned char before[88];
	s64 atime_sec;
	u64 atime_nsec;
	s64 mtime_sec;
	u64 mtime_nsec;
	unsigned char after[704 - 120];
};

struct fixture_dentry {
	unsigned char before[48];
	struct fixture_inode *inode;
	unsigned char after[208 - 56];
};

struct fixture_path {
	void *mnt;
	struct fixture_dentry *dentry;
};

struct fixture_filename {
	const char *name;
	const void *uptr;
	u32 refcnt;
	u32 padding;
	void *aname;
	char iname[];
};

_Static_assert(sizeof(struct fixture_inode) == 704, "split inode size");
_Static_assert(offsetof(struct fixture_inode, atime_sec) == 88,
	       "split inode atime_sec offset");
_Static_assert(offsetof(struct fixture_inode, mtime_sec) == 96,
	       "split inode mtime_sec offset");
_Static_assert(offsetof(struct fixture_inode, atime_nsec) == 112,
	       "split inode atime_nsec offset");
_Static_assert(offsetof(struct fixture_inode, mtime_nsec) == 116,
	       "split inode mtime_nsec offset");
_Static_assert(offsetof(struct fixture_timespec_inode, atime_sec) == 88,
	       "timespec inode atime_sec offset");
_Static_assert(offsetof(struct fixture_timespec_inode, atime_nsec) == 96,
	       "timespec inode atime_nsec offset");
_Static_assert(offsetof(struct fixture_timespec_inode, mtime_sec) == 104,
	       "timespec inode mtime_sec offset");
_Static_assert(offsetof(struct fixture_timespec_inode, mtime_nsec) == 112,
	       "timespec inode mtime_nsec offset");
_Static_assert(sizeof(struct fixture_path) == 16, "path size");
_Static_assert(sizeof(struct neverc_krt_path_storage) ==
	       sizeof(struct fixture_path), "public opaque path capacity");
_Static_assert(_Alignof(struct neverc_krt_path_storage) ==
	       _Alignof(struct fixture_path), "public opaque path alignment");
_Static_assert(offsetof(struct fixture_path, dentry) == 8,
	       "path.dentry offset");
_Static_assert(sizeof(struct fixture_dentry) == 208, "dentry size");
_Static_assert(offsetof(struct fixture_dentry, inode) == 48,
	       "dentry.d_inode offset");
_Static_assert(sizeof(struct fixture_filename) == 32, "filename size");
_Static_assert(offsetof(struct fixture_filename, name) == 0,
	       "filename.name offset");

static struct neverc_krt_gki_layout fixture_layout = {
	.filename_size = sizeof(struct fixture_filename),
	.filename_name = offsetof(struct fixture_filename, name),
	.filename_name_size = sizeof(((struct fixture_filename *)0)->name),
	.path_size = sizeof(struct fixture_path),
	.path_dentry = offsetof(struct fixture_path, dentry),
	.path_dentry_size = sizeof(((struct fixture_path *)0)->dentry),
	.dentry_size = sizeof(struct fixture_dentry),
	.dentry_inode = offsetof(struct fixture_dentry, inode),
	.dentry_inode_size = sizeof(((struct fixture_dentry *)0)->inode),
	.inode_size = sizeof(struct fixture_inode),
	.inode_atime_sec = offsetof(struct fixture_inode, atime_sec),
	.inode_atime_sec_size = sizeof(((struct fixture_inode *)0)->atime_sec),
	.inode_mtime_sec = offsetof(struct fixture_inode, mtime_sec),
	.inode_mtime_sec_size = sizeof(((struct fixture_inode *)0)->mtime_sec),
	.inode_atime_nsec = offsetof(struct fixture_inode, atime_nsec),
	.inode_atime_nsec_size = sizeof(((struct fixture_inode *)0)->atime_nsec),
	.inode_mtime_nsec = offsetof(struct fixture_inode, mtime_nsec),
	.inode_mtime_nsec_size = sizeof(((struct fixture_inode *)0)->mtime_nsec),
};
static int fixture_version_match = NEVERC_KRT_VER_EXACT;
static unsigned long fixture_layout_certificates =
	NEVERC_KRT_LAYOUT_CERT_INODE_TIMES |
	NEVERC_KRT_LAYOUT_CERT_PATH_INODE |
	NEVERC_KRT_LAYOUT_CERT_FILENAME_NAME;
static int fixture_have_igrab = 1;
static int fixture_have_iput = 1;
static int fixture_igrab_returns_null;
static int fixture_igrab_calls;
static int fixture_iput_calls;
static void *fixture_last_iput;
static int fixture_read_calls;
static int fixture_write_calls;
static int fixture_fail_read_on;
static int fixture_fail_write_on;
static int fixture_fail_write_on_again;

const struct neverc_krt_gki_layout *_neverc_krt_get_gki_layout(void)
{
	return &fixture_layout;
}

const struct neverc_krt_gki_layout *_neverc_krt_get_proven_gki_layout(
	unsigned long required)
{
	return (fixture_layout_certificates & required) == required ?
		&fixture_layout : NULL;
}

int neverc_krt_check_kernel_match(void)
{
	return fixture_version_match;
}

unsigned long _neverc_krt_current_layout_certificates(void)
{
	return fixture_layout_certificates;
}

long neverc_krt_mem_read(void *dst, const void *src, size_t len)
{
	fixture_read_calls++;
	if (fixture_fail_read_on == fixture_read_calls)
		return -1;
	memcpy(dst, src, len);
	return 0;
}

long neverc_krt_mem_write(void *dst, const void *src, size_t len)
{
	fixture_write_calls++;
	if (fixture_fail_write_on == fixture_write_calls ||
	    fixture_fail_write_on_again == fixture_write_calls)
		return -1;
	memcpy(dst, src, len);
	return 0;
}

struct inode;

static struct inode *fixture_igrab(struct inode *inode)
{
	fixture_igrab_calls++;
	return fixture_igrab_returns_null ? NULL : inode;
}

static void fixture_iput(struct inode *inode)
{
	fixture_iput_calls++;
	fixture_last_iput = inode;
}

void *neverc_krt_test_inode_lookup(const char *name)
{
	if (strcmp(name, "igrab") == 0)
		return fixture_have_igrab ? (void *)fixture_igrab : NULL;
	if (strcmp(name, "iput") == 0)
		return fixture_have_iput ? (void *)fixture_iput : NULL;
	return NULL;
}

static void check_path_get_takes_and_put_drops_inode_reference(void)
{
	struct fixture_inode inode = {0};
	struct fixture_dentry dentry = {.inode = &inode};
	struct fixture_path path = {.dentry = &dentry};
	void *referenced;

	fixture_igrab_calls = 0;
	fixture_iput_calls = 0;
	fixture_last_iput = NULL;
	referenced = neverc_krt_path_inode_get(&path);
	assert(referenced == &inode);
	assert(fixture_igrab_calls == 1);
	neverc_krt_inode_put(referenced);
	assert(fixture_iput_calls == 1);
	assert(fixture_last_iput == &inode);
}

static void check_exact_profile_borrows_filename_name(void)
{
	const char name[] = "/proc/123/status";
	struct fixture_filename filename = {.name = name};
	const char *(*filename_name_fn)(const void *) =
		neverc_krt_filename_name;
	int (*filename_available_fn)(void) =
		neverc_krt_filename_name_available;
	int (*path_available_fn)(void) = neverc_krt_path_storage_available;

	assert(filename_available_fn() == 1);
	assert(path_available_fn() == 1);
	assert(filename_name_fn(&filename) == name);
}

static void check_compatible_release_uses_family_layout(void)
{
	struct fixture_inode inode = {0};
	struct fixture_dentry dentry = {.inode = &inode};
	struct fixture_path path = {.dentry = &dentry};
	const char name[] = "/proc/321/status";
	struct fixture_filename filename = {.name = name};
	struct neverc_krt_inode_times times;

	fixture_version_match = NEVERC_KRT_VER_COMPAT;
	fixture_layout_certificates = 0;
	assert(neverc_krt_inode_set_times(&inode, 1, 2, 3, 4) != 0);
	assert(neverc_krt_inode_get_times(&inode, &times) != 0);
	assert(neverc_krt_path_inode_get(&path) == NULL);
	assert(neverc_krt_path_storage_available() == 0);
	assert(neverc_krt_filename_name_available() == 0);
	assert(neverc_krt_filename_name(&filename) == NULL);

	fixture_layout_certificates = NEVERC_KRT_LAYOUT_CERT_INODE_TIMES |
		NEVERC_KRT_LAYOUT_CERT_PATH_INODE |
		NEVERC_KRT_LAYOUT_CERT_FILENAME_NAME;
	assert(neverc_krt_inode_set_times(&inode, 1, 2, 3, 4) == 0);
	assert(neverc_krt_inode_get_times(&inode, &times) == 0);
	assert(times.atime_sec == 1 && times.atime_nsec == 2);
	assert(times.mtime_sec == 3 && times.mtime_nsec == 4);
	assert(neverc_krt_path_inode_get(&path) == &inode);
	assert(neverc_krt_path_storage_available() == 1);
	assert(neverc_krt_filename_name_available() == 1);
	assert(neverc_krt_filename_name(&filename) == name);
	neverc_krt_inode_put(&inode);

	fixture_version_match = NEVERC_KRT_VER_MISMATCH;
	fixture_layout_certificates = NEVERC_KRT_LAYOUT_CERT_INODE_TIMES |
		NEVERC_KRT_LAYOUT_CERT_PATH_INODE |
		NEVERC_KRT_LAYOUT_CERT_FILENAME_NAME;
	assert(neverc_krt_inode_set_times(&inode, 1, 2, 3, 4) != 0);
	assert(neverc_krt_path_inode_get(&path) == NULL);
	assert(neverc_krt_path_storage_available() == 0);
	assert(neverc_krt_filename_name_available() == 0);
	assert(neverc_krt_filename_name(&filename) == NULL);
	fixture_version_match = NEVERC_KRT_VER_EXACT;
	fixture_layout_certificates = NEVERC_KRT_LAYOUT_CERT_INODE_TIMES |
		NEVERC_KRT_LAYOUT_CERT_PATH_INODE |
		NEVERC_KRT_LAYOUT_CERT_FILENAME_NAME;
}

static void check_filename_layout_and_read_fail_closed(void)
{
	struct neverc_krt_gki_layout saved = fixture_layout;
	const char name[] = "target";
	struct fixture_filename filename = {.name = name};

	assert(neverc_krt_filename_name(NULL) == NULL);

	fixture_read_calls = 0;
	fixture_fail_read_on = 1;
	assert(neverc_krt_filename_name(&filename) == NULL);
	fixture_fail_read_on = 0;

	fixture_layout.filename_name_size = 4;
	assert(neverc_krt_filename_name_available() == 0);
	assert(neverc_krt_filename_name(&filename) == NULL);
	fixture_layout = saved;
	fixture_layout.filename_name = fixture_layout.filename_size - 7;
	assert(neverc_krt_filename_name_available() == 0);
	assert(neverc_krt_filename_name(&filename) == NULL);
	fixture_layout = saved;
	fixture_layout.filename_size = 0;
	assert(neverc_krt_filename_name_available() == 0);
	assert(neverc_krt_filename_name(&filename) == NULL);
	fixture_layout = saved;
}

static void check_nsec_range_and_invalid_arguments_fail_closed(void)
{
	struct fixture_inode inode = {
		.atime_sec = 11,
		.mtime_sec = 22,
		.atime_nsec = 33,
		.mtime_nsec = 44,
	};
	struct neverc_krt_inode_times out;

	assert(neverc_krt_inode_set_times(
			&inode, 1, 1000000000U, 2, 3) != 0);
	assert(neverc_krt_inode_set_times(
			&inode, 1, 2, 3, 1000000000U) != 0);
	assert(inode.atime_sec == 11 && inode.mtime_sec == 22);
	assert(inode.atime_nsec == 33 && inode.mtime_nsec == 44);
	assert(neverc_krt_inode_set_times(NULL, 1, 2, 3, 4) != 0);
	memset(&out, 0xa5, sizeof(out));
	assert(neverc_krt_inode_get_times(NULL, &out) != 0);
	assert(memcmp(&out, &(struct neverc_krt_inode_times){0},
		      sizeof(out)) == 0);
	assert(neverc_krt_inode_get_times(&inode, NULL) != 0);
}

static void check_inode_layout_width_bounds_and_overlap(void)
{
	struct neverc_krt_gki_layout saved = fixture_layout;
	struct fixture_inode inode = {0};
	struct neverc_krt_inode_times out;

	fixture_layout.inode_atime_sec_size = 4;
	assert(neverc_krt_inode_set_times(&inode, 1, 2, 3, 4) != 0);
	fixture_layout = saved;
	fixture_layout.inode_atime_nsec_size = 0;
	assert(neverc_krt_inode_get_times(&inode, &out) != 0);
	fixture_layout = saved;
	fixture_layout.inode_mtime_nsec = fixture_layout.inode_size - 3;
	assert(neverc_krt_inode_set_times(&inode, 1, 2, 3, 4) != 0);
	fixture_layout = saved;
	fixture_layout.inode_mtime_sec = fixture_layout.inode_atime_sec + 4;
	assert(neverc_krt_inode_get_times(&inode, &out) != 0);
	fixture_layout = saved;
	fixture_layout.inode_size = 0;
	assert(neverc_krt_inode_set_times(&inode, 1, 2, 3, 4) != 0);
	fixture_layout = saved;
}

static void check_getter_read_failure_or_invalid_nsec_clears_output(void)
{
	struct fixture_inode inode = {
		.atime_sec = 1,
		.mtime_sec = 2,
		.atime_nsec = 3,
		.mtime_nsec = 4,
	};
	struct neverc_krt_inode_times out;
	struct neverc_krt_inode_times zero = {0};
	int failed_read;

	for (failed_read = 1; failed_read <= 4; failed_read++) {
		fixture_read_calls = 0;
		fixture_fail_read_on = failed_read;
		memset(&out, 0xa5, sizeof(out));
		assert(neverc_krt_inode_get_times(&inode, &out) != 0);
		assert(memcmp(&out, &zero, sizeof(out)) == 0);
	}
	fixture_fail_read_on = 0;

	inode.atime_nsec = 1000000000U;
	memset(&out, 0xa5, sizeof(out));
	assert(neverc_krt_inode_get_times(&inode, &out) != 0);
	assert(memcmp(&out, &zero, sizeof(out)) == 0);
}

static void check_setter_write_failure_rolls_back_exact_tuple(void)
{
	struct fixture_inode original = {
		.atime_sec = 11,
		.mtime_sec = 22,
		.atime_nsec = 33,
		.mtime_nsec = 44,
	};
	int failed_write;

	for (failed_write = 1; failed_write <= 4; failed_write++) {
		struct fixture_inode inode = original;

		fixture_write_calls = 0;
		fixture_fail_write_on = failed_write;
		fixture_fail_write_on_again = 0;
		assert(neverc_krt_inode_set_times(&inode, 1, 2, 3, 4) == -3);
		assert(memcmp(&inode, &original, sizeof(inode)) == 0);
	}
	fixture_fail_write_on = 0;
}

static void check_setter_reports_unproven_rollback(void)
{
	struct fixture_inode inode = {
		.atime_sec = 11,
		.mtime_sec = 22,
		.atime_nsec = 33,
		.mtime_nsec = 44,
	};

	fixture_write_calls = 0;
	fixture_fail_write_on = 4;
	fixture_fail_write_on_again = 5;
	assert(neverc_krt_inode_set_times(&inode, 1, 2, 3, 4) == -4);
	assert(inode.atime_sec == 1);
	assert(inode.mtime_sec == 22);
	assert(inode.atime_nsec == 33);
	assert(inode.mtime_nsec == 44);
	fixture_fail_write_on = 0;
	fixture_fail_write_on_again = 0;
}

static void check_missing_inode_reference_functions_fail_closed(void)
{
	struct fixture_inode inode = {0};
	struct fixture_dentry dentry = {.inode = &inode};
	struct fixture_path path = {.dentry = &dentry};

	fixture_have_igrab = 0;
	fixture_have_iput = 1;
	assert(neverc_krt_path_inode_get(&path) == NULL);
	fixture_have_igrab = 1;
	fixture_have_iput = 0;
	assert(neverc_krt_path_inode_get(&path) == NULL);
	fixture_have_iput = 1;
	fixture_igrab_returns_null = 1;
	assert(neverc_krt_path_inode_get(&path) == NULL);
	assert(fixture_igrab_calls == 1);
	fixture_igrab_returns_null = 0;
}

static void check_path_layout_and_null_chain_fail_closed(void)
{
	struct neverc_krt_gki_layout saved = fixture_layout;
	struct fixture_inode inode = {0};
	struct fixture_dentry dentry = {.inode = &inode};
	struct fixture_path path = {.dentry = &dentry};

	assert(neverc_krt_path_inode_get(NULL) == NULL);
	path.dentry = NULL;
	assert(neverc_krt_path_inode_get(&path) == NULL);
	path.dentry = &dentry;
	dentry.inode = NULL;
	assert(neverc_krt_path_inode_get(&path) == NULL);
	dentry.inode = &inode;

	fixture_layout.path_dentry_size = 4;
	assert(neverc_krt_path_inode_get(&path) == NULL);
	fixture_layout = saved;
	fixture_layout.path_size = 24;
	assert(neverc_krt_path_inode_get(&path) == NULL);
	fixture_layout = saved;
	fixture_layout.path_dentry = fixture_layout.path_size - 7;
	assert(neverc_krt_path_inode_get(&path) == NULL);
	fixture_layout = saved;
	fixture_layout.dentry_inode_size = 4;
	assert(neverc_krt_path_inode_get(&path) == NULL);
	fixture_layout = saved;
	fixture_layout.dentry_inode = fixture_layout.dentry_size - 7;
	assert(neverc_krt_path_inode_get(&path) == NULL);
	fixture_layout = saved;

	fixture_read_calls = 0;
	fixture_fail_read_on = 1;
	assert(neverc_krt_path_inode_get(&path) == NULL);
	fixture_read_calls = 0;
	fixture_fail_read_on = 2;
	assert(neverc_krt_path_inode_get(&path) == NULL);
	fixture_fail_read_on = 0;
}

static void check_exact_profile_reads_and_writes_four_scalars(void)
{
	struct fixture_inode inode = {0};
	struct neverc_krt_inode_times times;

	assert(neverc_krt_inode_set_times(&inode, -7, 123, 42, 999999999) == 0);
	assert(inode.atime_sec == -7);
	assert(inode.atime_nsec == 123);
	assert(inode.mtime_sec == 42);
	assert(inode.mtime_nsec == 999999999);

	memset(&times, 0xa5, sizeof(times));
	assert(neverc_krt_inode_get_times(&inode, &times) == 0);
	assert(times.atime_sec == -7);
	assert(times.atime_nsec == 123);
	assert(times.mtime_sec == 42);
	assert(times.mtime_nsec == 999999999);
}

static void check_timespec64_nsec_width_is_fully_written(void)
{
	struct neverc_krt_gki_layout saved = fixture_layout;
	struct fixture_timespec_inode inode = {
		.atime_nsec = ~(u64)0,
		.mtime_nsec = ~(u64)0,
	};
	struct neverc_krt_inode_times times;

	fixture_layout.inode_size = sizeof(inode);
	fixture_layout.inode_atime_sec =
		offsetof(struct fixture_timespec_inode, atime_sec);
	fixture_layout.inode_atime_sec_size = sizeof(inode.atime_sec);
	fixture_layout.inode_atime_nsec =
		offsetof(struct fixture_timespec_inode, atime_nsec);
	fixture_layout.inode_atime_nsec_size = sizeof(inode.atime_nsec);
	fixture_layout.inode_mtime_sec =
		offsetof(struct fixture_timespec_inode, mtime_sec);
	fixture_layout.inode_mtime_sec_size = sizeof(inode.mtime_sec);
	fixture_layout.inode_mtime_nsec =
		offsetof(struct fixture_timespec_inode, mtime_nsec);
	fixture_layout.inode_mtime_nsec_size = sizeof(inode.mtime_nsec);

	assert(neverc_krt_inode_set_times(&inode, 1, 2, 3, 4) == 0);
	assert(inode.atime_nsec == 2);
	assert(inode.mtime_nsec == 4);
	assert(neverc_krt_inode_get_times(&inode, &times) == 0);
	assert(times.atime_sec == 1 && times.atime_nsec == 2);
	assert(times.mtime_sec == 3 && times.mtime_nsec == 4);
	fixture_layout = saved;
}

int main(void)
{
	check_exact_profile_borrows_filename_name();
	check_missing_inode_reference_functions_fail_closed();
	check_exact_profile_reads_and_writes_four_scalars();
	check_timespec64_nsec_width_is_fully_written();
	check_path_get_takes_and_put_drops_inode_reference();
	check_compatible_release_uses_family_layout();
	check_filename_layout_and_read_fail_closed();
	check_nsec_range_and_invalid_arguments_fail_closed();
	check_inode_layout_width_bounds_and_overlap();
	check_getter_read_failure_or_invalid_nsec_clears_output();
	check_setter_write_failure_rolls_back_exact_tuple();
	check_setter_reports_unproven_rollback();
	check_path_layout_and_null_chain_fail_closed();
	return 0;
}
