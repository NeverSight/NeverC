#ifndef TRANSLATE_PROJECT_COMPUTE_HPP
#define TRANSLATE_PROJECT_COMPUTE_HPP
namespace calculation {
struct Pair { int first; unsigned int second; };
unsigned int mix(unsigned int value);
int magnitude(int value);
inline unsigned int combine(Pair value) {
  return mix((unsigned int)value.first) ^ value.second;
}
}
extern "C" unsigned int translate_project(unsigned int value);
#endif
