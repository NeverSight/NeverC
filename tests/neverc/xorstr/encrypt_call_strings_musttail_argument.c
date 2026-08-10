__attribute__((noinline)) static int consume(const char *value) {
  return value[0] == 'a' ? 0 : 1;
}

__attribute__((noinline)) static int forward(const char *unused) {
  (void)unused;
  [[clang::musttail]] return consume("auto-musttail-secret");
}

int main(void) { return forward((const char *)0); }
