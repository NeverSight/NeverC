/* SPDX-License-Identifier: GPL-2.0 */
/* neverc_krt_hook.h - NeverC arm64 inline-hook engine. */
#ifndef NEVERC_KRT_HOOK_H
#define NEVERC_KRT_HOOK_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <linux/string.h>
#include <nvk_rt.h>
#include <nvk_mem.h>


typedef struct {
	u64 regs[31];       /* X0 - X30                          */
	u64 fpcr;           /* saved FPCR                         */
	u64 nzcv;           /* saved NZCV flags                   */
	u64 force_jump;     /* if nonzero, redirect execution     */
	u64 fpsr;           /* saved FPSR                         */
	u64 _pad;           /* align to 16                        */
} neverc_krt_reg_ctx;

typedef struct {
	u64 lo, hi;
} neverc_krt_fp128;

#define NEVERC_KRT_CTX_X(ctx, n) ((ctx)->regs[n])
#define NEVERC_KRT_CTX_X0(ctx)   ((ctx)->regs[0])
#define NEVERC_KRT_CTX_X1(ctx)   ((ctx)->regs[1])
#define NEVERC_KRT_CTX_X2(ctx)   ((ctx)->regs[2])
#define NEVERC_KRT_CTX_X3(ctx)   ((ctx)->regs[3])
#define NEVERC_KRT_CTX_X4(ctx)   ((ctx)->regs[4])
#define NEVERC_KRT_CTX_X5(ctx)   ((ctx)->regs[5])
#define NEVERC_KRT_CTX_X6(ctx)   ((ctx)->regs[6])
#define NEVERC_KRT_CTX_X7(ctx)   ((ctx)->regs[7])
#define NEVERC_KRT_CTX_X8(ctx)   ((ctx)->regs[8])
#define NEVERC_KRT_CTX_LR(ctx)   ((ctx)->regs[30])
#define NEVERC_KRT_CTX_NZCV(ctx) ((ctx)->nzcv)
#define NEVERC_KRT_CTX_RET(ctx)  NEVERC_KRT_CTX_X0(ctx)

#define NEVERC_KRT_CTX_ARG(ctx, n) ((ctx)->regs[n])
#define NEVERC_KRT_CTX_SYSCALL_NR(ctx) NEVERC_KRT_CTX_X8(ctx)

#define NEVERC_KRT_CTX_FORCE_JUMP(ctx, addr)                   \
	do { (ctx)->force_jump = (u64)(unsigned long)(addr); } while (0)

/*
 * Skip the original function entirely; return ret_val to the caller.
 * Works by redirecting execution to the saved LR (X30).
 */
#define NEVERC_KRT_CTX_SKIP(ctx, ret_val) do {                              \
	(ctx)->regs[0] = (u64)(unsigned long)(ret_val);              \
	(ctx)->force_jump = (ctx)->regs[30];                         \
} while (0)

/* Skip the original function (void return). */
#define NEVERC_KRT_CTX_SKIP_VOID(ctx)                                       \
	do { (ctx)->force_jump = (ctx)->regs[30]; } while (0)

/* Redirect execution to a different function instead of the original. */
#define NEVERC_KRT_CTX_REDIRECT(ctx, fn_addr)                               \
	NEVERC_KRT_CTX_FORCE_JUMP(ctx, fn_addr)

/* Set argument N (X0..X7 for standard AAPCS64 call convention). */
#define NEVERC_KRT_CTX_SET_ARG(ctx, n, val)                                 \
	do { (ctx)->regs[n] = (u64)(unsigned long)(val); } while (0)

/*
 * Cast a neverc_krt_hook_ctx's trampoline to a callable original-function pointer.
 * Usage:  faccessat_fn orig = NEVERC_KRT_CTX_ORIG_FN(&my_ctx, faccessat_fn);
 */
#define NEVERC_KRT_CTX_ORIG_FN(h, fn_type) ((fn_type)(h)->tramp_code)

typedef struct {
	neverc_krt_fp128 q[32];    /* Q0 - Q31 (128-bit each)            */
} neverc_krt_fp_state;

#define NEVERC_KRT_SAVE_FP(st)                                                  \
	__asm__ __volatile__(                                            \
	    "stp q0,  q1,  [%0, #0]   \n"                               \
	    "stp q2,  q3,  [%0, #32]  \n"                               \
	    "stp q4,  q5,  [%0, #64]  \n"                               \
	    "stp q6,  q7,  [%0, #96]  \n"                               \
	    "stp q8,  q9,  [%0, #128] \n"                               \
	    "stp q10, q11, [%0, #160] \n"                               \
	    "stp q12, q13, [%0, #192] \n"                               \
	    "stp q14, q15, [%0, #224] \n"                               \
	    "stp q16, q17, [%0, #256] \n"                               \
	    "stp q18, q19, [%0, #288] \n"                               \
	    "stp q20, q21, [%0, #320] \n"                               \
	    "stp q22, q23, [%0, #352] \n"                               \
	    "stp q24, q25, [%0, #384] \n"                               \
	    "stp q26, q27, [%0, #416] \n"                               \
	    "stp q28, q29, [%0, #448] \n"                               \
	    "stp q30, q31, [%0, #480] \n"                               \
	    : : "r"(st) : "memory")

#define NEVERC_KRT_RESTORE_FP(st)                                               \
	__asm__ __volatile__(                                            \
	    "ldp q0,  q1,  [%0, #0]   \n"                               \
	    "ldp q2,  q3,  [%0, #32]  \n"                               \
	    "ldp q4,  q5,  [%0, #64]  \n"                               \
	    "ldp q6,  q7,  [%0, #96]  \n"                               \
	    "ldp q8,  q9,  [%0, #128] \n"                               \
	    "ldp q10, q11, [%0, #160] \n"                               \
	    "ldp q12, q13, [%0, #192] \n"                               \
	    "ldp q14, q15, [%0, #224] \n"                               \
	    "ldp q16, q17, [%0, #256] \n"                               \
	    "ldp q18, q19, [%0, #288] \n"                               \
	    "ldp q20, q21, [%0, #320] \n"                               \
	    "ldp q22, q23, [%0, #352] \n"                               \
	    "ldp q24, q25, [%0, #384] \n"                               \
	    "ldp q26, q27, [%0, #416] \n"                               \
	    "ldp q28, q29, [%0, #448] \n"                               \
	    "ldp q30, q31, [%0, #480] \n"                               \
	    : : "r"(st) : "memory")


