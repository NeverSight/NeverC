// SPDX-License-Identifier: GPL-2.0
#ifdef NVK_GEN_KSRC
#include <linux/fs.h>
#include <linux/stddef.h>

#define NVK_EMIT(name, val)                                                    \
	__asm__ __volatile__("\n.ascii \"==NVK== " #name " %0 ==\"\n"           \
			     :                                                 \
			     : "i"((long)(val)))

void nvk_gen_fops_offsets(void)
{
	NVK_EMIT(FOPS_OWNER, offsetof(struct file_operations, owner));
	NVK_EMIT(FOPS_READ, offsetof(struct file_operations, read));
	NVK_EMIT(FOPS_WRITE, offsetof(struct file_operations, write));
	NVK_EMIT(FOPS_UNLOCKED_IOCTL,
		 offsetof(struct file_operations, unlocked_ioctl));
	NVK_EMIT(FOPS_OPEN, offsetof(struct file_operations, open));
	NVK_EMIT(FOPS_RELEASE, offsetof(struct file_operations, release));
	NVK_EMIT(FOPS_SIZE, sizeof(struct file_operations));
}
#else
const unsigned long nvk_gen_fops_placeholder;
#endif
