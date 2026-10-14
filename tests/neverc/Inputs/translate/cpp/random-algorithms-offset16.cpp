// Expected permutations, samples and state come from LLVM 20.1.8 libc++.
#include <algorithm>
struct G {using result_type=unsigned short;unsigned state,calls;static constexpr result_type min(){return 1000;}static constexpr result_type max(){return 1011;}result_type operator()(){++calls;state=(state+5)%12;return static_cast<result_type>(state+1000);}};
int main() {
  int values[17], output[8]{};
  for (int i = 0; i < 17; ++i) values[i] = i;
  G generator{17, 0};
  std::shuffle(values, values + 17, generator);
  unsigned long long digest = 0;
  for (int i = 0; i < 17; ++i)
    digest = digest * 257 + static_cast<unsigned>(values[i] + 1);
  if (digest != 3646850025663735961ULL ||
      generator.state != 5ULL ||
      generator.calls != 36) return 1;
  auto end = std::sample(values, values + 17, output, 8, generator);
  int expected[8]{1, 3, 5, 7, 8, 9, 11, 6};
  for (int i = 0; i < 8; ++i)
    if (output[i] != expected[i]) return 2;
  if (end != output + 8 || generator.state != 0ULL ||
      generator.calls != 71) return 3;
  return 0;
}
