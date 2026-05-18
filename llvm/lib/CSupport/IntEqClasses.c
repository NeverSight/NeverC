/*===- IntEqClasses.c - Union-Find algorithm (pure C) -----------*- C -*-===*/
#include "include/csupport/lint_leq_lclasses.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

unsigned csupport_uf_join(unsigned *ec, unsigned a, unsigned b) {
  unsigned eca = ec[a];
  unsigned ecb = ec[b];
  while (eca != ecb) {
    if (eca < ecb) {
      ec[b] = eca;
      b = ecb;
      ecb = ec[b];
    } else {
      ec[a] = ecb;
      a = eca;
      eca = ec[a];
    }
  }
  return eca;
}

unsigned csupport_uf_find_leader(const unsigned *ec, unsigned a) {
  while (a != ec[a])
    a = ec[a];
  return a;
}

unsigned csupport_uf_compress(unsigned *ec, unsigned size) {
  unsigned num_classes = 0;
  for (unsigned i = 0; i < size; ++i)
    ec[i] = (ec[i] == i) ? num_classes++ : ec[ec[i]];
  return num_classes;
}
