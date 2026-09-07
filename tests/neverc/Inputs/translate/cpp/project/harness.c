#include <stdio.h>
unsigned int translate_project(unsigned int value);
int main(void) {
  unsigned int seed = 0x16c0ffeeu;
  for (unsigned int index = 0; index < 512u; ++index) {
    seed = seed * 1664525u + 1013904223u;
    printf("%u %u\n", seed, translate_project(seed));
  }
  return 0;
}
