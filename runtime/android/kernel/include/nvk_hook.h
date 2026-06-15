/* SPDX-License-Identifier: GPL-2.0 */
/* nvk_hook.h - NeverC arm64 inline-hook engine. */
#ifndef NVK_HOOK_H
#define NVK_HOOK_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <linux/string.h>
#include <nvk_mem.h>


typedef struct {
	u64 regs[31];       /* X0 - X30                          */
	u64 _pad;           /* align to 256                       */
	u64 nzcv;           /* saved NZCV flags                   */
	u64 force_jump;     /* if nonzero, redirect execution     */
} nvk_reg_ctx;

typedef struct {
	u64 lo, hi;
} nvk_fp128;

typedef struct {
	nvk_fp128 q[32];    /* Q0 - Q31 (128-bit each)            */
} nvk_fp_state;

#define NVK_SAVE_FP(st)                                                  \
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

#define NVK_RESTORE_FP(st)                                               \
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


#define NVK_A64_NOP       0xD503201FU
#define NVK_A64_BTI_C     0xD503245FU
#define NVK_A64_BTI_JC    0xD50324DFU
#define NVK_A64_PACIASP   0xD503233FU
#define NVK_A64_PACIBSP   0xD503237FU

#define NVK_A64_RET_X16   0xD65F0200U   /* RET X16 */
#define NVK_A64_RET_X17   0xD65F0220U   /* RET X17 */

static __always_inline int nvk_a64_is_bti(u32 i)
{ return i == 0xD503245FU || i == 0xD503249FU || i == 0xD50324DFU; }

static __always_inline int nvk_a64_is_pac(u32 i)
{
	return i == NVK_A64_PACIASP || i == NVK_A64_PACIBSP
	    || i == 0xD50323BFU  /* AUTIASP */
	    || i == 0xD50323FFU; /* AUTIBSP */
}


static __always_inline u32 nvk_a64_movz(int rd, u16 imm, int hw)
{ return 0xD2800000U | ((u32)hw << 21) | ((u32)imm << 5) | rd; }

static __always_inline u32 nvk_a64_movk(int rd, u16 imm, int hw)
{ return 0xF2800000U | ((u32)hw << 21) | ((u32)imm << 5) | rd; }

static int nvk_a64_gen_mov64(u32 *out, int rd, u64 addr)
{
	int n = 0;
	out[n++] = nvk_a64_movz(rd, (u16)(addr & 0xFFFF), 0);
	out[n++] = nvk_a64_movk(rd, (u16)((addr >> 16) & 0xFFFF), 1);
	out[n++] = nvk_a64_movk(rd, (u16)((addr >> 32) & 0xFFFF), 2);
	if (addr >> 48)
		out[n++] = nvk_a64_movk(rd, (u16)((addr >> 48) & 0xFFFF), 3);
	return n;
}

static __always_inline u32 nvk_a64_gen_b(long off)
{ return 0x14000000U | (((u32)(off >> 2)) & 0x03FFFFFFU); }

static __always_inline int nvk_a64_b_in_range(long off)
{ return off >= -0x8000000L && off < 0x8000000L; }

static __always_inline long nvk_sext(long v, int bits)
{ long m = 1L << (bits - 1); return (v ^ m) - m; }


enum nvk_pcrel {
	NVK_PC_NONE = 0,
	NVK_PC_ADRP, NVK_PC_ADR,
	NVK_PC_B, NVK_PC_BL,
	NVK_PC_BCOND, NVK_PC_CBZ, NVK_PC_TBZ,
	NVK_PC_LDR_LIT,
	NVK_PC_LDRSW_LIT,
	NVK_PC_PRFM_LIT,
};

static __always_inline enum nvk_pcrel nvk_a64_classify(u32 i)
{
	if ((i & 0x1F000000) == 0x10000000)
		return (i & 0x80000000) ? NVK_PC_ADRP : NVK_PC_ADR;
	if ((i & 0xFC000000) == 0x14000000) return NVK_PC_B;
	if ((i & 0xFC000000) == 0x94000000) return NVK_PC_BL;
	if ((i & 0xFF000010) == 0x54000000) return NVK_PC_BCOND;
	if ((i & 0x7E000000) == 0x34000000) return NVK_PC_CBZ;
	if ((i & 0x7E000000) == 0x36000000) return NVK_PC_TBZ;
	if ((i & 0xFF000000) == 0x98000000) return NVK_PC_LDRSW_LIT;
	if ((i & 0xFF000000) == 0xD8000000) return NVK_PC_PRFM_LIT;
	if ((i & 0x3B000000) == 0x18000000) return NVK_PC_LDR_LIT;
	return NVK_PC_NONE;
}


