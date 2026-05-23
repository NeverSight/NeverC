//===-- COM.cpp - Windows COM initialization (C ABI) ----------------------===//
#ifdef _WIN32

#include <objbase.h>

extern "C" void csupport_com_initialize(int threading_mode,
                                        int speed_over_memory) {
  DWORD Mode = threading_mode == 0 ? COINIT_APARTMENTTHREADED : COINIT_MULTITHREADED;
  if (speed_over_memory)
    Mode |= COINIT_SPEED_OVER_MEMORY;
  ::CoInitializeEx(nullptr, Mode);
}

extern "C" void csupport_com_uninitialize(void) { ::CoUninitialize(); }

#endif
