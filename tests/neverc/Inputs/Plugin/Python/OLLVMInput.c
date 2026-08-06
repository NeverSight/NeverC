int ollvm_transform(int x, int y) {
  int result = x + y;
  if ((x & 1) != 0)
    result ^= y;
  else
    result |= y;
  return result;
}

int main(void) {
  return ollvm_transform(7, 11) == 25 ? 0 : 1;
}
