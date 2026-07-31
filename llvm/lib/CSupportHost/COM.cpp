//===-- COM.cpp - Windows COM initialization (C ABI) ----------------------===//
#include "csupport/lcom.h"

#ifdef _WIN32

#include <objbase.h>

int csupport_com_initialize(csupport_com_threading_mode_t threading_mode,
                            int speed_over_memory) {
  DWORD Mode = threading_mode == CSUPPORT_COM_SINGLE_THREADED
                   ? COINIT_APARTMENTTHREADED
                   : COINIT_MULTITHREADED;
  if (speed_over_memory)
    Mode |= COINIT_SPEED_OVER_MEMORY;
  return SUCCEEDED(::CoInitializeEx(nullptr, Mode));
}

void csupport_com_uninitialize(void) { ::CoUninitialize(); }

#else

int csupport_com_initialize(csupport_com_threading_mode_t threading_mode,
                            int speed_over_memory) {
  (void)threading_mode;
  (void)speed_over_memory;
  return 1;
}

void csupport_com_uninitialize(void) {}

#endif
