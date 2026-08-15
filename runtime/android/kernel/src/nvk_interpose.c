/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>
#include <linux/string.h>

#include "nvk_internal.h"

struct ftrace_ops;
struct ftrace_regs;
struct pt_regs;
struct kprobe;

#define NEVERC_KRT_INTERPOSE_FORCE_INLINE __attribute__((always_inline))

NEVERC_KRT_INTERPOSE_FORCE_INLINE long
_neverc_krt_sext(long value, int bits)
{
	long sign = 1L << (bits - 1);

	return (value ^ sign) - sign;
}

/*
 * These public hot-path helpers live in the embedded runtime rather than the
 * public header.  The runtime is linked before optimization, so always_inline
 * still produces the same caller-side code without exposing implementation.
 */
NEVERC_KRT_INTERPOSE_FORCE_INLINE int
neverc_krt_interpose_enter(struct neverc_krt_interpose *interpose)
{
	unsigned long task;
	unsigned long previous;

	__asm__ __volatile__("mrs %0, sp_el0" : "=r"(task));
	previous = __atomic_exchange_n(&interpose->guard, task, __ATOMIC_ACQUIRE);
	if (previous == task)
		return 0;
	NEVERC_KRT_INTERPOSE_COUNT(interpose);
	return 1;
}

NEVERC_KRT_INTERPOSE_FORCE_INLINE void
neverc_krt_interpose_leave(struct neverc_krt_interpose *interpose)
{
	__atomic_store_n(&interpose->guard, 0, __ATOMIC_RELEASE);
}

NEVERC_KRT_INTERPOSE_FORCE_INLINE int
neverc_krt_interpose_enter_safe(struct neverc_krt_interpose *interpose)
{
	if (unlikely(!READ_ONCE(interpose->enabled)))
		return 0;
	return neverc_krt_interpose_enter(interpose);
}

NEVERC_KRT_INTERPOSE_FORCE_INLINE u64
neverc_krt_interpose_hits(struct neverc_krt_interpose *interpose)
{
	return __atomic_load_n(&interpose->hit_count, __ATOMIC_RELAXED);
}

NEVERC_KRT_INTERPOSE_FORCE_INLINE void
neverc_krt_interpose_reset_stats(struct neverc_krt_interpose *interpose)
{
	__atomic_store_n(&interpose->hit_count, 0, __ATOMIC_RELAXED);
}

NEVERC_KRT_INTERPOSE_FORCE_INLINE int
neverc_krt_interpose_is_enabled(struct neverc_krt_interpose *interpose)
{
	return READ_ONCE(interpose->enabled);
}

/* ---- internal ftrace constants (only used in this file) ---- */

#define NEVERC_KRT_FTRACE_FL_SAVE_REGS     (1UL << 2)
#define NEVERC_KRT_FTRACE_FL_SAVE_REGS_IF  (1UL << 3)
#define NEVERC_KRT_FTRACE_FL_RECURSION     (1UL << 4)
#define NEVERC_KRT_FTRACE_FL_IPMODIFY      (1UL << 12)

/* ---- internal kprobe interpose type (only used in this file) ---- */

struct neverc_krt_kprobe_interpose {
	unsigned char kp_storage[160];
	void *target;
	void *replace;
	void *orig;
	int   active;
};

/* ---- internal interpose chain (only used in this file) ---- */

#define NEVERC_KRT_CHAIN_MAX 4

struct neverc_krt_interpose_chain_entry {
	void *handler;
	int   priority;
	int   active;
};

struct neverc_krt_interpose_chain {
	struct neverc_krt_interpose              interpose;
	struct neverc_krt_interpose_chain_entry  entries[NEVERC_KRT_CHAIN_MAX];
	int                          count;
	void                        *orig_fn;
	void                        *dispatch_fn;
};

typedef long (*neverc_krt_chain_handler_t)(void *orig, void *a0, void *a1,
					   void *a2, void *a3, void *a4, void *a5);
typedef long (*neverc_krt_chain_orig_t)(void *a0, void *a1, void *a2,
					void *a3, void *a4, void *a5);

/* ---- internal interpose stats (only used in this file) ---- */

struct neverc_krt_interpose_stats {
	u64 total_installs;
	u64 total_removes;
	u64 pool_allocs;
	u64 pool_alloc_fails;
	int pool_pages;
	int pool_used_bytes;
	int pool_total_bytes;
	int active_interposes;
};

/* ---- internal registry limits ---- */

#define NEVERC_KRT_REGISTRY_MAX 16
#define NEVERC_KRT_PROBE_MAX 16

/* ---- forward declarations ---- */

static long _neverc_krt_chain_call(
	struct neverc_krt_interpose_chain *chain, int index, void *next,
	void *a0, void *a1, void *a2, void *a3, void *a4, void *a5);
static long _neverc_krt_chain_call_orig(
	struct neverc_krt_interpose_chain *chain,
	void *a0, void *a1, void *a2, void *a3, void *a4, void *a5);
static u64 neverc_krt_pool_alloc_count(void);
static u64 neverc_krt_pool_alloc_bytes(void);
static int neverc_krt_pool_page_count(void);
static int neverc_krt_pool_usage(int *total_used, int *total_cap);

/* ---- pool / cache state ---- */

static int          _neverc_krt_pool_count;
static volatile u64 _neverc_krt_pool_alloc_total;
static volatile u64 _neverc_krt_pool_alloc_bytes;
/* Serializes read/verify/write ownership transactions at patched entries and
 * function-pointer slots.
 * Unlike the short metadata spinlocks, this lock may span a sleepable text
 * patch operation and therefore must never be taken with IRQs disabled. */
static volatile int _neverc_krt_patch_lock;

/* ---- internal pool / cache defines ---- */

#define _NEVERC_KRT_POOL_MIN_PAGE 4096
#define _NEVERC_KRT_POOL_ALIGN    16
#define _NEVERC_KRT_POOL_MAX      32

#ifndef _NEVERC_KRT_POOL_MAGIC
#  if __has_builtin(__builtin_neverc_random_u64)
#    define _NEVERC_KRT_POOL_MAGIC ((u32)__builtin_neverc_random_u64())
#  elif defined(NEVERC_KRT_CACHE_SEED)
#    define _NEVERC_KRT_POOL_MAGIC ((u32)(NEVERC_KRT_CACHE_SEED))
#  else
#    define _NEVERC_KRT_POOL_MAGIC 0x4E564B50U
#  endif
#endif

/* ---- internal inline helpers ---- */

static __always_inline int _neverc_krt_pool_page_size(void)
{
	unsigned long tcr;
	__asm__ __volatile__("mrs %0, tcr_el1" : "=r"(tcr));
	u32 tg1 = (tcr >> 30) & 3;
	if (tg1 == 1) return 16384;
	if (tg1 == 2) return 65536;
	return 4096;
}

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
	return (addr >> 63) != 0;
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

static __always_inline void _neverc_krt_tlbi_range(unsigned long start,
					    unsigned long end)
{
	unsigned long pgsz = neverc_krt_page_size();
	unsigned long addr;

	if (!pgsz)
		pgsz = 0x1000UL;
	for (addr = start & ~(pgsz - 1); addr < end; addr += pgsz)
		__asm__ __volatile__("tlbi vale1is, %0"
				     :: "r"((addr >> 12) & ((1UL << 44) - 1))
				     : "memory");
	__asm__ __volatile__("dsb ish" ::: "memory");
	__asm__ __volatile__("isb" ::: "memory");
}

/* ---- ARM64 instruction helpers (internal to nvk_interpose.c) ---- */

#define NEVERC_KRT_A64_BRK_KPROBE 0xD4200080U
#define NEVERC_KRT_A64_FTRACE_NOP  0xD503201FU
#define NEVERC_KRT_A64_BRK_FTRACE  0xD4200000U

static __always_inline int neverc_krt_a64_is_bti(u32 i)
{ return i == 0xD503245FU || i == 0xD503249FU || i == 0xD50324DFU; }

static __always_inline int neverc_krt_a64_is_pac(u32 i)
{
	return i == NEVERC_KRT_A64_PACIASP || i == NEVERC_KRT_A64_PACIBSP
	    || i == 0xD50323BFU
	    || i == 0xD50323FFU;
}

static __always_inline int neverc_krt_a64_is_pac_sign(u32 i)
{ return i == NEVERC_KRT_A64_PACIASP || i == NEVERC_KRT_A64_PACIBSP; }

static __always_inline int neverc_krt_a64_is_pac_auth(u32 i)
{ return i == 0xD50323BFU || i == 0xD50323FFU; }

static __always_inline u32 neverc_krt_a64_movz(int rd, u16 imm, int hw)
{ return 0xD2800000U | ((u32)hw << 21) | ((u32)imm << 5) | rd; }

static __always_inline u32 neverc_krt_a64_movk(int rd, u16 imm, int hw)
{ return 0xF2800000U | ((u32)hw << 21) | ((u32)imm << 5) | rd; }

static __always_inline u32 neverc_krt_a64_gen_b(long off)
{ return 0x14000000U | (((u32)(off >> 2)) & 0x03FFFFFFU); }

static __always_inline int neverc_krt_a64_b_in_range(long off)
{ return off >= -0x8000000L && off < 0x8000000L; }

enum neverc_krt_pcrel {
	NEVERC_KRT_PC_NONE = 0,
	NEVERC_KRT_PC_ADRP, NEVERC_KRT_PC_ADR,
	NEVERC_KRT_PC_B, NEVERC_KRT_PC_BL,
	NEVERC_KRT_PC_BCOND, NEVERC_KRT_PC_CBZ, NEVERC_KRT_PC_TBZ,
	NEVERC_KRT_PC_LDR_LIT,
	NEVERC_KRT_PC_LDRSW_LIT,
	NEVERC_KRT_PC_PRFM_LIT,
};

static __always_inline int neverc_krt_a64_is_stp_fp_lr(u32 insn)
{ return (insn & 0xFFC07FFF) == 0xA9807BFD; }

static __always_inline int neverc_krt_a64_is_frame_setup(u32 insn)
{
	if ((insn & 0x7FE0FFE0) == 0x2A0003E0) return 1; /* mov wd, wm */
	if ((insn & 0xFFE0FFE0) == 0xAA0003E0) return 1; /* mov xd, xm */
	/* add x29, sp, #imm12 (includes mov x29, sp) */
	if ((insn & 0xFF800000) == 0x91000000 && (insn & 0x1F) == 29 &&
	    ((insn >> 5) & 0x1F) == 31)
		return 1;
	return 0;
}

static __always_inline int neverc_krt_a64_is_scs_push(u32 insn)
{
	if (insn == 0xF800841EU) return 1;
	if (insn == 0xF81F0A5EU) return 1;
	if (insn == 0xF900025EU) return 1;
	return 0;
}

static __always_inline int neverc_krt_a64_is_interpose_patch(u32 insn)
{
	if (insn == NEVERC_KRT_A64_LDR_X16_PC8) return 1;
	return 0;
}

#define NEVERC_KRT_A64_IS_BRK(insn) \
	(((u32)(insn) & 0xFFE0001FU) == 0xD4200000U)

/* Keep the exception mask exact.  A broader 0xFE200000 mask also matches
 * ordinary MRS instructions such as `mrs x9, sp_el0` (0xd5384109), making
 * syscall wrappers with a standard current-task load falsely unrelocatable. */
_Static_assert(NEVERC_KRT_A64_IS_BRK(0xD4200000U),
	"BRK #0 must remain hazardous");
_Static_assert(NEVERC_KRT_A64_IS_BRK(0xD43BD5A0U),
	"BRK immediate must remain hazardous");
_Static_assert(!NEVERC_KRT_A64_IS_BRK(0xD5384109U),
	"MRS SP_EL0 must not be classified as BRK");

static __always_inline int neverc_krt_a64_is_kprobe_bp(u32 insn)
{
	return insn == NEVERC_KRT_A64_BRK_KPROBE
	    || NEVERC_KRT_A64_IS_BRK(insn);
}

static __always_inline int neverc_krt_a64_is_exclusive(u32 insn)
{ return (insn & 0x3F000000) == 0x08000000; }

static __always_inline int neverc_krt_a64_is_svc_hvc(u32 insn)
{
	u32 masked = insn & 0xFFE0001FU;
	return masked == 0xD4000001U
	    || masked == 0xD4000002U
	    || masked == 0xD4000003U;
}

static __always_inline int neverc_krt_a64_is_hazardous(u32 insn)
{
	if (neverc_krt_a64_is_exclusive(insn)) return 1;
	if (neverc_krt_a64_is_svc_hvc(insn))   return 1;
	if (NEVERC_KRT_A64_IS_BRK(insn))       return 1;
	return 0;
}

/* ---- context stub template ---- */

#define _CTX_SIZE  304
#define _CTX_FPCR  248
#define _CTX_NZCV  256
#define _CTX_FORCE 264
#define _CTX_FPSR  272
#define _CTX_FORCE_MODE 280
#define _CTX_GUARD_TOKEN 288
#define _CTX_GUARD_SLOTS 16
#define _CTX_GUARD_WORDS (_CTX_GUARD_SLOTS * 2)

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
#define _A64E_CBNZ_W(t,off)   (0x35000000U|(((u32)(off)&0x7FFFFU)<<5)|(u32)(t))
#define _A64E_CBZ_W_FWD(t,off)(0x34000000U|(((u32)(off)&0x7FFFFU)<<5)|(u32)(t))
#define _A64E_CBZ_X(t,off)    (0xB4000000U|(((u32)(off)&0x7FFFFU)<<5)|(u32)(t))
#define _A64E_BEQ_FWD(off)    (0x54000000U|(((u32)(off)&0x7FFFFU)<<5))
#define _A64E_LDR_WREG(t,n)   (0xB9400000U|((u32)(n)<<5)|(u32)(t))
#define _A64E_LDR_XREG(t,n)   (0xF9400000U|((u32)(n)<<5)|(u32)(t))
#define _A64E_STR_XREG(t,n)   (0xF9000000U|((u32)(n)<<5)|(u32)(t))
#define _A64E_STLR_XREG(t,n)  (0xC89FFC00U|((u32)(n)<<5)|(u32)(t))
#define _A64E_LDAR_WREG(t,n)  (0x88DFFC00U|((u32)(n)<<5)|(u32)(t))
#define _A64E_LDAR_XREG(t,n)  (0xC8DFFC00U|((u32)(n)<<5)|(u32)(t))
#define _A64E_LDAXR_XREG(t,n) (0xC85FFC00U|((u32)(n)<<5)|(u32)(t))
#define _A64E_STLXR_XREG(s,t,n) \
	(0xC800FC00U|((u32)(s)<<16)|((u32)(n)<<5)|(u32)(t))
#define _A64E_ADD_X_I(d,n,imm) \
	(0x91000000U|((u32)(imm)<<10)|((u32)(n)<<5)|(u32)(d))
#define _A64E_SUB_X_I(d,n,imm) \
	(0xD1000000U|((u32)(imm)<<10)|((u32)(n)<<5)|(u32)(d))
#define _A64E_EOR_REG(d,n,m) \
	(0xCA000000U|((u32)(m)<<16)|((u32)(n)<<5)|(u32)(d))
#define _A64E_STP_PRE16(t1,t2) \
	(0xA9800000U|((0x7EU)<<15)|((u32)(t2)<<10)|(31U<<5)|(u32)(t1))
#define _A64E_LDP_POST16(t1,t2) \
	(0xA8C00000U|((2U)<<15)|((u32)(t2)<<10)|(31U<<5)|(u32)(t1))
#define _A64E_MOVZ(rd,hw)     (0xD2800000U|((u32)(hw)<<21)|(u32)(rd))
#define _A64E_MOVK16(rd)      (0xF2A00000U|(u32)(rd))
#define _A64E_MOVK32(rd)      (0xF2C00000U|(u32)(rd))
#define _A64E_MOVK48(rd)      (0xF2E00000U|(u32)(rd))
#define _A64E_MOVZ_I(rd,imm)  (0xD2800000U|((u32)(imm)<<5)|(u32)(rd))

#define _A64E_DMB_ISH  0xD5033BBFu
#define _A64E_SEV      0xD503209FU
#define _A64E_MRS_FPCR(t)  (0xD53B4400U|(u32)(t))
#define _A64E_MSR_FPCR(t)  (0xD51B4400U|(u32)(t))
#define _A64E_MRS_FPSR(t)  (0xD53B4420U|(u32)(t))
#define _A64E_MSR_FPSR(t)  (0xD51B4420U|(u32)(t))

