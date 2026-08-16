/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_VIS_SEQ_INTERNAL_H
#define NEVERC_KRT_VIS_SEQ_INTERNAL_H

#include "nvk_profile.h"

/* _IOWR('f', 17, struct procmap_query), whose stable UAPI size is 104. */
#define NEVERC_KRT_PROCMAP_QUERY 0xC0686611U

struct neverc_krt_maps_filter_region {
	unsigned long start;
	unsigned long end;
	void *mm_identity;
};

/*
 * The first four seq_file fields are stable from Linux 5.10 through 6.18.
 * Keep this as a private prefix so the public SDK can leave struct seq_file
 * opaque while filters can inspect only bytes emitted by one show callback.
 */
struct neverc_krt_seq_file_prefix {
	void *buf;
	unsigned long size;
	unsigned long from;
	unsigned long count;
};

_Static_assert(__builtin_offsetof(struct neverc_krt_seq_file_prefix, count) ==
		       3 * sizeof(unsigned long),
	       "unexpected seq_file prefix layout");

static inline int _neverc_krt_interpose_install_is_owned(
	int install_status, int handle_active)
{
	return install_status == 0 || handle_active;
}

/*
 * A failed second-hook install must surface a live-handle remove error so
 * cleanup can retry.  Only fall back to the original install status after
 * both reader hooks have been dropped or were never owned.
 */
static inline int _neverc_krt_maps_failed_second_hook_status(
	int install_status, int ioctl_remove_status, int show_remove_status)
{
	if (ioctl_remove_status)
		return ioctl_remove_status;
	if (show_remove_status)
		return show_remove_status;
	return install_status;
}

static inline void *_neverc_krt_seq_operations_show(const void *operations)
{
	void *show = (void *)0;

	if (!operations ||
	    neverc_krt_mem_read(
		    &show, (const char *)operations + 3 * sizeof(void *),
		    sizeof(show)))
		return (void *)0;
	return show;
}

static inline int _neverc_krt_seq_file_output_contains(
	const void *seq_file, unsigned long begin, const char *needle)
{
	struct neverc_krt_seq_file_prefix seq;
	unsigned long needle_len = 0;
	unsigned long i;
	unsigned long j;

	if (!seq_file || !needle || !*needle ||
	    neverc_krt_mem_read(&seq, seq_file, sizeof(seq)))
		return 0;
	while (needle[needle_len])
		needle_len++;
	if (!seq.buf || begin > seq.count || seq.count >= seq.size ||
	    needle_len > seq.count - begin)
		return 0;

	for (i = begin; i <= seq.count - needle_len; i++) {
		for (j = 0; j < needle_len; j++) {
			char ch;

			if (neverc_krt_mem_read(
				    &ch, (const char *)seq.buf + i + j, 1) ||
			    ch != needle[j])
				break;
		}
		if (j == needle_len)
			return 1;
	}
	return 0;
}

static inline int _neverc_krt_seq_file_char(
	const struct neverc_krt_seq_file_prefix *seq,
	unsigned long offset, char *value)
{
	if (!seq || !value || !seq->buf || offset >= seq->count)
		return -1;
	return neverc_krt_mem_read(
		value, (const char *)seq->buf + offset, 1) ? -1 : 0;
}

static inline int _neverc_krt_seq_file_literal_at(
	const struct neverc_krt_seq_file_prefix *seq,
	unsigned long offset, unsigned long end, const char *literal)
{
	unsigned long i = 0;

	if (!seq || !literal)
		return 0;
	while (literal[i]) {
		char ch;

		if (offset + i >= end ||
		    _neverc_krt_seq_file_char(seq, offset + i, &ch) ||
		    ch != literal[i])
			return 0;
		i++;
	}
	return 1;
}

static inline int _neverc_krt_seq_file_find_literal(
	const struct neverc_krt_seq_file_prefix *seq,
	unsigned long begin, unsigned long end, const char *literal,
	unsigned long *match)
{
	unsigned long i;

	if (!seq || !literal || !*literal || !match || begin > end)
		return -1;
	for (i = begin; i < end; i++) {
		if (_neverc_krt_seq_file_literal_at(seq, i, end, literal)) {
			*match = i;
			return 0;
		}
	}
	return -1;
}

static inline int _neverc_krt_seq_file_token_bounds(
	const struct neverc_krt_seq_file_prefix *seq,
	unsigned long begin, unsigned long end, unsigned int token_index,
	unsigned long *token_begin, unsigned long *token_end)
{
	unsigned long pos = begin;
	unsigned int index;

	if (!seq || !token_begin || !token_end || begin > end)
		return -1;
	for (index = 0; index <= token_index; index++) {
		char ch;

		while (pos < end) {
			if (_neverc_krt_seq_file_char(seq, pos, &ch))
				return -1;
			if (ch != ' ' && ch != '\t')
				break;
			pos++;
		}
		if (pos >= end)
			return -1;
		*token_begin = pos;
		while (pos < end) {
			if (_neverc_krt_seq_file_char(seq, pos, &ch))
				return -1;
			if (ch == ' ' || ch == '\t' || ch == '\n')
				break;
			pos++;
		}
		*token_end = pos;
	}
	return 0;
}

