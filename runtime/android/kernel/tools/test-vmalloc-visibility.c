// SPDX-License-Identifier: GPL-2.0
/* Host contract for proc visibility resolvers, scope, and mountpoint parsing. */

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "nvk_profile.h"

static void *fixture_seq_operations[4];
static void *fixture_file_operations[16];
static int fixture_fail_read;
static int fixture_named_lookups;
static int fixture_ops_lookups;
static int fixture_hide_maps_op;
static int fixture_hide_mounts_op;
static int fixture_hide_named_maps;
static int fixture_hide_named_mounts;

static unsigned long fixture_lookup(const char *name)
{
	if (strcmp(name, "vmalloc_info_show") == 0) {
		fixture_named_lookups++;
		return 0x612612UL;
	}
	if (strcmp(name, "proc_pid_maps_op") == 0 && fixture_hide_maps_op)
		return 0;
	if (strcmp(name, "mounts_op") == 0 && fixture_hide_mounts_op)
		return 0;
	if (strcmp(name, "vmalloc_op") == 0 ||
	    strcmp(name, "modules_op") == 0 ||
	    strcmp(name, "proc_pid_maps_op") == 0 ||
	    strcmp(name, "mounts_op") == 0) {
		fixture_ops_lookups++;
		return (unsigned long)fixture_seq_operations;
	}
	if (strcmp(name, "proc_pid_maps_operations") == 0)
		return (unsigned long)fixture_file_operations;
	if (strcmp(name, "show_map") == 0)
		return fixture_hide_named_maps ? 0 : 0x51013UL;
	if (strcmp(name, "show_vfsmnt") == 0)
		return fixture_hide_named_mounts ? 0 : 0x515010UL;
	if (strcmp(name, "show_mountinfo") == 0)
		return fixture_hide_named_mounts ? 0 : 0x515020UL;
	return 0;
}

#define NEVERC_KRT_LOOKUP(name) ((void *)fixture_lookup(name))

static long neverc_krt_mem_read(void *dst, const void *src, size_t size)
{
	if (fixture_fail_read)
		return -1;
	memcpy(dst, src, size);
	return 0;
}

static long neverc_krt_mem_write(void *dst, const void *src, size_t size)
{
	if (fixture_fail_read)
		return -1;
	memcpy(dst, src, size);
	return 0;
}

/* linux/sched.h exposes current as an object-like get_current() macro. */
#define current get_current()
#include "nvk_vis_seq.h"
#include "nvk_vis_vmalloc.h"
#undef current

static void check_named_backend(void)
{
	fixture_named_lookups = 0;
	fixture_ops_lookups = 0;
	assert(_neverc_krt_resolve_vmalloc_show_backend(
		       NEVERC_KRT_VMALLOC_VIS_BACKEND_NAMED_SHOW) ==
	       (void *)0x612612UL);
	assert(fixture_named_lookups == 1);
	assert(fixture_ops_lookups == 0);
}

static void check_seq_operations_backend(void)
{
	fixture_named_lookups = 0;
	fixture_ops_lookups = 0;
	fixture_seq_operations[3] = (void *)0x510510UL;
	assert(_neverc_krt_resolve_vmalloc_show_backend(
		       NEVERC_KRT_VMALLOC_VIS_BACKEND_SEQ_OPERATIONS) ==
	       (void *)0x510510UL);
	assert(fixture_named_lookups == 0);
	assert(fixture_ops_lookups == 1);

	fixture_fail_read = 1;
	assert(_neverc_krt_resolve_vmalloc_show_backend(
		       NEVERC_KRT_VMALLOC_VIS_BACKEND_SEQ_OPERATIONS) == NULL);
	fixture_fail_read = 0;
}

