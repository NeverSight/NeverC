#ifndef __WMMINTRIN_H
#define __WMMINTRIN_H

#if !defined(__i386__) && !defined(__x86_64__)
#error "This header is only meant to be used on x86 targets"
#endif

#include <emmintrin.h>

#include <__wmmintrin_aes.h>

#include <__wmmintrin_pclmul.h>

#endif /* __WMMINTRIN_H */
