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


#define NVK_HOOK_MAX_PATCH   4   /* 16 bytes at target */
#define NVK_HOOK_TRAMP_CAP  64   /* max trampoline instructions */
#define NVK_HOOK_STUB_CAP  128   /* max context-stub instructions */

struct nvk_hook {
	void       *target;
	void       *replace;
	u32        *trampoline;          /* module_alloc'd code page */
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
	/* Always patch 4 instructions (16 bytes = LDR+BR+.quad) */
	*total = (s + 3 > NVK_HOOK_MAX_PATCH) ? NVK_HOOK_MAX_PATCH : s + 3;
	if (*total < 4) *total = 4;
	*skip = s;
}


static int nvk_hook_install(struct nvk_hook *h, void *target,
			    void *replace, void **orig)
{
	u32 *code = (u32 *)target;
	u32  tramp[NVK_HOOK_TRAMP_CAP];
	int  tidx = 0, skip, patch_count, i;
	u32  patch[NVK_HOOK_MAX_PATCH];

	if (!_nvk_inited) return -1;
	h->target = target;
	h->replace = replace;
	h->trampoline = (void *)0;
	h->active = 0;

	/* Short function guard. */
	{
		unsigned long fn_sz = _nvk_fn_size(target);
		if (fn_sz > 0 && fn_sz < NVK_HOOK_MAX_PATCH * 4)
			return -2; /* function too short */
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
		if (n == 0) goto fail;
		tidx += n;
		if (tidx >= NVK_HOOK_TRAMP_CAP - 8) goto fail;
	}

	/* Jump back to target + patch_count*4 using RET X17. */
	unsigned long back = (unsigned long)target + patch_count * 4;
	tidx += nvk_a64_gen_mov64(&tramp[tidx], 17, back);
	tramp[tidx++] = NVK_A64_RET_X17;

	/* Allocate and fill trampoline. */
	h->trampoline = (u32 *)_nvk_modalloc(4096);
	if (!h->trampoline) return -1;

	for (i = 0; i < tidx; i++)
		h->trampoline[i] = tramp[i];
	_nvk_flushic((unsigned long)h->trampoline,
		     (unsigned long)&h->trampoline[tidx]);

	*orig = (void *)h->trampoline;

	patch[0] = 0x58000050U;  /* LDR X16, [PC, #8] */
	patch[1] = 0xD61F0200U;  /* BR  X16 */
	*(unsigned long *)&patch[2] = (unsigned long)replace;

	if (_nvk_patch_multi(code, patch, patch_count) != 0)
		goto fail;

	h->active = 1;
	return 0;

fail:
	if (h->trampoline) {
		_nvk_modfree(h->trampoline);
		h->trampoline = (void *)0;
	}
	return -1;
}


#define _CTX_SIZE  272   /* 31*8 + 8(pad) + 8(nzcv) + 8(force_jump) */
#define _CTX_NZCV  256
#define _CTX_FORCE 264