static int nvk_a64_relocate_abs(u32 insn, unsigned long old_pc, u32 *out)
{
	enum nvk_pcrel kind = nvk_a64_classify(insn);
	unsigned long target;
	int n = 0;

	switch (kind) {

	case NVK_PC_NONE:
		out[n++] = insn;
		return n;

	case NVK_PC_ADRP: {
		int immlo = (insn >> 29) & 3;
		long immhi = nvk_sext((insn >> 5) & 0x7FFFF, 19);
		target = (old_pc & ~0xFFFUL) + (((immhi << 2) | immlo) << 12);
		int rd = insn & 0x1F;
		n = nvk_a64_gen_mov64(out, rd, target);
		return n;
	}

	case NVK_PC_ADR: {
		int immlo = (insn >> 29) & 3;
		long immhi = nvk_sext((insn >> 5) & 0x7FFFF, 19);
		target = old_pc + ((immhi << 2) | immlo);
		int rd = insn & 0x1F;
		return nvk_a64_gen_mov64(out, rd, target);
	}

	case NVK_PC_B: {
		long imm26 = nvk_sext(insn & 0x3FFFFFF, 26);
		target = old_pc + (imm26 << 2);
		n = nvk_a64_gen_mov64(out, 17, target);
		out[n++] = NVK_A64_RET_X17;
		return n;
	}

	case NVK_PC_BL: {
		long imm26 = nvk_sext(insn & 0x3FFFFFF, 26);
		target = old_pc + (imm26 << 2);
		n = nvk_a64_gen_mov64(out, 17, target);
		out[n++] = 0xD63F0220U;  /* BLR X17 */
		return n;
	}

	case NVK_PC_BCOND: {
		long imm19 = nvk_sext((insn >> 5) & 0x7FFFF, 19);
		target = old_pc + (imm19 << 2);
		u32 inv = (insn & 0xFF00000FU) ^ 1U;  /* invert LSB of cond */
		int skip_n = 1 + 3 + 1;  /* worst case: MOVZ+2MOVK+RET = 4/5 */
		/* We'll fix the skip offset after emitting the jump. */
		int cond_slot = n;
		out[n++] = 0; /* placeholder */
		int jump_start = n;
		n += nvk_a64_gen_mov64(&out[n], 17, target);
		out[n++] = NVK_A64_RET_X17;
		skip_n = n - jump_start;
		/* B.!cond skip:  imm19 = skip_n, shifted left 5 */
		out[cond_slot] = inv | (((u32)skip_n & 0x7FFFF) << 5);
		return n;
	}

	case NVK_PC_CBZ: {
		long imm19 = nvk_sext((insn >> 5) & 0x7FFFF, 19);
		target = old_pc + (imm19 << 2);
		/* Invert CBZ<->CBNZ: toggle bit 24 */
		u32 inv = insn ^ 0x01000000U;
		inv &= ~(0x7FFFFU << 5);  /* clear imm19 */
		int cond_slot = n;
		out[n++] = 0; /* placeholder */
		int jump_start = n;
		n += nvk_a64_gen_mov64(&out[n], 17, target);
		out[n++] = NVK_A64_RET_X17;
		int skip_n = n - jump_start;
		out[cond_slot] = inv | (((u32)skip_n & 0x7FFFF) << 5);
		return n;
	}

	case NVK_PC_TBZ: {
		long imm14 = nvk_sext((insn >> 5) & 0x3FFF, 14);
		target = old_pc + (imm14 << 2);
		/* Invert TBZ<->TBNZ: toggle bit 24 */
		u32 inv = insn ^ 0x01000000U;
		inv &= ~(0x3FFFU << 5);  /* clear imm14 */
		int cond_slot = n;
		out[n++] = 0; /* placeholder */
		int jump_start = n;
		n += nvk_a64_gen_mov64(&out[n], 17, target);
		out[n++] = NVK_A64_RET_X17;
		int skip_n = n - jump_start;
		out[cond_slot] = inv | (((u32)skip_n & 0x3FFFU) << 5);
		return n;
	}

	case NVK_PC_LDR_LIT: {
		long imm19 = nvk_sext((insn >> 5) & 0x7FFFF, 19);
		target = old_pc + (imm19 << 2);
		int rt = insn & 0x1F;
		int opc = (insn >> 30) & 3;
		int is_simd = (insn >> 26) & 1;
		n = nvk_a64_gen_mov64(out, 17, target);
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

	case NVK_PC_LDRSW_LIT: {
		long imm19 = nvk_sext((insn >> 5) & 0x7FFFF, 19);
		target = old_pc + (imm19 << 2);
		int rt = insn & 0x1F;
		n = nvk_a64_gen_mov64(out, 17, target);
		out[n++] = 0xB9800000U | (17 << 5) | rt; /* LDRSW Xt, [X17] */
		return n;
	}

	case NVK_PC_PRFM_LIT: {
		long imm19 = nvk_sext((insn >> 5) & 0x7FFFF, 19);
		target = old_pc + (imm19 << 2);
		int rt = insn & 0x1F;
		n = nvk_a64_gen_mov64(out, 17, target);
		out[n++] = 0xF9800000U | (17 << 5) | rt; /* PRFM type, [X17] */
		return n;
	}
	}

	out[0] = insn;
	return 1;
}


enum nvk_hook_err {
	NVK_HOOK_OK         =  0,
	NVK_HOOK_E_NOINIT   = -1,
	NVK_HOOK_E_SHORT    = -2,
	NVK_HOOK_E_RELOC    = -3,
	NVK_HOOK_E_ALLOC    = -4,
	NVK_HOOK_E_PATCH    = -5,
	NVK_HOOK_E_CONFLICT = -6,
};

static __always_inline const char *nvk_hook_strerror(int err)
{
	switch (err) {
	case NVK_HOOK_OK:         return "ok";
	case NVK_HOOK_E_NOINIT:   return "engine not initialized";
	case NVK_HOOK_E_SHORT:    return "target function too short";
	case NVK_HOOK_E_RELOC:    return "instruction relocation failed";
	case NVK_HOOK_E_ALLOC:    return "trampoline alloc failed";
	case NVK_HOOK_E_PATCH:    return "code patch failed";
	case NVK_HOOK_E_CONFLICT: return "target already hooked/kprobed";
	default:                  return "unknown";
	}
}

#define NVK_HOOK_MAX_PATCH   6   /* BTI + PAC + LDR + BR + .quad(2) */
#define NVK_HOOK_TRAMP_CAP  64
#define NVK_HOOK_STUB_CAP  128

struct nvk_hook {
	void       *target;
	void       *replace;
	u32        *trampoline;
	u32         orig_insns[NVK_HOOK_MAX_PATCH];
	int         patch_count;
	int         active;
	volatile int enabled;
	int         short_b;
	volatile u64 hit_count;
	volatile unsigned long guard;
};

#define NVK_HOOK_COUNT(h) \
	__atomic_fetch_add(&(h)->hit_count, 1, __ATOMIC_RELAXED)

static __always_inline u64 nvk_hook_hits(struct nvk_hook *h)
{ return __atomic_load_n(&h->hit_count, __ATOMIC_RELAXED); }

static __always_inline void nvk_hook_reset_stats(struct nvk_hook *h)
{ __atomic_store_n(&h->hit_count, 0, __ATOMIC_RELAXED); }

static __always_inline int nvk_hook_enter(struct nvk_hook *h)
{
	unsigned long task;
	__asm__ __volatile__("mrs %0, sp_el0" : "=r"(task));
	unsigned long prev = __atomic_exchange_n(&h->guard, task,
						 __ATOMIC_ACQUIRE);
	if (prev == task)
		return 0;
	NVK_HOOK_COUNT(h);
	return 1;
}

static __always_inline void nvk_hook_leave(struct nvk_hook *h)
{
	__atomic_store_n(&h->guard, 0, __ATOMIC_RELEASE);
}

typedef void *(*nvk_modalloc_fn)(unsigned long);
typedef void *(*nvk_execmem_alloc_fn)(int type, unsigned long size);
typedef void  (*nvk_modfree_fn)(void *);
typedef void  (*nvk_flushic_fn)(unsigned long, unsigned long);
typedef int   (*nvk_patchtext_fn)(void **, u32 *, int);
typedef int   (*nvk_patchtns_fn)(void *, u32);

typedef void (*nvk_syncrcu_fn)(void);
typedef void (*nvk_msleep_fn)(unsigned int);

static nvk_modalloc_fn       _nvk_modalloc;
static nvk_execmem_alloc_fn  _nvk_execmem_alloc;
static nvk_modfree_fn        _nvk_modfree;
static nvk_flushic_fn        _nvk_flushic;
static nvk_patchtext_fn      _nvk_patchtext;
static nvk_patchtns_fn       _nvk_patchtns;
static nvk_syncrcu_fn        _nvk_syncrcu;
static nvk_msleep_fn         _nvk_msleep;
static int _nvk_inited;

typedef int (*nvk_ksize_fn)(unsigned long addr, unsigned long *sz,
			    unsigned long *off);
static nvk_ksize_fn _nvk_ksize;

#define _NVK_POOL_MIN_PAGE 4096
#define _NVK_POOL_ALIGN    16
#define _NVK_POOL_MAX      8

static __always_inline int _nvk_pool_page_size(void)
{
	unsigned long tcr;
	__asm__ __volatile__("mrs %0, tcr_el1" : "=r"(tcr));
	u32 tg1 = (tcr >> 30) & 3;
	if (tg1 == 1) return 16384;
	if (tg1 == 2) return 65536;
	return 4096;
}

static volatile int _nvk_pool_lock;

static __always_inline void _nvk_spin_lock(volatile int *lock)
{
	while (__atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE))
		__asm__ __volatile__("wfe" ::: "memory");
}

static __always_inline void _nvk_spin_unlock(volatile int *lock)
{
	__atomic_store_n(lock, 0, __ATOMIC_RELEASE);
	__asm__ __volatile__("sev" ::: "memory");
}

struct _nvk_pool_page {
	u32    *base;
	int     used;
	int     refcnt;
};

static struct _nvk_pool_page _nvk_pool[_NVK_POOL_MAX];
static int _nvk_pool_count;
static volatile u64 _nvk_pool_alloc_total;
static volatile u64 _nvk_pool_alloc_bytes;

static int _nvk_pool_pgsz;

static u32 *_nvk_pool_alloc(int bytes)
{
	int i, pgsz;
	u32 *ret = (void *)0;
	bytes = (bytes + _NVK_POOL_ALIGN - 1) & ~(_NVK_POOL_ALIGN - 1);

	if (!_nvk_pool_pgsz)
		_nvk_pool_pgsz = _nvk_pool_page_size();
	pgsz = _nvk_pool_pgsz;

	_nvk_spin_lock(&_nvk_pool_lock);

	for (i = 0; i < _nvk_pool_count; i++) {
		if (_nvk_pool[i].used + bytes <= pgsz) {
			ret = (u32 *)((unsigned long)_nvk_pool[i].base +
				      _nvk_pool[i].used);
			_nvk_pool[i].used += bytes;
			_nvk_pool[i].refcnt++;
			__atomic_fetch_add(&_nvk_pool_alloc_total, 1,
					   __ATOMIC_RELAXED);
			__atomic_fetch_add(&_nvk_pool_alloc_bytes, bytes,
					   __ATOMIC_RELAXED);
			_nvk_spin_unlock(&_nvk_pool_lock);
			return ret;
		}
	}

	if (_nvk_pool_count >= _NVK_POOL_MAX || !_nvk_modalloc) {
		_nvk_spin_unlock(&_nvk_pool_lock);
		return (void *)0;
	}

	_nvk_spin_unlock(&_nvk_pool_lock);

	u32 *page = (u32 *)_nvk_alloc_exec(pgsz);
	if (!page) return (void *)0;

	_nvk_spin_lock(&_nvk_pool_lock);
	if (_nvk_pool_count >= _NVK_POOL_MAX) {
		_nvk_spin_unlock(&_nvk_pool_lock);
		_nvk_modfree(page);
		return (void *)0;
	}
	i = _nvk_pool_count++;
	_nvk_pool[i].base = page;
	_nvk_pool[i].used = bytes;
	_nvk_pool[i].refcnt = 1;
	_nvk_spin_unlock(&_nvk_pool_lock);
	return page;
}

