/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>
#include <linux/string.h>

#include <nvk_internal.h>

/* ---- pool / cache state ---- */

static int          _neverc_krt_pool_count;
static volatile u64 _neverc_krt_pool_alloc_total;
static volatile u64 _neverc_krt_pool_alloc_bytes;

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

/* ---- ARM64 instruction helpers (internal to nvk_hook.c) ---- */

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
	if ((insn & 0x7FE0FFE0) == 0x2A0003E0) return 1;
	if ((insn & 0xFFE0FFE0) == 0xAA0003E0) return 1;
	return 0;
}

static __always_inline int neverc_krt_a64_is_scs_push(u32 insn)
{
	if (insn == 0xF800841EU) return 1;
	if (insn == 0xF81F0A5EU) return 1;
	if (insn == 0xF900025EU) return 1;
	return 0;
}

static __always_inline int neverc_krt_a64_is_hook_patch(u32 insn)
{
	if (insn == NEVERC_KRT_A64_BRK_KPROBE) return 1;
	if (insn == 0x58000050U) return 1;
	return 0;
}

static __always_inline int neverc_krt_a64_is_kprobe_bp(u32 insn)
{
	return insn == NEVERC_KRT_A64_BRK_KPROBE
	    || (insn & 0xFFE0001FU) == 0xD4200000U;
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
	if ((insn & 0xFE200000U) == 0xD4200000U) return 1;
	return 0;
}

/* ---- context stub template ---- */

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
	/* 56 */ _A64E_CBNZ_FWD(1, 85-56),  /* -> force_jump path */
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

/* ---- internal typedefs ---- */

typedef void *(*neverc_krt_modalloc_fn)(unsigned long);
typedef void *(*neverc_krt_execmem_alloc_fn)(int type, unsigned long size);
typedef void  (*neverc_krt_modfree_fn)(void *);
typedef void  (*neverc_krt_flushic_fn)(unsigned long, unsigned long);
typedef int   (*neverc_krt_patchtext_fn)(void **, u32 *, int);
typedef int   (*neverc_krt_patchtns_fn)(void *, u32);
typedef void  (*neverc_krt_syncrcu_fn)(void);
typedef void  (*neverc_krt_msleep_fn)(unsigned int);
typedef int   (*neverc_krt_ksize_fn)(unsigned long addr, unsigned long *sz,
				     unsigned long *off);
typedef int   (*neverc_krt_register_ftrace_fn)(struct neverc_krt_ftrace_ops *ops);
typedef int   (*neverc_krt_unregister_ftrace_fn)(struct neverc_krt_ftrace_ops *ops);
typedef int   (*neverc_krt_ftrace_set_filter_ip_fn)(struct neverc_krt_ftrace_ops *ops,
						    unsigned long ip,
						    int remove, int reset);
typedef int   (*neverc_krt_ftrace_set_fn)(unsigned long ip, int enable);
typedef int   (*neverc_krt_register_kprobe_fn)(void *kp);
typedef void  (*neverc_krt_unregister_kprobe_fn)(void *kp);

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
static neverc_krt_patchtns_fn       _neverc_krt_patchtns;
static neverc_krt_syncrcu_fn        _neverc_krt_syncrcu;
static neverc_krt_msleep_fn         _neverc_krt_msleep;
static int                          _neverc_krt_inited;
static neverc_krt_ksize_fn          _neverc_krt_ksize;

static volatile int                 _neverc_krt_pool_lock;
static struct _neverc_krt_pool_page _neverc_krt_pool[_NEVERC_KRT_POOL_MAX];
static int                          _neverc_krt_pool_pgsz;
static volatile u64                 _neverc_krt_pool_alloc_fail;
static volatile u64                 _neverc_krt_hook_install_cnt;
static volatile u64                 _neverc_krt_hook_remove_cnt;

static neverc_krt_register_ftrace_fn     _neverc_krt_register_ftrace;
static neverc_krt_unregister_ftrace_fn   _neverc_krt_unregister_ftrace;
static neverc_krt_ftrace_set_filter_ip_fn _neverc_krt_ftrace_set_filter;
static neverc_krt_ftrace_set_fn           _neverc_krt_ftrace_set_ip;
static int                                _neverc_krt_ftrace_avail;

static neverc_krt_register_kprobe_fn   _neverc_krt_reg_kprobe;
static neverc_krt_unregister_kprobe_fn _neverc_krt_unreg_kprobe;

/* ---- static forward declarations ---- */

static void *_neverc_krt_alloc_exec(unsigned long size);
static u32 *_neverc_krt_pool_alloc(int bytes);
static void _neverc_krt_pool_free(u32 *ptr);
static unsigned long _neverc_krt_fn_size(void *addr);
static void _neverc_krt_write_insn(void *addr, u32 insn);
static int _neverc_krt_verify_patch(u32 *target, u32 *expected, int count);
static void _neverc_krt_scan_entry(const u32 *buf, int *skip, int *total);
static void _neverc_krt_patch_mov64(u32 *page, int slot, int rd, u64 addr);
static void _neverc_krt_full_barrier(void);
static void _neverc_krt_quiesce(void);
static void _neverc_krt_quiesce_deep(void);
static void _neverc_krt_poison_tramp(u32 *tramp, int max_words);
static void _neverc_krt_ftrace_thunk(unsigned long ip, unsigned long parent_ip,
				     void *ops, void *regs);
static int neverc_krt_a64_gen_mov64(u32 *out, int rd, u64 addr);
static int neverc_krt_a64_relocate_abs(u32 insn, unsigned long old_pc, u32 *out);
static enum neverc_krt_pcrel neverc_krt_a64_classify(u32 i);
static int neverc_krt_a64_is_kcfi_tag(u32 *addr);
static int neverc_krt_a64_is_ftrace_site(u32 *code);

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