#define NEVERC_KRT_A64_NOP       0xD503201FU
#define NEVERC_KRT_A64_BTI_C     0xD503245FU
#define NEVERC_KRT_A64_BTI_JC    0xD50324DFU
#define NEVERC_KRT_A64_PACIASP   0xD503233FU
#define NEVERC_KRT_A64_PACIBSP   0xD503237FU

#define NEVERC_KRT_A64_RET_X16   0xD65F0200U   /* RET X16 */
#define NEVERC_KRT_A64_RET_X17   0xD65F0220U   /* RET X17 */

static __always_inline int neverc_krt_a64_is_bti(u32 i)
{ return i == 0xD503245FU || i == 0xD503249FU || i == 0xD50324DFU; }

static __always_inline int neverc_krt_a64_is_pac(u32 i)
{
	return i == NEVERC_KRT_A64_PACIASP || i == NEVERC_KRT_A64_PACIBSP
	    || i == 0xD50323BFU  /* AUTIASP */
	    || i == 0xD50323FFU; /* AUTIBSP */
}

static __always_inline int neverc_krt_a64_is_pac_sign(u32 i)
{
	return i == NEVERC_KRT_A64_PACIASP || i == NEVERC_KRT_A64_PACIBSP;
}

static __always_inline int neverc_krt_a64_is_pac_auth(u32 i)
{
	return i == 0xD50323BFU || i == 0xD50323FFU;
}


static __always_inline u32 neverc_krt_a64_movz(int rd, u16 imm, int hw)
{ return 0xD2800000U | ((u32)hw << 21) | ((u32)imm << 5) | rd; }

static __always_inline u32 neverc_krt_a64_movk(int rd, u16 imm, int hw)
{ return 0xF2800000U | ((u32)hw << 21) | ((u32)imm << 5) | rd; }

int neverc_krt_a64_gen_mov64(u32 *out, int rd, u64 addr);


static __always_inline u32 neverc_krt_a64_gen_b(long off)
{ return 0x14000000U | (((u32)(off >> 2)) & 0x03FFFFFFU); }

static __always_inline int neverc_krt_a64_b_in_range(long off)
{ return off >= -0x8000000L && off < 0x8000000L; }

static __always_inline long neverc_krt_sext(long v, int bits)
{ long m = 1L << (bits - 1); return (v ^ m) - m; }


enum neverc_krt_pcrel {
	NEVERC_KRT_PC_NONE = 0,
	NEVERC_KRT_PC_ADRP, NEVERC_KRT_PC_ADR,
	NEVERC_KRT_PC_B, NEVERC_KRT_PC_BL,
	NEVERC_KRT_PC_BCOND, NEVERC_KRT_PC_CBZ, NEVERC_KRT_PC_TBZ,
	NEVERC_KRT_PC_LDR_LIT,
	NEVERC_KRT_PC_LDRSW_LIT,
	NEVERC_KRT_PC_PRFM_LIT,
};

static __always_inline enum neverc_krt_pcrel neverc_krt_a64_classify(u32 i)
{
	if ((i & 0x1F000000) == 0x10000000)
		return (i & 0x80000000) ? NEVERC_KRT_PC_ADRP : NEVERC_KRT_PC_ADR;
	if ((i & 0xFC000000) == 0x14000000) return NEVERC_KRT_PC_B;
	if ((i & 0xFC000000) == 0x94000000) return NEVERC_KRT_PC_BL;
	if ((i & 0xFF000010) == 0x54000000) return NEVERC_KRT_PC_BCOND;
	if ((i & 0x7E000000) == 0x34000000) return NEVERC_KRT_PC_CBZ;
	if ((i & 0x7E000000) == 0x36000000) return NEVERC_KRT_PC_TBZ;
	if ((i & 0xFF000000) == 0x98000000) return NEVERC_KRT_PC_LDRSW_LIT;
	if ((i & 0xFF000000) == 0xD8000000) return NEVERC_KRT_PC_PRFM_LIT;
	if ((i & 0x3B000000) == 0x18000000) return NEVERC_KRT_PC_LDR_LIT;
	return NEVERC_KRT_PC_NONE;
}


int neverc_krt_a64_relocate_abs(u32 insn, unsigned long old_pc, u32 *out);



enum neverc_krt_hook_err {
	NEVERC_KRT_HOOK_OK         =  0,
	NEVERC_KRT_HOOK_E_NOINIT   = -1,
	NEVERC_KRT_HOOK_E_SHORT    = -2,
	NEVERC_KRT_HOOK_E_RELOC    = -3,
	NEVERC_KRT_HOOK_E_ALLOC    = -4,
	NEVERC_KRT_HOOK_E_PATCH    = -5,
	NEVERC_KRT_HOOK_E_CONFLICT = -6,
};

static __always_inline int neverc_krt_hook_strerror(int err, char *buf, int sz)
{
	if (!buf || sz < 4) return -1;
	char c0 = 'E', c1 = '0' + ((-err) / 10), c2 = '0' + ((-err) % 10);
	if (err == 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }
	buf[0] = c0; buf[1] = c1; buf[2] = c2; buf[3] = '\0';
	return 3;
}

#define NEVERC_KRT_HOOK_MAX_PATCH   6   /* BTI + PAC + LDR + BR + .quad(2) */
#define NEVERC_KRT_HOOK_TRAMP_CAP  64
#define NEVERC_KRT_HOOK_STUB_CAP  128

struct neverc_krt_hook {
	void       *target;
	void       *replace;
	u32        *trampoline;
	u32         orig_insns[NEVERC_KRT_HOOK_MAX_PATCH];
	int         patch_count;
	int         active;
	volatile int enabled;
	int         short_b;
	volatile u64 hit_count;
	volatile unsigned long guard;
};

#define NEVERC_KRT_HOOK_COUNT(h) \
	__atomic_fetch_add(&(h)->hit_count, 1, __ATOMIC_RELAXED)

static __always_inline u64 neverc_krt_hook_hits(struct neverc_krt_hook *h)
{ return __atomic_load_n(&h->hit_count, __ATOMIC_RELAXED); }

static __always_inline void neverc_krt_hook_reset_stats(struct neverc_krt_hook *h)
{ __atomic_store_n(&h->hit_count, 0, __ATOMIC_RELAXED); }

