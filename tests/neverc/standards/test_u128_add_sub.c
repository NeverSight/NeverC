typedef unsigned long long U64;
typedef unsigned __int128 U128;
__attribute__((noinline)) U128 wide_add(U128 a, U128 b) { return a + b; }
__attribute__((noinline)) U128 wide_sub(U128 a, U128 b) { return a - b; }
struct Case { U64 al, ah, bl, bh, addl, addh, subl, subh; };
static volatile struct Case cases[] = {
  {0, 0, 0, 0, 0, 0, 0, 0},
  {~0ULL, 0, 1, 0, 0, 1, ~0ULL - 1, 0},
  {0, 1, 1, 0, 1, 1, ~0ULL, 0},
  {~0ULL, ~0ULL, 1, 0, 0, 0, ~0ULL - 1, ~0ULL},
  {1, 0, ~0ULL, ~0ULL, 0, 0, 2, 0},
  {0, 0, ~0ULL, ~0ULL, ~0ULL, ~0ULL, 1, 0}
};
int main(void) {
  for (unsigned i = 0; i != sizeof(cases) / sizeof(cases[0]); ++i) {
    U128 a = ((U128)cases[i].ah << 64) | cases[i].al;
    U128 b = ((U128)cases[i].bh << 64) | cases[i].bl;
    U128 sum = wide_add(a, b);
    U128 difference = wide_sub(a, b);
    if ((U64)sum != cases[i].addl || (U64)(sum >> 64) != cases[i].addh)
      return 10 + (int)i;
    if ((U64)difference != cases[i].subl || (U64)(difference >> 64) != cases[i].subh)
      return 20 + (int)i;
  }
  return 0;
}