static int neverc_krt_a64_relocate_abs(u32 insn, unsigned long old_pc, u32 *out)
{
	enum neverc_krt_pcrel kind = neverc_krt_a64_classify(insn);
	unsigned long target;
	int n = 0;

	switch (kind) {

	case NEVERC_KRT_PC_NONE:
		out[n++] = insn;
		return n;

	case NEVERC_KRT_PC_ADRP: {
		int immlo = (insn >> 29) & 3;
		long immhi = neverc_krt_sext((insn >> 5) & 0x7FFFF, 19);
		target = (old_pc & ~0xFFFUL) + (((immhi << 2) | immlo) << 12);
		int rd = insn & 0x1F;
		n = neverc_krt_a64_gen_mov64(out, rd, target);
		return n;
	}

	case NEVERC_KRT_PC_ADR: {
		int immlo = (insn >> 29) & 3;
		long immhi = neverc_krt_sext((insn >> 5) & 0x7FFFF, 19);
		target = old_pc + ((immhi << 2) | immlo);
		int rd = insn & 0x1F;
		return neverc_krt_a64_gen_mov64(out, rd, target);
	}

	case NEVERC_KRT_PC_B: {
		long imm26 = neverc_krt_sext(insn & 0x3FFFFFF, 26);
		target = old_pc + (imm26 << 2);
		n = neverc_krt_a64_gen_mov64(out, 17, target);
		out[n++] = NEVERC_KRT_A64_RET_X17;
		return n;
	}

	case NEVERC_KRT_PC_BL: {
		long imm26 = neverc_krt_sext(insn & 0x3FFFFFF, 26);
		target = old_pc + (imm26 << 2);
		n = neverc_krt_a64_gen_mov64(out, 17, target);
		out[n++] = 0xD63F0220U;  /* BLR X17 */
		return n;
	}

	case NEVERC_KRT_PC_BCOND: {
		long imm19 = neverc_krt_sext((insn >> 5) & 0x7FFFF, 19);
		target = old_pc + (imm19 << 2);
		u32 inv = (insn & 0xFF00000FU) ^ 1U;  /* invert LSB of cond */
		int skip_n = 1 + 3 + 1;  /* worst case: MOVZ+2MOVK+RET = 4/5 */
		/* We'll fix the skip offset after emitting the jump. */
		int cond_slot = n;
		out[n++] = 0; /* placeholder */
		int jump_start = n;
		n += neverc_krt_a64_gen_mov64(&out[n], 17, target);
		out[n++] = NEVERC_KRT_A64_RET_X17;
		skip_n = n - jump_start;
		/* B.!cond skip:  imm19 = skip_n, shifted left 5 */
		out[cond_slot] = inv | (((u32)skip_n & 0x7FFFF) << 5);
		return n;
	}

	case NEVERC_KRT_PC_CBZ: {
		long imm19 = neverc_krt_sext((insn >> 5) & 0x7FFFF, 19);
		target = old_pc + (imm19 << 2);
		/* Invert CBZ<->CBNZ: toggle bit 24 */
		u32 inv = insn ^ 0x01000000U;
		inv &= ~(0x7FFFFU << 5);  /* clear imm19 */
		int cond_slot = n;
		out[n++] = 0; /* placeholder */
		int jump_start = n;
		n += neverc_krt_a64_gen_mov64(&out[n], 17, target);
		out[n++] = NEVERC_KRT_A64_RET_X17;
		int skip_n = n - jump_start;
		out[cond_slot] = inv | (((u32)skip_n & 0x7FFFF) << 5);
		return n;
	}

	case NEVERC_KRT_PC_TBZ: {
		long imm14 = neverc_krt_sext((insn >> 5) & 0x3FFF, 14);
		target = old_pc + (imm14 << 2);
		/* Invert TBZ<->TBNZ: toggle bit 24 */
		u32 inv = insn ^ 0x01000000U;
		inv &= ~(0x3FFFU << 5);  /* clear imm14 */
		int cond_slot = n;
		out[n++] = 0; /* placeholder */
		int jump_start = n;
		n += neverc_krt_a64_gen_mov64(&out[n], 17, target);
		out[n++] = NEVERC_KRT_A64_RET_X17;
		int skip_n = n - jump_start;
		out[cond_slot] = inv | (((u32)skip_n & 0x3FFFU) << 5);
		return n;
	}

	case NEVERC_KRT_PC_LDR_LIT: {
		long imm19 = neverc_krt_sext((insn >> 5) & 0x7FFFF, 19);
		target = old_pc + (imm19 << 2);
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
		long imm19 = neverc_krt_sext((insn >> 5) & 0x7FFFF, 19);
		target = old_pc + (imm19 << 2);
		int rt = insn & 0x1F;
		n = neverc_krt_a64_gen_mov64(out, 17, target);
		out[n++] = 0xB9800000U | (17 << 5) | rt; /* LDRSW Xt, [X17] */
		return n;
	}

	case NEVERC_KRT_PC_PRFM_LIT: {
		long imm19 = neverc_krt_sext((insn >> 5) & 0x7FFFF, 19);
		target = old_pc + (imm19 << 2);
		int rt = insn & 0x1F;
		n = neverc_krt_a64_gen_mov64(out, 17, target);
		out[n++] = 0xF9800000U | (17 << 5) | rt; /* PRFM type, [X17] */
		return n;
	}
	}

	out[0] = insn;
	return 1;
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
				for (w = 0; w < sz / 4; w++)
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

int neverc_krt_hook_init(void)
{
	if (_neverc_krt_inited) return 0;
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
	_neverc_krt_patchtext = (neverc_krt_patchtext_fn)NEVERC_KRT_LOOKUP("aarch64_insn_patch_text");
	_neverc_krt_patchtns  = (neverc_krt_patchtns_fn)NEVERC_KRT_LOOKUP("aarch64_insn_patch_text_nosync");
	_neverc_krt_ksize = (neverc_krt_ksize_fn)NEVERC_KRT_LOOKUP("kallsyms_lookup_size_offset");
	_neverc_krt_syncrcu = (neverc_krt_syncrcu_fn)NEVERC_KRT_LOOKUP("synchronize_rcu");
	_neverc_krt_msleep  = (neverc_krt_msleep_fn)NEVERC_KRT_LOOKUP("msleep");
	if ((!_neverc_krt_modalloc && !_neverc_krt_execmem_alloc) ||
	    !_neverc_krt_modfree || !_neverc_krt_flushic)
		return -1;
	_neverc_krt_inited = 1;
	return 0;
}

static unsigned long _neverc_krt_fn_size(void *addr)
{
	unsigned long sz = 0, off = 0;
	if (_neverc_krt_ksize && _neverc_krt_ksize((unsigned long)addr, &sz, &off))
		return sz - off;
	return 0;
}

static void _neverc_krt_write_insn(void *addr, u32 insn)
{
	int need_prot;

	if (_neverc_krt_patchtns) {
		_neverc_krt_patchtns(addr, insn);
		return;
	}

	need_prot = neverc_krt_mem_make_rw((unsigned long)addr);
	*(volatile u32 *)addr = insn;
	_neverc_krt_dcache_clean((unsigned long)addr,
			  (unsigned long)addr + 4);
	if (_neverc_krt_flushic)
		_neverc_krt_flushic((unsigned long)addr,
			     (unsigned long)addr + 4);
	else
		_neverc_krt_icache_inval((unsigned long)addr,
				  (unsigned long)addr + 4);
	if (need_prot == 0)
		neverc_krt_mem_make_ro((unsigned long)addr);
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
	int i;

	if (_neverc_krt_patchtext && count <= 16) {
		void *addrs[16];
		for (i = 0; i < count; i++)
			addrs[i] = &target[i];
		int ret = _neverc_krt_patchtext(addrs, insns, count);
		if (ret == 0 && _neverc_krt_verify_patch(target, insns, count) == 0)
			return 0;
	}

	unsigned long pgsz = _neverc_krt_mem_get_page_size();
	unsigned long start = (unsigned long)target & ~(pgsz - 1);
	unsigned long end = ((unsigned long)&target[count - 1]) & ~(pgsz - 1);
	unsigned long p;
	for (p = start; p <= end; p += pgsz)
		neverc_krt_mem_make_rw(p);

	for (i = 0; i < count; i++)
		_neverc_krt_write_insn(&target[i], insns[i]);

	for (p = start; p <= end; p += pgsz)
		neverc_krt_mem_make_ro(p);

	return _neverc_krt_verify_patch(target, insns, count);
}

static void _neverc_krt_scan_entry(const u32 *buf, int *skip, int *total)
{
	int s = 0;
	while (s < NEVERC_KRT_HOOK_MAX_PATCH - 4) {
		u32 insn = buf[s];
		if (neverc_krt_a64_is_bti(insn) || insn == NEVERC_KRT_A64_NOP ||
		    neverc_krt_a64_is_pac_sign(insn) || neverc_krt_a64_is_stp_fp_lr(insn) ||
		    neverc_krt_a64_is_frame_setup(insn) || neverc_krt_a64_is_scs_push(insn))
			s++;
		else
			break;
	}
	*total = s + 4;
	if (*total > NEVERC_KRT_HOOK_MAX_PATCH)
		*total = NEVERC_KRT_HOOK_MAX_PATCH;
	if (*total < 4)
		*total = 4;
	*skip = s;
}

enum neverc_krt_scan_result neverc_krt_hook_scan(void *target)
{
	target = (void *)neverc_krt_strip_pac((unsigned long)target);
	u32 *code = (u32 *)target;
	int skip, patch_count, i;

	u32 scan_buf[NEVERC_KRT_HOOK_MAX_PATCH];
	if (neverc_krt_mem_read(scan_buf, code, sizeof(scan_buf)))
		return NEVERC_KRT_SCAN_TOO_SHORT;

	if (neverc_krt_a64_is_hook_patch(scan_buf[0]))
		return NEVERC_KRT_SCAN_ALREADY_HOOKED;

	if (neverc_krt_a64_is_kprobe_bp(scan_buf[0]))
		return NEVERC_KRT_SCAN_KPROBE_ACTIVE;

	if (neverc_krt_a64_is_ftrace_site(code))
		return NEVERC_KRT_SCAN_FTRACE_ACTIVE;

	if (_neverc_krt_ksize) {
		unsigned long fn_sz = _neverc_krt_fn_size(target);
		if (fn_sz > 0 && fn_sz < NEVERC_KRT_HOOK_MAX_PATCH * 4)
			return NEVERC_KRT_SCAN_TOO_SHORT;
	}

	_neverc_krt_scan_entry(scan_buf, &skip, &patch_count);

	for (i = 0; i < patch_count; i++) {
		u32 insn = scan_buf[i];
		if (neverc_krt_a64_is_bti(insn) || insn == NEVERC_KRT_A64_NOP)
			continue;
		if (neverc_krt_a64_is_hazardous(insn))
			return NEVERC_KRT_SCAN_HAZARDOUS;
		u32 tmp[8];
		int n = neverc_krt_a64_relocate_abs(
			insn, (unsigned long)&code[i], tmp);
		if (n == 0) return NEVERC_KRT_SCAN_UNRELOCATABLE;
	}

	return NEVERC_KRT_SCAN_OK;
}

int neverc_krt_hook_install(struct neverc_krt_hook *h, void *target,
			    void *replace, void **orig)
{
	target = (void *)neverc_krt_strip_pac((unsigned long)target);
	u32 *code = (u32 *)target;
	u32  tramp[NEVERC_KRT_HOOK_TRAMP_CAP];
	int  tidx = 0, skip, patch_count, i;
	u32  patch[NEVERC_KRT_HOOK_MAX_PATCH];
	int  use_short_b = 0;

	if (!_neverc_krt_inited) return NEVERC_KRT_HOOK_E_NOINIT;
	if (!target || !replace || !orig) return NEVERC_KRT_HOOK_E_SHORT;
	if (((unsigned long)target & 3) != 0) return NEVERC_KRT_HOOK_E_SHORT;
	if (!_neverc_krt_is_kern_ptr((unsigned long)target))
		return NEVERC_KRT_HOOK_E_SHORT;

	u32 ibuf[NEVERC_KRT_HOOK_MAX_PATCH];
	if (neverc_krt_mem_read(ibuf, code, sizeof(ibuf)))
		return NEVERC_KRT_HOOK_E_SHORT;

	if (neverc_krt_a64_is_kprobe_bp(ibuf[0]))
		return NEVERC_KRT_HOOK_E_CONFLICT;

	h->target = target;
	h->replace = replace;
	h->trampoline = (void *)0;
	h->active = 0;
	h->short_b = 0;
	h->hit_count = 0;
	h->guard = 0;

	{
		int chained = neverc_krt_a64_is_hook_patch(ibuf[0]);

		_neverc_krt_scan_entry(ibuf, &skip, &patch_count);

		if (!chained) {
			unsigned long fn_sz = _neverc_krt_fn_size(target);
			if (fn_sz > 0 &&
			    fn_sz < (unsigned long)patch_count * 4) {
				int min_pc = skip + 1;
				long b_off = (long)(unsigned long)replace -
					(long)(unsigned long)&code[skip];
				if (fn_sz >= (unsigned long)min_pc * 4 &&
				    neverc_krt_a64_b_in_range(b_off)) {
					use_short_b = 1;
					patch_count = min_pc;
				} else {
					return NEVERC_KRT_HOOK_E_SHORT;
				}
			}
		}

		h->patch_count = patch_count;
		h->short_b = use_short_b;
		for (i = 0; i < patch_count; i++)
			h->orig_insns[i] = ibuf[i];

		tramp[tidx++] = NEVERC_KRT_A64_BTI_JC;

		if (chained) {
			int qoff = 0;
			if (neverc_krt_a64_is_bti(ibuf[0])) qoff = 1;
			unsigned long prev;
			if (neverc_krt_mem_read(&prev, &code[qoff + 2], 8))
				return NEVERC_KRT_HOOK_E_SHORT;
			tidx += neverc_krt_a64_gen_mov64(&tramp[tidx], 17, prev);
			tramp[tidx++] = NEVERC_KRT_A64_RET_X17;
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
					return NEVERC_KRT_HOOK_E_RELOC;
				unsigned long insn_pc =
					(unsigned long)&code[i];
				int n = neverc_krt_a64_relocate_abs(
					insn, insn_pc, &tramp[tidx]);
				if (n == 0) return NEVERC_KRT_HOOK_E_RELOC;
				tidx += n;
				if (tidx >= NEVERC_KRT_HOOK_TRAMP_CAP - 8)
					return NEVERC_KRT_HOOK_E_RELOC;
			}
			unsigned long back =
				(unsigned long)target + patch_count * 4;
			tidx += neverc_krt_a64_gen_mov64(&tramp[tidx], 17, back);
			tramp[tidx++] = NEVERC_KRT_A64_RET_X17;
		}
	}

	h->trampoline = _neverc_krt_pool_alloc(tidx * 4);
	if (!h->trampoline) return NEVERC_KRT_HOOK_E_ALLOC;

	for (i = 0; i < tidx; i++)
		h->trampoline[i] = tramp[i];
	_neverc_krt_dcache_clean((unsigned long)h->trampoline,
			  (unsigned long)&h->trampoline[tidx]);
	if (_neverc_krt_flushic)
		_neverc_krt_flushic((unsigned long)h->trampoline,
			     (unsigned long)&h->trampoline[tidx]);
	else
		_neverc_krt_icache_inval((unsigned long)h->trampoline,
				  (unsigned long)&h->trampoline[tidx]);

	*orig = (void *)h->trampoline;

	for (i = 0; i < patch_count; i++)
		patch[i] = NEVERC_KRT_A64_NOP;
	{
		int jmp = 0;
		if (neverc_krt_a64_is_bti(ibuf[0])) {
			patch[0] = ibuf[0];
			jmp = 1;
		}
		if (use_short_b) {
			long b_off = (long)(unsigned long)replace -
				(long)(unsigned long)&code[jmp];
			patch[jmp] = neverc_krt_a64_gen_b(b_off);
		} else {
			patch[jmp + 0] = 0x58000050U;
			patch[jmp + 1] = 0xD61F0200U;
			*(unsigned long *)&patch[jmp + 2] =
				(unsigned long)replace;
		}
	}

	if (_neverc_krt_patch_multi(code, patch, patch_count) != 0) {
		_neverc_krt_pool_free(h->trampoline);
		h->trampoline = (void *)0;
		return NEVERC_KRT_HOOK_E_PATCH;
	}

	h->active = 1;
	h->enabled = 1;
	__atomic_fetch_add(&_neverc_krt_hook_install_cnt, 1, __ATOMIC_RELAXED);
	return NEVERC_KRT_HOOK_OK;
}

