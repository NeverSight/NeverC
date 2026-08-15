/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_TEST_INODE_METADATA_SHIM_H
#define NEVERC_KRT_TEST_INODE_METADATA_SHIM_H

#include <nvk_inode.h>

enum neverc_krt_version_match {
	NEVERC_KRT_VER_EXACT = 0,
	NEVERC_KRT_VER_COMPAT = 1,
	NEVERC_KRT_VER_MISMATCH = -1,
	NEVERC_KRT_VER_UNKNOWN = -2,
};

#define NEVERC_KRT_LAYOUT_CERT_INODE_TIMES (1UL << 1)
#define NEVERC_KRT_LAYOUT_CERT_PATH_INODE  (1UL << 2)
#define NEVERC_KRT_LAYOUT_CERT_FILENAME_NAME (1UL << 3)

struct neverc_krt_gki_layout {
	unsigned long filename_size;
	unsigned long filename_name;
	unsigned long filename_name_size;
	unsigned long path_size;
	unsigned long path_dentry;
	unsigned long path_dentry_size;
	unsigned long dentry_size;
	unsigned long dentry_inode;
	unsigned long dentry_inode_size;
	unsigned long inode_size;
	unsigned long inode_atime_sec;
	unsigned long inode_atime_sec_size;
	unsigned long inode_mtime_sec;
	unsigned long inode_mtime_sec_size;
	unsigned long inode_atime_nsec;
	unsigned long inode_atime_nsec_size;
	unsigned long inode_mtime_nsec;
	unsigned long inode_mtime_nsec_size;
};

const struct neverc_krt_gki_layout *_neverc_krt_get_gki_layout(void);
const struct neverc_krt_gki_layout *_neverc_krt_get_proven_gki_layout(
	unsigned long required);
int neverc_krt_check_kernel_match(void);
unsigned long _neverc_krt_current_layout_certificates(void);
long neverc_krt_mem_read(void *dst, const void *src, size_t len);
long neverc_krt_mem_write(void *dst, const void *src, size_t len);
void *neverc_krt_test_inode_lookup(const char *name);

#define NEVERC_KRT_LOOKUP(name) neverc_krt_test_inode_lookup(name)

#endif /* NEVERC_KRT_TEST_INODE_METADATA_SHIM_H */
