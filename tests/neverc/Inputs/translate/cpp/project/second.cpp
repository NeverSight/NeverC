#include "compute.hpp"
static unsigned int private_step(unsigned int value) { return value ^ 165u; }
extern "C" unsigned int translate_project(unsigned int value) {
  calculation::Pair pair{calculation::magnitude((int)(value % 1000u) - 500),
                         private_step(value)};
  return calculation::combine(pair);
}