static const u32 _neverc_krt_ctx_stub_template[] = {
	/*  0 */ NEVERC_KRT_A64_BTI_JC,
	/*  1 */ _A64E_STP_PRE16(14, 15),
	/*  2 */ _A64E_STP_PRE16(16, 17),
	/*  3 */ _A64E_MOVZ(16, 0),       /* persistent inflight */
	/*  4 */ _A64E_MOVK16(16),
	/*  5 */ _A64E_MOVK32(16),
	/*  6 */ _A64E_MOVK48(16),
	/*  7 */ _A64E_LDAXR_XREG(15, 16),
	/*  8 */ _A64E_ADD_X_I(15, 15, 1),
	/*  9 */ _A64E_STLXR_XREG(17, 15, 16),
	/* 10 */ _A64E_CBNZ_W(17, -3),
	/* 11 */ _A64E_MOVZ(16, 0),       /* persistent enabled */
	/* 12 */ _A64E_MOVK16(16),
	/* 13 */ _A64E_MOVK32(16),
	/* 14 */ _A64E_MOVK48(16),
	/* 15 */ _A64E_LDAR_XREG(16, 16),
	/* 16 */ _A64E_CBZ_X(16, 206-16),
	/* 17 */ _A64E_LDP_POST16(16, 17),
	/* 18 */ _A64E_LDP_POST16(14, 15),
	/* 19 */ _A64E_SUB_SP_I(_CTX_SIZE),
	/* 20 */ _A64E_STP_SP( 0,  1,   0),
	/* 21 */ _A64E_STP_SP( 2,  3,  16),
	/* 22 */ _A64E_STP_SP( 4,  5,  32),
	/* 23 */ _A64E_STP_SP( 6,  7,  48),
	/* 24 */ _A64E_STP_SP( 8,  9,  64),
	/* 25 */ _A64E_STP_SP(10, 11,  80),
	/* 26 */ _A64E_STP_SP(12, 13,  96),
	/* 27 */ _A64E_STP_SP(14, 15, 112),
	/* 28 */ _A64E_STP_SP(16, 17, 128),
	/* 29 */ _A64E_STP_SP(18, 19, 144),
	/* 30 */ _A64E_STP_SP(20, 21, 160),
	/* 31 */ _A64E_STP_SP(22, 23, 176),
	/* 32 */ _A64E_STP_SP(24, 25, 192),
	/* 33 */ _A64E_STP_SP(26, 27, 208),
	/* 34 */ _A64E_STP_SP(28, 29, 224),
	/* 35 */ _A64E_STP_SP(30, 31, 240),
	/* 36 */ _A64E_MRS_NZCV(1),
	/* 37 */ _A64E_STR_SP(1,  _CTX_NZCV),
	/* 38 */ _A64E_MRS_FPCR(1),
	/* 39 */ _A64E_STR_SP(1,  _CTX_FPCR),
	/* 40 */ _A64E_MRS_FPSR(1),
	/* 41 */ _A64E_STR_SP(1,  _CTX_FPSR),
	/* 42 */ _A64E_STR_SP(31, _CTX_FORCE),
	/* 43 */ _A64E_STR_SP(31, _CTX_FORCE_MODE),
	/* 44 */ _A64E_LDR_SP(1, 240),     /* publish original caller LR */
	/* 45 */ _A64E_MOVZ(0, 0),        /* guard-set base */
	/* 46 */ _A64E_MOVK16(0),
	/* 47 */ _A64E_MOVK32(0),
	/* 48 */ _A64E_MOVK48(0),
	/* 49 */ _A64E_MOVZ(3, 0),        /* guard-enter helper */
	/* 50 */ _A64E_MOVK16(3),
	/* 51 */ _A64E_MOVK32(3),
	/* 52 */ _A64E_MOVK48(3),
	/* 53 */ 0xD63F0060U,              /* BLR X3 */
	/* 54 */ _A64E_STR_SP(0, _CTX_GUARD_TOKEN),
	/* 55 */ _A64E_CBZ_X(0, 78-55),  /* recursive/full -> restore */
	/* 56 */ _A64E_MOV_FROM_SP(0),
	/* 57 */ _A64E_MOVZ(3, 0),        /* business handler */
	/* 58 */ _A64E_MOVK16(3),
	/* 59 */ _A64E_MOVK32(3),
	/* 60 */ _A64E_MOVK48(3),
	/* 61 */ 0xD63F0060U,              /* BLR X3 */
	/* 62 */ _A64E_LDR_SP(1, _CTX_FORCE),
	/* 63 */ _A64E_CBZ_X(1, 66-63),
	/* 64 */ _A64E_LDR_SP(2, _CTX_FORCE_MODE),
	/* 65 */ _A64E_CBNZ_FWD(2, 106-65), /* CALL keeps guard */
	/* 66 */ _A64E_LDR_SP(1, _CTX_GUARD_TOKEN),
	/* 67 */ _A64E_MOVZ(0, 0),        /* guard-set base */
	/* 68 */ _A64E_MOVK16(0),
	/* 69 */ _A64E_MOVK32(0),
	/* 70 */ _A64E_MOVK48(0),
	/* 71 */ _A64E_MOVZ(3, 0),        /* guard-leave helper */
	/* 72 */ _A64E_MOVK16(3),
	/* 73 */ _A64E_MOVK32(3),
	/* 74 */ _A64E_MOVK48(3),
	/* 75 */ 0xD63F0060U,              /* BLR X3 */
	/* 76 */ _A64E_LDR_SP(1, _CTX_FORCE),
	/* 77 */ _A64E_CBNZ_FWD(1, 106-77),
	/* 78 */ _A64E_LDR_SP(2, _CTX_FPCR),
	/* 79 */ _A64E_MSR_FPCR(2),
	/* 80 */ _A64E_LDR_SP(2, _CTX_FPSR),
	/* 81 */ _A64E_MSR_FPSR(2),
	/* 82 */ _A64E_LDR_SP(2, _CTX_NZCV),
	/* 83 */ _A64E_MSR_NZCV(2),
	/* 84 */ _A64E_LDP_SP( 2,  3,  16),
	/* 85 */ _A64E_LDP_SP( 4,  5,  32),
	/* 86 */ _A64E_LDP_SP( 6,  7,  48),
	/* 87 */ _A64E_LDP_SP( 8,  9,  64),
	/* 88 */ _A64E_LDP_SP(10, 11,  80),
	/* 89 */ _A64E_LDP_SP(12, 13,  96),
	/* 90 */ _A64E_LDP_SP(14, 15, 112),
	/* 91 */ _A64E_LDP_SP(16, 17, 128),
	/* 92 */ _A64E_LDP_SP(18, 19, 144),
	/* 93 */ _A64E_LDP_SP(20, 21, 160),
	/* 94 */ _A64E_LDP_SP(22, 23, 176),
	/* 95 */ _A64E_LDP_SP(24, 25, 192),
	/* 96 */ _A64E_LDP_SP(26, 27, 208),
	/* 97 */ _A64E_LDP_SP(28, 29, 224),
	/* 98 */ _A64E_LDP_SP(30, 31, 240),
	/* 99 */ _A64E_LDP_SP( 0,  1,   0),
	/*100 */ _A64E_ADD_SP_I(_CTX_SIZE),
	/*101 */ _A64E_MOVZ(17, 0),       /* draining passthrough */
	/*102 */ _A64E_MOVK16(17),
	/*103 */ _A64E_MOVK32(17),
	/*104 */ _A64E_MOVK48(17),
	/*105 */ NEVERC_KRT_A64_BR_X17,
	/*106 */ _A64E_LDR_SP(16, _CTX_FORCE_MODE),
	/*107 */ _A64E_MOV_REG(17, 1),
	/*108 */ _A64E_LDR_SP(2, _CTX_FPCR),
	/*109 */ _A64E_MSR_FPCR(2),
	/*110 */ _A64E_LDR_SP(2, _CTX_FPSR),
	/*111 */ _A64E_MSR_FPSR(2),
	/*112 */ _A64E_LDR_SP(2, _CTX_NZCV),
	/*113 */ _A64E_MSR_NZCV(2),
	/*114 */ _A64E_LDP_SP( 2,  3,  16),
	/*115 */ _A64E_LDP_SP( 4,  5,  32),
	/*116 */ _A64E_LDP_SP( 6,  7,  48),
	/*117 */ _A64E_LDP_SP( 8,  9,  64),
	/*118 */ _A64E_LDP_SP(10, 11,  80),
	/*119 */ _A64E_LDP_SP(12, 13,  96),
	/*120 */ _A64E_LDP_SP(14, 15, 112),
	/*121 */ _A64E_LDP_SP(18, 19, 144),
	/*122 */ _A64E_LDP_SP(20, 21, 160),
	/*123 */ _A64E_LDP_SP(22, 23, 176),
	/*124 */ _A64E_LDP_SP(24, 25, 192),
	/*125 */ _A64E_LDP_SP(26, 27, 208),
	/*126 */ _A64E_LDP_SP(28, 29, 224),
	/*127 */ _A64E_LDR_SP(30, 240),
	/*128 */ _A64E_LDP_SP( 0,  1,   0),
	/*129 */ _A64E_CBNZ_FWD(16, 145-129),
	/*130 */ _A64E_ADD_SP_I(_CTX_SIZE),
	/*131 */ _A64E_STP_PRE16(14, 15),
	/*132 */ _A64E_STP_PRE16(16, 17),
	/*133 */ _A64E_MOVZ(16, 0),       /* direct-force inflight */
	/*134 */ _A64E_MOVK16(16),
	/*135 */ _A64E_MOVK32(16),
	/*136 */ _A64E_MOVK48(16),
	/*137 */ _A64E_LDAXR_XREG(15, 16),
	/*138 */ _A64E_SUB_X_I(15, 15, 1),
	/*139 */ _A64E_STLXR_XREG(17, 15, 16),
	/*140 */ _A64E_CBNZ_W(17, -3),
	/*141 */ _A64E_SEV,
	/*142 */ _A64E_LDP_POST16(16, 17),
	/*143 */ _A64E_LDP_POST16(14, 15),
	/*144 */ NEVERC_KRT_A64_RET_X17,
	/*145 */ _A64E_ADD_SP_I(_CTX_SIZE), /* exact original target SP */
	/*146 */ 0xD63F0220U,              /* BLR X17 -> replacement */
	/*147 */ NEVERC_KRT_A64_BTI_JC,
	/*148 */ _A64E_SUB_SP_I(160),     /* preserve all return GPRs + NZCV */
	/*149 */ _A64E_STP_SP( 0,  1,   0),
	/*150 */ _A64E_STP_SP( 2,  3,  16),
	/*151 */ _A64E_STP_SP( 4,  5,  32),
	/*152 */ _A64E_STP_SP( 6,  7,  48),
	/*153 */ _A64E_STP_SP( 8,  9,  64),
	/*154 */ _A64E_STP_SP(10, 11,  80),
	/*155 */ _A64E_STP_SP(12, 13,  96),
	/*156 */ _A64E_STP_SP(14, 15, 112),
	/*157 */ _A64E_STP_SP(16, 17, 128),
	/*158 */ _A64E_MRS_NZCV(2),
	/*159 */ _A64E_STR_SP(2, 144),
	/*160 */ _A64E_MOVZ(9, 0),        /* retained guard-set base */
	/*161 */ _A64E_MOVK16(9),
	/*162 */ _A64E_MOVK32(9),
	/*163 */ _A64E_MOVK48(9),
	/*164 */ _A64E_MRS_SP_EL0(10),    /* current task */
	/*165 */ _A64E_MOVZ_I(11, _CTX_GUARD_SLOTS),
	/*166 */ _A64E_LDAR_XREG(12, 9),  /* slot.task */
	/*167 */ _A64E_EOR_REG(12, 12, 10),
	/*168 */ _A64E_CBZ_X(12, 173-168),
	/*169 */ _A64E_ADD_X_I(9, 9, 16), /* next {task, caller_lr} */
	/*170 */ _A64E_SUB_X_I(11, 11, 1),
	/*171 */ _A64E_CBNZ_FWD(11, 166-171),
	/*172 */ 0xD43BD5A0U,              /* fail-stop: missing owned slot */
	/*173 */ _A64E_ADD_X_I(13, 9, 8), /* &slot.caller_lr */
	/*174 */ _A64E_LDAR_XREG(30, 13),
	/*175 */ _A64E_CBZ_X(30, 172-175),
	/*176 */ _A64E_STR_XREG(31, 13),  /* clear LR before task publish */
	/*177 */ _A64E_STLR_XREG(31, 9),  /* release slot ownership */
	/*178 */ _A64E_STR_SP(30, 152),
	/*179 */ _A64E_LDR_SP(2, 144),
	/*180 */ _A64E_MSR_NZCV(2),
	/*181 */ _A64E_LDP_SP( 0,  1,   0),
	/*182 */ _A64E_LDP_SP( 2,  3,  16),
	/*183 */ _A64E_LDP_SP( 4,  5,  32),
	/*184 */ _A64E_LDP_SP( 6,  7,  48),
	/*185 */ _A64E_LDP_SP( 8,  9,  64),
	/*186 */ _A64E_LDP_SP(10, 11,  80),
	/*187 */ _A64E_LDP_SP(12, 13,  96),
	/*188 */ _A64E_LDP_SP(14, 15, 112),
	/*189 */ _A64E_LDP_SP(16, 17, 128),
	/*190 */ _A64E_LDR_SP(30, 152),
	/*191 */ _A64E_ADD_SP_I(160),
	/*192 */ _A64E_STP_PRE16(14, 15),
	/*193 */ _A64E_STP_PRE16(16, 17),
	/*194 */ _A64E_MOVZ(16, 0),       /* call-force inflight */
	/*195 */ _A64E_MOVK16(16),
	/*196 */ _A64E_MOVK32(16),
	/*197 */ _A64E_MOVK48(16),
	/*198 */ _A64E_LDAXR_XREG(15, 16),
	/*199 */ _A64E_SUB_X_I(15, 15, 1),
	/*200 */ _A64E_STLXR_XREG(17, 15, 16),
	/*201 */ _A64E_CBNZ_W(17, -3),
	/*202 */ _A64E_SEV,
	/*203 */ _A64E_LDP_POST16(16, 17),
	/*204 */ _A64E_LDP_POST16(14, 15),
	/*205 */ 0xD65F03C0U,
	/*206 */ _A64E_LDP_POST16(16, 17),
	/*207 */ _A64E_LDP_POST16(14, 15),
	/*208 */ _A64E_MOVZ(17, 0),       /* disabled -> passthrough */
	/*209 */ _A64E_MOVK16(17),
	/*210 */ _A64E_MOVK32(17),
	/*211 */ _A64E_MOVK48(17),
	/*212 */ NEVERC_KRT_A64_BR_X17,
};

#define _CTX_STUB_LEN     (sizeof(_neverc_krt_ctx_stub_template) / sizeof(u32))
#define _CTX_INFLIGHT_SLOT_ENTER 3
#define _CTX_ENABLED_SLOT 11
#define _CTX_GUARD_SLOT_A 45
#define _CTX_GUARD_ENTER_SLOT 49
#define _CTX_HANDLER_SLOT 57
#define _CTX_GUARD_SLOT_B 67
#define _CTX_GUARD_LEAVE_SLOT 71
#define _CTX_TRAMP_SLOT_A 101
#define _CTX_INFLIGHT_SLOT_DIRECT 133
#define _CTX_GUARD_SLOT_CALL 160
#define _CTX_INFLIGHT_SLOT_CALL 194
#define _CTX_TRAMP_SLOT_B 208

_Static_assert(_CTX_STUB_LEN == 213, "context stub v11 size mismatch");
_Static_assert(_CTX_SIZE % 16 == 0, "context frame must be 16-byte aligned");

/* ---- internal typedefs ---- */

typedef void *(*neverc_krt_modalloc_fn)(unsigned long);
typedef void *(*neverc_krt_execmem_alloc_fn)(int type, unsigned long size);
typedef void  (*neverc_krt_modfree_fn)(void *);
typedef void  (*neverc_krt_flushic_fn)(unsigned long, unsigned long);
typedef int   (*neverc_krt_patchtext_fn)(void **, u32 *, int);
typedef void  (*neverc_krt_syncrcu_fn)(void);
typedef void  (*neverc_krt_msleep_fn)(unsigned int);
typedef int   (*neverc_krt_ksize_fn)(unsigned long addr, unsigned long *sz,
				     unsigned long *off);
typedef int   (*neverc_krt_register_ftrace_fn)(struct ftrace_ops *ops);
typedef int   (*neverc_krt_unregister_ftrace_fn)(struct ftrace_ops *ops);
typedef int   (*neverc_krt_ftrace_set_filter_ip_fn)(struct ftrace_ops *ops,
						    unsigned long ip,
						    int remove, int reset);
typedef int   (*neverc_krt_register_kprobe_fn)(struct kprobe *kp);
typedef void  (*neverc_krt_unregister_kprobe_fn)(struct kprobe *kp);

/* ---- internal structs ---- */

struct _neverc_krt_pool_page {
	u32    *base;
	int     used;
	int     refcnt;
	u32     magic;
};

/* ---- internal variables (file-local) ---- */

static neverc_krt_modalloc_fn       _neverc_krt_modalloc;
static neverc_krt_execmem_alloc_fn  _neverc_krt_execmem_alloc;
static neverc_krt_modfree_fn        _neverc_krt_modfree;
static neverc_krt_flushic_fn        _neverc_krt_flushic;
static neverc_krt_patchtext_fn      _neverc_krt_patchtext;
static neverc_krt_syncrcu_fn        _neverc_krt_syncrcu;
static neverc_krt_msleep_fn         _neverc_krt_msleep;
static int (*_neverc_krt_set_memory_x)(unsigned long addr, int numpages);
static int                          _neverc_krt_inited;
enum {
	_NEVERC_KRT_LIFECYCLE_STOPPED = 0,
	_NEVERC_KRT_LIFECYCLE_RUNNING,
	_NEVERC_KRT_LIFECYCLE_STARTING,
	_NEVERC_KRT_LIFECYCLE_STOPPING,
};
static volatile int                 _neverc_krt_lifecycle_state;
static volatile unsigned int        _neverc_krt_lifecycle_ops;
static neverc_krt_ksize_fn          _neverc_krt_ksize;

static volatile int                 _neverc_krt_pool_lock;
static struct _neverc_krt_pool_page _neverc_krt_pool[_NEVERC_KRT_POOL_MAX];
static int                          _neverc_krt_pool_pgsz;
static volatile u64                 _neverc_krt_pool_alloc_fail;
static volatile u64                 _neverc_krt_interpose_install_cnt;
static volatile u64                 _neverc_krt_interpose_remove_cnt;

static neverc_krt_register_ftrace_fn     _neverc_krt_register_ftrace;
static neverc_krt_unregister_ftrace_fn   _neverc_krt_unregister_ftrace;
static neverc_krt_ftrace_set_filter_ip_fn _neverc_krt_ftrace_set_filter;
static int                                _neverc_krt_ftrace_avail;

static neverc_krt_register_kprobe_fn   _neverc_krt_reg_kprobe;
static neverc_krt_unregister_kprobe_fn _neverc_krt_unreg_kprobe;

/* ---- static forward declarations ---- */

static void *_neverc_krt_alloc_exec(unsigned long size);
static u32 *_neverc_krt_pool_alloc(int bytes);
static void _neverc_krt_pool_free(u32 *ptr);
static unsigned long _neverc_krt_fn_size(void *addr);
static int _neverc_krt_verify_patch(u32 *target, u32 *expected, int count);
static void _neverc_krt_scan_entry(const u32 *buf, int *skip, int *total);
static void _neverc_krt_patch_mov64(u32 *page, int slot, int rd, u64 addr);
static void _neverc_krt_full_barrier(void);
static void _neverc_krt_quiesce(void);
static int neverc_krt_a64_gen_mov64(u32 *out, int rd, u64 addr);
static int neverc_krt_a64_relocate_abs(u32 insn, unsigned long old_pc, u32 *out,
				       unsigned long win_lo, unsigned long win_hi);
static enum neverc_krt_pcrel neverc_krt_a64_classify(u32 i);
static int neverc_krt_a64_is_ftrace_site(u32 *code);

static int _neverc_krt_lifecycle_begin(void)
{
	if (__atomic_load_n(&_neverc_krt_lifecycle_state,
			    __ATOMIC_ACQUIRE) != _NEVERC_KRT_LIFECYCLE_RUNNING)
		return NEVERC_KRT_INTERPOSE_E_NOINIT;
	__atomic_fetch_add(&_neverc_krt_lifecycle_ops, 1, __ATOMIC_ACQ_REL);
	if (__atomic_load_n(&_neverc_krt_lifecycle_state,
			    __ATOMIC_ACQUIRE) == _NEVERC_KRT_LIFECYCLE_RUNNING)
		return NEVERC_KRT_INTERPOSE_OK;
	__atomic_fetch_sub(&_neverc_krt_lifecycle_ops, 1, __ATOMIC_ACQ_REL);
	__asm__ __volatile__("sev" ::: "memory");
	return NEVERC_KRT_INTERPOSE_E_NOINIT;
}

static void _neverc_krt_lifecycle_end(void)
{
	__atomic_fetch_sub(&_neverc_krt_lifecycle_ops, 1, __ATOMIC_RELEASE);
	__asm__ __volatile__("sev" ::: "memory");
}

static int neverc_krt_a64_gen_mov64(u32 *out, int rd, u64 addr)
{
	int n = 0;
	out[n++] = neverc_krt_a64_movz(rd, (u16)(addr & 0xFFFF), 0);
	out[n++] = neverc_krt_a64_movk(rd, (u16)((addr >> 16) & 0xFFFF), 1);
	out[n++] = neverc_krt_a64_movk(rd, (u16)((addr >> 32) & 0xFFFF), 2);
	if (addr >> 48)
		out[n++] = neverc_krt_a64_movk(rd, (u16)((addr >> 48) & 0xFFFF), 3);
	return n;
}

static int neverc_krt_a64_relocate_abs(u32 insn, unsigned long old_pc, u32 *out,
				       unsigned long win_lo, unsigned long win_hi)
{
	enum neverc_krt_pcrel kind = neverc_krt_a64_classify(insn);
	unsigned long target;
	int n = 0;

	/* A relocated control-flow branch whose destination lands inside the
	 * overwritten patch window [win_lo, win_hi) would resolve to the live
	 * target address, which now holds the LDR X16/BR X16 dispatch bytes —
	 * re-entering the stub (recursion / wild jump).  Fully relocating such an
	 * intra-window branch to the corresponding trampoline slot is unsupported,
	 * so fail closed (return 0 → caller refuses the hook) instead of emitting
	 * a branch into patched bytes.  Address/data-relative forms (ADRP/ADR/
	 * LDR-literal) are not control flow and are left alone. */
#define _NEVERC_KRT_BRANCH_IN_WINDOW(t) \
	(win_hi > win_lo && (t) >= win_lo && (t) < win_hi)

	switch (kind) {

	case NEVERC_KRT_PC_NONE:
		out[n++] = insn;
		return n;

	case NEVERC_KRT_PC_ADRP: {
		int immlo = (insn >> 29) & 3;
		long immhi = _neverc_krt_sext((insn >> 5) & 0x7FFFF, 19);
		long displacement = (immhi * 4 + immlo) * 4096;
		target = (old_pc & ~0xFFFUL) + displacement;
		int rd = insn & 0x1F;
		n = neverc_krt_a64_gen_mov64(out, rd, target);
		return n;
	}

	case NEVERC_KRT_PC_ADR: {
		int immlo = (insn >> 29) & 3;
		long immhi = _neverc_krt_sext((insn >> 5) & 0x7FFFF, 19);
		target = old_pc + immhi * 4 + immlo;
		int rd = insn & 0x1F;
		return neverc_krt_a64_gen_mov64(out, rd, target);
	}

	case NEVERC_KRT_PC_B: {
		long imm26 = _neverc_krt_sext(insn & 0x3FFFFFF, 26);
		target = old_pc + imm26 * 4;
		if (_NEVERC_KRT_BRANCH_IN_WINDOW(target))
			return 0;
		n = neverc_krt_a64_gen_mov64(out, 17, target);
		/* same as tramp resume: target is kernel text, not our BTI_JC */
		out[n++] = NEVERC_KRT_A64_RET_X17;
		return n;
	}

	case NEVERC_KRT_PC_BL: {
		long imm26 = _neverc_krt_sext(insn & 0x3FFFFFF, 26);
		target = old_pc + imm26 * 4;
		if (_NEVERC_KRT_BRANCH_IN_WINDOW(target))
			return 0;
		n = neverc_krt_a64_gen_mov64(out, 17, target);
		out[n++] = 0xD63F0220U;  /* BLR X17 */
		return n;
	}

	case NEVERC_KRT_PC_BCOND: {
		long imm19 = _neverc_krt_sext((insn >> 5) & 0x7FFFF, 19);
		target = old_pc + imm19 * 4;
		if (_NEVERC_KRT_BRANCH_IN_WINDOW(target))
			return 0;
		u32 inv = (insn & 0xFF00000FU) ^ 1U;  /* invert LSB of cond */
		int skip_n;
		/* We'll fix the skip offset after emitting the jump. */
		int cond_slot = n;
		out[n++] = 0; /* placeholder */
		n += neverc_krt_a64_gen_mov64(&out[n], 17, target);
		out[n++] = NEVERC_KRT_A64_RET_X17;
		skip_n = n - cond_slot;
		/* B.!cond skip:  imm19 = skip_n, shifted left 5 */
		out[cond_slot] = inv | (((u32)skip_n & 0x7FFFF) << 5);
		return n;
	}

	case NEVERC_KRT_PC_CBZ: {
		long imm19 = _neverc_krt_sext((insn >> 5) & 0x7FFFF, 19);
		target = old_pc + imm19 * 4;
		if (_NEVERC_KRT_BRANCH_IN_WINDOW(target))
			return 0;
		/* Invert CBZ<->CBNZ: toggle bit 24 */
		u32 inv = insn ^ 0x01000000U;
		inv &= ~(0x7FFFFU << 5);  /* clear imm19 */
		int cond_slot = n;
		out[n++] = 0; /* placeholder */
		n += neverc_krt_a64_gen_mov64(&out[n], 17, target);
		out[n++] = NEVERC_KRT_A64_RET_X17;
		int skip_n = n - cond_slot;
		out[cond_slot] = inv | (((u32)skip_n & 0x7FFFF) << 5);
		return n;
	}

	case NEVERC_KRT_PC_TBZ: {
		long imm14 = _neverc_krt_sext((insn >> 5) & 0x3FFF, 14);
		target = old_pc + imm14 * 4;
		if (_NEVERC_KRT_BRANCH_IN_WINDOW(target))
			return 0;
		/* Invert TBZ<->TBNZ: toggle bit 24 */
		u32 inv = insn ^ 0x01000000U;
		inv &= ~(0x3FFFU << 5);  /* clear imm14 */
		int cond_slot = n;
		out[n++] = 0; /* placeholder */
		n += neverc_krt_a64_gen_mov64(&out[n], 17, target);
		out[n++] = NEVERC_KRT_A64_RET_X17;
		int skip_n = n - cond_slot;
		out[cond_slot] = inv | (((u32)skip_n & 0x3FFFU) << 5);
		return n;
	}

	case NEVERC_KRT_PC_LDR_LIT: {
		long imm19 = _neverc_krt_sext((insn >> 5) & 0x7FFFF, 19);
		target = old_pc + imm19 * 4;
		int rt = insn & 0x1F;
		int opc = (insn >> 30) & 3;
		int is_simd = (insn >> 26) & 1;
		n = neverc_krt_a64_gen_mov64(out, 17, target);
		if (is_simd) {
			if (opc == 0)
				out[n++] = 0xBD400000U | (17 << 5) | rt;
			else if (opc == 1)
				out[n++] = 0xFD400000U | (17 << 5) | rt;
			else if (opc == 2)
				out[n++] = 0x3DC00000U | (17 << 5) | rt;
			else
				return 0;
		} else {
			if (opc == 0)
				out[n++] = 0xB9400000U | (17 << 5) | rt;
			else
				out[n++] = 0xF9400000U | (17 << 5) | rt;
		}
		return n;
	}

	case NEVERC_KRT_PC_LDRSW_LIT: {
		long imm19 = _neverc_krt_sext((insn >> 5) & 0x7FFFF, 19);
		target = old_pc + imm19 * 4;
		int rt = insn & 0x1F;
		n = neverc_krt_a64_gen_mov64(out, 17, target);
		out[n++] = 0xB9800000U | (17 << 5) | rt; /* LDRSW Xt, [X17] */
		return n;
	}

	case NEVERC_KRT_PC_PRFM_LIT: {
		long imm19 = _neverc_krt_sext((insn >> 5) & 0x7FFFF, 19);
		target = old_pc + imm19 * 4;
		int rt = insn & 0x1F;
		n = neverc_krt_a64_gen_mov64(out, 17, target);
		out[n++] = 0xF9800000U | (17 << 5) | rt; /* PRFM type, [X17] */
		return n;
	}
	}

	out[0] = insn;
	return 1;
#undef _NEVERC_KRT_BRANCH_IN_WINDOW
}

static void *_neverc_krt_alloc_exec(unsigned long size)
{
	if (_neverc_krt_modalloc)
		return _neverc_krt_modalloc(size);
	if (_neverc_krt_execmem_alloc)
		return _neverc_krt_execmem_alloc(0 /* EXECMEM_MODULE_TEXT */, size);
	return (void *)0;
}

