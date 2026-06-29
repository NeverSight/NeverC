/* SPDX-License-Identifier: GPL-2.0 */
/*
 * nvk_hook_internal.h — Internal hook engine types and low-level API.
 *
 * This file is ONLY for the runtime implementation (nvk_hook.c).
 * User modules should include <nvk_hook.h> for the public API.
 */
#ifndef NEVERC_KRT_HOOK_INTERNAL_H
#define NEVERC_KRT_HOOK_INTERNAL_H

#include <nvk_hook.h>
#include <nvk_mem.h>

/* ---- ARM64 instruction constants ---- */

#define NEVERC_KRT_A64_NOP       0xD503201FU
#define NEVERC_KRT_A64_BTI_C     0xD503245FU
#define NEVERC_KRT_A64_BTI_JC    0xD50324DFU
#define NEVERC_KRT_A64_PACIASP   0xD503233FU
#define NEVERC_KRT_A64_PACIBSP   0xD503237FU

#define NEVERC_KRT_A64_RET_X16   0xD65F0200U
#define NEVERC_KRT_A64_RET_X17   0xD65F0220U

/* ---- Low-level hook structure ---- */

#define NEVERC_KRT_HOOK_MAX_PATCH   6
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

__always_inline u64 neverc_krt_hook_hits(struct neverc_krt_hook *h)
{ return __atomic_load_n(&h->hit_count, __ATOMIC_RELAXED); }

__always_inline void neverc_krt_hook_reset_stats(struct neverc_krt_hook *h)
{ __atomic_store_n(&h->hit_count, 0, __ATOMIC_RELAXED); }

int neverc_krt_in_irq_context(void);

__always_inline int neverc_krt_irq_disabled(void)
{
	unsigned long daif;
	__asm__ __volatile__("mrs %0, daif" : "=r"(daif));
	return (daif & (1UL << 7)) != 0;
}

__always_inline int neverc_krt_hook_enter(struct neverc_krt_hook *h)
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

__always_inline void neverc_krt_hook_leave(struct neverc_krt_hook *h)
{
	__atomic_store_n(&h->guard, 0, __ATOMIC_RELEASE);
}

__always_inline int neverc_krt_hook_enter_safe(struct neverc_krt_hook *h)
{
	if (unlikely(!READ_ONCE(h->enabled)))
		return 0;
	return neverc_krt_hook_enter(h);
}

/* ---- Scan / diagnostic ---- */

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
int neverc_krt_hook_strerror(int err, char *buf, int sz);
enum neverc_krt_scan_result neverc_krt_hook_scan(void *target);

/* ---- Low-level install/remove ---- */

int neverc_krt_hook_install(struct neverc_krt_hook *h, void *target,
			    void *replace, void **orig);

typedef void (*neverc_krt_ctx_fp_handler_t)(neverc_krt_reg_ctx *ctx, neverc_krt_fp_state *fp);

#define NEVERC_KRT_CTX_HANDLER_FP(wrapper_name, user_fn)                \
	static void wrapper_name(neverc_krt_reg_ctx *ctx) {             \
		neverc_krt_fp_state __fp_state;                          \
		NEVERC_KRT_SAVE_FP(&__fp_state);                         \
		user_fn(ctx, &__fp_state);                               \
		NEVERC_KRT_RESTORE_FP(&__fp_state);                      \
	}

/*
 * Cast a neverc_krt_hook_ctx's trampoline to a callable original-function pointer.
 */
#define NEVERC_KRT_CTX_ORIG_FN(h, fn_type) ((fn_type)(h)->tramp_code)

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

__always_inline int neverc_krt_hook_is_enabled(struct neverc_krt_hook *h)
{ return READ_ONCE(h->enabled); }

__always_inline void neverc_krt_hook_enable(struct neverc_krt_hook *h)
{ WRITE_ONCE(h->enabled, 1); }

__always_inline void neverc_krt_hook_disable(struct neverc_krt_hook *h)
{ WRITE_ONCE(h->enabled, 0); }

/* ---- Batch operations ---- */

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

/* ---- kCFI-safe function pointer replacement ---- */

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

/* ---- ftrace-based hook (fallback) ---- */

struct neverc_krt_ftrace_ops {
	unsigned long _storage[32];
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
				   void *target, void *replace, void **orig);
void neverc_krt_ftrace_hook_remove(struct neverc_krt_ftrace_hook *h);

int neverc_krt_hook_auto(struct neverc_krt_hook *h, void *target,
			 void *replace, void **orig,
			 struct neverc_krt_ftrace_hook *ft_fallback);
void neverc_krt_hook_auto_remove(struct neverc_krt_hook *h,
				 struct neverc_krt_ftrace_hook *ft_fallback);

/* ---- kprobe-based hook (fallback) ---- */

struct neverc_krt_kprobe_hook {
	unsigned char kp_storage[160];
	void *target;
	void *replace;
	void *orig;
	int   active;
};

int neverc_krt_kprobe_hook_init(void);

/* ---- Pool stats ---- */

u64 neverc_krt_pool_alloc_count(void);
u64 neverc_krt_pool_alloc_bytes(void);
int neverc_krt_pool_page_count(void);
int neverc_krt_pool_usage(int *total_used, int *total_cap);

/* ---- Hook chain ---- */

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

#define NEVERC_KRT_CHAIN_DISPATCH(name, chain_ptr)                       \
	static long name(void *a0, void *a1, void *a2,                   \
			 void *a3, void *a4, void *a5)                   \
	{ return _neverc_krt_chain_run(&(chain_ptr), a0, a1, a2, a3, a4, a5); }

int neverc_krt_chain_init(struct neverc_krt_hook_chain *chain);
int neverc_krt_chain_add(struct neverc_krt_hook_chain *chain,
			 void *handler, int priority);
int neverc_krt_chain_remove(struct neverc_krt_hook_chain *chain, void *handler);
int neverc_krt_chain_install(struct neverc_krt_hook_chain *chain, void *target,
			     void *dispatch_fn);
void neverc_krt_chain_uninstall(struct neverc_krt_hook_chain *chain);

/* ---- Hook stats ---- */

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

/* ---- Registry internals ---- */

#define NEVERC_KRT_REGISTRY_MAX 16
#define NEVERC_KRT_PROBE_MAX 16

#endif /* NEVERC_KRT_HOOK_INTERNAL_H */