static void check_split_module_ranges(void)
{
	unsigned char module[128] = {0};
	struct neverc_krt_vmalloc_range ranges[3] = {{0}};
	unsigned long base;
	unsigned int size;

	base = 0x1000;
	size = 0x200;
	memcpy(module + 32, &base, sizeof(base));
	memcpy(module + 40, &size, sizeof(size));
	base = 0x4000;
	size = 0x300;
	memcpy(module + 56, &base, sizeof(base));
	memcpy(module + 64, &size, sizeof(size));

	assert(_neverc_krt_collect_module_vmalloc_ranges(
		       module, 32, 2, 24, 0, 8, ranges, 3) == 2);
	assert(ranges[0].start == 0x1000);
	assert(ranges[0].end == 0x1200);
	assert(ranges[1].start == 0x4000);
	assert(ranges[1].end == 0x4300);
	assert(_neverc_krt_vmalloc_range_overlaps(&ranges[0], 0x0ff0, 0x1010));
	assert(!_neverc_krt_vmalloc_range_overlaps(&ranges[0], 0x1200, 0x1300));

	base = ~0UL - 0x10;
	size = 0x20;
	memcpy(module + 32, &base, sizeof(base));
	memcpy(module + 40, &size, sizeof(size));
	assert(_neverc_krt_collect_module_vmalloc_ranges(
		       module, 32, 1, 24, 0, 8, ranges, 3) == -1);
}

static void check_maps_show_uses_seq_operations(void)
{
	fixture_ops_lookups = 0;
	fixture_seq_operations[3] = (void *)0x510a00UL;
	assert(_neverc_krt_resolve_maps_seq_operations() ==
	       fixture_seq_operations);
	assert(_neverc_krt_resolve_maps_show() == (void *)0x510a00UL);
	assert(fixture_ops_lookups == 2);
	assert(_neverc_krt_resolve_maps_show() !=
	       NEVERC_KRT_LOOKUP("show_map"));
	assert(_neverc_krt_resolve_maps_show() !=
	       NEVERC_KRT_LOOKUP("proc_pid_maps_operations"));

	fixture_hide_maps_op = 1;
	assert(_neverc_krt_resolve_maps_show() ==
	       NEVERC_KRT_LOOKUP("show_map"));
	fixture_hide_maps_op = 0;

	fixture_hide_named_maps = 1;
	fixture_seq_operations[3] = (void *)0x510a00UL;
	assert(_neverc_krt_resolve_maps_show() == (void *)0x510a00UL);
	assert(_neverc_krt_resolve_maps_show() !=
	       NEVERC_KRT_LOOKUP("show_map"));
	fixture_hide_named_maps = 0;
}

static void check_maps_hides_overlapping_vma(void)
{
	unsigned char vma[32] = {0};
	int mm_a;
	int mm_b;
	void *mm_identity = &mm_a;
	struct neverc_krt_maps_filter_region regions[1] = {
		{
			.start = 0x2000,
			.end = 0x3000,
			.mm_identity = &mm_a,
		},
	};
	unsigned long start = 0x2800;
	unsigned long end = 0x2c00;

	memcpy(vma + 0, &start, sizeof(start));
	memcpy(vma + 8, &end, sizeof(end));
	memcpy(vma + 16, &mm_identity, sizeof(mm_identity));
	assert(_neverc_krt_maps_vma_should_hide(
		       vma, 32, 0, 8, 16, regions, 1));

	mm_identity = &mm_b;
	memcpy(vma + 16, &mm_identity, sizeof(mm_identity));
	assert(!_neverc_krt_maps_vma_should_hide(
		       vma, 32, 0, 8, 16, regions, 1));
	assert(!_neverc_krt_maps_global_address_should_hide(
		       0x2800, regions, 1));

	regions[0].mm_identity = NULL;
	assert(_neverc_krt_maps_vma_should_hide(
		       vma, 32, 0, 8, 16, regions, 1));

	start = 0x1000;
	end = 0x1800;
	memcpy(vma + 0, &start, sizeof(start));
	memcpy(vma + 8, &end, sizeof(end));
	assert(!_neverc_krt_maps_vma_should_hide(
		       vma, 32, 0, 8, 16, regions, 1));
	assert(!_neverc_krt_maps_vma_should_hide(
		       vma, 16, 0, 8, 16, regions, 1));
	assert(_neverc_krt_maps_global_address_should_hide(
		       0x2800, regions, 1));
}

