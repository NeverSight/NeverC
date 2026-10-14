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
  int expected0[8]{2, 13, 14, 4, 11, 8, 1, 15};
  if (int result = check<std::minstd_rand0>(16807ULL, 4466006945371071924ULL, 491908712ULL,
      18056332760598948505ULL, expected0, 85944842ULL)) return 0+result;
  int expected1[8]{13, 3, 9, 12, 10, 15, 7, 4};
  if (int result = check<std::minstd_rand>(48271ULL, 5467402673509676114ULL, 1931632209ULL,
      16797411911568724633ULL, expected1, 1270684338ULL)) return 16+result;
  int expected2[8]{13, 3, 9, 12, 10, 15, 7, 4};
  if (int result = check<std::default_random_engine>(48271ULL, 5467402673509676114ULL, 1931632209ULL,
      16797411911568724633ULL, expected2, 1270684338ULL)) return 32+result;
  int expected3[8]{11, 7, 6, 14, 8, 2, 16, 9};
  if (int result = check<std::linear_congruential_engine<unsigned,1664525,1013904223,0>>(1015568748ULL, 13425925005586439917ULL, 1860345957ULL,
      18293149044577076889ULL, expected3, 628619503ULL)) return 48+result;
  int expected4[8]{0, 16, 8, 7, 15, 9, 3, 10};
  if (int result = check<std::linear_congruential_engine<unsigned long long,6364136223846793005ULL,1442695040888963407ULL,0>>(7806831264735756412ULL, 1723203945686548813ULL, 1834790053702311269ULL,
      6932542905646576793ULL, expected4, 9397566084216410720ULL)) return 64+result;
  int expected5[8]{5, 16, 14, 13, 8, 6, 1, 12};
  if (int result = check<std::linear_congruential_engine<unsigned short,25173,13849,65521>>(39022ULL, 9228724475232353539ULL, 46524ULL,
      16343214234032090777ULL, expected5, 62631ULL)) return 80+result;
  int expected6[8]{6, 14, 4, 10, 7, 16, 5, 13};
  if (int result = check<std::linear_congruential_engine<unsigned long long,48271,7,9223372036854775783ULL>>(48278ULL, 8451349782543073339ULL, 637942378840332727ULL,
      11501790280174896537ULL, expected6, 7909149427364075328ULL)) return 96+result;
  return 0;
}
