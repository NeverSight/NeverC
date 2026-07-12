/* SPDX-License-Identifier: GPL-2.0 */
#include <nvkmod.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/ioctl.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/errno.h>
#include <nvk_mem.h>

#define NEVERC_KRT_CHARDEV_NAME "neverc_krt_chardev"
#define NEVERC_KRT_CHARDEV_PROC_PATH "/proc/" NEVERC_KRT_CHARDEV_NAME
#define NEVERC_KRT_LOG_TAG NEVERC_KRT_CHARDEV_NAME
#include <nvk_log.h>

#define NEVERC_KRT_IOC_MAGIC 'N'

struct neverc_krt_stats {
	u32 reads;
	u32 writes;
	u32 ioctls;
};

#define NEVERC_KRT_IOC_GET_VERSION _IOR(NEVERC_KRT_IOC_MAGIC, 1, u32)
#define NEVERC_KRT_IOC_SET_VALUE   _IOW(NEVERC_KRT_IOC_MAGIC, 2, u32)
#define NEVERC_KRT_IOC_GET_VALUE   _IOR(NEVERC_KRT_IOC_MAGIC, 3, u32)
#define NEVERC_KRT_IOC_GET_STATS   _IOR(NEVERC_KRT_IOC_MAGIC, 4, struct neverc_krt_stats)
#define NEVERC_KRT_IOC_RESET       _IO(NEVERC_KRT_IOC_MAGIC, 5)

#define NEVERC_KRT_CHARDEV_VERSION 0x00010001
#define NEVERC_KRT_BUF_SIZE 4096

static struct {
	char buf[NEVERC_KRT_BUF_SIZE];
	unsigned int len;
	u32 value;
	struct neverc_krt_stats stats;
} dev_state;

static struct mutex dev_lock;

typedef void (*mutex_init_fn)(struct mutex *, const char *, void *);
typedef void (*mutex_lock_fn)(struct mutex *);
typedef void (*mutex_unlock_fn)(struct mutex *);

static mutex_init_fn   fn_mutex_init;
static mutex_lock_fn   fn_mutex_lock;
static mutex_unlock_fn fn_mutex_unlock;
static int             dev_lock_inited;

static void dev_lock_acquire(void)
{
	if (dev_lock_inited && fn_mutex_lock)
		fn_mutex_lock(&dev_lock);
}

static void dev_lock_release(void)
{
	if (dev_lock_inited && fn_mutex_unlock)
		fn_mutex_unlock(&dev_lock);
}

static int neverc_krt_open(struct inode *inode, struct file *filp)
{
	(void)inode;
	(void)filp;
	return 0;
}

static int neverc_krt_release(struct inode *inode, struct file *filp)
{
	(void)inode;
	(void)filp;
	return 0;
}

static ssize_t neverc_krt_read(struct file *filp, char __user *buf, size_t count,
			loff_t *ppos)
{
	static const char greeting[] =
		NEVERC_KRT_CHARDEV_NAME ": hello from kernel!\n";
	char local[NEVERC_KRT_BUF_SIZE];
	unsigned int len;

	(void)filp;

	dev_lock_acquire();
	dev_state.stats.reads++;

	if (dev_state.len > 0) {
		len = dev_state.len;
		for (unsigned int i = 0; i < len; i++)
			local[i] = dev_state.buf[i];
	} else {
		len = sizeof(greeting) - 1;
		for (unsigned int i = 0; i < len; i++)
			local[i] = greeting[i];
	}

	dev_lock_release();

	if ((unsigned long long)*ppos >= len)
		return 0;
	if (count > len - (unsigned int)*ppos)
		count = len - (unsigned int)*ppos;

	if (neverc_krt_mem_write_user(buf, local + *ppos, count))
		return -EFAULT;
	*ppos += count;
	return count;
}

static ssize_t neverc_krt_write(struct file *filp, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	(void)filp;
	(void)ppos;

	if (count > NEVERC_KRT_BUF_SIZE - 1)
		count = NEVERC_KRT_BUF_SIZE - 1;

	dev_lock_acquire();

	if (neverc_krt_mem_read_user(dev_state.buf, buf, count)) {
		dev_lock_release();
		return -EFAULT;
	}
	dev_state.buf[count] = '\0';
	dev_state.len = count;
	dev_state.stats.writes++;

	dev_lock_release();

	neverc_krt_log_dbg("wrote %zu bytes\n", count);
	return count;
}

