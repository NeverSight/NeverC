#ifndef CSUPPORT_LFLOATING_LPOINT_LMODE_H
#define CSUPPORT_LFLOATING_LPOINT_LMODE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum csupport_rounding_mode {
  CSUPPORT_RM_TOWARD_ZERO = 0,
  CSUPPORT_RM_NEAREST_TIES_TO_EVEN = 1,
  CSUPPORT_RM_TOWARD_POSITIVE = 2,
  CSUPPORT_RM_TOWARD_NEGATIVE = 3,
  CSUPPORT_RM_NEAREST_TIES_TO_AWAY = 4
} csupport_rounding_mode_t;

uint32_t csupport_fneg_fpclass(uint32_t mask);
uint32_t csupport_inverse_fabs_fpclass(uint32_t mask);
uint32_t csupport_unknown_sign_fpclass(uint32_t mask);

#ifdef __cplusplus
}
#endif
#endif