static __always_inline int neverc_krt_in_irq_context(void)
{
	/*
	 * ARM64 tracks interrupt context in preempt_count (per-task).
	 * thread_info layout with CONFIG_ARM64_SW_TTBR0_PAN=y (all GKI):
	 *   5.10:  flags(8) + addr_limit(8) + ttbr0(8) → preempt at 24
	 *   5.15+: flags(8) + ttbr0(8) → preempt at 16 (addr_limit removed)
	 * preempt.count (u32, little-endian) layout:
	 *   [19:16]=HARDIRQ, [15:8]=SOFTIRQ, [7:0]=PREEMPT
	 * in_interrupt() = (count & 0x000FFF00) != 0
	 */
	unsigned long task;
	u32 count;
	__asm__ __volatile__("mrs %0, sp_el0" : "=r"(task));
	int kv = __atomic_load_n(&_neverc_krt_kernel_ver, __ATOMIC_ACQUIRE);
	if (!kv)
		return 0;
	unsigned long off = (kv <= 510) ? 24 : 16;
	if (neverc_krt_mem_read(&count, (void *)(task + off), 4))
		return 0;
	return (count & 0x000FFF00U) != 0;
}

static __always_inline int neverc_krt_irq_disabled(void)
{
	unsigned long daif;
	__asm__ __volatile__("mrs %0, daif" : "=r"(daif));
	return (daif & (1UL << 7)) != 0;
}

static __always_inline int neverc_krt_hook_enter(struct neverc_krt_hook *h)
{
	unsigned long task;
	__asm__ __volatile__("mrs %0, sp_el0" : "=r"(task));
	unsigned long prev = __atomic_exchange_n(&h->guard, task,
						 __ATOMIC_ACQUIRE);
	if (prev == task)
		return 0;
	NEVERC_KRT_HOOK_COUNT(h);
	return 1;
}

static __always_inline void neverc_krt_hook_leave(struct neverc_krt_hook *h)
{
	__atomic_store_n(&h->guard, 0, __ATOMIC_RELEASE);
}

static __always_inline int neverc_krt_hook_enter_safe(struct neverc_krt_hook *h)
{
	if (!READ_ONCE(h->enabled))
		return 0;
	return neverc_krt_hook_enter(h);
}

typedef void *(*neverc_krt_modalloc_fn)(unsigned long);
typedef void *(*neverc_krt_execmem_alloc_fn)(int type, unsigned long size);
typedef void  (*neverc_krt_modfree_fn)(void *);
typedef void  (*neverc_krt_flushic_fn)(unsigned long, unsigned long);
typedef int   (*neverc_krt_patchtext_fn)(void **, u32 *, int);
typedef int   (*neverc_krt_patchtns_fn)(void *, u32);

typedef void (*neverc_krt_syncrcu_fn)(void);
typedef void (*neverc_krt_msleep_fn)(unsigned int);

NEVERC_KRT_RT_VAR neverc_krt_modalloc_fn       _neverc_krt_modalloc;
NEVERC_KRT_RT_VAR neverc_krt_execmem_alloc_fn  _neverc_krt_execmem_alloc;
NEVERC_KRT_RT_VAR neverc_krt_modfree_fn        _neverc_krt_modfree;
NEVERC_KRT_RT_VAR neverc_krt_flushic_fn        _neverc_krt_flushic;
NEVERC_KRT_RT_VAR neverc_krt_patchtext_fn      _neverc_krt_patchtext;
NEVERC_KRT_RT_VAR neverc_krt_patchtns_fn       _neverc_krt_patchtns;
NEVERC_KRT_RT_VAR neverc_krt_syncrcu_fn        _neverc_krt_syncrcu;
NEVERC_KRT_RT_VAR neverc_krt_msleep_fn         _neverc_krt_msleep;
NEVERC_KRT_RT_VAR int _neverc_krt_inited;

typedef int (*neverc_krt_ksize_fn)(unsigned long addr, unsigned long *sz,
			    unsigned long *off);
NEVERC_KRT_RT_VAR neverc_krt_ksize_fn _neverc_krt_ksize;

void *_neverc_krt_alloc_exec(unsigned long size);


#define _NEVERC_KRT_POOL_MIN_PAGE 4096
#define _NEVERC_KRT_POOL_ALIGN    16
#define _NEVERC_KRT_POOL_MAX      32

static __always_inline int _neverc_krt_pool_page_size(void)
{
	unsigned long tcr;
	__asm__ __volatile__("mrs %0, tcr_el1" : "=r"(tcr));
	u32 tg1 = (tcr >> 30) & 3;
	if (tg1 == 1) return 16384;
	if (tg1 == 2) return 65536;
	return 4096;
}

NEVERC_KRT_RT_VAR volatile int _neverc_krt_pool_lock;
NEVERC_KRT_RT_VAR unsigned long _neverc_krt_pool_irqflags;

static __always_inline unsigned long _neverc_krt_irq_save(void)
{
	unsigned long flags;
	__asm__ __volatile__("mrs %0, daif\n"
			     "msr daifset, #3\n"
			     : "=r"(flags) :: "memory");
	return flags;
}

static __always_inline void _neverc_krt_irq_restore(unsigned long flags)
{
	__asm__ __volatile__("msr daif, %0\n" :: "r"(flags) : "memory");
}

static __always_inline void _neverc_krt_spin_lock(volatile int *lock)
{
	while (__atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE))
		__asm__ __volatile__("wfe" ::: "memory");
}

static __always_inline void _neverc_krt_spin_unlock(volatile int *lock)
{
	__atomic_store_n(lock, 0, __ATOMIC_RELEASE);
	__asm__ __volatile__("sev" ::: "memory");
}

static __always_inline unsigned long _neverc_krt_spin_lock_irqsave(volatile int *lock)
{
	unsigned long flags = _neverc_krt_irq_save();
	_neverc_krt_spin_lock(lock);
	return flags;
}

static __always_inline void _neverc_krt_spin_unlock_irqrestore(volatile int *lock,
							unsigned long flags)
{
	_neverc_krt_spin_unlock(lock);
	_neverc_krt_irq_restore(flags);
}

static __always_inline unsigned long _neverc_krt_clear_tags(unsigned long addr)
{
	return addr & ~(0xFFUL << 56);
}

static __always_inline int _neverc_krt_is_kern_ptr(unsigned long addr)
{
	return _neverc_krt_clear_tags(addr) >= 0xFFFF000000000000UL;
}

