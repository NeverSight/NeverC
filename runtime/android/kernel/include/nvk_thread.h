/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_THREAD_H
#define NEVERC_KRT_THREAD_H

#include <linux/types.h>
#include <linux/sched.h>

int neverc_krt_thread_init(void);


struct task_struct *neverc_krt_thread_run(int (*fn)(void *), void *data,
					  const char *name);


int neverc_krt_thread_stop(struct task_struct *task);


int neverc_krt_thread_should_stop(void);


void neverc_krt_thread_sleep_ms(unsigned int ms);


void neverc_krt_thread_yield(void);


void neverc_krt_thread_stop_all(void);


int neverc_krt_thread_active_count(void);


#define neverc_krt_thread_run_enc(fn, data, name_lit) \
	neverc_krt_thread_run((fn), (data), NC_XORSTR(name_lit))

#endif /* NEVERC_KRT_THREAD_H */
