static const char *captured;

__attribute__((noinline)) static void capture(const char *value) {
  captured = value;
}

__attribute__((noinline)) static int verify(void) {
  return captured[0] == 'i' ? 0 : 1;
}

int main(void) {
  capture("identity-secret");
  return verify();
}
