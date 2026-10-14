// Pinned expected sequences and algorithm results: LLVM 20.1.8 libc++.
#include <random>
#include <algorithm>
template<class E> int check(unsigned long long first,
                            unsigned long long expectedDigest,
                            unsigned long long expectedNext,
                            unsigned long long expectedShuffle,
                            const int *expectedSample,
                            unsigned long long expectedAfter) {
  E engine;
  if (engine() != first || E::min() >= E::max()) return 1;
  engine.seed(17);
  unsigned long long digest = 0;
  for (int i = 0; i < 10003; ++i) digest = digest * 257 + engine();
  if (digest != expectedDigest || engine() != expectedNext) return 2;
  E copied(engine);
  if (copied != engine) return 3;
  copied.discard(1300);
  for (int i = 0; i < 1300; ++i) engine();
  if (!(copied == engine)) return 4;
  E assigned;
  assigned = copied;
  if (assigned != copied || assigned() != copied()) return 5;
  engine.seed();
  if (engine() != first) return 6;
  E seeded(17);
  engine.seed(17);
  if (seeded != engine) return 7;
  int values[17], output[8]{};
  for (int i = 0; i < 17; ++i) values[i] = i;
  std::shuffle(values, values + 17, engine);
  digest = 0;
  for (int i = 0; i < 17; ++i) digest = digest * 257 + static_cast<unsigned>(values[i] + 1);
  if (digest != expectedShuffle) return 8;
  const int *input = values;
  if (std::sample(input, input + 17, output, 8, engine) != output + 8) return 9;
  for (int i = 0; i < 8; ++i) if (output[i] != expectedSample[i]) return 10;
  if (engine() != expectedAfter) return 11;
  return 0;
}
int main() {
  int expected0[8]{15, 2, 0, 3, 10, 7, 1, 16};
  if (int result = check<std::mt19937>(3499211612ULL, 6323637946781022192ULL, 1619580843ULL,
      2593415429809172633ULL, expected0, 4827110ULL)) return 0+result;
  int expected1[8]{16, 1, 13, 9, 12, 8, 6, 0};
  if (int result = check<std::mt19937_64>(14514284786278117030ULL, 1137318967705930820ULL, 16283156750775118513ULL,
      14823925453341072281ULL, expected1, 9572388832724426598ULL)) return 16+result;
  int expected2[8]{9, 3, 14, 1, 7, 4, 11, 8};
  if (int result = check<std::mersenne_twister_engine<unsigned,16,7,3,8,0x9908,5,0xffff,7,0x9d2c,9,0xefc6,4,181>>(25216ULL, 945443134923202125ULL, 19938ULL,
      13093235841917605017ULL, expected2, 34465ULL)) return 32+result;
  return 0;
}
