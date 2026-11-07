// Native descriptor-query builtins; this source contains no assembly.
#define QUERY(OP, BITS, TYPE, SOURCE, ARG)                                     \
  unsigned OP##_##SOURCE##BITS(ARG input, TYPE prior, unsigned char *zf) {     \
    return __builtin_ia32_##OP##_##SOURCE##BITS(input, prior, zf);             \
  }
#define QUERIES(OP, BITS, TYPE)                                                \
  QUERY(OP, BITS, TYPE, reg, unsigned short)                                   \
  QUERY(OP, BITS, TYPE, mem, const void *)
QUERIES(lar, 16, unsigned short)
QUERIES(lar, 32, unsigned)
QUERIES(lsl, 16, unsigned short)
QUERIES(lsl, 32, unsigned)

unsigned verr_reg(unsigned short selector) {
  return __builtin_ia32_verr_reg(selector);
}
unsigned verr_mem(const void *selector) {
  return __builtin_ia32_verr_mem(selector);
}
unsigned verw_reg(unsigned short selector) {
  return __builtin_ia32_verw_reg(selector);
}
unsigned verw_mem(const void *selector) {
  return __builtin_ia32_verw_mem(selector);
}