static const u32 _nvk_ctx_stub_template[] = {
	/* [0]  */ NVK_A64_BTI_JC,
	/* [1]  */ 0xD1044400,  /* SUB SP, SP, #272 */
	/* [2]  */ 0xA9000FE0,  /* STP X0, X1, [SP, #0] */
	/* [3]  */ 0xA9010FE2,  /* STP X2, X3, [SP, #16] */
	/* [4]  */ 0xA9020FE4,  /* STP X4, X5, [SP, #32] */
	/* [5]  */ 0xA9030FE6,  /* STP X6, X7, [SP, #48] */
	/* [6]  */ 0xA9040FE8,  /* STP X8, X9, [SP, #64] */
	/* [7]  */ 0xA9050FEA,  /* STP X10, X11, [SP, #80] */
	/* [8]  */ 0xA9060FEC,  /* STP X12, X13, [SP, #96] */
	/* [9]  */ 0xA9070FEE,  /* STP X14, X15, [SP, #112] */
	/* [10] */ 0xA9080FF0,  /* STP X16, X17, [SP, #128] */
	/* [11] */ 0xA9090FF2,  /* STP X18, X19, [SP, #144] */
	/* [12] */ 0xA90A0FF4,  /* STP X20, X21, [SP, #160] */
	/* [13] */ 0xA90B0FF6,  /* STP X22, X23, [SP, #176] */
	/* [14] */ 0xA90C0FF8,  /* STP X24, X25, [SP, #192] */
	/* [15] */ 0xA90D0FFA,  /* STP X26, X27, [SP, #208] */
	/* [16] */ 0xA90E0FFC,  /* STP X28, X29, [SP, #224] */
	/* [17] */ 0xA90F7FFE,  /* STP X30, XZR, [SP, #240] */
	/* [18] */ 0xD53B4201,  /* MRS X1, NZCV */
	/* [19] */ 0xA9100FE1,  /* STP X1, XZR, [SP, #256] */
	/* [21] */ 0xD2800003,  /* MOVZ X3, #0 (patched) */
	/* [22] */ 0xF2A00003,  /* MOVK X3, #0, LSL#16 (patched) */
	/* [23] */ 0xF2C00003,  /* MOVK X3, #0, LSL#32 (patched) */
	/* [24] */ 0xF2E00003,  /* MOVK X3, #0, LSL#48 (patched) */
	/* [25] */ 0xD63F0060,  /* BLR X3 */
	/* ---- normal restore path ---- */
	/* [28] */ 0xF94080E1,  /* LDR X1, [SP, #256] (nzcv) */
	/* [29] */ 0xD51B4201,  /* MSR NZCV, X1 */
	/* [30] */ 0xA9410FE2,  /* LDP X2, X3, [SP, #16] */
	/* [31] */ 0xA9420FE4,  /* LDP X4, X5, [SP, #32] */
	/* [32] */ 0xA9430FE6,  /* LDP X6, X7, [SP, #48] */
	/* [33] */ 0xA9440FE8,  /* LDP X8, X9, [SP, #64] */
	/* [34] */ 0xA9450FEA,  /* LDP X10, X11, [SP, #80] */
	/* [35] */ 0xA9460FEC,  /* LDP X12, X13, [SP, #96] */
	/* [36] */ 0xA9470FEE,  /* LDP X14, X15, [SP, #112] */
	/* [37] */ 0xA9480FF0,  /* LDP X16, X17, [SP, #128] */
	/* [38] */ 0xA9490FF2,  /* LDP X18, X19, [SP, #144] */
	/* [39] */ 0xA94A0FF4,  /* LDP X20, X21, [SP, #160] */
	/* [40] */ 0xA94B0FF6,  /* LDP X22, X23, [SP, #176] */
	/* [41] */ 0xA94C0FF8,  /* LDP X24, X25, [SP, #192] */
	/* [42] */ 0xA94D0FFA,  /* LDP X26, X27, [SP, #208] */
	/* [43] */ 0xA94E0FFC,  /* LDP X28, X29, [SP, #224] */
	/* [44] */ 0xA94F7FFE,  /* LDP X30, XZR, [SP, #240] */
	/* [45] */ 0xA9400FE0,  /* LDP X0, X1, [SP, #0] */
	/* [46] */ 0x91044400,  /* ADD SP, SP, #272 */
	/* -- PATCH: jump to trampoline (slots 47-50) -- */
	/* [47] */ 0xD2800011,  /* MOVZ X17, #0 (patched) */
	/* [48] */ 0xF2A00011,  /* MOVK X17, #0, LSL#16 (patched) */
	/* [49] */ 0xF2C00011,  /* MOVK X17, #0, LSL#32 (patched) */
	/* [50] */ NVK_A64_RET_X17,
	/* ---- force jump path ---- */
	/* [51] */ NVK_A64_BTI_JC,
	/* [53] */ 0xF94080E1,  /* LDR X1, [SP, #256] (nzcv) */
	/* [54] */ 0xD51B4201,  /* MSR NZCV, X1 */
	/* [55] */ 0xA9410FE2,  /* LDP X2, X3, [SP, #16] */
	/* [56] */ 0xA9420FE4,  /* LDP X4, X5, [SP, #32] */
	/* [57] */ 0xA9430FE6,  /* LDP X6, X7, [SP, #48] */
	/* [58] */ 0xA9440FE8,  /* LDP X8, X9, [SP, #64] */
	/* [59] */ 0xA9450FEA,  /* LDP X10, X11, [SP, #80] */
	/* [60] */ 0xA9460FEC,  /* LDP X12, X13, [SP, #96] */
	/* [61] */ 0xA9470FEE,  /* LDP X14, X15, [SP, #112] */
	/* [63] */ 0xA94A0FF4,  /* LDP X20, X21, [SP, #160] */
	/* [64] */ 0xA94B0FF6,  /* LDP X22, X23, [SP, #176] */
	/* [65] */ 0xA94C0FF8,  /* LDP X24, X25, [SP, #192] */
	/* [66] */ 0xA94D0FFA,  /* LDP X26, X27, [SP, #208] */
	/* [67] */ 0xA94E0FFC,  /* LDP X28, X29, [SP, #224] */
	/* [68] */ 0xF9407BFE,  /* LDR X30, [SP, #240] */
	/* [69] */ 0xA9400FE0,  /* LDP X0, X1, [SP, #0] */
	/* [70] */ 0x91044400,  /* ADD SP, SP, #272 */
};