static void _nvk_pool_free(u32 *ptr)
{
	int i;
	int pgsz = _nvk_pool_pgsz ? _nvk_pool_pgsz : 4096;
	if (!ptr) return;
	_nvk_spin_lock(&_nvk_pool_lock);
	for (i = 0; i < _nvk_pool_count; i++) {
		unsigned long base = (unsigned long)_nvk_pool[i].base;
		if ((unsigned long)ptr >= base &&
		    (unsigned long)ptr < base + (unsigned long)pgsz) {
			if (_nvk_pool[i].refcnt <= 0) {
				_nvk_spin_unlock(&_nvk_pool_lock);
				return;
			}
			if (--_nvk_pool[i].refcnt <= 0) {
				u32 *to_free = _nvk_pool[i].base;
				_nvk_pool[i] = _nvk_pool[--_nvk_pool_count];
				_nvk_spin_unlock(&_nvk_pool_lock);
				_nvk_modfree(to_free);
				return;
			}
			_nvk_spin_unlock(&_nvk_pool_lock);
			return;
		}
	}
	_nvk_spin_unlock(&_nvk_pool_lock);
	_nvk_modfree(ptr);
}

static volatile u64 _nvk_hook_install_cnt;
static volatile u64 _nvk_hook_remove_cnt;

static void *_nvk_alloc_exec(unsigned long size)
{
	if (_nvk_modalloc)
		return _nvk_modalloc(size);
	if (_nvk_execmem_alloc)
		return _nvk_execmem_alloc(0 /* EXECMEM_MODULE_TEXT */, size);
	return (void *)0;
}

static int nvk_hook_init(void)
{
	if (_nvk_inited) return 0;
	_nvk_modalloc = (nvk_modalloc_fn)NVK_LOOKUP("module_alloc");
	if (!_nvk_modalloc)
		_nvk_execmem_alloc =
			(nvk_execmem_alloc_fn)NVK_LOOKUP("execmem_alloc");
	_nvk_modfree  = (nvk_modfree_fn)NVK_LOOKUP("module_memfree");
	if (!_nvk_modfree)
		_nvk_modfree = (nvk_modfree_fn)NVK_LOOKUP("execmem_free");
	if (!_nvk_modfree)
		_nvk_modfree = (nvk_modfree_fn)NVK_LOOKUP("vfree");
	_nvk_flushic = (nvk_flushic_fn)NVK_LOOKUP("flush_icache_range");
	if (!_nvk_flushic)
		_nvk_flushic = (nvk_flushic_fn)NVK_LOOKUP("__flush_icache_range");
	_nvk_patchtext = (nvk_patchtext_fn)NVK_LOOKUP("aarch64_insn_patch_text");
	_nvk_patchtns  = (nvk_patchtns_fn)NVK_LOOKUP("aarch64_insn_patch_text_nosync");
	_nvk_ksize = (nvk_ksize_fn)NVK_LOOKUP("kallsyms_lookup_size_offset");
	_nvk_syncrcu = (nvk_syncrcu_fn)NVK_LOOKUP("synchronize_rcu");
	_nvk_msleep  = (nvk_msleep_fn)NVK_LOOKUP("msleep");
	if ((!_nvk_modalloc && !_nvk_execmem_alloc) ||
	    !_nvk_modfree || !_nvk_flushic)
		return -1;
	_nvk_inited = 1;
	return 0;
}

static unsigned long _nvk_fn_size(void *addr)
{
	unsigned long sz = 0, off = 0;
	if (_nvk_ksize && _nvk_ksize((unsigned long)addr, &sz, &off))
		return sz - off;
	return 0;
}

#define NVK_A64_BRK_KPROBE 0xD4200080U

static __always_inline unsigned long nvk_strip_pac(unsigned long addr)
{
	unsigned long tcr, va_bits, mask;
	__asm__ __volatile__("mrs %0, tcr_el1" : "=r"(tcr));
	va_bits = 64 - ((tcr >> 16) & 0x3FUL);
	mask = (1UL << va_bits) - 1;
	addr = (addr & (1UL << 63)) ? (addr | ~mask) : (addr & mask);
	addr &= ~(0xFFUL << 56);
	return addr;
}

static __always_inline int nvk_a64_is_stp_fp_lr(u32 insn)
{
	return (insn & 0xFFC07FFF) == 0xA9807BFD;
}

static __always_inline int nvk_a64_is_frame_setup(u32 insn)
{
	if ((insn & 0x7FE0FFE0) == 0x2A0003E0) return 1; /* MOV Wd, Wn */
	if ((insn & 0xFFE0FFE0) == 0xAA0003E0) return 1; /* MOV Xd, Xn */
	return 0;
}

static __always_inline int nvk_a64_is_hook_patch(u32 insn)
{
	if (insn == NVK_A64_BRK_KPROBE) return 1;
	if (insn == 0x58000050U) return 1;  /* LDR X16, [PC+8] */
	return 0;
}

#define NVK_A64_FTRACE_NOP  0xD503201FU
#define NVK_A64_BRK_FTRACE  0xD4200000U  /* BRK #0 — ftrace entry */

static __always_inline int nvk_a64_is_ftrace_site(u32 *code)
{
	u32 insn = code[0];
	if (insn == NVK_A64_BRK_FTRACE) return 1;
	if ((insn & 0xFC000000) == 0x94000000) {
		long imm26 = nvk_sext(insn & 0x3FFFFFF, 26);
		long off = imm26 << 2;
		if (off < -0x100000 || off > 0x100000) return 1;
	}
	return 0;
}

static __always_inline int nvk_a64_is_kprobe_bp(u32 insn)
{
	return insn == NVK_A64_BRK_KPROBE
	    || (insn & 0xFFE0001FU) == 0xD4200000U;
}

static __always_inline int nvk_a64_is_exclusive(u32 insn)
{
	return (insn & 0x3F000000) == 0x08000000;
}

static __always_inline int nvk_a64_is_svc_hvc(u32 insn)
{
	u32 masked = insn & 0xFFE0001FU;
	return masked == 0xD4000001U  /* SVC */
	    || masked == 0xD4000002U  /* HVC */
	    || masked == 0xD4000003U; /* SMC */
}

static __always_inline int nvk_a64_is_hazardous(u32 insn)
{
	if (nvk_a64_is_exclusive(insn)) return 1;
	if (nvk_a64_is_svc_hvc(insn))   return 1;
	if ((insn & 0xFE200000U) == 0xD4200000U) return 1; /* BRK */
	return 0;
}

static __always_inline void _nvk_dcache_clean(unsigned long addr,
					      unsigned long end)
{
	unsigned long line;
	for (line = addr & ~63UL; line < end; line += 64)
		__asm__ __volatile__("dc cvau, %0" :: "r"(line) : "memory");
	__asm__ __volatile__("dsb ish" ::: "memory");
}

static __always_inline void _nvk_icache_inval(unsigned long addr,
					      unsigned long end)
{
	unsigned long line;
	for (line = addr & ~63UL; line < end; line += 64)
		__asm__ __volatile__("ic ivau, %0" :: "r"(line) : "memory");
	__asm__ __volatile__("dsb ish" ::: "memory");
	__asm__ __volatile__("isb" ::: "memory");
}

static void _nvk_write_insn(void *addr, u32 insn)
{
	if (_nvk_patchtns)
		_nvk_patchtns(addr, insn);
	else {
		*(volatile u32 *)addr = insn;
		_nvk_dcache_clean((unsigned long)addr,
				  (unsigned long)addr + 4);
		if (_nvk_flushic)
			_nvk_flushic((unsigned long)addr,
				     (unsigned long)addr + 4);
		else
			_nvk_icache_inval((unsigned long)addr,
					  (unsigned long)addr + 4);
	}
}

static int _nvk_verify_patch(u32 *target, u32 *expected, int count)
{
	int i;
	__asm__ __volatile__("dsb ish" ::: "memory");
	__asm__ __volatile__("isb" ::: "memory");
	for (i = 0; i < count; i++) {
		if (*(volatile u32 *)&target[i] != expected[i])
			return -1;
	}
	return 0;
}

static int _nvk_patch_multi(u32 *target, u32 *insns, int count)
{
	int i;

	if (_nvk_patchtext && count <= 16) {
		void *addrs[16];
		for (i = 0; i < count; i++)
			addrs[i] = &target[i];
		int ret = _nvk_patchtext(addrs, insns, count);
		if (ret) return ret;
	} else {
		for (i = 0; i < count; i++)
			_nvk_write_insn(&target[i], insns[i]);
	}

	return _nvk_verify_patch(target, insns, count);
}


static void _nvk_scan_entry(u32 *code, int *skip, int *total)
{
	int s = 0;
	if (nvk_a64_is_bti(code[s]))      s++;
	if (nvk_a64_is_pac(code[s]))      s++;
	if (nvk_a64_is_stp_fp_lr(code[s])) s++;
	if (code[s] == NVK_A64_NOP)       s++;
	*total = s + 4; /* prefix + LDR + BR + .quad(2) */
	if (*total > NVK_HOOK_MAX_PATCH)
		*total = NVK_HOOK_MAX_PATCH;
	if (*total < 4)
		*total = 4;
	*skip = s;
}

