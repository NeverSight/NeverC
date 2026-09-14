int calls;
int seed(int n) { ++calls; return n; }
template<int N> inline int variable = seed(N);
template<int N> struct Class {
  inline static int value = seed(N);
  template<int M> inline static int member = seed(M);
};
template<int N> inline const int &reference = seed(N);
template<int N> struct Outside { static const int value; };
template<int N> const int Outside<N>::value = seed(N);
template<int N> inline int array[2] = {seed(N), seed(N + 1)};
template<class T> int partial = seed(10);
template<class T> int partial<T *> = seed(11);
template<> inline int variable<12> = seed(12);
template<int N> struct Record { int value; Record() : value(seed(N)) {} };
template<int N> inline Record<N> record;
int main() {
  if (calls != 12) return 1;
  if (variable<1> != 1 || variable<2> != 2 || variable<12> != 12) return 2;
  if (Class<3>::value != 3 || Class<3>::member<4> != 4) return 3;
  if (reference<5> != 5 || reference<6> != 6 || &reference<5> == &reference<6>) return 4;
  if (Outside<7>::value != 7 || array<8>[0] != 8 || array<8>[1] != 9) return 5;
  if (partial<int *> != 11 || record<13>.value != 13) return 6;
  int *one = &variable<1>, *same = &variable<1>, *other = &variable<2>;
  if (one != same || one == other || calls != 12) return 7;
  ++*one;
  if (variable<1> != 2 || variable<2> != 2 || calls != 12) return 8;
  return 0;
}
