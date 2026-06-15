/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NeverC Android kernel chardev + ioctl demo (GKI .ko).
 *
 * Features:
 *   - /dev/nvk_chardev via misc_register (dynamic minor)
 *   - read:  returns kernel greeting or last-written data
 *   - write: stores data into a mutex-protected ring buffer
 *   - ioctl: NVK_IOC_GET_VERSION  → SDK version
 *            NVK_IOC_SET_VALUE    → store a u32
 *            NVK_IOC_GET_VALUE    → retrieve the stored u32
 *            NVK_IOC_GET_STATS   → read/write/ioctl counters
 *            NVK_IOC_RESET       → clear buffer and counters
 *   - /proc/nvk_chardev status entry via proc_create + single_open
 *
 * Build:  make                    (or: make KERNEL=601 etc.)
 * Deploy: adb push nvk_chardev.ko /data/local/tests/
 *         adb shell su -c 'insmod /data/local/tests/nvk_chardev.ko'
 *         adb shell su -c 'cat /dev/nvk_chardev'
 *         adb shell su -c 'echo hello > /dev/nvk_chardev'
 *         adb shell su -c 'cat /proc/nvk_chardev'
 *         adb shell su -c 'rmmod nvk_chardev'
 */
#include <nvkmod.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

/* ---- ioctl definitions ------------------------------------------------ */

#define NVK_IOC_MAGIC 'N'

struct nvk_stats {
	u32 reads;
	u32 writes;
	u32 ioctls;
};

#define NVK_IOC_GET_VERSION _IOR(NVK_IOC_MAGIC, 1, u32)
#define NVK_IOC_SET_VALUE   _IOW(NVK_IOC_MAGIC, 2, u32)
#define NVK_IOC_GET_VALUE   _IOR(NVK_IOC_MAGIC, 3, u32)
#define NVK_IOC_GET_STATS   _IOR(NVK_IOC_MAGIC, 4, struct nvk_stats)
#define NVK_IOC_RESET       _IO(NVK_IOC_MAGIC, 5)

#define NVK_CHARDEV_VERSION 0x00010001  /* 1.0.1 */
#define NVK_BUF_SIZE 4096

/* ---- device state (mutex-protected) ----------------------------------- */

static struct {
	char buf[NVK_BUF_SIZE];
	unsigned int len;
	u32 value;
	struct nvk_stats stats;
} dev_state;

/* Mutex: opaque 8-byte blob, zero-init = unlocked on arm64 GKI. */
static struct { unsigned char __opaque[8]; } dev_lock;

typedef void (*mutex_lock_fn)(void *);
typedef void (*mutex_unlock_fn)(void *);

static mutex_lock_fn   fn_mutex_lock;
static mutex_unlock_fn fn_mutex_unlock;

typedef unsigned long (*copy_from_user_fn)(void *, const void __user *,
					   unsigned long);
typedef unsigned long (*copy_to_user_fn)(void __user *, const void *,
					 unsigned long);
static copy_from_user_fn fn_copy_from_user;
static copy_to_user_fn   fn_copy_to_user;

static void dev_lock_acquire(void)
{
	if (fn_mutex_lock)
		fn_mutex_lock(&dev_lock);
}

static void dev_lock_release(void)
{
	if (fn_mutex_unlock)
		fn_mutex_unlock(&dev_lock);
}

/* ---- file operations -------------------------------------------------- */

static int nvk_open(struct inode *inode, struct file *filp)
{
	(void)inode;
	(void)filp;
	return 0;
}

static int nvk_release(struct inode *inode, struct file *filp)
{
	(void)inode;
	(void)filp;
	return 0;
}

static ssize_t nvk_read(struct file *filp, char __user *buf, size_t count,
			loff_t *ppos)
{
	static const char greeting[] = "nvk_chardev: hello from kernel!\n";
	const char *src;
	unsigned int len;

	(void)filp;

	dev_lock_acquire();
	dev_state.stats.reads++;

	if (dev_state.len > 0) {
		src = dev_state.buf;
		len = dev_state.len;
	} else {
		src = greeting;
		len = sizeof(greeting) - 1;
	}

	if (*ppos >= len) {
		dev_lock_release();
		return 0;
	}
	if (count > len - *ppos)
		count = len - *ppos;

	dev_lock_release();

	if (!fn_copy_to_user ||
	    fn_copy_to_user(buf, src + *ppos, count))
		return -14; /* -EFAULT */
	*ppos += count;
	return count;
}

