#include <stdio.h>

unsigned int translate_probe(unsigned int first, unsigned int second);
int translate_signed_boundary(unsigned int selector);

int main(void) {
  const unsigned int seed = 0x6e637431u;
  unsigned int state = seed;
  printf("seed=%u\n", seed);
  const unsigned int edges[] = {0u, 1u, 31u, 255u, 2147483647u,
                                2147483648u, 4294967295u};
  for (unsigned int i = 0; i < sizeof(edges) / sizeof(edges[0]); ++i) {
    unsigned int first = edges[i];
    unsigned int second = edges[6u - i];
    printf("%u %u %u\n", first, second, translate_probe(first, second));
  }
  for (unsigned int i = 0; i < 256u; ++i) {
    state = state * 1664525u + 1013904223u;
    unsigned int first = state;
    state = state * 1664525u + 1013904223u;
    unsigned int second = state;
    printf("%u %u %u\n", first, second, translate_probe(first, second));
  }
  for (unsigned int i = 0; i < 5u; ++i)
    printf("signed %u %d\n", i, translate_signed_boundary(i));
  return 0;
}
