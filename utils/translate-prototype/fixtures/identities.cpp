namespace example {
struct Pair { int first; unsigned int second; };
int score(int value) { return value + 1; }
unsigned int score(unsigned int value) { return value + 2u; }
int resolve() {
  Pair pair{score(3), score(4u)};
  return pair.first + static_cast<int>(pair.second);
}
}