static __always_inline void _neverc_krt_dcache_clean(unsigned long addr,
					      unsigned long end)
{
	unsigned long line;
	for (line = addr & ~63UL; line < end; line += 64)
		__asm__ __volatile__("dc cvau, %0" :: "r"(line) : "memory");
	__asm__ __volatile__("dsb ish" ::: "memory");
}

static __always_inline void _neverc_krt_icache_inval(unsigned long addr,
					      unsigned long end)
{
	unsigned long line;
	for (line = addr & ~63UL; line < end; line += 64)
		__asm__ __volatile__("ic ivau, %0" :: "r"(line) : "memory");
	__asm__ __volatile__("dsb ish" ::: "memory");
	__asm__ __volatile__("isb" ::: "memory");
}

#ifndef _NEVERC_KRT_POOL_MAGIC
#  if __has_builtin(__builtin_neverc_random_u64)
#    define _NEVERC_KRT_POOL_MAGIC ((u32)__builtin_neverc_random_u64())
#  elif defined(NEVERC_KRT_CACHE_SEED)
#    define _NEVERC_KRT_POOL_MAGIC ((u32)(NEVERC_KRT_CACHE_SEED))
#  else
#    define _NEVERC_KRT_POOL_MAGIC 0x4E564B50U
#  endif
#endif

struct _neverc_krt_pool_page {
	u32    *base;
	int     used;
	int     refcnt;
	u32     magic;
};

NEVERC_KRT_RT_VAR struct _neverc_krt_pool_page _neverc_krt_pool[_NEVERC_KRT_POOL_MAX];
NEVERC_KRT_RT_VAR int _neverc_krt_pool_count;
NEVERC_KRT_RT_VAR volatile u64 _neverc_krt_pool_alloc_total;
NEVERC_KRT_RT_VAR volatile u64 _neverc_krt_pool_alloc_bytes;

NEVERC_KRT_RT_VAR int _neverc_krt_pool_pgsz;

NEVERC_KRT_RT_VAR volatile u64 _neverc_krt_pool_alloc_fail;

u32 *_neverc_krt_pool_alloc(int bytes);


void _neverc_krt_pool_free(u32 *ptr);


NEVERC_KRT_RT_VAR volatile u64 _neverc_krt_hook_install_cnt;
NEVERC_KRT_RT_VAR volatile u64 _neverc_krt_hook_remove_cnt;

int neverc_krt_hook_init(void);


unsigned long _neverc_krt_fn_size(void *addr);


#define NEVERC_KRT_A64_BRK_KPROBE 0xD4200080U

static __always_inline unsigned long neverc_krt_strip_pac(unsigned long addr)
{
	unsigned long tcr, va_bits, mask;
	__asm__ __volatile__("mrs %0, tcr_el1" : "=r"(tcr));
	va_bits = 64 - ((tcr >> 16) & 0x3FUL);
	mask = (1UL << va_bits) - 1;

	int is_kernel = (addr >> 63) & 1;
	addr &= mask;
	if (is_kernel)
		addr |= ~mask;
	return addr;
}

static __always_inline int neverc_krt_a64_is_stp_fp_lr(u32 insn)
{
	return (insn & 0xFFC07FFF) == 0xA9807BFD;
}

static __always_inline int neverc_krt_a64_is_frame_setup(u32 insn)
{
	if ((insn & 0x7FE0FFE0) == 0x2A0003E0) return 1; /* MOV Wd, Wn */
	if ((insn & 0xFFE0FFE0) == 0xAA0003E0) return 1; /* MOV Xd, Xn */
	return 0;
}

static __always_inline int neverc_krt_a64_is_scs_push(u32 insn)
{
	/* str x30, [x18], #8 */
	if (insn == 0xF800841EU) return 1;
	/* str x30, [x18, #0]! (alternative) */
	if (insn == 0xF81F0A5EU) return 1;
	/* stur x30, [x18] (unsigned offset) */
	if (insn == 0xF900025EU) return 1;
	return 0;
}

static __always_inline int neverc_krt_a64_is_hook_patch(u32 insn)
{
	if (insn == NEVERC_KRT_A64_BRK_KPROBE) return 1;
	if (insn == 0x58000050U) return 1;  /* LDR X16, [PC+8] */
	return 0;
}

static __always_inline int neverc_krt_a64_is_kcfi_tag(u32 *addr)
{
	u32 insn;
	if (neverc_krt_mem_read(&insn, (void *)((unsigned long)addr - 4), 4))
		return 0;
	if (insn == 0 || insn == NEVERC_KRT_A64_NOP)
		return 0;
	if ((insn & 0xFFE0001FU) == 0xD4A00000U)
		return 1;
	return 0;
}

#define NEVERC_KRT_A64_FTRACE_NOP  0xD503201FU
#define NEVERC_KRT_A64_BRK_FTRACE  0xD4200000U  /* BRK #0 — ftrace entry */

static __always_inline int neverc_krt_a64_is_ftrace_site(u32 *code)
{
	u32 insn;
	if (neverc_krt_mem_read(&insn, code, 4))
		return 0;
	if (insn == NEVERC_KRT_A64_BRK_FTRACE) return 1;
	if ((insn & 0xFC000000) == 0x94000000) {
		long imm26 = neverc_krt_sext(insn & 0x3FFFFFF, 26);
		long off = imm26 << 2;
		if (off < -0x100000 || off > 0x100000) return 1;
	}
	return 0;
}

static __always_inline int neverc_krt_a64_is_kprobe_bp(u32 insn)
{
	return insn == NEVERC_KRT_A64_BRK_KPROBE
	    || (insn & 0xFFE0001FU) == 0xD4200000U;
}

static __always_inline int neverc_krt_a64_is_exclusive(u32 insn)
{
	return (insn & 0x3F000000) == 0x08000000;
}

static __always_inline int neverc_krt_a64_is_svc_hvc(u32 insn)
{
	u32 masked = insn & 0xFFE0001FU;
	return masked == 0xD4000001U  /* SVC */
	    || masked == 0xD4000002U  /* HVC */
	    || masked == 0xD4000003U; /* SMC */
}

static __always_inline int neverc_krt_a64_is_hazardous(u32 insn)
{
	if (neverc_krt_a64_is_exclusive(insn)) return 1;
	if (neverc_krt_a64_is_svc_hvc(insn))   return 1;
	if ((insn & 0xFE200000U) == 0xD4200000U) return 1; /* BRK */
	return 0;
}

void _neverc_krt_write_insn(void *addr, u32 insn);