static inline int _neverc_krt_seq_file_decimal_token(
	const struct neverc_krt_seq_file_prefix *seq,
	unsigned long begin, unsigned long end)
{
	unsigned long pos;

	if (!seq || begin >= end)
		return 0;
	for (pos = begin; pos < end; pos++) {
		char ch;

		if (_neverc_krt_seq_file_char(seq, pos, &ch) ||
		    ch < '0' || ch > '9')
			return 0;
	}
	return 1;
}

static inline int _neverc_krt_seq_file_device_token(
	const struct neverc_krt_seq_file_prefix *seq,
	unsigned long begin, unsigned long end)
{
	unsigned long colon = end;
	unsigned long pos;

	if (!seq || begin >= end)
		return 0;
	for (pos = begin; pos < end; pos++) {
		char ch;

		if (_neverc_krt_seq_file_char(seq, pos, &ch))
			return 0;
		if (ch == ':') {
			if (colon != end)
				return 0;
			colon = pos;
		} else if (ch < '0' || ch > '9') {
			return 0;
		}
	}
	return colon > begin && colon + 1 < end;
}

static inline int _neverc_krt_mount_path_prefix_matches(
	const struct neverc_krt_seq_file_prefix *seq,
	unsigned long begin, unsigned long end, const char *path)
{
	unsigned long pos = begin;
	unsigned long i = 0;

	if (!seq || !path || !*path || begin >= end)
		return 0;
	while (path[i]) {
		const char *escaped = (const char *)0;
		char ch;

		switch (path[i]) {
		case ' ':
			escaped = "\\040";
			break;
		case '\t':
			escaped = "\\011";
			break;
		case '\n':
			escaped = "\\012";
			break;
		case '\\':
			escaped = "\\134";
			break;
		default:
			if (pos >= end ||
			    _neverc_krt_seq_file_char(seq, pos, &ch) ||
			    ch != path[i])
				return 0;
			pos++;
			i++;
			continue;
		}
		if (!_neverc_krt_seq_file_literal_at(seq, pos, end, escaped))
			return 0;
		pos += 4;
		i++;
	}
	if (pos == end || path[i - 1] == '/')
		return 1;
	{
		char next;

		return !_neverc_krt_seq_file_char(seq, pos, &next) && next == '/';
	}
}

static inline int _neverc_krt_mount_output_matches_path(
	const void *seq_file, unsigned long begin, const char *path)
{
	struct neverc_krt_seq_file_prefix seq;
	unsigned long line_end;
	unsigned long mount_begin;
	unsigned long mount_end;
	unsigned long token0_begin, token0_end;
	unsigned long token1_begin, token1_end;
	unsigned long token2_begin, token2_end;
	unsigned long marker;

	if (!seq_file || !path || !*path ||
	    neverc_krt_mem_read(&seq, seq_file, sizeof(seq)) ||
	    !seq.buf || begin > seq.count || seq.count >= seq.size)
		return 0;
	line_end = begin;
	while (line_end < seq.count) {
		char ch;

		if (_neverc_krt_seq_file_char(&seq, line_end, &ch))
			return 0;
		if (ch == '\n')
			break;
		line_end++;
	}
	if (begin >= line_end)
		return 0;

	/* mountstats: "[no ]device ... mounted on PATH with fstype ..." */
	if (!_neverc_krt_seq_file_find_literal(
		    &seq, begin, line_end, " mounted on ", &marker)) {
		mount_begin = marker + sizeof(" mounted on ") - 1;
		if (_neverc_krt_seq_file_find_literal(
			    &seq, mount_begin, line_end, " with fstype ",
			    &mount_end))
			return 0;
		return _neverc_krt_mount_path_prefix_matches(
			&seq, mount_begin, mount_end, path);
	}

	if (_neverc_krt_seq_file_token_bounds(
		    &seq, begin, line_end, 0, &token0_begin, &token0_end) ||
	    _neverc_krt_seq_file_token_bounds(
		    &seq, begin, line_end, 1, &token1_begin, &token1_end))
		return 0;

	/*
	 * mountinfo starts with "id parent major:minor root mountpoint".
	 * Otherwise this is mounts/mounts-like output and token 1 is the
	 * mountpoint.
	 */
	if (_neverc_krt_seq_file_decimal_token(
		    &seq, token0_begin, token0_end) &&
	    _neverc_krt_seq_file_decimal_token(
		    &seq, token1_begin, token1_end) &&
	    !_neverc_krt_seq_file_token_bounds(
		    &seq, begin, line_end, 2, &token2_begin, &token2_end) &&
	    _neverc_krt_seq_file_device_token(
		    &seq, token2_begin, token2_end)) {
		if (_neverc_krt_seq_file_token_bounds(
			    &seq, begin, line_end, 4, &mount_begin, &mount_end))
			return 0;
	} else {
		mount_begin = token1_begin;
		mount_end = token1_end;
	}
	return _neverc_krt_mount_path_prefix_matches(
		&seq, mount_begin, mount_end, path);
}

