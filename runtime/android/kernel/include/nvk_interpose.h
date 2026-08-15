/* SPDX-License-Identifier: GPL-2.0 */
/*
 * nvk_interpose.h — NeverC kernel interpose public API.
 *
 * Two entry points:
 *   neverc_krt_interpose_register  — interpose a function at its entry point
 *   neverc_krt_probe_register — interpose any instruction at an arbitrary address
 *
 * Both support:
 *   - Multiple handlers on the same target (auto-chained)
 *   - Coexistence with existing interposes (ftrace, kprobe, other inline interposes)
 *   - Priority-based ordering (lower value = runs first)
 */
#ifndef NEVERC_KRT_INTERPOSE_H
#define NEVERC_KRT_INTERPOSE_H

#include <linux/types.h>

/* ====================================================================
 * Register context (used by probe handlers)
 * ==================================================================== */

typedef struct {
	u64 regs[31];       /* X0 - X30                          */
	u64 fpcr;           /* saved FPCR                        */
	u64 nzcv;           /* saved NZCV flags                  */
	u64 force_jump;     /* if nonzero, redirect via RET X17  */
	u64 fpsr;           /* saved FPSR                        */
	/* Internal force mode, reset by the generated stub on every entry. */
	u64 _pad;
} neverc_krt_reg_ctx;

/* PAC tag strip — declared early: FORCE_JUMP/SKIP macros need it. */
unsigned long neverc_krt_strip_pac(unsigned long addr);

/* Register access macros */
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

#define NEVERC_KRT_CTX_ARG(ctx, n)         ((ctx)->regs[n])
#define NEVERC_KRT_CTX_SYSCALL_NR(ctx)     NEVERC_KRT_CTX_X8(ctx)

/*
 * Redirect after the handler returns via RET X17 (BTI return type; see stub).
 * Addresses must be PAC-stripped and must outlive removal of the interpose
 * owner (normally the saved kernel LR or retained original trampoline).  Use
 * NEVERC_KRT_CTX_REDIRECT for owner-module replacement code.
 */
#define NEVERC_KRT_CTX_FORCE_JUMP(ctx, addr) \
	do { \
		(ctx)->force_jump = (u64)neverc_krt_strip_pac( \
			(unsigned long)(addr)); \
		(ctx)->_pad = 0; \
	} while (0)

/*
 * Skip the original function entirely; return ret_val to the caller.
 */
#define NEVERC_KRT_CTX_SKIP(ctx, ret_val) do {           \
	(ctx)->regs[0] = (u64)(unsigned long)(ret_val);  \
	(ctx)->force_jump = (u64)neverc_krt_strip_pac(   \
		(unsigned long)(ctx)->regs[30]);         \
	(ctx)->_pad = 0;                                   \
} while (0)

/* Skip the original function (void return). */
#define NEVERC_KRT_CTX_SKIP_VOID(ctx) \
	do { \
		(ctx)->force_jump = (u64)neverc_krt_strip_pac( \
			(unsigned long)(ctx)->regs[30]); \
		(ctx)->_pad = 0; \
	} while (0)

/*
 * Redirect a function-entry interpose to a replacement and retain the lease
 * until that function returns.  This CALL mode assumes the captured LR is the
 * unsigned caller LR at a function entry; arbitrary-instruction probes can
 * hold a PAC-signed in-function LR and must use a direct kernel continuation
 * instead.  FORCE_JUMP/SKIP remain direct transfers for such continuations.
 */
#define NEVERC_KRT_CTX_REDIRECT(ctx, fn_addr) \
	do { \
		NEVERC_KRT_CTX_FORCE_JUMP((ctx), (fn_addr)); \
		(ctx)->_pad = 1; \
	} while (0)

/* Set argument N (X0..X7 for AAPCS64). */
#define NEVERC_KRT_CTX_SET_ARG(ctx, n, val) \
	do { (ctx)->regs[n] = (u64)(unsigned long)(val); } while (0)

