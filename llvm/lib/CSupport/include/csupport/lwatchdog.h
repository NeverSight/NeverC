#ifndef CSUPPORT_LWATCHDOG_H
#define CSUPPORT_LWATCHDOG_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

void csupport_watchdog_start(unsigned int seconds);
void csupport_watchdog_stop(void);

#ifdef __cplusplus
}
#endif
#endif
