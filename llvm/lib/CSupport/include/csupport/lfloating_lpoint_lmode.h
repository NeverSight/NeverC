#ifndef CSUPPORT_LFLOATING_LPOINT_LMODE_H
#define CSUPPORT_LFLOATING_LPOINT_LMODE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

uint32_t csupport_fneg_fpclass(uint32_t mask);
uint32_t csupport_inverse_fabs_fpclass(uint32_t mask);
uint32_t csupport_unknown_sign_fpclass(uint32_t mask);

#ifdef __cplusplus
}
#endif
#endif