/* ====================================================================
 * FP/SIMD state save/restore (for handlers that touch NEON/FP regs)
 * ==================================================================== */

typedef struct {
	u64 lo, hi;
} neverc_krt_fp128;

typedef struct {
	neverc_krt_fp128 q[32];
} neverc_krt_fp_state;

#define NEVERC_KRT_SAVE_FP(st)                                       \
	__asm__ __volatile__(                                        \
	    "stp q0,  q1,  [%0, #0]   \n"                           \
	    "stp q2,  q3,  [%0, #32]  \n"                           \
	    "stp q4,  q5,  [%0, #64]  \n"                           \
	    "stp q6,  q7,  [%0, #96]  \n"                           \
	    "stp q8,  q9,  [%0, #128] \n"                           \
	    "stp q10, q11, [%0, #160] \n"                           \
	    "stp q12, q13, [%0, #192] \n"                           \
	    "stp q14, q15, [%0, #224] \n"                           \
	    "stp q16, q17, [%0, #256] \n"                           \
	    "stp q18, q19, [%0, #288] \n"                           \
	    "stp q20, q21, [%0, #320] \n"                           \
	    "stp q22, q23, [%0, #352] \n"                           \
	    "stp q24, q25, [%0, #384] \n"                           \
	    "stp q26, q27, [%0, #416] \n"                           \
	    "stp q28, q29, [%0, #448] \n"                           \
	    "stp q30, q31, [%0, #480] \n"                           \
	    : : "r"(st) : "memory")

#define NEVERC_KRT_RESTORE_FP(st)                                    \
	__asm__ __volatile__(                                        \
	    "ldp q0,  q1,  [%0, #0]   \n"                           \
	    "ldp q2,  q3,  [%0, #32]  \n"                           \
	    "ldp q4,  q5,  [%0, #64]  \n"                           \
	    "ldp q6,  q7,  [%0, #96]  \n"                           \
	    "ldp q8,  q9,  [%0, #128] \n"                           \
	    "ldp q10, q11, [%0, #160] \n"                           \
	    "ldp q12, q13, [%0, #192] \n"                           \
	    "ldp q14, q15, [%0, #224] \n"                           \
	    "ldp q16, q17, [%0, #256] \n"                           \
	    "ldp q18, q19, [%0, #288] \n"                           \
	    "ldp q20, q21, [%0, #320] \n"                           \
	    "ldp q22, q23, [%0, #352] \n"                           \
	    "ldp q24, q25, [%0, #384] \n"                           \
	    "ldp q26, q27, [%0, #416] \n"                           \
	    "ldp q28, q29, [%0, #448] \n"                           \
	    "ldp q30, q31, [%0, #480] \n"                           \
	    : : "r"(st) : "memory")

/*
 * FP guard pair — use inside a ctx/probe handler when it might touch
 * SIMD/FP registers (e.g. calling printk or format functions).
 *
 *   static void my_handler(neverc_krt_reg_ctx *ctx) {
 *       NEVERC_KRT_CTX_FP_GUARD_BEGIN;
 *       // ... safe to call FP-using functions here ...
 *       NEVERC_KRT_CTX_FP_GUARD_END;
 *   }
 */
#define NEVERC_KRT_CTX_FP_GUARD_BEGIN \
	neverc_krt_fp_state _neverc_krt_fps_guard; \
	NEVERC_KRT_SAVE_FP(&_neverc_krt_fps_guard)

#define NEVERC_KRT_CTX_FP_GUARD_END \
	NEVERC_KRT_RESTORE_FP(&_neverc_krt_fps_guard)

/* ====================================================================
 * Handler typedefs
 * ==================================================================== */

/* Probe handler: gets full register context at the interposed instruction. */
typedef void (*neverc_krt_ctx_handler_t)(neverc_krt_reg_ctx *ctx);