static u32 *_neverc_krt_pool_alloc(int bytes)
{
	int i, pgsz, best = -1;
	u32 *ret = (void *)0;
	int best_remain = 0x7FFFFFFF;
	unsigned long flags;
	bytes = (bytes + _NEVERC_KRT_POOL_ALIGN - 1) & ~(_NEVERC_KRT_POOL_ALIGN - 1);

	if (!_neverc_krt_pool_pgsz)
		_neverc_krt_pool_pgsz = _neverc_krt_pool_page_size();
	pgsz = _neverc_krt_pool_pgsz;

	if (bytes > pgsz)
		return (void *)0;

	flags = _neverc_krt_spin_lock_irqsave(&_neverc_krt_pool_lock);

	for (i = 0; i < _neverc_krt_pool_count; i++) {
		if (_neverc_krt_pool[i].magic != _NEVERC_KRT_POOL_MAGIC) continue;
		int remain = pgsz - _neverc_krt_pool[i].used;
		if (remain >= bytes && remain < best_remain) {
			best = i;
			best_remain = remain;
		}
	}

	if (best >= 0) {
		ret = (u32 *)((unsigned long)_neverc_krt_pool[best].base +
			      _neverc_krt_pool[best].used);
		_neverc_krt_pool[best].used += bytes;
		_neverc_krt_pool[best].refcnt++;
		__atomic_fetch_add(&_neverc_krt_pool_alloc_total, 1,
				   __ATOMIC_RELAXED);
		__atomic_fetch_add(&_neverc_krt_pool_alloc_bytes, bytes,
				   __ATOMIC_RELAXED);
		_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_pool_lock, flags);
		return ret;
	}

	if (_neverc_krt_pool_count >= _NEVERC_KRT_POOL_MAX ||
	    (!_neverc_krt_modalloc && !_neverc_krt_execmem_alloc)) {
		__atomic_fetch_add(&_neverc_krt_pool_alloc_fail, 1,
				   __ATOMIC_RELAXED);
		_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_pool_lock, flags);
		return (void *)0;
	}

	_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_pool_lock, flags);

	u32 *page = (u32 *)_neverc_krt_alloc_exec(pgsz);
	if (!page) {
		__atomic_fetch_add(&_neverc_krt_pool_alloc_fail, 1,
				   __ATOMIC_RELAXED);
		return (void *)0;
	}

	flags = _neverc_krt_spin_lock_irqsave(&_neverc_krt_pool_lock);
	if (_neverc_krt_pool_count >= _NEVERC_KRT_POOL_MAX) {
		_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_pool_lock, flags);
		if (_neverc_krt_modfree) _neverc_krt_modfree(page);
		__atomic_fetch_add(&_neverc_krt_pool_alloc_fail, 1,
				   __ATOMIC_RELAXED);
		return (void *)0;
	}
	i = _neverc_krt_pool_count++;
	_neverc_krt_pool[i].base = page;
	_neverc_krt_pool[i].used = bytes;
	_neverc_krt_pool[i].refcnt = 1;
	_neverc_krt_pool[i].magic = _NEVERC_KRT_POOL_MAGIC;
	__atomic_fetch_add(&_neverc_krt_pool_alloc_total, 1, __ATOMIC_RELAXED);
	__atomic_fetch_add(&_neverc_krt_pool_alloc_bytes, bytes, __ATOMIC_RELAXED);
	_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_pool_lock, flags);
	return page;
}

static void _neverc_krt_pool_free(u32 *ptr)
{
	int i;
	int pgsz = _neverc_krt_pool_pgsz ? _neverc_krt_pool_pgsz : 4096;
	unsigned long flags;
	if (!ptr) return;
	if (!_neverc_krt_is_kern_ptr((unsigned long)ptr)) return;

	flags = _neverc_krt_spin_lock_irqsave(&_neverc_krt_pool_lock);
	for (i = 0; i < _neverc_krt_pool_count; i++) {
		if (_neverc_krt_pool[i].magic != _NEVERC_KRT_POOL_MAGIC) continue;
		unsigned long base = (unsigned long)_neverc_krt_pool[i].base;
		if ((unsigned long)ptr >= base &&
		    (unsigned long)ptr < base + (unsigned long)pgsz) {
			if (_neverc_krt_pool[i].refcnt <= 0) {
				_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_pool_lock, flags);
				return;
			}
			if (--_neverc_krt_pool[i].refcnt <= 0) {
				u32 *to_free = _neverc_krt_pool[i].base;
				int sz = _neverc_krt_pool[i].used;
				_neverc_krt_pool[i].magic = 0;
				_neverc_krt_pool[i] = _neverc_krt_pool[--_neverc_krt_pool_count];
				_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_pool_lock, flags);
				int w;
				for (w = 0; w < (sz >> 2); w++)
					to_free[w] = 0xD4200000U | (0xDEADU << 5);
				_neverc_krt_dcache_clean((unsigned long)to_free,
						  (unsigned long)to_free + sz);
				if (_neverc_krt_flushic)
					_neverc_krt_flushic((unsigned long)to_free,
						     (unsigned long)to_free + sz);
				if (_neverc_krt_modfree) _neverc_krt_modfree(to_free);
				return;
			}
			_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_pool_lock, flags);
			return;
		}
	}
	_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_pool_lock, flags);
}

int neverc_krt_interpose_init(void)
{
	int expected = _NEVERC_KRT_LIFECYCLE_STOPPED;

	if (!__atomic_compare_exchange_n(
		    &_neverc_krt_lifecycle_state, &expected,
		    _NEVERC_KRT_LIFECYCLE_STARTING, 0,
		    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		if (expected == _NEVERC_KRT_LIFECYCLE_RUNNING)
			return NEVERC_KRT_INTERPOSE_OK;
		return NEVERC_KRT_INTERPOSE_E_CONFLICT;
	}
	_neverc_krt_modalloc = (neverc_krt_modalloc_fn)NEVERC_KRT_LOOKUP("module_alloc");
	if (!_neverc_krt_modalloc)
		_neverc_krt_execmem_alloc =
			(neverc_krt_execmem_alloc_fn)NEVERC_KRT_LOOKUP("execmem_alloc");
	_neverc_krt_modfree  = (neverc_krt_modfree_fn)NEVERC_KRT_LOOKUP("module_memfree");
	if (!_neverc_krt_modfree)
		_neverc_krt_modfree = (neverc_krt_modfree_fn)NEVERC_KRT_LOOKUP("execmem_free");
	if (!_neverc_krt_modfree)
		_neverc_krt_modfree = (neverc_krt_modfree_fn)NEVERC_KRT_LOOKUP("vfree");
	_neverc_krt_flushic = (neverc_krt_flushic_fn)NEVERC_KRT_LOOKUP("flush_icache_range");
	if (!_neverc_krt_flushic)
		_neverc_krt_flushic = (neverc_krt_flushic_fn)NEVERC_KRT_LOOKUP("__flush_icache_range");
	if (!_neverc_krt_flushic)
		_neverc_krt_flushic = (neverc_krt_flushic_fn)NEVERC_KRT_LOOKUP("caches_clean_inval_pou");
	_neverc_krt_patchtext = (neverc_krt_patchtext_fn)NEVERC_KRT_LOOKUP("aarch64_insn_patch_text");
	_neverc_krt_ksize = (neverc_krt_ksize_fn)NEVERC_KRT_LOOKUP("kallsyms_lookup_size_offset");
	_neverc_krt_syncrcu = (neverc_krt_syncrcu_fn)NEVERC_KRT_LOOKUP("synchronize_rcu");
	_neverc_krt_msleep  = (neverc_krt_msleep_fn)NEVERC_KRT_LOOKUP("msleep");
	_neverc_krt_set_memory_x = (int (*)(unsigned long, int))NEVERC_KRT_LOOKUP("set_memory_x");
	if ((!_neverc_krt_modalloc && !_neverc_krt_execmem_alloc) ||
	    !_neverc_krt_modfree || !_neverc_krt_flushic ||
	    !_neverc_krt_patchtext) {
		__atomic_store_n(&_neverc_krt_lifecycle_state,
				 _NEVERC_KRT_LIFECYCLE_STOPPED,
				 __ATOMIC_RELEASE);
		return -1;
	}
	WRITE_ONCE(_neverc_krt_inited, 1);
	__atomic_store_n(&_neverc_krt_lifecycle_state,
			 _NEVERC_KRT_LIFECYCLE_RUNNING, __ATOMIC_RELEASE);
	return 0;
}

static unsigned long _neverc_krt_fn_size(void *addr)
{
	unsigned long sz = 0, off = 0;
	if (_neverc_krt_ksize &&
	    _neverc_krt_ksize((unsigned long)addr, &sz, &off) &&
	    off <= sz)
		return sz - off;
	return 0;
}

static int _neverc_krt_verify_patch(u32 *target, u32 *expected, int count)
{
	int i;
	__asm__ __volatile__("dsb ish" ::: "memory");
	__asm__ __volatile__("isb" ::: "memory");
	for (i = 0; i < count; i++) {
		u32 val;
		if (neverc_krt_mem_read(&val, &target[i], 4))
			return -1;
		if (val != expected[i])
			return -1;
	}
	return 0;
}

int _neverc_krt_patch_multi(u32 *target, u32 *insns, int count)
{
	void *addrs[16];
	int i;

	/* Never publish a live entry through NeverC's ordinary per-word fallback.
	 * Require the kernel stop-machine multi-patch backend so peer CPUs remain
	 * synchronized while the entry words are updated.  The backend itself is
	 * not all-or-nothing on an individual write fault; callers must verify and
	 * retain a disabled recovery handle when restore cannot be proven. */
	if (!_neverc_krt_patchtext || !target || !insns ||
	    count <= 0 || count > (int)(sizeof(addrs) / sizeof(addrs[0])))
		return -1;
	for (i = 0; i < count; i++)
		addrs[i] = &target[i];
	if (_neverc_krt_patchtext(addrs, insns, count) != 0)
		return -1;
	return _neverc_krt_verify_patch(target, insns, count);
}

static void _neverc_krt_scan_entry(const u32 *buf, int *skip, int *total)
{
	int s = 0;
	while (s < NEVERC_KRT_INTERPOSE_MAX_PATCH - 4) {
		u32 insn = buf[s];
		if (neverc_krt_a64_is_bti(insn) || insn == NEVERC_KRT_A64_NOP ||
		    neverc_krt_a64_is_pac_sign(insn) ||
		    neverc_krt_a64_is_pac_auth(insn) ||
		    neverc_krt_a64_is_stp_fp_lr(insn) ||
		    neverc_krt_a64_is_frame_setup(insn) || neverc_krt_a64_is_scs_push(insn))
			s++;
		else
			break;
	}
	*total = s + 4;
	if (*total > NEVERC_KRT_INTERPOSE_MAX_PATCH)
		*total = NEVERC_KRT_INTERPOSE_MAX_PATCH;
	if (*total < 4)
		*total = 4;
	*skip = s;
}

enum neverc_krt_scan_result neverc_krt_interpose_scan(void *target)
{
	target = (void *)neverc_krt_strip_pac((unsigned long)target);
	u32 *code = (u32 *)target;
	int skip, patch_count, i;

	u32 scan_buf[NEVERC_KRT_INTERPOSE_MAX_PATCH];
	if (neverc_krt_mem_read(scan_buf, code, sizeof(scan_buf)))
		return NEVERC_KRT_SCAN_TOO_SHORT;

	int entry = neverc_krt_a64_is_bti(scan_buf[0]) ? 1 : 0;
	if (neverc_krt_a64_is_interpose_patch(scan_buf[entry]))
		return NEVERC_KRT_SCAN_ALREADY_INTERPOSEED;

	if (neverc_krt_a64_is_kprobe_bp(scan_buf[entry]))
		return NEVERC_KRT_SCAN_KPROBE_ACTIVE;

	if (neverc_krt_a64_is_ftrace_site(code))
		return NEVERC_KRT_SCAN_FTRACE_ACTIVE;

	_neverc_krt_scan_entry(scan_buf, &skip, &patch_count);
	if (_neverc_krt_ksize) {
		unsigned long fn_sz = _neverc_krt_fn_size(target);
		if (fn_sz > 0 &&
		    fn_sz < (unsigned long)patch_count * sizeof(u32))
			return NEVERC_KRT_SCAN_TOO_SHORT;
	}

	for (i = 0; i < patch_count; i++) {
		u32 insn = scan_buf[i];
		if (neverc_krt_a64_is_bti(insn) || insn == NEVERC_KRT_A64_NOP)
			continue;
		if (neverc_krt_a64_is_hazardous(insn))
			return NEVERC_KRT_SCAN_HAZARDOUS;
		u32 tmp[8];
		int n = neverc_krt_a64_relocate_abs(
			insn, (unsigned long)&code[i], tmp,
			(unsigned long)code,
			(unsigned long)code + (unsigned long)patch_count *
				sizeof(u32));
		if (n == 0) return NEVERC_KRT_SCAN_UNRELOCATABLE;
	}

	return NEVERC_KRT_SCAN_OK;
}

static int  _neverc_krt_ll_install(struct neverc_krt_interpose *h, void *target,
				   void *replace, void **orig);
static int  _neverc_krt_ll_remove(struct neverc_krt_interpose *h);
static int  _neverc_krt_ll_replace(struct neverc_krt_interpose *h,
				   void *new_replace, void **new_orig);
static int  _neverc_krt_restore_ctx_text(
				   struct neverc_krt_interpose_ctx *h);
static int  _neverc_krt_restore_ctx_text_unchecked(
				   struct neverc_krt_interpose_ctx *h);
static int  _neverc_krt_interpose_remove_ctx_many_impl(
				   struct neverc_krt_interpose_ctx **list,
				   int count);
static void _neverc_krt_release_ctx_owner(
				   struct neverc_krt_interpose_ctx *h);

#define _NEVERC_KRT_ADVANCED_MAX 64

enum _neverc_krt_advanced_kind {
	_NEVERC_KRT_ADVANCED_CTX = 1,
	_NEVERC_KRT_ADVANCED_FPTR,
	_NEVERC_KRT_ADVANCED_FTRACE,
};

struct _neverc_krt_advanced_owner {
	void *owner;
	u64 sequence;
	int kind;
	int used;
	int active;
};

static struct _neverc_krt_advanced_owner
	_neverc_krt_advanced[_NEVERC_KRT_ADVANCED_MAX];
static volatile int _neverc_krt_advanced_lock;
static volatile u64 _neverc_krt_advanced_sequence;

static int _neverc_krt_advanced_reserve(void *owner, int kind, int *slot_out)
{
	unsigned long flags;
	int free_slot = -1;
	int i;

	if (!owner || !slot_out)
		return NEVERC_KRT_INTERPOSE_E_SHORT;
	flags = _neverc_krt_spin_lock_irqsave(&_neverc_krt_advanced_lock);
	for (i = 0; i < _NEVERC_KRT_ADVANCED_MAX; i++) {
		if (_neverc_krt_advanced[i].used &&
		    _neverc_krt_advanced[i].owner == owner &&
		    _neverc_krt_advanced[i].kind == kind) {
			_neverc_krt_spin_unlock_irqrestore(
				&_neverc_krt_advanced_lock, flags);
			return NEVERC_KRT_INTERPOSE_E_CONFLICT;
		}
		if (!_neverc_krt_advanced[i].used && free_slot < 0)
			free_slot = i;
	}
	if (free_slot < 0) {
		_neverc_krt_spin_unlock_irqrestore(
			&_neverc_krt_advanced_lock, flags);
		return NEVERC_KRT_INTERPOSE_E_ALLOC;
	}
	_neverc_krt_advanced[free_slot].owner = owner;
	_neverc_krt_advanced[free_slot].kind = kind;
	_neverc_krt_advanced[free_slot].sequence =
		++_neverc_krt_advanced_sequence;
	_neverc_krt_advanced[free_slot].active = 0;
	_neverc_krt_advanced[free_slot].used = 1;
	*slot_out = free_slot;
	_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_advanced_lock, flags);
	return NEVERC_KRT_INTERPOSE_OK;
}

static void _neverc_krt_advanced_commit(int slot)
{
	unsigned long flags;

	flags = _neverc_krt_spin_lock_irqsave(&_neverc_krt_advanced_lock);
	if (slot >= 0 && slot < _NEVERC_KRT_ADVANCED_MAX &&
	    _neverc_krt_advanced[slot].used)
		_neverc_krt_advanced[slot].active = 1;
	_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_advanced_lock, flags);
}

static void _neverc_krt_advanced_release(void *owner, int kind)
{
	unsigned long flags;
	int i;

	flags = _neverc_krt_spin_lock_irqsave(&_neverc_krt_advanced_lock);
	for (i = 0; i < _NEVERC_KRT_ADVANCED_MAX; i++) {
		if (!_neverc_krt_advanced[i].used ||
		    _neverc_krt_advanced[i].owner != owner ||
		    _neverc_krt_advanced[i].kind != kind)
			continue;
		memset(&_neverc_krt_advanced[i], 0,
		       sizeof(_neverc_krt_advanced[i]));
		break;
	}
	_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_advanced_lock, flags);
}

int neverc_krt_interpose_install(struct neverc_krt_interpose *h, void *target,
			    void *replace, void **orig)
{
	int ret = _neverc_krt_lifecycle_begin();

	if (ret != NEVERC_KRT_INTERPOSE_OK)
		return ret;
	ret = _neverc_krt_ll_install(h, target, replace, orig);
	_neverc_krt_lifecycle_end();
	return ret;
}

static void _neverc_krt_patch_mov64(u32 *page, int slot, int rd, u64 addr)
{
	u32 buf[4];
	int n = neverc_krt_a64_gen_mov64(buf, rd, addr), i;
	for (i = 0; i < n && i < 4; i++)
		page[slot + i] = buf[i];
}

/* The generated stub must never dereference the caller-owned handle after a
 * successful remove.  Its mutable state therefore lives beside the retained
 * executable code.  Keep the public base.enabled field as an observable mirror
 * only; @stub_enabled is authoritative for dispatch. */
static void _neverc_krt_ctx_set_enabled(struct neverc_krt_interpose_ctx *h,
					unsigned long enabled)
{
	if (!h)
		return;
	if (h->stub_enabled)
		__atomic_store_n(h->stub_enabled, enabled, __ATOMIC_RELEASE);
	WRITE_ONCE(h->base.enabled, enabled ? 1 : 0);
}

static void _neverc_krt_ctx_clear_guard(struct neverc_krt_interpose_ctx *h)
{
	int i;

	if (!h)
		return;
	if (!h->stub_guard)
		return;
	for (i = 0; i < _CTX_GUARD_SLOTS; i++) {
		__atomic_store_n(&h->stub_guard[i * 2 + 1], 0UL,
				 __ATOMIC_RELAXED);
		__atomic_store_n(&h->stub_guard[i * 2], 0UL,
				 __ATOMIC_RELEASE);
	}
}

void neverc_krt_interpose_enable_ctx(struct neverc_krt_interpose_ctx *h)
{
	if (_neverc_krt_lifecycle_begin() != NEVERC_KRT_INTERPOSE_OK)
		return;
	if (h && READ_ONCE(h->base.active))
		_neverc_krt_ctx_set_enabled(h, 1);
	_neverc_krt_lifecycle_end();
}

void neverc_krt_interpose_disable_ctx(struct neverc_krt_interpose_ctx *h)
{
	if (_neverc_krt_lifecycle_begin() != NEVERC_KRT_INTERPOSE_OK)
		return;
	_neverc_krt_ctx_set_enabled(h, 0);
	_neverc_krt_lifecycle_end();
}

/*
 * A single "current task" word is not a recursion guard: task B overwrites
 * task A, which then admits A recursively and may clear B on return.  The
 * generated stub calls these helpers only after saving the complete integer
 * context.  A non-zero return is a 1-based ownership token; zero means either
 * recursion or a full set and must bypass the handler.
 */
static noinline unsigned long
_neverc_krt_ctx_guard_enter(volatile unsigned long *slots,
			    unsigned long caller_lr)
{
	unsigned long task;
	int i;

	if (!slots)
		return 0;
	__asm__ __volatile__("mrs %0, sp_el0" : "=r"(task));
	if (!task)
		return 0;

	for (i = 0; i < _CTX_GUARD_SLOTS; i++) {
		if (__atomic_load_n(&slots[i * 2], __ATOMIC_ACQUIRE) == task)
			return 0;
	}
	for (i = 0; i < _CTX_GUARD_SLOTS; i++) {
		unsigned long empty = 0;

		if (__atomic_compare_exchange_n(&slots[i * 2], &empty, task, 0,
						__ATOMIC_ACQ_REL,
						__ATOMIC_ACQUIRE)) {
			/* The owning task cannot reach CALL cleanup until this helper
			 * returns, so release-publish its original entry LR before the
			 * token becomes usable by the handler. */
			__atomic_store_n(&slots[i * 2 + 1], caller_lr,
					 __ATOMIC_RELEASE);
			return (unsigned long)i + 1;
		}
	}
	return 0;
}

static noinline void
_neverc_krt_ctx_guard_leave(volatile unsigned long *slots,
			    unsigned long token)
{
	unsigned long task;
	unsigned long owner;

	if (!slots || token == 0 || token > _CTX_GUARD_SLOTS)
		return;
	__asm__ __volatile__("mrs %0, sp_el0" : "=r"(task));
	if (!task)
		return;
	if (__atomic_load_n(&slots[(token - 1) * 2],
			    __ATOMIC_ACQUIRE) != task)
		return;
	owner = task;
	/* Clear metadata before release-publishing an empty task word.  A new
	 * owner may reuse the slot immediately after the successful CAS. */
	__atomic_store_n(&slots[(token - 1) * 2 + 1], 0UL,
			 __ATOMIC_RELAXED);
	__atomic_compare_exchange_n(&slots[(token - 1) * 2], &owner, 0UL, 0,
				    __ATOMIC_RELEASE, __ATOMIC_RELAXED);
}

/* ~5s minimum fail-safe bound (msleep(1) per iteration); a balanced in-flight
 * lease drains in microseconds, so this is only reached on a real imbalance. */
#define _NEVERC_KRT_DRAIN_MAX_ITERS 5000

static int _neverc_krt_wait_one_ctx_inflight(
		struct neverc_krt_interpose_ctx *h)
{
	volatile unsigned long *inflight;
	unsigned long v;
	int iter;

	if (!h || !(inflight = h->stub_inflight))
		return NEVERC_KRT_INTERPOSE_OK;

