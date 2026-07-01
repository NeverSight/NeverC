// SPDX-License-Identifier: GPL-2.0
/*
 * Emit offsetof/size probes for version-sensitive kernel structs.
 * Compile against a prepared GKI tree (gki_defconfig + modules_prepare):
 *
 *   clang --target=aarch64-linux-gnu -fno-lto -nostdlibinc -std=gnu11 \
 *     -D__KERNEL__ -DNVK_GEN_KSRC=1 \
 *     -I$KT/arch/arm64/include -I$O/arch/arm64/include/generated \
 *     -I$KT/include -I$O/include -I$O/include/generated \
 *     ... -S -o - tools/gen_layout_offsets.c | grep "==NVK=="
 */
#ifdef NVK_GEN_KSRC
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/stddef.h>
#include <linux/netfilter.h>

#define NVK_EMIT(name, val)                                                    \
	__asm__ __volatile__("\n.ascii \"==NVK== " #name " %0 ==\"\n"           \
			     :                                                 \
			     : "i"((long)(val)))

void nvk_gen_layout_offsets(void)
{
	NVK_EMIT(PROC_OPS_LSEEK, offsetof(struct proc_ops, proc_lseek));
	NVK_EMIT(PROC_OPS_SIZE, sizeof(struct proc_ops));
	NVK_EMIT(SKB_DATA, offsetof(struct sk_buff, data));
	NVK_EMIT(SKB_HEAD, offsetof(struct sk_buff, head));
	NVK_EMIT(NF_HOOK_OPS_SIZE, sizeof(struct nf_hook_ops));
	NVK_EMIT(FOPS_SIZE, sizeof(struct file_operations));
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	NVK_EMIT(FOPS_MMAP_PREPARE, offsetof(struct file_operations, mmap_prepare));
#endif
}
#else
const unsigned long nvk_gen_layout_placeholder;
#endif