static void _neverc_krt_patch_mov64(u32 *page, int slot, int rd, u64 addr)
{
	u32 buf[4];
	int n = neverc_krt_a64_gen_mov64(buf, rd, addr), i;
	for (i = 0; i < n && i < 4; i++)
		page[slot + i] = buf[i];
}

int neverc_krt_hook_install_ctx(struct neverc_krt_hook_ctx *h, void *target,
				neverc_krt_ctx_handler_t handler, void **call_orig)
{
	target = (void *)neverc_krt_strip_pac((unsigned long)target);
	u32 *code = (u32 *)target;
	u32  tramp[NEVERC_KRT_HOOK_TRAMP_CAP];
	int  tidx = 0, skip, patch_count, i, n;
	u32  patch[NEVERC_KRT_HOOK_MAX_PATCH];
	u32 *page;

	if (!_neverc_krt_inited) return NEVERC_KRT_HOOK_E_NOINIT;
	if (!target || !handler) return NEVERC_KRT_HOOK_E_SHORT;
	if (((unsigned long)target & 3) != 0) return NEVERC_KRT_HOOK_E_SHORT;
	if (!_neverc_krt_is_kern_ptr((unsigned long)target))
		return NEVERC_KRT_HOOK_E_SHORT;

	u32 ibuf[NEVERC_KRT_HOOK_MAX_PATCH];
	if (neverc_krt_mem_read(ibuf, code, sizeof(ibuf)))
		return NEVERC_KRT_HOOK_E_SHORT;

	if (neverc_krt_a64_is_kprobe_bp(ibuf[0]))
		return NEVERC_KRT_HOOK_E_CONFLICT;

	h->base.target = target;
	h->base.replace = (void *)handler;
	h->base.trampoline = (void *)0;
	h->base.active = 0;
	h->stub = (void *)0;
	h->guard_task = 0;

	{
		int chained = neverc_krt_a64_is_hook_patch(ibuf[0]);
		if (!chained) {
			unsigned long fn_sz = _neverc_krt_fn_size(target);
			if (fn_sz > 0 && fn_sz < NEVERC_KRT_HOOK_MAX_PATCH * 4)
				return NEVERC_KRT_HOOK_E_SHORT;
		}

		_neverc_krt_scan_entry(ibuf, &skip, &patch_count);
		h->base.patch_count = patch_count;
		for (i = 0; i < patch_count; i++)
			h->base.orig_insns[i] = ibuf[i];

		tramp[tidx++] = NEVERC_KRT_A64_BTI_JC;

		if (chained) {
			int qoff = 0;
			if (neverc_krt_a64_is_bti(ibuf[0])) qoff = 1;
			unsigned long prev;
			if (neverc_krt_mem_read(&prev, &code[qoff + 2], 8))
				return NEVERC_KRT_HOOK_E_SHORT;
			tidx += neverc_krt_a64_gen_mov64(&tramp[tidx], 17, prev);
			tramp[tidx++] = NEVERC_KRT_A64_RET_X17;
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
					return NEVERC_KRT_HOOK_E_RELOC;
				n = neverc_krt_a64_relocate_abs(
					insn, (unsigned long)&code[i],
					&tramp[tidx]);
				if (n == 0) return NEVERC_KRT_HOOK_E_RELOC;
				tidx += n;
				if (tidx >= NEVERC_KRT_HOOK_TRAMP_CAP - 8)
					return NEVERC_KRT_HOOK_E_RELOC;
			}
			unsigned long back =
				(unsigned long)target + patch_count * 4;
			tidx += neverc_krt_a64_gen_mov64(&tramp[tidx], 17, back);
			tramp[tidx++] = NEVERC_KRT_A64_RET_X17;
		}
	}

	{
		int page_sz = (_CTX_STUB_LEN + 4 + tidx + 4) * 4;
		page_sz = (page_sz + 63) & ~63; /* cache-line align */
		if (page_sz < 512) page_sz = 512;
		page = (u32 *)_neverc_krt_alloc_exec(page_sz);
		if (!page) return NEVERC_KRT_HOOK_E_ALLOC;
	}

	h->stub = page;
	h->tramp_code = page + _CTX_STUB_LEN + 4;
	h->base.trampoline = page;

	for (i = 0; i < (int)_CTX_STUB_LEN; i++)
		page[i] = _neverc_krt_ctx_stub_template[i];

	_neverc_krt_patch_mov64(page, _CTX_ENABLED_SLOT, 16,
			 (u64)(unsigned long)&h->base.enabled);
	_neverc_krt_patch_mov64(page, _CTX_GUARD_SLOT_A, 17,
			 (u64)(unsigned long)&h->guard_task);
	_neverc_krt_patch_mov64(page, _CTX_GUARD_SLOT_B, 19,
			 (u64)(unsigned long)&h->guard_task);
	_neverc_krt_patch_mov64(page, _CTX_HANDLER_SLOT, 3,
			 (u64)(unsigned long)handler);
	_neverc_krt_patch_mov64(page, _CTX_TRAMP_SLOT_A, 17,
			 (u64)(unsigned long)h->tramp_code);
	_neverc_krt_patch_mov64(page, _CTX_TRAMP_SLOT_B, 17,
			 (u64)(unsigned long)h->tramp_code);

	for (i = 0; i < tidx; i++)
		h->tramp_code[i] = tramp[i];

	{
		unsigned long flush_end = (unsigned long)&h->tramp_code[tidx];
		_neverc_krt_dcache_clean((unsigned long)page, flush_end);
		if (_neverc_krt_flushic)
			_neverc_krt_flushic((unsigned long)page, flush_end);
		else
			_neverc_krt_icache_inval((unsigned long)page, flush_end);
	}

	if (call_orig)
		*call_orig = (void *)h->tramp_code;

	for (i = 0; i < patch_count; i++)
		patch[i] = NEVERC_KRT_A64_NOP;
	{
		int jmp = 0;
		if (neverc_krt_a64_is_bti(ibuf[0])) {
			patch[0] = ibuf[0];
			jmp = 1;
		}
		patch[jmp + 0] = 0x58000050U;  /* LDR X16, [PC, #8] */
		patch[jmp + 1] = 0xD61F0200U;  /* BR  X16 */
		*(unsigned long *)&patch[jmp + 2] = (unsigned long)h->stub;
	}

	if (_neverc_krt_patch_multi(code, patch, patch_count) != 0) {
		if (_neverc_krt_modfree) _neverc_krt_modfree(page);
		h->stub = (void *)0;
		return NEVERC_KRT_HOOK_E_PATCH;
	}

	h->base.active = 1;
	h->base.enabled = 1;
	__atomic_fetch_add(&_neverc_krt_hook_install_cnt, 1, __ATOMIC_RELAXED);
	return NEVERC_KRT_HOOK_OK;
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
		return;
	}
	if (_neverc_krt_msleep)
		_neverc_krt_msleep(100);
	_neverc_krt_full_barrier();
}