	/* SEVL primes the first WFE so it cannot sleep through the event window;
	 * every generated in-flight decrement emits SEV.  A correctly balanced
	 * counter drains within microseconds. */
	__asm__ __volatile__("sevl" ::: "memory");
	for (iter = 0; ; iter++) {
		v = __atomic_load_n(inflight, __ATOMIC_ACQUIRE);
		if (v == 0)
			break;
		/*
		 * Bounded fail-safe.  A balanced lease always reaches zero, so this
		 * bound is never hit in normal teardown.  If it is (a future
		 * accounting imbalance), do not wedge delete_module forever: the
		 * Returning success here would hand owner code back while a handler
		 * or CALL replacement can still be executing it.  Keep the disabled
		 * handle owned and let the caller retry teardown.
		 */
		if (iter >= _NEVERC_KRT_DRAIN_MAX_ITERS) {
			neverc_krt_log_err(
				"interpose drain timeout: ctx=%px inflight=%lu — ownership retained",
				(void *)h, v);
			_neverc_krt_full_barrier();
			return NEVERC_KRT_INTERPOSE_E_PATCH;
		}
		if (_neverc_krt_msleep)
			_neverc_krt_msleep(1);
		else
			__asm__ __volatile__("wfe" ::: "memory");
	}
	_neverc_krt_full_barrier();
	return NEVERC_KRT_INTERPOSE_OK;
}

/*
 * Append an exactly-once release of the persistent ctx in-flight lease.
 * x14-x17 and NZCV are part of the original machine state at the relocated
 * continuation, so use only non-flag-setting instructions and restore all
 * scratch registers before leaving the generated trampoline.
 */
static int _neverc_krt_emit_ctx_inflight_dec(u32 *out, int *mov_slot)
{
	int loop;
	int n = 0;

	out[n++] = _A64E_STP_PRE16(14, 15);
	out[n++] = _A64E_STP_PRE16(16, 17);
	*mov_slot = n;
	out[n++] = _A64E_MOVZ(16, 0);
	out[n++] = _A64E_MOVK16(16);
	out[n++] = _A64E_MOVK32(16);
	out[n++] = _A64E_MOVK48(16);
	loop = n;
	out[n++] = _A64E_LDAXR_XREG(15, 16);
	out[n++] = _A64E_SUB_X_I(15, 15, 1);
	out[n++] = _A64E_STLXR_XREG(17, 15, 16);
	out[n] = _A64E_CBNZ_W(17, loop - n);
	n++;
	out[n++] = _A64E_SEV;
	out[n++] = _A64E_LDP_POST16(16, 17);
	out[n++] = _A64E_LDP_POST16(14, 15);
	return n;
}

static int _neverc_krt_interpose_install_ctx_impl(
				struct neverc_krt_interpose_ctx *h, void *target,
				neverc_krt_ctx_handler_t handler,
				void **call_orig)
{
	target = (void *)neverc_krt_strip_pac((unsigned long)target);
	u32 *code = (u32 *)target;
	u32  tramp[NEVERC_KRT_INTERPOSE_TRAMP_CAP];
	u32  passthrough[NEVERC_KRT_INTERPOSE_TRAMP_CAP];
	u32  kcfi_type_id = 0;
	int  kcfi_mode = NEVERC_KRT_KCFI_MODE_DISABLED;
	int  tidx = 0, pidx = 0, pass_dec_slot = 0;
	int  control_off, skip, patch_count, i, n;
	u32  patch[NEVERC_KRT_INTERPOSE_MAX_PATCH];
	u32  live[NEVERC_KRT_INTERPOSE_MAX_PATCH];
	u32 *page;
	volatile unsigned long *control;

	if (!_neverc_krt_inited) return NEVERC_KRT_INTERPOSE_E_NOINIT;
	if (!h || !target || !handler) return NEVERC_KRT_INTERPOSE_E_SHORT;
	if (READ_ONCE(h->base.active))
		return NEVERC_KRT_INTERPOSE_E_CONFLICT;
	if (((unsigned long)target & 3) != 0) return NEVERC_KRT_INTERPOSE_E_SHORT;
	if (!_neverc_krt_is_kern_ptr((unsigned long)target))
		return NEVERC_KRT_INTERPOSE_E_SHORT;

	u32 ibuf[NEVERC_KRT_INTERPOSE_MAX_PATCH];
	if (neverc_krt_mem_read(ibuf, code, sizeof(ibuf)))
		return NEVERC_KRT_INTERPOSE_E_SHORT;
	/* A compiler-instrumented indirect call to call_orig validates the word
	 * immediately before the destination.  Preserve the target function's
	 * exact type ID so the relocated trampoline remains a valid entry of the
	 * same C type.  Embedded runtime bitcode is built once, so this must use
	 * the live selected profile instead of compile-time profile selection.
	 * Force-jump-only users may omit
	 * call_orig and therefore do not require a C-callable prefix. */
	if (call_orig) {
		kcfi_mode = _neverc_krt_current_kcfi_mode();
		if (kcfi_mode < 0)
			return NEVERC_KRT_INTERPOSE_E_KCFI;
		if (kcfi_mode != NEVERC_KRT_KCFI_MODE_DISABLED) {
			if (!neverc_krt_cfi_has_tag(target) ||
			    neverc_krt_mem_read(&kcfi_type_id, code - 1,
						sizeof(kcfi_type_id)))
				return NEVERC_KRT_INTERPOSE_E_KCFI;
		}
	}

	{
		int entry = neverc_krt_a64_is_bti(ibuf[0]) ? 1 : 0;
		if (neverc_krt_a64_is_kprobe_bp(ibuf[entry]))
			return NEVERC_KRT_INTERPOSE_E_CONFLICT;
	}

	h->base.target = target;
	h->base.replace = (void *)handler;
	h->base.trampoline = (void *)0;
	h->base.active = 0;
	h->base.enabled = 0;
	h->base.short_b = 0;
	__atomic_store_n(&h->base.hit_count, 0, __ATOMIC_RELAXED);
	__atomic_store_n(&h->base.guard, 0, __ATOMIC_RELAXED);
	h->stub = (void *)0;
	h->tramp_code = (void *)0;
	h->passthrough_code = (void *)0;
	h->stub_enabled = (void *)0;
	h->stub_guard = (void *)0;
	h->stub_inflight = (void *)0;

	{
		int qoff = neverc_krt_a64_is_bti(ibuf[0]) ? 1 : 0;
		int chained =
			ibuf[qoff] == NEVERC_KRT_A64_LDR_X16_PC8 &&
			ibuf[qoff + 1] == NEVERC_KRT_A64_BR_X16;

		_neverc_krt_scan_entry(ibuf, &skip, &patch_count);

		if (!chained) {
			unsigned long fn_sz = _neverc_krt_fn_size(target);
			if (fn_sz > 0 &&
			    fn_sz < (unsigned long)patch_count * 4)
				return NEVERC_KRT_INTERPOSE_E_SHORT;
		}

		h->base.patch_count = patch_count;
		for (i = 0; i < patch_count; i++)
			h->base.orig_insns[i] = ibuf[i];

		tramp[tidx++] = NEVERC_KRT_A64_BTI_JC;

		if (chained) {
			unsigned long prev;
			if (neverc_krt_mem_read(&prev, &code[qoff + 2], 8))
				return NEVERC_KRT_INTERPOSE_E_SHORT;
			tidx += neverc_krt_a64_gen_mov64(&tramp[tidx], 17, prev);
			tramp[tidx++] = NEVERC_KRT_A64_BR_X17;
		} else {
			for (i = 0; i < patch_count; i++) {
				u32 insn = ibuf[i];
				if (neverc_krt_a64_is_bti(insn) ||
				    insn == NEVERC_KRT_A64_NOP)
					continue;
				if (neverc_krt_a64_is_pac_sign(insn)) {
					tramp[tidx++] = insn;
					continue;
				}
				if (neverc_krt_a64_is_hazardous(insn))
					return NEVERC_KRT_INTERPOSE_E_RELOC;
				n = neverc_krt_a64_relocate_abs(
					insn, (unsigned long)&code[i],
					&tramp[tidx],
					(unsigned long)code,
					(unsigned long)code +
						(unsigned long)patch_count *
							sizeof(u32));
				if (n == 0) return NEVERC_KRT_INTERPOSE_E_RELOC;
				tidx += n;
				if (tidx >= NEVERC_KRT_INTERPOSE_TRAMP_CAP - 8)
					return NEVERC_KRT_INTERPOSE_E_RELOC;
			}
			unsigned long back =
				(unsigned long)target + patch_count * 4;
			tidx += neverc_krt_a64_gen_mov64(&tramp[tidx], 17, back);
			/* mid-function resume: no BTI_J pad → RET, not BR */
			tramp[tidx++] = NEVERC_KRT_A64_RET_X17;
		}

		/*
		 * call_orig keeps the ordinary trampoline (tramp_code) because a
		 * handler remains leased while it calls and resumes from the
		 * original.  Stub bypasses use this second copy (passthrough_code)
		 * that releases the persistent in-flight lease.
		 *
		 * The lease MUST be released before the relocated original prologue
		 * runs.  By the time control reaches the passthrough the business
		 * handler has already returned, and the stub/passthrough pages are
		 * permanently retained on teardown, so the lease does not need to
		 * span the prologue.  Emitting the decrement *after* the relocated
		 * prologue was a latent leak: a prologue instruction that relocates
		 * to a branch (a tail-call `B`, a taken `B.cond`, `CBZ/TBZ`, ...)
		 * jumps straight to its resolved target and skips the trailing
		 * decrement.  Such a hook then increments its in-flight counter
		 * once per call and never decrements it, so teardown's drain never
		 * observes zero and delete_module hangs forever.  Decrement-first
		 * runs on every exit path (fall-through resume or prologue branch)
		 * and the emitted decrement saves/restores x14-x17, so the
		 * following relocated prologue still sees the original registers.
		 */
		if (tidx + 16 >= NEVERC_KRT_INTERPOSE_TRAMP_CAP)
			return NEVERC_KRT_INTERPOSE_E_RELOC;
		/* Stub bypasses arrive through BR X17, so the secondary entry needs
		 * its own BTI J-compatible landing pad before the lease decrement. */
		passthrough[pidx++] = NEVERC_KRT_A64_BTI_JC;
		{
			int dec_slot;
			int dec_start = pidx;

			pidx += _neverc_krt_emit_ctx_inflight_dec(
				&passthrough[pidx], &dec_slot);
			pass_dec_slot = dec_start + dec_slot;
		}
		for (i = 0; i < tidx; i++)
			passthrough[pidx++] = tramp[i];
		if (pidx > NEVERC_KRT_INTERPOSE_TRAMP_CAP)
			return NEVERC_KRT_INTERPOSE_E_RELOC;
	}

	{
		int code_words = _CTX_STUB_LEN + 4 + tidx + pidx;
		int page_sz;

		control_off = (code_words + 1) & ~1;
		/* enabled + 16 {task, caller_lr} slots + inflight, all u64. */
		page_sz = (control_off + 2 * (2 + _CTX_GUARD_WORDS)) * 4;
		page_sz = (page_sz + 63) & ~63; /* cache-line align */
		if (page_sz < 512) page_sz = 512;
		page = (u32 *)_neverc_krt_alloc_exec(page_sz);
		if (!page) return NEVERC_KRT_INTERPOSE_E_ALLOC;
	}

	h->stub = page;
	h->tramp_code = page + _CTX_STUB_LEN + 4;
	h->passthrough_code = h->tramp_code + tidx;
	control = (volatile unsigned long *)(page + control_off);
	h->stub_enabled = &control[0];
	h->stub_guard = &control[1];
	h->stub_inflight = &control[1 + _CTX_GUARD_WORDS];
	__atomic_store_n(h->stub_enabled, 0UL, __ATOMIC_RELAXED);
	for (i = 0; i < _CTX_GUARD_WORDS; i++)
		__atomic_store_n(&h->stub_guard[i], 0UL, __ATOMIC_RELAXED);
	__atomic_store_n(h->stub_inflight, 0UL, __ATOMIC_RELAXED);
	h->base.trampoline = page;
	/* Keep the 16-byte entry-prefix reservation deterministic.  KCFI reads
	 * tramp_code[-1], while the preceding words remain padding. */
	for (i = 1; i <= 4; i++)
		h->tramp_code[-i] = 0;
	h->tramp_code[-1] = kcfi_type_id;

	for (i = 0; i < (int)_CTX_STUB_LEN; i++)
		page[i] = _neverc_krt_ctx_stub_template[i];

	_neverc_krt_patch_mov64(page, _CTX_INFLIGHT_SLOT_ENTER, 16,
			 (u64)(unsigned long)h->stub_inflight);
	_neverc_krt_patch_mov64(page, _CTX_ENABLED_SLOT, 16,
			 (u64)(unsigned long)h->stub_enabled);
	_neverc_krt_patch_mov64(page, _CTX_GUARD_SLOT_A, 0,
			 (u64)(unsigned long)h->stub_guard);
	_neverc_krt_patch_mov64(page, _CTX_GUARD_ENTER_SLOT, 3,
			 (u64)(unsigned long)_neverc_krt_ctx_guard_enter);
	_neverc_krt_patch_mov64(page, _CTX_GUARD_SLOT_B, 0,
			 (u64)(unsigned long)h->stub_guard);
	_neverc_krt_patch_mov64(page, _CTX_GUARD_LEAVE_SLOT, 3,
			 (u64)(unsigned long)_neverc_krt_ctx_guard_leave);
	_neverc_krt_patch_mov64(page, _CTX_HANDLER_SLOT, 3,
			 (u64)(unsigned long)handler);
	_neverc_krt_patch_mov64(page, _CTX_TRAMP_SLOT_A, 17,
			 (u64)(unsigned long)h->passthrough_code);
	_neverc_krt_patch_mov64(page, _CTX_INFLIGHT_SLOT_DIRECT, 16,
			 (u64)(unsigned long)h->stub_inflight);
	_neverc_krt_patch_mov64(page, _CTX_GUARD_SLOT_CALL, 9,
			 (u64)(unsigned long)h->stub_guard);
	_neverc_krt_patch_mov64(page, _CTX_INFLIGHT_SLOT_CALL, 16,
			 (u64)(unsigned long)h->stub_inflight);
	_neverc_krt_patch_mov64(page, _CTX_TRAMP_SLOT_B, 17,
			 (u64)(unsigned long)h->passthrough_code);

	for (i = 0; i < tidx; i++)
		h->tramp_code[i] = tramp[i];
	for (i = 0; i < pidx; i++)
		h->passthrough_code[i] = passthrough[i];
	_neverc_krt_patch_mov64(h->passthrough_code, pass_dec_slot, 16,
			 (u64)(unsigned long)h->stub_inflight);

	{
		unsigned long flush_end =
			(unsigned long)&h->passthrough_code[pidx];
		_neverc_krt_dcache_clean((unsigned long)page, flush_end);
		if (_neverc_krt_flushic)
			_neverc_krt_flushic((unsigned long)page, flush_end);
		else
			_neverc_krt_icache_inval((unsigned long)page, flush_end);
	}

	if (_neverc_krt_set_memory_x &&
	    _neverc_krt_set_memory_x((unsigned long)page, 1) != 0) {
		if (_neverc_krt_modfree)
			_neverc_krt_modfree(page);
		h->stub = (void *)0;
		h->tramp_code = (void *)0;
		h->passthrough_code = (void *)0;
		h->stub_enabled = (void *)0;
		h->stub_guard = (void *)0;
		h->stub_inflight = (void *)0;
		h->base.trampoline = (void *)0;
		return NEVERC_KRT_INTERPOSE_E_ALLOC;
	}

	/*
	 * Publish call_orig and enabled BEFORE patching the entry.  Once the first
	 * target CPU consumes the new branch, the handler must never observe a
	 * NULL original trampoline.  The generated code is already complete and
	 * I-cache coherent here.
	 */
	if (call_orig)
		__atomic_store_n(call_orig, (void *)h->tramp_code,
				 __ATOMIC_RELEASE);
	h->base.active = 1;
	_neverc_krt_ctx_set_enabled(h, 1);
	_neverc_krt_full_barrier();

	for (i = 0; i < patch_count; i++)
		patch[i] = NEVERC_KRT_A64_NOP;
	{
		int jmp = 0;
		if (neverc_krt_a64_is_bti(ibuf[0])) {
			patch[0] = ibuf[0];
			jmp = 1;
		}
		patch[jmp + 0] = NEVERC_KRT_A64_LDR_X16_PC8;
		patch[jmp + 1] = NEVERC_KRT_A64_BR_X16;
		patch[jmp + 2] = (u32)(unsigned long)h->stub;
		patch[jmp + 3] =
			(u32)((unsigned long)h->stub >> 32);
	}

	/*
	 * Another NeverC interposer may have inspected this entry while this
	 * trampoline was being built.  Serialize the final compare-and-publish
	 * transaction and reject a stale snapshot instead of silently stacking a
	 * trampoline whose original path no longer matches the live text.
	 */
	_neverc_krt_spin_lock(&_neverc_krt_patch_lock);
	if (neverc_krt_mem_read(live, code,
			       (size_t)patch_count * sizeof(u32)) ||
	    memcmp(live, ibuf, (size_t)patch_count * sizeof(u32))) {
		_neverc_krt_spin_unlock(&_neverc_krt_patch_lock);
		_neverc_krt_ctx_set_enabled(h, 0);
		if (call_orig)
			__atomic_store_n(call_orig, (void *)0, __ATOMIC_RELEASE);
		_neverc_krt_full_barrier();
		h->base.active = 0;
		if (_neverc_krt_modfree)
			_neverc_krt_modfree(page);
		memset(h, 0, sizeof(*h));
		return NEVERC_KRT_INTERPOSE_E_PATCH;
	}

	if (_neverc_krt_patch_multi(code, patch, patch_count) != 0) {
		int drained;
		int restored;

		/* Publication may have succeeded even when its final verification
		 * failed.  A CPU can therefore hold the generated entry address before
		 * executing its first instruction.  Disable dispatch, synchronously
		 * restore and verify the target, drain exact leases, and permanently retire this
		 * page; never free it on a post-publication failure. */
		_neverc_krt_ctx_set_enabled(h, 0);
		_neverc_krt_full_barrier();
		__asm__ __volatile__("sev" ::: "memory");
		restored = _neverc_krt_restore_ctx_text_unchecked(h);
		_neverc_krt_spin_unlock(&_neverc_krt_patch_lock);
		drained = _neverc_krt_wait_one_ctx_inflight(h);
		if (drained == NEVERC_KRT_INTERPOSE_OK)
			_neverc_krt_ctx_clear_guard(h);
		if (restored == 0 && drained == NEVERC_KRT_INTERPOSE_OK) {
			if (call_orig)
				__atomic_store_n(call_orig, (void *)0,
						 __ATOMIC_RELEASE);
			_neverc_krt_release_ctx_owner(h);
			h->base.active = 0;
			h->base.target = (void *)0;
			h->base.replace = (void *)0;
			h->base.patch_count = 0;
		}
		return NEVERC_KRT_INTERPOSE_E_PATCH;
	}
	_neverc_krt_spin_unlock(&_neverc_krt_patch_lock);
	__atomic_fetch_add(&_neverc_krt_interpose_install_cnt, 1, __ATOMIC_RELAXED);
	return NEVERC_KRT_INTERPOSE_OK;
}

int neverc_krt_interpose_install_ctx(struct neverc_krt_interpose_ctx *h,
				void *target,
				neverc_krt_ctx_handler_t handler,
				void **call_orig)
{
	int ret = _neverc_krt_lifecycle_begin();
	int owner_slot;

	if (ret != NEVERC_KRT_INTERPOSE_OK)
		return ret;
	ret = _neverc_krt_advanced_reserve(
		h, _NEVERC_KRT_ADVANCED_CTX, &owner_slot);
	if (ret != NEVERC_KRT_INTERPOSE_OK) {
		_neverc_krt_lifecycle_end();
		return ret;
	}
	ret = _neverc_krt_interpose_install_ctx_impl(
		h, target, handler, call_orig);
	if (h && READ_ONCE(h->base.active))
		_neverc_krt_advanced_commit(owner_slot);
	else
		_neverc_krt_advanced_release(
			h, _NEVERC_KRT_ADVANCED_CTX);
	_neverc_krt_lifecycle_end();
	return ret;
}

/*
 * Low-level neverc_krt_interpose_install() is backed by install_ctx so every
 * hook — SDK internals and advanced callers — lands on the BTI-safe stub path.
 */
#define NEVERC_KRT_LL_MAX 32

struct neverc_krt_ll_slot {
	struct neverc_krt_interpose     *owner;
	struct neverc_krt_interpose_ctx  ctx;
	unsigned int                     used;
};

static struct neverc_krt_ll_slot _neverc_krt_ll[NEVERC_KRT_LL_MAX];

static void _neverc_krt_ll_invoke(int slot, neverc_krt_reg_ctx *ctx)
{
	struct neverc_krt_interpose *h =
		READ_ONCE(_neverc_krt_ll[slot].owner);
	void *replacement = (void *)0;
	void *trampoline;

	/*
	 * Redirect only after the context stub restores the original register
	 * state.  Calling replacement through a fixed C prototype would silently
	 * drop X6/X7, indirect-result state in X8, and non-scalar return values.
	 */
	if (h && READ_ONCE(h->enabled))
		replacement = READ_ONCE(h->replace);
	trampoline = READ_ONCE(_neverc_krt_ll[slot].ctx.tramp_code);

	if (replacement)
		NEVERC_KRT_CTX_REDIRECT(ctx, replacement);
	else if (trampoline)
		NEVERC_KRT_CTX_FORCE_JUMP(ctx, trampoline);
	else
		NEVERC_KRT_CTX_SKIP_VOID(ctx);
}

#include "nvk_interpose_ll_handlers.inc"

static int _neverc_krt_ll_alloc_slot(void)
{
	int i;
	for (i = 0; i < NEVERC_KRT_LL_MAX; i++) {
		unsigned int expected = 0;
		if (__atomic_compare_exchange_n(&_neverc_krt_ll[i].used,
						&expected, 1, 0,
						__ATOMIC_ACQ_REL,
						__ATOMIC_RELAXED))
			return i;
	}
	return -1;
}

static int _neverc_krt_ll_find_slot(struct neverc_krt_interpose *h)
{
	int i;
	for (i = 0; i < NEVERC_KRT_LL_MAX; i++) {
		if (__atomic_load_n(&_neverc_krt_ll[i].used, __ATOMIC_ACQUIRE) &&
		    READ_ONCE(_neverc_krt_ll[i].owner) == h)
			return i;
	}
	return -1;
}

static void _neverc_krt_ll_set_dispatch(struct neverc_krt_interpose *h,
					unsigned long enabled)
{
	int slot;

	if (!h)
		return;
	WRITE_ONCE(h->enabled, enabled ? 1 : 0);
	slot = _neverc_krt_ll_find_slot(h);
	if (slot >= 0)
		_neverc_krt_ctx_set_enabled(&_neverc_krt_ll[slot].ctx, enabled);
}

static void _neverc_krt_ll_disable_dispatch(struct neverc_krt_interpose *h)
{
	_neverc_krt_ll_set_dispatch(h, 0);
}

NEVERC_KRT_INTERPOSE_FORCE_INLINE void
neverc_krt_interpose_enable(struct neverc_krt_interpose *h)
{
	if (_neverc_krt_lifecycle_begin() != NEVERC_KRT_INTERPOSE_OK)
		return;
	_neverc_krt_ll_set_dispatch(h, 1);
	_neverc_krt_lifecycle_end();
}

