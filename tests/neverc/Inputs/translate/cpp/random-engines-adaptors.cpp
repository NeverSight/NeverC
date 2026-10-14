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
  int expected0[8]{3, 12, 2, 7, 13, 16, 1, 10};
  if (int result = check<std::ranlux24>(15039276ULL, 12186822786181601330ULL, 10749435ULL,
      8078719198400699289ULL, expected0, 4485281ULL)) return 0+result;
  int expected1[8]{4, 3, 7, 2, 15, 12, 13, 16};
  if (int result = check<std::ranlux48>(23459059301164ULL, 15269972990416581364ULL, 63187190503770ULL,
      3270659946023289497ULL, expected1, 57091018447349ULL)) return 16+result;
  int expected2[8]{8, 10, 11, 5, 16, 15, 7, 13};
  if (int result = check<std::knuth_b>(152607844ULL, 14162729635362984786ULL, 312551793ULL,
      10960659240532117145ULL, expected2, 2140309366ULL)) return 32+result;
  int expected3[8]{13, 3, 9, 12, 10, 15, 7, 4};
  if (int result = check<std::discard_block_engine<std::minstd_rand,5,5>>(48271ULL, 5467402673509676114ULL, 1931632209ULL,
      16797411911568724633ULL, expected3, 1270684338ULL)) return 48+result;
  int expected4[8]{15, 2, 0, 8, 6, 13, 10, 3};
  if (int result = check<std::discard_block_engine<std::mt19937,107,3>>(3499211612ULL, 2707181954856163965ULL, 3944237940ULL,
      17362649161214237849ULL, expected4, 3466988372ULL)) return 64+result;
  return 0;
}