int _neverc_krt_verify_patch(u32 *target, u32 *expected, int count);


int _neverc_krt_patch_multi(u32 *target, u32 *insns, int count);



void _neverc_krt_scan_entry(const u32 *buf, int *skip, int *total);


enum neverc_krt_scan_result {
	NEVERC_KRT_SCAN_OK              =  0,
	NEVERC_KRT_SCAN_TOO_SHORT       = -1,
	NEVERC_KRT_SCAN_HAZARDOUS       = -2,
	NEVERC_KRT_SCAN_UNRELOCATABLE   = -3,
	NEVERC_KRT_SCAN_ALREADY_HOOKED  =  1,
	NEVERC_KRT_SCAN_FTRACE_ACTIVE   =  2,
	NEVERC_KRT_SCAN_KPROBE_ACTIVE   =  3,
};

static __always_inline int neverc_krt_scan_strerror(int r, char *buf, int sz)
{
	if (!buf || sz < 4) return -1;
	char c0 = 'S', c1, c2;
	if (r >= 0) { c1 = '+'; c2 = '0' + (r % 10); }
	else        { c1 = '-'; c2 = '0' + ((-r) % 10); }
	buf[0] = c0; buf[1] = c1; buf[2] = c2; buf[3] = '\0';
	return 3;
}

enum neverc_krt_scan_result neverc_krt_hook_scan(void *target);



int neverc_krt_hook_install(struct neverc_krt_hook *h, void *target,
			    void *replace, void **orig);



#define _CTX_SIZE  288
#define _CTX_FPCR  248
#define _CTX_NZCV  256
#define _CTX_FORCE 264
#define _CTX_FPSR  272

#define _A64E_SUB_SP_I(imm)   (0xD10003FFU | ((u32)(imm) << 10))
#define _A64E_ADD_SP_I(imm)   (0x910003FFU | ((u32)(imm) << 10))
#define _A64E_STP_SP(t1,t2,o) \
	(0xA9000000U|((u32)((o)/8)<<15)|((u32)(t2)<<10)|(31U<<5)|(u32)(t1))
#define _A64E_LDP_SP(t1,t2,o) \
	(0xA9400000U|((u32)((o)/8)<<15)|((u32)(t2)<<10)|(31U<<5)|(u32)(t1))
#define _A64E_STR_SP(t,o)     (0xF9000000U|((u32)((o)/8)<<10)|(31U<<5)|(u32)(t))
#define _A64E_LDR_SP(t,o)     (0xF9400000U|((u32)((o)/8)<<10)|(31U<<5)|(u32)(t))
#define _A64E_MRS_NZCV(t)     (0xD53B4200U|(u32)(t))
#define _A64E_MSR_NZCV(t)     (0xD51B4200U|(u32)(t))
#define _A64E_MRS_SP_EL0(d)   (0xD5384100U|(u32)(d))
#define _A64E_MOV_FROM_SP(d)  (0x910003E0U|(u32)(d))
#define _A64E_MOV_REG(d,n)    (0xAA0003E0U|((u32)(n)<<16)|(u32)(d))
#define _A64E_CMP_REG(n,m)    (0xEB00001FU|((u32)(m)<<16)|((u32)(n)<<5))
#define _A64E_CBNZ_FWD(t,off) (0xB5000000U|(((u32)(off)&0x7FFFFU)<<5)|(u32)(t))
#define _A64E_CBZ_W_FWD(t,off)(0x34000000U|(((u32)(off)&0x7FFFFU)<<5)|(u32)(t))
#define _A64E_BEQ_FWD(off)    (0x54000000U|(((u32)(off)&0x7FFFFU)<<5))
#define _A64E_LDR_WREG(t,n)   (0xB9400000U|((u32)(n)<<5)|(u32)(t))
#define _A64E_LDR_XREG(t,n)   (0xF9400000U|((u32)(n)<<5)|(u32)(t))
#define _A64E_STR_XREG(t,n)   (0xF9000000U|((u32)(n)<<5)|(u32)(t))
#define _A64E_STP_PRE16(t1,t2) \
	(0xA9800000U|((0x7EU)<<15)|((u32)(t2)<<10)|(31U<<5)|(u32)(t1))
#define _A64E_LDP_POST16(t1,t2) \
	(0xA8C00000U|((2U)<<15)|((u32)(t2)<<10)|(31U<<5)|(u32)(t1))
#define _A64E_MOVZ(rd,hw)     (0xD2800000U|((u32)(hw)<<21)|(u32)(rd))
#define _A64E_MOVK16(rd)      (0xF2A00000U|(u32)(rd))
#define _A64E_MOVK32(rd)      (0xF2C00000U|(u32)(rd))
#define _A64E_MOVK48(rd)      (0xF2E00000U|(u32)(rd))

#define _A64E_DMB_ISH  0xD5033BBFu
#define _A64E_MRS_FPCR(t)  (0xD53B4400U|(u32)(t))
#define _A64E_MSR_FPCR(t)  (0xD51B4400U|(u32)(t))
#define _A64E_MRS_FPSR(t)  (0xD53B4420U|(u32)(t))
#define _A64E_MSR_FPSR(t)  (0xD51B4420U|(u32)(t))

