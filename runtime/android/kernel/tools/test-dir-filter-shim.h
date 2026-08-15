/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_TEST_DIR_FILTER_SHIM_H
#define NEVERC_KRT_TEST_DIR_FILTER_SHIM_H

#include <nvk_dir.h>

enum neverc_krt_filldir_abi {
	NEVERC_KRT_FILLDIR_ABI_UNSUPPORTED = 0,
	NEVERC_KRT_FILLDIR_ABI_RETURNS_INT = 1,
	NEVERC_KRT_FILLDIR_ABI_RETURNS_BOOL = 2,
};

enum neverc_krt_version_match {
	NEVERC_KRT_VER_EXACT = 0,
	NEVERC_KRT_VER_COMPAT = 1,
	NEVERC_KRT_VER_MISMATCH = -1,
	NEVERC_KRT_VER_UNKNOWN = -2,
};

#define NEVERC_KRT_LAYOUT_CERT_DIR_CONTEXT (1UL << 0)

struct neverc_krt_runtime_caps {
	enum neverc_krt_filldir_abi filldir_abi;
};

struct neverc_krt_gki_layout {
	unsigned long dir_context_size;
	unsigned long dir_context_actor;
	unsigned long dir_context_actor_size;
	unsigned long dir_context_pos;
	unsigned long dir_context_pos_size;
};

const struct neverc_krt_runtime_caps *_neverc_krt_current_caps(void);
const struct neverc_krt_gki_layout *_neverc_krt_get_gki_layout(void);
int neverc_krt_check_kernel_match(void);
unsigned long _neverc_krt_current_layout_certificates(void);
long neverc_krt_mem_read(void *dst, const void *src, size_t len);
long neverc_krt_mem_write(void *dst, const void *src, size_t len);

#endif /* NEVERC_KRT_TEST_DIR_FILTER_SHIM_H */