static ssize_t nvk_write(struct file *filp, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	(void)filp;
	(void)ppos;

	if (count > NVK_BUF_SIZE - 1)
		count = NVK_BUF_SIZE - 1;

	dev_lock_acquire();

	if (!fn_copy_from_user ||
	    fn_copy_from_user(dev_state.buf, buf, count)) {
		dev_lock_release();
		return -14; /* -EFAULT */
	}
	dev_state.buf[count] = '\0';
	dev_state.len = count;
	dev_state.stats.writes++;

	dev_lock_release();

	pr_info("nvk_chardev: wrote %zu bytes\n", count);
	return count;
}

static long nvk_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	u32 val;
	struct nvk_stats st;

	(void)filp;

	dev_lock_acquire();
	dev_state.stats.ioctls++;

	switch (cmd) {
	case NVK_IOC_GET_VERSION:
		val = NVK_CHARDEV_VERSION;
		dev_lock_release();
		if (!fn_copy_to_user ||
		    fn_copy_to_user((void __user *)arg, &val, sizeof(val)))
			return -14;
		return 0;

	case NVK_IOC_SET_VALUE:
		dev_lock_release();
		if (!fn_copy_from_user ||
		    fn_copy_from_user(&val, (void __user *)arg, sizeof(val)))
			return -14;
		dev_lock_acquire();
		dev_state.value = val;
		dev_lock_release();
		pr_info("nvk_chardev: SET_VALUE = %u\n", val);
		return 0;

	case NVK_IOC_GET_VALUE:
		val = dev_state.value;
		dev_lock_release();
		if (!fn_copy_to_user ||
		    fn_copy_to_user((void __user *)arg, &val, sizeof(val)))
			return -14;
		return 0;

	case NVK_IOC_GET_STATS:
		st = dev_state.stats;
		dev_lock_release();
		if (!fn_copy_to_user ||
		    fn_copy_to_user((void __user *)arg, &st, sizeof(st)))
			return -14;
		return 0;

	case NVK_IOC_RESET:
		dev_state.len = 0;
		dev_state.value = 0;
		dev_state.stats.reads = 0;
		dev_state.stats.writes = 0;
		dev_state.stats.ioctls = 0;
		dev_lock_release();
		pr_info("nvk_chardev: reset\n");
		return 0;

	default:
		dev_lock_release();
		return -25; /* -ENOTTY */
	}
}

static const struct file_operations nvk_fops = {
	.owner          = (void *)0,
	.open           = nvk_open,
	.release        = nvk_release,
	.read           = nvk_read,
	.write          = nvk_write,
	.unlocked_ioctl = nvk_ioctl,
};

static struct miscdevice nvk_misc = {
	.minor = 255, /* MISC_DYNAMIC_MINOR */
	.name  = "nvk_chardev",
	.fops  = &nvk_fops,
};

/* ---- /proc/nvk_chardev status ----------------------------------------- */

typedef struct proc_dir_entry *(*proc_create_fn)(
	const char *name, umode_t mode, struct proc_dir_entry *parent,
	const struct proc_ops *ops);
typedef void (*proc_remove_fn)(struct proc_dir_entry *);

static proc_create_fn fn_proc_create;
static proc_remove_fn fn_proc_remove;
static struct proc_dir_entry *proc_entry;

typedef void (*seq_printf_fn)(struct seq_file *, const char *, ...);
static seq_printf_fn fn_seq_printf;

static int nvk_proc_show(struct seq_file *m, void *v)
{
	(void)v;

	if (!fn_seq_printf)
		return 0;

	dev_lock_acquire();

	fn_seq_printf(m, "nvk_chardev status\n");
	fn_seq_printf(m, "  kernel:  %s\n", NVK_KERNEL_STR);
	fn_seq_printf(m, "  version: 0x%08x\n", NVK_CHARDEV_VERSION);
	fn_seq_printf(m, "  value:   %u\n", dev_state.value);
	fn_seq_printf(m, "  buf_len: %u / %u\n", dev_state.len, NVK_BUF_SIZE);
	fn_seq_printf(m, "  reads:   %u\n", dev_state.stats.reads);
	fn_seq_printf(m, "  writes:  %u\n", dev_state.stats.writes);
	fn_seq_printf(m, "  ioctls:  %u\n", dev_state.stats.ioctls);

	dev_lock_release();
	return 0;
}

typedef int (*single_open_fn)(struct file *, int (*)(struct seq_file *, void *),
			      void *);