#define _CTX_STUB_LEN (sizeof(_nvk_ctx_stub_template) / sizeof(u32))
#define _CTX_HANDLER_SLOT 21  /* first MOVZ for handler address */
#define _CTX_TRAMP_SLOT   47  /* first MOVZ for trampoline address */
#define _CTX_CBNZ_SLOT    27  /* CBNZ force_jump check */

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

	if (!_nvk_inited) return -1;
	h->base.target = target;
	h->base.replace = (void *)handler;
	h->base.trampoline = (void *)0;
	h->base.active = 0;
	h->stub = (void *)0;

	{
		unsigned long fn_sz = _nvk_fn_size(target);
		if (fn_sz > 0 && fn_sz < NVK_HOOK_MAX_PATCH * 4)
			return -2;
	}
	h->stub = (void *)0;

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
		if (n == 0) return -1;
		tidx += n;
		if (tidx >= NVK_HOOK_TRAMP_CAP - 8) return -1;
	}
	unsigned long back = (unsigned long)target + patch_count * 4;
	tidx += nvk_a64_gen_mov64(&tramp[tidx], 17, back);
	tramp[tidx++] = NVK_A64_RET_X17;

	page = (u32 *)_nvk_modalloc(4096);
	if (!page) return -1;

	h->stub = page;
	h->tramp_code = page + _CTX_STUB_LEN + 16;
	h->base.trampoline = page;

	/* Copy stub template. */
	for (i = 0; i < (int)_CTX_STUB_LEN; i++)
		page[i] = _nvk_ctx_stub_template[i];

	/* Patch handler address (X3, slots 21-24). */
	n = nvk_a64_gen_mov64(mov_buf, 3, (u64)(unsigned long)handler);
	for (i = 0; i < n && i < 4; i++)
		page[_CTX_HANDLER_SLOT + i] = mov_buf[i];

	/* Patch trampoline address (X17, slots 47-49). */
	n = nvk_a64_gen_mov64(mov_buf, 17,
			      (u64)(unsigned long)h->tramp_code);
	for (i = 0; i < n && i < 3; i++)
		page[_CTX_TRAMP_SLOT + i] = mov_buf[i];

	int cbnz_offset = 51 - _CTX_CBNZ_SLOT;
	page[_CTX_CBNZ_SLOT] = 0xB5000001U |
				(((u32)cbnz_offset & 0x7FFFF) << 5);

	/* Copy trampoline after stub. */
	for (i = 0; i < tidx; i++)
		h->tramp_code[i] = tramp[i];

	_nvk_flushic((unsigned long)page, (unsigned long)page + 4096);

	if (call_orig)
		*call_orig = (void *)h->tramp_code;

	/* 16-byte atomic patch at target. */
	patch[0] = 0x58000050U;
	patch[1] = 0xD61F0200U;
	*(unsigned long *)&patch[2] = (unsigned long)h->stub;

	if (_nvk_patch_multi(code, patch, patch_count) != 0) {
		_nvk_modfree(page);
		h->stub = (void *)0;
		return -1;
	}

	h->base.active = 1;
	return 0;
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
