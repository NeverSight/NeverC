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
  int expected0[8]{2, 8, 1, 16, 10, 0, 7, 9};
  if (int result = check<std::shuffle_order_engine<std::minstd_rand,1>>(48271ULL, 2665794924303079840ULL, 225891546ULL,
      1081718938601194137ULL, expected0, 1270684338ULL)) return 0+result;
  int expected1[8]{4, 16, 12, 2, 5, 6, 3, 14};
  if (int result = check<std::shuffle_order_engine<std::mt19937_64,16>>(418970542659199878ULL, 13115327187459657902ULL, 8667234434805884074ULL,
      14698298326174584729ULL, expected1, 5069513275983861044ULL)) return 16+result;
  int expected2[8]{16, 13, 1, 15, 6, 8, 3, 14};
  if (int result = check<std::shuffle_order_engine<std::mt19937_64,17>>(418970542659199878ULL, 5234427391432486768ULL, 11412893727396524725ULL,
      1292796075172700825ULL, expected2, 7690250406789934918ULL)) return 32+result;
  int expected3[8]{10, 16, 0, 9, 14, 4, 2, 15};
  if (int result = check<std::shuffle_order_engine<std::linear_congruential_engine<unsigned long long,48271,7,9223372036854775783ULL>,17>>(6992449415213576259ULL, 18093954264976525542ULL, 4309343452995085490ULL,
      1045956336452949401ULL, expected3, 9149435519090554049ULL)) return 48+result;
  int expected4[8]{4, 12, 0, 13, 11, 16, 5, 3};
  if (int result = check<std::discard_block_engine<std::shuffle_order_engine<std::independent_bits_engine<std::ranlux24_base,32,unsigned>,7>,11,3>>(3887811768ULL, 5475488000956498112ULL, 528952015ULL,
      2474818065568497561ULL, expected4, 963341020ULL)) return 64+result;
  return 0;
}
