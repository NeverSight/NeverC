__attribute__((noinline)) int xorstr_lto_consume(const char *value) {
  static const char expected[] = {
      'l', 't', 'o', '-', 'l', 'a', 't', 'e', '-', 's', 'e', 'c',
      'r', 'e', 't', '-', 'a', 'l', 'p', 'h', 'a', 0,
  };
  for (unsigned i = 0; i != sizeof(expected); ++i)
    if (value[i] != expected[i])
      return 1;
  return 0;
}
