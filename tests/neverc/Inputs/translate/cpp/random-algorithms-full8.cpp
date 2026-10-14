// Expected permutations, samples and state come from LLVM 20.1.8 libc++.
#include <algorithm>
struct G {using result_type=unsigned char;unsigned state,calls;static constexpr result_type min(){return 0;}static constexpr result_type max(){return 255;}result_type operator()(){++calls;state=(state*13+17)%256;return static_cast<result_type>(state);}};
int main() {
  int values[260], output[8]{};
  for (int i = 0; i < 260; ++i) values[i] = i;
  G generator{17, 0};
  std::shuffle(values, values + 260, generator);
  unsigned long long digest = 0;
  for (int i = 0; i < 260; ++i)
    digest = digest * 257 + static_cast<unsigned>(values[i] + 1);
  if (digest != 4849504684369707658ULL ||
      generator.state != 166ULL ||
      generator.calls != 361) return 1;
  auto end = std::sample(values, values + 260, output, 8, generator);
  int expected[8]{129, 157, 68, 74, 183, 234, 190, 62};
  for (int i = 0; i < 8; ++i)
    if (output[i] != expected[i]) return 2;
  if (end != output + 8 || generator.state != 224ULL ||
      generator.calls != 671) return 3;
  return 0;
}
