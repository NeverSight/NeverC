/* SPDX-License-Identifier: GPL-2.0 */
/* nvk_hook.h - NeverC arm64 inline-hook engine. */
#ifndef NVK_HOOK_H
#define NVK_HOOK_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <linux/string.h>


typedef struct {
	u64 regs[31];       /* X0 - X30                          */
	u64 _pad;           /* align to 256                       */
	u64 nzcv;           /* saved NZCV flags                   */
	u64 force_jump;     /* if nonzero, redirect execution     */
} nvk_reg_ctx;


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
{ return i == NVK_A64_PACIASP || i == NVK_A64_PACIBSP; }


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
		int is_32 = ((insn >> 30) & 3) == 0; /* opc=00 → 32-bit */
		n = nvk_a64_gen_mov64(out, 17, target);
		if (is_32)
			out[n++] = 0xB9400000U | (17 << 5) | rt; /* LDR Wt, [X17] */
		else
			out[n++] = 0xF9400000U | (17 << 5) | rt; /* LDR Xt, [X17] */
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
	NVK_HOOK_E_ALREADY  = -6,
};

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
};

typedef void *(*nvk_modalloc_fn)(unsigned long);
typedef void  (*nvk_modfree_fn)(void *);
typedef void  (*nvk_flushic_fn)(unsigned long, unsigned long);
typedef int   (*nvk_patchtext_fn)(void **, u32 *, int);
typedef int   (*nvk_patchtns_fn)(void *, u32);

static nvk_modalloc_fn  _nvk_modalloc;
static nvk_modfree_fn   _nvk_modfree;
static nvk_flushic_fn   _nvk_flushic;
static nvk_patchtext_fn _nvk_patchtext;     /* stop_machine version */
static nvk_patchtns_fn  _nvk_patchtns;      /* nosync version */
static int _nvk_inited;

typedef int (*nvk_ksize_fn)(unsigned long addr, unsigned long *sz,
			    unsigned long *off);
static nvk_ksize_fn _nvk_ksize;