static long neverc_krt_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	u32 val;
	struct neverc_krt_stats st;

	(void)filp;

	dev_lock_acquire();
	dev_state.stats.ioctls++;

	switch (cmd) {
	case NEVERC_KRT_IOC_GET_VERSION:
		val = NEVERC_KRT_CHARDEV_VERSION;
		dev_lock_release();
		if (neverc_krt_mem_write_user((void __user *)arg, &val, sizeof(val)))
			return -EFAULT;
		return 0;

	case NEVERC_KRT_IOC_SET_VALUE:
		dev_lock_release();
		if (neverc_krt_mem_read_user(&val, (void __user *)arg, sizeof(val)))
			return -EFAULT;
		dev_lock_acquire();
		dev_state.value = val;
		dev_lock_release();
		neverc_krt_log_dbg("SET_VALUE = %u\n", val);
		return 0;

	case NEVERC_KRT_IOC_GET_VALUE:
		val = dev_state.value;
		dev_lock_release();
		if (neverc_krt_mem_write_user((void __user *)arg, &val, sizeof(val)))
			return -EFAULT;
		return 0;

	case NEVERC_KRT_IOC_GET_STATS:
		st = dev_state.stats;
		dev_lock_release();
		if (neverc_krt_mem_write_user((void __user *)arg, &st, sizeof(st)))
			return -EFAULT;
		return 0;

	case NEVERC_KRT_IOC_RESET:
		dev_state.len = 0;
		dev_state.value = 0;
		dev_state.stats.reads = 0;
		dev_state.stats.writes = 0;
		dev_state.stats.ioctls = 0;
		dev_lock_release();
		neverc_krt_log_info("reset\n");
		return 0;

	default:
		dev_lock_release();
		return -ENOTTY;
	}
}

static const struct file_operations neverc_krt_fops = {
	.owner          = (void *)0,
	.open           = neverc_krt_open,
	.release        = neverc_krt_release,
	.read           = neverc_krt_read,
	.write          = neverc_krt_write,
	.unlocked_ioctl = neverc_krt_ioctl,
};

static struct miscdevice neverc_krt_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = NEVERC_KRT_CHARDEV_NAME,
	.fops  = &neverc_krt_fops,
};

typedef struct proc_dir_entry *(*proc_create_seq_private_fn)(
	const char *name, umode_t mode, struct proc_dir_entry *parent,
	const struct seq_operations *ops, unsigned int state_size, void *data);
typedef void (*proc_remove_fn)(struct proc_dir_entry *);

static proc_create_seq_private_fn fn_proc_create_seq;
static proc_remove_fn fn_proc_remove;
static struct proc_dir_entry *proc_entry;

typedef void (*seq_printf_fn)(struct seq_file *, const char *, ...);
static seq_printf_fn fn_seq_printf;

static void *neverc_krt_proc_start(struct seq_file *m, loff_t *pos)
{
	(void)m;
	return (*pos == 0) ? (void *)1 : (void *)0;
}

static void neverc_krt_proc_stop(struct seq_file *m, void *v)
{
	(void)m;
	(void)v;
}

static void *neverc_krt_proc_next(struct seq_file *m, void *v, loff_t *pos)
{
	(void)m;
	(void)v;
	++*pos;
	return (void *)0;
}

static int neverc_krt_proc_show(struct seq_file *m, void *v)
{
	(void)v;

	if (!fn_seq_printf)
		return 0;

	/*
	 * Proc is read-only diagnostics.  Avoid mutex here: if mutex_init was
	 * unavailable at module load, dev_lock_acquire() would spin forever on
	 * an uninitialized lock and trip the SoC watchdog during cat(1).
	 */
	fn_seq_printf(m, NEVERC_KRT_CHARDEV_NAME " status\n");
	fn_seq_printf(m, "  kernel:  %s\n", NEVERC_KRT_KERNEL_STR);
	fn_seq_printf(m, "  version: 0x%08x\n", NEVERC_KRT_CHARDEV_VERSION);
	fn_seq_printf(m, "  value:   %u\n", dev_state.value);
	fn_seq_printf(m, "  buf_len: %u / %u\n", dev_state.len, NEVERC_KRT_BUF_SIZE);
	fn_seq_printf(m, "  reads:   %u\n", dev_state.stats.reads);
	fn_seq_printf(m, "  writes:  %u\n", dev_state.stats.writes);
	fn_seq_printf(m, "  ioctls:  %u\n", dev_state.stats.ioctls);
	return 0;
}

