#include <neverc/xorstr/xorstr.h>

static int same(const char *lhs, const char *rhs) {
  while (*lhs && *rhs) {
    if (*lhs++ != *rhs++)
      return 0;
  }
  return *lhs == *rhs;
}

int main(void) {
  const char *empty = NC_XORSTR("");
  const char *short_name = NC_XORSTR("vsscanf");
  const char *long_name = NC_XORSTR("GetProcAddress");

  if (!same(empty, ""))
    return 1;
  if (!same(short_name, "vsscanf"))
    return 2;
  if (!same(long_name, "GetProcAddress"))
    return 3;
  return 0;
}
