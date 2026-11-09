#ifndef __NMMINTRIN_H
#define __NMMINTRIN_H

#if !defined(__x86_64__)
#error "This header is only meant to be used on x86_64"
#endif

/* To match expectations of gcc we put the sse4.2 definitions into smmintrin.h,
   just include it now then.  */
#include <smmintrin.h>
#endif /* __NMMINTRIN_H */