static const struct seq_operations neverc_krt_proc_seq_ops = {
	.start = neverc_krt_proc_start,
	.stop = neverc_krt_proc_stop,
	.next = neverc_krt_proc_next,
	.show = neverc_krt_proc_show,
};

static void setup_proc(void)
{
	fn_proc_create_seq =
		(proc_create_seq_private_fn)NEVERC_KRT_LOOKUP(
			"proc_create_seq_private");
	fn_proc_remove =
		(proc_remove_fn)NEVERC_KRT_LOOKUP("proc_remove");
	fn_seq_printf =
		(seq_printf_fn)NEVERC_KRT_LOOKUP("seq_printf");

	if (!fn_proc_create_seq || !fn_seq_printf) {
		neverc_krt_log_warn("proc seq helpers not found, skipping /proc entry\n");
		return;
	}

	proc_entry = fn_proc_create_seq(NEVERC_KRT_CHARDEV_NAME, 0444, (void *)0,
					&neverc_krt_proc_seq_ops, 0, (void *)0);
	if (proc_entry)
		neverc_krt_log_info(NEVERC_KRT_CHARDEV_PROC_PATH " created\n");
}

typedef int  (*misc_register_fn)(struct miscdevice *);
typedef void (*misc_deregister_fn)(struct miscdevice *);

static misc_register_fn   do_misc_register;
static misc_deregister_fn do_misc_deregister;

static int neverc_krt_chardev_init(void)
{
	int ret;

	ret = NEVERC_KRT_BOOTSTRAP();
	if (ret)
		return ret;

	neverc_krt_mem_init();

	fn_mutex_init = (mutex_init_fn)NEVERC_KRT_LOOKUP("__mutex_init");
	fn_mutex_lock = (mutex_lock_fn)NEVERC_KRT_LOOKUP("mutex_lock");
	fn_mutex_unlock = (mutex_unlock_fn)NEVERC_KRT_LOOKUP("mutex_unlock");

	dev_lock_inited = 0;
	if (fn_mutex_init && fn_mutex_lock && fn_mutex_unlock) {
		fn_mutex_init(&dev_lock, NEVERC_KRT_CHARDEV_NAME, (void *)0);
		dev_lock_inited = 1;
	} else {
		neverc_krt_log_warn("mutex helpers not found; chardev runs unlocked\n");
	}

	do_misc_register =
		(misc_register_fn)NEVERC_KRT_LOOKUP("misc_register");
	do_misc_deregister =
		(misc_deregister_fn)NEVERC_KRT_LOOKUP("misc_deregister");

	if (!do_misc_register || !do_misc_deregister) {
		neverc_krt_log_err("miscdevice helpers not found\n");
		return -1;
	}

	ret = do_misc_register(&neverc_krt_misc);
	if (ret) {
		neverc_krt_log_err("misc_register failed: %d\n", ret);
		return ret;
	}

	setup_proc();

	neverc_krt_log_info("loaded on %s, /dev/" NEVERC_KRT_CHARDEV_NAME
			    " ready\n",
		     NEVERC_KRT_KERNEL_STR);
	return 0;
}

static void neverc_krt_chardev_exit(void)
{
	if (proc_entry && fn_proc_remove)
		fn_proc_remove(proc_entry);
	if (do_misc_deregister)
		do_misc_deregister(&neverc_krt_misc);
	neverc_krt_log_info("unloaded\n");
}

module_init(neverc_krt_chardev_init);
module_exit(neverc_krt_chardev_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("NeverC");
MODULE_DESCRIPTION("NeverC chardev + ioctl + proc demo");

NEVERC_KRT_DEFINE_MODULE(NEVERC_KRT_CHARDEV_NAME);