static inline int _neverc_krt_seq_file_rewind(
	void *seq_file, unsigned long count)
{
	struct neverc_krt_seq_file_prefix seq;

	if (!seq_file ||
	    neverc_krt_mem_read(&seq, seq_file, sizeof(seq)) ||
	    count > seq.count || seq.count >= seq.size)
		return -1;
	return neverc_krt_mem_write(
		(char *)seq_file +
			__builtin_offsetof(struct neverc_krt_seq_file_prefix, count),
		&count, sizeof(count)) ? -1 : 0;
}

/*
 * /proc/modules uses a compiler-local m_show on every current GKI.  Android
 * 12 5.10 only exports the unique-hashed form, so a plain m_show lookup
 * fails.  modules_op.show is the stable slot on 5.10–6.18.
 */
static inline void *_neverc_krt_resolve_modules_show(void)
{
	return _neverc_krt_seq_operations_show(NEVERC_KRT_LOOKUP("modules_op"));
}

/*
 * /proc/pid/maps keeps show_map compiler-local.  Android 12 5.10 only
 * exports the unique-hashed form.  proc_pid_maps_op.show is the stable
 * seq_operations slot on 5.10–6.18.  proc_pid_maps_operations is
 * file_operations and must not be read as seq_operations.
 */
static inline void *_neverc_krt_resolve_maps_seq_operations(void)
{
	return (void *)NEVERC_KRT_LOOKUP("proc_pid_maps_op");
}

static inline void *_neverc_krt_resolve_maps_show(void)
{
	void *show = _neverc_krt_seq_operations_show(
		_neverc_krt_resolve_maps_seq_operations());

	if (show)
		return show;
	return (void *)NEVERC_KRT_LOOKUP("show_map");
}

static inline void *_neverc_krt_resolve_maps_ioctl(
	unsigned long unlocked_ioctl_offset)
{
	const void *operations =
		(const void *)NEVERC_KRT_LOOKUP("proc_pid_maps_operations");
	void *ioctl_fn = (void *)0;

	if (!operations ||
	    neverc_krt_mem_read(
		    &ioctl_fn,
		    (const char *)operations + unlocked_ioctl_offset,
		    sizeof(ioctl_fn)))
		return (void *)0;
	return ioctl_fn;
}

static inline int _neverc_krt_maps_ioctl_required(int capability)
{
	/* Unknown capability values fail closed like the supported layouts. */
	return capability != NEVERC_KRT_PROCMAP_IOCTL_LAYOUT_UNSUPPORTED;
}

static inline int _neverc_krt_maps_should_install_ioctl(
	int ioctl_required, const void *ioctl_target)
{
	return ioctl_required && ioctl_target;
}

static inline int _neverc_krt_maps_ioctl_layout_from_caps(
	const struct neverc_krt_runtime_caps *caps,
	struct neverc_krt_maps_ioctl_layout *layout)
{
	if (!caps || !layout)
		return -1;
	if (caps->procmap_ioctl_layout ==
	    NEVERC_KRT_PROCMAP_IOCTL_LAYOUT_UNSUPPORTED)
		return -1;
	if (!caps->procmap_ioctl.file_size)
		return -1;
	*layout = caps->procmap_ioctl;
	return 0;
}

