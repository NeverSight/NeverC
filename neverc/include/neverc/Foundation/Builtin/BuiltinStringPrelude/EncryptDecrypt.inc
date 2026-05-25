R"prelude(
#ifndef NEVERC_STRING_ENCRYPT_BYTE
#define NEVERC_STRING_ENCRYPT_BYTE(byte, key, idx) \
  ((char)((unsigned char)(byte) ^ \
          (unsigned char)((key) >> (8 * ((idx) % sizeof(__SIZE_TYPE__))))))
#endif

static __attribute__((always_inline)) char
__neverc_xfree_dec(unsigned char a, unsigned char b) {
  volatile __SIZE_TYPE__ va = a;
  volatile __SIZE_TYPE__ vb = b;
  return (char)((unsigned char)(va + vb - ((va & vb) + (vb & va))));
}

#ifndef NEVERC_STRING_DECRYPT_BYTE
#define NEVERC_STRING_DECRYPT_BYTE(byte, key, idx) \
  __neverc_xfree_dec((unsigned char)(byte), \
    (unsigned char)((key) >> (8 * ((idx) % sizeof(__SIZE_TYPE__)))))
#endif

static __attribute__((always_inline)) string
__neverc_string_decrypt_literal(const char *enc, __SIZE_TYPE__ len,
                                __SIZE_TYPE__ key) {
  if (len == 0)
    return __neverc_string_empty();
  char *p = (char *)NEVERC_STRING_ALLOC(len + 1);
  if (!p)
    return __neverc_string_empty();
  volatile __SIZE_TYPE__ vkey = key;
  for (__SIZE_TYPE__ i = 0; i < len; i++)
    p[i] = NEVERC_STRING_DECRYPT_BYTE(enc[i], vkey, i);
  p[len] = 0;
  return (string){p, len, len + 1};
}

)prelude"
