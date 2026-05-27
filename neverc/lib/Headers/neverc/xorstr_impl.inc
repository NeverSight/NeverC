/*===---- neverc/xorstr_impl.inc - XOR string decrypt implementation ------===*\
 *
 * Stack-allocated XOR decrypt helper for NC_XORSTR().
 * Included by <neverc/xorstr.h> -- do not include directly.
 *
 * The decrypt byte operation avoids the XOR instruction to reduce
 * signature visibility: a ^ b == a + b - 2*(a & b).
 *
\*===----------------------------------------------------------------------===*/

#ifndef _NEVERC_XORSTR_IMPL_INC_
#define _NEVERC_XORSTR_IMPL_INC_

static __attribute__((always_inline)) unsigned char
__neverc_xorstr_dec_byte(unsigned char a, unsigned char b) {
  volatile __SIZE_TYPE__ va = a;
  volatile __SIZE_TYPE__ vb = b;
  return (unsigned char)(va + vb - ((va & vb) + (vb & va)));
}

static __attribute__((always_inline)) const char *
__neverc_xorstr_decrypt(const char *enc, __SIZE_TYPE__ len,
                        __SIZE_TYPE__ key) {
  char *buf = (char *)__builtin_alloca(len + 1);
  volatile __SIZE_TYPE__ vkey = key;
  for (__SIZE_TYPE__ i = 0; i < len; i++) {
    unsigned char kb =
        (unsigned char)(vkey >> (8 * (i % sizeof(__SIZE_TYPE__))));
    buf[i] = (char)__neverc_xorstr_dec_byte((unsigned char)enc[i], kb);
  }
  buf[len] = 0;
  return buf;
}

#endif /* _NEVERC_XORSTR_IMPL_INC_ */
