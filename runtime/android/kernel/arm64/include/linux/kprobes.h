/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Minimal struct kprobe shim — compatible across GKI 5.10–6.18.
 *
 * Only the fields needed by neverc_krt_kprobe_lookup() are laid out
 * explicitly; the rest is an opaque tail blob.  The field offsets
 * have been verified against all supported GKI trees:
 *
 *   hlist_node hlist     16 bytes  (+0)
 *   list_head  list      16 bytes  (+16)
 *   ulong      nmissed    8 bytes  (+32)
 *   opcode_t  *addr       8 bytes  (+40)   <-- we read this
 *   const char*symbol     8 bytes  (+48)   <-- we write this
 *   uint       offset     4 bytes  (+56)
 *   (pad)                 4 bytes  (+60)
 *   pre_handler           8 bytes  (+64)   <-- we write this
 */
#ifndef _NEVERC_KRT_LINUX_KPROBES_H
#define _NEVERC_KRT_LINUX_KPROBES_H

#include <linux/types.h>

/*
 * Max sizeof(struct kprobe) across GKI arm64 5.10–6.18:
 *   5.10  = 136 bytes (0x88) — has fault_handler field
 *   5.15+ = 128 bytes (0x80) — fault_handler removed
 * 0x90 (144) covers all versions with headroom.
 */
#ifndef NEVERC_KRT_KP_SIZE
#define NEVERC_KRT_KP_SIZE 0x90
#endif

struct kprobe {
	unsigned char _head[40];
	void *addr;
	const char *symbol_name;
	unsigned int offset;
	unsigned int _pad;
	int (*pre_handler)(struct kprobe *, void *);
	unsigned char _tail[NEVERC_KRT_KP_SIZE - 72];
};

_Static_assert(__builtin_offsetof(struct kprobe, addr) == 40,
	       "kprobe.addr offset mismatch — update _head size");
_Static_assert(__builtin_offsetof(struct kprobe, symbol_name) == 48,
	       "kprobe.symbol_name offset mismatch");
_Static_assert(__builtin_offsetof(struct kprobe, pre_handler) == 64,
	       "kprobe.pre_handler offset mismatch");
_Static_assert(sizeof(struct kprobe) == NEVERC_KRT_KP_SIZE,
	       "struct kprobe size != NEVERC_KRT_KP_SIZE");

int register_kprobe(struct kprobe *kp);
void unregister_kprobe(struct kprobe *kp);

#endif /* _NEVERC_KRT_LINUX_KPROBES_H */