NEVERC_KRT_INTERPOSE_FORCE_INLINE void
neverc_krt_interpose_disable(struct neverc_krt_interpose *h)
{
	if (_neverc_krt_lifecycle_begin() != NEVERC_KRT_INTERPOSE_OK)
		return;
	_neverc_krt_ll_set_dispatch(h, 0);
	_neverc_krt_lifecycle_end();
}

static void _neverc_krt_ll_release_slot(int slot)
{
	WRITE_ONCE(_neverc_krt_ll[slot].owner, (void *)0);
	__atomic_store_n(&_neverc_krt_ll[slot].used, 0, __ATOMIC_RELEASE);
}

static void _neverc_krt_ll_sync_from_ctx(struct neverc_krt_interpose *h,
					 struct neverc_krt_interpose_ctx *c,
					 void *replace)
{
	int i;
	void *target = c->base.target;

	h->target = target;
	h->replace = replace;
	h->trampoline = (u32 *)c->tramp_code;
	h->patch_count = c->base.patch_count;
	h->short_b = 0;
	h->active = c->base.active;
	for (i = 0; i < c->base.patch_count; i++)
		h->orig_insns[i] = c->base.orig_insns[i];
}

static noinline int _neverc_krt_ctx_install_retained(
		struct neverc_krt_interpose_ctx *ctx, int install_ret)
{
	return install_ret != NEVERC_KRT_INTERPOSE_OK && ctx &&
		READ_ONCE(ctx->base.active);
}

static int _neverc_krt_ll_install(struct neverc_krt_interpose *h, void *target,
				  void *replace, void **orig)
{
	int slot, ret;

	if (!_neverc_krt_inited) return NEVERC_KRT_INTERPOSE_E_NOINIT;
	if (!h || !target || !replace || !orig) return NEVERC_KRT_INTERPOSE_E_SHORT;
	if (h->active) return NEVERC_KRT_INTERPOSE_E_CONFLICT;

	slot = _neverc_krt_ll_alloc_slot();
	if (slot < 0) return NEVERC_KRT_INTERPOSE_E_ALLOC;

	WRITE_ONCE(_neverc_krt_ll[slot].owner, h);

	h->target = (void *)neverc_krt_strip_pac((unsigned long)target);
	h->replace = replace;
	h->trampoline = (void *)0;
	h->active = 0;
	h->short_b = 0;
	WRITE_ONCE(h->enabled, 1);
	__atomic_store_n(&h->hit_count, 0, __ATOMIC_RELAXED);
	__atomic_store_n(&h->guard, 0, __ATOMIC_RELAXED);

	ret = _neverc_krt_interpose_install_ctx_impl(
					       &_neverc_krt_ll[slot].ctx,
					       h->target,
					       _neverc_krt_ll_handlers[slot],
					       orig);
	if (ret != NEVERC_KRT_INTERPOSE_OK) {
		/* A post-publication E_PATCH can retain a disabled active context
		 * when restoring the entry also failed.  Preserve the slot/owner and
		 * mirror enough state into @h for interpose_remove() to retry. */
		if (_neverc_krt_ctx_install_retained(
				&_neverc_krt_ll[slot].ctx, ret)) {
			_neverc_krt_ll_sync_from_ctx(
				h, &_neverc_krt_ll[slot].ctx, replace);
			WRITE_ONCE(h->enabled, 0);
			return ret;
		}
		_neverc_krt_ll_release_slot(slot);
		h->target = (void *)0;
		h->replace = (void *)0;
		h->trampoline = (void *)0;
		h->patch_count = 0;
		h->active = 0;
		WRITE_ONCE(h->enabled, 0);
		return ret;
	}

	_neverc_krt_ll_sync_from_ctx(h, &_neverc_krt_ll[slot].ctx, replace);
	return NEVERC_KRT_INTERPOSE_OK;
}

static int _neverc_krt_ll_remove(struct neverc_krt_interpose *h)
{
	int slot;
	int ret;

	if (!h || !h->active)
		return NEVERC_KRT_INTERPOSE_OK;

	slot = _neverc_krt_ll_find_slot(h);
	if (slot < 0)
		return NEVERC_KRT_INTERPOSE_E_CONFLICT;

	WRITE_ONCE(h->enabled, 0);
	{
		struct neverc_krt_interpose_ctx *one =
			&_neverc_krt_ll[slot].ctx;
		ret = _neverc_krt_interpose_remove_ctx_many_impl(&one, 1);
	}
	if (ret != NEVERC_KRT_INTERPOSE_OK ||
	    _neverc_krt_ll[slot].ctx.base.active) {
		/* E_PATCH contract is active-but-disabled so the retained stub can
		 * only bypass to the original while its owner retries teardown. */
		WRITE_ONCE(h->enabled, 0);
		return ret != NEVERC_KRT_INTERPOSE_OK ?
			ret : NEVERC_KRT_INTERPOSE_E_PATCH;
	}
	__atomic_store_n(&h->guard, 0, __ATOMIC_RELEASE);
	_neverc_krt_ll_release_slot(slot);
	h->active = 0;
	h->trampoline = (void *)0;
	return NEVERC_KRT_INTERPOSE_OK;
}

static int _neverc_krt_ll_replace(struct neverc_krt_interpose *h,
				  void *new_replace, void **new_orig)
{
	int slot;
	int ret;

	if (!h || !h->active || !new_replace)
		return -1;

	slot = _neverc_krt_ll_find_slot(h);
	if (slot < 0)
		return -1;

	WRITE_ONCE(h->enabled, 0);
	_neverc_krt_full_barrier();
	ret = _neverc_krt_wait_one_ctx_inflight(&_neverc_krt_ll[slot].ctx);
	if (ret != NEVERC_KRT_INTERPOSE_OK)
		return ret;
	WRITE_ONCE(h->replace, new_replace);
	if (new_orig)
		*new_orig = (void *)_neverc_krt_ll[slot].ctx.tramp_code;
	WRITE_ONCE(h->enabled, 1);
	return 0;
}

static void _neverc_krt_full_barrier(void)
{
	__asm__ __volatile__("dsb ish" ::: "memory");
	__asm__ __volatile__("isb" ::: "memory");
}

static void _neverc_krt_quiesce(void)
{
	if (_neverc_krt_syncrcu) {
		_neverc_krt_syncrcu();
		_neverc_krt_full_barrier();
		return;
	}
	if (_neverc_krt_msleep)
		_neverc_krt_msleep(100);
	_neverc_krt_full_barrier();
}

int neverc_krt_interpose_pause(struct neverc_krt_interpose *h)
{
	int slot;
	int ret;

	if (!h)
		return NEVERC_KRT_INTERPOSE_E_NOINIT;
	ret = _neverc_krt_lifecycle_begin();
	if (ret != NEVERC_KRT_INTERPOSE_OK)
		return ret;
	WRITE_ONCE(h->enabled, 0);
	slot = _neverc_krt_ll_find_slot(h);
	if (slot >= 0) {
		/* The public mirror only stops replacement selection after the LL
		 * dispatcher has already entered.  Disable the persistent ctx as well,
		 * then wait until every replacement and callOrig path has returned. */
		_neverc_krt_ctx_set_enabled(&_neverc_krt_ll[slot].ctx, 0);
		_neverc_krt_full_barrier();
		ret = _neverc_krt_wait_one_ctx_inflight(&_neverc_krt_ll[slot].ctx);
	} else {
		_neverc_krt_full_barrier();
		_neverc_krt_quiesce();
		_neverc_krt_full_barrier();
		ret = NEVERC_KRT_INTERPOSE_OK;
	}
	_neverc_krt_lifecycle_end();
	return ret;
}

int neverc_krt_interpose_remove(struct neverc_krt_interpose *h)
{
	int ret = _neverc_krt_lifecycle_begin();

	if (ret != NEVERC_KRT_INTERPOSE_OK)
		return ret;
	ret = _neverc_krt_ll_remove(h);
	_neverc_krt_lifecycle_end();
	return ret;
}

int neverc_krt_interpose_replace(struct neverc_krt_interpose *h, void *new_replace,
			    void **new_orig)
{
	int ret = _neverc_krt_lifecycle_begin();

	if (ret != NEVERC_KRT_INTERPOSE_OK)
		return ret;
	ret = _neverc_krt_ll_replace(h, new_replace, new_orig);
	_neverc_krt_lifecycle_end();
	return ret;
}

static int _neverc_krt_build_ctx_entry_patch(
		const struct neverc_krt_interpose_ctx *h,
		u32 patch[NEVERC_KRT_INTERPOSE_MAX_PATCH])
{
	unsigned long stub;
	int i;
	int jmp = 0;

	if (!h || !h->base.active || !h->base.target)
		return -1;
	if (h->base.patch_count <= 0 ||
	    h->base.patch_count > NEVERC_KRT_INTERPOSE_MAX_PATCH ||
	    !h->stub)
		return -1;

	for (i = 0; i < h->base.patch_count; i++)
		patch[i] = NEVERC_KRT_A64_NOP;
	if (neverc_krt_a64_is_bti(h->base.orig_insns[0])) {
		patch[0] = h->base.orig_insns[0];
		jmp = 1;
	}
	if (h->base.patch_count - jmp < 4)
		return -1;
	stub = (unsigned long)h->stub;
	patch[jmp] = NEVERC_KRT_A64_LDR_X16_PC8;
	patch[jmp + 1] = NEVERC_KRT_A64_BR_X16;
	patch[jmp + 2] = (u32)stub;
	patch[jmp + 3] = (u32)(stub >> 32);
	return 0;
}

static int _neverc_krt_restore_ctx_text_unchecked(
		struct neverc_krt_interpose_ctx *h)
{
	u32 *code = (u32 *)h->base.target;

	if (_neverc_krt_patch_multi(code, h->base.orig_insns,
				    h->base.patch_count) != 0)
		return -1;
	_neverc_krt_tlbi_range((unsigned long)code,
			(unsigned long)&code[h->base.patch_count]);
	return 0;
}

static int _neverc_krt_restore_ctx_text(struct neverc_krt_interpose_ctx *h)
{
	u32 expected[NEVERC_KRT_INTERPOSE_MAX_PATCH];
	u32 live[NEVERC_KRT_INTERPOSE_MAX_PATCH];
	size_t bytes;
	int ret = -1;

	if (!h || !h->base.active || !h->base.target ||
	    h->base.patch_count <= 0 ||
	    h->base.patch_count > NEVERC_KRT_INTERPOSE_MAX_PATCH)
		return -1;
	bytes = (size_t)h->base.patch_count * sizeof(u32);

	_neverc_krt_spin_lock(&_neverc_krt_patch_lock);
	if (neverc_krt_mem_read(live, h->base.target, bytes))
		goto out;
	if (!memcmp(live, h->base.orig_insns, bytes)) {
		ret = 0;
		goto out;
	}
	if (_neverc_krt_build_ctx_entry_patch(h, expected) ||
	    memcmp(live, expected, bytes)) {
		neverc_krt_log_err(
			"interpose restore refused: target=%px entry ownership lost",
			h->base.target);
		goto out;
	}
	ret = _neverc_krt_restore_ctx_text_unchecked(h);
out:
	_neverc_krt_spin_unlock(&_neverc_krt_patch_lock);
	return ret;
}

static void _neverc_krt_release_ctx_owner(struct neverc_krt_interpose_ctx *h)
{
	if (!h || !h->stub)
		return;
	/*
	 * The target branch and the first stub instruction are not one atomic
	 * operation.  A CPU may arrive here after drain observed zero; therefore
	 * the disabled stub, passthrough trampoline and control words are permanent
	 * runtime-owned memory.  Clear only caller-visible ownership pointers.
	 */
	h->stub = (void *)0;
	h->tramp_code = (void *)0;
	h->passthrough_code = (void *)0;
	h->stub_enabled = (void *)0;
	h->stub_guard = (void *)0;
	h->stub_inflight = (void *)0;
	h->base.trampoline = (void *)0;
}

/*
 * Batch teardown invariant:
 *   ① disable all → ② restore all entry text → ③ exact in-flight drain →
 *   ④ hand caller ownership back while retaining safe stubs. A failed entry
 *   stays patched but disabled,
 *   so no peer can dispatch a new handler while restored trampolines are
 *   released. Never run per-hook expedited RCU storms on hot paths.
 */
static int _neverc_krt_interpose_remove_ctx_many_impl(
					 struct neverc_krt_interpose_ctx **list,
					 int count)
{
#define NVK_RM_CTX_TYPE struct neverc_krt_interpose_ctx
#define NVK_RM_CTX_AT(index) (list[(index)])
#define NVK_RM_IS_ACTIVE(h) ((h) && (h)->base.active)
#define NVK_RM_DISABLE(h) do { \
	_neverc_krt_ctx_set_enabled((h), 0); \
} while (0)
#define NVK_RM_BARRIER() _neverc_krt_full_barrier()
#define NVK_RM_WAKE() __asm__ __volatile__("sev" ::: "memory")
#define NVK_RM_RESTORE(h) _neverc_krt_restore_ctx_text(h)
#define NVK_RM_DRAIN(h) _neverc_krt_wait_one_ctx_inflight(h)
#define NVK_RM_CLEAR_GUARD(h) _neverc_krt_ctx_clear_guard(h)
#define NVK_RM_RELEASE(h) do { \
	_neverc_krt_release_ctx_owner(h); \
	(h)->base.active = 0; \
	__atomic_fetch_add(&_neverc_krt_interpose_remove_cnt, 1, \
			   __ATOMIC_RELAXED); \
} while (0)
#define NVK_RM_OK NEVERC_KRT_INTERPOSE_OK
#define NVK_RM_E_PATCH NEVERC_KRT_INTERPOSE_E_PATCH

#include "nvk_interpose_remove_ctx_many.inc"

#undef NVK_RM_E_PATCH
#undef NVK_RM_OK
#undef NVK_RM_RELEASE
#undef NVK_RM_CLEAR_GUARD
#undef NVK_RM_DRAIN
#undef NVK_RM_RESTORE
#undef NVK_RM_WAKE
#undef NVK_RM_BARRIER
#undef NVK_RM_DISABLE
#undef NVK_RM_IS_ACTIVE
#undef NVK_RM_CTX_AT
#undef NVK_RM_CTX_TYPE
	return ret;
}

int neverc_krt_interpose_remove_ctx_many(struct neverc_krt_interpose_ctx **list,
					 int count)
{
	int ret = _neverc_krt_lifecycle_begin();

	if (ret != NEVERC_KRT_INTERPOSE_OK)
		return ret;
	ret = _neverc_krt_interpose_remove_ctx_many_impl(list, count);
	if (list && count > 0) {
		int i;

		for (i = 0; i < count; i++) {
			if (list[i] && !READ_ONCE(list[i]->base.active))
				_neverc_krt_advanced_release(
					list[i], _NEVERC_KRT_ADVANCED_CTX);
		}
	}
	_neverc_krt_lifecycle_end();
	return ret;
}

int neverc_krt_interpose_remove_ctx(struct neverc_krt_interpose_ctx *h)
{
	struct neverc_krt_interpose_ctx *one = h;

	if (!h || !h->base.active)
		return NEVERC_KRT_INTERPOSE_OK;
	return neverc_krt_interpose_remove_ctx_many(&one, 1);
}

int neverc_krt_interpose_replace_ctx(struct neverc_krt_interpose_ctx *h,
				neverc_krt_ctx_handler_t new_handler)
{
	int ret = _neverc_krt_lifecycle_begin();

	if (ret != NEVERC_KRT_INTERPOSE_OK)
		return ret;
	if (!h || !h->base.active || !h->stub || !new_handler) {
		_neverc_krt_lifecycle_end();
		return -1;
	}
	_neverc_krt_ctx_set_enabled(h, 0);
	_neverc_krt_full_barrier();
	ret = _neverc_krt_wait_one_ctx_inflight(h);
	if (ret != NEVERC_KRT_INTERPOSE_OK) {
		_neverc_krt_lifecycle_end();
		return ret;
	}
	WRITE_ONCE(h->base.replace, (void *)new_handler);
	_neverc_krt_patch_mov64(h->stub, _CTX_HANDLER_SLOT, 3,
			 (u64)(unsigned long)new_handler);
	_neverc_krt_dcache_clean((unsigned long)&h->stub[_CTX_HANDLER_SLOT],
			  (unsigned long)&h->stub[_CTX_HANDLER_SLOT + 4]);
	if (_neverc_krt_flushic)
		_neverc_krt_flushic((unsigned long)&h->stub[_CTX_HANDLER_SLOT],
			     (unsigned long)&h->stub[_CTX_HANDLER_SLOT + 4]);
	else
		_neverc_krt_icache_inval(
			(unsigned long)&h->stub[_CTX_HANDLER_SLOT],
			(unsigned long)&h->stub[_CTX_HANDLER_SLOT + 4]);
	_neverc_krt_full_barrier();
	_neverc_krt_ctx_set_enabled(h, 1);
	_neverc_krt_lifecycle_end();
	return 0;
}

static int _neverc_krt_interpose_install_ctx_batch_impl(
				      struct neverc_krt_interpose_ctx_batch *batch,
				      int count)
{
	int i, ok = 0;

	if (count == 0)
		return 0;
	if (!batch || count < 0)
		return -1;
	for (i = 0; i < count; i++) {
		int owner_slot;

		batch[i].result = _neverc_krt_advanced_reserve(
			batch[i].interpose, _NEVERC_KRT_ADVANCED_CTX,
			&owner_slot);
		if (batch[i].result != NEVERC_KRT_INTERPOSE_OK)
			continue;
		batch[i].result = _neverc_krt_interpose_install_ctx_impl(
				batch[i].interpose, batch[i].target,
				batch[i].handler, batch[i].call_orig);
		if (batch[i].interpose &&
		    READ_ONCE(batch[i].interpose->base.active))
			_neverc_krt_advanced_commit(owner_slot);
		else
			_neverc_krt_advanced_release(
				batch[i].interpose, _NEVERC_KRT_ADVANCED_CTX);
		if (batch[i].result == NEVERC_KRT_INTERPOSE_OK) ok++;
	}
	if (ok > 0 && ok < count) {
#define NVK_IB_COUNT count
#define NVK_IB_IS_SUCCESS(index) \
	(batch[(index)].result == NEVERC_KRT_INTERPOSE_OK)
#define NVK_IB_DISABLE(index) \
	neverc_krt_interpose_disable_ctx(batch[(index)].interpose)
#define NVK_IB_QUIESCE() do { \
	_neverc_krt_full_barrier(); \
	__asm__ __volatile__("sev" ::: "memory"); \
} while (0)
#define NVK_IB_REMOVE_CHUNK(indices, n) do { \
	struct neverc_krt_interpose_ctx *nvk_ib_undo[64]; \
	int nvk_ib_j; \
	for (nvk_ib_j = 0; nvk_ib_j < (n); nvk_ib_j++) \
		nvk_ib_undo[nvk_ib_j] = \
			batch[(indices)[nvk_ib_j]].interpose; \
	(void)_neverc_krt_interpose_remove_ctx_many_impl(nvk_ib_undo, (n)); \
} while (0)
#define NVK_IB_IS_ACTIVE(index) \
	(batch[(index)].interpose && \
	 READ_ONCE(batch[(index)].interpose->base.active))
#define NVK_IB_CLEAR_ORIG(index) do { \
	if (batch[(index)].call_orig) \
		__atomic_store_n(batch[(index)].call_orig, (void *)0, \
				 __ATOMIC_RELEASE); \
} while (0)
#define NVK_IB_SET_RESULT(index, value) \
	(batch[(index)].result = (value))
#define NVK_IB_E_PATCH NEVERC_KRT_INTERPOSE_E_PATCH
#define NVK_IB_E_ROLLBACK NEVERC_KRT_INTERPOSE_E_ROLLBACK

#include "nvk_interpose_install_rollback.inc"

#undef NVK_IB_E_ROLLBACK
#undef NVK_IB_E_PATCH
#undef NVK_IB_SET_RESULT
#undef NVK_IB_CLEAR_ORIG
#undef NVK_IB_IS_ACTIVE
#undef NVK_IB_REMOVE_CHUNK
#undef NVK_IB_QUIESCE
#undef NVK_IB_DISABLE
#undef NVK_IB_IS_SUCCESS
#undef NVK_IB_COUNT
		for (i = 0; i < count; i++) {
			if (batch[i].interpose &&
			    !READ_ONCE(batch[i].interpose->base.active))
				_neverc_krt_advanced_release(
					batch[i].interpose,
					_NEVERC_KRT_ADVANCED_CTX);
		}
		return -1;
	}
	return ok == count ? 0 : -1;
}

int neverc_krt_interpose_install_ctx_batch(
				      struct neverc_krt_interpose_ctx_batch *batch,
				      int count)
{
	int ret = _neverc_krt_lifecycle_begin();

	if (ret != NEVERC_KRT_INTERPOSE_OK)
		return ret;
	ret = _neverc_krt_interpose_install_ctx_batch_impl(batch, count);
	_neverc_krt_lifecycle_end();
	return ret;
}

static int _neverc_krt_interpose_install_batch_impl(
				      struct neverc_krt_interpose_batch *batch,
				      int count)
{
	int i, ok = 0;

	if (count == 0)
		return 0;
	if (!batch || count < 0)
		return -1;
	for (i = 0; i < count; i++) {
		batch[i].result = _neverc_krt_ll_install(
			batch[i].interpose, batch[i].target,
			batch[i].replace, batch[i].orig);
		if (batch[i].result == NEVERC_KRT_INTERPOSE_OK) ok++;
	}
	if (ok > 0 && ok < count) {
#define NVK_IB_COUNT count
#define NVK_IB_IS_SUCCESS(index) \
	(batch[(index)].result == NEVERC_KRT_INTERPOSE_OK)
#define NVK_IB_DISABLE(index) \
	_neverc_krt_ll_disable_dispatch(batch[(index)].interpose)
#define NVK_IB_QUIESCE() do { \
	_neverc_krt_full_barrier(); \
	__asm__ __volatile__("sev" ::: "memory"); \
} while (0)
#define NVK_IB_REMOVE_CHUNK(indices, n) do { \
	int nvk_ib_j; \
	for (nvk_ib_j = 0; nvk_ib_j < (n); nvk_ib_j++) \
		(void)_neverc_krt_ll_remove( \
			batch[(indices)[nvk_ib_j]].interpose); \
} while (0)
#define NVK_IB_IS_ACTIVE(index) \
	(batch[(index)].interpose && \
	 READ_ONCE(batch[(index)].interpose->active))
#define NVK_IB_CLEAR_ORIG(index) do { \
	if (batch[(index)].orig) \
		__atomic_store_n(batch[(index)].orig, (void *)0, \
				 __ATOMIC_RELEASE); \
} while (0)
#define NVK_IB_SET_RESULT(index, value) \
	(batch[(index)].result = (value))