static const u32 _neverc_krt_ctx_stub_template[] = {
	/*  0 */ NEVERC_KRT_A64_BTI_JC,
	/*  1 */ _A64E_STP_PRE16(16, 17),
	/*  2 */ _A64E_MOVZ(16, 0),
	/*  3 */ _A64E_MOVK16(16),
	/*  4 */ _A64E_MOVK32(16),
	/*  5 */ _A64E_MOVK48(16),
	/*  6 */ _A64E_LDR_WREG(16, 16),
	/*  7 */ _A64E_DMB_ISH,
	/*  8 */ _A64E_CBZ_W_FWD(16, 110-8),
	/*  9 */ _A64E_MRS_SP_EL0(16),
	/* 10 */ _A64E_MOVZ(17, 0),
	/* 11 */ _A64E_MOVK16(17),
	/* 12 */ _A64E_MOVK32(17),
	/* 13 */ _A64E_MOVK48(17),
	/* 14 */ _A64E_LDR_XREG(17, 17),
	/* 15 */ _A64E_CMP_REG(16, 17),
	/* 16 */ _A64E_BEQ_FWD(110-16),
	/* 17 */ _A64E_LDP_POST16(16, 17),
	/* 18 */ _A64E_SUB_SP_I(_CTX_SIZE),
	/* 19 */ _A64E_STP_SP( 0,  1,   0),
	/* 20 */ _A64E_STP_SP( 2,  3,  16),
	/* 21 */ _A64E_STP_SP( 4,  5,  32),
	/* 22 */ _A64E_STP_SP( 6,  7,  48),
	/* 23 */ _A64E_STP_SP( 8,  9,  64),
	/* 24 */ _A64E_STP_SP(10, 11,  80),
	/* 25 */ _A64E_STP_SP(12, 13,  96),
	/* 26 */ _A64E_STP_SP(14, 15, 112),
	/* 27 */ _A64E_STP_SP(16, 17, 128),
	/* 28 */ _A64E_STP_SP(18, 19, 144),
	/* 29 */ _A64E_STP_SP(20, 21, 160),
	/* 30 */ _A64E_STP_SP(22, 23, 176),
	/* 31 */ _A64E_STP_SP(24, 25, 192),
	/* 32 */ _A64E_STP_SP(26, 27, 208),
	/* 33 */ _A64E_STP_SP(28, 29, 224),
	/* 34 */ _A64E_STP_SP(30, 31, 240),
	/* 35 */ _A64E_MRS_NZCV(1),
	/* 36 */ _A64E_STR_SP(1,  _CTX_NZCV),
	/* 37 */ _A64E_MRS_FPCR(1),
	/* 38 */ _A64E_STR_SP(1,  _CTX_FPCR),
	/* 39 */ _A64E_MRS_FPSR(1),
	/* 40 */ _A64E_STR_SP(1,  _CTX_FPSR),
	/* 41 */ _A64E_STR_SP(31, _CTX_FORCE),
	/* 42 */ _A64E_MRS_SP_EL0(0),
	/* 43 */ _A64E_MOVZ(19, 0),
	/* 44 */ _A64E_MOVK16(19),
	/* 45 */ _A64E_MOVK32(19),
	/* 46 */ _A64E_MOVK48(19),
	/* 47 */ _A64E_STR_XREG(0, 19),
	/* 48 */ _A64E_MOV_FROM_SP(0),
	/* 49 */ _A64E_MOVZ(3, 0),
	/* 50 */ _A64E_MOVK16(3),
	/* 51 */ _A64E_MOVK32(3),
	/* 52 */ _A64E_MOVK48(3),
	/* 53 */ 0xD63F0060U,                    /* BLR X3 */
	/* 54 */ _A64E_STR_XREG(31, 19),
	/* 55 */ _A64E_LDR_SP(1, _CTX_FORCE),
	/* 56 */ _A64E_CBNZ_FWD(1, 85-56),  /* → force_jump path */
	/* 57 */ _A64E_LDR_SP(2, _CTX_FPCR),
	/* 58 */ _A64E_MSR_FPCR(2),
	/* 59 */ _A64E_LDR_SP(2, _CTX_FPSR),
	/* 60 */ _A64E_MSR_FPSR(2),
	/* 61 */ _A64E_LDR_SP(2, _CTX_NZCV),
	/* 62 */ _A64E_MSR_NZCV(2),
	/* 63 */ _A64E_LDP_SP( 2,  3,  16),
	/* 64 */ _A64E_LDP_SP( 4,  5,  32),
	/* 65 */ _A64E_LDP_SP( 6,  7,  48),
	/* 66 */ _A64E_LDP_SP( 8,  9,  64),
	/* 67 */ _A64E_LDP_SP(10, 11,  80),
	/* 68 */ _A64E_LDP_SP(12, 13,  96),
	/* 69 */ _A64E_LDP_SP(14, 15, 112),
	/* 70 */ _A64E_LDP_SP(16, 17, 128),
	/* 71 */ _A64E_LDP_SP(18, 19, 144),
	/* 72 */ _A64E_LDP_SP(20, 21, 160),
	/* 73 */ _A64E_LDP_SP(22, 23, 176),
	/* 74 */ _A64E_LDP_SP(24, 25, 192),
	/* 75 */ _A64E_LDP_SP(26, 27, 208),
	/* 76 */ _A64E_LDP_SP(28, 29, 224),
	/* 77 */ _A64E_LDP_SP(30, 31, 240),
	/* 78 */ _A64E_LDP_SP( 0,  1,   0),
	/* 79 */ _A64E_ADD_SP_I(_CTX_SIZE),
	/* 80 */ _A64E_MOVZ(17, 0),
	/* 81 */ _A64E_MOVK16(17),
	/* 82 */ _A64E_MOVK32(17),
	/* 83a*/ _A64E_MOVK48(17),
	/* 84 */ NEVERC_KRT_A64_RET_X17,
	/* 85 */ _A64E_LDR_SP(16, 128),
	/* 86 */ _A64E_MOV_REG(17, 1),
	/* 87 */ _A64E_LDR_SP(2, _CTX_FPCR),
	/* 88 */ _A64E_MSR_FPCR(2),
	/* 89 */ _A64E_LDR_SP(2, _CTX_FPSR),
	/* 90 */ _A64E_MSR_FPSR(2),
	/* 91 */ _A64E_LDR_SP(2, _CTX_NZCV),
	/* 92 */ _A64E_MSR_NZCV(2),
	/* 93 */ _A64E_LDP_SP( 2,  3,  16),
	/* 94 */ _A64E_LDP_SP( 4,  5,  32),
	/* 95 */ _A64E_LDP_SP( 6,  7,  48),
	/* 96 */ _A64E_LDP_SP( 8,  9,  64),
	/* 97 */ _A64E_LDP_SP(10, 11,  80),
	/* 98 */ _A64E_LDP_SP(12, 13,  96),
	/* 99 */ _A64E_LDP_SP(14, 15, 112),
	/*100 */ _A64E_LDP_SP(18, 19, 144),
	/*101 */ _A64E_LDP_SP(20, 21, 160),
	/*102 */ _A64E_LDP_SP(22, 23, 176),
	/*103 */ _A64E_LDP_SP(24, 25, 192),
	/*104a*/ _A64E_LDP_SP(26, 27, 208),
	/*105 */ _A64E_LDP_SP(28, 29, 224),
	/*106 */ _A64E_LDR_SP(30, 240),
	/*107 */ _A64E_LDP_SP( 0,  1,   0),
	/*108 */ _A64E_ADD_SP_I(_CTX_SIZE),
	/*109 */ NEVERC_KRT_A64_RET_X17,
	/*110 */ _A64E_LDP_POST16(16, 17),
	/*111 */ _A64E_MOVZ(17, 0),
	/*112 */ _A64E_MOVK16(17),
	/*113 */ _A64E_MOVK32(17),
	/*114 */ _A64E_MOVK48(17),
	/*115 */ NEVERC_KRT_A64_RET_X17,
};