enum nvk_scan_result {
	NVK_SCAN_OK              =  0,
	NVK_SCAN_TOO_SHORT       = -1,
	NVK_SCAN_HAZARDOUS       = -2,
	NVK_SCAN_UNRELOCATABLE   = -3,
	NVK_SCAN_ALREADY_HOOKED  =  1,
	NVK_SCAN_FTRACE_ACTIVE   =  2,
	NVK_SCAN_KPROBE_ACTIVE   =  3,
};

static __always_inline const char *nvk_scan_strerror(int r)
{
	switch (r) {
	case NVK_SCAN_OK:             return "hookable";
	case NVK_SCAN_TOO_SHORT:      return "function too short";
	case NVK_SCAN_HAZARDOUS:      return "contains hazardous instructions";
	case NVK_SCAN_UNRELOCATABLE:  return "unrelocatable pc-relative insn";
	case NVK_SCAN_ALREADY_HOOKED: return "already hooked";
	case NVK_SCAN_FTRACE_ACTIVE:  return "ftrace active at entry";
	case NVK_SCAN_KPROBE_ACTIVE:  return "kprobe active at entry";
	default:                      return "unknown";
	}
}

static enum nvk_scan_result nvk_hook_scan(void *target)
{
	target = (void *)nvk_strip_pac((unsigned long)target);
	u32 *code = (u32 *)target;
	int skip, patch_count, i;

	if (nvk_a64_is_hook_patch(code[0]))
		return NVK_SCAN_ALREADY_HOOKED;

	if (nvk_a64_is_kprobe_bp(code[0]))
		return NVK_SCAN_KPROBE_ACTIVE;

	if (nvk_a64_is_ftrace_site(code))
		return NVK_SCAN_FTRACE_ACTIVE;

	if (_nvk_ksize) {
		unsigned long fn_sz = _nvk_fn_size(target);
		if (fn_sz > 0 && fn_sz < NVK_HOOK_MAX_PATCH * 4)
			return NVK_SCAN_TOO_SHORT;
	}

	_nvk_scan_entry(code, &skip, &patch_count);

	for (i = 0; i < patch_count; i++) {
		u32 insn = code[i];
		if (nvk_a64_is_bti(insn) || insn == NVK_A64_NOP)
			continue;
		if (nvk_a64_is_hazardous(insn))
			return NVK_SCAN_HAZARDOUS;
		u32 tmp[8];
		int n = nvk_a64_relocate_abs(
			insn, (unsigned long)&code[i], tmp);
		if (n == 0) return NVK_SCAN_UNRELOCATABLE;
	}

	return NVK_SCAN_OK;
}


static int nvk_hook_install(struct nvk_hook *h, void *target,
			    void *replace, void **orig)
{
	target = (void *)nvk_strip_pac((unsigned long)target);
	u32 *code = (u32 *)target;
	u32  tramp[NVK_HOOK_TRAMP_CAP];
	int  tidx = 0, skip, patch_count, i;
	u32  patch[NVK_HOOK_MAX_PATCH];
	int  use_short_b = 0;

	if (!_nvk_inited) return NVK_HOOK_E_NOINIT;
	if (!target || !replace || !orig) return NVK_HOOK_E_SHORT;
	if (((unsigned long)target & 3) != 0) return NVK_HOOK_E_SHORT;
	if ((unsigned long)target < 0xFFFF000000000000UL)
		return NVK_HOOK_E_SHORT;

	if (nvk_a64_is_kprobe_bp(code[0]))
		return NVK_HOOK_E_CONFLICT;

	h->target = target;
	h->replace = replace;
	h->trampoline = (void *)0;
	h->active = 0;
	h->short_b = 0;
	h->hit_count = 0;
	h->guard = 0;

	{
		int chained = nvk_a64_is_hook_patch(code[0]);

		_nvk_scan_entry(code, &skip, &patch_count);

		if (!chained) {
			unsigned long fn_sz = _nvk_fn_size(target);
			if (fn_sz > 0 &&
			    fn_sz < (unsigned long)patch_count * 4) {
				int min_pc = skip + 1;
				long b_off = (long)(unsigned long)replace -
					(long)(unsigned long)&code[skip];
				if (fn_sz >= (unsigned long)min_pc * 4 &&
				    nvk_a64_b_in_range(b_off)) {
					use_short_b = 1;
					patch_count = min_pc;
				} else {
					return NVK_HOOK_E_SHORT;
				}
			}
		}

		h->patch_count = patch_count;
		h->short_b = use_short_b;
		for (i = 0; i < patch_count; i++)
			h->orig_insns[i] = code[i];

		tramp[tidx++] = NVK_A64_BTI_JC;

		if (chained) {
			int qoff = 0;
			if (nvk_a64_is_bti(code[0])) qoff = 1;
			unsigned long prev = *(unsigned long *)&code[qoff + 2];
			tidx += nvk_a64_gen_mov64(&tramp[tidx], 17, prev);
			tramp[tidx++] = NVK_A64_RET_X17;
		} else {
			for (i = 0; i < patch_count; i++) {
				u32 insn = code[i];
				if (nvk_a64_is_bti(insn) ||
				    insn == NVK_A64_NOP)
					continue;
				if (nvk_a64_is_hazardous(insn))
					return NVK_HOOK_E_RELOC;
				unsigned long insn_pc =
					(unsigned long)&code[i];
				int n = nvk_a64_relocate_abs(
					insn, insn_pc, &tramp[tidx]);
				if (n == 0) return NVK_HOOK_E_RELOC;
				tidx += n;
				if (tidx >= NVK_HOOK_TRAMP_CAP - 8)
					return NVK_HOOK_E_RELOC;
			}
			unsigned long back =
				(unsigned long)target + patch_count * 4;
			tidx += nvk_a64_gen_mov64(&tramp[tidx], 17, back);
			tramp[tidx++] = NVK_A64_RET_X17;
		}
	}

	h->trampoline = _nvk_pool_alloc(tidx * 4);
	if (!h->trampoline) return NVK_HOOK_E_ALLOC;

	for (i = 0; i < tidx; i++)
		h->trampoline[i] = tramp[i];
	_nvk_dcache_clean((unsigned long)h->trampoline,
			  (unsigned long)&h->trampoline[tidx]);
	_nvk_flushic((unsigned long)h->trampoline,
		     (unsigned long)&h->trampoline[tidx]);

	*orig = (void *)h->trampoline;

	for (i = 0; i < patch_count; i++)
		patch[i] = NVK_A64_NOP;
	{
		int jmp = 0;
		if (nvk_a64_is_bti(code[0])) {
			patch[0] = code[0];
			jmp = 1;
		}
		if (use_short_b) {
			long b_off = (long)(unsigned long)replace -
				(long)(unsigned long)&code[jmp];
			patch[jmp] = nvk_a64_gen_b(b_off);
		} else {
			patch[jmp + 0] = 0x58000050U;
			patch[jmp + 1] = 0xD61F0200U;
			*(unsigned long *)&patch[jmp + 2] =
				(unsigned long)replace;
		}
	}

	if (_nvk_patch_multi(code, patch, patch_count) != 0) {
		_nvk_pool_free(h->trampoline);
		h->trampoline = (void *)0;
		return NVK_HOOK_E_PATCH;
	}

	h->active = 1;
	h->enabled = 1;
	__atomic_fetch_add(&_nvk_hook_install_cnt, 1, __ATOMIC_RELAXED);
	return NVK_HOOK_OK;
}


#define _CTX_SIZE  272
#define _CTX_NZCV  256
#define _CTX_FORCE 264

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

