/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_TEST_BINDER_FILTER_SHIM_H
#define NEVERC_KRT_TEST_BINDER_FILTER_SHIM_H

#include <errno.h>
#include <stddef.h>

#include <nvk_binder.h>

#ifndef __user
#define __user
#endif

#define READ_ONCE(value) (value)
#define WRITE_ONCE(value, new_value) ((value) = (new_value))

struct file {
	int unused;
};

struct file_operations {
	long (*unlocked_ioctl)(struct file *filp, unsigned int cmd,
			       unsigned long arg);
};

struct neverc_krt_interpose {
	int active;
};

enum neverc_krt_binder_filter_backend {
	NEVERC_KRT_BINDER_FILTER_BACKEND_UNSUPPORTED = 0,
	NEVERC_KRT_BINDER_FILTER_BACKEND_TRANSACTION = 1,
};

struct neverc_krt_runtime_caps {
	enum neverc_krt_binder_filter_backend binder_filter_backend;
};

unsigned long neverc_krt_binder_test_lookup(const char *name);
#define NEVERC_KRT_LOOKUP(name) \
	((void *)neverc_krt_binder_test_lookup(name))

const struct neverc_krt_runtime_caps *_neverc_krt_current_caps(void);
long neverc_krt_mem_read_user(void *dst, const void __user *src, size_t len);
int neverc_krt_current_pid(void);
int neverc_krt_interpose_install(struct neverc_krt_interpose *handle,
				 void *target, void *replacement,
				 void **original);
int neverc_krt_interpose_remove(struct neverc_krt_interpose *handle);

#endif /* NEVERC_KRT_TEST_BINDER_FILTER_SHIM_H */
