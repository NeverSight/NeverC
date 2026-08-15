/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_TEST_TASK_THREAD_IDS_SHIM_H
#define NEVERC_KRT_TEST_TASK_THREAD_IDS_SHIM_H

#include <linux/errno.h>
#include <linux/list.h>
#include <linux/types.h>
#include <nvk_process.h>

#include <stdint.h>

#ifndef EOPNOTSUPP
#define EOPNOTSUPP ENOTSUPP
#endif
#ifndef ELOOP
#define ELOOP 40
#endif

enum neverc_krt_version_match {
	NEVERC_KRT_VER_EXACT = 0,
	NEVERC_KRT_VER_COMPAT = 1,
	NEVERC_KRT_VER_MISMATCH = -1,
	NEVERC_KRT_VER_UNKNOWN = -2,
};

#define NEVERC_KRT_LAYOUT_CERT_TASK_THREADS (1UL << 4)
#define _NEVERC_KRT_THREAD_WALK_LIMIT 16UL
#define NEVERC_KRT_THREAD_POINTER_VALID(pointer) \
	((uintptr_t)(pointer) > 4096UL && !((uintptr_t)(pointer) & 7UL))

struct neverc_krt_gki_layout {
	unsigned long task_size;
	unsigned long task_pid;
	unsigned long task_thread_pid;
	unsigned long task_signal;
	unsigned long task_thread_node;
	unsigned long signal_size;
	unsigned long signal_thread_head;
};

const struct neverc_krt_gki_layout *_neverc_krt_get_gki_layout(void);
const struct neverc_krt_gki_layout *_neverc_krt_get_proven_gki_layout(
	unsigned long required);
unsigned long _neverc_krt_current_layout_certificates(void);
int neverc_krt_check_kernel_match(void);
long neverc_krt_mem_read(void *dst, const void *src, size_t len);
int neverc_krt_test_thread_rcu_lock(void);
void neverc_krt_test_thread_rcu_unlock(void);
int neverc_krt_test_thread_nofault_available(void);

#define NEVERC_KRT_THREAD_RCU_LOCK() neverc_krt_test_thread_rcu_lock()
#define NEVERC_KRT_THREAD_RCU_UNLOCK() neverc_krt_test_thread_rcu_unlock()
#define NEVERC_KRT_THREAD_NOFAULT_AVAILABLE() \
	neverc_krt_test_thread_nofault_available()

#endif /* NEVERC_KRT_TEST_TASK_THREAD_IDS_SHIM_H */
