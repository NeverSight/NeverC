/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_THREAD_H
#define NEVERC_KRT_THREAD_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <nvk_mem.h>

typedef struct task_struct *(*neverc_krt_kthread_create_fn)(
	int (*fn)(void *), void *data, const char *namefmt, ...);
typedef int  (*neverc_krt_wake_up_process_fn)(struct task_struct *);
typedef int  (*neverc_krt_kthread_stop_fn)(struct task_struct *);
typedef int  (*neverc_krt_kthread_should_stop_fn)(void);
typedef void (*neverc_krt_schedule_fn)(void);
typedef long (*neverc_krt_schedule_timeout_fn)(long timeout);
typedef void (*neverc_krt_set_current_state_fn)(long state);
typedef void (*neverc_krt_msleep_fn)(unsigned int msecs);
typedef void (*neverc_krt_usleep_range_fn)(unsigned long min, unsigned long max);

NEVERC_KRT_RT_VAR neverc_krt_kthread_create_fn     _neverc_krt_kthread_create;
NEVERC_KRT_RT_VAR neverc_krt_wake_up_process_fn    _neverc_krt_wake_up_process;
NEVERC_KRT_RT_VAR neverc_krt_kthread_stop_fn       _neverc_krt_kthread_stop;
NEVERC_KRT_RT_VAR neverc_krt_kthread_should_stop_fn _neverc_krt_kthread_should_stop;
NEVERC_KRT_RT_VAR neverc_krt_schedule_fn            _neverc_krt_schedule;
NEVERC_KRT_RT_VAR neverc_krt_schedule_timeout_fn    _neverc_krt_schedule_timeout;
NEVERC_KRT_RT_VAR neverc_krt_set_current_state_fn   _neverc_krt_set_current_state;
NEVERC_KRT_RT_VAR neverc_krt_msleep_fn              _neverc_krt_msleep_thr;
NEVERC_KRT_RT_VAR neverc_krt_usleep_range_fn        _neverc_krt_usleep_range;
NEVERC_KRT_RT_VAR int                        _neverc_krt_thread_inited;

int neverc_krt_thread_init(void);


#define NEVERC_KRT_THREAD_MAX 8

#define NEVERC_KRT_THREAD_NAME_LEN 16

struct neverc_krt_thread {
	struct task_struct *task;
	volatile int        running;
	volatile int        stop_req;
	volatile u64        iter_count;
	char                name[NEVERC_KRT_THREAD_NAME_LEN];
};

NEVERC_KRT_RT_VAR struct neverc_krt_thread _neverc_krt_threads[NEVERC_KRT_THREAD_MAX];
NEVERC_KRT_RT_VAR volatile int      _neverc_krt_thread_count;
NEVERC_KRT_RT_VAR volatile int      _neverc_krt_thread_lock;

static __always_inline void _neverc_krt_thr_lock(void)
{
	while (__atomic_exchange_n(&_neverc_krt_thread_lock, 1, __ATOMIC_ACQUIRE))
		__asm__ __volatile__("wfe" ::: "memory");
}

static __always_inline void _neverc_krt_thr_unlock(void)
{
	__atomic_store_n(&_neverc_krt_thread_lock, 0, __ATOMIC_RELEASE);
	__asm__ __volatile__("sev" ::: "memory");
}

struct task_struct *neverc_krt_thread_run(int (*fn)(void *), void *data,
					  const char *name);


int neverc_krt_thread_stop(struct task_struct *task);


static __always_inline int neverc_krt_thread_should_stop(void)
{
	if (_neverc_krt_kthread_should_stop)
		return _neverc_krt_kthread_should_stop();
	return 0;
}

void neverc_krt_thread_sleep_ms(unsigned int ms);


static __always_inline void neverc_krt_thread_yield(void)
{
	if (_neverc_krt_schedule)
		_neverc_krt_schedule();
}

void neverc_krt_thread_stop_all(void);


int neverc_krt_thread_active_count(void);


#define neverc_krt_thread_run_enc(fn, data, name_lit) \
	neverc_krt_thread_run((fn), (data), NC_XORSTR(name_lit))

#endif /* NEVERC_KRT_THREAD_H */