static const u32 _nvk_ctx_stub_template[] = {
	/*  0 */ NVK_A64_BTI_JC,
	/*  1 */ _A64E_STP_PRE16(16, 17),
	/*  2 */ _A64E_MOVZ(16, 0),
	/*  3 */ _A64E_MOVK16(16),
	/*  4 */ _A64E_MOVK32(16),
	/*  5 */ _A64E_MOVK48(16),
	/*  6 */ _A64E_LDR_WREG(16, 16),
	/*  7 */ _A64E_CBZ_W_FWD(16, 97-7),
	/*  8 */ _A64E_MRS_SP_EL0(16),
	/*  9 */ _A64E_MOVZ(17, 0),
	/* 10 */ _A64E_MOVK16(17),
	/* 11 */ _A64E_MOVK32(17),
	/* 12 */ _A64E_MOVK48(17),
	/* 13 */ _A64E_LDR_XREG(17, 17),
	/* 14 */ _A64E_CMP_REG(16, 17),
	/* 15 */ _A64E_BEQ_FWD(97-15),
	/* 16 */ _A64E_LDP_POST16(16, 17),
	/* 17 */ _A64E_SUB_SP_I(_CTX_SIZE),
	/* 18 */ _A64E_STP_SP( 0,  1,   0),
	/* 19 */ _A64E_STP_SP( 2,  3,  16),
	/* 20 */ _A64E_STP_SP( 4,  5,  32),
	/* 21 */ _A64E_STP_SP( 6,  7,  48),
	/* 22 */ _A64E_STP_SP( 8,  9,  64),
	/* 23 */ _A64E_STP_SP(10, 11,  80),
	/* 24 */ _A64E_STP_SP(12, 13,  96),
	/* 25 */ _A64E_STP_SP(14, 15, 112),
	/* 26 */ _A64E_STP_SP(16, 17, 128),
	/* 27 */ _A64E_STP_SP(18, 19, 144),
	/* 28 */ _A64E_STP_SP(20, 21, 160),
	/* 29 */ _A64E_STP_SP(22, 23, 176),
	/* 30 */ _A64E_STP_SP(24, 25, 192),
	/* 31 */ _A64E_STP_SP(26, 27, 208),
	/* 32 */ _A64E_STP_SP(28, 29, 224),
	/* 33 */ _A64E_STP_SP(30, 31, 240),
	/* 34 */ _A64E_MRS_NZCV(1),
	/* 35 */ _A64E_STR_SP(1,  _CTX_NZCV),
	/* 36 */ _A64E_STR_SP(31, _CTX_FORCE),
	/* 37 */ _A64E_MRS_SP_EL0(0),
	/* 38 */ _A64E_MOVZ(19, 0),
	/* 39 */ _A64E_MOVK16(19),
	/* 40 */ _A64E_MOVK32(19),
	/* 41 */ _A64E_MOVK48(19),
	/* 42 */ _A64E_STR_XREG(0, 19),
	/* 43 */ _A64E_MOV_FROM_SP(0),
	/* 44 */ _A64E_MOVZ(3, 0),
	/* 45 */ _A64E_MOVK16(3),
	/* 46 */ _A64E_MOVK32(3),
	/* 47 */ _A64E_MOVK48(3),
	/* 48 */ 0xD63F0060U,                    /* BLR X3 */
	/* 49 */ _A64E_STR_XREG(31, 19),
	/* 50 */ _A64E_LDR_SP(1, _CTX_FORCE),
	/* 51 */ _A64E_CBNZ_FWD(1, 76-51),  /* → force_jump path */
	/* 52 */ _A64E_LDR_SP(2, _CTX_NZCV),
	/* 53 */ _A64E_MSR_NZCV(2),
	/* 54 */ _A64E_LDP_SP( 2,  3,  16),
	/* 55 */ _A64E_LDP_SP( 4,  5,  32),
	/* 56 */ _A64E_LDP_SP( 6,  7,  48),
	/* 57 */ _A64E_LDP_SP( 8,  9,  64),
	/* 58 */ _A64E_LDP_SP(10, 11,  80),
	/* 59 */ _A64E_LDP_SP(12, 13,  96),
	/* 60 */ _A64E_LDP_SP(14, 15, 112),
	/* 61 */ _A64E_LDP_SP(16, 17, 128),
	/* 62 */ _A64E_LDP_SP(18, 19, 144),
	/* 63 */ _A64E_LDP_SP(20, 21, 160),
	/* 64 */ _A64E_LDP_SP(22, 23, 176),
	/* 65 */ _A64E_LDP_SP(24, 25, 192),
	/* 66 */ _A64E_LDP_SP(26, 27, 208),
	/* 67 */ _A64E_LDP_SP(28, 29, 224),
	/* 68 */ _A64E_LDP_SP(30, 31, 240),
	/* 69 */ _A64E_LDP_SP( 0,  1,   0),
	/* 70 */ _A64E_ADD_SP_I(_CTX_SIZE),
	/* 71 */ _A64E_MOVZ(17, 0),
	/* 72 */ _A64E_MOVK16(17),
	/* 73 */ _A64E_MOVK32(17),
	/* 74 */ _A64E_MOVK48(17),
	/* 75 */ NVK_A64_RET_X17,
	/* 76 */ _A64E_LDR_SP(16, 128),
	/* 77 */ _A64E_MOV_REG(17, 1),
	/* 78 */ _A64E_LDR_SP(2, _CTX_NZCV),
	/* 79 */ _A64E_MSR_NZCV(2),
	/* 80 */ _A64E_LDP_SP( 2,  3,  16),
	/* 81 */ _A64E_LDP_SP( 4,  5,  32),
	/* 82 */ _A64E_LDP_SP( 6,  7,  48),
	/* 83 */ _A64E_LDP_SP( 8,  9,  64),
	/* 84 */ _A64E_LDP_SP(10, 11,  80),
	/* 85 */ _A64E_LDP_SP(12, 13,  96),
	/* 86 */ _A64E_LDP_SP(14, 15, 112),
	/* 87 */ _A64E_LDP_SP(18, 19, 144),
	/* 88 */ _A64E_LDP_SP(20, 21, 160),
	/* 89 */ _A64E_LDP_SP(22, 23, 176),
	/* 90 */ _A64E_LDP_SP(24, 25, 192),
	/* 91 */ _A64E_LDP_SP(26, 27, 208),
	/* 92 */ _A64E_LDP_SP(28, 29, 224),
	/* 93 */ _A64E_LDR_SP(30, 240),
	/* 94 */ _A64E_LDP_SP( 0,  1,   0),
	/* 95 */ _A64E_ADD_SP_I(_CTX_SIZE),
	/* 96 */ NVK_A64_RET_X17,
	/* 97 */ _A64E_LDP_POST16(16, 17),
	/* 98 */ _A64E_MOVZ(17, 0),
	/* 99 */ _A64E_MOVK16(17),
	/*100 */ _A64E_MOVK32(17),
	/*101 */ _A64E_MOVK48(17),
	/*102 */ NVK_A64_RET_X17,
};

#define _CTX_STUB_LEN     (sizeof(_nvk_ctx_stub_template) / sizeof(u32))
#define _CTX_ENABLED_SLOT 2
#define _CTX_GUARD_SLOT_A 9
#define _CTX_GUARD_SLOT_B 38
#define _CTX_HANDLER_SLOT 44
#define _CTX_TRAMP_SLOT_A 71
#define _CTX_TRAMP_SLOT_B 98

_Static_assert(_CTX_STUB_LEN == 103, "context stub v4 size mismatch");
_Static_assert(_CTX_SIZE % 16 == 0, "context frame must be 16-byte aligned");

typedef void (*nvk_ctx_handler_t)(nvk_reg_ctx *ctx);
typedef void (*nvk_ctx_fp_handler_t)(nvk_reg_ctx *ctx, nvk_fp_state *fp);

#define NVK_CTX_HANDLER_FP(wrapper_name, user_fn)                        \
	static void wrapper_name(nvk_reg_ctx *ctx) {                    \
		nvk_fp_state __fp_state;                                \
		NVK_SAVE_FP(&__fp_state);                               \
		user_fn(ctx, &__fp_state);                              \
		NVK_RESTORE_FP(&__fp_state);                            \
	}

struct nvk_hook_ctx {
	struct nvk_hook     base;
	u32                *stub;
	u32                *tramp_code;
	volatile unsigned long guard_task;
};

static void _nvk_patch_mov64(u32 *page, int slot, int rd, u64 addr)
{
	u32 buf[4];
	int n = nvk_a64_gen_mov64(buf, rd, addr), i;
	for (i = 0; i < n && i < 4; i++)
		page[slot + i] = buf[i];
}

