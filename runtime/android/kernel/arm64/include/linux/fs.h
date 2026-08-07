/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_FS_H
#define _NEVERC_KRT_LINUX_FS_H

#include <linux/types.h>
#include <nvkmod_version.h>

struct file;        /* opaque */
struct inode;       /* opaque */
struct module;      /* opaque */
struct poll_table_struct;
struct kiocb;              /* opaque */
struct iov_iter;           /* opaque */
struct dir_context;        /* opaque */
struct vm_area_struct;     /* opaque */
struct pipe_inode_info;    /* opaque */
struct file_lock;          /* opaque */
struct seq_file;           /* opaque */
struct file_lease;         /* opaque */
struct io_uring_cmd;       /* opaque */
struct io_comp_batch;      /* opaque */

typedef unsigned int __poll_t;
typedef void *fl_owner_t;

/*
 * struct file_operations — version-specific layouts verified against GKI
 * gki_defconfig builds (CONFIG_COMPAT=y, CONFIG_MMU=y on arm64):
 *
 *   Field            5.10/5.15  6.1   6.6   6.12  6.18
 *   ──────────────────────────────────────────────────────
 *   owner            0          0     0     0     0
 *   fop_flags        —          —     —     8     8
 *   read             16         16    16    24    24
 *   write            24         24    24    32    32
 *   unlocked_ioctl   80         80    72    80    80
 *   mmap             96         96    88    96    96
 *   mmap_prepare     —          —     —     —     264
 *   open             112        112   104   104   104
 *   release          128        128   120   120   120
 *   sizeof           288        272   264   264   272
 *
 * 5.10/5.15 have 4×ANDROID_KABI_RESERVE (u64) = +32 bytes.
 * 6.1+ dropped KABI reserves from file_operations.
 * 6.6+ dropped iterate/sendpage, added splice_eof/uring_cmd*.
 * 6.12+ replaced mmap_supported_flags with fop_flags (u32+pad).
 * 6.18+ appended mmap_prepare after uring_cmd_iopoll.
 *
 * User modules MUST compile with the correct -DNVK_KERNEL= value so
 * designated initializers land at the offsets the running kernel expects.
 */

#if NEVERC_KRT_LINUX_AT_LEAST(6, 18, 0)

typedef unsigned int __bitwise fop_flags_t;
struct vm_area_desc; /* opaque, new in 6.18 */

struct file_operations {
	struct module *owner;
	fop_flags_t fop_flags;
	u32 _fop_pad;
	loff_t (*llseek)(struct file *, loff_t, int);
	ssize_t (*read)(struct file *, char __user *, size_t, loff_t *);
	ssize_t (*write)(struct file *, const char __user *, size_t, loff_t *);
	ssize_t (*read_iter)(struct kiocb *, struct iov_iter *);
	ssize_t (*write_iter)(struct kiocb *, struct iov_iter *);
	int (*iopoll)(struct kiocb *, struct io_comp_batch *, unsigned int);
	int (*iterate_shared)(struct file *, struct dir_context *);
	__poll_t (*poll)(struct file *, struct poll_table_struct *);
	long (*unlocked_ioctl)(struct file *, unsigned int, unsigned long);
	long (*compat_ioctl)(struct file *, unsigned int, unsigned long);
	int (*mmap)(struct file *, struct vm_area_struct *);
	int (*open)(struct inode *, struct file *);
	int (*flush)(struct file *, fl_owner_t);
	int (*release)(struct inode *, struct file *);
	unsigned char _tail[264 - 128];
	int (*mmap_prepare)(struct vm_area_desc *);
};

_Static_assert(__builtin_offsetof(struct file_operations, read) == 24,
	       "file_operations.read offset mismatch (6.18)");
_Static_assert(__builtin_offsetof(struct file_operations, unlocked_ioctl) == 80,
	       "file_operations.unlocked_ioctl offset mismatch (6.18)");
_Static_assert(__builtin_offsetof(struct file_operations, mmap_prepare) == 264,
	       "file_operations.mmap_prepare offset mismatch (6.18)");
_Static_assert(__builtin_offsetof(struct file_operations, open) == 104,
	       "file_operations.open offset mismatch (6.18)");
_Static_assert(__builtin_offsetof(struct file_operations, release) == 120,
	       "file_operations.release offset mismatch (6.18)");
_Static_assert(sizeof(struct file_operations) == NEVERC_KRT_FOPS_SIZE,
	       "file_operations size mismatch (6.18)");

#elif NEVERC_KRT_LINUX_AT_LEAST(6, 12, 0)

typedef unsigned int __bitwise fop_flags_t;

struct file_operations {
	struct module *owner;
	fop_flags_t fop_flags;
	u32 _fop_pad;
	loff_t (*llseek)(struct file *, loff_t, int);
	ssize_t (*read)(struct file *, char __user *, size_t, loff_t *);
	ssize_t (*write)(struct file *, const char __user *, size_t, loff_t *);
	ssize_t (*read_iter)(struct kiocb *, struct iov_iter *);
	ssize_t (*write_iter)(struct kiocb *, struct iov_iter *);
	int (*iopoll)(struct kiocb *, struct io_comp_batch *, unsigned int);
	int (*iterate_shared)(struct file *, struct dir_context *);
	__poll_t (*poll)(struct file *, struct poll_table_struct *);
	long (*unlocked_ioctl)(struct file *, unsigned int, unsigned long);
	long (*compat_ioctl)(struct file *, unsigned int, unsigned long);
	int (*mmap)(struct file *, struct vm_area_struct *);
	int (*open)(struct inode *, struct file *);
	int (*flush)(struct file *, fl_owner_t);
	int (*release)(struct inode *, struct file *);
	unsigned char _tail[NEVERC_KRT_FOPS_SIZE - 128];
};

