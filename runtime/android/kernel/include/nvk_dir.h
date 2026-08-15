/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_DIR_H
#define NEVERC_KRT_DIR_H

#include <linux/types.h>

struct dir_context;

/*
 * Stack-owned storage for one synchronous directory enumeration.  Its
 * contents are private to the runtime; callers must not copy or inspect it.
 */
#define NEVERC_KRT_DIR_FILTER_SCOPE_WORDS 16U
struct neverc_krt_dir_filter_scope {
	unsigned long opaque[NEVERC_KRT_DIR_FILTER_SCOPE_WORDS];
};

typedef bool (*neverc_krt_dir_should_hide_fn)(const char *name, int namelen,
					       loff_t offset, u64 ino,
					       unsigned int type, void *opaque);

/*
 * One means dir_context is covered by the selected family on EXACT or
 * same-series COMPAT.  A live certificate may overlay offsets; it is not
 * required to use the API.
 */
int neverc_krt_dir_filter_available(void);

/*
 * Copy @original_ctx into a stack-owned proxy and replace only its actor.
 * The proxy remains valid until the synchronous matching end call.
 */
int neverc_krt_dir_filter_begin(struct neverc_krt_dir_filter_scope *scope,
				void *original_ctx,
				neverc_krt_dir_should_hide_fn should_hide,
				void *opaque, void **proxy_ctx);

/*
 * Copy the kernel-updated proxy position back and invalidate the scope.
 * A non-zero result also reports deferred position-synchronization failure.
 */
int neverc_krt_dir_filter_end(struct neverc_krt_dir_filter_scope *scope);

#endif /* NEVERC_KRT_DIR_H */
