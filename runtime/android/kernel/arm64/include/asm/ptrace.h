/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_ASM_PTRACE_H
#define _NEVERC_KRT_ASM_PTRACE_H

#include <linux/types.h>

/*
 * ARM64 pt_regs layout — stable core fields across GKI 5.10–6.12.
 *
 * Only regs[0..30] are accessed by the runtime (via X0–X8 for syscall
 * arguments).  Fields after pstate vary slightly across versions:
 *   5.10–5.15: orig_x0 + {syscallno(s32),unused2(u32)} + orig_addr_limit + pmr_save + stackframe[2]
 *   6.1+:      orig_x0 + {syscallno(s32),unused2(u32)} + sdei_ttbr1    + pmr_save + stackframe[2]
 * The name/semantics of the field at offset 288 changed but the size
 * and alignment did not.  sizeof(struct pt_regs) >= 320 on all versions.
 */
struct pt_regs {
	u64 regs[31];   /* x0 – x30, offsets 0–240 */
	u64 sp;         /* offset 248 */
	u64 pc;         /* offset 256 */
	u64 pstate;     /* offset 264 */
	u64 orig_x0;    /* offset 272 */
	s32 syscallno;  /* offset 280 */
	u32 _pad0;      /* offset 284 */
	u64 _reserved;  /* offset 288: orig_addr_limit (5.10/5.15) or sdei_ttbr1 (6.1+) */
	u64 pmr_save;   /* offset 296 */
	u64 stackframe[2]; /* offset 304 */
};

#define ARM_pt_regs_x(r, n)  ((r)->regs[(n)])
#define ARM_pt_regs_sp(r)    ((r)->sp)
#define ARM_pt_regs_pc(r)    ((r)->pc)

#endif /* _NEVERC_KRT_ASM_PTRACE_H */
