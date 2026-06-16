/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_THREAD_H
#define NVK_THREAD_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <nvk_mem.h>

typedef struct task_struct *(*nvk_kthread_create_fn)(
	int (*fn)(void *), void *data, const char *namefmt, ...);
typedef int  (*nvk_wake_up_process_fn)(struct task_struct *);
typedef int  (*nvk_kthread_stop_fn)(struct task_struct *);
typedef int  (*nvk_kthread_should_stop_fn)(void);
typedef void (*nvk_schedule_fn)(void);
typedef long (*nvk_schedule_timeout_fn)(long timeout);
typedef void (*nvk_set_current_state_fn)(long state);
typedef void (*nvk_msleep_fn)(unsigned int msecs);
typedef void (*nvk_usleep_range_fn)(unsigned long min, unsigned long max);

NVK_RT_VAR nvk_kthread_create_fn     _nvk_kthread_create;
NVK_RT_VAR nvk_wake_up_process_fn    _nvk_wake_up_process;
NVK_RT_VAR nvk_kthread_stop_fn       _nvk_kthread_stop;
NVK_RT_VAR nvk_kthread_should_stop_fn _nvk_kthread_should_stop;
NVK_RT_VAR nvk_schedule_fn            _nvk_schedule;
NVK_RT_VAR nvk_schedule_timeout_fn    _nvk_schedule_timeout;
NVK_RT_VAR nvk_set_current_state_fn   _nvk_set_current_state;
NVK_RT_VAR nvk_msleep_fn              _nvk_msleep_thr;
NVK_RT_VAR nvk_usleep_range_fn        _nvk_usleep_range;
NVK_RT_VAR int                        _nvk_thread_inited;

int nvk_thread_init(void);


#define NVK_THREAD_MAX 8

#define NVK_THREAD_NAME_LEN 16

struct nvk_thread {
	struct task_struct *task;
	volatile int        running;
	volatile int        stop_req;
	volatile u64        iter_count;
	char                name[NVK_THREAD_NAME_LEN];
};

NVK_RT_VAR struct nvk_thread _nvk_threads[NVK_THREAD_MAX];
NVK_RT_VAR volatile int      _nvk_thread_count;
NVK_RT_VAR volatile int      _nvk_thread_lock;

static __always_inline void _nvk_thr_lock(void)
{
	while (__atomic_exchange_n(&_nvk_thread_lock, 1, __ATOMIC_ACQUIRE))
		__asm__ __volatile__("wfe" ::: "memory");
}

static __always_inline void _nvk_thr_unlock(void)
{
	__atomic_store_n(&_nvk_thread_lock, 0, __ATOMIC_RELEASE);
	__asm__ __volatile__("sev" ::: "memory");
}

struct task_struct *nvk_thread_run(int (*fn)(void *), void *data,
					  const char *name);


int nvk_thread_stop(struct task_struct *task);


static __always_inline int nvk_thread_should_stop(void)
{
	if (_nvk_kthread_should_stop)
		return _nvk_kthread_should_stop();
	return 0;
}

void nvk_thread_sleep_ms(unsigned int ms);


static __always_inline void nvk_thread_yield(void)
{
	if (_nvk_schedule)
		_nvk_schedule();
}

void nvk_thread_stop_all(void);


int nvk_thread_active_count(void);


#define nvk_thread_run_enc(fn, data, name_lit) \
	nvk_thread_run((fn), (data), NC_XORSTR(name_lit))

#endif /* NVK_THREAD_H */
