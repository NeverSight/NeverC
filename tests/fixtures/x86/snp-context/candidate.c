/* SPDX-License-Identifier: AGPL-3.0-only */
#include <snpcontextintrin.h>

typedef unsigned long long u64;

/* Inputs live across the intrinsic exercise both integer and vector allocation.
 */
u64 candidate(unsigned form, void *g, void *flags, u64 seed, const double *fp,
              double *result) {
  double a = fp[0], b = fp[1], c = fp[2], d = fp[3], e = fp[4], f = fp[5];
  u64 v0 = seed + 7, v1 = seed * 13, v2 = seed ^ 0xfedcba9876543210ull;
  u64 v3 = seed + 0x123456789abcdef0ull;
#ifdef SNP_SOURCE
  neverd_x64_state_v6 state = {0};
  const unsigned offsets[] = {0, 4, 8, 12, 16, 20, 24, 28, 32, 37, 41};
  if (form >= 11)
    __builtin_trap();
  for (unsigned i = 0; i < 16; ++i)
    state.GPR[i] = 0x8877665544332211ull + seed * 13 + i * 7;
  for (unsigned i = 0; i < sizeof(state.ZMM); ++i)
    ((unsigned char *)state.ZMM)[i] = seed + i * 13;
  __builtin_memcpy(state.GPR, g, 24);
  __builtin_memcpy(&state.RFlags, flags, 8);
  state.PC = 0x1000 + offsets[form];
  state.MXCSR = 0x1f80;
  state.CS = (neverd_x64_segment){0, 0xffffffff, 8, 0xa09b};
  state.SS = (neverd_x64_segment){0, 0xffffffff, 16, 0xc093};
  neverd_x64_state_v6 unchanged = state;
  uint64_t budget = 1;
  if (neverd_x64_continue_v6(&state, &budget) != 9 || budget)
    __builtin_trap();
  unchanged.PC += form == 8 ? 5 : 4;
  for (unsigned i = 0; i < 3; ++i)
    unchanged.GPR[i] = state.GPR[i];
  unchanged.RFlags = state.RFlags;
  if (__builtin_memcmp(&unchanged, &state, sizeof(state)))
    __builtin_trap();
  __builtin_memcpy(g, state.GPR, 24);
  __builtin_memcpy(flags, &state.RFlags, 8);
#else
  switch (form) {
  case 0:
    __builtin_ia32_psmash_context(g, flags, 0);
    break;
  case 1:
    __builtin_ia32_pvalidate_context(g, flags, 0);
    break;
  case 2:
    __builtin_ia32_rmpadjust_context(g, flags, 0);
    break;
  case 3:
    __builtin_ia32_rmpupdate_context(g, flags, 0);
    break;
  case 4:
    __builtin_ia32_rmpquery_context(g, flags, 0);
    break;
  case 5:
    __builtin_ia32_rmpread_context(g, flags, 0);
    break;
  case 6:
    __builtin_ia32_rmpopt_context(g, flags, 0);
    break;
  case 7:
    __builtin_ia32_rmpchkd_context(g, flags, 0);
    break;
  case 8:
    __builtin_ia32_pvalidate_context(g, flags, 1);
    break;
  case 9:
  case 10: {
    u64 a;
    __builtin_memcpy(&a, g, 8);
    __sev_vmgexit(a);
    break;
  }
  case 11:
    __builtin_ia32_rmpread_context(g, flags, 0x6500);
    break;
  default:
    __builtin_trap();
  }
#endif
  result[0] = a + b;
  result[1] = c - d;
  result[2] = e * f;
  return v0 ^ v1 ^ v2 ^ v3;
}
