/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_TEST_TASK_WALK_SHIM_H
#define NEVERC_KRT_TEST_TASK_WALK_SHIM_H

#include <linux/errno.h>
#include <linux/list.h>
#include <linux/types.h>
#include <nvk_process.h>

#include <stddef.h>

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
#define NEVERC_KRT_LAYOUT_CERT_TASK_WALK (1UL << 5)
#define NEVERC_KRT_LAYOUT_CERT_TASK_REF (1UL << 6)
#define NEVERC_KRT_LAYOUT_CERT_TASK_USER_STATE (1UL << 7)
#define NEVERC_KRT_TASK_LAYOUT_WALK (1U << 0)
#define NEVERC_KRT_TASK_LAYOUT_REF (1U << 1)
#define NEVERC_KRT_TASK_LAYOUT_USER_STATE (1U << 2)
#define NEVERC_KRT_TASK_LAYOUT_THREADS (1U << 3)
#define _NEVERC_KRT_TASK_WALK_LIMIT 8UL

struct neverc_krt_gki_layout {
	unsigned long task_size;
	unsigned long task_tasks;
};

const struct neverc_krt_gki_layout *_neverc_krt_get_gki_layout(void);
const struct neverc_krt_gki_layout *_neverc_krt_get_proven_gki_layout(
	unsigned long required);
unsigned long _neverc_krt_current_layout_certificates(void);
int neverc_krt_check_kernel_match(void);
long neverc_krt_mem_read(void *dst, const void *src, size_t len);
struct task_struct *neverc_krt_test_task_walk_init_task(void);
int neverc_krt_test_task_walk_pointer_valid(const void *pointer);
int neverc_krt_test_task_walk_rcu_lock(void);
void neverc_krt_test_task_walk_rcu_unlock(void);
int neverc_krt_test_task_walk_rcu_available(void);
int neverc_krt_test_task_walk_nofault_available(void);
int neverc_krt_test_task_runtime_init(void);
int neverc_krt_task_layout_available(unsigned int required);
int neverc_krt_test_task_pid_available(void);
int neverc_krt_test_task_ref_available(void);
int neverc_krt_test_task_user_state_available(void);

#define NEVERC_KRT_TASK_WALK_INIT_TASK() \
	neverc_krt_test_task_walk_init_task()
#define NEVERC_KRT_TASK_WALK_POINTER_VALID(pointer) \
	neverc_krt_test_task_walk_pointer_valid(pointer)
#define NEVERC_KRT_TASK_WALK_RCU_LOCK() neverc_krt_test_task_walk_rcu_lock()
#define NEVERC_KRT_TASK_WALK_RCU_UNLOCK() neverc_krt_test_task_walk_rcu_unlock()
#define NEVERC_KRT_TASK_WALK_RCU_AVAILABLE() \
	neverc_krt_test_task_walk_rcu_available()
#define NEVERC_KRT_TASK_WALK_NOFAULT_AVAILABLE() \
	neverc_krt_test_task_walk_nofault_available()
#define NEVERC_KRT_TASK_RUNTIME_INIT() neverc_krt_test_task_runtime_init()
#define NEVERC_KRT_TASK_PID_AVAILABLE() neverc_krt_test_task_pid_available()
#define NEVERC_KRT_TASK_REF_AVAILABLE() neverc_krt_test_task_ref_available()
#define NEVERC_KRT_TASK_USER_STATE_AVAILABLE() \
	neverc_krt_test_task_user_state_available()

#endif /* NEVERC_KRT_TEST_TASK_WALK_SHIM_H */