static void _neverc_krt_quiesce_deep(void)
{
	_neverc_krt_quiesce();
	_neverc_krt_full_barrier();
	_neverc_krt_quiesce();
	_neverc_krt_full_barrier();
}

void neverc_krt_hook_pause(struct neverc_krt_hook *h)
{
	WRITE_ONCE(h->enabled, 0);
	_neverc_krt_full_barrier();
	_neverc_krt_quiesce();
	_neverc_krt_full_barrier();
}

void neverc_krt_hook_resume(struct neverc_krt_hook *h)
{
	_neverc_krt_full_barrier();
	WRITE_ONCE(h->enabled, 1);
}

static void _neverc_krt_poison_tramp(u32 *tramp, int max_words)
{
	int i;
	for (i = 0; i < max_words; i++) {
		u32 insn;
		if (neverc_krt_mem_read(&insn, &tramp[i], 4)) break;
		if (insn == 0) break;
		tramp[i] = 0xD4200000U | (0xDEADU << 5); /* BRK #0xDEAD */
	}
	_neverc_krt_dcache_clean((unsigned long)tramp,
			  (unsigned long)&tramp[i]);
	if (_neverc_krt_flushic)
		_neverc_krt_flushic((unsigned long)tramp,
			     (unsigned long)&tramp[i]);
}

