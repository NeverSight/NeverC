#include <cmath>
extern "C" double translate_math_abs(double value) {
  return std::fabs(value);
}
extern "C" double translate_math_floor(double value) {
  return std::floor(value);
}