#define NVK_IB_E_PATCH NEVERC_KRT_INTERPOSE_E_PATCH
#define NVK_IB_E_ROLLBACK NEVERC_KRT_INTERPOSE_E_ROLLBACK

#include "nvk_interpose_install_rollback.inc"

#undef NVK_IB_E_ROLLBACK
#undef NVK_IB_E_PATCH
#undef NVK_IB_SET_RESULT
#undef NVK_IB_CLEAR_ORIG
#undef NVK_IB_IS_ACTIVE
#undef NVK_IB_REMOVE_CHUNK
#undef NVK_IB_QUIESCE
#undef NVK_IB_DISABLE
#undef NVK_IB_IS_SUCCESS
#undef NVK_IB_COUNT
		return -1;
	}
	return ok == count ? 0 : -1;
}

int neverc_krt_interpose_install_batch(struct neverc_krt_interpose_batch *batch,
				       int count)
{
	int ret = _neverc_krt_lifecycle_begin();

	if (ret != NEVERC_KRT_INTERPOSE_OK)
		return ret;
	ret = _neverc_krt_interpose_install_batch_impl(batch, count);
	_neverc_krt_lifecycle_end();
	return ret;
}

int neverc_krt_cfi_make_thunk(struct neverc_krt_cfi_thunk *thunk,
			      void *orig_func, void *new_func)
{
	u32 tag;
	int n;

	if (!thunk || !orig_func || !new_func)
		return -1;

	tag = neverc_krt_cfi_read_tag(orig_func);
	thunk->tag = tag;
	n = 0;
	thunk->code[n++] = NEVERC_KRT_A64_BTI_JC;
	n += neverc_krt_a64_gen_mov64(&thunk->code[n], 17,
				(u64)neverc_krt_strip_pac((unsigned long)new_func));
	/* RET X17 is BTI-exempt.  BR X17 requires a BTI J/JC landing pad;
	 * kernel / NeverC entries are typically BTI C or PACIASP. */
	thunk->code[n++] = NEVERC_KRT_A64_RET_X17;
	return 0;
}

static int _neverc_krt_fptr_replace_impl(
			    struct neverc_krt_fptr_interpose *h,
			    void *struct_addr, unsigned long field_off,
			    void *new_fn)
{
	void **slot;
	void *orig;
	void *entry;
	void *live;
	int ret;

	if (!h || !struct_addr || !new_fn) return -1;
	if (!_neverc_krt_inited) return NEVERC_KRT_INTERPOSE_E_NOINIT;
	if (h->active) return NEVERC_KRT_INTERPOSE_E_CONFLICT;

	slot = (void **)((unsigned long)struct_addr + field_off);
	if (neverc_krt_mem_read(&orig, slot, sizeof(orig)))
		return NEVERC_KRT_INTERPOSE_E_PATCH;
	if (!orig) return -2;

	h->struct_addr = struct_addr;
	h->field_off = field_off;
	h->orig_fn = orig;
	h->active = 0;

	if (neverc_krt_cfi_has_tag(orig)) {
		ret = neverc_krt_cfi_make_thunk(&h->thunk, orig, new_fn);
		if (ret)
			return ret;
		h->thunk_page = _neverc_krt_pool_alloc(sizeof(h->thunk));
		if (!h->thunk_page) return NEVERC_KRT_INTERPOSE_E_ALLOC;
		unsigned char *dst = (unsigned char *)h->thunk_page;
		unsigned char *src = (unsigned char *)&h->thunk;
		unsigned long i;
		for (i = 0; i < sizeof(h->thunk); i++)
			dst[i] = src[i];
		_neverc_krt_dcache_clean((unsigned long)h->thunk_page,
				  (unsigned long)h->thunk_page + sizeof(h->thunk));
		if (_neverc_krt_flushic)
			_neverc_krt_flushic((unsigned long)h->thunk_page,
				     (unsigned long)h->thunk_page + sizeof(h->thunk));
		else
			_neverc_krt_icache_inval(
				(unsigned long)h->thunk_page,
				(unsigned long)h->thunk_page + sizeof(h->thunk));
		entry = (void *)((unsigned long)h->thunk_page + 4);
	} else {
		h->thunk_page = (void *)0;
		entry = new_fn;
	}

	_neverc_krt_spin_lock(&_neverc_krt_patch_lock);
	if (neverc_krt_mem_read(&live, slot, sizeof(live)) || live != orig) {
		ret = NEVERC_KRT_INTERPOSE_E_CONFLICT;
	} else {
		ret = neverc_krt_mem_write_protected(
			(unsigned long)slot, &entry, sizeof(entry));
	}
	_neverc_krt_spin_unlock(&_neverc_krt_patch_lock);
	if (ret) {
		if (h->thunk_page)
			_neverc_krt_pool_free(h->thunk_page);
		h->thunk_page = (void *)0;
		return ret;
	}

	h->installed_fn = entry;
	h->active = 1;
	return 0;
}

int neverc_krt_fptr_replace(struct neverc_krt_fptr_interpose *h,
			    void *struct_addr, unsigned long field_off,
			    void *new_fn)
{
	int ret = _neverc_krt_lifecycle_begin();
	int owner_slot;

	if (ret != NEVERC_KRT_INTERPOSE_OK)
		return ret;
	ret = _neverc_krt_advanced_reserve(
		h, _NEVERC_KRT_ADVANCED_FPTR, &owner_slot);
	if (ret != NEVERC_KRT_INTERPOSE_OK) {
		_neverc_krt_lifecycle_end();
		return ret;
	}
	ret = _neverc_krt_fptr_replace_impl(h, struct_addr, field_off, new_fn);
	if (h && READ_ONCE(h->active))
		_neverc_krt_advanced_commit(owner_slot);
	else
		_neverc_krt_advanced_release(
			h, _NEVERC_KRT_ADVANCED_FPTR);
	_neverc_krt_lifecycle_end();
	return ret;
}

static int _neverc_krt_fptr_restore_impl(
			    struct neverc_krt_fptr_interpose *h)
{
	void **slot;
	void *live;
	int ret = NEVERC_KRT_INTERPOSE_OK;

	if (!h || !h->active)
		return NEVERC_KRT_INTERPOSE_OK;

	slot = (void **)((unsigned long)h->struct_addr + h->field_off);
	_neverc_krt_spin_lock(&_neverc_krt_patch_lock);
	if (neverc_krt_mem_read(&live, slot, sizeof(live))) {
		ret = NEVERC_KRT_INTERPOSE_E_PATCH;
	} else if (live != h->orig_fn && live != h->installed_fn) {
		neverc_krt_log_err(
			"function-pointer restore refused: slot=%px ownership lost",
			slot);
		ret = NEVERC_KRT_INTERPOSE_E_PATCH;
	} else if (live == h->installed_fn &&
		   neverc_krt_mem_write_protected(
			   (unsigned long)slot, &h->orig_fn,
			   sizeof(h->orig_fn))) {
		ret = NEVERC_KRT_INTERPOSE_E_PATCH;
	}
	_neverc_krt_spin_unlock(&_neverc_krt_patch_lock);
	if (ret != NEVERC_KRT_INTERPOSE_OK)
		return ret;

	if (h->thunk_page)
		_neverc_krt_pool_free(h->thunk_page);
	h->thunk_page = (void *)0;
	h->installed_fn = (void *)0;
	h->active = 0;
	return NEVERC_KRT_INTERPOSE_OK;
}

int neverc_krt_fptr_restore(struct neverc_krt_fptr_interpose *h)
{
	int ret = _neverc_krt_lifecycle_begin();

	if (ret != NEVERC_KRT_INTERPOSE_OK)
		return ret;
	ret = _neverc_krt_fptr_restore_impl(h);
	if (h && !READ_ONCE(h->active))
		_neverc_krt_advanced_release(
			h, _NEVERC_KRT_ADVANCED_FPTR);
	_neverc_krt_lifecycle_end();
	return ret;
}

/*
 * 6.18+: register_ftrace_function / unregister_ftrace_function /
 * ftrace_set_filter_ip were removed from the kernel symbol table.
 * Returns -1 on 6.18 (graceful degradation; use inline patching
 * or kprobes instead).
 */
int neverc_krt_ftrace_init(void)
{
	if (_neverc_krt_ftrace_avail) return 0;

	_neverc_krt_register_ftrace =
		(neverc_krt_register_ftrace_fn)NEVERC_KRT_LOOKUP("register_ftrace_function");
	_neverc_krt_unregister_ftrace =
		(neverc_krt_unregister_ftrace_fn)NEVERC_KRT_LOOKUP("unregister_ftrace_function");
	_neverc_krt_ftrace_set_filter =
		(neverc_krt_ftrace_set_filter_ip_fn)NEVERC_KRT_LOOKUP("ftrace_set_filter_ip");

	if (!_neverc_krt_register_ftrace || !_neverc_krt_unregister_ftrace ||
	    !_neverc_krt_ftrace_set_filter)
		return -1;

	_neverc_krt_ftrace_avail = 1;
	return 0;
}

static void _neverc_krt_ftrace_thunk_common(unsigned long ip,
					    unsigned long parent_ip,
					    struct ftrace_ops *ops,
					    void *regs)
{
	(void)ip;
	(void)parent_ip;
	if (!ops || !regs) return;
	struct neverc_krt_ftrace_interpose *h = (struct neverc_krt_ftrace_interpose *)(
		(char *)ops - __builtin_offsetof(struct neverc_krt_ftrace_interpose, _ops_storage));
	const struct neverc_krt_gki_layout *layout;
	if (!h->replace) return;

	layout = _neverc_krt_get_gki_layout();
	if (!layout || !layout->pt_regs_pc ||
	    layout->pt_regs_pc + sizeof(unsigned long) > layout->pt_regs_size)
		return;
	*(unsigned long *)((char *)regs + layout->pt_regs_pc) =
		(unsigned long)h->replace;
}

/* The embedded runtime is compiled once, so retain both source-level KCFI types. */
static void _neverc_krt_ftrace_thunk_pt_regs(unsigned long ip,
					 unsigned long parent_ip,
					 struct ftrace_ops *ops,
					 struct pt_regs *regs)
{
	_neverc_krt_ftrace_thunk_common(ip, parent_ip, ops, regs);
}

static void _neverc_krt_ftrace_thunk_ftrace_regs(unsigned long ip,
					    unsigned long parent_ip,
					    struct ftrace_ops *ops,
					    struct ftrace_regs *regs)
{
	_neverc_krt_ftrace_thunk_common(ip, parent_ip, ops, regs);
}

static int _neverc_krt_ftrace_interpose_install_impl(
				   struct neverc_krt_ftrace_interpose *h,
				   void *target, void *replace, void **orig)
{
	int ret;
	const struct neverc_krt_runtime_caps *caps;
	struct ftrace_ops *ops;

	caps = _neverc_krt_current_caps();
	if (!caps || !caps->has_ftrace_registration_api ||
	    !_neverc_krt_ftrace_avail)
		return -1;
	{
		const struct neverc_krt_gki_layout *layout =
			_neverc_krt_get_gki_layout();
		unsigned char *ops_bytes;
		unsigned long thunk;
		unsigned long i;

		if (!layout || !layout->ftrace_ops_flags ||
		    layout->ftrace_ops_func + sizeof(unsigned long) >
			    sizeof(h->_ops_storage) ||
		    layout->ftrace_ops_flags + sizeof(unsigned long) >
			    sizeof(h->_ops_storage))
			return -1;
		if (!h || !target || !replace) return -2;
		if (h->active) return NEVERC_KRT_INTERPOSE_E_CONFLICT;

		h->target = target;
		h->replace = replace;
		h->orig = target;
		h->active = 0;
		h->registered = 0;
		h->filtered = 0;

		ops_bytes = (unsigned char *)h->_ops_storage;
		for (i = 0; i < sizeof(h->_ops_storage); i++)
			ops_bytes[i] = 0;
		ops = (struct ftrace_ops *)h->_ops_storage;

		switch (caps->ftrace_callback_abi) {
		case NEVERC_KRT_FTRACE_ABI_FTRACE_REGS:
			thunk = (unsigned long)
				_neverc_krt_ftrace_thunk_ftrace_regs;
			break;
		case NEVERC_KRT_FTRACE_ABI_PT_REGS:
			thunk = (unsigned long)
				_neverc_krt_ftrace_thunk_pt_regs;
			break;
		default:
			return -3;
		}
		*(unsigned long *)(ops_bytes + layout->ftrace_ops_func) = thunk;
		*(unsigned long *)(ops_bytes + layout->ftrace_ops_flags) =
			NEVERC_KRT_FTRACE_FL_SAVE_REGS
			| NEVERC_KRT_FTRACE_FL_IPMODIFY
			| NEVERC_KRT_FTRACE_FL_RECURSION;
	}

	ret = _neverc_krt_ftrace_set_filter(ops,
				     (unsigned long)target, 0, 1);
	if (ret) return ret;
	h->filtered = 1;

	ret = _neverc_krt_register_ftrace(ops);
	if (ret) {
		if (_neverc_krt_ftrace_set_filter(
			    ops, (unsigned long)target, 1, 0)) {
			h->active = 1;
			return NEVERC_KRT_INTERPOSE_E_PATCH;
		}
		h->filtered = 0;
		return ret;
	}
	h->registered = 1;

	if (orig) *orig = h->orig;
	h->active = 1;
	return 0;
}

int neverc_krt_ftrace_interpose_install(struct neverc_krt_ftrace_interpose *h,
				   void *target, void *replace,
				   void **orig)
{
	int ret = _neverc_krt_lifecycle_begin();
	int owner_slot;

	if (ret != NEVERC_KRT_INTERPOSE_OK)
		return ret;
	ret = _neverc_krt_advanced_reserve(
		h, _NEVERC_KRT_ADVANCED_FTRACE, &owner_slot);
	if (ret != NEVERC_KRT_INTERPOSE_OK) {
		_neverc_krt_lifecycle_end();
		return ret;
	}
	ret = _neverc_krt_ftrace_interpose_install_impl(
		h, target, replace, orig);
	if (h && READ_ONCE(h->active))
		_neverc_krt_advanced_commit(owner_slot);
	else
		_neverc_krt_advanced_release(
			h, _NEVERC_KRT_ADVANCED_FTRACE);
	_neverc_krt_lifecycle_end();
	return ret;
}

static int _neverc_krt_ftrace_interpose_remove_impl(
				   struct neverc_krt_ftrace_interpose *h)
{
	struct ftrace_ops *ops;

	if (!h || !h->active)
		return NEVERC_KRT_INTERPOSE_OK;
	ops = (struct ftrace_ops *)h->_ops_storage;
	if (h->registered) {
		if (!_neverc_krt_unregister_ftrace ||
		    _neverc_krt_unregister_ftrace(ops))
			return NEVERC_KRT_INTERPOSE_E_PATCH;
		h->registered = 0;
	}
	if (h->filtered) {
		if (!_neverc_krt_ftrace_set_filter ||
		    _neverc_krt_ftrace_set_filter(
			    ops, (unsigned long)h->target, 1, 0))
			return NEVERC_KRT_INTERPOSE_E_PATCH;
		h->filtered = 0;
	}
	h->active = 0;
	h->target = (void *)0;
	h->replace = (void *)0;
	h->orig = (void *)0;
	return NEVERC_KRT_INTERPOSE_OK;
}

int neverc_krt_ftrace_interpose_remove(struct neverc_krt_ftrace_interpose *h)
{
	int ret = _neverc_krt_lifecycle_begin();

	if (ret != NEVERC_KRT_INTERPOSE_OK)
		return ret;
	ret = _neverc_krt_ftrace_interpose_remove_impl(h);
	if (h && !READ_ONCE(h->active))
		_neverc_krt_advanced_release(
			h, _NEVERC_KRT_ADVANCED_FTRACE);
	_neverc_krt_lifecycle_end();
	return ret;
}

int neverc_krt_interpose_auto(struct neverc_krt_interpose *h, void *target,
			 void *replace, void **orig,
			 struct neverc_krt_ftrace_interpose *ft_fallback)
{
	enum neverc_krt_scan_result scan = neverc_krt_interpose_scan(target);
	int inline_ret = NEVERC_KRT_INTERPOSE_E_RELOC;
	int ret;

	if (scan == NEVERC_KRT_SCAN_OK) {
		inline_ret =
			neverc_krt_interpose_install(h, target, replace, orig);
		if (inline_ret == NEVERC_KRT_INTERPOSE_OK)
			return inline_ret;
		if (inline_ret == NEVERC_KRT_INTERPOSE_E_PATCH)
			return inline_ret;
	}

	if (ft_fallback && _neverc_krt_ftrace_avail) {
		ret = neverc_krt_ftrace_interpose_install(ft_fallback,
					      target, replace, orig);
		if (ret == 0) return ret;
		if (ret == NEVERC_KRT_INTERPOSE_E_PATCH)
			return ret;
	}

	if (scan == NEVERC_KRT_SCAN_OK)
		return inline_ret;

	return NEVERC_KRT_INTERPOSE_E_RELOC;
}

static int neverc_krt_kprobe_interpose_init(void)
{
	if (_neverc_krt_reg_kprobe) return 0;
	_neverc_krt_reg_kprobe =
		(neverc_krt_register_kprobe_fn)NEVERC_KRT_LOOKUP("register_kprobe");
	_neverc_krt_unreg_kprobe =
		(neverc_krt_unregister_kprobe_fn)NEVERC_KRT_LOOKUP("unregister_kprobe");
	return (_neverc_krt_reg_kprobe && _neverc_krt_unreg_kprobe) ? 0 : -1;
}

int neverc_krt_interpose_auto_remove(struct neverc_krt_interpose *h,
				struct neverc_krt_ftrace_interpose *ft_fallback)
{
	int ret;

	if (h && h->active) {
		ret = neverc_krt_interpose_remove(h);
		if (ret != NEVERC_KRT_INTERPOSE_OK)
			return ret;
	}
	if (ft_fallback && ft_fallback->active) {
		ret = neverc_krt_ftrace_interpose_remove(ft_fallback);
		if (ret != NEVERC_KRT_INTERPOSE_OK)
			return ret;
	}
	return NEVERC_KRT_INTERPOSE_OK;
}

static int neverc_krt_pool_usage(int *total_used, int *total_cap)
{
	int i, used = 0, cap = 0;
	int pgsz = _neverc_krt_pool_pgsz ? _neverc_krt_pool_pgsz : 4096;
	unsigned long flags;
	flags = _neverc_krt_spin_lock_irqsave(&_neverc_krt_pool_lock);
	for (i = 0; i < _neverc_krt_pool_count; i++) {
		used += _neverc_krt_pool[i].used;
		cap += pgsz;
	}
	_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_pool_lock, flags);
	if (total_used) *total_used = used;
	if (total_cap)  *total_cap = cap;
	return _neverc_krt_pool_count;
}

static long _neverc_krt_chain_call_orig(
	struct neverc_krt_interpose_chain *chain,
	void *a0, void *a1, void *a2, void *a3, void *a4, void *a5)
{
	neverc_krt_chain_orig_t orig;

	if (unlikely(!chain))
		return 0;
	orig = (neverc_krt_chain_orig_t)__atomic_load_n(
		(unsigned long *)&chain->orig_fn, __ATOMIC_ACQUIRE);
	return orig ? orig(a0, a1, a2, a3, a4, a5) : 0;
}

static long _neverc_krt_chain_call(
	struct neverc_krt_interpose_chain *chain, int index, void *next,
	void *a0, void *a1, void *a2, void *a3, void *a4, void *a5)
{
	neverc_krt_chain_handler_t handler;
	neverc_krt_chain_orig_t next_fn = (neverc_krt_chain_orig_t)next;
	int count;
	int active;

	if (unlikely(!chain || !next_fn))
		return 0;
	count = __atomic_load_n(&chain->count, __ATOMIC_ACQUIRE);
	if (index < 0 || index >= count || index >= NEVERC_KRT_CHAIN_MAX)
		return next_fn(a0, a1, a2, a3, a4, a5);

	handler = (neverc_krt_chain_handler_t)__atomic_load_n(
		(unsigned long *)&chain->entries[index].handler,
		__ATOMIC_ACQUIRE);
	active = __atomic_load_n(
		&chain->entries[index].active, __ATOMIC_ACQUIRE);
	if (!active || !handler)
		return next_fn(a0, a1, a2, a3, a4, a5);
	return handler(next, a0, a1, a2, a3, a4, a5);
}

static int neverc_krt_chain_init(struct neverc_krt_interpose_chain *chain)
{
	if (!chain) return -1;
	unsigned char *p = (unsigned char *)chain;
	unsigned long sz = sizeof(*chain);
	unsigned long i;
	for (i = 0; i < sz; i++) p[i] = 0;
	return 0;
}

static int neverc_krt_chain_add(struct neverc_krt_interpose_chain *chain,
			 void *handler, int priority)
{
	int i, slot = -1;

	if (!chain || !handler) return -1;
	if (chain->count >= NEVERC_KRT_CHAIN_MAX) return -2;

	for (i = 0; i < chain->count; i++) {
		if (chain->entries[i].handler == handler)
			return -3;
	}

	slot = chain->count;
	for (i = chain->count - 1; i >= 0; i--) {
		if (chain->entries[i].priority > priority) {
			chain->entries[i + 1] = chain->entries[i];
			slot = i;
		} else {
			break;
		}
	}

	chain->entries[slot].handler = handler;
	chain->entries[slot].priority = priority;
	__asm__ __volatile__("dmb ish" ::: "memory");
	WRITE_ONCE(chain->entries[slot].active, 1);
	__asm__ __volatile__("dmb ish" ::: "memory");
	WRITE_ONCE(chain->count, chain->count + 1);
	return 0;
}

static void neverc_krt_interpose_get_stats(struct neverc_krt_interpose_stats *out)
{
	if (!out) return;
	out->total_installs = __atomic_load_n(&_neverc_krt_interpose_install_cnt,
					      __ATOMIC_RELAXED);
	out->total_removes  = __atomic_load_n(&_neverc_krt_interpose_remove_cnt,
					      __ATOMIC_RELAXED);
	out->pool_allocs = neverc_krt_pool_alloc_count();
	out->pool_alloc_fails = __atomic_load_n(&_neverc_krt_pool_alloc_fail,
						__ATOMIC_RELAXED);
	out->pool_pages = neverc_krt_pool_usage(&out->pool_used_bytes,
					  &out->pool_total_bytes);
	out->active_interposes = (int)(out->total_installs - out->total_removes);
}

static int neverc_krt_chain_remove(struct neverc_krt_interpose_chain *chain, void *handler)
{
	int i;
	if (!chain || !handler) return -1;

	for (i = 0; i < chain->count; i++) {
		if (chain->entries[i].handler == handler) {
			WRITE_ONCE(chain->entries[i].active, 0);
			__asm__ __volatile__("dmb ish" ::: "memory");
			for (; i < chain->count - 1; i++)
				chain->entries[i] = chain->entries[i + 1];
			chain->entries[chain->count - 1].handler = (void *)0;
			chain->entries[chain->count - 1].priority = 0;
			chain->entries[chain->count - 1].active = 0;
			__asm__ __volatile__("dmb ish" ::: "memory");
			WRITE_ONCE(chain->count, chain->count - 1);
			return 0;
		}
	}
	return -2;
}

