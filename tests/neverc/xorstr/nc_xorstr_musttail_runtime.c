#include <neverc/xorstr/xorstr.h>

__attribute__((noinline)) static int finish(int value) { return value; }

__attribute__((noinline)) static int use_secret_then_tail_call(int value) {
  const char *secret = NC_XORSTR("musttail-secret");
  if (secret[0] == 'm')
    ++value;
  [[clang::musttail]] return finish(value);
}

int main(void) { return use_secret_then_tail_call(0) == 1 ? 0 : 1; }
