static const char *saved;

__attribute__((noinline)) static void remember(const char *value) {
  saved = value;
}

__attribute__((noinline)) static int verify(const char *base,
                                            const char *interior) {
  return saved == base && saved + 3 == interior && base[0] == 'p' &&
         interior[0] == 'n';
}

int main(void) {
  const char *value = "pointer-identity-secret";
  remember(value);
  return verify(value, value + 3) ? 0 : 1;
}