static int nvk_hook_init(void)
{
	if (_nvk_inited) return 0;
	_nvk_modalloc = (nvk_modalloc_fn)NVK_LOOKUP("module_alloc");
	_nvk_modfree  = (nvk_modfree_fn)NVK_LOOKUP("module_memfree");
	if (!_nvk_modfree)
		_nvk_modfree = (nvk_modfree_fn)NVK_LOOKUP("vfree");
	_nvk_flushic = (nvk_flushic_fn)NVK_LOOKUP("flush_icache_range");
	if (!_nvk_flushic)
		_nvk_flushic = (nvk_flushic_fn)NVK_LOOKUP("__flush_icache_range");
	_nvk_patchtext = (nvk_patchtext_fn)NVK_LOOKUP("aarch64_insn_patch_text");
	_nvk_patchtns  = (nvk_patchtns_fn)NVK_LOOKUP("aarch64_insn_patch_text_nosync");
	_nvk_ksize = (nvk_ksize_fn)NVK_LOOKUP("kallsyms_lookup_size_offset");
	if (!_nvk_modalloc || !_nvk_modfree || !_nvk_flushic)
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

static __always_inline int nvk_a64_is_hook_patch(u32 insn)
{
	if (insn == NVK_A64_BRK_KPROBE) return 1;
	if (insn == 0x58000050U) return 1;  /* LDR X16, [PC+8] */
	return 0;
}

static void _nvk_write_insn(void *addr, u32 insn)
{
	if (_nvk_patchtns)
		_nvk_patchtns(addr, insn);
	else {
		*(volatile u32 *)addr = insn;
		_nvk_flushic((unsigned long)addr, (unsigned long)addr + 4);
	}
}

static int _nvk_patch_multi(u32 *target, u32 *insns, int count)
{
	int i;

	if (_nvk_patchtext && count <= 16) {
		void *addrs[16];
		for (i = 0; i < count; i++)
			addrs[i] = &target[i];
		return _nvk_patchtext(addrs, insns, count);
	}

	for (i = 0; i < count; i++)
		_nvk_write_insn(&target[i], insns[i]);
	return 0;
}


static void _nvk_scan_entry(u32 *code, int *skip, int *total)
{
	int s = 0;
	if (nvk_a64_is_bti(code[s]))  s++;
	if (nvk_a64_is_pac(code[s]))  s++;
	*total = s + 4; /* prefix + LDR + BR + .quad(2) */
	if (*total > NVK_HOOK_MAX_PATCH)
		*total = NVK_HOOK_MAX_PATCH;
	if (*total < 4)
		*total = 4;
	*skip = s;
}


static int nvk_hook_install(struct nvk_hook *h, void *target,
			    void *replace, void **orig)
{
	u32 *code = (u32 *)target;
	u32  tramp[NVK_HOOK_TRAMP_CAP];
	int  tidx = 0, skip, patch_count, i;
	u32  patch[NVK_HOOK_MAX_PATCH];

	if (!_nvk_inited) return NVK_HOOK_E_NOINIT;
	h->target = target;
	h->replace = replace;
	h->trampoline = (void *)0;
	h->active = 0;

	if (nvk_a64_is_hook_patch(code[0]))
		return NVK_HOOK_E_ALREADY;

	{
		unsigned long fn_sz = _nvk_fn_size(target);
		if (fn_sz > 0 && fn_sz < NVK_HOOK_MAX_PATCH * 4)
			return NVK_HOOK_E_SHORT;
	}

	_nvk_scan_entry(code, &skip, &patch_count);
	h->patch_count = patch_count;
	for (i = 0; i < patch_count; i++)
		h->orig_insns[i] = code[i];

	tramp[tidx++] = NVK_A64_BTI_JC;

	for (i = 0; i < patch_count; i++) {
		u32 insn = code[i];
		if (nvk_a64_is_bti(insn) || insn == NVK_A64_NOP)
			continue;
		unsigned long insn_pc = (unsigned long)&code[i];
		int n = nvk_a64_relocate_abs(insn, insn_pc, &tramp[tidx]);
		if (n == 0) return NVK_HOOK_E_RELOC;
		tidx += n;
		if (tidx >= NVK_HOOK_TRAMP_CAP - 8) return NVK_HOOK_E_RELOC;
	}

	unsigned long back = (unsigned long)target + patch_count * 4;
	tidx += nvk_a64_gen_mov64(&tramp[tidx], 17, back);
	tramp[tidx++] = NVK_A64_RET_X17;

	h->trampoline = (u32 *)_nvk_modalloc(4096);
	if (!h->trampoline) return NVK_HOOK_E_ALLOC;

	for (i = 0; i < tidx; i++)
		h->trampoline[i] = tramp[i];
	_nvk_flushic((unsigned long)h->trampoline,
		     (unsigned long)&h->trampoline[tidx]);

	*orig = (void *)h->trampoline;

	for (i = 0; i < patch_count; i++)
		patch[i] = NVK_A64_NOP;
	{
		int jmp = 0;
		if (nvk_a64_is_bti(code[0])) {
			patch[0] = code[0]; /* keep BTI landing pad */
			jmp = 1;
		}
		patch[jmp + 0] = 0x58000050U;  /* LDR X16, [PC, #8] */
		patch[jmp + 1] = 0xD61F0200U;  /* BR  X16 */
		*(unsigned long *)&patch[jmp + 2] = (unsigned long)replace;
	}

	if (_nvk_patch_multi(code, patch, patch_count) != 0) {
		_nvk_modfree(h->trampoline);
		h->trampoline = (void *)0;
		return NVK_HOOK_E_PATCH;
	}

	h->active = 1;
	return NVK_HOOK_OK;
}


#define _CTX_SIZE  272
#define _CTX_NZCV  256
#define _CTX_FORCE 264

/*
 * ARM64 instruction encoding macros.
 * Every instruction in the context stub template is generated via these macros,
 * eliminating any chance of hand-encoding errors.
 */
#define _A64E_SUB_SP_I(imm)  (0xD10003FFU | ((u32)(imm) << 10))
#define _A64E_ADD_SP_I(imm)  (0x910003FFU | ((u32)(imm) << 10))
#define _A64E_STP_SP(t1, t2, off) \
	(0xA9000000U | ((u32)((off)/8) << 15) | \
	 ((u32)(t2) << 10) | (31U << 5) | (u32)(t1))
#define _A64E_LDP_SP(t1, t2, off) \
	(0xA9400000U | ((u32)((off)/8) << 15) | \
	 ((u32)(t2) << 10) | (31U << 5) | (u32)(t1))
#define _A64E_STR_SP(t, off) \
	(0xF9000000U | ((u32)((off)/8) << 10) | (31U << 5) | (u32)(t))
#define _A64E_LDR_SP(t, off) \
	(0xF9400000U | ((u32)((off)/8) << 10) | (31U << 5) | (u32)(t))
#define _A64E_MRS_NZCV(t)   (0xD53B4200U | (u32)(t))
#define _A64E_MSR_NZCV(t)   (0xD51B4200U | (u32)(t))
#define _A64E_MOV_FROM_SP(d) (0x910003E0U | (u32)(d))
#define _A64E_MOV_REG(d, n)  (0xAA0003E0U | ((u32)(n) << 16) | (u32)(d))
#define _A64E_CBNZ_FWD(t, off) \
	(0xB5000000U | (((u32)(off) & 0x7FFFFU) << 5) | (u32)(t))
#define _A64E_MOVZ(rd, hw)   (0xD2800000U | ((u32)(hw) << 21) | (u32)(rd))
#define _A64E_MOVK16(rd)     (0xF2A00000U | (u32)(rd))
#define _A64E_MOVK32(rd)     (0xF2C00000U | (u32)(rd))
#define _A64E_MOVK48(rd)     (0xF2E00000U | (u32)(rd))

/*
 * Context stub: saves all 31 GPRs + NZCV, calls handler(nvk_reg_ctx*),
 * checks force_jump, restores regs and enters trampoline (or force target).
 *
 * Layout on stack (_CTX_SIZE = 272, 16-byte aligned):
 *   [SP+0..247]  regs[31] (X0-X30)  +  _pad
 *   [SP+256]     nzcv
 *   [SP+264]     force_jump
 */
static const u32 _nvk_ctx_stub_template[] = {
	/* ---- save all registers ---- */
	/*  0 */ NVK_A64_BTI_JC,
	/*  1 */ _A64E_SUB_SP_I(_CTX_SIZE),
	/*  2 */ _A64E_STP_SP( 0,  1,   0),
	/*  3 */ _A64E_STP_SP( 2,  3,  16),
	/*  4 */ _A64E_STP_SP( 4,  5,  32),
	/*  5 */ _A64E_STP_SP( 6,  7,  48),
	/*  6 */ _A64E_STP_SP( 8,  9,  64),
	/*  7 */ _A64E_STP_SP(10, 11,  80),
	/*  8 */ _A64E_STP_SP(12, 13,  96),
	/*  9 */ _A64E_STP_SP(14, 15, 112),
	/* 10 */ _A64E_STP_SP(16, 17, 128),
	/* 11 */ _A64E_STP_SP(18, 19, 144),
	/* 12 */ _A64E_STP_SP(20, 21, 160),
	/* 13 */ _A64E_STP_SP(22, 23, 176),
	/* 14 */ _A64E_STP_SP(24, 25, 192),
	/* 15 */ _A64E_STP_SP(26, 27, 208),
	/* 16 */ _A64E_STP_SP(28, 29, 224),
	/* 17 */ _A64E_STP_SP(30, 31, 240),   /* X30 + XZR padding */
	/* 18 */ _A64E_MRS_NZCV(1),
	/* 19 */ _A64E_STR_SP(1,  _CTX_NZCV),
	/* 20 */ _A64E_STR_SP(31, _CTX_FORCE), /* force_jump = 0 */
	/* ---- call handler(ctx) ---- */
	/* 21 */ _A64E_MOV_FROM_SP(0),         /* X0 = SP (ctx pointer) */
	/* 22 */ _A64E_MOVZ(3, 0),             /* handler address (patched) */
	/* 23 */ _A64E_MOVK16(3),
	/* 24 */ _A64E_MOVK32(3),
	/* 25 */ _A64E_MOVK48(3),
	/* 26 */ 0xD63F0060U,                  /* BLR X3 */
	/* ---- check force_jump ---- */
	/* 27 */ _A64E_LDR_SP(1, _CTX_FORCE),
	/* 28 */ _A64E_CBNZ_FWD(1, 24),        /* if nonzero → slot 52 */
	/* ---- normal restore path ---- */
	/* 29 */ _A64E_LDR_SP(2, _CTX_NZCV),
	/* 30 */ _A64E_MSR_NZCV(2),
	/* 31 */ _A64E_LDP_SP( 2,  3,  16),
	/* 32 */ _A64E_LDP_SP( 4,  5,  32),
	/* 33 */ _A64E_LDP_SP( 6,  7,  48),
	/* 34 */ _A64E_LDP_SP( 8,  9,  64),
	/* 35 */ _A64E_LDP_SP(10, 11,  80),
	/* 36 */ _A64E_LDP_SP(12, 13,  96),
	/* 37 */ _A64E_LDP_SP(14, 15, 112),
	/* 38 */ _A64E_LDP_SP(16, 17, 128),
	/* 39 */ _A64E_LDP_SP(18, 19, 144),
	/* 40 */ _A64E_LDP_SP(20, 21, 160),
	/* 41 */ _A64E_LDP_SP(22, 23, 176),
	/* 42 */ _A64E_LDP_SP(24, 25, 192),
	/* 43 */ _A64E_LDP_SP(26, 27, 208),
	/* 44 */ _A64E_LDP_SP(28, 29, 224),
	/* 45 */ _A64E_LDP_SP(30, 31, 240),
	/* 46 */ _A64E_LDP_SP( 0,  1,   0),   /* restore X0, X1 last */
	/* 47 */ _A64E_ADD_SP_I(_CTX_SIZE),
	/* 48 */ _A64E_MOVZ(17, 0),            /* trampoline addr (patched) */
	/* 49 */ _A64E_MOVK16(17),
	/* 50 */ _A64E_MOVK32(17),
	/* 51 */ NVK_A64_RET_X17,
	/* ---- force jump path (target of CBNZ at slot 28) ---- */
	/* 52 */ _A64E_MOV_REG(17, 1),         /* X17 = force_jump addr */
	/* 53 */ _A64E_LDR_SP(2, _CTX_NZCV),
	/* 54 */ _A64E_MSR_NZCV(2),
	/* 55 */ _A64E_LDP_SP( 2,  3,  16),
	/* 56 */ _A64E_LDP_SP( 4,  5,  32),
	/* 57 */ _A64E_LDP_SP( 6,  7,  48),
	/* 58 */ _A64E_LDP_SP( 8,  9,  64),
	/* 59 */ _A64E_LDP_SP(10, 11,  80),
	/* 60 */ _A64E_LDP_SP(12, 13,  96),
	/* 61 */ _A64E_LDP_SP(14, 15, 112),
	/* 62 */ _A64E_LDP_SP(18, 19, 144),   /* skip X16/X17 (scratch) */
	/* 63 */ _A64E_LDP_SP(20, 21, 160),
	/* 64 */ _A64E_LDP_SP(22, 23, 176),
	/* 65 */ _A64E_LDP_SP(24, 25, 192),
	/* 66 */ _A64E_LDP_SP(26, 27, 208),
	/* 67 */ _A64E_LDP_SP(28, 29, 224),
	/* 68 */ _A64E_LDR_SP(30, 240),        /* X30 only */
	/* 69 */ _A64E_LDP_SP( 0,  1,   0),
	/* 70 */ _A64E_ADD_SP_I(_CTX_SIZE),
	/* 71 */ NVK_A64_RET_X17,
};

#define _CTX_STUB_LEN     (sizeof(_nvk_ctx_stub_template) / sizeof(u32))
#define _CTX_HANDLER_SLOT 22
#define _CTX_TRAMP_SLOT   48

_Static_assert(_CTX_STUB_LEN == 72, "context stub size mismatch");
_Static_assert(_CTX_SIZE % 16 == 0, "context frame must be 16-byte aligned");
_Static_assert(_CTX_HANDLER_SLOT + 4 < _CTX_TRAMP_SLOT,
	       "handler slots must not overlap trampoline slots");

typedef void (*nvk_ctx_handler_t)(nvk_reg_ctx *ctx);

struct nvk_hook_ctx {
	struct nvk_hook     base;
	u32                *stub;          /* context-save stub code */
	u32                *tramp_code;    /* trampoline (after stub in same page) */
};

static int nvk_hook_install_ctx(struct nvk_hook_ctx *h, void *target,
				nvk_ctx_handler_t handler, void **call_orig)
{
	u32 *code = (u32 *)target;
	u32  tramp[NVK_HOOK_TRAMP_CAP];
	int  tidx = 0, skip, patch_count, i;
	u32  patch[NVK_HOOK_MAX_PATCH];
	u32  mov_buf[4];
	int  n;
	u32 *page;

	if (!_nvk_inited) return NVK_HOOK_E_NOINIT;
	h->base.target = target;
	h->base.replace = (void *)handler;
	h->base.trampoline = (void *)0;
	h->base.active = 0;
	h->stub = (void *)0;

	if (nvk_a64_is_hook_patch(code[0]))
		return NVK_HOOK_E_ALREADY;

	{
		unsigned long fn_sz = _nvk_fn_size(target);
		if (fn_sz > 0 && fn_sz < NVK_HOOK_MAX_PATCH * 4)
			return NVK_HOOK_E_SHORT;
	}

	_nvk_scan_entry(code, &skip, &patch_count);
	h->base.patch_count = patch_count;
	for (i = 0; i < patch_count; i++)
		h->base.orig_insns[i] = code[i];

	tramp[tidx++] = NVK_A64_BTI_JC;
	for (i = 0; i < patch_count; i++) {
		u32 insn = code[i];
		if (nvk_a64_is_bti(insn) || insn == NVK_A64_NOP)
			continue;
		n = nvk_a64_relocate_abs(insn, (unsigned long)&code[i],
					 &tramp[tidx]);
		if (n == 0) return NVK_HOOK_E_RELOC;
		tidx += n;
		if (tidx >= NVK_HOOK_TRAMP_CAP - 8) return NVK_HOOK_E_RELOC;
	}
	unsigned long back = (unsigned long)target + patch_count * 4;
	tidx += nvk_a64_gen_mov64(&tramp[tidx], 17, back);
	tramp[tidx++] = NVK_A64_RET_X17;

	page = (u32 *)_nvk_modalloc(4096);
	if (!page) return NVK_HOOK_E_ALLOC;

	h->stub = page;
	h->tramp_code = page + _CTX_STUB_LEN + 16;
	h->base.trampoline = page;

	for (i = 0; i < (int)_CTX_STUB_LEN; i++)
		page[i] = _nvk_ctx_stub_template[i];

	/* Patch handler address into X3 load sequence (slots 22-25). */
	n = nvk_a64_gen_mov64(mov_buf, 3, (u64)(unsigned long)handler);
	for (i = 0; i < n && i < 4; i++)
		page[_CTX_HANDLER_SLOT + i] = mov_buf[i];

	/* Patch trampoline address into X17 load sequence (slots 48-50). */
	n = nvk_a64_gen_mov64(mov_buf, 17,
			      (u64)(unsigned long)h->tramp_code);
	for (i = 0; i < n && i < 3; i++)
		page[_CTX_TRAMP_SLOT + i] = mov_buf[i];

	for (i = 0; i < tidx; i++)
		h->tramp_code[i] = tramp[i];

	_nvk_flushic((unsigned long)page, (unsigned long)page + 4096);

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
	return NVK_HOOK_OK;
}


static void nvk_hook_remove(struct nvk_hook *h)
{
	int i;
	if (!h->active) return;

	u32 *code = (u32 *)h->target;
	_nvk_patch_multi(code, h->orig_insns, h->patch_count);

	if (h->trampoline) {
		_nvk_modfree(h->trampoline);
		h->trampoline = (void *)0;
	}
	h->active = 0;
}

static void nvk_hook_remove_ctx(struct nvk_hook_ctx *h)
{
	if (!h->base.active) return;

	u32 *code = (u32 *)h->base.target;
	_nvk_patch_multi(code, h->base.orig_insns, h->base.patch_count);

	if (h->stub) {
		_nvk_modfree(h->stub);
		h->stub = (void *)0;
		h->tramp_code = (void *)0;
		h->base.trampoline = (void *)0;
	}
	h->base.active = 0;
}

#endif /* NVK_HOOK_H */
