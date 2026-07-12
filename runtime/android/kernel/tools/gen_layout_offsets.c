// SPDX-License-Identifier: GPL-2.0
/* Emit layout evidence for every GKI type used by value or by field offset. */
#ifdef NVK_GEN_KSRC
#include <asm/ptrace.h>
#include <asm/thread_info.h>
#include <linux/cpumask.h>
#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/firmware.h>
#include <linux/hrtimer.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/kprobes.h>
#include <linux/miscdevice.h>
#include <linux/mm_types.h>
#include <linux/netlink.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/proc_fs.h>
#include <linux/regmap.h>
#include <linux/scatterlist.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/skbuff.h>
#include <linux/stddef.h>
#include <linux/sysfs.h>
#include <linux/timer.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/netfilter.h>

#define NVK_EMIT(name, val)                                                    \
	__asm__ __volatile__("\n.ascii \"==NVK== " #name " %0 ==\"\n"           \
			     :                                                 \
			     : "i"((long)(val)))

void nvk_gen_layout_offsets(void)
{
	NVK_EMIT(TASK_SIZE, sizeof(struct task_struct));
	NVK_EMIT(TASK_TASKS, offsetof(struct task_struct, tasks));
	NVK_EMIT(TASK_MM, offsetof(struct task_struct, mm));
	NVK_EMIT(TASK_ACTIVE_MM, offsetof(struct task_struct, active_mm));
	NVK_EMIT(TASK_PID, offsetof(struct task_struct, pid));
	NVK_EMIT(TASK_TGID, offsetof(struct task_struct, tgid));
	NVK_EMIT(TASK_GROUP_LEADER, offsetof(struct task_struct, group_leader));
	NVK_EMIT(TASK_THREAD_PID, offsetof(struct task_struct, thread_pid));
	NVK_EMIT(TASK_REAL_CRED, offsetof(struct task_struct, real_cred));
	NVK_EMIT(TASK_CRED, offsetof(struct task_struct, cred));
	NVK_EMIT(TASK_COMM, offsetof(struct task_struct, comm));
	NVK_EMIT(TASK_NSPROXY, offsetof(struct task_struct, nsproxy));
#ifdef CONFIG_SECCOMP
	NVK_EMIT(TASK_SECCOMP, offsetof(struct task_struct, seccomp));
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
	NVK_EMIT(TASK_CPU,
		  offsetof(struct task_struct, thread_info) +
		  offsetof(struct thread_info, cpu));
#endif
	NVK_EMIT(CPUMASK_SIZE, sizeof(struct cpumask));
	NVK_EMIT(CONFIG_NR_CPUS, NR_CPUS);
	NVK_EMIT(PAGE_SHIFT, PAGE_SHIFT);
	NVK_EMIT(CONFIG_ARM64_VA_BITS, CONFIG_ARM64_VA_BITS);
	NVK_EMIT(CONFIG_ARM64_PA_BITS, CONFIG_ARM64_PA_BITS);
	NVK_EMIT(CONFIG_PGTABLE_LEVELS, CONFIG_PGTABLE_LEVELS);

	NVK_EMIT(CRED_SIZE, sizeof(struct cred));
	NVK_EMIT(CRED_UID, offsetof(struct cred, uid));
	NVK_EMIT(CRED_GID, offsetof(struct cred, gid));
	NVK_EMIT(CRED_CAP_INHERITABLE, offsetof(struct cred, cap_inheritable));

	NVK_EMIT(VMA_SIZE, sizeof(struct vm_area_struct));
	NVK_EMIT(VMA_START, offsetof(struct vm_area_struct, vm_start));
	NVK_EMIT(VMA_END, offsetof(struct vm_area_struct, vm_end));
	NVK_EMIT(VMA_MM, offsetof(struct vm_area_struct, vm_mm));
	NVK_EMIT(VMA_PAGE_PROT, offsetof(struct vm_area_struct, vm_page_prot));
	NVK_EMIT(VMA_FLAGS, offsetof(struct vm_area_struct, vm_flags));
	NVK_EMIT(VMA_PGOFF, offsetof(struct vm_area_struct, vm_pgoff));
	NVK_EMIT(VMA_FILE, offsetof(struct vm_area_struct, vm_file));

	NVK_EMIT(FILE_SIZE, sizeof(struct file));
	NVK_EMIT(FILE_PATH_DENTRY,
		  offsetof(struct file, f_path) + offsetof(struct path, dentry));
	NVK_EMIT(DENTRY_DNAME_NAME,
		  offsetof(struct dentry, d_name) + offsetof(struct qstr, name));

	NVK_EMIT(KPROBE_SIZE, sizeof(struct kprobe));
	NVK_EMIT(KPROBE_ADDR, offsetof(struct kprobe, addr));
	NVK_EMIT(KPROBE_SYMBOL_NAME, offsetof(struct kprobe, symbol_name));
	NVK_EMIT(KPROBE_PRE_HANDLER, offsetof(struct kprobe, pre_handler));

	NVK_EMIT(WORK_SIZE, sizeof(struct work_struct));
	NVK_EMIT(WORK_DATA, offsetof(struct work_struct, data));
	NVK_EMIT(WORK_ENTRY, offsetof(struct work_struct, entry));
	NVK_EMIT(WORK_FUNC, offsetof(struct work_struct, func));
	NVK_EMIT(DELAYED_WORK_SIZE, sizeof(struct delayed_work));
	NVK_EMIT(DELAYED_WORK_WORK, offsetof(struct delayed_work, work));
	NVK_EMIT(DELAYED_WORK_TIMER, offsetof(struct delayed_work, timer));

	NVK_EMIT(TIMER_SIZE, sizeof(struct timer_list));
	NVK_EMIT(TIMER_ENTRY, offsetof(struct timer_list, entry));
	NVK_EMIT(TIMER_EXPIRES, offsetof(struct timer_list, expires));
	NVK_EMIT(TIMER_FUNCTION, offsetof(struct timer_list, function));
	NVK_EMIT(TIMER_FLAGS, offsetof(struct timer_list, flags));

	NVK_EMIT(HRTIMER_SIZE, sizeof(struct hrtimer));
	NVK_EMIT(HRTIMER_FUNCTION, offsetof(struct hrtimer, function));
	NVK_EMIT(HRTIMER_BASE, offsetof(struct hrtimer, base));

	NVK_EMIT(WAIT_QUEUE_HEAD_SIZE, sizeof(struct wait_queue_head));
	NVK_EMIT(WAIT_QUEUE_HEAD_LOCK, offsetof(struct wait_queue_head, lock));
	NVK_EMIT(WAIT_QUEUE_HEAD_LIST, offsetof(struct wait_queue_head, head));
	NVK_EMIT(WAIT_QUEUE_ENTRY_SIZE, sizeof(struct wait_queue_entry));
	NVK_EMIT(WAIT_QUEUE_ENTRY_FLAGS,
		  offsetof(struct wait_queue_entry, flags));
	NVK_EMIT(WAIT_QUEUE_ENTRY_PRIVATE,
		  offsetof(struct wait_queue_entry, private));
	NVK_EMIT(WAIT_QUEUE_ENTRY_FUNC,
		  offsetof(struct wait_queue_entry, func));
	NVK_EMIT(WAIT_QUEUE_ENTRY_LIST,
		  offsetof(struct wait_queue_entry, entry));
	NVK_EMIT(COMPLETION_SIZE, sizeof(struct completion));
	NVK_EMIT(COMPLETION_DONE, offsetof(struct completion, done));
	NVK_EMIT(COMPLETION_WAIT, offsetof(struct completion, wait));
	NVK_EMIT(TASKLET_SIZE, sizeof(struct tasklet_struct));

	NVK_EMIT(IDR_SIZE, sizeof(struct idr));
	NVK_EMIT(IDR_BASE, offsetof(struct idr, idr_base));
	NVK_EMIT(IDR_NEXT, offsetof(struct idr, idr_next));

	NVK_EMIT(ATTRIBUTE_SIZE, sizeof(struct attribute));
	NVK_EMIT(ATTRIBUTE_MODE, offsetof(struct attribute, mode));
	NVK_EMIT(ATTRIBUTE_GROUP_SIZE, sizeof(struct attribute_group));
	NVK_EMIT(ATTRIBUTE_GROUP_ATTRS,
		  offsetof(struct attribute_group, attrs));
	NVK_EMIT(ATTRIBUTE_GROUP_BIN_ATTRS,
		  offsetof(struct attribute_group, bin_attrs));

	NVK_EMIT(SEMAPHORE_SIZE, sizeof(struct semaphore));
	NVK_EMIT(SEMAPHORE_LOCK, offsetof(struct semaphore, lock));
	NVK_EMIT(SEMAPHORE_COUNT, offsetof(struct semaphore, count));
	NVK_EMIT(SEMAPHORE_WAIT_LIST, offsetof(struct semaphore, wait_list));

	NVK_EMIT(PROC_OPS_LSEEK, offsetof(struct proc_ops, proc_lseek));
	NVK_EMIT(PROC_OPS_SIZE, sizeof(struct proc_ops));
	NVK_EMIT(NETLINK_CFG_SIZE, sizeof(struct netlink_kernel_cfg));
	NVK_EMIT(NETLINK_CFG_GROUPS,
		  offsetof(struct netlink_kernel_cfg, groups));
	NVK_EMIT(NETLINK_CFG_FLAGS, offsetof(struct netlink_kernel_cfg, flags));
	NVK_EMIT(NETLINK_CFG_INPUT, offsetof(struct netlink_kernel_cfg, input));

	NVK_EMIT(SKB_DATA, offsetof(struct sk_buff, data));
	NVK_EMIT(SKB_HEAD, offsetof(struct sk_buff, head));

	NVK_EMIT(NF_HOOK_OPS_SIZE, sizeof(struct nf_hook_ops));
	NVK_EMIT(NF_HOOK_OPS_HOOK, offsetof(struct nf_hook_ops, hook));
	NVK_EMIT(NF_HOOK_OPS_DEV, offsetof(struct nf_hook_ops, dev));
	NVK_EMIT(NF_HOOK_OPS_PRIV, offsetof(struct nf_hook_ops, priv));
	NVK_EMIT(NF_HOOK_OPS_PF, offsetof(struct nf_hook_ops, pf));
	NVK_EMIT(NF_HOOK_OPS_HOOKNUM, offsetof(struct nf_hook_ops, hooknum));
	NVK_EMIT(NF_HOOK_OPS_PRIORITY, offsetof(struct nf_hook_ops, priority));

	NVK_EMIT(FOPS_SIZE, sizeof(struct file_operations));
	NVK_EMIT(FOPS_OWNER, offsetof(struct file_operations, owner));
	NVK_EMIT(FOPS_READ, offsetof(struct file_operations, read));
	NVK_EMIT(FOPS_WRITE, offsetof(struct file_operations, write));
	NVK_EMIT(FOPS_UNLOCKED_IOCTL,
		  offsetof(struct file_operations, unlocked_ioctl));
	NVK_EMIT(FOPS_OPEN, offsetof(struct file_operations, open));
	NVK_EMIT(FOPS_RELEASE, offsetof(struct file_operations, release));
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	NVK_EMIT(FOPS_MMAP_PREPARE, offsetof(struct file_operations, mmap_prepare));
#endif

	NVK_EMIT(PT_REGS_SIZE, sizeof(struct pt_regs));
	NVK_EMIT(PT_REGS_REGS, offsetof(struct pt_regs, regs));
	NVK_EMIT(PT_REGS_SP, offsetof(struct pt_regs, sp));
	NVK_EMIT(PT_REGS_PC, offsetof(struct pt_regs, pc));
	NVK_EMIT(PT_REGS_PSTATE, offsetof(struct pt_regs, pstate));

	NVK_EMIT(NOTIFIER_BLOCK_SIZE, sizeof(struct notifier_block));
	NVK_EMIT(NOTIFIER_BLOCK_CALL,
		  offsetof(struct notifier_block, notifier_call));
	NVK_EMIT(NOTIFIER_BLOCK_NEXT, offsetof(struct notifier_block, next));
	NVK_EMIT(NOTIFIER_BLOCK_PRIORITY,
		  offsetof(struct notifier_block, priority));

	NVK_EMIT(MISCDEVICE_SIZE, sizeof(struct miscdevice));
	NVK_EMIT(MISCDEVICE_MINOR, offsetof(struct miscdevice, minor));
	NVK_EMIT(MISCDEVICE_NAME, offsetof(struct miscdevice, name));
	NVK_EMIT(MISCDEVICE_FOPS, offsetof(struct miscdevice, fops));
	NVK_EMIT(MISCDEVICE_MODE, offsetof(struct miscdevice, mode));

	NVK_EMIT(RESOURCE_SIZE, sizeof(struct resource));
	NVK_EMIT(RESOURCE_START, offsetof(struct resource, start));
	NVK_EMIT(RESOURCE_END, offsetof(struct resource, end));
	NVK_EMIT(RESOURCE_FLAGS, offsetof(struct resource, flags));

	NVK_EMIT(FIRMWARE_SIZE, sizeof(struct firmware));
	NVK_EMIT(FIRMWARE_DATA, offsetof(struct firmware, data));

	NVK_EMIT(SCATTERLIST_SIZE, sizeof(struct scatterlist));
	NVK_EMIT(SCATTERLIST_PAGE_LINK,
		  offsetof(struct scatterlist, page_link));
	NVK_EMIT(SCATTERLIST_OFFSET, offsetof(struct scatterlist, offset));
	NVK_EMIT(SCATTERLIST_LENGTH, offsetof(struct scatterlist, length));
	NVK_EMIT(SG_TABLE_SIZE, sizeof(struct sg_table));

	NVK_EMIT(REGMAP_CONFIG_SIZE, sizeof(struct regmap_config));
	NVK_EMIT(REGMAP_CONFIG_NAME, offsetof(struct regmap_config, name));
	NVK_EMIT(REGMAP_CONFIG_REG_BITS,
		  offsetof(struct regmap_config, reg_bits));
	NVK_EMIT(REGMAP_CONFIG_REG_STRIDE,
		  offsetof(struct regmap_config, reg_stride));
	NVK_EMIT(REGMAP_CONFIG_VAL_BITS,
		  offsetof(struct regmap_config, val_bits));
	NVK_EMIT(REGMAP_CONFIG_FAST_IO,
		  offsetof(struct regmap_config, fast_io));
	NVK_EMIT(REGMAP_CONFIG_MAX_REGISTER,
		  offsetof(struct regmap_config, max_register));
	NVK_EMIT(REGMAP_CONFIG_REG_DEFAULTS,
		  offsetof(struct regmap_config, reg_defaults));
	NVK_EMIT(REGMAP_CONFIG_NUM_REG_DEFAULTS,
		  offsetof(struct regmap_config, num_reg_defaults));

	NVK_EMIT(DEV_PM_OPS_SIZE, sizeof(struct dev_pm_ops));
	NVK_EMIT(DEV_PM_OPS_SUSPEND, offsetof(struct dev_pm_ops, suspend));
	NVK_EMIT(DEV_PM_OPS_RESUME, offsetof(struct dev_pm_ops, resume));
	NVK_EMIT(DEV_PM_OPS_RUNTIME_SUSPEND,
		  offsetof(struct dev_pm_ops, runtime_suspend));
	NVK_EMIT(DEV_PM_OPS_RUNTIME_RESUME,
		  offsetof(struct dev_pm_ops, runtime_resume));

	NVK_EMIT(VMAP_AREA_SIZE, sizeof(struct vmap_area));
	NVK_EMIT(VMAP_AREA_START, offsetof(struct vmap_area, va_start));
}
#else
const unsigned long nvk_gen_layout_placeholder;
#endif
