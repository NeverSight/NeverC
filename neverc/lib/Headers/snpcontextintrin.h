/*===---- snpcontextintrin.h - Native SNP register completion -------------===
 * SPDX-License-Identifier: AGPL-3.0-only
 *===----------------------------------------------------------------------===*/
#ifndef __SNPCONTEXTINTRIN_H
#define __SNPCONTEXTINTRIN_H

#if !defined(__x86_64__) && !defined(_M_X64)
#error "SNP context requires x86-64"
#endif

/* These are real privileged instructions, with ambient SNP, paging, RMP and
 * segment state. The context and compiler frame must be accessible and disjoint
 * from all memory affected by the instruction. Normal completion updates only
 * architectural outputs; faults and partial progress remain in the CPU state.
 * RMPCHKD publishes actual values for its undefined arithmetic flags.
 * The raw builtins take (&RAX, &RFLAGS, control): bit 0 selects PVALIDATE's
 * address32 form and bits 15:8 optionally hold one segment-prefix byte.
 */
struct __snp_context {
  unsigned long long RAX, RCX, RDX, RFLAGS;
};

static __inline__ void __attribute__((__always_inline__, __nodebug__))
__snp_psmash(struct __snp_context *__context) {
  __builtin_ia32_psmash_context(&__context->RAX, &__context->RFLAGS, 0);
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
__snp_pvalidate(struct __snp_context *__context) {
  __builtin_ia32_pvalidate_context(&__context->RAX, &__context->RFLAGS, 0);
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
__snp_rmpadjust(struct __snp_context *__context) {
  __builtin_ia32_rmpadjust_context(&__context->RAX, &__context->RFLAGS, 0);
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
__snp_rmpupdate(struct __snp_context *__context) {
  __builtin_ia32_rmpupdate_context(&__context->RAX, &__context->RFLAGS, 0);
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
__snp_rmpquery(struct __snp_context *__context) {
  __builtin_ia32_rmpquery_context(&__context->RAX, &__context->RFLAGS, 0);
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
__snp_rmpread(struct __snp_context *__context) {
  __builtin_ia32_rmpread_context(&__context->RAX, &__context->RFLAGS, 0);
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
__snp_rmpopt(struct __snp_context *__context) {
  __builtin_ia32_rmpopt_context(&__context->RAX, &__context->RFLAGS, 0);
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
__snp_rmpchkd(struct __snp_context *__context) {
  __builtin_ia32_rmpchkd_context(&__context->RAX, &__context->RFLAGS, 0);
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
__snp_pvalidate_addr32(struct __snp_context *__context) {
  __builtin_ia32_pvalidate_context(&__context->RAX, &__context->RFLAGS, 1);
}

/* SEV-ES/GHCB returning service contract. Private CPU state and the compiler
 * environment survive the exit; shared-memory/MSR communication stays ambient.
 * RAX carries the optional VMGEXIT parameter. Non-SEV-ES VMMCALL execution or
 * services that replace caller state require a different contract.
 */
static __inline__ void __attribute__((__always_inline__, __nodebug__))
__sev_vmgexit(unsigned long long __parameter) {
  __builtin_ia32_vmgexit_sev(__parameter);
}

#endif /* __SNPCONTEXTINTRIN_H */