static void check_mounts_show_uses_seq_operations(void)
{
	fixture_ops_lookups = 0;
	fixture_seq_operations[3] = (void *)0x510b00UL;
	assert(_neverc_krt_resolve_mounts_show() == (void *)0x510b00UL);
	assert(fixture_ops_lookups == 1);
	assert(_neverc_krt_resolve_mounts_show() !=
	       NEVERC_KRT_LOOKUP("show_vfsmnt"));
	assert(_neverc_krt_resolve_mounts_show() !=
	       NEVERC_KRT_LOOKUP("show_mountinfo"));

	fixture_hide_mounts_op = 1;
	assert(_neverc_krt_resolve_mounts_show() ==
	       NEVERC_KRT_LOOKUP("show_vfsmnt"));
	fixture_hide_mounts_op = 0;

	fixture_hide_named_mounts = 1;
	fixture_seq_operations[3] = (void *)0x510b00UL;
	assert(_neverc_krt_resolve_mounts_show() == (void *)0x510b00UL);
	assert(_neverc_krt_resolve_mounts_show() !=
	       NEVERC_KRT_LOOKUP("show_vfsmnt"));
	fixture_hide_named_mounts = 0;
}

static void check_mounts_filter_matches_rendered_path(void)
{
	char output[128] =
		"/dev/block/dm-1 /system ext4 ro 0 0\n"
		"overlay /data/local/tmp overlay rw 0 0\n";
	char mountinfo[128] =
		"36 29 0:32 / /data/local/tmp rw,nosuid - ext4 "
		"/dev/block/dm-1 rw\n";
	char mountstats[128] =
		"device /dev/block/dm-1 mounted on /data/local/tmp "
		"with fstype ext4\n";
	char mountstats_no_device[128] =
		"no device mounted on /data/local/tmp with fstype tmpfs\n";
	char option_only[128] =
		"overlay /mnt overlay rw,lowerdir=/data/local/tmp 0 0\n";
	char escaped[128] =
		"none /data/a\\040b tmpfs rw 0 0\n";
	char component_boundary[128] =
		"none /database tmpfs rw 0 0\n";
	struct neverc_krt_seq_file_prefix seq = {
		.buf = output,
		.size = sizeof(output),
		.count = strlen(output),
	};
	unsigned long before = strlen("/dev/block/dm-1 /system ext4 ro 0 0\n");

	assert(_neverc_krt_mount_output_matches_path(
		       &seq, before, "/data/local/tmp"));
	assert(!_neverc_krt_mount_output_matches_path(
		       &seq, before, "/system"));
	assert(_neverc_krt_seq_file_rewind(&seq, before) == 0);
	assert(seq.count == before);

	seq.buf = mountinfo;
	seq.size = sizeof(mountinfo);
	seq.count = strlen(mountinfo);
	assert(_neverc_krt_mount_output_matches_path(
		       &seq, 0, "/data/local/tmp"));

	seq.buf = mountstats;
	seq.size = sizeof(mountstats);
	seq.count = strlen(mountstats);
	assert(_neverc_krt_mount_output_matches_path(
		       &seq, 0, "/data/local/tmp"));

	seq.buf = mountstats_no_device;
	seq.size = sizeof(mountstats_no_device);
	seq.count = strlen(mountstats_no_device);
	assert(_neverc_krt_mount_output_matches_path(
		       &seq, 0, "/data/local/tmp"));

	seq.buf = option_only;
	seq.size = sizeof(option_only);
	seq.count = strlen(option_only);
	assert(!_neverc_krt_mount_output_matches_path(
		       &seq, 0, "/data/local/tmp"));
	assert(_neverc_krt_mount_output_matches_path(
		       &seq, 0, "/mnt"));

	seq.buf = escaped;
	seq.size = sizeof(escaped);
	seq.count = strlen(escaped);
	assert(_neverc_krt_mount_output_matches_path(
		       &seq, 0, "/data/a b"));

	seq.buf = component_boundary;
	seq.size = sizeof(component_boundary);
	seq.count = strlen(component_boundary);
	assert(!_neverc_krt_mount_output_matches_path(
		       &seq, 0, "/data"));

	seq.count = seq.size;
	assert(!_neverc_krt_mount_output_matches_path(
		       &seq, 0, "/data/local/tmp"));
}

