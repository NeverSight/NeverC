/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_THREAD_H
#define NVK_THREAD_H

#include <linux/types.h>
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

static nvk_kthread_create_fn     _nvk_kthread_create;
static nvk_wake_up_process_fn    _nvk_wake_up_process;
static nvk_kthread_stop_fn       _nvk_kthread_stop;
static nvk_kthread_should_stop_fn _nvk_kthread_should_stop;
static nvk_schedule_fn            _nvk_schedule;
static nvk_schedule_timeout_fn    _nvk_schedule_timeout;
static nvk_set_current_state_fn   _nvk_set_current_state;
static nvk_msleep_fn              _nvk_msleep_thr;
static nvk_usleep_range_fn        _nvk_usleep_range;
static int                        _nvk_thread_inited;

static int nvk_thread_init(void)
{
	if (_nvk_thread_inited) return 0;

	_nvk_kthread_create =
		(nvk_kthread_create_fn)NVK_LOOKUP("kthread_create_on_node");
	if (!_nvk_kthread_create)
		_nvk_kthread_create =
			(nvk_kthread_create_fn)NVK_LOOKUP("kthread_create");

	_nvk_wake_up_process =
		(nvk_wake_up_process_fn)NVK_LOOKUP("wake_up_process");
	_nvk_kthread_stop =
		(nvk_kthread_stop_fn)NVK_LOOKUP("kthread_stop");
	_nvk_kthread_should_stop =
		(nvk_kthread_should_stop_fn)NVK_LOOKUP("kthread_should_stop");
	_nvk_schedule =
		(nvk_schedule_fn)NVK_LOOKUP("schedule");
	_nvk_schedule_timeout =
		(nvk_schedule_timeout_fn)NVK_LOOKUP("schedule_timeout_interruptible");
	_nvk_set_current_state =
		(nvk_set_current_state_fn)NVK_LOOKUP("__set_current_state");
	_nvk_msleep_thr =
		(nvk_msleep_fn)NVK_LOOKUP("msleep");
	_nvk_usleep_range =
		(nvk_usleep_range_fn)NVK_LOOKUP("usleep_range_state");
	if (!_nvk_usleep_range)
		_nvk_usleep_range =
			(nvk_usleep_range_fn)NVK_LOOKUP("usleep_range");

	if (!_nvk_kthread_create || !_nvk_wake_up_process)
		return -1;

	_nvk_thread_inited = 1;
	return 0;
}

#define NVK_THREAD_MAX 8

struct nvk_thread {
	struct task_struct *task;
	volatile int        running;
	volatile int        stop_req;
	volatile u64        iter_count;
	const char         *name;
};

static struct nvk_thread _nvk_threads[NVK_THREAD_MAX];
static volatile int      _nvk_thread_count;
static volatile int      _nvk_thread_lock;

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

static struct task_struct *nvk_thread_run(int (*fn)(void *), void *data,
					  const char *name)
{
	struct task_struct *task;
	int idx;

	if (!_nvk_kthread_create || !_nvk_wake_up_process)
		return (void *)0;

	task = _nvk_kthread_create(fn, data, name);
	if (!task || (long)task < 0)
		return (void *)0;

	_nvk_thr_lock();
	idx = _nvk_thread_count;
	if (idx < NVK_THREAD_MAX) {
		_nvk_threads[idx].task = task;
		_nvk_threads[idx].running = 1;
		_nvk_threads[idx].stop_req = 0;
		_nvk_threads[idx].iter_count = 0;
		_nvk_threads[idx].name = name;
		_nvk_thread_count = idx + 1;
	}
	_nvk_thr_unlock();

	_nvk_wake_up_process(task);
	return task;
}

static int nvk_thread_stop(struct task_struct *task)
{
	int i, ret = 0;

	if (!task) return -1;

	if (_nvk_kthread_stop)
		ret = _nvk_kthread_stop(task);

	_nvk_thr_lock();
	for (i = 0; i < _nvk_thread_count; i++) {
		if (_nvk_threads[i].task == task) {
			_nvk_threads[i].running = 0;
			_nvk_threads[i].task = (void *)0;
			break;
		}
	}
	_nvk_thr_unlock();

	return ret;
}

static __always_inline int nvk_thread_should_stop(void)
{
	if (_nvk_kthread_should_stop)
		return _nvk_kthread_should_stop();
	return 0;
}

static void nvk_thread_sleep_ms(unsigned int ms)
{
	if (_nvk_msleep_thr)
		_nvk_msleep_thr(ms);
	else if (_nvk_schedule_timeout) {
		unsigned long hz = 100;
		long ticks = (long)ms * (long)hz / 1000;
		if (ticks < 1) ticks = 1;
		_nvk_schedule_timeout(ticks);
	}
}

static __always_inline void nvk_thread_yield(void)
{
	if (_nvk_schedule)
		_nvk_schedule();
}

static void nvk_thread_stop_all(void)
{
	struct task_struct *tasks[NVK_THREAD_MAX];
	int cnt, i;

	_nvk_thr_lock();
	cnt = _nvk_thread_count;
	for (i = 0; i < cnt; i++) {
		tasks[i] = _nvk_threads[i].task;
		_nvk_threads[i].running = 0;
		_nvk_threads[i].task = (void *)0;
	}
	_nvk_thread_count = 0;
	_nvk_thr_unlock();

	for (i = 0; i < cnt; i++) {
		if (tasks[i] && _nvk_kthread_stop)
			_nvk_kthread_stop(tasks[i]);
	}
}

static int nvk_thread_active_count(void)
{
	int i, count = 0;
	_nvk_thr_lock();
	for (i = 0; i < _nvk_thread_count; i++) {
		if (_nvk_threads[i].running)
			count++;
	}
	_nvk_thr_unlock();
	return count;
}

#define nvk_thread_run_enc(fn, data, name_lit) \
	nvk_thread_run((fn), (data), NC_XORSTR(name_lit))

#endif /* NVK_THREAD_H */