/*
 * Function-entry interpose handler:
 *   long fn(void *orig, void *a0, void *a1, void *a2,
 *           void *a3, void *a4, void *a5)
 *
 *   - Call orig(a0..a5) to invoke the original function.
 *   - Return value propagates to the caller.
 */
typedef long (*neverc_krt_interpose_handler_t)(void *orig, void *a0, void *a1,
					  void *a2, void *a3, void *a4,
					  void *a5);

/* ====================================================================
 * Error codes
 * ==================================================================== */

enum neverc_krt_interpose_err {
	NEVERC_KRT_INTERPOSE_OK         =  0,
	NEVERC_KRT_INTERPOSE_E_NOINIT   = -1,
	NEVERC_KRT_INTERPOSE_E_SHORT    = -2,
	NEVERC_KRT_INTERPOSE_E_RELOC    = -3,
	NEVERC_KRT_INTERPOSE_E_ALLOC    = -4,
	NEVERC_KRT_INTERPOSE_E_PATCH    = -5,
	NEVERC_KRT_INTERPOSE_E_CONFLICT = -6,
	/* A C-callable original trampoline could not inherit the target's KCFI
	 * type prefix.  The entry is left untouched. */
	NEVERC_KRT_INTERPOSE_E_KCFI     = -7,
	/* This item installed successfully, but another item in the same batch
	 * failed and the successful installation was rolled back. */
	NEVERC_KRT_INTERPOSE_E_ROLLBACK = -8,
};

/* ====================================================================
 * Low-level interpose structure
 *
 * Users who need fine-grained control (manual install/remove without
 * auto-chaining) use the advanced SDK API in <nvk_interpose_advanced.h>.
 * The high-level API (interpose_register) manages these internally.
 * ==================================================================== */

#define NEVERC_KRT_INTERPOSE_MAX_PATCH   6

struct neverc_krt_interpose {
	void       *target;
	void       *replace;
	u32        *trampoline;
	u32         orig_insns[NEVERC_KRT_INTERPOSE_MAX_PATCH];
	int         patch_count;
	int         active;
	volatile int enabled;
	int         short_b;
	volatile u64 hit_count;
	volatile unsigned long guard;
};

/* ====================================================================
 * Initialization
 * ==================================================================== */

/*
 * Initialize the interpose engine. Must be called once before any
 * interpose_register / probe_register calls.
 */
int neverc_krt_interpose_init(void);

/*
 * Release all resources.  Call this before committing to module unload.
 * E_PATCH means at least one target entry could not be restored; the engine
 * remains initialized and the caller must keep the module loaded and retry.
 */
int neverc_krt_interpose_cleanup(void);

/* ====================================================================
 * API 1: Function-entry interpose (neverc_krt_interpose_register)
 *
 * Interposes a function at its prologue. Multiple handlers on the same
 * target are automatically chained by priority (lower = runs first).
 * Coexists with ftrace/kprobe interposes already on the target.
 *
 * Handler signature:
 *   long my_interpose(void *orig, void *a0, ..., void *a5)
 *   - Call ((orig_fn_type)orig)(a0, ...) to invoke the original.
 *   - Return value propagates to the caller.
 *
 * Priority:
 *   Lower values execute first.
 *   Use negative priorities to run before other interposes (e.g. -100).
 *   Use positive priorities to run after other interposes (e.g. 100).
 *   Default: 0.
 * ==================================================================== */

struct neverc_krt_interpose_ref {
	int   slot;
	void *handler;
};

/*
 * Register a function-entry interpose.
 *
 * @target   Kernel function to interpose (use NEVERC_KRT_LOOKUP).
 * @handler  Interpose handler function.
 * @priority Execution order (lower = earlier). Use negative to run first.
 * @orig     Receives callable pointer to the original function.
 * @ref      Filled with opaque handle for unregister.
 *
 * Returns 0 on success, negative error code on failure.
 * Exceptional E_PATCH contract: if @ref.slot is non-negative, entry
 * publication and restore both failed.  The retained slot is disabled and
 * accepts only unregister/cleanup; the caller must keep @handler/@ref alive
 * and retry unregister until it succeeds.
 */
