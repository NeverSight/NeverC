#include <neverc/xorstr/xorstr.h>

static const unsigned char ciphertext[] = {
    0xa5, 0x3f, 0x89, 0x7e, 0x10, 0x67, 0x38,
    0x8e, 0xc5, 0x7d, 0x5e, 0xc1, 0x15, 0x16,
};

int main(void) {
  char output[14] = {0};
  const char *decoded = __neverc_xorstr_decrypt(
      (const char *)ciphertext + 1,
      (__SIZE_TYPE__)0x21012545290125c8ULL,
      (__SIZE_TYPE__)0x123456789abcdef1ULL, output);
  static const char expected[] = "offset-secret";
  for (unsigned i = 0; i != sizeof(expected); ++i)
    if (decoded[i] != expected[i])
      return 1;
  return 0;
}