static inline int _neverc_krt_maps_ioctl_target_mm(
	const void *file, const void *expected_operations,
	const struct neverc_krt_maps_ioctl_layout *layout, void **target_mm)
{
	void *seq_file = (void *)0;
	void *seq_owner = (void *)0;
	void *seq_operations = (void *)0;
	void *maps_private = (void *)0;
	void *mm = (void *)0;

	if (target_mm)
		*target_mm = (void *)0;
	if (!file || !expected_operations || !layout || !target_mm ||
	    layout->file_size < sizeof(void *) ||
	    layout->file_private_data >
		    layout->file_size - sizeof(void *) ||
	    layout->seq_file_size < sizeof(void *) ||
	    layout->seq_file_file >
		    layout->seq_file_size - sizeof(void *) ||
	    layout->seq_file_operations >
		    layout->seq_file_size - sizeof(void *) ||
	    layout->seq_file_private >
		    layout->seq_file_size - sizeof(void *) ||
	    layout->proc_maps_private_size < sizeof(void *) ||
	    layout->proc_maps_private_mm >
		    layout->proc_maps_private_size - sizeof(void *))
		return -1;
	if (neverc_krt_mem_read(
		    &seq_file, (const char *)file + layout->file_private_data,
		    sizeof(seq_file)) ||
	    !seq_file ||
	    neverc_krt_mem_read(
		    &seq_owner,
		    (const char *)seq_file + layout->seq_file_file,
		    sizeof(seq_owner)) ||
	    seq_owner != file ||
	    neverc_krt_mem_read(
		    &seq_operations,
		    (const char *)seq_file + layout->seq_file_operations,
		    sizeof(seq_operations)) ||
	    seq_operations != expected_operations ||
	    neverc_krt_mem_read(
		    &maps_private,
		    (const char *)seq_file + layout->seq_file_private,
		    sizeof(maps_private)) ||
	    !maps_private ||
	    neverc_krt_mem_read(
		    &mm,
		    (const char *)maps_private + layout->proc_maps_private_mm,
		    sizeof(mm)) ||
	    !mm)
		return -1;
	*target_mm = mm;
	return 0;
}

static inline int _neverc_krt_maps_ioctl_should_block(
	unsigned int command,
	const struct neverc_krt_maps_filter_region *regions, int range_count,
	const void *target_mm, int target_mm_known)
{
	int i;

	if (command != NEVERC_KRT_PROCMAP_QUERY ||
	    !regions || range_count <= 0)
		return 0;
	for (i = 0; i < range_count; i++) {
		if (!regions[i].mm_identity)
			return 1;
		if (target_mm_known && regions[i].mm_identity == target_mm)
			return 1;
	}
	/*
	 * Task-scoped rules cannot name a process when the private layout
	 * walk fails.  Fail open for that query instead of disabling every
	 * PROCMAP_QUERY on the system.
	 */
	return 0;
}

static inline int _neverc_krt_maps_ioctl_install_allowed(
	int ioctl_required,
	const struct neverc_krt_maps_filter_region *regions, int range_count)
{
	int i;

	if (!ioctl_required)
		return 1;
	if (!regions || range_count <= 0)
		return 0;
	for (i = 0; i < range_count; i++) {
		if (!regions[i].mm_identity)
			return 0;
	}
	return 1;
}

static inline int _neverc_krt_maps_global_address_should_hide(
	unsigned long address,
	const struct neverc_krt_maps_filter_region *regions, int count)
{
	int i;

	if (!regions || count <= 0)
		return 0;
	for (i = 0; i < count; i++) {
		if (!regions[i].mm_identity &&
		    address >= regions[i].start &&
		    address < regions[i].end)
			return 1;
	}
	return 0;
}

static inline int _neverc_krt_maps_vma_should_hide(
	const void *vma, unsigned long vma_size,
	unsigned long start_offset, unsigned long end_offset,
	unsigned long mm_offset,
	const struct neverc_krt_maps_filter_region *regions, int count)
{
	unsigned long start = 0;
	unsigned long end = 0;
	void *mm_identity = (void *)0;
	int i;

	if (!vma || !regions || count <= 0 ||
	    vma_size < sizeof(unsigned long) ||
	    start_offset > vma_size - sizeof(unsigned long) ||
	    end_offset > vma_size - sizeof(unsigned long) ||
	    mm_offset > vma_size - sizeof(mm_identity))
		return 0;
	if (neverc_krt_mem_read(&start, (const char *)vma + start_offset,
				sizeof(start)) ||
	    neverc_krt_mem_read(&end, (const char *)vma + end_offset,
				sizeof(end)) ||
	    neverc_krt_mem_read(
		    &mm_identity, (const char *)vma + mm_offset,
		    sizeof(mm_identity)))
		return 0;
	for (i = 0; i < count; i++) {
		if ((!regions[i].mm_identity ||
		     regions[i].mm_identity == mm_identity) &&
		    start < end &&
		    regions[i].start < regions[i].end &&
		    start < regions[i].end && regions[i].start < end)
			return 1;
	}
	return 0;
}

/*
 * /proc/mounts, /proc/mountinfo, and /proc/mountstats share mounts_op.
 * Android 12 5.10 only exports hashed show_vfsmnt / show_mountinfo.
 * mounts_op.show is the stable m_show slot on 5.10–6.18.
 */
static inline void *_neverc_krt_resolve_mounts_show(void)
{
	void *show = _neverc_krt_seq_operations_show(
		NEVERC_KRT_LOOKUP("mounts_op"));

	if (show)
		return show;
	show = (void *)NEVERC_KRT_LOOKUP("show_vfsmnt");
	if (show)
		return show;
	return (void *)NEVERC_KRT_LOOKUP("show_mountinfo");
}

#endif /* NEVERC_KRT_VIS_SEQ_INTERNAL_H */
