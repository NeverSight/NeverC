__attribute__((noinline)) static int consume(const char *value,
                                             char expected) {
  return value[0] == expected ? 0 : 1;
}

__attribute__((noinline)) static int dynamic_offset(int choose, int offset) {
  const char *value = choose ? "dynamic-alpha" : "dynamic-omega";
  return consume(value + offset, offset == 4 ? 'm' : 'd');
}

__attribute__((noinline)) static int loop_phi(int count) {
  const char *value = "alpha-loop-secret";
  for (int i = 0; i < count; ++i)
    if (i & 1)
      value = "omega-loop-secret";
  return consume(value, count > 1 ? 'o' : 'a');
}

int main(void) {
  return dynamic_offset(0, 0) | dynamic_offset(1, 4) | loop_phi(1) |
         loop_phi(5);
}
