/* SPDX-License-Identifier: GPL-2.0 */
/*
 * nvk_interpose_advanced.h — Advanced interpose API for module authors who
 * need direct install/remove control, context interposes, batch ops, kCFI
 * thunks, fptr replacement, and ftrace/kprobe fallbacks.
 *
 * For the high-level register/unregister API, see <nvk_interpose.h>.
 * Include this header explicitly when you need the low-level controls below.
 */
#ifndef NEVERC_KRT_INTERPOSE_ADVANCED_H
#define NEVERC_KRT_INTERPOSE_ADVANCED_H

#include <nvk_interpose.h>
#include <nvk_mem.h>

/* ---- Scan / diagnostic ---- */

enum neverc_krt_scan_result {
	NEVERC_KRT_SCAN_OK              =  0,
	NEVERC_KRT_SCAN_TOO_SHORT       = -1,
	NEVERC_KRT_SCAN_HAZARDOUS       = -2,
	NEVERC_KRT_SCAN_UNRELOCATABLE   = -3,
	NEVERC_KRT_SCAN_ALREADY_INTERPOSEED  =  1,
	NEVERC_KRT_SCAN_FTRACE_ACTIVE   =  2,
	NEVERC_KRT_SCAN_KPROBE_ACTIVE   =  3,
};

int neverc_krt_scan_strerror(int r, char *buf, int sz);
int neverc_krt_interpose_strerror(int err, char *buf, int sz);
enum neverc_krt_scan_result neverc_krt_interpose_scan(void *target);

/* ---- Context interpose (probe-style with full register context) ---- */

typedef void (*neverc_krt_ctx_fp_handler_t)(neverc_krt_reg_ctx *ctx, neverc_krt_fp_state *fp);

#define NEVERC_KRT_CTX_HANDLER_FP(wrapper_name, user_fn)                \
	static void wrapper_name(neverc_krt_reg_ctx *ctx) {             \
		neverc_krt_fp_state __fp_state;                          \
		NEVERC_KRT_SAVE_FP(&__fp_state);                         \
		user_fn(ctx, &__fp_state);                               \
		NEVERC_KRT_RESTORE_FP(&__fp_state);                      \
	}

#define NEVERC_KRT_CTX_ORIG_FN(h, fn_type) ((fn_type)(h)->tramp_code)

struct neverc_krt_interpose_ctx {
	struct neverc_krt_interpose     base;
	u32                *stub;
	u32                *tramp_code;
	volatile unsigned long guard_task;
};

int neverc_krt_interpose_install(struct neverc_krt_interpose *h, void *target,
			    void *replace, void **orig);
void neverc_krt_interpose_remove(struct neverc_krt_interpose *h);
int neverc_krt_interpose_replace(struct neverc_krt_interpose *h, void *new_replace,
			    void **new_orig);

int neverc_krt_interpose_install_ctx(struct neverc_krt_interpose_ctx *h, void *target,
				neverc_krt_ctx_handler_t handler, void **call_orig);
void neverc_krt_interpose_remove_ctx(struct neverc_krt_interpose_ctx *h);
int neverc_krt_interpose_replace_ctx(struct neverc_krt_interpose_ctx *h,
				neverc_krt_ctx_handler_t new_handler);

/* ---- Batch operations ---- */

struct neverc_krt_interpose_batch {
	struct neverc_krt_interpose *interpose;
	void            *target;
	void            *replace;
	void           **orig;
	int              result;
};

struct neverc_krt_interpose_ctx_batch {
	struct neverc_krt_interpose_ctx *interpose;
	void               *target;
	neverc_krt_ctx_handler_t   handler;
	void              **call_orig;
	int                 result;
};

int neverc_krt_interpose_install_ctx_batch(struct neverc_krt_interpose_ctx_batch *batch,
				      int count);
int neverc_krt_interpose_install_batch(struct neverc_krt_interpose_batch *batch, int count);

/* ---- kCFI-safe function pointer replacement ---- */

u32 neverc_krt_cfi_read_tag(void *func);
int neverc_krt_cfi_has_tag(void *func);

struct neverc_krt_cfi_thunk {
	u32  tag;
	u32  code[8];
};

int neverc_krt_cfi_make_thunk(struct neverc_krt_cfi_thunk *thunk,
			      void *orig_func, void *new_func);

struct neverc_krt_fptr_interpose {
	void                *struct_addr;
	unsigned long        field_off;
	void                *orig_fn;
	struct neverc_krt_cfi_thunk thunk;
	u32                 *thunk_page;
	int                  active;
};

int neverc_krt_fptr_replace(struct neverc_krt_fptr_interpose *h,
			    void *struct_addr, unsigned long field_off,
			    void *new_fn);
void neverc_krt_fptr_restore(struct neverc_krt_fptr_interpose *h);

int neverc_krt_ftrace_init(void);

struct neverc_krt_ftrace_interpose {
	void                   *target;
	void                   *replace;
	void                   *orig;
	unsigned long           _ops_storage[32];
	int                     active;
};

int neverc_krt_ftrace_interpose_install(struct neverc_krt_ftrace_interpose *h,
				   void *target, void *replace, void **orig);
void neverc_krt_ftrace_interpose_remove(struct neverc_krt_ftrace_interpose *h);

int neverc_krt_interpose_auto(struct neverc_krt_interpose *h, void *target,
			 void *replace, void **orig,
			 struct neverc_krt_ftrace_interpose *ft_fallback);
void neverc_krt_interpose_auto_remove(struct neverc_krt_interpose *h,
				 struct neverc_krt_ftrace_interpose *ft_fallback);

#endif /* NEVERC_KRT_INTERPOSE_ADVANCED_H */
