/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_ASM_PTRACE_H
#define _NVK_ASM_PTRACE_H

#include <linux/types.h>

struct pt_regs {
	u64 regs[31];   /* x0 - x30 */
	u64 sp;
	u64 pc;
	u64 pstate;
	u64 orig_x0;
	u64 syscallno;
	u64 orig_addr_limit;
	u64 pmr_save;
	u64 stackframe[2];
};

#define ARM_pt_regs_x(r, n)  ((r)->regs[(n)])
#define ARM_pt_regs_sp(r)    ((r)->sp)
#define ARM_pt_regs_pc(r)    ((r)->pc)

#endif /* _NVK_ASM_PTRACE_H */