_Static_assert(__builtin_offsetof(struct file_operations, read) == 24,
	       "file_operations.read offset mismatch (6.12)");
_Static_assert(__builtin_offsetof(struct file_operations, unlocked_ioctl) == 80,
	       "file_operations.unlocked_ioctl offset mismatch (6.12)");
_Static_assert(__builtin_offsetof(struct file_operations, open) == 104,
	       "file_operations.open offset mismatch (6.12)");
_Static_assert(__builtin_offsetof(struct file_operations, release) == 120,
	       "file_operations.release offset mismatch (6.12)");
_Static_assert(sizeof(struct file_operations) == NEVERC_KRT_FOPS_SIZE,
	       "file_operations size mismatch (6.12)");

#elif NEVERC_KRT_LINUX_AT_LEAST(6, 6, 0)

struct file_operations {
	struct module *owner;
	loff_t (*llseek)(struct file *, loff_t, int);
	ssize_t (*read)(struct file *, char __user *, size_t, loff_t *);
	ssize_t (*write)(struct file *, const char __user *, size_t, loff_t *);
	ssize_t (*read_iter)(struct kiocb *, struct iov_iter *);
	ssize_t (*write_iter)(struct kiocb *, struct iov_iter *);
	int (*iopoll)(struct kiocb *, struct io_comp_batch *, unsigned int);
	int (*iterate_shared)(struct file *, struct dir_context *);
	__poll_t (*poll)(struct file *, struct poll_table_struct *);
	long (*unlocked_ioctl)(struct file *, unsigned int, unsigned long);
	long (*compat_ioctl)(struct file *, unsigned int, unsigned long);
	int (*mmap)(struct file *, struct vm_area_struct *);
	unsigned long mmap_supported_flags;
	int (*open)(struct inode *, struct file *);
	int (*flush)(struct file *, fl_owner_t);
	int (*release)(struct inode *, struct file *);
	unsigned char _tail[NEVERC_KRT_FOPS_SIZE - 128];
};

_Static_assert(__builtin_offsetof(struct file_operations, read) == 16,
	       "file_operations.read offset mismatch (6.6)");
_Static_assert(__builtin_offsetof(struct file_operations, unlocked_ioctl) == 72,
	       "file_operations.unlocked_ioctl offset mismatch (6.6)");
_Static_assert(__builtin_offsetof(struct file_operations, open) == 104,
	       "file_operations.open offset mismatch (6.6)");
_Static_assert(__builtin_offsetof(struct file_operations, release) == 120,
	       "file_operations.release offset mismatch (6.6)");
_Static_assert(sizeof(struct file_operations) == NEVERC_KRT_FOPS_SIZE,
	       "file_operations size mismatch (6.6)");

#else /* 5.10 / 5.15 / 6.1 */

struct file_operations {
	struct module *owner;
	loff_t (*llseek)(struct file *, loff_t, int);
	ssize_t (*read)(struct file *, char __user *, size_t, loff_t *);
	ssize_t (*write)(struct file *, const char __user *, size_t, loff_t *);
	ssize_t (*read_iter)(struct kiocb *, struct iov_iter *);
	ssize_t (*write_iter)(struct kiocb *, struct iov_iter *);
#if NEVERC_KRT_LINUX_BEFORE(6, 1, 0)
	int (*iopoll)(struct kiocb *, bool);
#else
	int (*iopoll)(struct kiocb *, struct io_comp_batch *, unsigned int);
#endif
	int (*iterate)(struct file *, struct dir_context *);
	int (*iterate_shared)(struct file *, struct dir_context *);
	__poll_t (*poll)(struct file *, struct poll_table_struct *);
	long (*unlocked_ioctl)(struct file *, unsigned int, unsigned long);
	long (*compat_ioctl)(struct file *, unsigned int, unsigned long);
	int (*mmap)(struct file *, struct vm_area_struct *);
	unsigned long mmap_supported_flags;
	int (*open)(struct inode *, struct file *);
	int (*flush)(struct file *, fl_owner_t);
	int (*release)(struct inode *, struct file *);
	unsigned char _tail[NEVERC_KRT_FOPS_SIZE - 136];
};

_Static_assert(__builtin_offsetof(struct file_operations, read) == 16,
	       "file_operations.read offset mismatch (5.10-6.1)");
_Static_assert(__builtin_offsetof(struct file_operations, unlocked_ioctl) == 80,
	       "file_operations.unlocked_ioctl offset mismatch (5.10-6.1)");
_Static_assert(__builtin_offsetof(struct file_operations, open) == 112,
	       "file_operations.open offset mismatch (5.10-6.1)");
_Static_assert(__builtin_offsetof(struct file_operations, release) == 128,
	       "file_operations.release offset mismatch (5.10-6.1)");
_Static_assert(sizeof(struct file_operations) == NEVERC_KRT_FOPS_SIZE,
	       "file_operations size mismatch (5.10-6.1)");

#endif /* semantic Linux API series */

#endif /* _NEVERC_KRT_LINUX_FS_H */