void neverc_krt_hook_remove(struct neverc_krt_hook *h)
{
	if (!h->active) return;

	WRITE_ONCE(h->enabled, 0);
	__atomic_store_n(&h->guard, 0, __ATOMIC_RELEASE);
	_neverc_krt_full_barrier();
	__asm__ __volatile__("sev" ::: "memory");

	_neverc_krt_quiesce_deep();

	u32 *code = (u32 *)h->target;
	_neverc_krt_patch_multi(code, h->orig_insns, h->patch_count);
	_neverc_krt_tlbi_range((unsigned long)code,
			(unsigned long)&code[h->patch_count]);

	_neverc_krt_quiesce_deep();

	if (h->trampoline) {
		_neverc_krt_poison_tramp(h->trampoline, NEVERC_KRT_HOOK_TRAMP_CAP);
		_neverc_krt_quiesce();
		_neverc_krt_pool_free(h->trampoline);
		h->trampoline = (void *)0;
	}
	h->active = 0;
	__atomic_fetch_add(&_neverc_krt_hook_remove_cnt, 1, __ATOMIC_RELAXED);
}

int neverc_krt_hook_replace(struct neverc_krt_hook *h, void *new_replace,
			    void **new_orig)
{
	u32 patch[NEVERC_KRT_HOOK_MAX_PATCH];
	u32 *code;
	int i, jmp;

	if (!h->active) return -1;

	code = (u32 *)h->target;

	{
		int tampered = 0;
		u32 cur;
		int vslot = neverc_krt_a64_is_bti(h->orig_insns[0]) ? 1 : 0;
		if (!neverc_krt_mem_read(&cur, &code[vslot], 4)) {
			if (cur != 0x58000050U) {
				long b_off = (long)(unsigned long)h->replace -
					(long)(unsigned long)&code[vslot];
				u32 expected_b = neverc_krt_a64_gen_b(b_off);
				if (cur != expected_b) tampered = 1;
			}
		}
		if (tampered) return -2;
	}

	WRITE_ONCE(h->enabled, 0);
	_neverc_krt_full_barrier();
	_neverc_krt_quiesce_deep();

	h->replace = new_replace;

	for (i = 0; i < h->patch_count; i++)
		patch[i] = NEVERC_KRT_A64_NOP;
	jmp = 0;
	if (neverc_krt_a64_is_bti(h->orig_insns[0])) {
		patch[0] = h->orig_insns[0];
		jmp = 1;
	}
	if (h->short_b) {
		long b_off = (long)(unsigned long)new_replace -
			(long)(unsigned long)&code[jmp];
		if (!neverc_krt_a64_b_in_range(b_off))
			return -1;
		patch[jmp] = neverc_krt_a64_gen_b(b_off);
	} else {
		patch[jmp + 0] = 0x58000050U;
		patch[jmp + 1] = 0xD61F0200U;
		*(unsigned long *)&patch[jmp + 2] =
			(unsigned long)new_replace;
	}

	_neverc_krt_patch_multi(code, patch, h->patch_count);
	_neverc_krt_full_barrier();

	if (new_orig)
		*new_orig = (void *)h->trampoline;
	WRITE_ONCE(h->enabled, 1);
	return 0;
}

