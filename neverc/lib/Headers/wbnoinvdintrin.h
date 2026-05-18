#if !defined __X86INTRIN_H && !defined __IMMINTRIN_H
#error "Never use <wbnoinvdintrin.h> directly; include <x86intrin.h> instead."
#endif

#ifndef __WBNOINVDINTRIN_H
#define __WBNOINVDINTRIN_H

static __inline__ void
    __attribute__((__always_inline__, __nodebug__, __target__("wbnoinvd")))
    _wbnoinvd(void) {
  __builtin_ia32_wbnoinvd();
}

#endif /* __WBNOINVDINTRIN_H */
