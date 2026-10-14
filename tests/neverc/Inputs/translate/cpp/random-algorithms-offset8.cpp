// Expected permutations, samples and state come from LLVM 20.1.8 libc++.
#include <algorithm>
struct G {using result_type=unsigned char;unsigned state,calls;static constexpr result_type min(){return 5;}static constexpr result_type max(){return 11;}result_type operator()(){++calls;state=(state+5)%7;return static_cast<result_type>(state+5);}};
int main() {
  int values[17], output[8]{};
  for (int i = 0; i < 17; ++i) values[i] = i;
  G generator{17, 0};
  std::shuffle(values, values + 17, generator);
  unsigned long long digest = 0;
  for (int i = 0; i < 17; ++i)
    digest = digest * 257 + static_cast<unsigned>(values[i] + 1);
  if (digest != 886439578783348121ULL ||
      generator.state != 0ULL ||
      generator.calls != 75) return 1;
  auto end = std::sample(values, values + 17, output, 8, generator);
  int expected[8]{2, 4, 5, 6, 7, 0, 11, 12};
  for (int i = 0; i < 8; ++i)
    if (output[i] != expected[i]) return 2;
  if (end != output + 8 || generator.state != 0ULL ||
      generator.calls != 145) return 3;
  return 0;
}
