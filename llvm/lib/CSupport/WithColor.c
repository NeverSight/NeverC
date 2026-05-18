/*===- WithColor.c - Colored output (pure C) --------------------*- C -*-===*/
#include "include/csupport/lwith_lcolor.h"
#include "include/csupport/ostream.h"

static const char *ansi_color_codes[] = {
    "\033[30m", "\033[31m", "\033[32m", "\033[33m",
    "\033[34m", "\033[35m", "\033[36m", "\033[37m",
    "\033[90m", "\033[91m", "\033[92m", "\033[93m",
    "\033[94m", "\033[95m", "\033[96m", "\033[97m"
};

void csupport_set_color(csupport_ostream_t *os, int color_idx, int bold) {
  if (color_idx < 0 || color_idx >= 16) return;
  if (bold)
    csupport_os_write_str(os, "\033[1m");
  csupport_os_write_str(os, ansi_color_codes[color_idx]);
}

void csupport_reset_color(csupport_ostream_t *os) {
  csupport_os_write_str(os, "\033[0m");
}
