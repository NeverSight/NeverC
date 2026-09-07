#include <stdio.h>
int sequencing_probe(void);
int deterministic_probe(void);
int main(void) {
  printf("%d %d\n", sequencing_probe(), deterministic_probe());
  return 0;
}
