// Independent original-instruction oracle, linked with the NeverC object.
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define DECL(OP, BITS, TYPE)                                                   \
  unsigned OP##_reg##BITS(unsigned short, TYPE, unsigned char *);              \
  unsigned OP##_mem##BITS(const void *, TYPE, unsigned char *);
DECL(lar, 16, unsigned short)
DECL(lar, 32, unsigned)
DECL(lsl, 16, unsigned short)
DECL(lsl, 32, unsigned)
unsigned verr_reg(unsigned short), verr_mem(const void *);
unsigned verw_reg(unsigned short), verw_mem(const void *);

static unsigned checks;
#define REQUIRE(TEST)                                                          \
  do {                                                                         \
    if (!(TEST)) {                                                             \
      fprintf(stderr, "line %d: %s\n", __LINE__, #TEST);                       \
      exit(1);                                                                 \
    }                                                                          \
    ++checks;                                                                  \
  } while (0)

int main(void) {
  const long page = sysconf(_SC_PAGESIZE);
  unsigned char *memory = mmap(0, page * 2, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  REQUIRE(memory != MAP_FAILED);
  REQUIRE(mprotect(memory + page, page, PROT_NONE) == 0);
  unsigned short cs, ds, ss;
  __asm__ volatile("mov %%cs, %0" : "=r"(cs));
  __asm__ volatile("mov %%ds, %0" : "=r"(ds));
  __asm__ volatile("mov %%ss, %0" : "=r"(ss));
  const unsigned short selectors[] = {0,  1, 2, 3,      cs,    ds,
                                      ss, 4, 7, 0xfffb, 0xffff};
  const uint32_t priors[] = {0, 1, 0x80000001, 0x12345678, 0xffffffff};
  for (unsigned i = 0; i < sizeof(selectors) / sizeof(*selectors); ++i)
    for (unsigned j = 0; j < sizeof(priors) / sizeof(*priors); ++j)
      for (unsigned alignment = 0; alignment != 2; ++alignment) {
        // Both an unaligned word and the exact word preceding a guard page.
        void *source = memory + (alignment ? page - 2 : 1);
        memcpy(source, &selectors[i], 2);
#define CHECK_QUERY(OP, BITS, TYPE, SUFFIX, SELECTOR)                          \
  do {                                                                         \
    TYPE expected = priors[j];                                                 \
    unsigned char zf;                                                          \
    __asm__ volatile(#OP SUFFIX " " SELECTOR ", %0; sete %1"                   \
                     : "+r"(expected), "=qm"(zf)                               \
                     : "r"((unsigned)selectors[i])                             \
                     : "cc", "memory");                                        \
    unsigned char actual_zf = 0x5a;                                            \
    REQUIRE(OP##_reg##BITS(selectors[i], priors[j], &actual_zf) == expected);  \
    REQUIRE(actual_zf == zf);                                                  \
    actual_zf = 0x5a;                                                          \
    REQUIRE(OP##_mem##BITS(source, priors[j], &actual_zf) == expected);        \
    REQUIRE(actual_zf == zf);                                                  \
  } while (0)
        CHECK_QUERY(lar, 16, unsigned short, "w", "%w2");
        CHECK_QUERY(lar, 32, unsigned, "l", "%k2");
        CHECK_QUERY(lsl, 16, unsigned short, "w", "%w2");
        CHECK_QUERY(lsl, 32, unsigned, "l", "%k2");
#define CHECK_VERIFY(OP)                                                       \
  do {                                                                         \
    unsigned char expected;                                                    \
    __asm__ volatile(#OP " %1; sete %0"                                        \
                     : "=qm"(expected)                                         \
                     : "r"(selectors[i])                                       \
                     : "cc", "memory");                                        \
    REQUIRE(OP##_reg(selectors[i]) == expected);                               \
    REQUIRE(OP##_mem(source) == expected);                                     \
  } while (0)
        CHECK_VERIFY(verr);
        CHECK_VERIFY(verw);
      }
  REQUIRE(munmap(memory, page * 2) == 0);
  printf("{\"native_query_comparisons\":1320,\"native_assertions\":%u}\n",
         checks - 3);
  return 0;
}
