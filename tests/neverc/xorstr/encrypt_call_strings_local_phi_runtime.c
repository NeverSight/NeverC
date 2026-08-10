__attribute__((noinline)) static int consume(const char *value) {
  return value[0] == 'p' ? 0 : 1;
}

__attribute__((noinline)) static int check(int choose) {
  const char *value;
  if (choose)
    value = "phi-secret-alpha";
  else
    value = "phi-secret-omega";
  return consume(value);
}

int main(void) { return check(0) | check(1); }
