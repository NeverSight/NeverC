#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
  printf("Hello from NeverC on Linux!\n");
  printf("  argc  = %d\n", argc);
  printf("  argv0 = %s\n", argv[0]);

  const char *msg = "NeverC cross-compiled this binary";
  char buf[64];
  strncpy(buf, msg, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  size_t len = strlen(buf);
  for (size_t i = 0; i < len; ++i)
    buf[i] = (char)(buf[i] ^ 0x20);

  printf("  xor   = %s (len=%zu)\n", buf, len);
  printf("  atoi  = %d\n", atoi("2025"));

  return 0;
}
