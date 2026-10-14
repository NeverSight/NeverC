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
  int expected0[8]{6, 9, 8, 4, 10, 7, 2, 14};
  if (int result = check<std::independent_bits_engine<std::minstd_rand,1,unsigned>>(0ULL, 11652807124124053691ULL, 0ULL,
      4504918563141560473ULL, expected0, 1ULL)) return 0+result;
  int expected1[8]{12, 7, 16, 8, 15, 3, 4, 0};
  if (int result = check<std::independent_bits_engine<std::minstd_rand,32,unsigned>>(3163445217ULL, 17954691431264585136ULL, 504917779ULL,
      11436155110063564953ULL, expected1, 39890325ULL)) return 16+result;
  int expected2[8]{6, 10, 0, 7, 3, 4, 8, 9};
  if (int result = check<std::independent_bits_engine<std::mt19937,64,unsigned long long>>(15028999435905310454ULL, 11492198984645093024ULL, 9775444043821637277ULL,
      3474510638534463897ULL, expected2, 12484062493621003848ULL)) return 32+result;
  int expected3[8]{3, 12, 2, 13, 14, 6, 16, 9};
  if (int result = check<std::independent_bits_engine<std::ranlux24_base,16,unsigned short>>(31532ULL, 1799820156534981059ULL, 15573ULL,
      8078719198400699289ULL, expected3, 22778ULL)) return 48+result;
  int expected4[8]{16, 1, 13, 9, 12, 8, 6, 0};
  if (int result = check<std::independent_bits_engine<std::mt19937_64,64,unsigned long long>>(14514284786278117030ULL, 1137318967705930820ULL, 16283156750775118513ULL,
      14823925453341072281ULL, expected4, 9572388832724426598ULL)) return 64+result;
  int expected5[8]{16, 1, 13, 9, 12, 8, 6, 0};
  if (int result = check<std::independent_bits_engine<std::mt19937_64,8,unsigned>>(166ULL, 12797156585753081924ULL, 177ULL,
      14823925453341072281ULL, expected5, 102ULL)) return 80+result;
  return 0;
}
