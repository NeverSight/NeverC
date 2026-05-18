#ifndef CSUPPORT_LINT_LEQ_LCLASSES_H
#define CSUPPORT_LINT_LEQ_LCLASSES_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

unsigned csupport_uf_join(unsigned *ec, unsigned a, unsigned b);
unsigned csupport_uf_find_leader(const unsigned *ec, unsigned a);
unsigned csupport_uf_compress(unsigned *ec, unsigned size);

#ifdef __cplusplus
}
#endif
#endif
