/* SPDX-License-Identifier: GPL-2.0 */
/* Build against prepared native kernel headers, independently of NeverC SDK. */
#include <linux/module.h>
#include <linux/sched.h>
/* Retain native get_current code so the verifier can check its system register. */
static __used noinline struct task_struct *probe_current(void) { return current; }
static int __init probe_init(void) { return 0; }
static void __exit probe_exit(void) {}
module_init(probe_init);
module_exit(probe_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("NeverC native loader and current-task ABI probe");
