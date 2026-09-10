// Compile a foundational LLVM header without any pre-included dependencies.
// In particular, fatal-error message recovery must not pull high-level Support
// headers into an ADT header while its types are still being defined.
#ifndef NEVERC_SUPPORT_HEADER
#define NEVERC_SUPPORT_HEADER "llvm/ADT/ArrayRef.h"
#endif

#include NEVERC_SUPPORT_HEADER