static void check_failed_install_retains_cleanup_ownership(void)
{
	assert(_neverc_krt_interpose_install_is_owned(0, 1));
	assert(_neverc_krt_interpose_install_is_owned(0, 0));
	assert(_neverc_krt_interpose_install_is_owned(-5, 1));
	assert(!_neverc_krt_interpose_install_is_owned(-5, 0));
	assert(_neverc_krt_maps_failed_second_hook_status(-5, -7, 0) == -7);
	assert(_neverc_krt_maps_failed_second_hook_status(-5, 0, -3) == -3);
	assert(_neverc_krt_maps_failed_second_hook_status(-5, 0, 0) == -5);
}

static void check_procmap_query_scope_and_layout(void)
{
	unsigned char file[216] = {0};
	unsigned char seq[136] = {0};
	unsigned char maps_private[136] = {0};
	struct neverc_krt_maps_ioctl_layout layout;
	struct neverc_krt_runtime_caps caps_612 = {
		.procmap_ioctl_layout = NEVERC_KRT_PROCMAP_IOCTL_LAYOUT_GKI_612,
		.procmap_ioctl = {
			.file_size = 216,
			.file_private_data = 32,
			.seq_file_size = 136,
			.seq_file_file = 120,
			.seq_file_operations = 104,
			.seq_file_private = 128,
			.proc_maps_private_size = 120,
			.proc_maps_private_mm = 16,
		},
	};
	struct neverc_krt_runtime_caps caps_618 = {
		.procmap_ioctl_layout = NEVERC_KRT_PROCMAP_IOCTL_LAYOUT_GKI_618,
		.procmap_ioctl = {
			.file_size = 208,
			.file_private_data = 24,
			.seq_file_size = 128,
			.seq_file_file = 112,
			.seq_file_operations = 96,
			.seq_file_private = 120,
			.proc_maps_private_size = 136,
			.proc_maps_private_mm = 112,
		},
	};
	struct neverc_krt_runtime_caps caps_none = {
		.procmap_ioctl_layout =
			NEVERC_KRT_PROCMAP_IOCTL_LAYOUT_UNSUPPORTED,
	};
	struct neverc_krt_maps_filter_region regions[1] = {
		{
			.start = 0x2000,
			.end = 0x3000,
		},
	};
	int mm_a;
	int mm_b;
	void *file_pointer = file;
	void *seq_pointer = seq;
	void *private_pointer = maps_private;
	void *operations_pointer = fixture_seq_operations;
	void *mm_pointer = &mm_a;
	void *resolved_mm = NULL;
	unsigned long ioctl_offset = 10 * sizeof(void *);

	assert(!_neverc_krt_maps_ioctl_required(
		       NEVERC_KRT_PROCMAP_IOCTL_LAYOUT_UNSUPPORTED));
	assert(_neverc_krt_maps_ioctl_required(
		       NEVERC_KRT_PROCMAP_IOCTL_LAYOUT_GKI_612));
	assert(_neverc_krt_maps_ioctl_required(
		       NEVERC_KRT_PROCMAP_IOCTL_LAYOUT_GKI_618));
	assert(_neverc_krt_maps_ioctl_required(99));
	assert(!_neverc_krt_maps_should_install_ioctl(
		       0, (void *)0x612f00UL));
	assert(!_neverc_krt_maps_should_install_ioctl(1, (void *)0));
	assert(_neverc_krt_maps_should_install_ioctl(
		       1, (void *)0x612f00UL));

	fixture_file_operations[10] = (void *)0x612f00UL;
	assert(_neverc_krt_resolve_maps_ioctl(ioctl_offset) ==
	       (void *)0x612f00UL);
	assert(_neverc_krt_maps_ioctl_layout_from_caps(&caps_612, &layout) == 0);
	assert(layout.file_private_data == 32);
	assert(layout.seq_file_operations == 104);
	assert(layout.seq_file_private == 128);
	assert(layout.proc_maps_private_mm == 16);

	memcpy(file + layout.file_private_data,
	       &seq_pointer, sizeof(seq_pointer));
	memcpy(seq + layout.seq_file_file,
	       &file_pointer, sizeof(file_pointer));
	memcpy(seq + layout.seq_file_operations,
	       &operations_pointer, sizeof(operations_pointer));
	memcpy(seq + layout.seq_file_private,
	       &private_pointer, sizeof(private_pointer));
	memcpy(maps_private + layout.proc_maps_private_mm,
	       &mm_pointer, sizeof(mm_pointer));
	assert(_neverc_krt_maps_ioctl_target_mm(
		       file, fixture_seq_operations, &layout, &resolved_mm) == 0);
	assert(resolved_mm == &mm_a);

	operations_pointer = fixture_file_operations;
	memcpy(seq + layout.seq_file_operations,
	       &operations_pointer, sizeof(operations_pointer));
	assert(_neverc_krt_maps_ioctl_target_mm(
		       file, fixture_seq_operations, &layout, &resolved_mm) == -1);
	operations_pointer = fixture_seq_operations;
	memcpy(seq + layout.seq_file_operations,
	       &operations_pointer, sizeof(operations_pointer));

	assert(_neverc_krt_maps_ioctl_layout_from_caps(&caps_618, &layout) == 0);
	assert(layout.file_private_data == 24);
	assert(layout.seq_file_operations == 96);
	assert(layout.seq_file_private == 120);
	assert(layout.proc_maps_private_mm == 112);
	memset(file, 0, sizeof(file));
	memset(seq, 0, sizeof(seq));
	memset(maps_private, 0, sizeof(maps_private));
	mm_pointer = &mm_b;
	memcpy(file + layout.file_private_data,
	       &seq_pointer, sizeof(seq_pointer));
	memcpy(seq + layout.seq_file_file,
	       &file_pointer, sizeof(file_pointer));
	memcpy(seq + layout.seq_file_operations,
	       &operations_pointer, sizeof(operations_pointer));
	memcpy(seq + layout.seq_file_private,
	       &private_pointer, sizeof(private_pointer));
	memcpy(maps_private + layout.proc_maps_private_mm,
	       &mm_pointer, sizeof(mm_pointer));
	assert(_neverc_krt_maps_ioctl_target_mm(
		       file, fixture_seq_operations, &layout, &resolved_mm) == 0);
	assert(resolved_mm == &mm_b);
	fixture_fail_read = 1;
	assert(_neverc_krt_maps_ioctl_target_mm(
		       file, fixture_seq_operations, &layout, &resolved_mm) == -1);
	fixture_fail_read = 0;
	assert(_neverc_krt_maps_ioctl_layout_from_caps(&caps_none, &layout) == -1);
	assert(_neverc_krt_maps_ioctl_layout_from_caps((void *)0, &layout) == -1);

	regions[0].mm_identity = &mm_a;
	assert(_neverc_krt_maps_ioctl_should_block(
		       NEVERC_KRT_PROCMAP_QUERY, regions, 1, &mm_a, 1));
	assert(!_neverc_krt_maps_ioctl_should_block(
		       NEVERC_KRT_PROCMAP_QUERY, regions, 1, &mm_b, 1));
	assert(!_neverc_krt_maps_ioctl_should_block(
		       NEVERC_KRT_PROCMAP_QUERY, regions, 1, NULL, 0));
	assert(_neverc_krt_maps_ioctl_install_allowed(1, regions, 1));
	regions[0].mm_identity = NULL;
	assert(_neverc_krt_maps_ioctl_should_block(
		       NEVERC_KRT_PROCMAP_QUERY, regions, 1, &mm_b, 1));
	assert(!_neverc_krt_maps_ioctl_should_block(
		       0x12345678U, regions, 1, &mm_a, 1));
	assert(!_neverc_krt_maps_ioctl_install_allowed(1, regions, 1));
	assert(_neverc_krt_maps_ioctl_install_allowed(0, regions, 1));
}

int main(void)
{
	check_named_backend();
	check_seq_operations_backend();
	check_split_module_ranges();
	assert(_neverc_krt_resolve_vmalloc_show_backend(0) == NULL);

	fixture_ops_lookups = 0;
	fixture_seq_operations[3] = (void *)0xabcabcUL;
	assert(_neverc_krt_resolve_modules_show() == (void *)0xabcabcUL);
	assert(fixture_ops_lookups == 1);
	assert(_neverc_krt_resolve_modules_show() !=
	       NEVERC_KRT_LOOKUP("m_show"));
	check_maps_show_uses_seq_operations();
	check_maps_hides_overlapping_vma();
	check_mounts_show_uses_seq_operations();
	check_mounts_filter_matches_rendered_path();
	check_failed_install_retains_cleanup_ownership();
	check_procmap_query_scope_and_layout();
	return 0;
}
