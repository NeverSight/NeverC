/*===- FloatingPointMode.c - FP class manipulation (pure C) -----*- C -*-===*/
#include "include/csupport/lfloating_lpoint_lmode.h"
#include <stdint.h>

uint32_t csupport_fneg_fpclass(uint32_t mask) {
  uint32_t nan_bits = mask & 0x3;
  uint32_t result = nan_bits;
  if (mask & 0x004) result |= 0x200;  /* negInf → posInf */
  if (mask & 0x008) result |= 0x100;  /* negNormal → posNormal */
  if (mask & 0x010) result |= 0x080;  /* negSubnormal → posSubnormal */
  if (mask & 0x020) result |= 0x040;  /* negZero → posZero */
  if (mask & 0x040) result |= 0x020;  /* posZero → negZero */
  if (mask & 0x080) result |= 0x010;  /* posSubnormal → negSubnormal */
  if (mask & 0x100) result |= 0x008;  /* posNormal → negNormal */
  if (mask & 0x200) result |= 0x004;  /* posInf → negInf */
  return result;
}

uint32_t csupport_inverse_fabs_fpclass(uint32_t mask) {
  uint32_t result = mask & 0x3;
  if (mask & 0x040) result |= 0x060;  /* posZero → zero */
  if (mask & 0x080) result |= 0x090;  /* posSub → sub */
  if (mask & 0x100) result |= 0x108;  /* posNorm → norm */
  if (mask & 0x200) result |= 0x204;  /* posInf → inf */
  return result;
}

uint32_t csupport_unknown_sign_fpclass(uint32_t mask) {
  uint32_t result = mask & 0x3;             /* nan bits unchanged */
  if (mask & 0x060) result |= 0x060;       /* any zero → both zeros */
  if (mask & 0x090) result |= 0x090;       /* any sub → both subs */
  if (mask & 0x108) result |= 0x108;       /* any norm → both norms */
  if (mask & 0x204) result |= 0x204;       /* any inf → both infs */
  return result;
}
