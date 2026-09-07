namespace example {
int pack(int left, int right) { return left * 10 + right; }
}

// C++17 sequences an assignment's RHS before its LHS. Function arguments
// are indeterminately sequenced; either 23 or 32 is permitted here.
extern "C" int sequencing_probe() {
  int value = 0;
  value = ++value + 1;
  int assigned = value;
  int arguments = example::pack(value++, value++);
  int after_arguments = value;
  bool skipped = false && ++value;
  bool taken = true || ++value;
  int shifted = value++ << value;
  return assigned * 1000000 + arguments * 10000 + after_arguments * 1000
       + shifted * 10 + value + skipped + taken;
}

extern "C" int deterministic_probe() {
  int value = 1;
  value = value++ + 1;
  int shifted = value++ << value;
  return shifted * 100 + value;
}