static int nvk_hook_install_ctx(struct nvk_hook_ctx *h, void *target,
				nvk_ctx_handler_t handler, void **call_orig)
{
	target = (void *)nvk_strip_pac((unsigned long)target);
	u32 *code = (u32 *)target;
	u32  tramp[NVK_HOOK_TRAMP_CAP];
	int  tidx = 0, skip, patch_count, i, n;
	u32  patch[NVK_HOOK_MAX_PATCH];
	u32 *page;

	if (!_nvk_inited) return NVK_HOOK_E_NOINIT;
	if (!target || !handler) return NVK_HOOK_E_SHORT;
	if (((unsigned long)target & 3) != 0) return NVK_HOOK_E_SHORT;
	if ((unsigned long)target < 0xFFFF000000000000UL)
		return NVK_HOOK_E_SHORT;

	if (nvk_a64_is_kprobe_bp(code[0]))
		return NVK_HOOK_E_CONFLICT;

	h->base.target = target;
	h->base.replace = (void *)handler;
	h->base.trampoline = (void *)0;
	h->base.active = 0;
	h->stub = (void *)0;
	h->guard_task = 0;

	{
		int chained = nvk_a64_is_hook_patch(code[0]);
		if (!chained) {
			unsigned long fn_sz = _nvk_fn_size(target);
			if (fn_sz > 0 && fn_sz < NVK_HOOK_MAX_PATCH * 4)
				return NVK_HOOK_E_SHORT;
		}

		_nvk_scan_entry(code, &skip, &patch_count);
		h->base.patch_count = patch_count;
		for (i = 0; i < patch_count; i++)
			h->base.orig_insns[i] = code[i];

		tramp[tidx++] = NVK_A64_BTI_JC;

		if (chained) {
			int qoff = 0;
			if (nvk_a64_is_bti(code[0])) qoff = 1;
			unsigned long prev =
				*(unsigned long *)&code[qoff + 2];
			tidx += nvk_a64_gen_mov64(&tramp[tidx], 17, prev);
			tramp[tidx++] = NVK_A64_RET_X17;
		} else {
			for (i = 0; i < patch_count; i++) {
				u32 insn = code[i];
				if (nvk_a64_is_bti(insn) ||
				    insn == NVK_A64_NOP)
					continue;
				if (nvk_a64_is_hazardous(insn))
					return NVK_HOOK_E_RELOC;
				n = nvk_a64_relocate_abs(
					insn, (unsigned long)&code[i],
					&tramp[tidx]);
				if (n == 0) return NVK_HOOK_E_RELOC;
				tidx += n;
				if (tidx >= NVK_HOOK_TRAMP_CAP - 8)
					return NVK_HOOK_E_RELOC;
			}
			unsigned long back =
				(unsigned long)target + patch_count * 4;
			tidx += nvk_a64_gen_mov64(&tramp[tidx], 17, back);
			tramp[tidx++] = NVK_A64_RET_X17;
		}
	}

	{
		int page_sz = (_CTX_STUB_LEN + 4 + tidx + 4) * 4;
		page_sz = (page_sz + 63) & ~63; /* cache-line align */
		if (page_sz < 512) page_sz = 512;
		page = (u32 *)_nvk_alloc_exec(page_sz);
		if (!page) return NVK_HOOK_E_ALLOC;
	}

	h->stub = page;
	h->tramp_code = page + _CTX_STUB_LEN + 4;
	h->base.trampoline = page;

	for (i = 0; i < (int)_CTX_STUB_LEN; i++)
		page[i] = _nvk_ctx_stub_template[i];

	_nvk_patch_mov64(page, _CTX_ENABLED_SLOT, 16,
			 (u64)(unsigned long)&h->base.enabled);
	_nvk_patch_mov64(page, _CTX_GUARD_SLOT_A, 17,
			 (u64)(unsigned long)&h->guard_task);
	_nvk_patch_mov64(page, _CTX_GUARD_SLOT_B, 19,
			 (u64)(unsigned long)&h->guard_task);
	_nvk_patch_mov64(page, _CTX_HANDLER_SLOT, 3,
			 (u64)(unsigned long)handler);
	_nvk_patch_mov64(page, _CTX_TRAMP_SLOT_A, 17,
			 (u64)(unsigned long)h->tramp_code);
	_nvk_patch_mov64(page, _CTX_TRAMP_SLOT_B, 17,
			 (u64)(unsigned long)h->tramp_code);

	for (i = 0; i < tidx; i++)
		h->tramp_code[i] = tramp[i];

	{
		unsigned long flush_end = (unsigned long)&h->tramp_code[tidx];
		_nvk_dcache_clean((unsigned long)page, flush_end);
		_nvk_flushic((unsigned long)page, flush_end);
	}

	if (call_orig)
		*call_orig = (void *)h->tramp_code;

	for (i = 0; i < patch_count; i++)
		patch[i] = NVK_A64_NOP;
	{
		int jmp = 0;
		if (nvk_a64_is_bti(code[0])) {
			patch[0] = code[0];
			jmp = 1;
		}
		patch[jmp + 0] = 0x58000050U;  /* LDR X16, [PC, #8] */
		patch[jmp + 1] = 0xD61F0200U;  /* BR  X16 */
		*(unsigned long *)&patch[jmp + 2] = (unsigned long)h->stub;
	}

	if (_nvk_patch_multi(code, patch, patch_count) != 0) {
		_nvk_modfree(page);
		h->stub = (void *)0;
		return NVK_HOOK_E_PATCH;
	}

	h->base.active = 1;
	h->base.enabled = 1;
	__atomic_fetch_add(&_nvk_hook_install_cnt, 1, __ATOMIC_RELAXED);
	return NVK_HOOK_OK;
}


static void _nvk_quiesce(void)
{
	if (_nvk_syncrcu)
		_nvk_syncrcu();
	else if (_nvk_msleep)
		_nvk_msleep(50);
}

static void _nvk_full_barrier(void)
{
	__asm__ __volatile__("dsb ish" ::: "memory");
	__asm__ __volatile__("isb" ::: "memory");
}

static void nvk_hook_remove(struct nvk_hook *h)
{
	if (!h->active) return;

	WRITE_ONCE(h->enabled, 0);
	__atomic_store_n(&h->guard, 0, __ATOMIC_RELEASE);
	_nvk_full_barrier();
	__asm__ __volatile__("sev" ::: "memory");

	_nvk_quiesce();

	u32 *code = (u32 *)h->target;
	_nvk_patch_multi(code, h->orig_insns, h->patch_count);

	_nvk_full_barrier();
	_nvk_quiesce();
	_nvk_full_barrier();

	if (h->trampoline) {
		_nvk_pool_free(h->trampoline);
		h->trampoline = (void *)0;
	}
	h->active = 0;
	__atomic_fetch_add(&_nvk_hook_remove_cnt, 1, __ATOMIC_RELAXED);
}

static int nvk_hook_replace(struct nvk_hook *h, void *new_replace,
			    void **new_orig)
{
	u32 patch[NVK_HOOK_MAX_PATCH];
	u32 *code;
	int i, jmp;

	if (!h->active) return -1;
	WRITE_ONCE(h->enabled, 0);
	_nvk_full_barrier();
	_nvk_quiesce();

	h->replace = new_replace;
	code = (u32 *)h->target;

	for (i = 0; i < h->patch_count; i++)
		patch[i] = NVK_A64_NOP;
	jmp = 0;
	if (nvk_a64_is_bti(h->orig_insns[0])) {
		patch[0] = h->orig_insns[0];
		jmp = 1;
	}
	if (h->short_b) {
		long b_off = (long)(unsigned long)new_replace -
			(long)(unsigned long)&code[jmp];
		if (!nvk_a64_b_in_range(b_off))
			return -1;
		patch[jmp] = nvk_a64_gen_b(b_off);
	} else {
		patch[jmp + 0] = 0x58000050U;
		patch[jmp + 1] = 0xD61F0200U;
		*(unsigned long *)&patch[jmp + 2] =
			(unsigned long)new_replace;
	}

	_nvk_patch_multi(code, patch, h->patch_count);
	_nvk_full_barrier();

	if (new_orig)
		*new_orig = (void *)h->trampoline;
	WRITE_ONCE(h->enabled, 1);
	return 0;
}

static void nvk_hook_remove_ctx(struct nvk_hook_ctx *h)
{
	if (!h->base.active) return;

	WRITE_ONCE(h->base.enabled, 0);
	WRITE_ONCE(h->guard_task, 0);
	_nvk_full_barrier();
	__asm__ __volatile__("sev" ::: "memory");

	_nvk_quiesce();

	u32 *code = (u32 *)h->base.target;
	_nvk_patch_multi(code, h->base.orig_insns, h->base.patch_count);

	_nvk_full_barrier();
	_nvk_quiesce();
	_nvk_full_barrier();

	if (h->stub) {
		_nvk_modfree(h->stub);
		h->stub = (void *)0;
		h->tramp_code = (void *)0;
		h->base.trampoline = (void *)0;
	}
	h->base.active = 0;
	__atomic_fetch_add(&_nvk_hook_remove_cnt, 1, __ATOMIC_RELAXED);
}

