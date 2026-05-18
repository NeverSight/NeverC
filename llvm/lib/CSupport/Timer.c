/*===- Timer.c - Timing infrastructure (pure C) -----------------*- C -*-===*/
#include "include/csupport/ltimer.h"
#include <stdint.h>
#include <stdio.h>

int csupport_format_timer_print_val(char *buf, size_t buflen,
                                    double val, double total) {
  if (buflen == 0) return 0;
  if (total < 1e-7)
    return snprintf(buf, buflen, "        -----     ");
  else
    return snprintf(buf, buflen, "  %7.4f (%5.1f%%)", val, val * 100.0 / total);
}

uint64_t csupport_get_instructions_executed(void) {
  return 0;
}
