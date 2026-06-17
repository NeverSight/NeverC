/* SPDX-License-Identifier: GPL-2.0 */
/* neverc_krt_thread.c — kernel thread management. */
#include <nvk.h>

/* ---- internal types ---- */

typedef struct task_struct *(*neverc_krt_kthread_create_fn)(
	int (*fn)(void *), void *data, const char *namefmt, ...);
typedef int  (*neverc_krt_wake_up_process_fn)(struct task_struct *);
typedef int  (*neverc_krt_kthread_stop_fn)(struct task_struct *);
typedef long (*neverc_krt_schedule_timeout_fn)(long timeout);
typedef void (*neverc_krt_set_current_state_fn)(long state);
typedef void (*neverc_krt_msleep_fn)(unsigned int msecs);
typedef void (*neverc_krt_usleep_range_fn)(unsigned long min, unsigned long max);

/* ---- internal variables (file-local) ---- */

static neverc_krt_kthread_create_fn     _neverc_krt_kthread_create;
static neverc_krt_wake_up_process_fn    _neverc_krt_wake_up_process;
static neverc_krt_kthread_stop_fn       _neverc_krt_kthread_stop;
static neverc_krt_schedule_timeout_fn   _neverc_krt_schedule_timeout;
static neverc_krt_set_current_state_fn  _neverc_krt_set_current_state;
static neverc_krt_msleep_fn             _neverc_krt_msleep_thr;
static neverc_krt_usleep_range_fn       _neverc_krt_usleep_range;
static int                              _neverc_krt_thread_inited;

#define NEVERC_KRT_THREAD_MAX 8
#define NEVERC_KRT_THREAD_NAME_LEN 16

struct neverc_krt_thread {
	struct task_struct *task;
	volatile int        running;
	volatile int        stop_req;
	volatile u64        iter_count;
	char                name[NEVERC_KRT_THREAD_NAME_LEN];
};

static struct neverc_krt_thread _neverc_krt_threads[NEVERC_KRT_THREAD_MAX];
static volatile int             _neverc_krt_thread_count;
static volatile int             _neverc_krt_thread_lock;

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

int neverc_krt_thread_init(void)
{
	if (_neverc_krt_thread_inited) return 0;

	_neverc_krt_kthread_create =
		(neverc_krt_kthread_create_fn)NEVERC_KRT_LOOKUP("kthread_create_on_node");
	if (!_neverc_krt_kthread_create)
		_neverc_krt_kthread_create =
			(neverc_krt_kthread_create_fn)NEVERC_KRT_LOOKUP("kthread_create");

	_neverc_krt_wake_up_process =
		(neverc_krt_wake_up_process_fn)NEVERC_KRT_LOOKUP("wake_up_process");
	_neverc_krt_kthread_stop =
		(neverc_krt_kthread_stop_fn)NEVERC_KRT_LOOKUP("kthread_stop");
	_neverc_krt_kthread_should_stop =
		(neverc_krt_kthread_should_stop_fn)NEVERC_KRT_LOOKUP("kthread_should_stop");
	_neverc_krt_schedule =
		(neverc_krt_schedule_fn)NEVERC_KRT_LOOKUP("schedule");
	_neverc_krt_schedule_timeout =
		(neverc_krt_schedule_timeout_fn)NEVERC_KRT_LOOKUP("schedule_timeout_interruptible");
	_neverc_krt_set_current_state =
		(neverc_krt_set_current_state_fn)NEVERC_KRT_LOOKUP("__set_current_state");
	_neverc_krt_msleep_thr =
		(neverc_krt_msleep_fn)NEVERC_KRT_LOOKUP("msleep");
	_neverc_krt_usleep_range =
		(neverc_krt_usleep_range_fn)NEVERC_KRT_LOOKUP("usleep_range_state");
	if (!_neverc_krt_usleep_range)
		_neverc_krt_usleep_range =
			(neverc_krt_usleep_range_fn)NEVERC_KRT_LOOKUP("usleep_range");

	if (!_neverc_krt_kthread_create || !_neverc_krt_wake_up_process)
		return -1;

	_neverc_krt_thread_inited = 1;
	return 0;
}

struct task_struct *neverc_krt_thread_run(int (*fn)(void *), void *data,
					  const char *name)
{
	struct task_struct *task;
	int idx;

	if (!_neverc_krt_kthread_create || !_neverc_krt_wake_up_process)
		return (void *)0;

	task = _neverc_krt_kthread_create(fn, data, name);
	if (!task || (long)task < 0)
		return (void *)0;

	_neverc_krt_thr_lock();
	idx = _neverc_krt_thread_count;
	if (idx < NEVERC_KRT_THREAD_MAX) {
		_neverc_krt_threads[idx].task = task;
		_neverc_krt_threads[idx].running = 1;
		_neverc_krt_threads[idx].stop_req = 0;
		_neverc_krt_threads[idx].iter_count = 0;
		{
			int ni = 0;
			if (name) {
				while (name[ni] && ni < NEVERC_KRT_THREAD_NAME_LEN - 1) {
					_neverc_krt_threads[idx].name[ni] = name[ni];
					ni++;
				}
			}
			_neverc_krt_threads[idx].name[ni] = '\0';
		}
		_neverc_krt_thread_count = idx + 1;
	}
	_neverc_krt_thr_unlock();

	_neverc_krt_wake_up_process(task);
	return task;
}

int neverc_krt_thread_stop(struct task_struct *task)
{
	int i, ret = 0;

	if (!task) return -1;

	if (_neverc_krt_kthread_stop)
		ret = _neverc_krt_kthread_stop(task);

	_neverc_krt_thr_lock();
	for (i = 0; i < _neverc_krt_thread_count; i++) {
		if (_neverc_krt_threads[i].task == task) {
			_neverc_krt_threads[i].running = 0;
			_neverc_krt_threads[i].task = (void *)0;
			break;
		}
	}
	_neverc_krt_thr_unlock();

	return ret;
}

void neverc_krt_thread_sleep_ms(unsigned int ms)
{
	if (_neverc_krt_msleep_thr)
		_neverc_krt_msleep_thr(ms);
	else if (_neverc_krt_schedule_timeout) {
		unsigned long hz = 100;
		long ticks = (long)ms * (long)hz / 1000;
		if (ticks < 1) ticks = 1;
		_neverc_krt_schedule_timeout(ticks);
	}
}

void neverc_krt_thread_stop_all(void)
{
	struct task_struct *tasks[NEVERC_KRT_THREAD_MAX];
	int cnt, i;

	_neverc_krt_thr_lock();
	cnt = _neverc_krt_thread_count;
	for (i = 0; i < cnt; i++) {
		tasks[i] = _neverc_krt_threads[i].task;
		_neverc_krt_threads[i].running = 0;
		_neverc_krt_threads[i].task = (void *)0;
	}
	_neverc_krt_thread_count = 0;
	_neverc_krt_thr_unlock();

	for (i = 0; i < cnt; i++) {
		if (tasks[i] && _neverc_krt_kthread_stop)
			_neverc_krt_kthread_stop(tasks[i]);
	}
}

int neverc_krt_thread_active_count(void)
{
	int i, count = 0;
	_neverc_krt_thr_lock();
	for (i = 0; i < _neverc_krt_thread_count; i++) {
		if (_neverc_krt_threads[i].running)
			count++;
	}
	_neverc_krt_thr_unlock();
	return count;
}