static int nvk_hook_replace_ctx(struct nvk_hook_ctx *h,
				nvk_ctx_handler_t new_handler)
{
	if (!h->base.active || !h->stub) return -1;
	WRITE_ONCE(h->base.enabled, 0);
	_nvk_full_barrier();
	_nvk_quiesce();
	h->base.replace = (void *)new_handler;
	_nvk_patch_mov64(h->stub, _CTX_HANDLER_SLOT, 3,
			 (u64)(unsigned long)new_handler);
	_nvk_dcache_clean((unsigned long)&h->stub[_CTX_HANDLER_SLOT],
			  (unsigned long)&h->stub[_CTX_HANDLER_SLOT + 4]);
	_nvk_flushic((unsigned long)&h->stub[_CTX_HANDLER_SLOT],
		     (unsigned long)&h->stub[_CTX_HANDLER_SLOT + 4]);
	_nvk_full_barrier();
	WRITE_ONCE(h->base.enabled, 1);
	return 0;
}

static __always_inline int nvk_hook_is_enabled(struct nvk_hook *h)
{ return READ_ONCE(h->enabled); }

static __always_inline void nvk_hook_enable(struct nvk_hook *h)
{ WRITE_ONCE(h->enabled, 1); }

static __always_inline void nvk_hook_disable(struct nvk_hook *h)
{ WRITE_ONCE(h->enabled, 0); }

struct nvk_hook_batch {
	struct nvk_hook *hook;
	void            *target;
	void            *replace;
	void           **orig;
	int              result;
};

static int nvk_hook_install_batch(struct nvk_hook_batch *batch, int count)
{
	int i, ok = 0;
	for (i = 0; i < count; i++) {
		batch[i].result = nvk_hook_install(
			batch[i].hook, batch[i].target,
			batch[i].replace, batch[i].orig);
		if (batch[i].result == NVK_HOOK_OK) ok++;
	}
	if (ok > 0 && ok < count) {
		for (i = 0; i < count; i++) {
			if (batch[i].result == NVK_HOOK_OK)
				nvk_hook_remove(batch[i].hook);
		}
		return -1;
	}
	return ok == count ? 0 : -1;
}

static void nvk_hook_cleanup(void)
{
	int i;

	_nvk_full_barrier();

	if (_nvk_syncrcu) _nvk_syncrcu();
	_nvk_full_barrier();
	if (_nvk_syncrcu) _nvk_syncrcu();

	if (_nvk_msleep) _nvk_msleep(100);

	_nvk_spin_lock(&_nvk_pool_lock);
	for (i = 0; i < _nvk_pool_count; i++) {
		if (_nvk_pool[i].base && _nvk_modfree)
			_nvk_modfree(_nvk_pool[i].base);
		_nvk_pool[i].base = (void *)0;
		_nvk_pool[i].used = 0;
		_nvk_pool[i].refcnt = 0;
	}
	_nvk_pool_count = 0;
	_nvk_pool_alloc_total = 0;
	_nvk_pool_alloc_bytes = 0;
	_nvk_spin_unlock(&_nvk_pool_lock);
	_nvk_inited = 0;
}


/* --- kCFI-safe function pointer replacement --- */

static __always_inline u32 nvk_cfi_read_tag(void *func)
{
	u32 tag = 0;
	unsigned long addr = nvk_strip_pac((unsigned long)func);
	nvk_mem_read(&tag, (void *)(addr - 4), 4);
	return tag;
}

static __always_inline int nvk_cfi_has_tag(void *func)
{
	u32 tag = nvk_cfi_read_tag(func);
	return tag != 0 && tag != NVK_A64_NOP && tag != NVK_A64_BTI_C;
}

struct nvk_cfi_thunk {
	u32  tag;
	u32  code[8];
};

static int nvk_cfi_make_thunk(struct nvk_cfi_thunk *thunk,
			      void *orig_func, void *new_func)
{
	u32 tag;
	int n;

	if (!thunk || !orig_func || !new_func)
		return -1;

	tag = nvk_cfi_read_tag(orig_func);
	thunk->tag = tag;
	n = 0;
	thunk->code[n++] = NVK_A64_BTI_JC;
	n += nvk_a64_gen_mov64(&thunk->code[n], 17,
				(u64)(unsigned long)new_func);
	thunk->code[n++] = NVK_A64_RET_X17;
	return 0;
}

struct nvk_fptr_hook {
	void                *struct_addr;
	unsigned long        field_off;
	void                *orig_fn;
	struct nvk_cfi_thunk thunk;
	u32                 *thunk_page;
	int                  active;
};

static int nvk_fptr_replace(struct nvk_fptr_hook *h,
			    void *struct_addr, unsigned long field_off,
			    void *new_fn)
{
	void **slot;
	void *orig;

	if (!h || !struct_addr) return -1;
	if (!_nvk_inited) return NVK_HOOK_E_NOINIT;

	slot = (void **)((unsigned long)struct_addr + field_off);
	orig = *slot;
	if (!orig) return -2;

	h->struct_addr = struct_addr;
	h->field_off = field_off;
	h->orig_fn = orig;
	h->active = 0;

	if (nvk_cfi_has_tag(orig)) {
		nvk_cfi_make_thunk(&h->thunk, orig, new_fn);
		h->thunk_page = _nvk_pool_alloc(sizeof(h->thunk));
		if (!h->thunk_page) return NVK_HOOK_E_ALLOC;
		unsigned char *dst = (unsigned char *)h->thunk_page;
		unsigned char *src = (unsigned char *)&h->thunk;
		unsigned long i;
		for (i = 0; i < sizeof(h->thunk); i++)
			dst[i] = src[i];
		_nvk_dcache_clean((unsigned long)h->thunk_page,
				  (unsigned long)h->thunk_page + sizeof(h->thunk));
		_nvk_flushic((unsigned long)h->thunk_page,
			     (unsigned long)h->thunk_page + sizeof(h->thunk));
		void *entry = (void *)((unsigned long)h->thunk_page + 4);
		nvk_mem_write_protected((unsigned long)slot, &entry,
					sizeof(entry));
	} else {
		h->thunk_page = (void *)0;
		nvk_mem_write_protected((unsigned long)slot, &new_fn,
					sizeof(new_fn));
	}

	h->active = 1;
	return 0;
}

static void nvk_fptr_restore(struct nvk_fptr_hook *h)
{
	void **slot;
	if (!h || !h->active) return;

	slot = (void **)((unsigned long)h->struct_addr + h->field_off);
	nvk_mem_write_protected((unsigned long)slot, &h->orig_fn,
				sizeof(h->orig_fn));

	if (h->thunk_page)
		_nvk_pool_free(h->thunk_page);
	h->thunk_page = (void *)0;
	h->active = 0;
}


/* --- ftrace-based hook (fallback for unhookable functions) --- */

typedef int  (*nvk_ftrace_set_fn)(unsigned long ip, int enable);
typedef void (*nvk_ftrace_regs_fn)(unsigned long ip, unsigned long pip,
				   void *fregs, void *data);

struct nvk_ftrace_ops {
	unsigned long            func;
	unsigned long            flags;
	unsigned long            _pad[4];
};

typedef int (*nvk_register_ftrace_fn)(struct nvk_ftrace_ops *ops);
typedef int (*nvk_unregister_ftrace_fn)(struct nvk_ftrace_ops *ops);
typedef int (*nvk_ftrace_set_filter_ip_fn)(struct nvk_ftrace_ops *ops,
					   unsigned long ip,
					   int remove, int reset);

static nvk_register_ftrace_fn     _nvk_register_ftrace;
static nvk_unregister_ftrace_fn   _nvk_unregister_ftrace;
static nvk_ftrace_set_filter_ip_fn _nvk_ftrace_set_filter;
static nvk_ftrace_set_fn           _nvk_ftrace_set_ip;
static int _nvk_ftrace_avail;

#define NVK_FTRACE_FL_SAVE_REGS     0x0002UL
#define NVK_FTRACE_FL_SAVE_REGS_IF  0x0004UL
#define NVK_FTRACE_FL_RECURSION     0x0008UL
#define NVK_FTRACE_FL_IPMODIFY      0x0040UL

static int nvk_ftrace_init(void)
{
	if (_nvk_ftrace_avail) return 0;

	_nvk_register_ftrace =
		(nvk_register_ftrace_fn)NVK_LOOKUP("register_ftrace_function");
	_nvk_unregister_ftrace =
		(nvk_unregister_ftrace_fn)NVK_LOOKUP("unregister_ftrace_function");
	_nvk_ftrace_set_filter =
		(nvk_ftrace_set_filter_ip_fn)NVK_LOOKUP("ftrace_set_filter_ip");

	if (!_nvk_register_ftrace || !_nvk_unregister_ftrace ||
	    !_nvk_ftrace_set_filter)
		return -1;

	_nvk_ftrace_avail = 1;
	return 0;
}

struct nvk_ftrace_hook {
	void                   *target;
	void                   *replace;
	void                   *orig;
	struct nvk_ftrace_ops   ops;
	int                     active;
};

