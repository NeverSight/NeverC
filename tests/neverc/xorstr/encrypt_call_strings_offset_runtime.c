__attribute__((noinline)) int starts_with_efix(const char *value) {
  volatile const char *observed = value;
  return observed[0] == 'e' && observed[1] == 'f' && observed[2] == 'i' &&
         observed[3] == 'x' && observed[4] == '\0';
}

int main(void) { return starts_with_efix("prefix" + 2) ? 0 : 1; }