void neverc_krt_hook_remove_ctx(struct neverc_krt_hook_ctx *h)
{
	if (!h->base.active) return;

	WRITE_ONCE(h->base.enabled, 0);
	WRITE_ONCE(h->guard_task, 0);
	_neverc_krt_full_barrier();
	__asm__ __volatile__("sev" ::: "memory");

	_neverc_krt_quiesce_deep();

	u32 *code = (u32 *)h->base.target;
	_neverc_krt_patch_multi(code, h->base.orig_insns, h->base.patch_count);
	_neverc_krt_tlbi_range((unsigned long)code,
			(unsigned long)&code[h->base.patch_count]);

	_neverc_krt_quiesce_deep();

	if (h->stub) {
		_neverc_krt_poison_tramp(h->stub, (int)_CTX_STUB_LEN + 8);
		if (h->tramp_code)
			_neverc_krt_poison_tramp(h->tramp_code,
					  NEVERC_KRT_HOOK_TRAMP_CAP);
		_neverc_krt_quiesce();
		if (_neverc_krt_modfree) _neverc_krt_modfree(h->stub);
		h->stub = (void *)0;
		h->tramp_code = (void *)0;
		h->base.trampoline = (void *)0;
	}
	h->base.active = 0;
	__atomic_fetch_add(&_neverc_krt_hook_remove_cnt, 1, __ATOMIC_RELAXED);
}

int neverc_krt_hook_replace_ctx(struct neverc_krt_hook_ctx *h,
				neverc_krt_ctx_handler_t new_handler)
{
	if (!h->base.active || !h->stub) return -1;
	WRITE_ONCE(h->base.enabled, 0);
	_neverc_krt_full_barrier();
	_neverc_krt_quiesce_deep();
	h->base.replace = (void *)new_handler;
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
	WRITE_ONCE(h->base.enabled, 1);
	return 0;
}

int neverc_krt_hook_install_ctx_batch(struct neverc_krt_hook_ctx_batch *batch,
				      int count)
{
	int i, ok = 0;
	for (i = 0; i < count; i++) {
		batch[i].result = neverc_krt_hook_install_ctx(
			batch[i].hook, batch[i].target,
			batch[i].handler, batch[i].call_orig);
		if (batch[i].result == NEVERC_KRT_HOOK_OK) ok++;
	}
	if (ok > 0 && ok < count) {
		for (i = 0; i < count; i++) {
			if (batch[i].result == NEVERC_KRT_HOOK_OK)
				neverc_krt_hook_remove_ctx(batch[i].hook);
		}
		return -1;
	}
	return ok == count ? 0 : -1;
}

int neverc_krt_hook_install_batch(struct neverc_krt_hook_batch *batch, int count)
{
	int i, ok = 0;
	for (i = 0; i < count; i++) {
		batch[i].result = neverc_krt_hook_install(
			batch[i].hook, batch[i].target,
			batch[i].replace, batch[i].orig);
		if (batch[i].result == NEVERC_KRT_HOOK_OK) ok++;
	}
	if (ok > 0 && ok < count) {
		for (i = 0; i < count; i++) {
			if (batch[i].result == NEVERC_KRT_HOOK_OK)
				neverc_krt_hook_remove(batch[i].hook);
		}
		return -1;
	}
	return ok == count ? 0 : -1;
}

