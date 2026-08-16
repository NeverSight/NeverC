/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_INODE_H
#define NEVERC_KRT_INODE_H

#include <linux/types.h>

/* Scalar inode timestamps, independent of any private struct inode layout. */
struct neverc_krt_inode_times {
	s64 atime_sec;
	u32 atime_nsec;
	s64 mtime_sec;
	u32 mtime_nsec;
};

/*
 * Opaque output storage for kern_path().  Its contents remain private to the
 * selected GKI profile; callers may borrow its address as a struct path output
 * but must use neverc_krt_path_inode_get() to inspect it.  Call
 * neverc_krt_path_storage_available() before passing storage to kern_path().
 */
struct neverc_krt_path_storage {
	u64 words[2];
} __attribute__((aligned(8)));

_Static_assert(sizeof(struct neverc_krt_path_storage) == 16,
	       "opaque path storage must be 16 bytes");
_Static_assert(__alignof__(struct neverc_krt_path_storage) == 8,
	       "opaque path storage must be 8-byte aligned");

/*
 * Read or replace the four timestamp scalars of a borrowed opaque inode.
 * Nanoseconds passed to the setter must be less than 1,000,000,000.  This is
 * a low-level metadata accessor, not a VFS timestamp operation: callers must
 * provide any synchronization required against concurrent inode updates.
 * A setter write fault returns -3 after exact rollback, or -4 when rollback
 * cannot be proven and the caller must treat the tuple as indeterminate.
 */
int neverc_krt_inode_get_times(const void *opaque_inode,
			       struct neverc_krt_inode_times *out);
int neverc_krt_inode_set_times(void *opaque_inode,
			       s64 atime_sec, u32 atime_nsec,
			       s64 mtime_sec, u32 mtime_nsec);

/*
 * Return struct filename::name from a borrowed opaque filename, or NULL when
 * the selected family layout is unavailable or unreadable.  The
 * returned pointer is borrowed: it is valid only while the opaque filename
 * and its kernel-owned name remain alive, and callers must not free it.
 */
/* Check this at hook setup time; 0 means the hook must not be installed. */
int neverc_krt_filename_name_available(void);
const char *neverc_krt_filename_name(const void *opaque_filename);

/* Return 1 only when neverc_krt_path_storage is safe for the active layout. */
int neverc_krt_path_storage_available(void);

/* Return a referenced opaque inode from borrowed neverc_krt_path_storage. */
void *neverc_krt_path_inode_get(const void *opaque_path);

/* Drop a reference returned by neverc_krt_path_inode_get(). */
void neverc_krt_inode_put(void *opaque_inode);

#endif /* NEVERC_KRT_INODE_H */
