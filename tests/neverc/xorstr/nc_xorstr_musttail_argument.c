#include <neverc/xorstr/xorstr.h>

__attribute__((noinline)) static int consume(const char *value) {
  return value[0] == 'm' ? 0 : 1;
}

__attribute__((noinline)) static int forward(const char *unused) {
  (void)unused;
  [[clang::musttail]] return consume(NC_XORSTR("musttail-argument"));
}

int main(void) { return forward((const char *)0); }
