/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_ASM_PTRACE_H
#define _NEVERC_KRT_ASM_PTRACE_H

#include <linux/types.h>

/*
 * ARM64 pt_regs layout — stable core fields across GKI 5.10–6.18.
 *
 * Only regs[0..30], sp, pc, pstate, orig_x0, and syscallno are
 * accessed by the runtime.  Fields after syscallno vary:
 *
 *   5.10:       {syscallno(s32),unused(s32)} orig_addr_limit(u64) pmr_save(u64) stackframe[2]
 *   5.15–6.12:  {syscallno(s32),unused(s32)} sdei_ttbr1(u64)     pmr_save(u64) stackframe[2]
 *   6.18:       {syscallno(s32),pmr(u32)}    sdei_ttbr1(u64)     stackframe{fp,pc,type}
 *
 * PT_REGS_SIZE is 320 on all GKI versions (without lockdep/exit_rcu
 * debug fields that may follow).  The tail fields are defined as
 * opaque storage — runtime code MUST NOT access them by name.
 */
struct pt_regs {
	u64 regs[31];   /* x0 – x30, offsets 0–240 */
	u64 sp;         /* offset 248 */
	u64 pc;         /* offset 256 */
	u64 pstate;     /* offset 264 */
	u64 orig_x0;    /* offset 272 */
	s32 syscallno;  /* offset 280 */
	u8  _tail[36];  /* offsets 284–319: version-dependent, do not access */
};

#define ARM_pt_regs_x(r, n)  ((r)->regs[(n)])
#define ARM_pt_regs_sp(r)    ((r)->sp)
#define ARM_pt_regs_pc(r)    ((r)->pc)

#endif /* _NEVERC_KRT_ASM_PTRACE_H */
