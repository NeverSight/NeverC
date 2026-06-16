/* SPDX-License-Identifier: GPL-2.0 */
/* nvk_thread.c — implementations extracted from nvk_thread.h. */
#include <nvk.h>

int nvk_thread_init(void)
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

struct task_struct *nvk_thread_run(int (*fn)(void *), void *data,
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
		{
			int ni = 0;
			if (name) {
				while (name[ni] && ni < NVK_THREAD_NAME_LEN - 1) {
					_nvk_threads[idx].name[ni] = name[ni];
					ni++;
				}
			}
			_nvk_threads[idx].name[ni] = '\0';
		}
		_nvk_thread_count = idx + 1;
	}
	_nvk_thr_unlock();

	_nvk_wake_up_process(task);
	return task;
}

int nvk_thread_stop(struct task_struct *task)
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

void nvk_thread_sleep_ms(unsigned int ms)
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

void nvk_thread_stop_all(void)
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

int nvk_thread_active_count(void)
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