static single_open_fn fn_single_open;

static int nvk_proc_open(struct inode *inode, struct file *file)
{
	(void)inode;
	if (fn_single_open)
		return fn_single_open(file, nvk_proc_show, (void *)0);
	return -1;
}

typedef ssize_t (*seq_read_fn)(struct file *, char __user *, size_t, loff_t *);
typedef loff_t (*seq_lseek_fn)(struct file *, loff_t, int);
typedef int (*single_release_fn)(struct inode *, struct file *);

static seq_read_fn       fn_seq_read;
static seq_lseek_fn      fn_seq_lseek;
static single_release_fn fn_single_release;

static struct proc_ops nvk_proc_ops;

static void setup_proc(void)
{
	fn_proc_create =
		(proc_create_fn)NVK_LOOKUP("proc_create");
	fn_proc_remove =
		(proc_remove_fn)NVK_LOOKUP("proc_remove");
	fn_seq_printf =
		(seq_printf_fn)NVK_LOOKUP("seq_printf");
	fn_single_open =
		(single_open_fn)NVK_LOOKUP("single_open");
	fn_seq_read =
		(seq_read_fn)NVK_LOOKUP("seq_read");
	fn_seq_lseek =
		(seq_lseek_fn)NVK_LOOKUP("seq_lseek");
	fn_single_release =
		(single_release_fn)NVK_LOOKUP("single_release");

	if (!fn_proc_create || !fn_single_open || !fn_seq_read) {
		pr_info("nvk_chardev: proc helpers not found, "
			"skipping /proc entry\n");
		return;
	}

	nvk_proc_ops.proc_open = nvk_proc_open;
	nvk_proc_ops.proc_read = fn_seq_read;
	nvk_proc_ops.proc_lseek = fn_seq_lseek;
	nvk_proc_ops.proc_release = fn_single_release;

	proc_entry = fn_proc_create("nvk_chardev", 0444,
				    (void *)0, &nvk_proc_ops);
	if (proc_entry)
		pr_info("nvk_chardev: /proc/nvk_chardev created\n");
}

/* ---- module init / exit ----------------------------------------------- */

typedef int  (*misc_register_fn)(struct miscdevice *);
typedef void (*misc_deregister_fn)(struct miscdevice *);

static misc_register_fn   do_misc_register;
static misc_deregister_fn do_misc_deregister;

static int nvk_chardev_init(void)
{
	int ret;

	ret = NVK_BOOTSTRAP();
	if (ret)
		return ret;

	fn_mutex_lock = (mutex_lock_fn)NVK_LOOKUP("mutex_lock");
	fn_mutex_unlock = (mutex_unlock_fn)NVK_LOOKUP("mutex_unlock");
	fn_copy_from_user =
		(copy_from_user_fn)NVK_LOOKUP("_copy_from_user");
	if (!fn_copy_from_user)
		fn_copy_from_user =
			(copy_from_user_fn)NVK_LOOKUP("raw_copy_from_user");
	fn_copy_to_user =
		(copy_to_user_fn)NVK_LOOKUP("_copy_to_user");
	if (!fn_copy_to_user)
		fn_copy_to_user =
			(copy_to_user_fn)NVK_LOOKUP("raw_copy_to_user");

	/* Resolve misc device helpers. */
	do_misc_register =
		(misc_register_fn)NVK_LOOKUP("misc_register");
	do_misc_deregister =
		(misc_deregister_fn)NVK_LOOKUP("misc_deregister");

	if (!do_misc_register || !do_misc_deregister) {
		pr_info("nvk_chardev: misc_register not found\n");
		return -1;
	}

	ret = do_misc_register(&nvk_misc);
	if (ret) {
		pr_info("nvk_chardev: misc_register failed: %d\n", ret);
		return ret;
	}

	setup_proc();

	pr_info("nvk_chardev: loaded on %s, /dev/nvk_chardev ready\n",
		NVK_KERNEL_STR);
	return 0;
}

static void nvk_chardev_exit(void)
{
	if (proc_entry && fn_proc_remove)
		fn_proc_remove(proc_entry);
	if (do_misc_deregister)
		do_misc_deregister(&nvk_misc);
	pr_info("nvk_chardev: unloaded\n");
}

module_init(nvk_chardev_init);
module_exit(nvk_chardev_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("NeverC");
MODULE_DESCRIPTION("NeverC Android kernel chardev + ioctl + proc demo");

NVK_DEFINE_MODULE("nvk_chardev");