void neverc_krt_hook_cleanup(void)
{
	int i;
	unsigned long flags;

	_neverc_krt_full_barrier();

	if (_neverc_krt_syncrcu) _neverc_krt_syncrcu();
	_neverc_krt_full_barrier();
	if (_neverc_krt_syncrcu) _neverc_krt_syncrcu();

	if (_neverc_krt_msleep) _neverc_krt_msleep(100);

	flags = _neverc_krt_spin_lock_irqsave(&_neverc_krt_pool_lock);
	for (i = 0; i < _neverc_krt_pool_count; i++) {
		if (_neverc_krt_pool[i].base && _neverc_krt_modfree &&
		    _neverc_krt_pool[i].magic == _NEVERC_KRT_POOL_MAGIC)
			_neverc_krt_modfree(_neverc_krt_pool[i].base);
		_neverc_krt_pool[i].base = (void *)0;
		_neverc_krt_pool[i].used = 0;
		_neverc_krt_pool[i].refcnt = 0;
		_neverc_krt_pool[i].magic = 0;
	}
	_neverc_krt_pool_count = 0;
	_neverc_krt_pool_alloc_total = 0;
	_neverc_krt_pool_alloc_bytes = 0;
	_neverc_krt_spin_unlock_irqrestore(&_neverc_krt_pool_lock, flags);
	_neverc_krt_inited = 0;
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
				(u64)(unsigned long)new_func);
	thunk->code[n++] = NEVERC_KRT_A64_RET_X17;
	return 0;
}

int neverc_krt_fptr_replace(struct neverc_krt_fptr_hook *h,
			    void *struct_addr, unsigned long field_off,
			    void *new_fn)
{
	void **slot;
	void *orig;

	if (!h || !struct_addr) return -1;
	if (!_neverc_krt_inited) return NEVERC_KRT_HOOK_E_NOINIT;

	slot = (void **)((unsigned long)struct_addr + field_off);
	orig = *slot;
	if (!orig) return -2;

	h->struct_addr = struct_addr;
	h->field_off = field_off;
	h->orig_fn = orig;
	h->active = 0;

	if (neverc_krt_cfi_has_tag(orig)) {
		neverc_krt_cfi_make_thunk(&h->thunk, orig, new_fn);
		h->thunk_page = _neverc_krt_pool_alloc(sizeof(h->thunk));
		if (!h->thunk_page) return NEVERC_KRT_HOOK_E_ALLOC;
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
		void *entry = (void *)((unsigned long)h->thunk_page + 4);
		neverc_krt_mem_write_protected((unsigned long)slot, &entry,
					sizeof(entry));
	} else {
		h->thunk_page = (void *)0;
		neverc_krt_mem_write_protected((unsigned long)slot, &new_fn,
					sizeof(new_fn));
	}

	h->active = 1;
	return 0;
}

void neverc_krt_fptr_restore(struct neverc_krt_fptr_hook *h)
{
	void **slot;
	if (!h || !h->active) return;

	slot = (void **)((unsigned long)h->struct_addr + h->field_off);
	neverc_krt_mem_write_protected((unsigned long)slot, &h->orig_fn,
				sizeof(h->orig_fn));

	if (h->thunk_page)
		_neverc_krt_pool_free(h->thunk_page);
	h->thunk_page = (void *)0;
	h->active = 0;
}

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

static void _neverc_krt_ftrace_thunk(unsigned long ip, unsigned long parent_ip,
				     void *ops, void *regs)
{
	if (!ops || !regs) return;
	struct neverc_krt_ftrace_hook *h = (struct neverc_krt_ftrace_hook *)(
		(char *)ops - __builtin_offsetof(struct neverc_krt_ftrace_hook, ops));
	if (!h->replace) return;

	unsigned long *r = (unsigned long *)regs;
	r[32] = (unsigned long)h->replace;
}

int neverc_krt_ftrace_hook_install(struct neverc_krt_ftrace_hook *h,
				   void *target, void *replace,
				   void **orig)
{
	int ret;

	if (!_neverc_krt_ftrace_avail) return -1;
	if (!target || !replace) return -2;

	h->target = target;
	h->replace = replace;
	h->orig = target;
	h->active = 0;

	unsigned char *p = (unsigned char *)&h->ops;
	unsigned long i;
	for (i = 0; i < sizeof(h->ops); i++) p[i] = 0;

	/*
	 * Kernel's struct ftrace_ops layout:
	 *   [0]  func   (ftrace_func_t)
	 *   [8]  next   (kernel-managed, leave zero)
	 *   [16] flags  (unsigned long)
	 */
	h->ops._storage[0] = (unsigned long)_neverc_krt_ftrace_thunk;
	h->ops._storage[2] = NEVERC_KRT_FTRACE_FL_SAVE_REGS
			    | NEVERC_KRT_FTRACE_FL_IPMODIFY
			    | NEVERC_KRT_FTRACE_FL_RECURSION;

	ret = _neverc_krt_ftrace_set_filter(&h->ops,
				     (unsigned long)target, 0, 1);
	if (ret) return ret;

	ret = _neverc_krt_register_ftrace(&h->ops);
	if (ret) return ret;

	if (orig) *orig = h->orig;
	h->active = 1;
	return 0;
}

void neverc_krt_ftrace_hook_remove(struct neverc_krt_ftrace_hook *h)
{
	if (!h || !h->active) return;
	if (_neverc_krt_unregister_ftrace)
		_neverc_krt_unregister_ftrace(&h->ops);
	h->active = 0;
}

int neverc_krt_hook_auto(struct neverc_krt_hook *h, void *target,
			 void *replace, void **orig,
			 struct neverc_krt_ftrace_hook *ft_fallback)
{
	enum neverc_krt_scan_result scan = neverc_krt_hook_scan(target);
	int ret;

	if (scan == NEVERC_KRT_SCAN_OK) {
		ret = neverc_krt_hook_install(h, target, replace, orig);
		if (ret == NEVERC_KRT_HOOK_OK) return ret;
	}

	if (ft_fallback && _neverc_krt_ftrace_avail) {
		ret = neverc_krt_ftrace_hook_install(ft_fallback,
					      target, replace, orig);
		if (ret == 0) return ret;
	}

