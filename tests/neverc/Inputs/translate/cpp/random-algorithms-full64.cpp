// Expected permutations, samples and state come from LLVM 20.1.8 libc++.
#include <algorithm>
struct G {using result_type=unsigned long long;unsigned long long state;unsigned calls;static constexpr result_type min(){return 0;}static constexpr result_type max(){return ~0ULL;}result_type operator()(){++calls;state=state*6364136223846793005ULL+1442695040888963407ULL;return state;}};
int main() {
  int values[17], output[8]{};
  for (int i = 0; i < 17; ++i) values[i] = i;
  G generator{17, 0};
  std::shuffle(values, values + 17, generator);
  unsigned long long digest = 0;
  for (int i = 0; i < 17; ++i)
    digest = digest * 257 + static_cast<unsigned>(values[i] + 1);
  if (digest != 6932542905646576793ULL ||
      generator.state != 2419996495968598203ULL ||
      generator.calls != 18) return 1;
  auto end = std::sample(values, values + 17, output, 8, generator);
  int expected[8]{0, 16, 8, 7, 15, 9, 3, 10};
  for (int i = 0; i < 8; ++i)
    if (output[i] != expected[i]) return 2;
  if (end != output + 8 || generator.state != 10940616297574655989ULL ||
      generator.calls != 36) return 3;
  return 0;
}
