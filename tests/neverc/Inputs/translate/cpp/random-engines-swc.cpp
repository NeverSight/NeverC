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
  int expected0[8]{3, 12, 2, 13, 14, 6, 16, 9};
  if (int result = check<std::ranlux24_base>(15039276ULL, 3742952400530047427ULL, 10370261ULL,
      8078719198400699289ULL, expected0, 15554810ULL)) return 0+result;
  int expected1[8]{0, 4, 8, 9, 3, 16, 7, 2};
  if (int result = check<std::ranlux48_base>(23459059301164ULL, 6181760495208374989ULL, 86686680364760ULL,
      8901701390367228313ULL, expected1, 32084593267566ULL)) return 16+result;
  int expected2[8]{10, 4, 13, 2, 0, 5, 7, 9};
  if (int result = check<std::subtract_with_carry_engine<unsigned,32,3,7>>(3758475880ULL, 517745248815397476ULL, 873610545ULL,
      1890176634882336921ULL, expected2, 1067383538ULL)) return 32+result;
  int expected3[8]{5, 13, 11, 3, 2, 1, 12, 4};
  if (int result = check<std::subtract_with_carry_engine<unsigned long long,64,3,7>>(14132745644274366486ULL, 14545446689681653690ULL, 14338293668244360377ULL,
      18160816003219011993ULL, expected3, 8832358936021538219ULL)) return 48+result;
  int expected4[8]{0, 12, 1, 13, 9, 10, 3, 14};
  if (int result = check<std::subtract_with_carry_engine<unsigned,16,2,5>>(32000ULL, 12806804996137312704ULL, 3654ULL,
      7801068607320063641ULL, expected4, 43453ULL)) return 64+result;
  return 0;
}