	if (scan == NEVERC_KRT_SCAN_OK)
		return neverc_krt_hook_install(h, target, replace, orig);

	return NEVERC_KRT_HOOK_E_RELOC;
}

int neverc_krt_kprobe_hook_init(void)
{
	if (_neverc_krt_reg_kprobe) return 0;
	_neverc_krt_reg_kprobe =
		(neverc_krt_register_kprobe_fn)NEVERC_KRT_LOOKUP("register_kprobe");
	_neverc_krt_unreg_kprobe =
		(neverc_krt_unregister_kprobe_fn)NEVERC_KRT_LOOKUP("unregister_kprobe");
	return (_neverc_krt_reg_kprobe && _neverc_krt_unreg_kprobe) ? 0 : -1;
}

void neverc_krt_hook_auto_remove(struct neverc_krt_hook *h,
				 struct neverc_krt_ftrace_hook *ft_fallback)
{
	if (h && h->active)
		neverc_krt_hook_remove(h);
	if (ft_fallback && ft_fallback->active)
		neverc_krt_ftrace_hook_remove(ft_fallback);
}

int neverc_krt_pool_usage(int *total_used, int *total_cap)
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

long _neverc_krt_chain_run(struct neverc_krt_hook_chain *chain,
			   void *a0, void *a1, void *a2,
			   void *a3, void *a4, void *a5)
{
	int i, cnt;
	long ret = 0;
	struct neverc_krt_hook_chain_entry snap[NEVERC_KRT_CHAIN_MAX];
	if (!chain) return 0;
	__asm__ __volatile__("dmb ish" ::: "memory");
	cnt = READ_ONCE(chain->count);
	if (cnt > NEVERC_KRT_CHAIN_MAX) cnt = NEVERC_KRT_CHAIN_MAX;
	for (i = 0; i < cnt; i++) {
		snap[i].handler  = READ_ONCE(chain->entries[i].handler);
		snap[i].active   = READ_ONCE(chain->entries[i].active);
	}
	__asm__ __volatile__("dmb ish" ::: "memory");
	for (i = 0; i < cnt; i++) {
		if (!snap[i].active || !snap[i].handler) continue;
		neverc_krt_chain_handler_t h = (neverc_krt_chain_handler_t)snap[i].handler;
		ret = h(chain->orig_fn, a0, a1, a2, a3, a4, a5);
		if (ret != 0) return ret;
	}
	return ret;
}

int neverc_krt_chain_init(struct neverc_krt_hook_chain *chain)
{
	if (!chain) return -1;
	unsigned char *p = (unsigned char *)chain;
	unsigned long sz = sizeof(*chain);
	unsigned long i;
	for (i = 0; i < sz; i++) p[i] = 0;
	return 0;
}

int neverc_krt_chain_add(struct neverc_krt_hook_chain *chain,
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

void neverc_krt_hook_get_stats(struct neverc_krt_hook_stats *out)
{
	if (!out) return;
	out->total_installs = __atomic_load_n(&_neverc_krt_hook_install_cnt,
					      __ATOMIC_RELAXED);
	out->total_removes  = __atomic_load_n(&_neverc_krt_hook_remove_cnt,
					      __ATOMIC_RELAXED);
	out->pool_allocs = neverc_krt_pool_alloc_count();
	out->pool_alloc_fails = __atomic_load_n(&_neverc_krt_pool_alloc_fail,
						__ATOMIC_RELAXED);
	out->pool_pages = neverc_krt_pool_usage(&out->pool_used_bytes,
					  &out->pool_total_bytes);
	out->active_hooks = (int)(out->total_installs - out->total_removes);
}

int neverc_krt_chain_remove(struct neverc_krt_hook_chain *chain, void *handler)
{
	int i;
	if (!chain || !handler) return -1;

	for (i = 0; i < chain->count; i++) {
		if (chain->entries[i].handler == handler) {
			WRITE_ONCE(chain->entries[i].active, 0);
			__asm__ __volatile__("dmb ish" ::: "memory");
			for (; i < chain->count - 1; i++)
				chain->entries[i] = chain->entries[i + 1];
			__asm__ __volatile__("dmb ish" ::: "memory");
			WRITE_ONCE(chain->count, chain->count - 1);
			return 0;
		}
	}
	return -2;
}

int neverc_krt_chain_install(struct neverc_krt_hook_chain *chain, void *target,
			     void *dispatch_fn)
{
	if (!chain || !target || !dispatch_fn) return -1;
	if (chain->hook.active) return -2;
	if (chain->count == 0) return -3;

	chain->dispatch_fn = dispatch_fn;
	return neverc_krt_hook_install(&chain->hook, target,
				dispatch_fn, &chain->orig_fn);
}

void neverc_krt_chain_uninstall(struct neverc_krt_hook_chain *chain)
{
	if (!chain) return;
	if (chain->hook.active)
		neverc_krt_hook_remove(&chain->hook);
}

int neverc_krt_hook_strerror(int err, char *buf, int sz)
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

int neverc_krt_in_irq_context(void)
{
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

unsigned long neverc_krt_strip_pac(unsigned long addr)
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

static int neverc_krt_a64_is_kcfi_tag(u32 *addr)
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

static int neverc_krt_a64_is_ftrace_site(u32 *code)
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

u32 neverc_krt_cfi_read_tag(void *func)
{
	u32 tag = 0;
	unsigned long addr = neverc_krt_strip_pac((unsigned long)func);
	neverc_krt_mem_read(&tag, (void *)(addr - 4), 4);
	return tag;
}

int neverc_krt_cfi_has_tag(void *func)
{
	u32 tag = neverc_krt_cfi_read_tag(func);
	return tag != 0 && tag != NEVERC_KRT_A64_NOP && tag != NEVERC_KRT_A64_BTI_C;
}

u64 neverc_krt_pool_alloc_count(void)
{
	return __atomic_load_n(&_neverc_krt_pool_alloc_total, __ATOMIC_RELAXED);
}

u64 neverc_krt_pool_alloc_bytes(void)
{
	return __atomic_load_n(&_neverc_krt_pool_alloc_bytes, __ATOMIC_RELAXED);
}

int neverc_krt_pool_page_count(void)
{
	return _neverc_krt_pool_count;
}

