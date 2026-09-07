namespace arithmetic {
struct Pair { int first; unsigned int second; };

int add(int a, int b) { return a + b; }
unsigned int add(unsigned int a, unsigned int b) { return a + b; }

int factorial(int value) {
  if (value < 2)
    return 1;
  return value * factorial(value - 1);
}
}

int main() {
  int value = 1;
  value = value++ + 4;
  if (value != 5)
    return 1;

  bool skipped = false && (++value > 0);
  bool stopped = true || (++value > 0);
  if (skipped || !stopped || value != 5)
    return 2;

  arithmetic::Pair original{7, 4294967295u};
  arithmetic::Pair copied = original;
  copied.first = arithmetic::add(copied.first, 3);
  copied.second = arithmetic::add(copied.second, 2u);
  if (original.first != 7 || copied.first != 10 || copied.second != 1u)
    return 3;

  int total = 0;
  for (int outer = 0; outer < 4; ++outer) {
    for (int inner = 0; inner < 5; ++inner) {
      if (inner == 1)
        continue;
      if (inner == 4)
        break;
      total += outer + inner;
    }
  }
  if (total != 38 || arithmetic::factorial(6) != 720)
    return 4;
  return 0;
}
