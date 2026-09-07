#include "compute.hpp"
namespace calculation {
static unsigned int private_step(unsigned int value) { return value + 3u; }
unsigned int mix(unsigned int value) { return private_step(value) * 5u; }
int magnitude(int value) { return value < 0 ? -value : value; }
}
