#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void print_sysinfo(void) {
  printf("Static binary info:\n");
  printf("  sizeof(void*)  = %zu\n", sizeof(void *));
  printf("  sizeof(long)   = %zu\n", sizeof(long));
  printf("  sizeof(size_t) = %zu\n", sizeof(size_t));

#if defined(__x86_64__)
  printf("  arch           = x86_64\n");
#elif defined(__aarch64__)
  printf("  arch           = aarch64\n");
#else
  printf("  arch           = unknown\n");
#endif
}

static void demo_math(void) {
  printf("\nMath functions:\n");

  double values[] = {0.0, 1.0, 2.0, 3.14159265, 100.0};
  for (int i = 0; i < 5; ++i) {
    double v = values[i];
    printf("  sqrt(%.2f) = %.6f, sin(%.2f) = %.6f\n",
           v, sqrt(v), v, sin(v));
  }

  printf("  pow(2, 10) = %.0f\n", pow(2.0, 10.0));
  printf("  log(1000)  = %.6f\n", log(1000.0));
  printf("  exp(1)     = %.6f\n", exp(1.0));
}

static void demo_string(void) {
  printf("\nString operations:\n");

  char buf[128];
  snprintf(buf, sizeof(buf), "Hello from %s static binary", "NeverC");
  printf("  snprintf: %s\n", buf);

  char *dup = strdup(buf);
  if (dup) {
    printf("  strdup:   %s (len=%zu)\n", dup, strlen(dup));
    free(dup);
  }

  char sorted[] = "neverc";
  size_t len = strlen(sorted);
  for (size_t i = 0; i < len; ++i)
    for (size_t j = i + 1; j < len; ++j)
      if (sorted[i] > sorted[j]) {
        char tmp = sorted[i];
        sorted[i] = sorted[j];
        sorted[j] = tmp;
      }
  printf("  sorted:   %s\n", sorted);
}

static void demo_memory(void) {
  printf("\nDynamic memory:\n");

  size_t total = 0;
  for (int i = 0; i < 10; ++i) {
    size_t sz = (size_t)(1 << (i + 4));
    void *p = malloc(sz);
    if (p) {
      memset(p, 0xCC, sz);
      total += sz;
      free(p);
    }
  }
  printf("  allocated+freed %zu bytes across 10 sizes\n", total);
}

int main(void) {
  printf("NeverC fully-static Linux binary\n");
  printf("================================\n");

  print_sysinfo();
  demo_math();
  demo_string();
  demo_memory();

  printf("\nDone.\n");
  return 0;
}
