/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_KTHREAD_H
#define _NVK_LINUX_KTHREAD_H

#include <linux/types.h>

struct task_struct;

struct task_struct *kthread_create_on_node(int (*threadfn)(void *data),
					  void *data, int node,
					  const char *namefmt, ...);

#define kthread_create(threadfn, data, namefmt, ...)                          \
	kthread_create_on_node(threadfn, data, -1, namefmt, ##__VA_ARGS__)

#define kthread_run(threadfn, data, namefmt, ...)                             \
	({                                                                    \
		struct task_struct *__k =                                     \
			kthread_create(threadfn, data, namefmt,               \
				       ##__VA_ARGS__);                        \
		if (__k && !((unsigned long)__k & 0xFFF))                     \
			wake_up_process(__k);                                 \
		__k;                                                          \
	})

int kthread_stop(struct task_struct *k);
bool kthread_should_stop(void);
bool kthread_should_park(void);
void kthread_parkme(void);
int kthread_park(struct task_struct *k);
void kthread_unpark(struct task_struct *k);
int wake_up_process(struct task_struct *tsk);

#endif /* _NVK_LINUX_KTHREAD_H */
