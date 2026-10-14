// Expected permutations, samples and state come from LLVM 20.1.8 libc++.
#include <algorithm>
struct G {using result_type=unsigned;unsigned state,calls;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
int main() {
  int values[17], output[8]{};
  for (int i = 0; i < 17; ++i) values[i] = i;
  G generator{17, 0};
  std::shuffle(values, values + 17, generator);
  unsigned long long digest = 0;
  for (int i = 0; i < 17; ++i)
    digest = digest * 257 + static_cast<unsigned>(values[i] + 1);
  if (digest != 18293149044577076889ULL ||
      generator.state != 3092164965ULL ||
      generator.calls != 20) return 1;
  auto end = std::sample(values, values + 17, output, 8, generator);
  int expected[8]{11, 7, 6, 14, 8, 2, 16, 9};
  for (int i = 0; i < 8; ++i)
    if (output[i] != expected[i]) return 2;
  if (end != output + 8 || generator.state != 735758288ULL ||
      generator.calls != 37) return 3;
  return 0;
}
