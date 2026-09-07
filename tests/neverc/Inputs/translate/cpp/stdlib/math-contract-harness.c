#include <errno.h>
#include <fenv.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

double reference_abs(double), reference_floor(double);
double translate_math_abs(double), translate_math_floor(double);

static const uint64_t edges[] = {
    0, 0x8000000000000000ull, 1, 0x8000000000000001ull,
    0x3ff8000000000000ull, 0xbff8000000000000ull,
    0x3ff0000000000000ull, 0xbff0000000000000ull,
    0x432fffffffffffffull, 0xc32fffffffffffffull,
    0x4330000000000000ull, 0xc330000000000000ull,
    0x4340000000000000ull, 0xc340000000000000ull,
    0x7ff0000000000000ull, 0xfff0000000000000ull,
    0x7ff8000000001234ull, 0xfff8000000001234ull,
    0x7ff0000000001234ull, 0xfff0000000001234ull,
    0x7fefffffffffffffull, 0xffefffffffffffffull};

int main(void) {
  const int modes[] = {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
  double (*reference[])(double) = {reference_abs, reference_floor};
  double (*generated[])(double) = {translate_math_abs, translate_math_floor};
  unsigned int failures = 0, cases = 0;
  for (unsigned int mode = 0; mode < 4; ++mode) {
    if (fesetround(modes[mode])) return 2;
    uint64_t seed = 0x16c0ffee12345678ull;
    for (unsigned int index = 0; index < 4096 + sizeof(edges) / sizeof(*edges); ++index) {
      seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;
      uint64_t input_bits = index < sizeof(edges) / sizeof(*edges) ? edges[index] : seed;
      double input;
      memcpy(&input, &input_bits, sizeof(input));
      for (unsigned int operation = 0; operation < 2; ++operation) {
        uint64_t result_bits[2];
        int errors[2], flags[2], rounding[2];
        for (unsigned int implementation = 0; implementation < 2; ++implementation) {
          feclearexcept(FE_ALL_EXCEPT);
          if (index & 1u) feraiseexcept(FE_DIVBYZERO);
          errno = 123;
          double result = implementation ? generated[operation](input) : reference[operation](input);
          errors[implementation] = errno;
          flags[implementation] = fetestexcept(FE_ALL_EXCEPT);
          rounding[implementation] = fegetround();
          memcpy(&result_bits[implementation], &result, sizeof(result));
        }
        ++cases;
        if (result_bits[0] != result_bits[1] || errors[0] != errors[1] ||
            flags[0] != flags[1] || rounding[0] != modes[mode] || rounding[1] != modes[mode]) {
          ++failures;
          if (failures <= 32)
            printf("mode=%u op=%u index=%u seed=%016llx input=%016llx "
                   "reference=%016llx errno=%d flags=%x generated=%016llx errno=%d flags=%x\n",
                   mode, operation, index, (unsigned long long)seed, (unsigned long long)input_bits,
                   (unsigned long long)result_bits[0], errors[0], flags[0],
                   (unsigned long long)result_bits[1], errors[1], flags[1]);
        }
      }
    }
  }
  printf("math-contract cases=%u failures=%u seed=16c0ffee12345678\n", cases, failures);
  return failures ? 1 : 0;
}
