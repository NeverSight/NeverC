// NeverC C++ ABI v1 — pure virtual call handler
#include <stdlib.h>
extern "C" void __cxa_pure_virtual(void) { abort(); }
extern "C" void __cxa_deleted_virtual(void) { abort(); }
