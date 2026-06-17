/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_THREAD_H
#define NEVERC_KRT_THREAD_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/sched.h>

typedef int  (*neverc_krt_kthread_should_stop_fn)(void);
typedef void (*neverc_krt_schedule_fn)(void);

NEVERC_KRT_RT_VAR neverc_krt_kthread_should_stop_fn _neverc_krt_kthread_should_stop;
NEVERC_KRT_RT_VAR neverc_krt_schedule_fn            _neverc_krt_schedule;

int neverc_krt_thread_init(void);


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