static void _nvk_ftrace_thunk(unsigned long ip, unsigned long parent_ip,
			      void *fregs, void *data)
{
	struct nvk_ftrace_hook *h = (struct nvk_ftrace_hook *)data;
	if (!h || !h->replace) return;

	unsigned long *regs_ip = (unsigned long *)fregs;
	if (regs_ip) {
		unsigned long i;
		for (i = 0; i < 32; i++) {
			if (regs_ip[i] == ip) {
				regs_ip[i] = (unsigned long)h->replace;
				return;
			}
		}
		regs_ip[30] = (unsigned long)h->replace;
	}
}

static int nvk_ftrace_hook_install(struct nvk_ftrace_hook *h,
				   void *target, void *replace,
				   void **orig)
{
	int ret;

	if (!_nvk_ftrace_avail) return -1;
	if (!target || !replace) return -2;

	h->target = target;
	h->replace = replace;
	h->orig = target;
	h->active = 0;

	unsigned char *p = (unsigned char *)&h->ops;
	unsigned long i;
	for (i = 0; i < sizeof(h->ops); i++) p[i] = 0;

	h->ops.func = (unsigned long)_nvk_ftrace_thunk;
	h->ops.flags = NVK_FTRACE_FL_SAVE_REGS | NVK_FTRACE_FL_IPMODIFY
		     | NVK_FTRACE_FL_RECURSION;

	ret = _nvk_ftrace_set_filter(&h->ops,
				     (unsigned long)target, 0, 1);
	if (ret) return ret;

	ret = _nvk_register_ftrace(&h->ops);
	if (ret) return ret;

	if (orig) *orig = h->orig;
	h->active = 1;
	return 0;
}

static void nvk_ftrace_hook_remove(struct nvk_ftrace_hook *h)
{
	if (!h || !h->active) return;
	if (_nvk_unregister_ftrace)
		_nvk_unregister_ftrace(&h->ops);
	h->active = 0;
}

static int nvk_hook_auto(struct nvk_hook *h, void *target,
			 void *replace, void **orig,
			 struct nvk_ftrace_hook *ft_fallback)
{
	enum nvk_scan_result scan = nvk_hook_scan(target);
	int ret;

	if (scan == NVK_SCAN_OK) {
		ret = nvk_hook_install(h, target, replace, orig);
		if (ret == NVK_HOOK_OK) return ret;
	}

	if (ft_fallback && _nvk_ftrace_avail) {
		ret = nvk_ftrace_hook_install(ft_fallback,
					      target, replace, orig);
		if (ret == 0) return ret;
	}

	if (scan == NVK_SCAN_OK)
		return nvk_hook_install(h, target, replace, orig);

	return NVK_HOOK_E_RELOC;
}

/* --- kprobe-based hook (lightweight fallback) --- */

typedef int (*nvk_kprobe_pre_fn)(void *kp, void *regs);

struct nvk_kprobe_hook {
	void *kp_storage[8];
	void *target;
	void *replace;
	void *orig;
	int   active;
};

typedef int (*nvk_register_kprobe_fn)(void *kp);
typedef void (*nvk_unregister_kprobe_fn)(void *kp);

static nvk_register_kprobe_fn   _nvk_reg_kprobe;
static nvk_unregister_kprobe_fn _nvk_unreg_kprobe;

static int nvk_kprobe_hook_init(void)
{
	if (_nvk_reg_kprobe) return 0;
	_nvk_reg_kprobe =
		(nvk_register_kprobe_fn)NVK_LOOKUP("register_kprobe");
	_nvk_unreg_kprobe =
		(nvk_unregister_kprobe_fn)NVK_LOOKUP("unregister_kprobe");
	return (_nvk_reg_kprobe && _nvk_unreg_kprobe) ? 0 : -1;
}

static void nvk_hook_auto_remove(struct nvk_hook *h,
				 struct nvk_ftrace_hook *ft_fallback)
{
	if (h && h->active)
		nvk_hook_remove(h);
	if (ft_fallback && ft_fallback->active)
		nvk_ftrace_hook_remove(ft_fallback);
}


static __always_inline u64 nvk_pool_alloc_count(void)
{ return __atomic_load_n(&_nvk_pool_alloc_total, __ATOMIC_RELAXED); }

static __always_inline u64 nvk_pool_alloc_bytes(void)
{ return __atomic_load_n(&_nvk_pool_alloc_bytes, __ATOMIC_RELAXED); }

static __always_inline int nvk_pool_page_count(void)
{ return _nvk_pool_count; }

static int nvk_pool_usage(int *total_used, int *total_cap)
{
	int i, used = 0, cap = 0;
	int pgsz = _nvk_pool_pgsz ? _nvk_pool_pgsz : 4096;
	_nvk_spin_lock(&_nvk_pool_lock);
	for (i = 0; i < _nvk_pool_count; i++) {
		used += _nvk_pool[i].used;
		cap += pgsz;
	}
	_nvk_spin_unlock(&_nvk_pool_lock);
	if (total_used) *total_used = used;
	if (total_cap)  *total_cap = cap;
	return _nvk_pool_count;
}


/* --- Hook chain: multiple handlers on the same target --- */

#define NVK_CHAIN_MAX 4

struct nvk_hook_chain_entry {
	void *handler;
	int   priority;
	int   active;
};

struct nvk_hook_chain {
	struct nvk_hook              hook;
	struct nvk_hook_chain_entry  entries[NVK_CHAIN_MAX];
	int                          count;
	void                        *orig_fn;
	void                        *dispatch_fn;
};

typedef long (*nvk_chain_handler_t)(void *orig, void *a0, void *a1,
				    void *a2, void *a3, void *a4, void *a5);

static long _nvk_chain_run(struct nvk_hook_chain *chain,
			   void *a0, void *a1, void *a2,
			   void *a3, void *a4, void *a5)
{
	int i;
	long ret = 0;
	if (!chain) return 0;
	for (i = 0; i < chain->count; i++) {
		if (!chain->entries[i].active) continue;
		nvk_chain_handler_t h =
			(nvk_chain_handler_t)chain->entries[i].handler;
		ret = h(chain->orig_fn, a0, a1, a2, a3, a4, a5);
	}
	return ret;
}

#define NVK_CHAIN_DISPATCH(name, chain_ptr)                                   \
	static long name(void *a0, void *a1, void *a2,                       \
			 void *a3, void *a4, void *a5)                       \
	{ return _nvk_chain_run(&(chain_ptr), a0, a1, a2, a3, a4, a5); }

static int nvk_chain_init(struct nvk_hook_chain *chain)
{
	if (!chain) return -1;
	unsigned char *p = (unsigned char *)chain;
	unsigned long sz = sizeof(*chain);
	unsigned long i;
	for (i = 0; i < sz; i++) p[i] = 0;
	return 0;
}

static int nvk_chain_add(struct nvk_hook_chain *chain,
			 void *handler, int priority)
{
	int i, slot = -1;

	if (!chain || !handler) return -1;
	if (chain->count >= NVK_CHAIN_MAX) return -2;

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
	chain->entries[slot].active = 1;
	chain->count++;
	return 0;
}

struct nvk_hook_stats {
	u64 total_installs;
	u64 total_removes;
	u64 pool_allocs;
	int active_hooks;
};

static void nvk_hook_get_stats(struct nvk_hook_stats *out)
{
	if (!out) return;
	out->total_installs = __atomic_load_n(&_nvk_hook_install_cnt,
					      __ATOMIC_RELAXED);
	out->total_removes  = __atomic_load_n(&_nvk_hook_remove_cnt,
					      __ATOMIC_RELAXED);
	out->pool_allocs = nvk_pool_alloc_count();
	out->active_hooks = (int)(out->total_installs - out->total_removes);
}

static int nvk_chain_remove(struct nvk_hook_chain *chain, void *handler)
{
	int i;
	if (!chain || !handler) return -1;

	for (i = 0; i < chain->count; i++) {
		if (chain->entries[i].handler == handler) {
			for (; i < chain->count - 1; i++)
				chain->entries[i] = chain->entries[i + 1];
			chain->count--;
			return 0;
		}
	}
	return -2;
}

static int nvk_chain_install(struct nvk_hook_chain *chain, void *target,
			     void *dispatch_fn)
{
	if (!chain || !target || !dispatch_fn) return -1;
	if (chain->hook.active) return -2;
	if (chain->count == 0) return -3;

	chain->dispatch_fn = dispatch_fn;
	return nvk_hook_install(&chain->hook, target,
				dispatch_fn, &chain->orig_fn);
}

static void nvk_chain_uninstall(struct nvk_hook_chain *chain)
{
	if (!chain) return;
	if (chain->hook.active)
		nvk_hook_remove(&chain->hook);
}

#endif /* NVK_HOOK_H */