#define _CTX_STUB_LEN     (sizeof(_neverc_krt_ctx_stub_template) / sizeof(u32))
#define _CTX_ENABLED_SLOT 2
#define _CTX_GUARD_SLOT_A 10
#define _CTX_GUARD_SLOT_B 43
#define _CTX_HANDLER_SLOT 49
#define _CTX_TRAMP_SLOT_A 80
#define _CTX_TRAMP_SLOT_B 111

_Static_assert(_CTX_STUB_LEN == 116, "context stub v6 size mismatch");
_Static_assert(_CTX_SIZE % 16 == 0, "context frame must be 16-byte aligned");

typedef void (*neverc_krt_ctx_handler_t)(neverc_krt_reg_ctx *ctx);
typedef void (*neverc_krt_ctx_fp_handler_t)(neverc_krt_reg_ctx *ctx, neverc_krt_fp_state *fp);

/*
 * Declare a wrapper that saves/restores Q0-Q31 around a user handler.
 * user_fn must have signature: void fn(neverc_krt_reg_ctx *ctx, neverc_krt_fp_state *fp).
 * Install the wrapper_name (not user_fn) with neverc_krt_hook_install_ctx().
 */
#define NEVERC_KRT_CTX_HANDLER_FP(wrapper_name, user_fn)                        \
	void wrapper_name(neverc_krt_reg_ctx *ctx);


/*
 * Inline FP guard pair — use directly inside a ctx handler when the
 * handler might touch SIMD/FP registers (e.g. calling functions that
 * use floating-point arithmetic).  512 bytes of kernel stack.
 *
 *   static void my_handler(neverc_krt_reg_ctx *ctx) {
 *       NEVERC_KRT_CTX_FP_GUARD_BEGIN;
 *       // ... safe to touch FP here ...
 *       NEVERC_KRT_CTX_FP_GUARD_END;
 *   }
 */
#define NEVERC_KRT_CTX_FP_GUARD_BEGIN                                           \
	neverc_krt_fp_state _neverc_krt_fps_guard;                                    \
	NEVERC_KRT_SAVE_FP(&_neverc_krt_fps_guard)

#define NEVERC_KRT_CTX_FP_GUARD_END                                            \
	NEVERC_KRT_RESTORE_FP(&_neverc_krt_fps_guard)

struct neverc_krt_hook_ctx {
	struct neverc_krt_hook     base;
	u32                *stub;
	u32                *tramp_code;
	volatile unsigned long guard_task;
};

void _neverc_krt_patch_mov64(u32 *page, int slot, int rd, u64 addr);


int neverc_krt_hook_install_ctx(struct neverc_krt_hook_ctx *h, void *target,
				neverc_krt_ctx_handler_t handler, void **call_orig);



void _neverc_krt_full_barrier(void);


static __always_inline void _neverc_krt_tlbi_range(unsigned long start,
					    unsigned long end)
{
	unsigned long addr;
	for (addr = start & ~0xFFFUL; addr < end; addr += 0x1000)
		__asm__ __volatile__("tlbi vale1is, %0"
				     :: "r"(addr >> 12) : "memory");
	__asm__ __volatile__("dsb ish" ::: "memory");
	__asm__ __volatile__("isb" ::: "memory");
}

void _neverc_krt_quiesce(void);


void _neverc_krt_quiesce_deep(void);


void neverc_krt_hook_pause(struct neverc_krt_hook *h);


void neverc_krt_hook_resume(struct neverc_krt_hook *h);


void _neverc_krt_poison_tramp(u32 *tramp, int max_words);


void neverc_krt_hook_remove(struct neverc_krt_hook *h);


int neverc_krt_hook_replace(struct neverc_krt_hook *h, void *new_replace,
			    void **new_orig);


void neverc_krt_hook_remove_ctx(struct neverc_krt_hook_ctx *h);


int neverc_krt_hook_replace_ctx(struct neverc_krt_hook_ctx *h,
				neverc_krt_ctx_handler_t new_handler);


static __always_inline int neverc_krt_hook_is_enabled(struct neverc_krt_hook *h)
{ return READ_ONCE(h->enabled); }

static __always_inline void neverc_krt_hook_enable(struct neverc_krt_hook *h)
{ WRITE_ONCE(h->enabled, 1); }

static __always_inline void neverc_krt_hook_disable(struct neverc_krt_hook *h)
{ WRITE_ONCE(h->enabled, 0); }

struct neverc_krt_hook_batch {
	struct neverc_krt_hook *hook;
	void            *target;
	void            *replace;
	void           **orig;
	int              result;
};

struct neverc_krt_hook_ctx_batch {
	struct neverc_krt_hook_ctx *hook;
	void               *target;
	neverc_krt_ctx_handler_t   handler;
	void              **call_orig;
	int                 result;
};

int neverc_krt_hook_install_ctx_batch(struct neverc_krt_hook_ctx_batch *batch,
				      int count);


int neverc_krt_hook_install_batch(struct neverc_krt_hook_batch *batch, int count);


void neverc_krt_hook_cleanup(void);



/* --- kCFI-safe function pointer replacement --- */

static __always_inline u32 neverc_krt_cfi_read_tag(void *func)
{
	u32 tag = 0;
	unsigned long addr = neverc_krt_strip_pac((unsigned long)func);
	neverc_krt_mem_read(&tag, (void *)(addr - 4), 4);
	return tag;
}

static __always_inline int neverc_krt_cfi_has_tag(void *func)
{
	u32 tag = neverc_krt_cfi_read_tag(func);
	return tag != 0 && tag != NEVERC_KRT_A64_NOP && tag != NEVERC_KRT_A64_BTI_C;
}

struct neverc_krt_cfi_thunk {
	u32  tag;
	u32  code[8];
};

int neverc_krt_cfi_make_thunk(struct neverc_krt_cfi_thunk *thunk,
			      void *orig_func, void *new_func);


struct neverc_krt_fptr_hook {
	void                *struct_addr;
	unsigned long        field_off;
	void                *orig_fn;
	struct neverc_krt_cfi_thunk thunk;
	u32                 *thunk_page;
	int                  active;
};