int neverc_krt_interpose_register(void *target, void *handler, int priority,
			     void **orig, struct neverc_krt_interpose_ref *ref);

/*
 * Remove a previously registered interpose.
 * If it was the last handler on that target, the interpose is fully removed.
 * A faulted multi-handler chain is recovered only by interpose cleanup;
 * unregister returns E_PATCH rather than re-enabling a partially restored
 * entry.
 */
int neverc_krt_interpose_unregister(struct neverc_krt_interpose_ref *ref);

/*
 * Query how many handlers are registered on a given target.
 */
int neverc_krt_interpose_registry_count(void *target);

/* ====================================================================
 * API 2: Arbitrary-instruction probe (neverc_krt_probe_register)
 *
 * Interposes any instruction at any address — does NOT need to be a function
 * entry. Multiple handlers on the same address are automatically chained
 * by priority.
 *
 * Handler signature:
 *   void my_probe(neverc_krt_reg_ctx *ctx)
 *   - Read/write registers via ctx->regs[N].
 *   - Set ctx->force_jump to redirect execution.
 *   - Use NEVERC_KRT_CTX_SKIP(ctx, val) to skip the function.
 *
 * Priority:
 *   Same semantics as interpose_register — lower = runs first.
 *
 * Limitations:
 *   - X17 is clobbered (trampoline back-jump).
 *   - X16 additionally clobbered if target > +/-128MB from stub.
 *   - Patched instruction must not be exclusive/svc and must be relocatable.
 * ==================================================================== */

struct neverc_krt_probe_ref {
	int   slot;
	void *handler;
};

/*
 * Register a probe at an arbitrary instruction address.
 *
 * @addr     Address of the instruction to interpose.
 * @handler  Probe handler function.
 * @priority Execution order (lower = earlier).
 * @ref      Filled with opaque handle for unregister.
 *
 * Returns 0 on success, negative error code on failure.
 * On E_PATCH, a non-negative @ref.slot denotes the same retained-disabled
 * recovery ownership contract as neverc_krt_interpose_register().
 */
int neverc_krt_probe_register(void *addr,
			      neverc_krt_ctx_handler_t handler,
			      int priority,
			      struct neverc_krt_probe_ref *ref);

/*
 * Remove a previously registered probe.
 * If it was the last handler at that address, the patch is fully restored.
 * Faulted multi-handler probe chains follow the same cleanup-only recovery
 * rule as function-entry interposes.
 */
int neverc_krt_probe_unregister(struct neverc_krt_probe_ref *ref);

/*
 * Query how many handlers are registered at a given address.
 */
int neverc_krt_probe_count(void *addr);

/* ====================================================================
 * Re-entry guard
 *
 * Prevents recursive interpose invocations on the same task.
 * Usage:
 *   if (!neverc_krt_interpose_enter(&interpose)) return orig(...);
 *   // ... interpose logic ...
 *   neverc_krt_interpose_leave(&interpose);
 * ==================================================================== */

#define NEVERC_KRT_INTERPOSE_COUNT(h) \
	__atomic_fetch_add(&(h)->hit_count, 1, __ATOMIC_RELAXED)

int neverc_krt_interpose_enter(struct neverc_krt_interpose *h);
void neverc_krt_interpose_leave(struct neverc_krt_interpose *h);
int neverc_krt_interpose_enter_safe(struct neverc_krt_interpose *h);
u64 neverc_krt_interpose_hits(struct neverc_krt_interpose *h);
void neverc_krt_interpose_reset_stats(struct neverc_krt_interpose *h);
int neverc_krt_interpose_is_enabled(struct neverc_krt_interpose *h);
void neverc_krt_interpose_enable(struct neverc_krt_interpose *h);
void neverc_krt_interpose_disable(struct neverc_krt_interpose *h);

#endif /* NEVERC_KRT_INTERPOSE_H */