static int neverc_krt_chain_install(struct neverc_krt_interpose_chain *chain, void *target,
			     void *dispatch_fn)
{
	if (!chain || !target || !dispatch_fn) return -1;
	if (chain->interpose.active) return -2;
	if (chain->count == 0) return -3;

	chain->dispatch_fn = dispatch_fn;
	return neverc_krt_interpose_install(&chain->interpose, target,
				dispatch_fn, &chain->orig_fn);
}

static void neverc_krt_chain_uninstall(struct neverc_krt_interpose_chain *chain)
{
	if (!chain) return;
	if (chain->interpose.active)
		neverc_krt_interpose_remove(&chain->interpose);
}

/* ================================================================
 * High-level interpose registry — auto-chain, transparent to callers.
 * Uses neverc_krt_interpose_install_ctx internally (safe on BTI/GP pages).
 * ================================================================ */

static struct {
	void                        *target;
	struct neverc_krt_interpose_chain chain;
	int                          used;
	int                          transitioning;
	int                          faulted;
} _neverc_krt_registry[NEVERC_KRT_REGISTRY_MAX];

static struct neverc_krt_interpose_ctx _neverc_krt_reg_ctxs[NEVERC_KRT_REGISTRY_MAX]
	__attribute__((aligned(64)));

static volatile int _neverc_krt_registry_lock;

#define _NKR_ARGS_DECL                                                         \
	void *a0, void *a1, void *a2, void *a3, void *a4, void *a5
#define _NKR_ARGS a0, a1, a2, a3, a4, a5
#define _NKR_CHAIN_NEXT(n, index, next_index)                                 \
	static long _neverc_krt_reg_next_##n##_##index(_NKR_ARGS_DECL)         \
	{                                                                      \
		return _neverc_krt_chain_call(                                 \
			&_neverc_krt_registry[n].chain, index,                 \
			(void *)_neverc_krt_reg_next_##n##_##next_index,       \
			_NKR_ARGS);                                             \
	}
#define _NKR_CTX_DISPATCH(n)                                                   \
	static long _neverc_krt_reg_next_##n##_4(_NKR_ARGS_DECL)               \
	{                                                                      \
		return _neverc_krt_chain_call_orig(                            \
			&_neverc_krt_registry[n].chain, _NKR_ARGS);            \
	}                                                                      \
	_NKR_CHAIN_NEXT(n, 3, 4)                                               \
	_NKR_CHAIN_NEXT(n, 2, 3)                                               \
	_NKR_CHAIN_NEXT(n, 1, 2)                                               \
	_NKR_CHAIN_NEXT(n, 0, 1)                                               \
	static void _neverc_krt_reg_ctx_dispatch_##n(neverc_krt_reg_ctx *ctx)  \
	{                                                                      \
		long ret = _neverc_krt_reg_next_##n##_0(                       \
			(void *)ctx->regs[0], (void *)ctx->regs[1],           \
			(void *)ctx->regs[2], (void *)ctx->regs[3],           \
			(void *)ctx->regs[4], (void *)ctx->regs[5]);          \
		ctx->regs[0] = (u64)ret;                                       \
		NEVERC_KRT_CTX_SKIP_VOID(ctx);                                 \
	}

_NKR_CTX_DISPATCH(0)  _NKR_CTX_DISPATCH(1)  _NKR_CTX_DISPATCH(2)  _NKR_CTX_DISPATCH(3)
_NKR_CTX_DISPATCH(4)  _NKR_CTX_DISPATCH(5)  _NKR_CTX_DISPATCH(6)  _NKR_CTX_DISPATCH(7)
_NKR_CTX_DISPATCH(8)  _NKR_CTX_DISPATCH(9)  _NKR_CTX_DISPATCH(10) _NKR_CTX_DISPATCH(11)
_NKR_CTX_DISPATCH(12) _NKR_CTX_DISPATCH(13) _NKR_CTX_DISPATCH(14) _NKR_CTX_DISPATCH(15)

#undef _NKR_CTX_DISPATCH
#undef _NKR_CHAIN_NEXT
#undef _NKR_ARGS
#undef _NKR_ARGS_DECL

static neverc_krt_ctx_handler_t _neverc_krt_reg_ctx_dispatchers[NEVERC_KRT_REGISTRY_MAX] = {
	_neverc_krt_reg_ctx_dispatch_0,  _neverc_krt_reg_ctx_dispatch_1,
	_neverc_krt_reg_ctx_dispatch_2,  _neverc_krt_reg_ctx_dispatch_3,
	_neverc_krt_reg_ctx_dispatch_4,  _neverc_krt_reg_ctx_dispatch_5,
	_neverc_krt_reg_ctx_dispatch_6,  _neverc_krt_reg_ctx_dispatch_7,
	_neverc_krt_reg_ctx_dispatch_8,  _neverc_krt_reg_ctx_dispatch_9,
	_neverc_krt_reg_ctx_dispatch_10, _neverc_krt_reg_ctx_dispatch_11,
	_neverc_krt_reg_ctx_dispatch_12, _neverc_krt_reg_ctx_dispatch_13,
	_neverc_krt_reg_ctx_dispatch_14, _neverc_krt_reg_ctx_dispatch_15,
};

static int _neverc_krt_reg_find_target(void *target)
{
	int i;
	for (i = NEVERC_KRT_REGISTRY_MAX - 1; i >= 0; i--) {
		if (_neverc_krt_registry[i].used &&
		    _neverc_krt_registry[i].target == target)
			return i;
	}
	return -1;
}

static int _neverc_krt_reg_alloc_slot(void)
{
	int i;
	for (i = 0; i < NEVERC_KRT_REGISTRY_MAX; i++) {
		if (!_neverc_krt_registry[i].used)
			return i;
	}
	return -1;
}

static int _neverc_krt_interpose_register_impl(
			     void *target, void *handler, int priority,
			     void **orig, struct neverc_krt_interpose_ref *ref)
{
	unsigned long flags;
	int slot, ret;

	if (!target || !handler || !ref)
		return -1;
	ref->slot = -1;
	ref->handler = (void *)0;
	if (!_neverc_krt_inited)
		return NEVERC_KRT_INTERPOSE_E_NOINIT;

	flags = _neverc_krt_spin_lock_irqsave(&_neverc_krt_registry_lock);

	slot = _neverc_krt_reg_find_target(target);
	if (slot >= 0) {
		if (_neverc_krt_registry[slot].faulted) {
			_neverc_krt_spin_unlock_irqrestore(
				&_neverc_krt_registry_lock, flags);
			return NEVERC_KRT_INTERPOSE_E_PATCH;
		}
		if (_neverc_krt_registry[slot].transitioning) {
			_neverc_krt_spin_unlock_irqrestore(
				&_neverc_krt_registry_lock, flags);
			return NEVERC_KRT_INTERPOSE_E_CONFLICT;
		}
		_neverc_krt_registry[slot].transitioning = 1;
		_neverc_krt_ctx_set_enabled(&_neverc_krt_reg_ctxs[slot], 0);
		_neverc_krt_spin_unlock_irqrestore(
			&_neverc_krt_registry_lock, flags);
		_neverc_krt_full_barrier();
		ret = _neverc_krt_wait_one_ctx_inflight(
			&_neverc_krt_reg_ctxs[slot]);
		if (ret != NEVERC_KRT_INTERPOSE_OK) {
			flags = _neverc_krt_spin_lock_irqsave(
				&_neverc_krt_registry_lock);
			_neverc_krt_registry[slot].faulted = 1;
			_neverc_krt_registry[slot].transitioning = 0;
			_neverc_krt_spin_unlock_irqrestore(
				&_neverc_krt_registry_lock, flags);
			return ret;
		}

		flags = _neverc_krt_spin_lock_irqsave(
			&_neverc_krt_registry_lock);
		ret = neverc_krt_chain_add(&_neverc_krt_registry[slot].chain,
					   handler, priority);
		if (ret == 0) {
			if (orig)
				*orig = _neverc_krt_registry[slot].chain.orig_fn;
			ref->slot = slot;
			ref->handler = handler;
		}
		_neverc_krt_registry[slot].transitioning = 0;
		_neverc_krt_full_barrier();
		_neverc_krt_ctx_set_enabled(&_neverc_krt_reg_ctxs[slot], 1);
		_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_registry_lock,
						   flags);
		return ret;
	}

	slot = _neverc_krt_reg_alloc_slot();
	if (slot < 0) {
		_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_registry_lock,
						   flags);
		return NEVERC_KRT_INTERPOSE_E_ALLOC;
	}

	_neverc_krt_registry[slot].target = target;
	_neverc_krt_registry[slot].used = 1;
	_neverc_krt_registry[slot].transitioning = 1;
	_neverc_krt_registry[slot].faulted = 0;
	neverc_krt_chain_init(&_neverc_krt_registry[slot].chain);
	neverc_krt_chain_add(&_neverc_krt_registry[slot].chain,
			     handler, priority);

	_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_registry_lock, flags);

	ret = _neverc_krt_interpose_install_ctx_impl(
					   &_neverc_krt_reg_ctxs[slot],
					   target,
					   _neverc_krt_reg_ctx_dispatchers[slot],
					   (void **)&_neverc_krt_registry[slot].chain.orig_fn);
	if (ret != 0) {
		flags = _neverc_krt_spin_lock_irqsave(&_neverc_krt_registry_lock);
		if (_neverc_krt_ctx_install_retained(
				&_neverc_krt_reg_ctxs[slot], ret)) {
			/* Error return still transfers an exceptional recovery handle:
			 * only unregister/cleanup may touch this faulted slot. */
			_neverc_krt_registry[slot].faulted = 1;
			_neverc_krt_registry[slot].transitioning = 0;
			if (orig)
				*orig = _neverc_krt_registry[slot].chain.orig_fn;
			ref->slot = slot;
			ref->handler = handler;
			_neverc_krt_spin_unlock_irqrestore(
				&_neverc_krt_registry_lock, flags);
			return ret;
		}
		_neverc_krt_registry[slot].chain.count = 0;
		_neverc_krt_registry[slot].used = 0;
		_neverc_krt_registry[slot].transitioning = 0;
		_neverc_krt_registry[slot].faulted = 0;
		_neverc_krt_registry[slot].target = (void *)0;
		_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_registry_lock,
						   flags);
		return ret;
	}

	flags = _neverc_krt_spin_lock_irqsave(&_neverc_krt_registry_lock);
	_neverc_krt_registry[slot].transitioning = 0;
	_neverc_krt_registry[slot].faulted = 0;
	_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_registry_lock, flags);

	if (orig)
		*orig = _neverc_krt_registry[slot].chain.orig_fn;
	ref->slot = slot;
	ref->handler = handler;

	return 0;
}

int neverc_krt_interpose_register(void *target, void *handler, int priority,
			     void **orig, struct neverc_krt_interpose_ref *ref)
{
	int ret = _neverc_krt_lifecycle_begin();

	if (ret != NEVERC_KRT_INTERPOSE_OK)
		return ret;
	ret = _neverc_krt_interpose_register_impl(
		target, handler, priority, orig, ref);
	_neverc_krt_lifecycle_end();
	return ret;
}

static int _neverc_krt_interpose_unregister_impl(
			     struct neverc_krt_interpose_ref *ref)
{
	unsigned long flags;
	int slot, ret;

	if (!ref)
		return -1;

	slot = ref->slot;
	if (slot < 0 || slot >= NEVERC_KRT_REGISTRY_MAX)
		return -1;

	flags = _neverc_krt_spin_lock_irqsave(&_neverc_krt_registry_lock);

	if (!_neverc_krt_registry[slot].used) {
		_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_registry_lock,
						   flags);
		return -2;
	}

	if (_neverc_krt_registry[slot].transitioning) {
		_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_registry_lock,
						   flags);
		return NEVERC_KRT_INTERPOSE_E_CONFLICT;
	}
	/* Global cleanup may have failed while a multi-handler entry was being
	 * restored.  It owns recovery of that complete chain.  Removing one member
	 * here and re-enabling the faulted stub would defeat fail-stop teardown. */
	if (_neverc_krt_registry[slot].faulted &&
	    _neverc_krt_registry[slot].chain.count > 1) {
		_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_registry_lock,
						   flags);
		return NEVERC_KRT_INTERPOSE_E_PATCH;
	}

	if (_neverc_krt_registry[slot].chain.count > 1) {
		_neverc_krt_registry[slot].transitioning = 1;
		_neverc_krt_ctx_set_enabled(&_neverc_krt_reg_ctxs[slot], 0);
		_neverc_krt_spin_unlock_irqrestore(
			&_neverc_krt_registry_lock, flags);
		_neverc_krt_full_barrier();
		ret = _neverc_krt_wait_one_ctx_inflight(
			&_neverc_krt_reg_ctxs[slot]);
		if (ret != NEVERC_KRT_INTERPOSE_OK) {
			flags = _neverc_krt_spin_lock_irqsave(
				&_neverc_krt_registry_lock);
			_neverc_krt_registry[slot].faulted = 1;
			_neverc_krt_registry[slot].transitioning = 0;
			_neverc_krt_spin_unlock_irqrestore(
				&_neverc_krt_registry_lock, flags);
			return ret;
		}

		flags = _neverc_krt_spin_lock_irqsave(
			&_neverc_krt_registry_lock);
		ret = neverc_krt_chain_remove(&_neverc_krt_registry[slot].chain,
					      ref->handler);
		if (ret == 0) {
			ref->slot = -1;
			ref->handler = (void *)0;
		}
		_neverc_krt_registry[slot].transitioning = 0;
		_neverc_krt_full_barrier();
		_neverc_krt_ctx_set_enabled(&_neverc_krt_reg_ctxs[slot], 1);
		_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_registry_lock,
						   flags);
		return ret;
	}

	if (_neverc_krt_registry[slot].chain.count != 1 ||
	    _neverc_krt_registry[slot].chain.entries[0].handler != ref->handler) {
		_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_registry_lock,
						   flags);
		return -2;
	}

	_neverc_krt_registry[slot].transitioning = 1;
	_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_registry_lock, flags);

	{
		struct neverc_krt_interpose_ctx *one =
			&_neverc_krt_reg_ctxs[slot];
		(void)_neverc_krt_interpose_remove_ctx_many_impl(&one, 1);
	}

	flags = _neverc_krt_spin_lock_irqsave(&_neverc_krt_registry_lock);
	if (_neverc_krt_reg_ctxs[slot].base.active) {
		_neverc_krt_registry[slot].faulted = 1;
		_neverc_krt_registry[slot].transitioning = 0;
		_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_registry_lock,
						   flags);
		return NEVERC_KRT_INTERPOSE_E_PATCH;
	}

	ret = neverc_krt_chain_remove(&_neverc_krt_registry[slot].chain,
				      ref->handler);
	if (ret == 0) {
		ref->slot = -1;
		ref->handler = (void *)0;
		_neverc_krt_registry[slot].used = 0;
		_neverc_krt_registry[slot].target = (void *)0;
		_neverc_krt_registry[slot].faulted = 0;
	}
	_neverc_krt_registry[slot].transitioning = 0;
	_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_registry_lock, flags);
	return ret;
}

int neverc_krt_interpose_unregister(struct neverc_krt_interpose_ref *ref)
{
	int ret = _neverc_krt_lifecycle_begin();

	if (ret != NEVERC_KRT_INTERPOSE_OK)
		return ret;
	ret = _neverc_krt_interpose_unregister_impl(ref);
	_neverc_krt_lifecycle_end();
	return ret;
}

int neverc_krt_interpose_registry_count(void *target)
{
	unsigned long flags;
	int slot, cnt = 0;

	flags = _neverc_krt_spin_lock_irqsave(&_neverc_krt_registry_lock);
	slot = _neverc_krt_reg_find_target(target);
	if (slot >= 0)
		cnt = _neverc_krt_registry[slot].chain.count;
	_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_registry_lock, flags);
	return cnt;
}

/* ================================================================
 * Arbitrary-point probe — interpose any instruction (ctx mode, auto-chain).
 * Uses the proven ctx-interpose mechanism (neverc_krt_interpose_install_ctx)
 * for the underlying patching/stub. The probe chain layer adds
 * multi-handler support on top.
 * ================================================================ */

/* --- probe ctx chain (handlers receive neverc_krt_reg_ctx *) --- */

#define _PROBE_CHAIN_MAX NEVERC_KRT_CHAIN_MAX

struct _neverc_krt_probe_chain {
	struct neverc_krt_interpose_chain_entry entries[_PROBE_CHAIN_MAX];
	int count;
};

static void _neverc_krt_probe_ctx_chain_run(neverc_krt_reg_ctx *ctx,
					    struct _neverc_krt_probe_chain *chain)
{
	int i, cnt;
	struct neverc_krt_interpose_chain_entry snap[_PROBE_CHAIN_MAX];

	if (unlikely(!chain)) return;
	cnt = __atomic_load_n(&chain->count, __ATOMIC_ACQUIRE);
	if (unlikely(cnt > _PROBE_CHAIN_MAX)) cnt = _PROBE_CHAIN_MAX;

	for (i = 0; i < cnt; i++) {
		snap[i].handler = (void *)__atomic_load_n(
			(unsigned long *)&chain->entries[i].handler,
			__ATOMIC_ACQUIRE);
		snap[i].active = __atomic_load_n(&chain->entries[i].active,
						  __ATOMIC_RELAXED);
	}
	for (i = 0; i < cnt; i++) {
		if (unlikely(!snap[i].active || !snap[i].handler)) continue;
		neverc_krt_ctx_handler_t h =
			(neverc_krt_ctx_handler_t)snap[i].handler;
		h(ctx);
		if (ctx->force_jump) return;
	}
}

/* --- probe registry (backed by ctx-interpose) --- */

struct _neverc_krt_probe_slot {
	void                            *addr;
	int                              used;
	int                              transitioning;
	int                              faulted;
	struct _neverc_krt_probe_chain   chain;
};

static struct _neverc_krt_probe_slot _neverc_krt_probes[NEVERC_KRT_PROBE_MAX];
static struct neverc_krt_interpose_ctx    _neverc_krt_probe_ctxs[NEVERC_KRT_PROBE_MAX]
	__attribute__((aligned(64)));
static volatile int _neverc_krt_probe_lock;

#define _NKP_DISPATCH(n)                                                       \
	static void _neverc_krt_probe_dispatch_##n(neverc_krt_reg_ctx *ctx)    \
	{                                                                      \
		_neverc_krt_probe_ctx_chain_run(ctx,                           \
			&_neverc_krt_probes[n].chain);                        \
	}

_NKP_DISPATCH(0)  _NKP_DISPATCH(1)  _NKP_DISPATCH(2)  _NKP_DISPATCH(3)
_NKP_DISPATCH(4)  _NKP_DISPATCH(5)  _NKP_DISPATCH(6)  _NKP_DISPATCH(7)
_NKP_DISPATCH(8)  _NKP_DISPATCH(9)  _NKP_DISPATCH(10) _NKP_DISPATCH(11)
_NKP_DISPATCH(12) _NKP_DISPATCH(13) _NKP_DISPATCH(14) _NKP_DISPATCH(15)

static neverc_krt_ctx_handler_t _neverc_krt_probe_dispatchers[NEVERC_KRT_PROBE_MAX] = {
	_neverc_krt_probe_dispatch_0,  _neverc_krt_probe_dispatch_1,
	_neverc_krt_probe_dispatch_2,  _neverc_krt_probe_dispatch_3,
	_neverc_krt_probe_dispatch_4,  _neverc_krt_probe_dispatch_5,
	_neverc_krt_probe_dispatch_6,  _neverc_krt_probe_dispatch_7,
	_neverc_krt_probe_dispatch_8,  _neverc_krt_probe_dispatch_9,
	_neverc_krt_probe_dispatch_10, _neverc_krt_probe_dispatch_11,
	_neverc_krt_probe_dispatch_12, _neverc_krt_probe_dispatch_13,
	_neverc_krt_probe_dispatch_14, _neverc_krt_probe_dispatch_15,
};

/* --- internal probe install/remove using ctx-interpose --- */

static int _neverc_krt_probe_install_slot(int slot)
{
	struct _neverc_krt_probe_slot *ps = &_neverc_krt_probes[slot];

	return _neverc_krt_interpose_install_ctx_impl(
					   &_neverc_krt_probe_ctxs[slot],
					   ps->addr,
					   _neverc_krt_probe_dispatchers[slot],
					   (void *)0);
}

static void _neverc_krt_probe_remove_slot(int slot)
{
	struct neverc_krt_interpose_ctx *one =
		&_neverc_krt_probe_ctxs[slot];

	(void)_neverc_krt_interpose_remove_ctx_many_impl(&one, 1);
}

/* --- chain helpers for probe --- */

static int _neverc_krt_probe_chain_add(struct _neverc_krt_probe_chain *chain,
				       void *handler, int priority)
{
	int i, slot;

	if (!chain || !handler) return -1;
	if (chain->count >= _PROBE_CHAIN_MAX) return -2;

	for (i = 0; i < chain->count; i++) {
		if (chain->entries[i].handler == handler)
			return -3;
	}

	slot = chain->count;
	for (i = chain->count - 1; i >= 0; i--) {
		if (chain->entries[i].priority > priority) {
			chain->entries[i + 1] = chain->entries[i];
			slot = i;
		} else {
			break;
		}
	}

	chain->entries[slot].handler = handler;
	chain->entries[slot].priority = priority;
	__asm__ __volatile__("dmb ish" ::: "memory");
	WRITE_ONCE(chain->entries[slot].active, 1);
	__asm__ __volatile__("dmb ish" ::: "memory");
	WRITE_ONCE(chain->count, chain->count + 1);
	return 0;
}

static int _neverc_krt_probe_chain_remove(struct _neverc_krt_probe_chain *chain,
					  void *handler)
{
	int i;
	if (!chain || !handler) return -1;

	for (i = 0; i < chain->count; i++) {
		if (chain->entries[i].handler == handler) {
			WRITE_ONCE(chain->entries[i].active, 0);
			__asm__ __volatile__("dmb ish" ::: "memory");
			for (; i < chain->count - 1; i++)
				chain->entries[i] = chain->entries[i + 1];
			chain->entries[chain->count - 1].handler = (void *)0;
			chain->entries[chain->count - 1].priority = 0;
			chain->entries[chain->count - 1].active = 0;
			__asm__ __volatile__("dmb ish" ::: "memory");
			WRITE_ONCE(chain->count, chain->count - 1);
			return 0;
		}
	}
	return -2;
}

/* --- public API --- */

static int _neverc_krt_probe_find_addr(void *addr)
{
	int i;
	for (i = NEVERC_KRT_PROBE_MAX - 1; i >= 0; i--) {
		if (_neverc_krt_probes[i].used &&
		    _neverc_krt_probes[i].addr == addr)
			return i;
	}
	return -1;
}

