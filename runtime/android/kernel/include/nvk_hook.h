/* SPDX-License-Identifier: GPL-2.0 */
/* neverc_krt_hook.h - NeverC arm64 inline-hook engine. */
#ifndef NEVERC_KRT_HOOK_H
#define NEVERC_KRT_HOOK_H

#include <linux/types.h>
#include <linux/compiler.h>
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

enum neverc_krt_pcrel neverc_krt_a64_classify(u32 i);


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

int neverc_krt_hook_strerror(int err, char *buf, int sz);

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

int neverc_krt_in_irq_context(void);

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


int neverc_krt_hook_init(void);


#define NEVERC_KRT_A64_BRK_KPROBE 0xD4200080U

unsigned long neverc_krt_strip_pac(unsigned long addr);

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

int neverc_krt_a64_is_kcfi_tag(u32 *addr);

#define NEVERC_KRT_A64_FTRACE_NOP  0xD503201FU
#define NEVERC_KRT_A64_BRK_FTRACE  0xD4200000U  /* BRK #0 — ftrace entry */

int neverc_krt_a64_is_ftrace_site(u32 *code);

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

enum neverc_krt_scan_result {
	NEVERC_KRT_SCAN_OK              =  0,
	NEVERC_KRT_SCAN_TOO_SHORT       = -1,
	NEVERC_KRT_SCAN_HAZARDOUS       = -2,
	NEVERC_KRT_SCAN_UNRELOCATABLE   = -3,
	NEVERC_KRT_SCAN_ALREADY_HOOKED  =  1,
	NEVERC_KRT_SCAN_FTRACE_ACTIVE   =  2,
	NEVERC_KRT_SCAN_KPROBE_ACTIVE   =  3,
};

int neverc_krt_scan_strerror(int r, char *buf, int sz);

enum neverc_krt_scan_result neverc_krt_hook_scan(void *target);



int neverc_krt_hook_install(struct neverc_krt_hook *h, void *target,
			    void *replace, void **orig);



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

int neverc_krt_hook_install_ctx(struct neverc_krt_hook_ctx *h, void *target,
				neverc_krt_ctx_handler_t handler, void **call_orig);



void neverc_krt_hook_pause(struct neverc_krt_hook *h);


void neverc_krt_hook_resume(struct neverc_krt_hook *h);


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

u32 neverc_krt_cfi_read_tag(void *func);

int neverc_krt_cfi_has_tag(void *func);

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

struct neverc_krt_ftrace_ops {
	unsigned long            func;
	unsigned long            flags;
	unsigned long            _pad[4];
};

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

int neverc_krt_ftrace_hook_install(struct neverc_krt_ftrace_hook *h,
				   void *target, void *replace,
				   void **orig);


void neverc_krt_ftrace_hook_remove(struct neverc_krt_ftrace_hook *h);


int neverc_krt_hook_auto(struct neverc_krt_hook *h, void *target,
			 void *replace, void **orig,
			 struct neverc_krt_ftrace_hook *ft_fallback);


/* --- kprobe-based hook (lightweight fallback) --- */

struct neverc_krt_kprobe_hook {
	void *kp_storage[8];
	void *target;
	void *replace;
	void *orig;
	int   active;
};

int neverc_krt_kprobe_hook_init(void);


void neverc_krt_hook_auto_remove(struct neverc_krt_hook *h,
				 struct neverc_krt_ftrace_hook *ft_fallback);



u64 neverc_krt_pool_alloc_count(void);

u64 neverc_krt_pool_alloc_bytes(void);

int neverc_krt_pool_page_count(void);

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
