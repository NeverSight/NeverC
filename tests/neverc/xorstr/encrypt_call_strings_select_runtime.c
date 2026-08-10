__attribute__((noinline)) static int consume(const char *value,
                                             const char *expected) {
  while (*value || *expected)
    if (*value++ != *expected++)
      return 1;
  return 0;
}

__attribute__((noinline)) static int check(int choose_alpha) {
  const char *expected =
      choose_alpha ? "select-secret-alpha" : "select-secret-omega";
  return consume(choose_alpha ? "select-secret-alpha" : "select-secret-omega",
                 expected);
}

int main(void) { return check(0) | check(1); }