static int _neverc_krt_probe_alloc_slot(void)
{
	int i;
	for (i = 0; i < NEVERC_KRT_PROBE_MAX; i++) {
		if (!_neverc_krt_probes[i].used)
			return i;
	}
	return -1;
}

static int _neverc_krt_probe_register_impl(
			      void *addr, neverc_krt_ctx_handler_t handler,
			      int priority, struct neverc_krt_probe_ref *ref)
{
	unsigned long flags;
	int slot, ret;

	if (!addr || !handler || !ref)
		return -1;
	ref->slot = -1;
	ref->handler = (void *)0;
	if (!_neverc_krt_inited)
		return NEVERC_KRT_INTERPOSE_E_NOINIT;
	if (((unsigned long)addr & 3) != 0)
		return NEVERC_KRT_INTERPOSE_E_SHORT;

	addr = (void *)neverc_krt_strip_pac((unsigned long)addr);

	flags = _neverc_krt_spin_lock_irqsave(&_neverc_krt_probe_lock);

	slot = _neverc_krt_probe_find_addr(addr);
	if (slot >= 0) {
		if (_neverc_krt_probes[slot].faulted) {
			_neverc_krt_spin_unlock_irqrestore(
				&_neverc_krt_probe_lock, flags);
			return NEVERC_KRT_INTERPOSE_E_PATCH;
		}
		if (_neverc_krt_probes[slot].transitioning) {
			_neverc_krt_spin_unlock_irqrestore(
				&_neverc_krt_probe_lock, flags);
			return NEVERC_KRT_INTERPOSE_E_CONFLICT;
		}
		_neverc_krt_probes[slot].transitioning = 1;
		_neverc_krt_ctx_set_enabled(&_neverc_krt_probe_ctxs[slot], 0);
		_neverc_krt_spin_unlock_irqrestore(
			&_neverc_krt_probe_lock, flags);
		_neverc_krt_full_barrier();
		ret = _neverc_krt_wait_one_ctx_inflight(
			&_neverc_krt_probe_ctxs[slot]);
		if (ret != NEVERC_KRT_INTERPOSE_OK) {
			flags = _neverc_krt_spin_lock_irqsave(
				&_neverc_krt_probe_lock);
			_neverc_krt_probes[slot].faulted = 1;
			_neverc_krt_probes[slot].transitioning = 0;
			_neverc_krt_spin_unlock_irqrestore(
				&_neverc_krt_probe_lock, flags);
			return ret;
		}

		flags = _neverc_krt_spin_lock_irqsave(
			&_neverc_krt_probe_lock);
		ret = _neverc_krt_probe_chain_add(
			&_neverc_krt_probes[slot].chain,
			(void *)handler, priority);
		if (ret == 0) {
			ref->slot = slot;
			ref->handler = (void *)handler;
		}
		_neverc_krt_probes[slot].transitioning = 0;
		_neverc_krt_full_barrier();
		_neverc_krt_ctx_set_enabled(&_neverc_krt_probe_ctxs[slot], 1);
		_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_probe_lock,
						   flags);
		return ret;
	}

	slot = _neverc_krt_probe_alloc_slot();
	if (slot < 0) {
		_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_probe_lock,
						   flags);
		return NEVERC_KRT_INTERPOSE_E_ALLOC;
	}

	/* Initialize slot */
	{
		unsigned char *p = (unsigned char *)&_neverc_krt_probes[slot];
		unsigned long sz = sizeof(_neverc_krt_probes[slot]);
		unsigned long i;
		for (i = 0; i < sz; i++) p[i] = 0;
	}

	_neverc_krt_probes[slot].addr = addr;
	_neverc_krt_probes[slot].used = 1;
	_neverc_krt_probes[slot].transitioning = 1;

	_neverc_krt_probe_chain_add(&_neverc_krt_probes[slot].chain,
				    (void *)handler, priority);

	/*
	 * Release spinlock before install: _neverc_krt_patch_multi may call
	 * aarch64_insn_patch_text which uses stop_machine (needs sleepable
	 * context, cannot hold spinlock or have IRQs disabled).
	 */
	_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_probe_lock, flags);

	/* Install the probe (allocate stub, patch code) */
	ret = _neverc_krt_probe_install_slot(slot);
	if (ret != 0) {
		flags = _neverc_krt_spin_lock_irqsave(&_neverc_krt_probe_lock);
		if (_neverc_krt_ctx_install_retained(
				&_neverc_krt_probe_ctxs[slot], ret)) {
			_neverc_krt_probes[slot].faulted = 1;
			_neverc_krt_probes[slot].transitioning = 0;
			ref->slot = slot;
			ref->handler = (void *)handler;
			_neverc_krt_spin_unlock_irqrestore(
				&_neverc_krt_probe_lock, flags);
			return ret;
		}
		_neverc_krt_probes[slot].chain.count = 0;
		_neverc_krt_probes[slot].used = 0;
		_neverc_krt_probes[slot].transitioning = 0;
		_neverc_krt_probes[slot].faulted = 0;
		_neverc_krt_probes[slot].addr = (void *)0;
		_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_probe_lock,
						   flags);
		return ret;
	}

	flags = _neverc_krt_spin_lock_irqsave(&_neverc_krt_probe_lock);
	_neverc_krt_probes[slot].transitioning = 0;
	_neverc_krt_probes[slot].faulted = 0;
	_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_probe_lock, flags);

	ref->slot = slot;
	ref->handler = (void *)handler;

	return 0;
}

int neverc_krt_probe_register(void *addr,
			      neverc_krt_ctx_handler_t handler,
			      int priority,
			      struct neverc_krt_probe_ref *ref)
{
	int ret = _neverc_krt_lifecycle_begin();

	if (ret != NEVERC_KRT_INTERPOSE_OK)
		return ret;
	ret = _neverc_krt_probe_register_impl(addr, handler, priority, ref);
	_neverc_krt_lifecycle_end();
	return ret;
}

static int _neverc_krt_probe_unregister_impl(
			      struct neverc_krt_probe_ref *ref)
{
	unsigned long flags;
	int slot, ret;

	if (!ref)
		return -1;

	slot = ref->slot;
	if (slot < 0 || slot >= NEVERC_KRT_PROBE_MAX)
		return -1;

	flags = _neverc_krt_spin_lock_irqsave(&_neverc_krt_probe_lock);

	if (!_neverc_krt_probes[slot].used) {
		_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_probe_lock,
						   flags);
		return -2;
	}
	if (_neverc_krt_probes[slot].transitioning) {
		_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_probe_lock,
						   flags);
		return NEVERC_KRT_INTERPOSE_E_CONFLICT;
	}
	if (_neverc_krt_probes[slot].faulted &&
	    _neverc_krt_probes[slot].chain.count > 1) {
		_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_probe_lock, flags);
		return NEVERC_KRT_INTERPOSE_E_PATCH;
	}

	if (_neverc_krt_probes[slot].chain.count > 1) {
		_neverc_krt_probes[slot].transitioning = 1;
		_neverc_krt_ctx_set_enabled(&_neverc_krt_probe_ctxs[slot], 0);
		_neverc_krt_spin_unlock_irqrestore(
			&_neverc_krt_probe_lock, flags);
		_neverc_krt_full_barrier();
		ret = _neverc_krt_wait_one_ctx_inflight(
			&_neverc_krt_probe_ctxs[slot]);
		if (ret != NEVERC_KRT_INTERPOSE_OK) {
			flags = _neverc_krt_spin_lock_irqsave(
				&_neverc_krt_probe_lock);
			_neverc_krt_probes[slot].faulted = 1;
			_neverc_krt_probes[slot].transitioning = 0;
			_neverc_krt_spin_unlock_irqrestore(
				&_neverc_krt_probe_lock, flags);
			return ret;
		}

		flags = _neverc_krt_spin_lock_irqsave(
			&_neverc_krt_probe_lock);
		ret = _neverc_krt_probe_chain_remove(
			&_neverc_krt_probes[slot].chain, ref->handler);
		if (ret == 0) {
			ref->slot = -1;
			ref->handler = (void *)0;
		}
		_neverc_krt_probes[slot].transitioning = 0;
		_neverc_krt_full_barrier();
		_neverc_krt_ctx_set_enabled(&_neverc_krt_probe_ctxs[slot], 1);
		_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_probe_lock,
						   flags);
		return ret;
	}

	if (_neverc_krt_probes[slot].chain.count != 1 ||
	    _neverc_krt_probes[slot].chain.entries[0].handler != ref->handler) {
		_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_probe_lock,
						   flags);
		return -2;
	}

	_neverc_krt_probes[slot].transitioning = 1;
	_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_probe_lock, flags);

	_neverc_krt_probe_remove_slot(slot);

	flags = _neverc_krt_spin_lock_irqsave(&_neverc_krt_probe_lock);
	if (_neverc_krt_probe_ctxs[slot].base.active) {
		_neverc_krt_probes[slot].faulted = 1;
		_neverc_krt_probes[slot].transitioning = 0;
		_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_probe_lock,
						   flags);
		return NEVERC_KRT_INTERPOSE_E_PATCH;
	}

	ret = _neverc_krt_probe_chain_remove(
		&_neverc_krt_probes[slot].chain, ref->handler);
	if (ret == 0) {
		ref->slot = -1;
		ref->handler = (void *)0;
		_neverc_krt_probes[slot].used = 0;
		_neverc_krt_probes[slot].addr = (void *)0;
		_neverc_krt_probes[slot].faulted = 0;
	}
	_neverc_krt_probes[slot].transitioning = 0;
	_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_probe_lock, flags);
	return ret;
}

int neverc_krt_probe_unregister(struct neverc_krt_probe_ref *ref)
{
	int ret = _neverc_krt_lifecycle_begin();

	if (ret != NEVERC_KRT_INTERPOSE_OK)
		return ret;
	ret = _neverc_krt_probe_unregister_impl(ref);
	_neverc_krt_lifecycle_end();
	return ret;
}

int neverc_krt_probe_count(void *addr)
{
	unsigned long flags;
	int slot, cnt = 0;

	addr = (void *)neverc_krt_strip_pac((unsigned long)addr);
	flags = _neverc_krt_spin_lock_irqsave(&_neverc_krt_probe_lock);
	slot = _neverc_krt_probe_find_addr(addr);
	if (slot >= 0)
		cnt = _neverc_krt_probes[slot].chain.count;
	_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_probe_lock, flags);
	return cnt;
}

/* ================================================================ */

int neverc_krt_interpose_strerror(int err, char *buf, int sz)
{
	if (!buf || sz < 4) return -1;
	char c0 = 'E', c1 = '0' + ((-err) / 10), c2 = '0' + ((-err) % 10);
	if (err == 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }
	buf[0] = c0; buf[1] = c1; buf[2] = c2; buf[3] = '\0';
	return 3;
}

int neverc_krt_scan_strerror(int r, char *buf, int sz)
{
	if (!buf || sz < 4) return -1;
	char c0 = 'S', c1, c2;
	if (r >= 0) { c1 = '+'; c2 = '0' + (r % 10); }
	else        { c1 = '-'; c2 = '0' + ((-r) % 10); }
	buf[0] = c0; buf[1] = c1; buf[2] = c2; buf[3] = '\0';
	return 3;
}

static enum neverc_krt_pcrel neverc_krt_a64_classify(u32 i)
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

unsigned long neverc_krt_strip_pac(unsigned long addr)
{
	unsigned long tcr, va_bits, mask;
	__asm__ __volatile__("mrs %0, tcr_el1" : "=r"(tcr));
	va_bits = 64 - ((tcr >> 16) & 0x3FUL);
	if (va_bits >= 64)
		return addr;
	mask = (1UL << va_bits) - 1;

	int is_kernel = (addr >> 63) & 1;
	addr &= mask;
	if (is_kernel)
		addr |= ~mask;
	return addr;
}

static int neverc_krt_a64_is_ftrace_site(u32 *code)
{
	u32 insn;
	if (neverc_krt_mem_read(&insn, code, 4))
		return 0;
	if (insn == NEVERC_KRT_A64_BRK_FTRACE) return 1;
	if ((insn & 0xFC000000) == 0x94000000) {
		long imm26 = _neverc_krt_sext(insn & 0x3FFFFFF, 26);
		long off = imm26 << 2;
		if (off < -0x100000 || off > 0x100000) return 1;
	}
	return 0;
}

u32 neverc_krt_cfi_read_tag(void *func)
{
	u32 tag = 0;
	if (!func)
		return 0;
	unsigned long addr = neverc_krt_strip_pac((unsigned long)func);
	if (!_neverc_krt_is_kern_ptr(addr) || addr < sizeof(tag))
		return 0;
	neverc_krt_mem_read(&tag, (void *)(addr - 4), 4);
	return tag;
}

int neverc_krt_cfi_has_tag(void *func)
{
	u32 tag = neverc_krt_cfi_read_tag(func);
	return tag != 0 && tag != NEVERC_KRT_A64_NOP &&
	       !neverc_krt_a64_is_bti(tag);
}

static u64 neverc_krt_pool_alloc_count(void)
{
	return __atomic_load_n(&_neverc_krt_pool_alloc_total, __ATOMIC_RELAXED);
}

static u64 neverc_krt_pool_alloc_bytes(void)
{
	return __atomic_load_n(&_neverc_krt_pool_alloc_bytes, __ATOMIC_RELAXED);
}

static int _neverc_krt_cleanup_advanced(void)
{
	u64 ceiling = ~(u64)0;
	int failed = 0;

	for (;;) {
		unsigned long flags;
		void *owner = (void *)0;
		u64 sequence = 0;
		int kind = 0;
		int i;
		int ret;

		flags = _neverc_krt_spin_lock_irqsave(
			&_neverc_krt_advanced_lock);
		for (i = 0; i < _NEVERC_KRT_ADVANCED_MAX; i++) {
			struct _neverc_krt_advanced_owner *entry =
				&_neverc_krt_advanced[i];

			if (!entry->used || !entry->active ||
			    entry->sequence >= ceiling ||
			    entry->sequence <= sequence)
				continue;
			owner = entry->owner;
			kind = entry->kind;
			sequence = entry->sequence;
		}
		_neverc_krt_spin_unlock_irqrestore(
			&_neverc_krt_advanced_lock, flags);
		if (!owner)
			break;
		ceiling = sequence;

		switch (kind) {
		case _NEVERC_KRT_ADVANCED_CTX: {
			struct neverc_krt_interpose_ctx *one = owner;

			ret = _neverc_krt_interpose_remove_ctx_many_impl(&one, 1);
			if (ret == NEVERC_KRT_INTERPOSE_OK &&
			    !READ_ONCE(one->base.active))
				_neverc_krt_advanced_release(owner, kind);
			else
				failed = 1;
			break;
		}
		case _NEVERC_KRT_ADVANCED_FPTR:
			ret = _neverc_krt_fptr_restore_impl(owner);
			if (ret == NEVERC_KRT_INTERPOSE_OK &&
			    !READ_ONCE(
				((struct neverc_krt_fptr_interpose *)owner)->active))
				_neverc_krt_advanced_release(owner, kind);
			else
				failed = 1;
			break;
		case _NEVERC_KRT_ADVANCED_FTRACE:
			ret = _neverc_krt_ftrace_interpose_remove_impl(owner);
			if (ret == NEVERC_KRT_INTERPOSE_OK &&
			    !READ_ONCE(
				((struct neverc_krt_ftrace_interpose *)owner)->active))
				_neverc_krt_advanced_release(owner, kind);
			else
				failed = 1;
			break;
		default:
			failed = 1;
			break;
		}
	}
	return failed ? NEVERC_KRT_INTERPOSE_E_PATCH :
		NEVERC_KRT_INTERPOSE_OK;
}

static int _neverc_krt_active_owner_count(void)
{
	int count = 0;
	int i;

	for (i = 0; i < NEVERC_KRT_REGISTRY_MAX; i++)
		if (_neverc_krt_reg_ctxs[i].base.active)
			count++;
	for (i = 0; i < NEVERC_KRT_PROBE_MAX; i++)
		if (_neverc_krt_probe_ctxs[i].base.active)
			count++;
	for (i = 0; i < NEVERC_KRT_LL_MAX; i++)
		if (__atomic_load_n(&_neverc_krt_ll[i].used,
				    __ATOMIC_ACQUIRE))
			count++;
	for (i = 0; i < _NEVERC_KRT_ADVANCED_MAX; i++)
		if (_neverc_krt_advanced[i].used &&
		    _neverc_krt_advanced[i].active)
			count++;
	return count;
}

static int neverc_krt_pool_page_count(void)
{
	return _neverc_krt_pool_count;
}

int neverc_krt_interpose_cleanup(void)
{
	int i;
	int pass;
	int cleanup_failed = 0;
	int expected = _NEVERC_KRT_LIFECYCLE_RUNNING;
	unsigned long flags;

	if (!__atomic_compare_exchange_n(
		    &_neverc_krt_lifecycle_state, &expected,
		    _NEVERC_KRT_LIFECYCLE_STOPPING, 0,
		    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		if (expected == _NEVERC_KRT_LIFECYCLE_STOPPED)
			return NEVERC_KRT_INTERPOSE_OK;
		return NEVERC_KRT_INTERPOSE_E_CONFLICT;
	}
	WRITE_ONCE(_neverc_krt_inited, 0);
	_neverc_krt_full_barrier();
	while (__atomic_load_n(&_neverc_krt_lifecycle_ops, __ATOMIC_ACQUIRE)) {
		if (_neverc_krt_msleep)
			_neverc_krt_msleep(1);
		else
			__asm__ __volatile__("wfe" ::: "memory");
	}

	for (pass = 0;
	     pass < NEVERC_KRT_REGISTRY_MAX + NEVERC_KRT_PROBE_MAX +
			    NEVERC_KRT_LL_MAX + _NEVERC_KRT_ADVANCED_MAX;
	     pass++) {
		int before = _neverc_krt_active_owner_count();
		int after;

		cleanup_failed = 0;
		/*
		 * Advanced handles may sit above or below an SDK-owned hook on the
		 * same entry.  Try them on both sides of each internal LIFO pass;
		 * ownership checks turn a temporarily blocked restore into a retry,
		 * never a blind overwrite.
		 */
		(void)_neverc_krt_cleanup_advanced();

	{
		struct neverc_krt_interpose_ctx *batch[NEVERC_KRT_REGISTRY_MAX];
		int n = 0;
		for (i = NEVERC_KRT_REGISTRY_MAX - 1; i >= 0; i--) {
			if (!_neverc_krt_registry[i].used)
				continue;
			if (_neverc_krt_reg_ctxs[i].base.active)
				batch[n++] = &_neverc_krt_reg_ctxs[i];
		}
		if (n)
			_neverc_krt_interpose_remove_ctx_many_impl(batch, n);
		for (i = NEVERC_KRT_REGISTRY_MAX - 1; i >= 0; i--) {
			if (!_neverc_krt_registry[i].used)
				continue;
			if (_neverc_krt_reg_ctxs[i].base.active) {
				_neverc_krt_registry[i].faulted = 1;
				cleanup_failed = 1;
				continue;
			}
			_neverc_krt_registry[i].chain.count = 0;
			_neverc_krt_registry[i].target = (void *)0;
			_neverc_krt_registry[i].used = 0;
			_neverc_krt_registry[i].transitioning = 0;
			_neverc_krt_registry[i].faulted = 0;
		}
	}

	for (i = NEVERC_KRT_PROBE_MAX - 1; i >= 0; i--) {
		if (!_neverc_krt_probes[i].used)
			continue;
		_neverc_krt_probe_remove_slot(i);
		if (_neverc_krt_probe_ctxs[i].base.active) {
			_neverc_krt_probes[i].faulted = 1;
			cleanup_failed = 1;
			continue;
		}
		_neverc_krt_probes[i].chain.count = 0;
		_neverc_krt_probes[i].addr = (void *)0;
		_neverc_krt_probes[i].used = 0;
		_neverc_krt_probes[i].transitioning = 0;
		_neverc_krt_probes[i].faulted = 0;
	}

	for (i = NEVERC_KRT_LL_MAX - 1; i >= 0; i--) {
		struct neverc_krt_interpose *owner;

		if (!__atomic_load_n(&_neverc_krt_ll[i].used, __ATOMIC_ACQUIRE))
			continue;
		owner = READ_ONCE(_neverc_krt_ll[i].owner);
		if (owner)
			_neverc_krt_ll_remove(owner);
		if (__atomic_load_n(&_neverc_krt_ll[i].used, __ATOMIC_ACQUIRE))
			cleanup_failed = 1;
	}

		if (_neverc_krt_cleanup_advanced() !=
		    NEVERC_KRT_INTERPOSE_OK)
			cleanup_failed = 1;
		after = _neverc_krt_active_owner_count();
		if (after == 0) {
			cleanup_failed = 0;
			break;
		}
		if (after >= before) {
			cleanup_failed = 1;
			break;
		}
	}

	if (cleanup_failed) {
		WRITE_ONCE(_neverc_krt_inited, 1);
		__atomic_store_n(&_neverc_krt_lifecycle_state,
				 _NEVERC_KRT_LIFECYCLE_RUNNING,
				 __ATOMIC_RELEASE);
		return NEVERC_KRT_INTERPOSE_E_PATCH;
	}

	memset(_neverc_krt_advanced, 0, sizeof(_neverc_krt_advanced));
	_neverc_krt_advanced_sequence = 0;
	if (_neverc_krt_syncrcu) _neverc_krt_syncrcu();
	_neverc_krt_full_barrier();
	if (_neverc_krt_syncrcu) _neverc_krt_syncrcu();

	if (_neverc_krt_msleep) _neverc_krt_msleep(100);

	for (;;) {
		void *base;
		u32 magic;

		flags = _neverc_krt_spin_lock_irqsave(&_neverc_krt_pool_lock);
		if (_neverc_krt_pool_count == 0) {
			__atomic_store_n(&_neverc_krt_pool_alloc_total, 0,
					 __ATOMIC_RELAXED);
			__atomic_store_n(&_neverc_krt_pool_alloc_bytes, 0,
					 __ATOMIC_RELAXED);
			_neverc_krt_spin_unlock_irqrestore(
				&_neverc_krt_pool_lock, flags);
			break;
		}

		i = --_neverc_krt_pool_count;
		base = _neverc_krt_pool[i].base;
		magic = _neverc_krt_pool[i].magic;
		_neverc_krt_pool[i].base = (void *)0;
		_neverc_krt_pool[i].used = 0;
		_neverc_krt_pool[i].refcnt = 0;
		_neverc_krt_pool[i].magic = 0;
		_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_pool_lock, flags);

		if (base && _neverc_krt_modfree &&
		    magic == _NEVERC_KRT_POOL_MAGIC)
			_neverc_krt_modfree(base);
	}
	__atomic_store_n(&_neverc_krt_lifecycle_state,
			 _NEVERC_KRT_LIFECYCLE_STOPPED, __ATOMIC_RELEASE);
	return NEVERC_KRT_INTERPOSE_OK;
}

#undef NEVERC_KRT_INTERPOSE_FORCE_INLINE