int neverc_krt_fptr_replace(struct neverc_krt_fptr_hook *h,
			    void *struct_addr, unsigned long field_off,
			    void *new_fn);


void neverc_krt_fptr_restore(struct neverc_krt_fptr_hook *h);



/* --- ftrace-based hook (fallback for unhookable functions) --- */

typedef int  (*neverc_krt_ftrace_set_fn)(unsigned long ip, int enable);
typedef void (*neverc_krt_ftrace_regs_fn)(unsigned long ip, unsigned long pip,
				   void *fregs, void *data);

struct neverc_krt_ftrace_ops {
	unsigned long            func;
	unsigned long            flags;
	unsigned long            _pad[4];
};

typedef int (*neverc_krt_register_ftrace_fn)(struct neverc_krt_ftrace_ops *ops);
typedef int (*neverc_krt_unregister_ftrace_fn)(struct neverc_krt_ftrace_ops *ops);
typedef int (*neverc_krt_ftrace_set_filter_ip_fn)(struct neverc_krt_ftrace_ops *ops,
					   unsigned long ip,
					   int remove, int reset);

NEVERC_KRT_RT_VAR neverc_krt_register_ftrace_fn     _neverc_krt_register_ftrace;
NEVERC_KRT_RT_VAR neverc_krt_unregister_ftrace_fn   _neverc_krt_unregister_ftrace;
NEVERC_KRT_RT_VAR neverc_krt_ftrace_set_filter_ip_fn _neverc_krt_ftrace_set_filter;
NEVERC_KRT_RT_VAR neverc_krt_ftrace_set_fn           _neverc_krt_ftrace_set_ip;
NEVERC_KRT_RT_VAR int _neverc_krt_ftrace_avail;

#define NEVERC_KRT_FTRACE_FL_SAVE_REGS     0x0002UL
#define NEVERC_KRT_FTRACE_FL_SAVE_REGS_IF  0x0004UL
#define NEVERC_KRT_FTRACE_FL_RECURSION     0x0008UL
#define NEVERC_KRT_FTRACE_FL_IPMODIFY      0x0040UL

int neverc_krt_ftrace_init(void);


struct neverc_krt_ftrace_hook {
	void                   *target;
	void                   *replace;
	void                   *orig;
	struct neverc_krt_ftrace_ops   ops;
	int                     active;
};

void _neverc_krt_ftrace_thunk(unsigned long ip, unsigned long parent_ip,
			      void *ops, void *regs);


int neverc_krt_ftrace_hook_install(struct neverc_krt_ftrace_hook *h,
				   void *target, void *replace,
				   void **orig);


void neverc_krt_ftrace_hook_remove(struct neverc_krt_ftrace_hook *h);


int neverc_krt_hook_auto(struct neverc_krt_hook *h, void *target,
			 void *replace, void **orig,
			 struct neverc_krt_ftrace_hook *ft_fallback);


/* --- kprobe-based hook (lightweight fallback) --- */

typedef int (*neverc_krt_kprobe_pre_fn)(void *kp, void *regs);

struct neverc_krt_kprobe_hook {
	void *kp_storage[8];
	void *target;
	void *replace;
	void *orig;
	int   active;
};

typedef int (*neverc_krt_register_kprobe_fn)(void *kp);
typedef void (*neverc_krt_unregister_kprobe_fn)(void *kp);

NEVERC_KRT_RT_VAR neverc_krt_register_kprobe_fn   _neverc_krt_reg_kprobe;
NEVERC_KRT_RT_VAR neverc_krt_unregister_kprobe_fn _neverc_krt_unreg_kprobe;

int neverc_krt_kprobe_hook_init(void);


void neverc_krt_hook_auto_remove(struct neverc_krt_hook *h,
				 struct neverc_krt_ftrace_hook *ft_fallback);



static __always_inline u64 neverc_krt_pool_alloc_count(void)
{ return __atomic_load_n(&_neverc_krt_pool_alloc_total, __ATOMIC_RELAXED); }

static __always_inline u64 neverc_krt_pool_alloc_bytes(void)
{ return __atomic_load_n(&_neverc_krt_pool_alloc_bytes, __ATOMIC_RELAXED); }

static __always_inline int neverc_krt_pool_page_count(void)
{ return _neverc_krt_pool_count; }

int neverc_krt_pool_usage(int *total_used, int *total_cap);



/* --- Hook chain: multiple handlers on the same target --- */

#define NEVERC_KRT_CHAIN_MAX 4

struct neverc_krt_hook_chain_entry {
	void *handler;
	int   priority;
	int   active;
};

struct neverc_krt_hook_chain {
	struct neverc_krt_hook              hook;
	struct neverc_krt_hook_chain_entry  entries[NEVERC_KRT_CHAIN_MAX];
	int                          count;
	void                        *orig_fn;
	void                        *dispatch_fn;
};

typedef long (*neverc_krt_chain_handler_t)(void *orig, void *a0, void *a1,
				    void *a2, void *a3, void *a4, void *a5);

long _neverc_krt_chain_run(struct neverc_krt_hook_chain *chain,
			   void *a0, void *a1, void *a2,
			   void *a3, void *a4, void *a5);


#define NEVERC_KRT_CHAIN_DISPATCH(name, chain_ptr)                                   \
	static long name(void *a0, void *a1, void *a2,                       \
			 void *a3, void *a4, void *a5)                       \
	{ return _neverc_krt_chain_run(&(chain_ptr), a0, a1, a2, a3, a4, a5); }

int neverc_krt_chain_init(struct neverc_krt_hook_chain *chain);


int neverc_krt_chain_add(struct neverc_krt_hook_chain *chain,
			 void *handler, int priority);


struct neverc_krt_hook_stats {
	u64 total_installs;
	u64 total_removes;
	u64 pool_allocs;
	u64 pool_alloc_fails;
	int pool_pages;
	int pool_used_bytes;
	int pool_total_bytes;
	int active_hooks;
};

void neverc_krt_hook_get_stats(struct neverc_krt_hook_stats *out);


int neverc_krt_chain_remove(struct neverc_krt_hook_chain *chain, void *handler);


int neverc_krt_chain_install(struct neverc_krt_hook_chain *chain, void *target,
			     void *dispatch_fn);


void neverc_krt_chain_uninstall(struct neverc_krt_hook_chain *chain);


#endif /* NEVERC_KRT_HOOK_H */
