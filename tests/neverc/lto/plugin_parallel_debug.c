int lto_f0(unsigned x) { return x * 33u + 0u; }
int lto_f1(unsigned x) { return x * 33u + 1u; }
int lto_f2(unsigned x) { return x * 33u + 2u; }
int lto_f3(unsigned x) { return x * 33u + 3u; }
int lto_f4(unsigned x) { return x * 33u + 4u; }
int lto_f5(unsigned x) { return x * 33u + 5u; }
int lto_f6(unsigned x) { return x * 33u + 6u; }
int lto_f7(unsigned x) { return x * 33u + 7u; }
int lto_f8(unsigned x) { return x * 33u + 8u; }
int lto_f9(unsigned x) { return x * 33u + 9u; }
int lto_f10(unsigned x) { return x * 33u + 10u; }
int lto_f11(unsigned x) { return x * 33u + 11u; }
typedef int (*lto_fn)(unsigned);
volatile lto_fn lto_functions[12] = {
    lto_f0, lto_f1, lto_f2, lto_f3, lto_f4, lto_f5,
    lto_f6, lto_f7, lto_f8, lto_f9, lto_f10, lto_f11};
int main(void) {
  return lto_functions[0](1u) + lto_functions[1](2u) +
         lto_functions[2](3u) + lto_functions[3](4u) +
         lto_functions[4](5u) + lto_functions[5](6u) +
         lto_functions[6](7u) + lto_functions[7](8u) +
         lto_functions[8](9u) + lto_functions[9](10u) +
         lto_functions[10](11u) + lto_functions[11](12u);
}
